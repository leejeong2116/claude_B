# 2027 BWSC 규정 대조

근거 문서: `규정/2027-BWSC-Event-Regulations-V1.0-Published-07052026.pdf` (V1.0, 2026-05-07 공표)
셀 규격: `datasheet/(삼성셀) Samsung-INR21700-50S datasheet.pdf`
팩 구성: 32S5P (`CLAUDE.md`)

> 이 문서는 **펌웨어가 규정을 지키는지**만 다룬다. 차체·배선·박스 구조 등은 범위 밖이다.
> 판정은 규정 원문과 셀 데이터시트 대조 결과이며, **최종 판단은 certifying engineer**의 몫이다
> (규정 2.5.6이 요구).

---

## 1. 규정 2.5.5 / 2.5.6 — 셀 운용 범위 자동 제한 ★ 핵심 조항

> **2.5.5** Electrochemical cells must not, **at any time**, be operated outside of the operating
> ranges for voltage, current and temperature specified by the manufacturer.
>
> **2.5.6** The competition vehicle must **automatically prevent** electrochemical cells from being
> operated outside the operating ranges … Teams must provide **endorsement by their certifying
> engineer** that an adequate and effective automatic battery management system has been designed
> and implemented so that fault conditions will be managed safely.

규정 2.5.5가 제출을 요구하는 7개 항목을 그대로 표로 만들었다. **이 표가 곧 2.5.6의 증빙 자료**다.

| # | 규정이 요구하는 항목 | 50S 데이터시트 | BQ 설정 (`B_BMS_init.c`) | 셀당 환산 | 판정 |
|---|---|---|---|---|---|
| 1 | minimum operating cell voltage | **2.5 V** (3.9 방전 컷오프) | `CUVThreshold` 50 → 2.53 V | 2.53 V | ✅ 30 mV 여유 |
| 2 | maximum operating cell voltage | **4.20 V** (3.5 정격 충전) | `COVThreshold` 83 → 4.1998 V | 4.20 V | ✅ 경계 일치 |
| 3 | maximum discharge current | **25 A** 연속 / 45 A (80 ℃ 컷 조건) | `OCD2` 125 A | **25 A** | ✅ 정격과 일치 |
| 4 | **maximum charge current** | **6 A 연속** / 10 A (step charge) | `OCC` 50 A | **10 A** | 🔴 **§2-1 참조** |
| 5 | maximum temperature while discharging | 표면 **80 ℃** 상한, 복귀 < 60 ℃ | `OTD` 65 ℃ / 복귀 60 ℃ | — | 🟡 **§2-2 참조** |
| 6 | minimum temperature while charging | **0 ℃** (3.14 Ambient) | `UTC` 0 ℃ / 복귀 5 ℃ | — | 🟡 **§2-3 참조** |
| 7 | maximum temperature while charging | **60 ℃**, 재충전 권장 < 45 ℃ | `OTC` 60 ℃ / 복귀 **55 ℃** | — | 🟡 **§2-4 참조** |

**전압·방전전류(1~3)는 문제없다.** 셀 데이터시트 값과 BQ 설정이 정확히 맞는다.

> 📌 데이터시트 Note (*1) 원문: *"Protection function of the battery pack **should be set within
> the specified** charge, discharge and temperature range in Cell Specification."*
> → 제조사가 명시적으로 "보호 설정을 규격 범위 안에 두라"고 요구한다. 규정 2.5.5와 같은 방향.

---

## 2. 조정이 필요한 항목

### 2-1. 🔴 충전 전류 — `OCC` 50 A는 연속 정격의 1.67배

데이터시트 3.8은 충전 전류를 **두 가지**로 규정한다:

| 항목 | 셀당 | 5P 팩 |
|---|---|---|
| **연속 충전 (continuous charge)** | **6 A** | **30 A** |
| step charge | 10 A | 50 A |

