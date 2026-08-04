/*
 * B_BMS_protect.c
 *
 * MCU 측 보호 감시자. 설계 원칙은 B_BMS_protect.h 참조.
 */

#include "B_BMS_protect.h"
#include "B_BMS.h"
#include "B_BMS_cmd.h"
#include "B_BMS_init.h"

BMS_ProtectState_t BMS_Protect = {0};

/* ---------------------------------------------------------------------------
 * 임계값
 *
 * 시간 기준은 ms가 아니라 "사이클 수"다. SLEEP/STOP1 진입 시 HAL_SuspendTick()이 SysTick을 꺼서
 * HAL_GetTick()으로는 실제 경과 시간을 잴 수 없기 때문이다(B_BMS_power_mode.c:42의 TODO와 동일한 사정).
 * 부하가 있을 때 루프 주기는 BMS_NORMAL_SCAN_PERIOD_MS(63 ms)로 고정되므로, 아래 값들은
 * 부하 상태 기준의 대략적인 시간이다.
 * ------------------------------------------------------------------------- */

/* 절연 경로(G3VM) 전파 + BQ 내부 반응을 기다리는 유예. 3사이클 ≈ 190 ms.
 * 참고: BOT의 CFETF/DFETF PF(임계 0.1 A / -0.5 A, 지연 5 s)가 같은 고장을 하드웨어에서 잡아준다.
 * 이 크로스체크는 그것보다 빠른 백업이므로 유예를 짧게 잡아도 된다. */
#define PATH_FAULT_CYCLES        3U

/* 연속 통신 실패 사이클. 5사이클 ≈ 315 ms */
#define COMM_FAULT_CYCLES        5U

/* 셀 전압 합 vs 스택 전압 괴리가 이만큼 이어지면 측정계 이상으로 본다 */
#define STACK_MISMATCH_CYCLES    10U

/* BOTHOFF 해제 전 "완전히 깨끗한" 상태를 유지해야 하는 사이클. 80사이클 ≈ 5 s */
#define CLEAN_CYCLES_TO_RELEASE  80U

/* BOTHOFF 해제 재시도 한도. 초과하면 latch */
#define BOTHOFF_RETRY_MAX        3U

/* latch형 보호(OCDL/SCDL) 복구 명령 재전송 간격. 80사이클 ≈ 5 s */
#define LATCH_RECOVER_INTERVAL   80U

/* 소프트 백업 임계 — BQ 임계보다 반드시 여유를 두어야 BQ가 먼저 동작한다.
 * BQ: CUV 2.53 V / COV 4.1998 V / OTD 65 degC (B_BMS_init.c) */
#define SOFT_CELL_UV_mV        2400U
#define SOFT_CELL_OV_mV        4250U
#define SOFT_TEMP_MAX_C          70.0f

/* 소프트 백업은 "BQ가 놓쳤다"를 판정하는 것이므로, BQ의 보호 지연보다 반드시 길게 기다려야 한다.
 * 그러지 않으면 BQ가 정상적으로 지연을 소화하는 동안 MCU가 먼저 끊어버리는 오탐이 난다.
 *   CUV/COV Delay = 1 s   -> 32사이클 ≈ 2 s
 *   OTD/OTC/OTF/SOT Delay = 5 s -> 110사이클 ≈ 7 s */
#define SOFT_CELLV_FAULT_CYCLES  32U
#define SOFT_TEMP_FAULT_CYCLES  110U

/* 셀 전압 읽기가 "그럴듯한가" 판정 범위. 0 = 아직 안 읽음 */
#define CELLV_PLAUSIBLE_MIN_mV  100U
#define CELLV_PLAUSIBLE_MAX_mV 5000U

/* 셀 합과 스택 전압의 허용 오차 (16셀 x 개별 오차 + 여유) */
#define STACK_SUM_TOLERANCE_mV 1500U

/* 소프트 백업/스택 체크를 시작하기 전에 최소한 이만큼은 정상적으로 돌아야 한다 */
#define WARMUP_CYCLES            5U

static uint16_t latch_recover_timer = 0;

/* ---------------------------------------------------------------------------
 * 기대 상태 계산
 *
 * B_BMS_init.h의 CHG/DSGFET_MASK_*는 B_BMS_init.c가 칩에 쓰는 바로 그 값이다.
 * SafetyStatus A/B/C에 AND를 걸면 "이 칩 기준으로 지금 CHG(또는 DSG)가 꺼져 있어야 하는가"가 나온다.
 *
 * PF는 ProtectionConfiguration(0x0602)의 "PF 발생 시 FET 차단" 설정에 의해 CHG/DSG를 모두 끄므로
 * 별도로 OR 한다.
 * ------------------------------------------------------------------------- */
