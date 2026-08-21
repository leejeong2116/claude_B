/*
 * B_BMS_fan.c
 *
 * 팩 냉각 팬 제어. 설계 근거와 제어 곡선은 B_BMS_fan.h 참조.
 */

#include "tim.h"

#include "B_BMS_fan.h"

// 실제로 장착된 팬 개수. 2027WSC 보고서 MCU 핀 표와 KiCad 회로도는 팬 2개분을 배선해 두었지만
// (PA1 = FAN1_PWM / TIM2 ch2, PA2 = FAN2_PWM / TIM2 ch3), 실물에는 1개만 장착된다
// (2026-08-04 확인). 없는 팬 채널을 구동해 봐야 의미가 없으므로 ch2만 쓴다.
//
// 나중에 두 번째 팬을 달면 이 값만 2로 바꾸면 된다 — .ioc와 회로도는 이미 준비돼 있고
// tim.c의 MX_TIM2_Init()도 두 채널 모두 PWM으로 설정해 둔 상태다.
#define BMS_FAN_COUNT 1

#define BMS_FAN1_PWM_CHANNEL TIM_CHANNEL_2
#define BMS_FAN2_PWM_CHANNEL TIM_CHANNEL_3

// 제어 곡선. OFF와 ON을 2도 벌린 것이 히스테리시스다 (헤더 참조).
#define BMS_FAN_OFF_TEMP_C 33.0f
#define BMS_FAN_ON_TEMP_C 35.0f
#define BMS_FAN_FULL_TEMP_C 55.0f
#define BMS_FAN_MIN_DUTY_PERCENT 20U
#define BMS_FAN_MAX_DUTY_PERCENT 100U

// 구조체에 남은 온도를 아직 믿을 수 있는 사이클 수. 읽기가 실패하면 온도 칸은 갱신되지
// 않고 직전 값이 남으므로, 나이를 안 보면 통신이 끊겨도 계속 "정상 온도"로 보인다.
// 1사이클 튐(CRC 한 번)마다 팬을 100%로 올리진 않으려고 2사이클(약 126ms)까지는 인정한다.
#define FAN_TEMP_STALE_CYCLES   2U

static uint8_t fan_duty = 0;

// 유효한 온도를 한 번이라도 받은 적이 있는가. "센서 고장(100%)"과 "센서 미실장(정지)"을
// 가르는 기준이다 — 자세한 이유는 헤더 참조.
static uint8_t fan_temp_ever_valid = 0;

// 서미스터가 빠졌거나 배선이 끊기면 BQ는 레일에 붙은 값을 돌려준다. 이 범위를 벗어나면
// 온도로 취급하지 않는다.
static uint8_t temp_is_valid(float temp_c)
{
    return (temp_c > -40.0f && temp_c < 130.0f) ? 1U : 0U;
}

void BMS_Fan_NoteTemperature(BMS_Unit *unit, float temp_c)
{
    if (temp_is_valid(temp_c)) {
        unit->temp_ever_valid = 1U;
    }
}

// 이 보드에서 쓸 수 있는 온도 중 최댓값. 쓸 값이 하나도 없으면 *valid = 0.
static float board_max_temp(const BMS_Unit *unit, uint8_t *valid)
{
    float max_temp = -1000.0f;
    const float temps[] = {
        unit->CELL_Temp,
        unit->Max_Cell_Temp,
        unit->Avg_Cell_Temp
    };

    *valid = 0U;

    // 미읽음(0.0f는 온도가 아니라 초기값) 또는 값이 오래됨
    if (unit->temp_ever_valid == 0U || unit->comm_fail_cycles >= FAN_TEMP_STALE_CYCLES) {
        return max_temp;
    }

    for (uint32_t i = 0; i < (sizeof(temps) / sizeof(temps[0])); i++) {
        if (temp_is_valid(temps[i])) {
            if (*valid == 0U || temps[i] > max_temp) {
                max_temp = temps[i];
            }
            *valid = 1U;
        }
    }

    // FET 온도(TS3)는 게이트를 구동하는 TOP에만 있다. BOT의 FET_Temp는 동기화된 사본이라
    // 여기서 또 보면 같은 값을 두 번 세게 된다.
    if (unit == &BMS[BMS_FET_BOARD] && temp_is_valid(unit->FET_Temp)) {
        if (*valid == 0U || unit->FET_Temp > max_temp) {
            max_temp = unit->FET_Temp;
        }
        *valid = 1U;
    }

    return max_temp;
}

