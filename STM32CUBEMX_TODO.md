# STM32CubeIDE / CubeMX 작업 목록

CubeMX(`.ioc`)를 열어서 해야 할 일과, 그때 주의할 것들.

> **핵심**: 이 프로젝트에는 CubeMX가 만들지 않은 손으로 쓴 파일이 여럿 있다.
> 재생성할 때 그것들이 어떻게 되는지 §1을 먼저 읽을 것.

---

## 1. 재생성하면 어떻게 되나 (먼저 읽을 것)

현재 `.ioc`의 코드 생성 설정:

| 설정 | 값 | 의미 |
|---|---|---|
| `KeepUserCode` | **true** | `/* USER CODE BEGIN */ ~ END */` 안은 보존된다 |
| `CoupleFile` | true | 주변장치마다 `.c/.h` 쌍으로 생성 (`gpio.c`, `i2c.c`, `tim.c` …) |
| `DeletePrevious` | **true** | 더 이상 필요없다고 판단된 **생성 파일을 지운다** |
| `BackupPrevious` | **false** | **백업을 남기지 않는다** |

### 안전한 파일 (CubeMX가 만든 적 없음 → 건드리지 않음)

```
Core/Src/B_BMS.c              Core/Inc/B_BMS.h
Core/Src/B_BMS_init.c         Core/Inc/B_BMS_init.h
Core/Src/B_BMS_protect.c      Core/Inc/B_BMS_protect.h
Core/Src/B_BMS_soc.c          Core/Inc/B_BMS_soc.h
Core/Src/B_BMS_power_mode.c   Core/Inc/B_BMS_power_mode.h
Core/Src/B_BMS_rtc.c          Core/Inc/B_BMS_rtc.h
Core/Src/B_TEST_BMS.c         Core/Inc/B_TEST_BMS.h
Core/Src/B_TEST_BMS_can.c     Core/Inc/B_BMS_cmd.h
```

### 위험한 파일 (CubeMX가 덮어씀 — USER CODE 블록 밖은 사라진다)

| 파일 | 이 프로젝트에서 손댄 부분 |
|---|---|
| `Core/Src/main.c` | `delayUS()`, `LV_STAT()`, while 루프 호출들 — **전부 USER CODE 안**이라 안전 |
| `Core/Src/stm32u5xx_it.c` | `HAL_GPIO_EXTI_Callback()`, **`RTC_IRQHandler()`** — USER CODE 1 안이라 안전 |
| `Core/Src/gpio.c` | 손댄 것 없음 |
| `Core/Src/tim.c` `i2c.c` `fdcan.c` | 손댄 것 없음 |
| `Core/Inc/stm32u5xx_hal_conf.h` | 모듈 활성화 매크로 — CubeMX가 관리 |

**재생성 전에 반드시 커밋해 둘 것.** `BackupPrevious=false`라 CubeMX는 백업을 안 만든다.
git이 유일한 안전망이다.

---

## 2. 지금 `.ioc` 상태 (사실)

**활성화된 주변장치**

```
CORTEX_M33_NS, DEBUG, FDCAN1, I2C2, I2C4, ICACHE,
LPBAM, LPBAMQUEUE, MEMORYMAP, NVIC, PWR, RCC, SYS, TIM2
```

**복사되어 있는 HAL 모듈** (활성화된 것만 복사된다)

```
def  dma  fdcan  flash  gpio  gtzc  i2c  icache  pwr  rcc  tim
```

→ **`rtc`가 없다.** 그래서 지금 `B_BMS_rtc.c`가 CMSIS 레지스터 직접 접근으로 되어 있다.

**클럭**

| 항목 | `.ioc` 값 |
|---|---|
| SYSCLK 소스 | MSI (PLL 경유) |
| MSI | 48 MHz (`RCC_MSIRANGE_0`) |
| **HSE_VALUE** | **8,000,000** ← §3-2 참조 |
| LSI | 32,000 |
| **LSE** | **설정 없음** ← §3-1 참조 |

**NVIC에 등록된 인터럽트**

```
BusFault  DebugMonitor  EXTI5  EXTI9  HardFault
MemoryManagement  NonMaskableInt  PendSV  SVCall  SysTick  UsageFault
```

→ **FDCAN 없음**(§3-3), **RTC 없음**(§3-1).

**GPIO/타이머 (확인됨, 손댈 것 없음)**