현재 `OCC = 50 A`는 **step charge 한도**에 맞춰져 있다. `step charge`는 충전 초반 짧은 구간에만
높은 전류를 쓰는 프로파일이지 지속 정격이 아니므로, **30~50 A 구간의 지속 충전은 규격 초과인데도
보호가 걸리지 않는다.** 규정 2.5.5의 *"at any time"* 과 2.5.6의 *"automatically prevent"* 에 걸린다.

**권고: `OCC`를 30 A로 낮추고, 동시에 모터 컨트롤러의 회생 전류 상한도 30 A로 건다.**

```
OCCThreshold = 30 A x 0.4 mOhm = 12 mV -> 12 / 2 = 6 = 0x06   (현재 0x0A)
```

> ⚠️ **이전에 나는 "OCC는 50 A로 두고 컨트롤러에서만 제한하라"고 권고했다. 규정을 보고 그 권고를
> 바꾼다.** 그때 근거는 "회생 중 CHG FET을 끊으면 DC 버스가 치솟아 컨트롤러가 위험하다"였고,
> 그 우려 자체는 여전히 유효하다. 해소 방법은 **둘을 같이 하는 것**이다 —
> 컨트롤러 회생 상한을 30 A로 걸면 OCC 30 A는 정상 운전에서 절대 트립하지 않고,
> 순수하게 백스톱으로만 남는다. 그러면 규정도 지키고 오작동 위험도 없다.
>
> `SOCCThreshold`(PF, 70 A)와의 순서는 30 A < 70 A 로 그대로 유지된다.

- [ ] 모터 컨트롤러 최대 회생 전류를 30 A 이하로 설정했는지 확인
- [ ] 확인 후 `OCCThreshold`를 `0x06`으로 변경
- [ ] MPPT 출력 상한도 30 A 이하인지 확인 (어레이는 ~9 A라 여유)

### 2-2. 🟡 방전 과온 — 규격 위반은 아니지만 서미스터 위치가 관건

데이터시트에 온도 규격이 **세 줄**로 나뉘어 있어 혼동하기 쉽다:

| 항목 | 충전 | 방전 |
|---|---|---|
| 3.14 (**Ambient**) | 0 ~ 60 ℃ | −20 ~ 60 ℃ |
| 3.15 (**Surface**) | 0 ~ 60 ℃ (재충전 권장 < 45 ℃) | −20 ~ **80 ℃** (재방전 **필수** < 60 ℃) |
| 3.16 (안전인증용) | −10 ~ 60 ℃ | — |

서미스터는 셀 **표면**에 붙으므로 3.15가 기준이다. 현재 `OTD = 65 ℃`는 표면 상한 80 ℃ 안이라
**규격 위반이 아니다.** ✅

다만 데이터시트 Note (*2)에 조건이 붙어 있다:

> *"Discharge OTP should not be over 80℃. Protection set should be based on the location of the
> cell surface with the **highest temp increase part** of the battery pack."*

→ **서미스터가 팩에서 가장 뜨거워지는 위치에 있어야** 이 설정이 유효하다. 엉뚱한 곳에 붙어 있으면
65 ℃로 읽힐 때 실제 최고점은 80 ℃를 넘을 수 있다.

- [ ] 서미스터가 팩 내 최고온 지점에 배치되어 있는지 확인 (열화상 권장)
- [ ] `OTDRecovery` 60 ℃ → **55 ℃ 정도로 낮추는 것 검토**
      (규격은 *"must re-discharge release **< 60℃**"* 인데 현재 복귀가 **정확히 60**이라 경계에 걸침)

### 2-3. 🟡 충전 저온 — 임계가 규격 경계와 정확히 같다

데이터시트 충전 하한 **0 ℃**, `UTCThreshold` 도 **0 ℃**. 즉 여유가 0이다.
서미스터 오차·자기발열·측정 지연을 감안하면 실제 셀이 −1 ℃일 때 0 ℃로 읽혀 충전이 허용될 수 있다.

리튬이온의 저온 충전은 **리튬 플레이팅**으로 직결되는 항목이라 여유를 두는 게 맞다.

