# 하드웨어 검증 체크리스트

PR #32 (`protect-logic`) 머지 전/후에 **실물 보드로 확인해야 하는 항목**들이다.

이 문서가 필요한 이유: PR의 변경 중 상당수가 데이터시트/TRM 대조와 회로 추론으로 결정된 값이고,
실제로 그렇게 동작하는지는 보드에 물려봐야만 알 수 있다. 특히 **BQ 핀 설정과 절연 경로는 코드만
읽어서는 맞는지 알 수 없다** — 핀 하나 잘못 잡혀 있으면 보호가 조용히 안 걸린다.

| 구분 | 항목 수 | 필요한 것 |
|---|---|---|
| 코드로 확인 가능 | 1 | ARM 툴체인 (Claude가 처리) |
| 벤치 필요 | 7 | BQStudio + 오실로스코프 + 전원/부하 |
| 차량/실주행 필요 | 4 | 조립된 팩 + CAN 로거 |

---

## 0. 사전 점검 — 이전 펌웨어로 인한 소자 손상 ⚠️

**다른 것보다 먼저 확인할 것.**

기존 `BQ769x2_Init()`은 TOP/BOT 분기가 없어서 **TOP의 DCHG/DDSG 핀을 푸시풀 출력(`0x22`)으로
설정**했다. 그런데 보고서 핀 표에 따르면 TOP의 31/32번에는 서미스터가 붙어 있다.
즉 이전 펌웨어를 올려서 돌린 적이 있다면 **IC가 서미스터를 출력으로 구동한 상태**였다.

- [ ] TOP 보드의 DCHG/DDSG 핀에 연결된 서미스터 저항값 측정 → 상온에서 정격(18 kΩ 계열)과 맞는가
- [ ] TOP BQ76972의 해당 핀이 살아 있는가 (아래 8번에서 온도가 정상 판독되면 OK)
- [ ] 이전 펌웨어를 실제로 플래시해서 전원을 넣은 적이 있는지 이력 확인

손상이 의심되면 서미스터/IC 교체를 먼저 하고 나머지 항목을 진행할 것.

---

## 1. 빌드 확인 — *Claude가 처리*

- [ ] `arm-none-eabi-gcc`로 전체 컴파일 + 링크, 경고 확인

```bash
winget install --id Arm.GnuArmEmbeddedToolchain --exact --accept-package-agreements --accept-source-agreements
```

설치만 해주면 나머지는 Claude가 돌린다. 오타·include 누락·타입 오류·`B_BMS_protect.c` 연결 여부가
여기서 걸러진다. **다만 이건 "컴파일이 된다"까지만 보증한다** — 아래 항목들은 대체하지 못한다.

---

## 2. 벤치 검증

### 2-1. BQ 레지스터 덤프 대조 (BQStudio)

PR에서 TOP에 새로 넣은 핀 설정 바이트 `0x2A`는 **TRM 표를 직접 읽은 값이 아니라 유도값**이다.
근거는 기존 `BOT.DFETOFF = 0x6A`에서 BOTHOFF 비트(`0x40`)를 뺀 것이고, ALERT 핀이 쓰는 옵션 비트와
같다는 점이다. 맞을 가능성이 높지만 확인 없이 믿으면 안 된다.

**방법:** BQStudio로 TOP / BOT에 각각 접속 → Data Memory → `.gg.csv` export →
`Core/Src/B_BMS_init.c` 상단의 `BQ_CFG_TOP` / `BQ_CFG_BOT`과 대조.

- [ ] `TOP.CFETOFF Pin Config` — CFETOFF 기능, Active-High, 입력으로 잡혀 있는가
- [ ] `TOP.DFETOFF Pin Config` — DFETOFF 기능이고 **BOTHOFF가 아닌가** (이게 핵심)
- [ ] `TOP.DCHG / DDSG Pin Config` — 서미스터(ADC 입력)로 잡혀 있는가
- [ ] `TOP.TS3 Config` — FET 온도(`0x0F`)로 잡혀 있는가
- [ ] `BOT.DFETOFF Pin Config` — BOTHOFF 유지되는가
- [ ] `BOT.DCHG / DDSG Pin Config` — 출력, Active-High 유지되는가
- [ ] `DA Configuration` — 양쪽 다 `0x06`인가

