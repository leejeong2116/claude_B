# EVM 테스트 항목

BQ76952EVM + STM32 보드로 **코드를 돌려서** 확인하는 항목들.

`HARDWARE_VERIFICATION.md`와 역할이 다르다:

| 문서 | 대상 | 필요한 것 |
|---|---|---|
| `HARDWARE_VERIFICATION.md` | **실제 팩/보드** — 절연 경로, 게이트 전압, 팩 전압 | 완성된 BMS 보드 |
| **이 문서** | **코드 동작** — 값이 맞게 읽히는지, 로직이 의도대로 도는지 | EVM 1~2대 |

EVM은 16S 셀 시뮬레이터로 전압을 임의로 만들 수 있어서, **실제 팩으로는 만들기 위험한 조건
(과전압·저전압·과온)을 안전하게 재현할 수 있다.** 코드 검증은 여기서 최대한 끝내는 게 좋다.

> ⚠️ EVM 1대만 있으면 TOP/BOT 중 한쪽만 테스트된다. 아래 §0 참조.

---

## 0. 시작 전 — EVM 대수에 따른 제약

이 펌웨어는 **BQ76972 2개(TOP/BOT)를 전제**로 짜여 있다. `LV_BMS_MAIN_RUN()`이 두 보드를 모두
초기화하고, 응답하지 않으면 BOTHOFF를 유지한 채 시작한다.

| EVM 대수 | 되는 것 | 안 되는 것 |
|---|---|---|
| **1대** | 단일 칩 읽기/쓰기, 보호 트립, 단위 검증, SOC | 크로스체크, 절연 경로, TOP/BOT 분기 |
| **2대** | 위 전부 + 감시자 로직 전부 | 실제 FET·G3VM 동작 (보드 필요) |

**1대만 있을 때**: 응답 없는 쪽 때문에 `TRIP_COMM`이 걸려 BOTHOFF가 계속 어서트된다.
이건 **정상 동작**이고, 오히려 §4-1 테스트가 저절로 되는 셈이다. 다른 항목을 보려면
`B_BMS_protect.c`의 `COMM_FAULT_CYCLES`를 임시로 크게 하거나 `BMS_Protect_Update()` 호출을
잠시 주석 처리할 것. **테스트 후 반드시 되돌릴 것.**

- [ ] EVM 대수 확인: ______ 대
- [ ] I2C 주소 확인 — 코드는 양쪽 다 `0x10`. EVM 기본값과 맞는가
- [ ] TOP은 `hi2c2`(PB13/PB14), BOT은 `hi2c4`(PB6/PB7) — 배선 확인
- [ ] EVM에 `RST_SHUT` / `ALERT` / `DFETOFF` 핀을 MCU와 연결했는가

---

## 1. 통신 기본

가장 먼저. 여기가 안 되면 아래가 전부 무의미하다.

- [ ] `LV_BMS_initOK`가 1이 되는가 (디버거 watch)
- [ ] `RX_CRC_Fail`이 0으로 유지되는가 — 증가하면 CRC 모드 설정 또는 배선 문제
- [ ] `I2C_HAL_Fail`이 0으로 유지되는가
- [ ] `BMS[TOP].comm_fail_cycles` / `BMS[BOT].comm_fail_cycles`가 0인가
- [ ] `DEVICE_NUMBER` 서브커맨드로 칩 ID를 읽어 BQ76972가 맞는지 확인

**CRC가 계속 깨지면**: `B_BMS_init.c`의 `CommType`(0x00 = I2C basic)과 EVM 설정을 대조.
`CRC_Mode`는 `B_BMS.h`에서 1로 하드코딩돼 있고 끄면 안 된다.

---

## 2. 레지스터 설정이 실제로 먹었는가 ★

`BQ769x2_Init()`은 `SET_CFGUPDATE` → 레지스터 쓰기 → `EXIT_CFGUPDATE` 순서인데,
**쓰기가 실패해도 코드가 알 수 없다** (`BQ769x2_SetRegister()`는 반환값이 없다).
BQStudio로 덤프해서 대조하는 게 유일한 확인 방법이다.