static uint8_t chg_should_be_off(const BMS_Unit *u)
{
    return ((u->value_SafetyStatusA & CHGFET_MASK_A) ||
            (u->value_SafetyStatusB & CHGFET_MASK_B) ||
            (u->value_SafetyStatusC & CHGFET_MASK_C) ||
            u->PF_ProtectionsTriggered) ? 1U : 0U;
}

static uint8_t dsg_should_be_off(const BMS_Unit *u)
{
    return ((u->value_SafetyStatusA & DSGFET_MASK_A) ||
            (u->value_SafetyStatusB & DSGFET_MASK_B) ||
            (u->value_SafetyStatusC & DSGFET_MASK_C) ||
            u->PF_ProtectionsTriggered) ? 1U : 0U;
}

static uint8_t cellv_is_plausible(uint16_t mv)
{
    return (mv >= CELLV_PLAUSIBLE_MIN_mV && mv <= CELLV_PLAUSIBLE_MAX_mV) ? 1U : 0U;
}

static uint8_t temp_is_plausible(float c)
{
    return (c > -40.0f && c < 130.0f) ? 1U : 0U;
}

/* 셀 전압 합 vs 스택 전압.
 *
 * 주의: Pack_Voltage는 쓰지 않는다. 스택형 구조에서 각 보드의 PACK 핀이 무엇을 보는지가 회로 의존적이라
 * "TOP + BOT = PACK"을 가정할 수 없기 때문이다. 대신 각 보드 안에서만 자기완결적으로 비교한다.
 *
 * 주의 2: Stack_Voltage가 16S에서 1 mV 단위라면 67 V = 67200 > uint16 범위를 넘는다.
 * (B_BMS.c의 stack_userV_to_mV() 주석 참조 — DAConfiguration=0x02 = 1 mV 단위로 되어 있음)
 * 실측으로 확정되기 전까지 오탐을 내지 않도록, 값이 그럴듯하지 않으면 이 검사를 건너뛴다. */
static uint8_t stack_sum_mismatch(const BMS_Unit *u)
{
    uint32_t sum = 0;

    for (int i = 0; i < 16; i++) {
        if (!cellv_is_plausible(u->CellVoltage[i])) {
            return 0U;   /* 아직 다 못 읽음 - 판정 보류 */
        }
        sum += u->CellVoltage[i];
    }

    /* 스택 전압이 셀 합과 자릿수부터 다르면 단위/오버플로 문제이지 측정계 고장이 아니다 */
    if (u->Stack_Voltage < (sum / 2U) || u->Stack_Voltage > (sum * 2U)) {
        return 0U;
    }

    uint32_t diff = (u->Stack_Voltage > sum) ? (u->Stack_Voltage - sum) : (sum - u->Stack_Voltage);
    return (diff > STACK_SUM_TOLERANCE_mV) ? 1U : 0U;
}

static uint8_t soft_cellv_fault(const BMS_Unit *u)
{
    for (int i = 0; i < 16; i++) {
        uint16_t mv = u->CellVoltage[i];
        if (!cellv_is_plausible(mv)) {
            continue;
        }
        if (mv <= SOFT_CELL_UV_mV || mv >= SOFT_CELL_OV_mV) {
            return 1U;
        }
    }
    return 0U;
}

static uint8_t soft_temp_fault(const BMS_Unit *u)
{
    const float temps[] = { u->CELL_Temp, u->Max_Cell_Temp, u->FET_Temp, u->Int_Temp };

    for (uint32_t i = 0; i < (sizeof(temps) / sizeof(temps[0])); i++) {
        if (temp_is_plausible(temps[i]) && temps[i] >= SOFT_TEMP_MAX_C) {
            return 1U;
        }
    }
    return 0U;
}

/* latch형 보호는 자동 복구가 안 된다. 조건이 정리되면 명시적으로 복구 명령을 보내야 하며,
 * 보내지 않으면 한 번 걸린 뒤 영구 락아웃된다.
 * 전류 관련 보호이므로 대상은 BOT뿐이다(TOP은 SRP/SRN 미사용). */
