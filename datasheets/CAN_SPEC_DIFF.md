# CAN 명세 ↔ 현재 코드 차이

**명세**: `STM32CubeIDE/workspace_1.19.0/BMS_PROJECT/BMS_CAN_ID_명세_최신_PR31.xlsx`
 (출처가 명세 안에 적혀 있음 — `claude_B.git · Core/Src/B_TEST_BMS_can.c · 커밋 b41d2fe (PR#31)`)
**코드**: `main` @ PR #38 머지 시점

`b41d2fe`는 PR #32 작업이 시작되기 **직전** 커밋이므로, 이 명세는 그 시점 코드와 정확히 일치한다.
따라서 아래 차이는 **PR #32~#38에서 생긴 것 전부**다.

---

## 결론: ID는 44개 전부 그대로. 실제 차이는 3건

| # | 항목 | 종류 | 유입 |
|---|---|---|---|
| 1 | **`0x080` 신설** — MCU 보호 감시자 진단 | 신규 프레임 | PR #35 |
| 2 | **`0x043`/`0x047` byte 2·3에 팬 정보 추가** | 기존 프레임에 필드 추가 | PR #35 · #37 |
| 3 | `0x050`/`0x059` `BattVoltageSum` 단위 명기 | 문서만 (값 불변) | PR #32 |

RUN 8개 + TEST 36개 = **44개 엔트리의 ID·보드·DLC가 명세와 100 % 일치**한다.
프레임 배치는 하나도 안 건드렸다.

---

## 1. `0x080` — 신설 프레임 (명세에 없음)

MCU 보호 감시자(`B_BMS_protect.c`) 상태. **보드별이 아니라 팩 단위**라 BOT/TOP 쌍이 없다.

| 바이트 | 필드 | 설명 |
|---|---|---|
| 0 | `TripReason` | `BMS_TripReason_t` (0=정상, 4=통신두절, 5=PF, 9=재시도초과 …) |
| 1 | `Flags` | b0 BOTHOFF어서트 · b1 latch · b2 CHG기대OFF · b3 DSG기대OFF · b4 CHG실측ON · b5 DSG실측ON |
| 2 | `RetryCount` | BOTHOFF 해제 재시도 횟수 |
| 3 | `ChgMismatchCycles` | CHG 차단 경로 불일치 연속 사이클 |
| 4 | `DsgMismatchCycles` | DSG 차단 경로 불일치 연속 사이클 |
| 5 | `StackMismatchCycles` | 셀합 vs 스택전압 불일치 연속 사이클 |
| 6 | `TopCommFailCycles` | TOP 연속 통신 실패 사이클 |
| 7 | `BotCommFailCycles` | BOT 연속 통신 실패 사이클 |

> **읽는 법**: byte 1의 **b2와 b4가 동시에 1**이면 CHG 차단 경로
> (`BOT.DCHG → G3VM → TOP.CFETOFF`) 고장. b3·b5는 DSG 경로.

DLC 8, 매 루프 송신 (`main.c`의 `BMS_CAN_SendProtectDiag()`).

---

## 2. `0x043` / `0x047` — SOC 프레임에 팬 정보 추가

명세는 이 프레임을 `SOC(u16 permille)` 로만 적고 있고 byte 2~7은 기재가 없다.
실제로는 **0으로 채워 낭비되고 있었다.** 거기에 팬 상태를 실었다.

| 바이트 | 명세 | 현재 코드 |
|---|---|---|
| 0~1 | `SOC_Permille` (u16, 0.1 %) | 동일 |
| **2** | (기재 없음 / 0) | **팬 duty [%]** (u8, 0~100) |
| **3** | (기재 없음 / 0) | **온도 유효 플래그** (u8, 0 = 유효 온도를 한 번도 못 읽음) |
| 4~7 | (기재 없음 / 0) | 0 유지 |

**기존 필드는 하나도 안 움직였다.** byte 0~1만 읽던 수신기는 영향 없다.

> 왜 넣었나 — 팬은 이 보드에서 압도적으로 큰 소비원인데(100 % = 8.52 W, MCU는 8.6 mW)
> 밖에서 몇 %로 도는지 볼 방법이 없었다. byte 3이 0이면 서미스터 미실장 의심.

