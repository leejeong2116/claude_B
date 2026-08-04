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
  BMS_Protect_Update()   ← MCU-side supervisor: cross-check cutoff path, drive BOTHOFF
  BMS_FanControl_Update()
  BMS_CAN_SendRunData(TOP/BOT)
  BMS_CAN_SendProtectDiag()
  LV_STAT()              ← LED status: red=BQ fault, green=normal, blue=MCU asserted BOTHOFF, TOP_RED=latched
  Enter_Sleep_Sequence() ← paces the loop and chooses MCU SLEEP/STOP1/SHUTDOWN based on no-load time
```

`Enter_Sleep_Sequence()` (in `B_BMS_power_mode.c`) is what paces the loop, not a fixed `HAL_Delay(100)`: while there is load it calls `HAL_Delay(BMS_NORMAL_SCAN_PERIOD_MS)` (63 ms) so the loop can't run away at I2C speed; once `Is_No_Load()` is true it also decides whether to drop into MCU SLEEP or STOP1 (see Power management below).

### Application source files (Core/Src/)

| File | Purpose |
|---|---|
| `B_BMS.c` | BQ769x2 I2C driver (read/write with CRC8), voltage/current/temp readback, fan control, protection status parsing |
| `B_BMS_init.c` | One-time BQ769x2 register configuration (called from `BMS_MAIN_RUN`). **TOP and BOT differ** — see "Per-board configuration" below |
| `B_BMS_protect.c` | MCU-side protection supervisor: cross-checks the BQ cutoff path, watches comms, drives BOTHOFF. See "FET cutoff path" below |
| `B_BMS_cmd.h` | All BQ769x2 data-memory register addresses and direct/subcommand codes |
| `B_BMS_power_mode.c` | MCU and BQ sleep/wakeup sequencing: SLEEP → STOP1 (after 1 hr no load) → SHUTDOWN (after 3 days) |
| `B_BMS_soc.c` | Pack SOC estimation: coulomb counting off the BQ769x2's CC2-based accumulated-charge integrator, re-anchored to an OCV lookup after long rest periods |
| `B_TEST_BMS.c` | Extended diagnostics readout (CB status, snapshots) |
| `B_TEST_BMS_can.c` | CAN telemetry — run data and full test/diagnostic frames |

### FET cutoff path (the most important thing to understand in this repo)

The two chips do **not** each drive their own FETs. Only TOP drives the pack's CHG/DSG gates.
BOT reaches those FETs through pins and opto-isolators, not through the MCU:

```
MCU PA15 (open-drain + external pull-up)
   │  BQ769x2_BOTHOFF() / _RESET()
   ▼
BOT.DFETOFF (BOTHOFF, active-H)
   ├─ BOT.DCHG (active-H, High when CHG FET should be off) ──G3VM──▶ TOP.CFETOFF ──▶ CHG FET off
   └─ BOT.DDSG (active-H, High when DSG FET should be off) ──G3VM──▶ TOP.DFETOFF ──▶ DSG FET off
