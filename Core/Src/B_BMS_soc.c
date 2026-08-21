/*
 * B_BMS_soc.c
 *
 * Pack-level SOC estimation: coulomb counting against the BQ769x2's
 * built-in CC2-based accumulated-charge integrator (DASTATUS6), periodically
 * re-anchored to an OCV lookup once the pack has rested long enough for
 * cell voltages to relax back toward open-circuit. This corrects the
 * long-term drift that pure coulomb counting accumulates over a multi-day
 * race without needing a full current*dt integration in the MCU.
 */
#include "B_BMS.h"
#include "B_BMS_soc.h"
#include "B_BMS_power_mode.h"

// 셀: Samsung SDI INR21700-50S
// datasheets 폴더 밖이지만 KUST/datasheet/(삼성셀) Samsung-INR21700-50S datasheet.pdf 기준.
//   3.1 Standard discharge capacity : Typ. 5,000 mAh (0.2C/1A 방전, 2.5V 컷오프)
//   3.2 Rated discharge capacity    : Min. 4,800 mAh (10A 방전)
//   3.3 Nominal voltage             : 3.6 V
//   3.5 Rated charge                : CCCV 6A, 4.20 V
//   3.9 Discharge cut-off           : 2.5 V
//
// 팩 구성: 32S5P (확정)
//   - 32S = 칩당 16S x BQ76972 2개. BMS 하드웨어 구성과 일치한다.
//   - 최대 팩 전압 32 x 4.20 V = 134.4 V, 공칭 32 x 3.6 V = 115.2 V
//     -> 2027WSC 보고서 5장의 FET 선정 근거(Vds 200 V, SOA를 140 V 기준으로 계산)와 맞는다.
//   - 칩당 스택 전압 16 x 4.20 V = 67.2 V
//     -> B_BMS_init.c의 SleepChargerVoltageThreshold(67.2 V) 설정과 맞는다.
//   - 팩 용량 5,000 mAh x 5P = 25,000 mAh
//
// 용량은 표준 방전 용량(Typ. 5,000 mAh)을 쓴다. 정격 용량(Min. 4,800 mAh)을 쓰면 SOC가 보수적으로
// (실제보다 높게) 나오는데, 쿨롱 카운팅 분모라 주행 가능 거리 추정을 낙관적으로 만들 수 있다.
// 셀 실측 용량이 나오면 BMS_CELL_CAPACITY_mAh를 그 값으로 교체할 것.
#define BMS_CELL_CAPACITY_mAh   5000.0f             // INR21700-50S 표준 방전 용량 (Typ.)
#define BMS_PACK_PARALLEL_COUNT 5.0f                // 32S5P 확정
#define BMS_PACK_CAPACITY_mAh (BMS_CELL_CAPACITY_mAh * BMS_PACK_PARALLEL_COUNT)  // 25,000 mAh

#define BMS_SOC_OCV_REST_MS (10UL * 60UL * 1000UL)  // 10분 이상 무부하 지속 시 셀 전압이 OCV에 근접했다고 보고 재보정

typedef struct {
    uint16_t ocv_mV;
    uint16_t soc_permille;
} SOC_OCVPoint;

// Samsung SDI INR21700-50S OCV -> SOC 근사 테이블 (셀 1개 기준, mV).
//
// >>> 출처와 한계를 반드시 읽을 것 <<<
//
// 50S 데이터시트에는 방전 전압 곡선 그래프가 없다. 7.8/7.9 "rate capabilities"는 전압 곡선이 아니라
// 용량 백분율 표이고, 수록된 그림(Fig.1/Fig.2)은 치수도와 포장도다. 즉 이전 Molicel M65A 테이블처럼
// "데이터시트 그래프를 눈금으로 읽는" 방법을 쓸 수 없었다.
//
// 따라서 아래 값은 데이터시트에서 확인 가능한 세 점
//     4.20 V(만충) / 3.6 V(공칭, 평균 방전 전압) / 2.5 V(방전 컷오프)
// 에 일반적인 고출력 NMC 셀의 OCV 형상을 맞춘 근사치다. 끝점 두 개(0%, 100%)만 데이터시트 확정값이고,
// 중간 구간의 형상은 측정값이 아니다.
//
// 영향 범위: 이 테이블은 (1) 부팅 직후 SOC 시딩, (2) 10분 이상 무부하 후 재보정에만 쓰인다.
// 주행 중 SOC는 쿨롱 카운팅이 담당하므로 중간 구간 오차가 즉시 주행에 반영되지는 않지만,
// 장시간 정차 후 표시 SOC가 튈 수 있다.
//
// 교체 방법: 만충 후 0.05C 이하로 방전하며 10% 간격으로 1시간씩 쉬게 한 뒤 개방전압을 재는 것이
// 가장 정확하다. 실측 데이터가 나오면 아래 값을 그대로 갈아끼우면 된다.
// (HARDWARE_VERIFICATION.md 3-1 항목 참조)
static const SOC_OCVPoint ocv_table[] = {
    {2500,    0},   // 데이터시트 3.9 방전 컷오프
    {3080,   50},
    {3320,  100},
    {3450,  200},
    {3530,  300},
    {3590,  400},   // 이 부근이 데이터시트 3.3 공칭 3.6 V 구간
    {3640,  500},
    {3710,  600},
    {3790,  700},
    {3880,  800},
    {3990,  900},
    {4100,  950},
    {4200, 1000},   // 데이터시트 3.5 정격 충전 전압
};
#define OCV_TABLE_LEN (sizeof(ocv_table) / sizeof(ocv_table[0]))