- [ ] `UTCThreshold` 0 ℃ → **+2 ~ +3 ℃** 로 상향 검토 (`0x02` / `0x03`)
- [ ] `UTCRecovery` 5 ℃ → 임계보다 충분히 위로 (예: 8 ℃)

### 2-4. 🟡 충전 과온 복귀 — 제조사 권장(45 ℃)보다 높다

`OTCRecovery = 55 ℃`. 데이터시트 3.15는 *"recommended recharge release **< 45℃**"* 라고 적고 있고,
코드 주석도 이미 이 사실을 알고 있다 (`// 셀 권장 재충전 온도 45`).

"권장(recommended)"이라 **엄밀한 운용 범위 위반은 아니지만**, 규정 2.5.6이 요구하는
certifying engineer 엔도스먼트에서 "왜 제조사 권장을 안 따랐나"를 설명해야 할 수 있다.

- [ ] `OTCRecovery` 55 ℃ → **45 ℃** 로 낮출지 결정 (팬이 있으므로 복귀 지연 부담은 크지 않음)

---

## 3. 규정 2.28 — Safe state (펌웨어 범위 밖이지만 반드시 확인)

> **2.28.1** … there must be a **contactor in series with the negative pole** of the battery and a
> **second contactor in series with the positive pole**, in each energy storage pack. …
> **MOSFETS and other semiconductor devices are not considered to offer galvanic isolation.**

**이 BMS의 차단 수단은 전부 MOSFET이다** (CHG/DSG FET, `IPF067N20NM6ATMA1` 10병렬).
규정상 **MOSFET은 safe state를 만족하지 못한다.** 팩마다 **+극과 −극에 각각 컨택터**가 필요하다.

> **2.28.3** … must be **fail-safe**; if an electrical activation mechanism fails, the competition
> vehicle must automatically and immediately place itself into safe state …
>
> **2.28.4** … must be designed in such a way that **failure of a semiconductor device or devices,
> no matter how improbable, cannot** prevent the competition vehicle from entering safe state …

→ 이 조항은 **safe state 회로에 MCU가 끼면 안 된다**는 뜻으로 읽힌다.
따라서 **현재 펌웨어에 컨택터 제어 출력이 없는 것은 오히려 규정에 부합한다.**
컨택터는 드라이버 스위치·외부 비상 스위치에서 **직결 하드와이어**로 구동되어야 한다.

**역할 분담을 이렇게 이해하면 된다:**

| 계층 | 담당 | 대상 |
|---|---|---|
| 상시 보호 (2.5.6) | **BQ MOSFET + MCU 감시자** | 과전압·저전압·과전류·과온 |
| 비상 차단 (2.28) | **컨택터 2개/팩, 하드와이어** | 드라이버 스위치, 외부 비상 스위치 |

- [ ] 팩마다 **+극·−극 컨택터 2개**가 있는지 확인 (보고서의 "Relay"가 이것인지, 몇 개인지)
- [ ] 컨택터 구동이 **MCU를 거치지 않는** 하드와이어인지 확인
- [ ] 활성화 실패 시 자동으로 safe state가 되는 fail-safe 구조인지 (2.28.3)
- [ ] 컨택터 정격 데이터시트 확보 (규정이 제출 요구)

> 참고: `PWR_EN`(PA0)이 LTC3895 `RUN`에 직결돼 있는데 (`STM32CUBEMX_TODO.md` §4-1),
> 만약 이 핀을 safe state 경로에 쓸 생각이라면 **2.28.4 위반**이다. 반도체(MCU) 고장이
> safe state 진입을 막으면 안 되기 때문이다. `PWR_EN`은 어디까지나 자체 전원 시퀀싱용으로만 쓸 것.

---

## 4. 규정 2.27 — 절연 감시 / 퓨즈

### 2.27.9 절연 감시 장치 🔴 펌웨어에 없음

> A system must be implemented to **monitor the isolation** of the competition vehicle's chassis and
> body … This system must take the form of a **permanently connected electronic device**.

**이 BMS는 절연 저항을 측정하지 않는다.** 별도의 IMD(Insulation Monitoring Device)가 필요하거나,
BMS에 기능을 추가해야 한다.