**불합격 시:** `B_BMS_init.c`의 `BQ_CFG_TOP` / `BQ_CFG_BOT` 해당 필드를 BQStudio가 보여주는
올바른 값으로 수정. 이 파일 상단 주석에 유도 근거를 적어뒀으니 같이 갱신할 것.

---

### 2-2. 선택적 차단 경로 실동작 ★ 가장 중요

이 PR의 존재 이유다. **"BOT의 충전 보호가 TOP의 CHG FET만 끄는가"** 를 실제로 확인한다.

**방법:** BQStudio(또는 MCU)에서 BOT에 서브커맨드를 보내 핀을 강제로 올린다.

| 보낼 것 | 대상 | 기대 결과 |
|---|---|---|
| `DCHG_HI` (0x2817) | BOT | TOP.CFETOFF가 High로 올라가고, **TOP FETStatus의 CHG=0, DSG는 1 유지** |
| `DCHG_LO` (0x2807) | BOT | 원복 |
| `DDSG_HI` (0x2818) | BOT | TOP.DFETOFF High, **TOP FETStatus의 DSG=0, CHG는 1 유지** |
| `DDSG_LO` (0x2808) | BOT | 원복 |

- [ ] `DCHG_HI` → TOP.CFETOFF 핀 전압 High (테스터/스코프로 직접 확인)
- [ ] `DCHG_HI` → TOP `FETStatus` CHG 비트 0
- [ ] **`DCHG_HI` → TOP `FETStatus` DSG 비트는 1 유지** ← 여기서 DSG까지 꺼지면 `TOP.DFETOFF`가
      아직 BOTHOFF로 잡혀 있는 것. 2-1로 돌아갈 것
- [ ] `DDSG_HI` → DSG만 0, CHG는 1 유지
- [ ] 각각 `_LO`로 원복되는가

**이 항목이 통과하면** 보고서의 신호 경로가 실제로 성립한다는 뜻이고, 이 PR의 1번 목적이 달성된 것이다.

> 참고: `DCHG_HI` / `DDSG_HI`가 디바이스 자체 보호 로직에 의해 다음 평가 주기에 덮어써질 수 있다.
> 강제가 유지되지 않으면 대신 실제 보호를 유발(예: 셀 하나를 COV 임계 위로)해서 같은 관측을 할 것.

---

### 2-3. BOTHOFF 경로 + 페일세이프

- [ ] MCU PA15를 Low→High → TOP CHG=0, DSG=0 (둘 다)
- [ ] MCU PA15를 High→Low → 둘 다 복귀
- [ ] **MCU 리셋 버튼을 누르는 동안** BOT_DFETOFF가 High로 뜨고 FET이 열리는가
      (오픈드레인 Hi-Z + 외부 풀업 페일세이프)
- [ ] 전원 인가 직후 — `LV_BMS_MAIN_RUN()`이 양쪽 칩 응답을 확인할 때까지 FET이 닫히지 않는가
      (BOTHOFF가 초기화 내내 유지되는지)
- [ ] 한쪽 보드의 I2C를 뽑은 채로 부팅 → FET이 끝까지 열려 있는가

---

### 2-4. G3VM 전파 지연 실측 ★ SCD 성립 여부

**이게 안 맞으면 SCD 보호가 서류상으로만 존재하게 된다.**

SCD는 450 µs로 설정돼 있지만 실제 차단 경로는
`BOT 내부 → DDSG 핀 → G3VM 광절연 → TOP.DFETOFF → 게이트 방전`이고,
G3VM 계열 PhotoMOS의 t_on/t_off는 보통 수백 µs~수 ms다. **광절연 소자가 응답시간을 지배한다.**