- [ ] BQStudio에서 Data Memory 덤프 (`.gg.csv`)
- [ ] `B_BMS_init.c`의 값과 전수 대조

특히 봐야 할 것:

| 레지스터 | 기대값 | 왜 |
|---|---|---|
| `DA Configuration` | **`0x06`** | 이게 `0x02`면 전압이 32767에 포화된다 |
| `CFETOFF Pin Config` | TOP `0x2A` / BOT `0x07` | **`0x2A`는 유도값이라 여기서 처음 확인된다** |
| `DFETOFF Pin Config` | TOP `0x2A` / BOT `0x6A` | TOP이 BOTHOFF면 안 됨 |
| `DCHG/DDSG Pin Config` | TOP `0x07` / BOT `0x22` | |
| `TS3 Config` | TOP `0x0F`(FET) / BOT `0x07`(셀) | OTF 보호의 유일한 소스 |
| `CHG/DSG FET Protections A/B/C` | `0x98/0xD5/0x56`, `0xE4/0xE6/0xE2` | 감시자가 같은 값을 가정한다 |
| `Enabled Protections A/B/C` | `0xFC/0xF7/0xF6` | |
| `Vcell Mode` | `0x0000` (16셀) | |

> ⚠️ `0x2A`가 틀린 값으로 판명되면 `B_BMS_init.c`의 `BQ_CFG_TOP`을 고치고 재빌드.
> 이게 이 펌웨어에서 **유일하게 근거가 약한 값**이다.

---

## 3. 측정값이 맞게 읽히는가

EVM 셀 시뮬레이터로 알고 있는 전압을 넣고 코드가 읽는 값과 비교한다.

### 3-1. 셀 전압

- [ ] 셀 시뮬레이터를 균일하게(예: 3.700 V) 설정
- [ ] `BMS[x].CellVoltage[0..15]`가 3700 ± 오차로 읽히는가 (단위 mV)
- [ ] CAN `0x048`~`0x04B`(BOT) / `0x051`~`0x054`(TOP)에 같은 값이 실리는가
- [ ] 셀 하나만 다르게(예: 3.500 V) → `MinCellVolatge`가 따라 내려가는가

### 3-2. 스택 전압 ★ (이번에 고친 부분)

16셀 × 3.700 V = **59.2 V** 기준.

- [ ] `BMS[x].Stack_Voltage` ≈ **59200** (mV)
- [ ] **32767 근처에 붙어 있으면 `DAConfiguration`이 안 먹은 것** → §2로
- [ ] `Battery_Voltage_Sum` × 10 ÷ 16 ≈ 3700 인가
- [ ] CAN `0x041`/`0x045`의 값도 동일한가

> 이전 코드에서는 여기가 **32767에 포화**돼 있었다. 이 항목이 이번 수정의 핵심 검증이다.

### 3-3. 전류 (BOT만)

TOP은 SRP/SRN 미사용이라 항상 0이 정상이다.

- [ ] 션트에 알고 있는 전류를 흘리고 `BMS[BOT].Pack_Current` 확인 (단위 **10 mA**)
- [ ] **충전 방향이 양수인가** (`OCD3Threshold`가 음수인 것과 일관)
- [ ] `BMS[TOP].Pack_Current`가 sync 후 BOT 값과 같아지는가 (`BMS_SyncSharedHardwareData`)
- [ ] CAN `0x046`(TOP)의 전류 필드는 **0**인가 (의도된 동작)
- [ ] `CC1_Current` / `CC3_Current`도 그럴듯한가

### 3-4. 온도

- [ ] TS1/TS3/HDQ에 서미스터(또는 저항) 연결 → 상온에서 25 ℃ 근처인가
- [ ] **TOP의 `FET_Temp`가 실제로 변하는가** (TS3 = FET 온도로 바꾼 것의 검증)
- [ ] 저항을 바꿔 온도를 올렸을 때 값이 따라오는가
- [ ] `Max_Cell_Temp` / `Min_Cell_Temp` / `Avg_Cell_Temp`가 그럴듯한가

