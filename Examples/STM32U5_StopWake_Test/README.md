# STM32U5 STOP1 Wake Test

This is a separate EVM test helper. It is not part of the BMS project build.

## CubeMX Setup

1. Create a new STM32U5 EVM project.
2. Configure one LED pin as GPIO output.
3. Configure one button or jumper input pin as GPIO EXTI.
4. Enable the EXTI NVIC interrupt for that pin.
5. Copy `stop_wake_test.c` and `stop_wake_test.h` into the EVM project.
6. In `stop_wake_test.h`, set:
   - `STOP_WAKE_LED_GPIO_Port`
   - `STOP_WAKE_LED_Pin`
   - `STOP_WAKE_EXTI_Pin`

## Main Integration

Add this include in `main.c`:

```c
#include "stop_wake_test.h"
```

Call this after peripheral initialization:

```c
StopWakeTest_Init();
```

Use this in the main loop:

```c
while (1)
{
    StopWakeTest_RunOnce();
}
```

Forward the EXTI callback:

```c
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    StopWakeTest_OnExti(GPIO_Pin);
}
```

## Expected Behavior

- At boot, the LED blinks three times.
- Before STOP1, the LED turns off.
- Press or toggle the EXTI wake input.
- After wake, `SystemClock_Config()` is called again.
- If wake came from the configured EXTI pin, the LED blinks twice.
- If execution resumed for another reason, the LED blinks quickly six times.