| 핀 | 신호 | 비고 |
|---|---|---|
| PA0 | `GPIO_Output` / label `PWR_EN` | §4-1 |
| PA1 | `S_TIM2_CH2` | FAN1 |
| PA2 | `S_TIM2_CH3` | FAN2 — `.ioc`에 이미 있음 |

---

## 3. 해야 할 일

### 3-1. RTC + LSE 활성화 ★ 우선순위 1

**왜**: 지금 `B_BMS_rtc.c`가 레지스터 직접 접근이다. 동작은 하지만 HAL로 가는 게 장기적으로 낫다.

**LSE 크리스탈은 보드에 있다** — 회로도의 **X1 = `MU00525-32.768K`** (전용 footprint까지 지정됨).
다만 `.ioc`에는 LSE 설정이 아예 없어서 CubeMX상으로는 꺼져 있다.
(현재 코드는 `RCC->BDCR`에서 직접 켜므로 `.ioc`와 무관하게 동작한다)

**CubeMX에서 할 일**

1. `RCC` → `Low Speed Clock (LSE)` = **Crystal/Ceramic Resonator**
2. Clock Configuration 탭에서 LSE가 32.768 kHz로 잡히는지 확인
3. `Timers` → `RTC` → Activate Clock Source 체크
4. RTC → `Parameters Settings`
   - Hour Format: 24H
   - Asynchronous Predivider: **127**
   - Synchronous Predivider: **255**
   (32768 / (128 × 256) = 1 Hz — 현재 코드와 같은 값)
5. RTC → `WakeUp` → Wake Up Clock: **RTCCLK/16 아님 → `ck_spre (1Hz)`** 선택
6. `NVIC Settings` 탭에서 **RTC wake-up interrupt 체크**
7. 코드 생성

**생성 후 코드에서 할 일**

`B_BMS_rtc.h`가 인터페이스 경계라 **호출부(`B_BMS_power_mode.c`, `main.c`)는 한 줄도 안 바뀐다.**
`B_BMS_rtc.c`의 함수 6개 **본문만** 교체하면 된다:

| 함수 | HAL로 교체 |
|---|---|
| `BMS_RTC_Init` | 생성된 `MX_RTC_Init()` 호출 + `rtc_ready = 1` |
| `BMS_RTC_NowMs` | `HAL_RTC_GetTime()` → `HAL_RTC_GetDate()` (**반드시 이 순서**, Date를 읽어야 섀도 잠금이 풀린다) |
| `BMS_RTC_ElapsedMs` | 그대로 (NowMs 기반이라 수정 불필요) |
| `BMS_RTC_StartWakeup` | `HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, sec-1, RTC_WAKEUPCLOCK_CK_SPRE_16BITS, 0)` |
| `BMS_RTC_StopWakeup` | `HAL_RTCEx_DeactivateWakeUpTimer(&hrtc)` |
| `BMS_RTC_IRQHandler` | **삭제** |

그리고 **`stm32u5xx_it.c`의 `RTC_IRQHandler()`를 삭제**할 것 — CubeMX가 생성해 준다. 안 지우면 중복 정의로 링크 에러.

- [ ] 위 작업 완료
- [ ] `BMS_RTC_IsReady()`가 1인지 확인 (LSE 기동 성공)
- [ ] 무부하 방치 시 30초 주기로 깨어나는지 (CAN 프레임 간격)

---

### 3-2. `HSE_VALUE` 8 MHz → 16 MHz

**회로도의 X2는 `CS07826-16M` = 16 MHz인데 `.ioc`는 `HSE_VALUE=8000000`이다. 2배 차이.**

**지금은 무해하다** — `SystemClock_Config()`가 HSE를 안 쓰고 MSI를 PLL 소스로 쓴다:

```c
RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
```

즉 X2는 달려 있는데 아무도 안 쓰는 상태다. 하지만 나중에 HSE로 전환하면
**시스템 클럭이 2배 틀어진다.** 지금 고쳐 두는 게 안전하다.

- [ ] CubeMX → RCC → HSE 입력 주파수를 **16 MHz**로 수정
- [ ] (HSE를 실제로 쓸 거라면) PLL 파라미터 재계산 — 현재는 MSI 48 MHz 기준으로 잡혀 있다

---

### 3-3. FDCAN 수신 인터럽트 — 필요하면

**현재 CAN 수신은 동작하지 않는다.** `MX_FDCAN1_Init()`이

```c
HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0);
```