**방법:** 오실로스코프 2채널.
- CH1: `BOT.DDSG` 핀
- CH2: `TOP.DFETOFF` 핀 (여유 되면 CH3에 TOP DSG FET 게이트)

- [ ] `DDSG_HI` 인가 시점부터 TOP.DFETOFF가 문턱을 넘기까지의 지연 (t_on) 측정: ________ µs
- [ ] 게이트가 실제로 방전되어 FET이 꺼지기까지 총 시간 측정: ________ µs
- [ ] **총 차단 시간 = SCD Delay(450 µs) + BQ 반응 + 위 실측값** 을 계산: ________ µs

**판정 기준:** 보고서 5장의 SOA 계산 — `IPF067N20NM6ATMA1`은 140 V / 100 A에서 약 50 µs를 버틴다.
총 차단 시간이 이걸 넘으면 SCD만으로는 단락을 막지 못한다.

**불합격 시 (넘을 가능성이 높다):**
- SCD 임계(`SCDThreshold`, 현재 80 mV ≈ 200 A)를 낮춰 더 일찍 트립시키거나
- 단락 구간은 2차 보호기(4.45 V 트립) / 퓨즈가 담당한다고 **설계 문서에 명시**하고, SCD는 백업으로 격하
- 소프트웨어로 메울 수 있는 문제가 아니다

- [ ] 측정한 t_on을 `Core/Src/B_BMS_protect.c`의 `PATH_FAULT_CYCLES` 주석에 기록

---

### 2-5. 크로스체크 오탐 확인 (2-4와 직결)

`B_BMS_protect.c`는 "꺼져 있어야 하는데 3사이클(≈190 ms) 동안 안 꺼짐"을 절연 경로 고장으로 본다.
**G3VM 전파 지연이 190 ms보다 길면, 정상 보호 트립마다 `TRIP_CHG_PATH` 오탐이 난다.**

- [ ] 2-4에서 측정한 지연이 190 ms보다 충분히 작은가 (µs~ms 수준이면 문제없음)
- [ ] 실제 보호를 한 번 유발시킨 뒤 CAN `0x080` byte 3/4 (mismatch cycles)가 3 미만에 머무는가
- [ ] `0x080` byte 0 (TripReason)이 2 또는 3으로 뜨지 않는가

**불합격 시:** `B_BMS_protect.c`의 `PATH_FAULT_CYCLES`를 실측 지연의 2~3배로 올릴 것.

---

### 2-6. 차지펌프 / 게이트 구동 전압

보고서 TOP 47번 CP1은 "Charge Pump"인데, `FETOptions=0x2C` 주석은 "차지펌프 사용 안하기 위해
CHG/DSG 병렬"이라고 되어 있다. **둘이 모순이라 PR에서는 판단을 보류하고 기존 동작(OFF)을 유지했다.**

**방법:** FET이 ON인 상태에서 게이트-소스 전압 측정.

- [ ] TOP CHG FET의 V_GS 측정: ________ V
- [ ] TOP DSG FET의 V_GS 측정: ________ V

**판정 기준:** `IPF067N20NM6ATMA1`의 `R_DS(on)` 6.7 mΩ은 **V_GS = 10 V 기준**이다.
V_GS가 이보다 낮으면 R_DS(on)이 올라가고, 100 A 도통 시 발열이 급증한다.

**불합격 시:** `B_BMS_init.c`의 `BQ_CFG_TOP.chg_pump`를 `0x01`(또는 TRM 권장값)로 바꾸고
`FETOptions`의 CHG/DSG 병렬 비트를 재검토. 두 설정은 함께 봐야 한다.

- [ ] 100 A 도통 상태에서 FET 케이스 온도 확인 (열화상 or 서미스터)

---

### 2-7. 전압 단위 검증 (`DAConfiguration = 0x06`)