static uint8_t soc_initialized = 0;
static float soc_permille_f = 0.0f;
static float last_accum_charge_mAh = 0.0f;

static uint16_t lookup_ocv_permille(float ocv_mV)
{
    if (ocv_mV <= (float)ocv_table[0].ocv_mV) {
        return ocv_table[0].soc_permille;
    }
    if (ocv_mV >= (float)ocv_table[OCV_TABLE_LEN - 1U].ocv_mV) {
        return ocv_table[OCV_TABLE_LEN - 1U].soc_permille;
    }

    for (uint32_t i = 0; i < (OCV_TABLE_LEN - 1U); i++) {
        if (ocv_mV >= (float)ocv_table[i].ocv_mV && ocv_mV <= (float)ocv_table[i + 1U].ocv_mV) {
            float span_mV = (float)(ocv_table[i + 1U].ocv_mV - ocv_table[i].ocv_mV);
            float span_permille = (float)(ocv_table[i + 1U].soc_permille - ocv_table[i].soc_permille);
            float frac = (ocv_mV - (float)ocv_table[i].ocv_mV) / span_mV;
            return (uint16_t)((float)ocv_table[i].soc_permille + (frac * span_permille));
        }
    }

    return 500U;
}

// TOP/BOT 각 16셀 평균 전압을 다시 평균 -> 팩 내 셀 1개의 대표 전압(mV)
//
// Battery_Voltage_Sum(DASTATUS5 바이트 8-9)의 단위는 userV다 — TRM Table 12-23(SLUUCW9 p.118)의
// 서브커맨드 상세 표 기준이며, 같은 표가 "This can be compared to the Stack Voltage()"라고 명시해
// Stack Voltage와 동일 단위임을 뒷받침한다.
// (주의: 개요 장의 Table 4-5(p.24)는 같은 필드를 cV로 표기해 서로 어긋난다. 상세 표를 따랐다.
//  어느 쪽이든 DAConfiguration=0x06에서는 10mV 단위라 아래 ×10은 동일하다.)
//
// 즉 이 값은 Stack_Voltage와 마찬가지로 DAConfiguration[USER_VOLTS_CV]에 묶여 있다.
// B_BMS.c의 stack_userV_to_mV()와 항상 같이 움직여야 한다.
//
// 이전 코드는 ×10 없이 그대로 mV로 취급했는데, 그때 DAConfiguration=0x02(밀리볼트)라 16S 59.2 V가
// 부호있는 16비트에서 32767로 포화됐고 -> /16 = 2048 mV -> OCV 테이블 하한(2500 mV) 미만이 되어
// lookup_ocv_permille()이 0을 반환했다. 즉 OCV 재보정 경로를 탈 때마다 SOC가 0%로 고정됐다.
// BMS_SOC_Init() 직후 첫 갱신과 10분 무부하 이후가 모두 이 경로다.
static float average_cell_voltage_mV(void)
{
    /* 장착된 유닛만 평균한다. 고정으로 2로 나누면 유닛이 하나일 때 평균이 절반이 되어
     * OCV 테이블 하한 아래로 떨어지고 SOC가 0으로 고정된다. */
    float sum = 0.0f;
    uint8_t n = 0U;

    for (int i = 0; i < STACK; i++) {
        if (!BMS_UNIT_USED(i)) { continue; }
        sum += ((float)BMS[i].Battery_Voltage_Sum * 10.0f) / 16.0f;
        n++;
    }

    return (n > 0U) ? (sum / (float)n) : 0.0f;
}

// AccumulatedCharge_Int(정수부) + Frac(32비트 고정소수 분수부)는 DASTATUS6()의 userAh 단위 raw 값(TRM Table 4-6).
// DAConfiguration=0x02로 USER_AMPS[1:0]=Centiamp(10mA)가 선택되어 있어(TRM Table 13-15, B_BMS_init.c:46)
// 1 userAh = 10mAh이므로 ×10 스케일 필요
static float accumulated_charge_mAh(const BMS_Unit *unit)
{
    float raw_userAh = (float)unit->AccumulatedCharge_Int + ((float)unit->AccumulatedCharge_Frac / 4294967296.0f);
    return raw_userAh * 10.0f;
}

void BMS_SOC_Init(void)
{
    soc_initialized = 0;
    soc_permille_f = 0.0f;
    last_accum_charge_mAh = 0.0f;
}

void BMS_SOC_Update(void)
{
    BMS_Unit *current_unit = &BMS[BMS_CURRENT_BOARD]; // 전류/쿨롱카운터는 션트가 달린 보드에서만 유효
    float accum_mAh = accumulated_charge_mAh(current_unit);

    if (!soc_initialized) {
        soc_permille_f = (float)lookup_ocv_permille(average_cell_voltage_mV());
        soc_initialized = 1;
    } else {
        float delta_mAh = accum_mAh - last_accum_charge_mAh;
        soc_permille_f += (delta_mAh / BMS_PACK_CAPACITY_mAh) * 1000.0f;

        if (no_load_time_ms >= BMS_SOC_OCV_REST_MS) {
            soc_permille_f = (float)lookup_ocv_permille(average_cell_voltage_mV());
        }
    }

    last_accum_charge_mAh = accum_mAh;

    if (soc_permille_f > 1000.0f) {
        soc_permille_f = 1000.0f;
    } else if (soc_permille_f < 0.0f) {
        soc_permille_f = 0.0f;
    }

    current_unit->SOC_Permille = (uint16_t)soc_permille_f;
}