을 부르는데 **NVIC에 FDCAN 항목이 없고, IRQ 핸들러도 없고, `HAL_FDCAN_RxFifo0Callback()`도 없다.**
지금은 NVIC가 꺼져 있어서 무해하지만, CAN으로 BMS에 명령(latch 해제, 테스트 데이터 요청 등)을
보낼 수 없다.

> ⚠️ **NVIC만 켜고 핸들러를 안 만들면 더 위험하다.** 인터럽트가 `Default_Handler`(무한 루프)로 빠진다.
> 반드시 둘을 같이 할 것.

- [ ] 수신 기능이 필요한지 결정
- [ ] 필요하면: CubeMX → FDCAN1 → NVIC Settings → `FDCAN1 interrupt 0` 체크
- [ ] `HAL_FDCAN_RxFifo0Callback()` 구현 (USER CODE 블록 안에)
- [ ] 필요 없으면: `ActivateNotification` 호출을 지우는 게 오해를 줄인다

---

### 3-4. TIM2 CH3 — 두 번째 팬을 달 때만

**실물에는 팬이 1개만 장착된다** (2026-08-04 확인). 보고서 MCU 핀 표와 KiCad 회로도는
팬 2개분(PA1/ch2, PA2/ch3)을 배선해 두었고 `.ioc`에도 두 채널이 다 들어 있지만
(`TIM2.Channel-PWM Generation3 CH3=TIM_CHANNEL_3`, `PA2.Signal=S_TIM2_CH3`),
코드는 `B_BMS.c`의 `BMS_FAN_COUNT`(현재 **1**)로 ch2만 구동한다.

**CubeMX에서 할 일 없음.** 나중에 두 번째 팬을 달면 `BMS_FAN_COUNT`를 2로 바꾸기만 하면 된다.

---

## 4. 코드 쪽 미결 (CubeMX와 무관하지만 같이 봐야 함)

### 4-1. `PWR_EN` (PA0) 전원 시퀀싱 ★

회로도 추적 결과:

```
U11 STM32U545CEU6Q  PA0 (PWR_EN) ──직결──▶ U9 LTC3895EFE-PBF  pin 34 = RUN
```

`RUN`은 12 V 컨버터 인에이블인데, `gpio.c`가 초기화에서 **Low로 쓰고 그 뒤 아무도 High로 안 올린다.**
즉 배터리 유래 12 V 컨버터가 계속 꺼진 채로 동작한다.

보고서 전력흐름상 "외부 12 V로 부팅 → 확인 후 배터리 전원으로 전환"이 의도로 보이는데,
그렇다면 **전환 단계가 없다.** 외부 12 V를 떼면 보드가 죽는다.

- [ ] 위 해석이 맞는지 회로 확인
- [ ] 맞다면 **언제** High로 올릴지 결정 (BQ 초기화 성공 후? BOTHOFF 해제와 같은 시점?)
- [ ] **PF 발생 시 다시 Low로 내려야 하는지** 결정

> 전원 시퀀싱이라 잘못 넣으면 보드가 자기 전원을 끊는다. 확인 전에는 코드를 고치지 않았다.

### 4-2. 루프 주기 실측 → `BMS_LOOP_PERIOD_MS_EST`

`B_BMS_protect.c`의 모든 시간 임계가 이 상수(현재 300 ms **추정**)에서 유도된다.
`EVM_TEST.md` §6에서 실측한 뒤 이 값 하나만 고치면 전부 따라 움직인다.

### 4-3. 나머지

`EVM_TEST.md` §9 (R1~R6)와 `HARDWARE_VERIFICATION.md` 참조.

---

## 5. 재생성 후 체크리스트

CubeMX로 코드를 생성한 뒤 매번 확인할 것:

- [ ] `git status`로 **예상치 못한 파일 삭제/변경**이 없는지 (`DeletePrevious=true` 주의)
- [ ] `Core/Src/B_BMS_*.c` 8개 파일이 그대로 있는지
- [ ] `main.c`의 USER CODE 블록이 살아있는지 — 특히
      `BMS_RTC_Init()` / `BMS_Protect_Init()` / `BMS_Protect_Update()` / `BMS_CAN_SendProtectDiag()` 호출
- [ ] `stm32u5xx_it.c`의 `HAL_GPIO_EXTI_Callback()`이 살아있는지
- [ ] `stm32u5xx_hal_conf.h`에서 필요한 모듈이 켜져 있는지
- [ ] 빌드 통과 + **경고 0개**
- [ ] `EVM_TEST.md` §1 통신 기본부터 다시 확인
