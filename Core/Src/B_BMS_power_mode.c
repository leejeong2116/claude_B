/*
 * B_BMS_power_mode.c
 *
 *  Created on: May 6, 2026
 *      Author: user
 */

#include "B_BMS_power_mode.h"
#include "B_BMS.h"
#include "B_BMS_cmd.h"
#include "B_BMS_protect.h"
#include "B_BMS_rtc.h"

#define BMS_NORMAL_SCAN_PERIOD_MS 63U //normal / 63ms scan
#define BMS_SLEEP_SCAN_PERIOD_MS 5000U // sleep / 5s scan

// 저전력 중 주기적으로 깨어나는 간격.
// BQ의 HWDDelay(B_BMS_init.c, 60초)보다 반드시 짧아야 한다 — 깨어나서 BQ를 읽는 것이
// 호스트 워치독을 리셋하는 동작이기 때문이다. 여유를 두어 절반으로 잡았다.
#define BMS_WAKEUP_PERIOD_S 30U
#define TIME_1_HOUR_MS 3600000UL
#define TIME_3_DAYS_MS 259200000UL
#define BMS_BATTERY_STATUS_SLEEP_BIT 0x8000U

uint32_t no_load_time_ms = 0;

volatile WakeupReason_t wakeup_reason = WAKEUP_NONE;
extern BMS_Unit BMS[STACK];

// "무부하" 판정 전류. 단위는 10 mA (DAConfiguration 의 USER_AMPS = centiamp).
//
// 이전 값은 1000 = 10 A 였는데, BQ 쪽 SleepCurrent 설정(B_BMS_init.c, 1000 mA = 1 A)과 10배 어긋난다.
// 32S5P 팩에서 10 A 는 약 1.15 kW 로 주행 중에도 나올 수 있는 전류라, MCU가 "주차 중"으로 보고
// 슬립 시퀀스를 시작하는 동안 BQ는 여전히 깨어 있는 상태가 된다.
//
// 두 판정이 같은 기준을 보도록 BQ의 SleepCurrent 와 맞췄다: 1 A = 100 (10 mA 단위).
// BQ 설정을 바꾸면 이 값도 같이 바꿀 것.
#define BMS_NO_LOAD_CURRENT_10mA 100

bool Is_No_Load(void)
{
    int16_t current = BMS[BOT].Pack_Current;

    return (current > -BMS_NO_LOAD_CURRENT_10mA && current < BMS_NO_LOAD_CURRENT_10mA);
}
/*
void Configure_BMS_Sleep_Mask(void)
{
    DirectCommands(&BMS[TOP], AlarmEnable, 0xF882, W);
    DirectCommands(&BMS[BOT], AlarmEnable, 0xF882, W);
}

void Configure_BMS_Normal_Mask(void)
{
    DirectCommands(&BMS[TOP], AlarmEnable, 0xF882, W);
    DirectCommands(&BMS[BOT], AlarmEnable, 0xF882, W);
}
*/
// 이제 elapsed_ms는 RTC로 실측한 값이다 (B_BMS_rtc.c). RTC를 못 세운 경우에만 예전처럼
// 가정된 스캔 주기를 더하는 폴백으로 동작한다 — 그 경우 1시간/3일 임계는 실제 시간과 어긋난다.
static void Accumulate_No_Load_Time(uint32_t elapsed_ms)
{
    if (no_load_time_ms <= (UINT32_MAX - elapsed_ms)) {
        no_load_time_ms += elapsed_ms;
    } else {
        no_load_time_ms = UINT32_MAX;
    }
}