---

## 4. 보호 로직 — EVM에서 안전하게 재현 가능한 것들 ★

셀 시뮬레이터로 전압을 움직이면 실제 셀 없이 보호를 트립시킬 수 있다.

### 4-1. COV (과전압) — CHG만 꺼져야 함

- [ ] 셀 하나를 **4.25 V**로 올림 (임계 4.1998 V, 지연 1 s)
- [ ] 1초 후 `SafetyStatusA` bit3(COV)가 서는가
- [ ] `BMS[TOP].CHG` → 0, **`BMS[TOP].DSG`는 1 유지** ← 선택적 차단 확인
- [ ] CAN `0x05A`/`0x06D`에 값이 실리는가
- [ ] CAN `0x080` byte 1의 b2(CHG 기대=OFF)가 서고 b4(CHG 실측=ON)는 꺼지는가
- [ ] 전압을 4.0 V로 내리면 복구되는가 (히스테리시스 101.2 mV)

### 4-2. CUV (저전압) — DSG만 꺼져야 함

- [ ] 셀 하나를 **2.4 V**로 내림 (임계 2.53 V, 지연 1 s)
- [ ] `SafetyStatusA` bit2(CUV), `BMS[TOP].DSG` → 0, **`CHG`는 1 유지**
- [ ] 전압 복구 시 되돌아오는가

### 4-3. 소프트 백업이 BQ보다 먼저 걸리지 않는가 ★

**감시자가 BQ를 앞지르면 안 된다.** 4-1/4-2를 하는 동안 계속 확인:

- [ ] CAN `0x080` byte 0이 **7(SOFT_CELLV)로 뜨지 않는가**
- [ ] BQ가 먼저 트립하면 `soft_*_cycles` 카운터가 0으로 유지되는가
  (감시자는 `chg/dsg_exp_off`가 서면 소프트 카운터를 지운다)

**만약 7이 뜬다면**: `SOFT_CELLV_FAULT_CYCLES`가 BQ 지연보다 짧은 것.
`BMS_LOOP_PERIOD_MS_EST`를 실측값으로 고칠 것 (§6).

### 4-4. 과온

- [ ] 서미스터 저항을 바꿔 **OTC(60 ℃)** 재현 → CHG만 OFF
- [ ] **OTD(65 ℃)** → DSG만 OFF
- [ ] **OTF(100 ℃)** — TOP의 TS3에서만 가능 → **CHG/DSG 둘 다 OFF**
      ← TS3을 FET 온도로 바꾸기 전에는 아예 트립되지 않던 항목

### 4-5. OCDL / SCDL 자동 복구 ★

한 번 걸리면 명시적 복구 명령 없이는 영구 락아웃되는 보호다.

- [ ] OCD를 **2회 이상** 반복 유발 (`OCDLLatchLimit` = 2) → `SafetyStatusC` bit5(OCDL)
- [ ] 조건 해제 후 **약 5초 내** 자동 복구되는가 (`recover_latched_protections()`)
- [ ] 복구 명령이 무한 반복되지 않는가 (I2C 트래픽으로 확인)
- [ ] SCDL도 동일하게

### 4-6. PF (영구 고장) — latch 확인

- [ ] `SOV`(4.3 V) 또는 `SUV`(2.2 V)를 재현
- [ ] `PF_ProtectionsTriggered`가 서는가
- [ ] CAN `0x080` byte 0 = **5(PF)**, byte 1 b1(latch) = **1**
- [ ] **조건을 해제해도 BOTHOFF가 풀리지 않는가** ← PF는 자동 복구 금지
- [ ] `PF_RESET`이 자동으로 나가지 않는가 (I2C 트래픽 확인) ← 나가면 심각한 버그

---