static uint8_t duty_from_temp(float temp_c, uint8_t valid)
{
    // 온도를 모를 때: 한 번도 못 읽었으면 서미스터가 없는 것으로 보고 정지,
    // 읽다가 끊긴 것이면 팩이 뜨거운데 모를 수 있으므로 최대로 돌린다 (헤더의 전력 계산 참조).
    if (valid == 0U) {
        if (fan_temp_ever_valid == 0U) {
            return 0U;
        }
        return (LV_BMS_running > 0U) ? BMS_FAN_MAX_DUTY_PERCENT : 0U;
    }

    if (temp_c <= BMS_FAN_OFF_TEMP_C) {
        return 0U;
    }

    // 히스테리시스: 정지 상태에서는 ON 임계를 넘어야 기동한다. 한 번 돌기 시작하면
    // OFF 임계까지 내려가야 멈추므로 임계 근처에서 켜졌다 꺼졌다 하지 않는다.
    if (fan_duty == 0U && temp_c < BMS_FAN_ON_TEMP_C) {
        return 0U;
    }

    if (temp_c >= BMS_FAN_FULL_TEMP_C) {
        return BMS_FAN_MAX_DUTY_PERCENT;
    }

    // ON~FULL 구간을 MIN~MAX duty로 선형 매핑
    float range = BMS_FAN_FULL_TEMP_C - BMS_FAN_ON_TEMP_C;
    float ratio = (temp_c - BMS_FAN_ON_TEMP_C) / range;
    if (ratio < 0.0f) {
        ratio = 0.0f;
    }

    float duty = (float)BMS_FAN_MIN_DUTY_PERCENT +
                 (ratio * (float)(BMS_FAN_MAX_DUTY_PERCENT - BMS_FAN_MIN_DUTY_PERCENT));

    return (uint8_t)(duty + 0.5f);
}

void BMS_FanControl_SetDuty(uint8_t duty_percent)
{
    if (duty_percent > BMS_FAN_MAX_DUTY_PERCENT) {
        duty_percent = BMS_FAN_MAX_DUTY_PERCENT;
    }

    uint32_t period = __HAL_TIM_GET_AUTORELOAD(&htim2) + 1U;
    uint32_t pulse = (period * (uint32_t)duty_percent) / 100U;

    // MAX가 100이면 위 계산과 같은 값이지만, 상한을 100 미만으로 낮췄을 때
    // "최대 duty = 완전히 High"를 보장한다.
    if (duty_percent >= BMS_FAN_MAX_DUTY_PERCENT) {
        pulse = period;
    }

    __HAL_TIM_SET_COMPARE(&htim2, BMS_FAN1_PWM_CHANNEL, pulse);
#if BMS_FAN_COUNT >= 2
    __HAL_TIM_SET_COMPARE(&htim2, BMS_FAN2_PWM_CHANNEL, pulse);
#endif

    fan_duty = duty_percent;
}

uint8_t BMS_FanControl_GetDuty(void)
{
    return fan_duty;
}

void BMS_FanControl_Init(void)
{
    BMS_FanControl_SetDuty(0U);
    HAL_TIM_PWM_Start(&htim2, BMS_FAN1_PWM_CHANNEL);
#if BMS_FAN_COUNT >= 2
    HAL_TIM_PWM_Start(&htim2, BMS_FAN2_PWM_CHANNEL);
#endif
}

void BMS_FanControl_Update(void)
{
    uint8_t any_valid = 0U;
    float max_temp = -1000.0f;

    // 두 보드를 통틀어 가장 뜨거운 값 하나로 정한다 (평균이 아닌 이유는 헤더 참조).
    for (uint32_t i = 0; i < STACK; i++) {
        if (!BMS_UNIT_USED(i)) { continue; }
        uint8_t valid = 0U;
        float board_temp = board_max_temp(&BMS[i], &valid);

        if (valid != 0U) {
            if (any_valid == 0U || board_temp > max_temp) {
                max_temp = board_temp;
            }
            any_valid = 1U;
        }

        // any_valid 가 아니라 보드별 플래그로 세운다. 지금 값을 못 믿어도(any_valid=0)
        // "읽은 적은 있다"가 남아야 duty_from_temp()가 고장(100%)과 미실장(정지)을 가른다.
        if (BMS[i].temp_ever_valid != 0U) {
            fan_temp_ever_valid = 1U;
        }
    }

    BMS_FanControl_SetDuty(duty_from_temp(max_temp, any_valid));
}

uint8_t BMS_FanControl_TempEverValid(void)
{
    return fan_temp_ever_valid;
}
