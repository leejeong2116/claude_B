/*
 * B_BMS_power_mode.c
 *
 *  Created on: May 6, 2026
 *      Author: user
 */

#include "B_BMS_power_mode.h"
#include "B_BMS.h"
#include "B_BMS_cmd.h"

#define BMS_NORMAL_SCAN_PERIOD_MS 63U //normal / 63ms scan
#define BMS_SLEEP_SCAN_PERIOD_MS 5000U // sleep / 5s scan
#define TIME_1_HOUR_MS 3600000UL
#define TIME_3_DAYS_MS 259200000UL
#define BMS_BATTERY_STATUS_SLEEP_BIT 0x8000U

uint32_t no_load_time_ms = 0;

volatile WakeupReason_t wakeup_reason = WAKEUP_NONE;
extern BMS_Unit BMS[STACK];

bool Is_No_Load(void)
{
    int16_t current = (int16_t)BMS[BOT].Pack_Current;

    return (current > -1000 && current < 1000);
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
// TODO(#27): elapsed_ms는 실측이 아니라 가정된 스캔 주기(BMS_SLEEP_SCAN_PERIOD_MS/BMS_NORMAL_SCAN_PERIOD_MS)다.
// HAL_SuspendTick()이 매 저전력 진입 전 SysTick을 꺼서 HAL_GetTick()으로 실제 경과 시간을 잴 수 없고,
// SLEEP/STOP1은 다음 인터럽트(주로 BQ ALERT)까지 무기한 블로킹되므로 실제 경과 시간이 이 상수와 전혀
// 무관하게 짧아지거나 길어질 수 있다 — 즉 1시간/3일 임계값 도달 시점이 실제 시간과 어긋날 수 있음.
// 올바른 수정은 STOP1을 관통해 계속 도는 RTC Wake-up Timer를 별도 주기적 웨이크 소스로 추가해
// 그 틱에서만 이 함수를 호출하는 것 (RTC/.ioc 변경 필요 — 하드웨어에서 검증 후 구현할 것).
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

    if (!Is_No_Load() || protections_triggered) {
        no_load_time_ms = 0;
        // 부하가 있을 때도 루프 주기를 일정하게 유지 (I2C/CAN이 풀 스피드로 폭주하지 않도록)
        HAL_Delay(BMS_NORMAL_SCAN_PERIOD_MS);
        return;
    }
    bool entered_stop_mode = false;
    bool is_bot_sleep = ((BMS[BOT].BattStat & BMS_BATTERY_STATUS_SLEEP_BIT) != 0U);
    bool is_top_sleep = ((BMS[TOP].BattStat & BMS_BATTERY_STATUS_SLEEP_BIT) != 0U);

    if (is_bot_sleep) {
            // TOP이 슬립이 아니면 슬립 전환 명령
            if (!is_top_sleep) {
                CommandSubcommands(&BMS[TOP], SLEEP_ENABLE);  // TOP 슬립 진입
            }
            Accumulate_No_Load_Time(BMS_SLEEP_SCAN_PERIOD_MS);  // 슬립 상태에서 시간 누적
        }
        // BOT 상태가 Non-Sleep일 경우
        else {
            // TOP이 슬립 상태라면 슬립 해제 명령
            if (is_top_sleep) {
                CommandSubcommands(&BMS[TOP], SLEEP_DISABLE);  // TOP 슬립 해제
            }
            Accumulate_No_Load_Time(BMS_NORMAL_SCAN_PERIOD_MS);  // 노멀 상태에서 시간 누적
        }

    if (no_load_time_ms >= TIME_3_DAYS_MS) {
        CommandSubcommands(&BMS[BOT], SHUTDOWN);
        CommandSubcommands(&BMS[TOP], SHUTDOWN);
        delayUS(5000);
        HAL_SuspendTick();
        HAL_PWREx_EnterSHUTDOWNMode();
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
