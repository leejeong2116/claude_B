# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Bare-metal STM32U545CEUXQ (ARM Cortex-M33) firmware for a 2-stack Battery Management System using two Texas Instruments BQ76972 (BQ769x2) ICs. Developed for the 2027 WSC KUST solar vehicle team.

**Target hardware:** STM32U545CEUXQ — 512 KB Flash, 272 KB RAM, running at ~160 MHz (MSI → PLL).

## Build and Flash

This is an STM32CubeIDE project. Build and flash from within the IDE using the standard Debug/Release build configurations.

- Project files: `.project`, `.cproject`, `.mxproject`, `.ioc`
- Linker scripts: `STM32U545CEUXQ_FLASH.ld` (normal run), `STM32U545CEUXQ_RAM.ld` (RAM debug)
- Startup: `Core/Startup/startup_stm32u545ceuxq.s`

There is no command-line build system (no Makefile or CMake). All compilation is done via the STM32CubeIDE toolchain (`arm-none-eabi-gcc`).

## Architecture

### Global state

`BMS[STACK]` in `B_BMS.c` is a two-element array of `BMS_Unit` structs (`BMS[TOP]` = index 0, `BMS[BOT]` = index 1). Nearly every function takes a `BMS_Unit *unit` pointer into this array. The array is declared `extern` and shared across all application files.

```
TOP (BMS[0]) — hi2c2, addr 0x10 — FET control board (BMS_FET_BOARD)
BOT (BMS[1]) — hi2c4, addr 0x10 — Current sensing board (BMS_CURRENT_BOARD)
```

Because the two BQ76972 chips share physical hardware (single current shunt, shared FETs), `BMS_SyncSharedHardwareData()` is called every cycle to copy current readings from BOT into TOP's struct and FET status from TOP into BOT's struct. Do not read current fields from `BMS[TOP]` or FET fields from `BMS[BOT]` directly without understanding this sync.

### Main loop (main.c)

```
LV_BMS_MAIN_RUN()        ← one-time init: I2C init, BQ reset, BQ769x2_Init for each board
BMS_FanControl_Init()

while(1):
  LV_BMS_WHILE_RUN()     ← read all BQ data + sync shared hardware data
  BMS_FanControl_Update()
  BMS_CAN_SendRunData(TOP/BOT)
  LV_STAT()              ← LED status: red=fault, green=normal
  HAL_Delay(100)         ← 100 ms cycle
```

### Application source files (Core/Src/)

| File | Purpose |
|---|---|
| `B_BMS.c` | BQ769x2 I2C driver (read/write with CRC8), voltage/current/temp readback, fan control, protection status parsing |
| `B_BMS_init.c` | One-time BQ769x2 register configuration (called from `BMS_MAIN_RUN`). **Only BOT is configured**; the `if(unit == &BMS[TOP])` block is intentionally empty — TOP uses default values |
| `B_BMS_cmd.h` | All BQ769x2 data-memory register addresses and direct/subcommand codes |
| `B_BMS_power_mode.c` | MCU and BQ sleep/wakeup sequencing: SLEEP → STOP1 (after 1 hr no load) → SHUTDOWN (after 3 days) |
| `B_TEST_BMS.c` | Extended diagnostics readout (CB status, snapshots) |
| `B_TEST_BMS_can.c` | CAN telemetry — run data and full test/diagnostic frames |

### I2C communication

`CRC_Mode` is hardcoded to `1`. Every byte written is followed by a CRC8 byte; every byte read is CRC8-verified. `RX_CRC_Fail` (global counter in `B_BMS.c`) increments on each CRC mismatch — check this during hardware debugging. Do not disable CRC mode; the BQ chip is configured for CRC mode in `B_BMS_init.c`.

BQ769x2 has three command types used throughout the driver:
- **Direct commands** (`DirectCommands`): single-register 1- or 2-byte reads/writes at addresses < 0x80
- **Subcommands** (`Subcommands` / `CommandSubcommands`): write a 16-bit command to 0x3E, then read 32 bytes from 0x40
- **Register writes** (`BQ769x2_SetRegister`): write to data-memory registers (addresses in `B_BMS_cmd.h`, all ≥ 0x9180), requires checksum byte at 0x60

### CAN telemetry (FDCAN1, Classic CAN)

All CAN frames are 8-byte standard-ID. Byte order is **big-endian** in CAN frames (unlike the little-endian BQ register reads).

| Board | Run data base ID | Test/diagnostic base ID |
|---|---|---|
| BOT | 0x500 | 0x100 |
| TOP | 0x600 | 0x200 |

Run data (3 frames per board): fault flags + status, stack/pack voltage, current + cell voltage extremes + temperature.

Test data adds: all 16 cell voltages, temperatures, CB status, CUV/COV snapshots, coulomb counter data.

### Fan control

TIM2 CH2 PWM drives the cooling fan. **TIM2 is reserved for fan — do not use it for timing.** The `delayUS()` function uses the DWT cycle counter for this reason. Fan ramps linearly from 20 % at 35 °C to 100 % at 55 °C; below 33 °C it turns off (with hysteresis).

### Power management

`Enter_Sleep_Sequence()` in `B_BMS_power_mode.c` handles three power levels:
1. **MCU SLEEP** — short idle, low no-load time
2. **MCU STOP1** — after 1 hour no load; clock must be reconfigured (`SystemClock_Config()`) on wake
3. **BQ SHUTDOWN + MCU SHUTDOWN** — after 3 days no load

After waking from STOP1, both `SystemClock_Config()` and `BMS_FanControl_Init()` (re-starts TIM2 PWM) must be called — this is already done in `B_BMS_power_mode.c`.

"No load" is defined as `|Pack_Current| < 1000` (units: 10 mA steps from BQ, so < 10 A).

### Examples directory

`Examples/STM32U5_StopWake_Test/` is a standalone EVM test helper for STOP1 wake behavior. It is **not part of the BMS project build** — it must be copied into a separate STM32CubeIDE project to use.

## Key invariants

- `BMS_CURRENT_BOARD = BOT`: current/coulomb-counter data comes only from BOT
- `BMS_FET_BOARD = TOP`: FET status (CHG/DSG/PDSG) and FET temperature come only from TOP
- After any STOP1 wake, call `SystemClock_Config()` before any peripheral use
- The BQ769x2 alarm bit 0x0080 (ADSCAN) gates voltage/current reads in `BQ769x2_ReadData()`; data is only refreshed when the ADC scan completes
- `BQ769x2_Init()` must be called after `CommandSubcommands(BQ769x2_RESET)` with sufficient delay (~60 ms)
- Do not call `BQ769x2_ReadFETStatus()` on the BOT unit; it guards against this internally