---

## 3. `0x050` / `0x059` — 단위 명기 (값은 그대로)

`BattVoltageSum` 이 **cV(10 mV) 단위**임을 CSV에 적었다. TRM Table 12-23 확인 결과다.
프레임 구조·값·DLC(6바이트) 전부 변화 없다.

---

## 4. 명세 서술 중 아직 안 맞는 것

| 항목 | 명세 | 실제 |
|---|---|---|
| **송신 주기** | *"메인 루프 약 100 ms 주기"* | **~300 ms 추정** |
| 비트레이트 500 kbit/s | | ✅ 일치 |
| DLC (0x050/0x059만 6바이트) | | ✅ 명세가 정확히 기재함 |
| TOP 전류 항상 0 | | ✅ 일치 |

주기가 100 ms가 아닌 이유: `Enter_Sleep_Sequence()`의 `HAL_Delay(63)`는 사이클 끝에 붙는 값일 뿐이고,
실제 주기는 `LV_BMS_WHILE_RUN()`의 I2C 왕복이 지배한다 —
`DirectCommands()`/`Subcommands()`가 호출마다 `delayUS(2000)`을 걸고 보드당 약 40회 돌아
두 보드 합쳐 ~168 ms, 여기에 100 kHz 실제 전송 ~68 ms가 더해진다.

- [ ] `EVM_TEST.md` §6에서 실측 후 명세의 주기 서술 갱신
      (`B_BMS_protect.c`의 `BMS_LOOP_PERIOD_MS_EST` 도 같은 값으로)

---

## 5. 명세 파일이 여러 벌이다

| 파일 | 기준 | 상태 |
|---|---|---|
| **`workspace/BMS_PROJECT/BMS_CAN_ID_명세_최신_PR31.xlsx`** | **b41d2fe (PR#31)** | **가장 정확.** 위 3건만 반영하면 최신이 됨 |
| `workspace/BMS_PROJECT/BMS_CAN_ID_명세_최신.xlsx` | (07-21) | PR31본의 이전 판 |
| `내 공부/BMS_CAN_ID_명세.xlsx` | 06-25 작성 / 07-10 갱신 | **RUN이 `0x5xx`/`0x6xx` 옛 체계.** 단 **비트맵(Fault_Bits) 시트는 다른 데 없는 자료** |
| `datasheets/bms can.xlsx` | — | 또 다른 옛 체계 |
| `datasheets/bms_can_id_spec.csv` | 현재 코드 | 코드와 일치 (레포 안) |

- [ ] `BMS_CAN_ID_명세_최신_PR31.xlsx` 에 위 3건을 반영해 `_PR38` 로 갱신
- [ ] `내 공부/BMS_CAN_ID_명세.xlsx` 의 **비트맵 시트를 별도 md로 옮겨 레포에 보존**
      (SafetyStatus A/B/C · PF Status A~D · FET Status · Alarm · Battery Status 비트 정의 — 품질이 좋고 코드/CSV에 없는 정보)
- [ ] 옛 체계 파일 2개(`내 공부/…`, `datasheets/bms can.xlsx`)는 `_구버전` 표시 또는 폐기

---

## 부기 — 옛 명세가 맞았고 코드가 틀렸던 것 (이미 해결)

`내 공부/BMS_CAN_ID_명세.xlsx`(옛 체계본)는 단위 요약에 이렇게 적고 있었다:

> `Stack_Voltage / Pack_Voltage | uint32, 단위 mV (**BQ raw × 10**)`
> `*_Raw 전압류 | Stack/Pack은 **10 mV 단위** raw`

그런데 코드는 issue #27 리뷰에서 이 ×10을 제거했고(`0ee61c2`), 그 결과 16S 67 V가
부호있는 16비트에서 **32767에 포화**되어 있었다. PR #32에서 `DAConfiguration`을
`0x06`(센티볼트)로 되돌리고 ×10을 복원했다.

**ID 체계는 옛것이지만 단위 서술은 그 문서가 처음부터 맞았다.** 위 비트맵 시트와 함께
살려둘 이유가 하나 더 있는 셈이다.
