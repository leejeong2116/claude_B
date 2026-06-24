/*
 * stop_wake_test.c
 *
 * Minimal STOP1 wake-up test for STM32U5 boards.
 */

#include "stop_wake_test.h"

extern void SystemClock_Config(void);

static volatile uint32_t wake_count = 0;
static volatile uint8_t woke_by_exti = 0;

static void led_write(GPIO_PinState state)
{
    HAL_GPIO_WritePin(STOP_WAKE_LED_GPIO_Port, STOP_WAKE_LED_Pin, state);
}

static void led_blink(uint32_t count, uint32_t on_ms, uint32_t off_ms)
{
    for (uint32_t i = 0; i < count; i++) {
        led_write(GPIO_PIN_SET);
        HAL_Delay(on_ms);
        led_write(GPIO_PIN_RESET);
        HAL_Delay(off_ms);
    }
}

void StopWakeTest_Init(void)
{
    wake_count = 0;
    woke_by_exti = 0;
    led_blink(3, 80, 80);
}

void StopWakeTest_OnExti(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == STOP_WAKE_EXTI_Pin) {
        woke_by_exti = 1;
    }
}

void StopWakeTest_RunOnce(void)
{
    led_blink(1, 500, 500);

    woke_by_exti = 0;
    led_write(GPIO_PIN_RESET);

    HAL_SuspendTick();
    HAL_PWREx_EnterSTOP1Mode(PWR_STOPENTRY_WFI);

    SystemClock_Config();
    HAL_ResumeTick();

    wake_count++;

    if (woke_by_exti != 0U) {
        led_blink(2, 120, 120);
    } else {
        led_blink(6, 40, 40);
    }

    HAL_Delay(1000);
}
