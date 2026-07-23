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

#define BMS_PACK_CAPACITY_mAh 26000.0f              // 팩 전체 용량 26Ah (30S4P, Molicel INR-21700-M65A 6500mAh/cell(typical) x 4P)
#define BMS_SOC_OCV_REST_MS (10UL * 60UL * 1000UL)  // 10분 이상 무부하 지속 시 셀 전압이 OCV에 근접했다고 보고 재보정

typedef struct {
    uint16_t ocv_mV;
    uint16_t soc_permille;
} SOC_OCVPoint;

// Molicel INR-21700-M65A 데이터시트(datasheets/(몰리셀)2025-Product-Data-Sheet-of-INR-21700-M65A...pdf)의
// "Discharge Rate Characteristics" 그래프 중 가장 낮은 방전율(1.30A, 약 0.2C, OCV에 가장 근접) 커브를
// 눈금 단위로 읽어 SOC 10% 간격으로 옮긴 값 (셀 1개 기준, mV). 데이터시트가 표가 아닌 그래프로만 제공되어
// 그래프 판독 기반 근사치이며, 실측/정밀 디지타이징 데이터가 있으면 교체할 것.
// Nominal 3.6V, Charge 4.2V, Discharge cut-off 2.5V (데이터시트 CELL CHARACTERISTICS 표 기준)
static const SOC_OCVPoint ocv_table[] = {
    {2500,    0},
    {3050,   50},
    {3280,  100},
    {3480,  200},
    {3580,  300},
    {3640,  400},
    {3680,  500},
    {3720,  600},
    {3780,  700},
    {3850,  800},
    {3970,  900},
    {4110,  950},
    {4200, 1000},
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
// Battery_Voltage_Sum은 DASTATUS5()의 "userV" 단위 raw 값(TRM Table 12-23, p.119)이며
// DAConfiguration[USER_VOLTS_CV]를 따른다(TRM Table 13-15) — Stack/Pack/LD Voltage와 동일한 근거.
// B_BMS_init.c의 BQ769x2_Init()은 TOP/BOT 분기 없이 DAConfiguration=0x02(USER_VOLTS_CV=0/밀리볼트)를
// 양쪽 보드 모두에 쓰므로 둘 다 이미 mV 단위이며 ×10을 적용하면 안 됨
// (Core/Src/B_BMS.c의 stack_userV_to_mV()와 동일한 근거, issue #27에서 발견된 TOP ×10 과대보고 버그 수정).
static float average_cell_voltage_mV(void)
{
    float top_avg = (float)BMS[TOP].Battery_Voltage_Sum / 16.0f;
    float bot_avg = (float)BMS[BOT].Battery_Voltage_Sum / 16.0f;

    return (top_avg + bot_avg) / 2.0f;
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
    BMS_Unit *current_unit = &BMS[BOT]; // BMS_CURRENT_BOARD: 전류/쿨롱카운터 데이터는 BOT에서만 유효
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