static void recover_latched_protections(void)
{
    BMS_Unit *bot = &BMS[BOT];

    if (bot->OCDL_Fault == 0U && bot->SCDL_Fault == 0U) {
        latch_recover_timer = 0;
        return;
    }

    /* 다른 보호가 아직 살아 있으면 복구를 시도하지 않는다 */
    if (bot->PF_ProtectionsTriggered || BMS[TOP].PF_ProtectionsTriggered) {
        return;
    }
    if (bot->comm_fail_cycles != 0U) {
        return;
    }

    if (latch_recover_timer > 0U) {
        latch_recover_timer--;
        return;
    }
    latch_recover_timer = LATCH_RECOVER_INTERVAL;

    if (bot->OCDL_Fault) {
        CommandSubcommands(bot, OCDL_RECOVER);
    }
    if (bot->SCDL_Fault) {
        CommandSubcommands(bot, SCDL_RECOVER);
    }
}

static void assert_bothoff(BMS_TripReason_t reason)
{
    BMS_Protect.reason = reason;
    BMS_Protect.clean_cycles = 0U;

    if (BMS_Protect.bothoff_asserted == 0U) {
        BQ769x2_BOTHOFF();
        BMS_Protect.bothoff_asserted = 1U;
    }

    /* PF와 재시도 초과는 자동 해제 금지 */
    if (reason == BMS_TRIP_PF || reason == BMS_TRIP_RETRY_EXHAUSTED) {
        BMS_Protect.latched = 1U;
    }
}

void BMS_Protect_Init(void)
{
    BMS_ProtectState_t cleared = {0};
    BMS_Protect = cleared;
    latch_recover_timer = 0;

    /* LV_BMS_MAIN_RUN()은 양쪽 칩이 응답할 때만 BOTHOFF를 해제한다. 실제 핀 상태를 읽어와
     * 감시자의 내부 상태와 어긋나지 않게 맞춘다(오픈드레인이라 IDR이 실제 라인 레벨을 반영한다). */
    if (HAL_GPIO_ReadPin(BOT_DFETOFF_GPIO_Port, BOT_DFETOFF_Pin) == GPIO_PIN_SET) {
        BMS_Protect.bothoff_asserted = 1U;
        BMS_Protect.reason = BMS_TRIP_INIT;
    }
}