- [ ] 차량에 별도 IMD가 있는지 확인
- [ ] 없다면 **누가 만들 것인지** 결정 (BMS에 넣을지, 독립 보드로 갈지)
- [ ] BMS에 넣는다면 CAN으로 절연 저항값을 내보내는 프레임 추가 필요

### 2.27.10 퓨즈 ✅ 하드웨어 (펌웨어 무관)

> A **fuse or circuit breaker** … must be mounted in or on each energy storage pack. Additionally,
> the **dc interrupting current** capacity … must be able to interrupt the maximum possible
> short-circuit current.

BQ의 퓨즈 구동 기능(`FUSE` 핀)은 **의도적으로 비활성**이다
(`ProtectionConfiguration = 0x0602`, 보고서 핀 표 *"FUSE 사용 안함"*). 규정이 요구하는 것은
물리 퓨즈이므로 이는 문제되지 않는다.

- [ ] 팩마다 퓨즈/차단기가 장착되어 있는지
- [ ] **DC 차단 정격** 데이터시트 확보 (AC용 퓨즈는 인정 안 됨 — 규정이 명시)

---

## 5. 정리 — 펌웨어에 새로 만들어야 하는 코드

| # | 항목 | 규정 | 코드 작업 |
|---|---|---|---|
| 1 | `OCCThreshold` 50 A → **30 A** (`0x0A` → `0x06`) | 2.5.5 / 2.5.6 | **한 줄.** 단 컨트롤러 회생 상한 확인이 선행 |
| 2 | `UTCThreshold` 0 ℃ → +2~3 ℃, 복귀 상향 | 2.5.5 (여유) | 두 줄 |
| 3 | `OTDRecovery` 60 → 55 ℃ | 데이터시트 *"< 60℃"* | 한 줄 |
| 4 | `OTCRecovery` 55 → 45 ℃ | 데이터시트 권장 | 한 줄 |
| 5 | **절연 감시** | **2.27.9** | **신규 — 하드웨어부터 결정 필요** |

**1~4는 전부 `B_BMS_init.c`의 상수 변경이고 새 로직이 필요 없다.** 다만 보호 임계값이라
임의로 바꾸지 않았다 — 특히 1번은 드라이브트레인과 얽혀 있어 컨트롤러 설정 확인이 먼저다.

**5번만 진짜 신규 개발**이고, 그마저도 BMS에 넣을지 독립 IMD로 갈지 결정이 선행한다.

### 규정 위반이 아닌 것 (확인 완료)

- 전압 보호 (CUV/COV) — 셀 규격과 정확히 일치
- 방전 전류 보호 (OCD1/OCD2/OCD3/SCD) — 연속 25 A / 순간 45 A 규격 안
- 방전 과온 (OTD 65 ℃) — 표면 규격 80 ℃ 안 (단 서미스터 위치 확인 필요)
- MOSFET 기반 상시 보호 — 2.5.6은 만족. 2.28은 별도 컨택터의 몫
- 퓨즈 기능 비활성 — 규정은 물리 퓨즈를 요구

---

## 6. 스크루티니어링 대비

규정 2.5.6은 **certifying engineer의 엔도스먼트**를 요구한다. 제출 자료로 쓸 수 있는 것:

| 자료 | 파일 |
|---|---|
| 셀 규격 ↔ BMS 설정 대조표 | **이 문서 §1** |
| BQ 레지스터 실제 설정값 덤프 | `EVM_TEST.md` §2 (BQStudio `.gg.csv`) |
| 보호 동작 실증 (COV/CUV/과온 트립) | `EVM_TEST.md` §4 |
| 차단 경로 실증 | `HARDWARE_VERIFICATION.md` §2-2 |
| 고장 시 거동 (통신두절·PF latch) | `EVM_TEST.md` §5 |

§1 표의 7개 항목이 규정 2.5.5가 요구하는 항목과 **1:1로 대응**하도록 만들었으므로,
그대로 제출 자료로 쓸 수 있다.