## 5. 감시자 로직

### 5-1. 통신 두절 (TRIP_COMM)

- [ ] 동작 중 한쪽 EVM의 I2C를 뽑음
- [ ] 약 1.5초 후 CAN `0x080` byte 0 = **4(COMM)**, byte 1 b0(BOTHOFF) = 1
- [ ] BOT_DFETOFF 핀이 High로 올라가는가 (테스터)
- [ ] BLUE LED 점등
- [ ] 다시 꽂으면 약 5초 후 자동 복구되는가
- [ ] **3회 반복하면 latch되는가** (`BOTHOFF_RETRY_MAX` = 3) → byte 0 = 9

### 5-2. 초기화 중 페일세이프

- [ ] 한쪽 EVM을 뽑은 채로 전원 인가
- [ ] `LV_BMS_MAIN_RUN()`이 BOTHOFF를 **해제하지 않고** 끝나는가
- [ ] BOT_DFETOFF가 부팅 내내 High인가

### 5-3. 감시자 개입 중 슬립 금지

- [ ] 무부하(전류 0) + 통신 두절 상태를 만듦
- [ ] MCU가 **잠들지 않고** 계속 도는가 (CAN 프레임이 계속 나오는지로 확인)
- [ ] 통신 복구 후에는 정상적으로 슬립에 드는가

### 5-4. 크로스체크 (EVM에서는 부분 확인만)

EVM에는 실제 FET이 없어서 `FETStatus`가 실제 게이트를 반영하지 않는다.
완전한 검증은 `HARDWARE_VERIFICATION.md` 2-2에서.

- [ ] 정상 보호 트립 중 CAN `0x080` byte 3/4(mismatch cycles)가 임계 미만에 머무는가
- [ ] byte 0이 **2/3(CHG_PATH/DSG_PATH)로 뜨지 않는가**

---

## 6. 루프 주기 실측 ★ (감시자 임계의 근거)

`B_BMS_protect.c`의 모든 임계가 `BMS_LOOP_PERIOD_MS_EST`(현재 **300 ms 추정**)에서 유도된다.
**실측하지 않으면 모든 시간 임계가 추정 위에 서 있다.**

측정 방법 (셋 중 아무거나):
- 남는 GPIO를 루프 시작/끝에 토글하고 오실로스코프로 주기 측정
- CAN `0x042` 프레임의 도착 간격을 로거에서 측정 ← **가장 쉬움**
- 디버거에서 `LV_BMS_running` 증가 속도 측정

- [ ] 실측 루프 주기: __________ ms
- [ ] `BMS_LOOP_PERIOD_MS_EST`를 실측값으로 수정 후 재빌드
- [ ] 부하 있을 때 / 무부하일 때 주기가 다른가: 부하 ______ / 무부하 ______

**주기가 예상보다 훨씬 길면** 개선 여지가 있다 — `DirectCommands()`가 호출마다
`delayUS(2000)`을 거는데, 보드당 40회면 그것만 80 ms다. TI 예제에서 그대로 가져온 값이라
실제로 필요한 최소 지연은 더 짧을 수 있다. 줄이면 응답성이 크게 좋아진다.

- [ ] `delayUS(2000)`을 1000 / 500으로 줄여보고 `RX_CRC_Fail`이 늘지 않는지 확인
      (늘면 되돌릴 것 — 통신 안정성이 우선)

---

## 7. SOC

- [ ] 셀 시뮬레이터를 3.700 V로 두고 부팅 → SOC가 **0이 아닌** 그럴듯한 값인가
      (OCV 테이블상 3700 mV ≈ 60 % 부근)
- [ ] 3.300 V로 두고 부팅 → 낮은 SOC가 나오는가
- [ ] 4.100 V → 높은 SOC
- [ ] CAN `0x043`(BOT) / `0x047`(TOP)에 같은 값이 실리는가
- [ ] 션트에 전류를 흘려 `AccumulatedCharge_Int`가 변하고 SOC가 따라 움직이는가