void BMS_Protect_Update(void)
{
    BMS_Unit *top = &BMS[TOP];
    BMS_Unit *bot = &BMS[BOT];

    /* -------- 1. 기대 상태 / 실측 상태 -------- */
    uint8_t chg_exp_off = (chg_should_be_off(top) || chg_should_be_off(bot)) ? 1U : 0U;
    uint8_t dsg_exp_off = (dsg_should_be_off(top) || dsg_should_be_off(bot)) ? 1U : 0U;

    /* FET 상태는 TOP만 유효 (BMS_FET_BOARD). TOP과 통신이 안 되면 실측값이 낡은 값이므로 판정 보류 */
    uint8_t fet_reading_valid = (top->comm_fail_this_cycle == 0U) ? 1U : 0U;

    BMS_Protect.chg_expected_off = chg_exp_off;
    BMS_Protect.dsg_expected_off = dsg_exp_off;
    BMS_Protect.chg_observed_on = top->CHG;
    BMS_Protect.dsg_observed_on = top->DSG;

    /* -------- 2. 크로스체크: 차단 경로 고장 --------
     * 기대 = OFF 인데 TOP의 FET이 계속 켜져 있다 = BOT.DCHG/DDSG → G3VM → TOP.CFETOFF/DFETOFF
     * 경로 어딘가가 끊겼거나, TOP의 핀 설정이 잘못됐다는 뜻.
     *
     * MCU가 BOTHOFF를 건 동안에는 기대 상태가 안전 상태에서 나온 게 아니므로 판정하지 않는다. */
    if (fet_reading_valid && BMS_Protect.bothoff_asserted == 0U) {
        BMS_Protect.chg_mismatch_cycles =
            (chg_exp_off && top->CHG) ? (uint16_t)(BMS_Protect.chg_mismatch_cycles + 1U) : 0U;
        BMS_Protect.dsg_mismatch_cycles =
            (dsg_exp_off && top->DSG) ? (uint16_t)(BMS_Protect.dsg_mismatch_cycles + 1U) : 0U;

        /* 역방향(기대=ON, 실측=OFF)은 이미 안전한 상태다. 차지펌프/게이트 드라이버 이상을 의심할
         * 근거는 되지만 액션은 취하지 않고 카운트만 남긴다. */
        BMS_Protect.stuck_off_cycles =
            ((!chg_exp_off && !top->CHG) || (!dsg_exp_off && !top->DSG))
                ? (uint16_t)(BMS_Protect.stuck_off_cycles + 1U) : 0U;
    } else {
        BMS_Protect.chg_mismatch_cycles = 0U;
        BMS_Protect.dsg_mismatch_cycles = 0U;
    }

    /* -------- 3. 스택 측정계 크로스체크 -------- */
    if (LV_BMS_running > WARMUP_CYCLES &&
        (stack_sum_mismatch(top) || stack_sum_mismatch(bot))) {
        if (BMS_Protect.stack_mismatch_cycles < UINT16_MAX) {
            BMS_Protect.stack_mismatch_cycles++;
        }
    } else {
        BMS_Protect.stack_mismatch_cycles = 0U;
    }

    /* -------- 3b. 소프트 백업 누적 --------
     * BQ가 이미 무언가를 껐다면(chg/dsg_exp_off) 백업이 개입할 이유가 없으므로 카운터를 지운다. */
    if (LV_BMS_running > WARMUP_CYCLES && !chg_exp_off && !dsg_exp_off) {
        BMS_Protect.soft_cellv_cycles =
            (soft_cellv_fault(top) || soft_cellv_fault(bot))
                ? (uint16_t)(BMS_Protect.soft_cellv_cycles + 1U) : 0U;
        BMS_Protect.soft_temp_cycles =
            (soft_temp_fault(top) || soft_temp_fault(bot))
                ? (uint16_t)(BMS_Protect.soft_temp_cycles + 1U) : 0U;
    } else {
        BMS_Protect.soft_cellv_cycles = 0U;
        BMS_Protect.soft_temp_cycles = 0U;
    }

    /* -------- 4. 트립 판정 (위쪽이 우선순위 높음) -------- */
    BMS_TripReason_t trip = BMS_TRIP_NONE;

    if (top->PF_ProtectionsTriggered || bot->PF_ProtectionsTriggered) {
        trip = BMS_TRIP_PF;
    } else if (top->comm_fail_cycles >= COMM_FAULT_CYCLES ||
               bot->comm_fail_cycles >= COMM_FAULT_CYCLES) {
        /* 상태를 못 읽는 것을 "정상"으로 가정하면 안 된다 */
        trip = BMS_TRIP_COMM;
    } else if (BMS_Protect.chg_mismatch_cycles >= PATH_FAULT_CYCLES) {
        trip = BMS_TRIP_CHG_PATH;
    } else if (BMS_Protect.dsg_mismatch_cycles >= PATH_FAULT_CYCLES) {
        trip = BMS_TRIP_DSG_PATH;
    } else if (BMS_Protect.stack_mismatch_cycles >= STACK_MISMATCH_CYCLES) {
        trip = BMS_TRIP_STACK_MISMATCH;
    } else if (BMS_Protect.soft_cellv_cycles >= SOFT_CELLV_FAULT_CYCLES) {
        /* 소프트 백업: BQ가 지연을 다 소화하고도 아무것도 안 껐는데 MCU가 본 값은 한계를 넘은 경우 */
        trip = BMS_TRIP_SOFT_CELLV;
    } else if (BMS_Protect.soft_temp_cycles >= SOFT_TEMP_FAULT_CYCLES) {
        trip = BMS_TRIP_SOFT_TEMP;
    }

    /* -------- 5. 상태기계 -------- */
    if (trip != BMS_TRIP_NONE) {
        assert_bothoff(trip);
    } else {
        if (BMS_Protect.clean_cycles < UINT16_MAX) {
            BMS_Protect.clean_cycles++;
        }

        if (BMS_Protect.bothoff_asserted && !BMS_Protect.latched &&
            BMS_Protect.clean_cycles >= CLEAN_CYCLES_TO_RELEASE) {

            if (BMS_Protect.retry_count >= BOTHOFF_RETRY_MAX) {
                /* 계속 되풀이된다 = 간헐적 고장. 사람이 볼 때까지 열어둔다 */
                assert_bothoff(BMS_TRIP_RETRY_EXHAUSTED);
            } else {
                BMS_Protect.retry_count++;
                BQ769x2_BOTHOFF_RESET();
                BMS_Protect.bothoff_asserted = 0U;
                BMS_Protect.reason = BMS_TRIP_NONE;
            }
        } else if (BMS_Protect.bothoff_asserted == 0U) {
            BMS_Protect.reason = BMS_TRIP_NONE;
        }
    }

    /* -------- 6. latch형 보호 복구 -------- */
    recover_latched_protections();
}

uint8_t BMS_Protect_IsFaulted(void)
{
    return (BMS_Protect.bothoff_asserted || BMS_Protect.latched ||
            BMS_Protect.reason != BMS_TRIP_NONE) ? 1U : 0U;
}