```

Consequences:

- **Selective cutoff (CHG-only / DSG-only) is a hardware path, not software.** The MCU has exactly
  one cutoff output (BOTHOFF), which always kills both. Never try to implement selective cutoff
  in the MCU; let the BQ pins do it.
- **All current-based protections (OCC/OCD1/OCD2/OCD3/SCD/OCDL/SCDL) can only trip on BOT** — TOP
  has no shunt (SRP/SRN unused). They reach the FETs only via the DCHG/DDSG → CFETOFF/DFETOFF path.
- **The MCU pin is fail-safe.** On MCU reset the open-drain pin goes Hi-Z and the external pull-up
  asserts BOTHOFF. `LV_BMS_MAIN_RUN()` re-asserts it for the duration of init and only releases it
  after confirming both chips respond.
- G3VM propagation delay dominates the fast protections. **SCD is configured at 450 µs but the real
  cutoff time is set by the opto-isolator**, which is typically far slower — verify against the G3VM
  datasheet before relying on SCD for short-circuit protection.

`B_BMS_protect.c` supervises this path. Its rule: BQ handles selective cutoff; the MCU only handles
what BQ cannot see (comm loss, path failure, measurement-chain inconsistency) and its only action is
BOTHOFF. Trip reasons are in `BMS_TripReason_t` and go out on CAN `0x080`.

The `CHGFET_MASK_*` / `DSGFET_MASK_*` constants in `B_BMS_init.h` are the single source of truth:
`B_BMS_init.c` writes them to the chip, and `B_BMS_protect.c` ANDs them against `SafetyStatus A/B/C`
to compute whether a FET *should* be off. **Changing one without the other silently breaks the
cross-check.**

### Per-board configuration (`B_BMS_init.c`)

`BQ769x2_Init()` picks a `BQ_BoardConfig_t` (`BQ_CFG_TOP` / `BQ_CFG_BOT`) based on the unit pointer.
Everything else in the sequence is identical for both boards. The board-specific registers are
`CFETOFFPinConfig`, `DFETOFFPinConfig`, `DCHGPinConfig`, `DDSGPinConfig`, `TS3Config`, and
`ChgPumpControl` — see the comment block at the top of that file for the derivation of each value
and for which ones still need hardware verification.

Two open items are flagged there as `TODO` rather than silently decided:
- `BQ_CFG_TOP.chg_pump` — the WSC report and the `FETOptions` comment disagree on whether TOP's
  charge pump is used. Measure the high-side gate voltage.
- `B_BMS_HWD_PROTECTION_ENABLE` — HWDF trips after 60 s of MCU silence, which collides with
  `Enter_Sleep_Sequence()` sleeping indefinitely. Default is unchanged (enabled); the real fix is
  an RTC periodic wake.

### Voltage/current units (`DAConfiguration`)

`DAConfiguration` is `0x06`: **centivolt (10 mV) user-volts, centiamp (10 mA) user-amps.**

The volts half is not a free choice. Per TRM Table 13-15 (SLUUCW9 p.159), `Stack Voltage` (0x34),
`PACK Pin Voltage` (0x36) and `LD Pin Voltage` (0x38) are reported in "user-volts" and must fit a
**signed** 16-bit integer, so millivolt units saturate at 32767 mV. Each chip measures 16S (up to
67.2 V), so millivolts are not usable here — `USER_VOLTS_CV` must stay at 1.

`stack_userV_to_mV()` in `B_BMS.c` applies the matching ×10. **These two must always change together.**

Not everything follows user-volts — check the TRM's unit column before assuming:

| Value | Unit | Follows `DAConfiguration`? |
|---|---|---|
| Cell voltages (0x14–0x32) | mV | no — always mV |
| Stack / PACK / LD voltage | userV | **yes** |
| `Battery Voltage Sum` (DASTATUS5 bytes 8–9) | cV | no — always 10 mV |
| Min/Max Cell Voltage (DASTATUS5 bytes 4–7) | mV | no |
| CC1/CC3 current, accumulated charge | userA / userAh | **yes** |
| Min Blow Fuse V, Shutdown Stack V, Sleep Charger V, PACK-TOS deltas | 10 mV | no |
| CUV/COV thresholds | 50.6 mV steps | no |

The current half (`USER_AMPS`) is assumed to be 10 mA in several places — `Is_No_Load()`'s `1000`
= 10 A, the userAh→mAh scaling in `B_BMS_soc.c`, and the OCC/OCD/SCD threshold comments in
`B_BMS_init.c`. Do not change bits 1:0 without auditing all of them.

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
| Status (fault/status) — run data | `0x040` | `0x044` |
| Stack/pack voltage — run data | `0x041` | `0x045` |
| Current/cell voltage/temp — run data | `0x042` | `0x046` |
| SOC — run data | `0x043` | `0x047` |
| Cell voltages ×16 (4 frames) | `0x048`–`0x04B` | `0x051`–`0x054` |
| Stack/pack voltage — test data *(legacy)* | `0x111` | `0x211` |
| Current detail (CC1/CC3/CB active cells) *(legacy)* | `0x112` | `0x212` |
| Temperature frame 1 (TS1/FET/Int/CFETOFF) | `0x04C` | `0x055` |
| Temperature frame 2 (HDQ/Max/Min/Avg) | `0x04D` | `0x056` |
| Status — test data *(legacy)* | `0x120` | `0x220` |
| Safety alert/status | `0x05A` | `0x06D` |
| Permanent-fail alert/status | `0x05B` | `0x06E` |
| FET status (CHG/DSG/PDSG) *(legacy)* | `0x123` | `0x223` |
| CB_Status1 | `0x05C` | `0x06F` |
| Coulomb counter: accumulated charge int/frac | `0x04E` | `0x057` |
| Coulomb counter: accumulated time/CC2 | `0x04F` | `0x058` |
| Coulomb counter: CC3/voltage sum (6 bytes) | `0x050` | `0x059` |
| SOC — test data *(legacy)* | `0x133` | `0x233` |
| CUV snapshot ×16 (4 frames) | `0x05D`–`0x060` | `0x070`–`0x073` |
| COV snapshot ×16 (4 frames) | `0x061`–`0x064` | `0x074`–`0x077` |
| CB_Status2 ×16 (4 frames) | `0x065`–`0x068` | `0x078`–`0x07B` |
| CB_Status3 ×16 (4 frames) | `0x069`–`0x06C` | `0x07C`–`0x07F` |
| MCU protection supervisor diagnostics | `0x080` (pack-wide, not per-board) | |

Notes:
- The FET status frame no longer carries `AlarmBits` — it's redundant with the `Alarm` field already in the status frame (`0x040`/`0x044`). `CB_Status1` was split out of that same combined frame into its own ID above.
- Run data (4 frames per board): fault flags + status, stack/pack voltage, current + cell voltage extremes + temperature, SOC.
- Test data adds: all 16 cell voltages, temperatures, CB status, CUV/COV snapshots, coulomb counter data, SOC.
- TOP has no current-sense pins (SRP/SRN unused — see the BQ76972 pin-usage table in the WSC report), so `Pack_Current`/`CC1_Current`/`CC3_Current` are always sent as `0` in TOP's frames (`0x046`, `0x212`) — real current data only comes from BOT (`BMS_CURRENT_BOARD`).
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

- **Cell**: Samsung SDI INR21700-50S (5000 mAh typ, 3.6 V nominal, 4.20 V charge, 2.5 V cutoff).
  `BMS_PACK_CAPACITY_mAh` is `BMS_CELL_CAPACITY_mAh × BMS_PACK_PARALLEL_COUNT`. **The parallel count
  is not confirmed** — the old comment said 30S4P while the hardware is 16S×2 = 32S, so the series
  count already disagreed. Fix `BMS_PACK_PARALLEL_COUNT` once the pack build is settled.
- **Coulomb counting**: uses the BQ769x2's own CC2-based integrator (`AccumulatedCharge_Int`/`AccumulatedCharge_Frac` from `DASTATUS6`) rather than manually integrating `Pack_Current * dt` in the MCU — the chip keeps integrating through MCU STOP1 sleep, so no samples are lost while asleep. Each cycle's delta is divided by the pack capacity to update SOC.
- **OCV re-anchoring**: once `no_load_time_ms` (tracked in `B_BMS_power_mode.c`) exceeds `BMS_SOC_OCV_REST_MS` (10 min), cell voltages are assumed to have relaxed to open-circuit, and SOC is overwritten from an OCV→SOC lookup table instead of the coulomb-counted value. This bounds long-run coulomb-counting drift.
- `BMS_SOC_Init()` (called once from `main()` after `LV_BMS_MAIN_RUN()`) resets the estimator so the first `BMS_SOC_Update()` call seeds SOC from OCV rather than an assumed baseline.
- The OCV table in `B_BMS_soc.c` has **only its two endpoints from the datasheet**. The 50S datasheet
  contains no discharge-voltage curve (§7.8/7.9 are capacity-percentage tables, and the figures are
  dimensional/packaging drawings), so the mid-curve shape is a generic high-power NMC approximation
  anchored to 4.20 V / 3.6 V nominal / 2.5 V cutoff. **Replace it with measured OCV data before
  relying on it for the race** — see `HARDWARE_VERIFICATION.md` §3-1 for the measurement procedure.

### Examples directory

`Examples/STM32U5_StopWake_Test/` is a standalone EVM test helper for STOP1 wake behavior. It is **not part of the BMS project build** — it must be copied into a separate STM32CubeIDE project to use.

## Key invariants

- `BMS_CURRENT_BOARD = BOT`: current/coulomb-counter data comes only from BOT
- `BMS_FET_BOARD = TOP`: FET status (CHG/DSG/PDSG) and FET temperature come only from TOP
- After any STOP1 wake, call `SystemClock_Config()` before any peripheral use
- The BQ769x2 alarm bit 0x0080 (ADSCAN) gates voltage/current reads in `BQ769x2_ReadData()`; data is only refreshed when the ADC scan completes
- `BQ769x2_Init()` must be called after `CommandSubcommands(BQ769x2_RESET)` with sufficient delay (~60 ms)
- Do not call `BQ769x2_ReadFETStatus()` on the BOT unit; it guards against this internally
- `CHGFET_MASK_*`/`DSGFET_MASK_*` (`B_BMS_init.h`) must match what `B_BMS_init.c` writes to the chip
- The MCU's only cutoff action is BOTHOFF (all FETs). Selective cutoff belongs to the BQ pin path
- Never send `PF_RESET` automatically — permanent failures require human inspection
- `OCDL`/`SCDL` latch and need explicit `OCDL_RECOVER`/`SCDL_RECOVER`; without them the pack locks out
  permanently (`recover_latched_protections()` in `B_BMS_protect.c` handles this)
- Timeouts in `B_BMS_protect.c` are counted in loop cycles, not ms — `HAL_GetTick()` is unusable
  because `HAL_SuspendTick()` stops SysTick before every low-power entry