PR에서 `0x02`(밀리볼트) → `0x06`(센티볼트)로 바꿨다. TRM Table 13-15가 밀리볼트는 32 V 이하
응용에만 쓰라고 명시하는데, 칩당 16S는 최대 67.2 V라 **기존에는 32767에 포화**돼 있었다.

- [ ] CAN `0x041`(BOT) / `0x045`(TOP)의 `Stack_Voltage`를 멀티미터 실측값과 비교
      - 기대: mV 단위 일치 (예: 실측 59.2 V → 프레임 값 59200)
      - **이전 증상: 32767 근처에 고정**
- [ ] `Pack_Voltage`도 동일하게 확인
- [ ] 셀 전압 16개 합(CAN `0x048`–`0x04B`)과 `Stack_Voltage`가 오차 범위 안에서 일치하는가
      (`B_BMS_protect.c`의 `STACK_SUM_TOLERANCE_mV`가 1500 mV로 잡혀 있음)
- [ ] CAN `0x080` byte 5 (stack mismatch cycles)가 0을 유지하는가

**불합격 시:** `B_BMS_init.c`의 `DAConfiguration`과 `B_BMS.c`의 `stack_userV_to_mV()`는
**반드시 함께** 바꿔야 한다. 한쪽만 고치면 10배 틀어진다.

---

### 2-8. 온도 채널 (OTF 보호 소스)

기존에는 TS1/TS3/HDQ가 전부 셀 온도라 **OTF가 트립될 소스 자체가 없었다.** PR에서 TOP의 TS3을
FET 온도로 바꿨다.

- [ ] BQStudio에서 TOP의 `FET Temperature`가 실제 서미스터 값을 따라가는가 (헤어드라이어 등으로 가열)
- [ ] TOP의 DCHG/DDSG 핀 온도가 정상 판독되는가 (0번 항목의 손상 여부와 연결)
- [ ] CAN `0x055` (TOP 온도 프레임 1)의 FETTemp 필드가 상온에서 그럴듯한 값인가
- [ ] FET 온도를 OTF 임계(100 ℃)까지 올릴 수 있으면 실제 트립 확인 — 어려우면 임계를 임시로 낮춰 시험

---

## 3. 차량 / 실주행 검증

### 3-1. SOC 초기값

`B_BMS_soc.c`가 DASTATUS5의 `Battery Voltage Sum`을 잘못된 단위로 읽어서 **OCV 재보정 경로를 탈
때마다 SOC가 0%로 고정**되고 있었다 (부팅 직후 + 10분 무부하 이후).

- [ ] 부팅 후 CAN `0x043`의 SOC가 실제 셀 전압에 맞는 값인가 (**0이 아닌지**)
- [ ] 10분 이상 무부하 방치 후에도 SOC가 0으로 떨어지지 않는가
- [ ] 충방전 사이클 중 SOC가 단조롭게 변하는가

> `B_BMS_soc.c`의 OCV 테이블은 Molicel 데이터시트 **그래프를 눈으로 읽은 근사치**다.
> 실측 OCV 특성 데이터가 있으면 교체할 것 — 이건 이 PR과 별개 작업이다.

---

### 3-2. 소프트 백업 오탐

`B_BMS_protect.c`의 백업 임계는 BQ 임계보다 여유를 두고, BQ의 보호 지연(1 s / 5 s)보다 길게
기다리도록 카운터를 걸어놨다. 그래도 실주행 데이터로 확인이 필요하다.

| 항목 | 값 | BQ 임계 |
|---|---|---|
| `SOFT_CELL_UV_mV` | 2400 mV | CUV 2.53 V |
| `SOFT_CELL_OV_mV` | 4250 mV | COV 4.1998 V |
| `SOFT_TEMP_MAX_C` | 70 ℃ | OTD 65 ℃ |

