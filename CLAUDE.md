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
  Handle_Wakeup_Event()  ← re-syncs BQ sleep mask after an ALERT-pin wakeup
  LV_BMS_WHILE_RUN()     ← read all BQ data + update SOC + sync shared hardware data
  BMS_FanControl_Update()
  BMS_CAN_SendRunData(TOP/BOT)
  LV_STAT()              ← LED status: red=fault (protection, permanent-fail, or I2C/CRC comm error), green=normal
  Enter_Sleep_Sequence() ← paces the loop and chooses MCU SLEEP/STOP1/SHUTDOWN based on no-load time
```

`Enter_Sleep_Sequence()` (in `B_BMS_power_mode.c`) is what paces the loop, not a fixed `HAL_Delay(100)`: while there is load it calls `HAL_Delay(BMS_NORMAL_SCAN_PERIOD_MS)` (63 ms) so the loop can't run away at I2C speed; once `Is_No_Load()` is true it also decides whether to drop into MCU SLEEP or STOP1 (see Power management below).

### Application source files (Core/Src/)

| File | Purpose |
|---|---|
| `B_BMS.c` | BQ769x2 I2C driver (read/write with CRC8), voltage/current/temp readback, fan control, protection status parsing |
| `B_BMS_init.c` | One-time BQ769x2 register configuration (called from `BMS_MAIN_RUN`). TOP and BOT run the identical register-write sequence — there is no per-board branch |
| `B_BMS_cmd.h` | All BQ769x2 data-memory register addresses and direct/subcommand codes |
| `B_BMS_power_mode.c` | MCU and BQ sleep/wakeup sequencing: SLEEP → STOP1 (after 1 hr no load) → SHUTDOWN (after 3 days) |
| `B_BMS_soc.c` | Pack SOC estimation: coulomb counting off the BQ769x2's CC2-based accumulated-charge integrator, re-anchored to an OCV lookup after long rest periods |
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

IDs follow `datasheets/bms_can_id_spec.csv`, defined as `CANID_*` macros at the top of `B_TEST_BMS_can.c`. Frames with no matching entry in that spreadsheet keep their legacy `base + offset` address (BOT test-data base `0x100`, TOP test-data base `0x200`) until a new ID is assigned — these are commented `/* legacy */` in the source.

| Content | BOT | TOP |
|---|---|---|
| Status (fault/status) — run data | `0x040` | `0x043` |
| Stack/pack voltage — run data | `0x041` | `0x044` |
| Current/cell voltage/temp — run data | `0x042` | `0x045` |
| SOC — run data *(legacy)* | `0x503` | `0x603` |
| Cell voltages ×16 (4 frames) | `0x046`–`0x049` | `0x04F`–`0x052` |
| Stack/pack voltage — test data *(legacy)* | `0x111` | `0x211` |
| Current detail (CC1/CC3/CB active cells) *(legacy)* | `0x112` | `0x212` |
| Temperature frame 1 (TS1/FET/Int/CFETOFF) | `0x04A` | `0x053` |
| Temperature frame 2 (HDQ/Max/Min/Avg) | `0x04B` | `0x054` |
| Status — test data *(legacy)* | `0x120` | `0x220` |
| Safety alert/status | `0x058` | `0x06B` |
| Permanent-fail alert/status | `0x059` | `0x06C` |
| FET status (CHG/DSG/PDSG) *(legacy)* | `0x123` | `0x223` |
| CB_Status1 | `0x05A` | `0x06D` |
| Coulomb counter: accumulated charge int/frac | `0x04C` | `0x055` |
| Coulomb counter: accumulated time/CC2 | `0x04D` | `0x056` |
| Coulomb counter: CC3/voltage sum (6 bytes) | `0x04E` | `0x057` |
| SOC — test data *(legacy)* | `0x133` | `0x233` |
| CUV snapshot ×16 (4 frames) | `0x05B`–`0x05E` | `0x06E`–`0x071` |
| COV snapshot ×16 (4 frames) | `0x05F`–`0x062` | `0x072`–`0x075` |
| CB_Status2 ×16 (4 frames) | `0x063`–`0x066` | `0x076`–`0x079` |
| CB_Status3 ×16 (4 frames) | `0x067`–`0x06A` | `0x07A`–`0x07D` |

Notes:
- The FET status frame no longer carries `AlarmBits` — it's redundant with the `Alarm` field already in the status frame (`0x040`/`0x043`). `CB_Status1` was split out of that same combined frame into its own ID above.
- Run data (4 frames per board): fault flags + status, stack/pack voltage, current + cell voltage extremes + temperature, SOC.
- Test data adds: all 16 cell voltages, temperatures, CB status, CUV/COV snapshots, coulomb counter data, SOC.
- TOP has no current-sense pins (SRP/SRN unused — see the BQ76972 pin-usage table in the WSC report), so `Pack_Current`/`CC1_Current`/`CC3_Current` are always sent as `0` in TOP's frames (`0x045`, `0x212`) — real current data only comes from BOT (`BMS_CURRENT_BOARD`).
- The LD (Load Detect) pin is unused on both boards, so `LD_Voltage_Raw` is no longer transmitted at all: the CC3 coulomb-counter frame (`0x04E`/`0x057`) was shrunk from 8 to 6 bytes (`bms_can_send_len()`, a DLC-parameterized variant of `bms_can_send()`) instead of zero-filling the field.
- The legacy `0x110`/`0x210` (raw Stack/Pack voltage) frame was removed entirely — it duplicated the already-scaled (mV) `Stack_Voltage`/`Pack_Voltage` sent in both the run-data (`0x041`/`0x044`) and legacy test-data (`0x111`/`0x211`) voltage frames.

### Fan control

TIM2 CH2 PWM drives the cooling fan. **TIM2 is reserved for fan — do not use it for timing.** The `delayUS()` function uses the DWT cycle counter for this reason. Fan ramps linearly from 20 % at 35 °C to 100 % at 55 °C; below 33 °C it turns off (with hysteresis).

### Power management

`Enter_Sleep_Sequence()` in `B_BMS_power_mode.c` handles three power levels:
1. **MCU SLEEP** — short idle, low no-load time
2. **MCU STOP1** — after 1 hour no load; clock must be reconfigured (`SystemClock_Config()`) on wake
3. **BQ SHUTDOWN + MCU SHUTDOWN** — after 3 days no load

After waking from STOP1, both `SystemClock_Config()` and `BMS_FanControl_Init()` (re-starts TIM2 PWM) must be called — this is already done in `B_BMS_power_mode.c`.

"No load" is defined as `|Pack_Current| < 1000` (units: 10 mA steps from BQ, so < 10 A).

### SOC estimation (`B_BMS_soc.c`)

`BMS_SOC_Update()` runs every cycle inside `LV_BMS_WHILE_RUN()`, right before `BMS_SyncSharedHardwareData()`, so both stacks see the same `SOC_Permille` value (0–1000 = 0.0–100.0 %) after sync.

- **Coulomb counting**: uses the BQ769x2's own CC2-based integrator (`AccumulatedCharge_Int`/`AccumulatedCharge_Frac` from `DASTATUS6`) rather than manually integrating `Pack_Current * dt` in the MCU — the chip keeps integrating through MCU STOP1 sleep, so no samples are lost while asleep. Each cycle's delta is divided by the pack capacity (`BMS_PACK_CAPACITY_mAh`, 24 Ah) to update SOC.
- **OCV re-anchoring**: once `no_load_time_ms` (tracked in `B_BMS_power_mode.c`) exceeds `BMS_SOC_OCV_REST_MS` (10 min), cell voltages are assumed to have relaxed to open-circuit, and SOC is overwritten from an OCV→SOC lookup table instead of the coulomb-counted value. This bounds long-run coulomb-counting drift.
- `BMS_SOC_Init()` (called once from `main()` after `LV_BMS_MAIN_RUN()`) resets the estimator so the first `BMS_SOC_Update()` call seeds SOC from OCV rather than an assumed baseline.
- The OCV table in `B_BMS_soc.c` is an approximate NMC Li-ion curve sized to the CUV/COV thresholds in `B_BMS_init.c` (~2.53 V–4.20 V) — **replace it with the actual cell's characterization data before relying on it for the race.**

### Examples directory

`Examples/STM32U5_StopWake_Test/` is a standalone EVM test helper for STOP1 wake behavior. It is **not part of the BMS project build** — it must be copied into a separate STM32CubeIDE project to use.

## Key invariants

- `BMS_CURRENT_BOARD = BOT`: current/coulomb-counter data comes only from BOT
- `BMS_FET_BOARD = TOP`: FET status (CHG/DSG/PDSG) and FET temperature come only from TOP
- After any STOP1 wake, call `SystemClock_Config()` before any peripheral use
- The BQ769x2 alarm bit 0x0080 (ADSCAN) gates voltage/current reads in `BQ769x2_ReadData()`; data is only refreshed when the ADC scan completes
- `BQ769x2_Init()` must be called after `CommandSubcommands(BQ769x2_RESET)` with sufficient delay (~60 ms)
- Do not call `BQ769x2_ReadFETStatus()` on the BOT unit; it guards against this internally