void Enter_Sleep_Sequence(void)
{
    bool protections_triggered = (BMS[BOT].ProtectionsTriggered != 0U) ||
                                  (BMS[TOP].ProtectionsTriggered != 0U);

    // MCU 감시자가 개입한 상태에서는 절대로 자면 안 된다.
    // 예: 통신 두절로 BOTHOFF를 건 경우 ProtectionsTriggered는 (읽을 수 없으니) 0이고
    //     Pack_Current도 낡은 0이라 Is_No_Load()가 참이 되어, 감시자가 복구 판정을 못 하는 채로
    //     MCU가 잠들어 버린다.
    bool mcu_protect_active = (BMS_Protect_IsFaulted() != 0U);

    if (!Is_No_Load() || protections_triggered || mcu_protect_active) {
        no_load_time_ms = 0;
        // 부하가 있을 때도 루프 주기를 일정하게 유지 (I2C/CAN이 풀 스피드로 폭주하지 않도록)
        HAL_Delay(BMS_NORMAL_SCAN_PERIOD_MS);
        return;
    }
    bool entered_stop_mode = false;
    bool is_bot_sleep = ((BMS[BOT].BattStat & BMS_BATTERY_STATUS_SLEEP_BIT) != 0U);
    bool is_top_sleep = ((BMS[TOP].BattStat & BMS_BATTERY_STATUS_SLEEP_BIT) != 0U);

    // RTC가 있으면 시간 누적은 아래에서 실측값으로 한다. 여기서 상수를 더하면 이중 계상이 된다.
    bool use_rtc_time = (BMS_RTC_IsReady() != 0U);

    if (is_bot_sleep) {
            // TOP이 슬립이 아니면 슬립 전환 명령
            if (!is_top_sleep) {
                CommandSubcommands(&BMS[TOP], SLEEP_ENABLE);  // TOP 슬립 진입
            }
            if (!use_rtc_time) {
                Accumulate_No_Load_Time(BMS_SLEEP_SCAN_PERIOD_MS);  // 폴백: 가정된 슬립 주기
            }
        }
        // BOT 상태가 Non-Sleep일 경우
        else {
            // TOP이 슬립 상태라면 슬립 해제 명령
            if (is_top_sleep) {
                CommandSubcommands(&BMS[TOP], SLEEP_DISABLE);  // TOP 슬립 해제
            }
            if (!use_rtc_time) {
                Accumulate_No_Load_Time(BMS_NORMAL_SCAN_PERIOD_MS);  // 폴백: 가정된 노멀 주기
            }
        }

    if (no_load_time_ms >= TIME_3_DAYS_MS) {
        CommandSubcommands(&BMS[BOT], SHUTDOWN);
        CommandSubcommands(&BMS[TOP], SHUTDOWN);
        delayUS(5000);
        HAL_SuspendTick();
        HAL_PWREx_EnterSHUTDOWNMode();
    }

    // 자기 전에 RTC 시각을 기록해 두고, 깨어난 뒤 차이를 읽어 실제 경과 시간을 얻는다.
    // SysTick은 아래에서 꺼지므로 HAL_GetTick()으로는 이걸 할 수 없다.
    uint8_t rtc_ok = BMS_RTC_IsReady();
    uint32_t sleep_entry_ms = rtc_ok ? BMS_RTC_NowMs() : 0U;

    // 주기적으로 깨어나야 하는 이유 두 가지:
    //   1. ALERT만 기다리면 BQ가 조용할 때 무기한 자게 되어 상태 감시가 멈춘다
    //   2. BQ 호스트 워치독(HWDDelay 60초)이 트립하면 양쪽 칩이 FET을 연다.
    //      주기적으로 깨어나 BQ를 읽으면 워치독이 리셋된다 (B_BMS_init.c의 HWD 주석 참조)
    if (rtc_ok) {
        BMS_RTC_StartWakeup(BMS_WAKEUP_PERIOD_S);
    }

    HAL_SuspendTick();

    if (no_load_time_ms >= TIME_1_HOUR_MS) {
        HAL_PWREx_EnterSTOP1Mode(PWR_STOPENTRY_WFI);
        entered_stop_mode = true;
    } else {
        HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
    }

    HAL_ResumeTick();

    if (entered_stop_mode) {
        SystemClock_Config();
        BMS_FanControl_Init();
    }

    if (rtc_ok) {
        BMS_RTC_StopWakeup();
        Accumulate_No_Load_Time(BMS_RTC_ElapsedMs(sleep_entry_ms));
    }
}

bool Handle_Wakeup_Event(void)
{
    bool already_ran_while_run = false;

    if (wakeup_reason == WAKEUP_BY_ALERT) {
    	LV_BMS_WHILE_RUN();
    	already_ran_while_run = true;

        if (!Is_No_Load() || BMS[BOT].ProtectionsTriggered || BMS[TOP].ProtectionsTriggered) {
            CommandSubcommands(&BMS[TOP], SLEEP_DISABLE);
            //Configure_BMS_Normal_Mask();
            no_load_time_ms = 0;
        }
    }

    wakeup_reason = WAKEUP_NONE;
    return already_ran_while_run;
}