> 이전 코드에서는 부팅 SOC가 **항상 0**이었다. 이 항목이 그 수정의 검증이다.
> OCV 테이블 중간 구간은 실측이 아니라 근사치이므로 (§`CLAUDE.md` 참조)
> "정확히 60 %"를 기대하지 말고 **단조성과 대략적 범위**만 볼 것.

---

## 8. CAN

- [ ] **비트레이트 확인 — 계산상 500 kbit/s**
      (`NominalPrescaler` 10, `TimeSeg1` 12, `TimeSeg2` 3 → 16 tq, FDCAN 클럭 PLL1Q 80 MHz)
      차량 나머지 CAN 버스와 맞는지 반드시 확인
- [ ] 정상 동작 시 매 사이클 프레임 9개가 나오는가
      (`0x040`~`0x043`, `0x044`~`0x047`, `0x080`)
- [ ] 바이트 순서가 **big-endian**인가
- [ ] `0x080` 진단 프레임의 각 필드가 §4/§5에서 의도대로 변하는가

**알려진 제약 (동작 확인 불필요, 인지만):**
- `AutoRetransmission = DISABLE` — 프레임을 놓쳐도 재전송 안 함. 텔레메트리라 다음 주기가 덮음
- `bms_can_send_len()`이 5 ms 타임아웃 후 프레임을 포기 — 버스오프에도 루프가 멈추지 않게 하려는 의도
- **CAN 수신은 동작하지 않는다** — §9 참조

---

## 9. 검토만 필요한 것 (테스트 대상 아님)

코드를 읽다 발견한, 지금 고칠지 판단이 필요한 항목들.

| # | 내용 | 판단 필요 |
|---|---|---|
| R1 | **CAN 수신 경로가 죽어 있다.** `MX_FDCAN1_Init()`이 `HAL_FDCAN_ActivateNotification(RX_FIFO0)`을 부르는데 **IRQ 핸들러도, NVIC 활성화도, `HAL_FDCAN_RxFifo0Callback`도 없다.** 지금은 NVIC가 꺼져 있어 무해하지만, CAN으로 BMS에 명령(latch 해제, 테스트 데이터 요청 등)을 보낼 수 없다 | 수신이 필요한가? |
| R2 | **`PWR_EN`(PA0)이 초기화 때 Low로 쓰이고 그 뒤 아무도 안 건드린다.** 이 핀이 어떤 전원을 게이팅한다면 영영 꺼져 있는 것 | 이 핀의 용도 확인 |
| R3 | **`BQ769x2_LOWV_SHUTDOWN()` / `Hard_Shutdown()` / `Wake_Up()`이 어디서도 호출되지 않는다** | 쓸 건가, 지울 건가 |
| R4 | **테스트 데이터 CAN 경로 전체가 미사용.** `BMS_CAN_SendTestData()`와 `BMS_Test_ReadAll()`이 main에서 호출되지 않는다. 셀 전압 16개·스냅샷·CB 상태가 전부 안 나감 | 테스트 중엔 켤 것인가 |
| R5 | `BMS_Test_ReadAll()`은 `BMS_SOC_Update()`도, `comm_fail_this_cycle` 초기화도 하지 않는다. 이 경로를 쓰려면 보완 필요 | R4와 함께 |
| R6 | `LV_STAT()`의 통신 오류 판정이 아직 전역 카운터를 본다. 보드별 `comm_fail_cycles`가 생겼으니 그쪽이 정확하다 | 사소 |

---

## 10. 테스트 후

- [ ] 실측값을 이 문서에 기입하고 커밋
- [ ] `BMS_LOOP_PERIOD_MS_EST`를 실측값으로 갱신
- [ ] §2에서 틀린 레지스터가 나왔으면 `B_BMS_init.c` 수정
- [ ] §9의 R1~R6 판단 결과 기록
- [ ] 임시로 바꾼 값(§0의 `COMM_FAULT_CYCLES` 등) **원복 확인**