- [ ] 만충~만방 사이클을 한 번 돌리며 CAN `0x080` byte 0이 **7(SOFT_CELLV) / 8(SOFT_TEMP)로 뜨지 않는가**
- [ ] 급가속·회생 구간에서도 오탐이 없는가
- [ ] 한여름 정차 조건(팬 100% 상태)에서 온도 백업이 걸리지 않는가

**오탐 발생 시:** 해당 임계를 더 벌리거나 `SOFT_*_FAULT_CYCLES`를 늘릴 것. 임계를 BQ 쪽으로
좁히는 방향은 금물이다 — 백업이 원본보다 먼저 걸리면 안 된다.

---

### 3-3. HWDF vs MCU 슬립

HWDF는 MCU가 60초간 통신하지 않으면 트립되어 CHG/DSG를 모두 연다. 그런데
`Enter_Sleep_Sequence()`는 ALERT가 올 때까지 무기한 잠든다. PR에서는 스위치
(`B_BMS_HWD_PROTECTION_ENABLE`)만 넣고 **기본값을 기존대로(활성) 유지**했다 — 안전 설정을
말없이 약화시키지 않기 위해서다.

- [ ] 무부하로 5분 이상 방치 → FET이 열리는가
- [ ] 열린다면 CAN `0x080` / SafetyStatusC bit1(HWDF)로 확인되는가
- [ ] MCU가 ALERT로 깨어나 스스로 복구하는가 (PR에서 `SFAlertMaskC`에 HWDF를 추가했다)
- [ ] 복구된다면 몇 초 걸리는가: ________ s

**결정해야 할 것:**
- 복구가 안 되거나 FET 개방 구간이 길면 → RTC Wake-up Timer 구현 (`.ioc` 변경 필요, **권장**)
- 또는 `B_BMS_HWD_PROTECTION_ENABLE`을 0으로 두고 대신 MCU IWDG를 켠다
  (MCU 행 → 리셋 → BOT_DFETOFF Hi-Z → 풀업 → BOTHOFF 페일세이프)

`B_BMS_init.c` 상단의 HWD 주석 블록에 두 방법의 근거를 정리해뒀다.

---

### 3-4. latch형 보호 복구 (OCDL / SCDL)

`OCDL`/`SCDL`은 자동 복구가 안 되고 명시적 복구 명령이 필요하다. 없으면 **한 번 걸린 뒤 팩이
영구 락아웃**된다. PR에서 `recover_latched_protections()`를 추가했다.

- [ ] OCD를 반복 유발해 OCDL을 latch시킬 것 (`OCDLLatchLimit` 2회)
- [ ] 조건 해제 후 약 5초 내에 자동 복구되는가
- [ ] SCDL도 동일하게 (`SCDLLatchLimit` 2회)
- [ ] 복구 명령이 무한 반복되지 않는가 (CAN `0x080`을 보며 확인)

---

## 4. 통과 후 정리

- [ ] 실측값들을 이 문서에 기록하고 커밋
- [ ] 값이 바뀐 항목은 `B_BMS_init.c` / `B_BMS_protect.c`의 해당 주석도 같이 갱신
- [ ] `CLAUDE.md`의 "FET cutoff path" 절에 G3VM 실측 지연을 반영
- [ ] PR #32의 체크박스 갱신

---

## 필요 장비

| 장비 | 용도 | 해당 항목 |
|---|---|---|
| BQStudio + EV2400 (또는 동등 인터페이스) | 레지스터 덤프/강제, 서브커맨드 전송 | 2-1, 2-2, 2-8 |
| 오실로스코프 (2채널 이상) | G3VM 전파 지연 실측 | 2-4 |
| 멀티미터 | 게이트 전압, 스택 전압 대조 | 2-3, 2-6, 2-7 |
| 가변 전원 + 전자부하 | 보호 유발, 전류 시험 | 2-2, 2-6, 3-4 |
| CAN 로거 | `0x080` 진단 프레임 관측 | 2-5, 2-7, 3-1~3-4 |
| 열화상 카메라 (있으면) | FET 발열 | 2-6 |
