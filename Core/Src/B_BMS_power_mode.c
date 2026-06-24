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
	if (!Is_No_Load()) { // 부하가 흐르고 있다면 (주행 중이라면)
	        no_load_time_ms = 0; // 누적된 시간 강제 초기화
	        return; // 즉시 함수 탈출! (아래 수면 로직 실행 안 함)
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

    HAL_SuspendTick();

    if (no_load_time_ms >= TIME_3_DAYS_MS) {
        CommandSubcommands(&BMS[BOT], SHUTDOWN);
        CommandSubcommands(&BMS[TOP], SHUTDOWN);
        HAL_Delay(5);
        HAL_PWREx_EnterSHUTDOWNMode();
    } else if (no_load_time_ms >= TIME_1_HOUR_MS) {
        HAL_PWREx_EnterSTOP1Mode(PWR_STOPENTRY_WFI);
        entered_stop_mode = true;
    } else {
        HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
    }

    if (entered_stop_mode) {
        SystemClock_Config();
        BMS_FanControl_Init();
    }

    HAL_ResumeTick();
}

void Handle_Wakeup_Event(void)
{
    if (wakeup_reason == WAKEUP_BY_ALERT) {
    	LV_BMS_WHILE_RUN();

        if (!Is_No_Load() || BMS[BOT].ProtectionsTriggered || BMS[TOP].ProtectionsTriggered) {
            CommandSubcommands(&BMS[TOP], SLEEP_DISABLE);
            //Configure_BMS_Normal_Mask();
            no_load_time_ms = 0;
        }
    }

    wakeup_reason = WAKEUP_NONE;
}
