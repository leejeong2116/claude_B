/*
 * B_BMS_rtc.h
 *
 * RTC 기반 시간축.
 *
 * 왜 필요한가
 * -----------
 * Enter_Sleep_Sequence()는 "무부하 1시간 -> STOP1", "3일 -> SHUTDOWN"으로 동작하는데,
 * 그 시간을 재는 방법이 없었다:
 *
 *   - 저전력 진입 전 HAL_SuspendTick()이 SysTick을 끄므로 HAL_GetTick()이 멈춘다
 *   - SLEEP/STOP1은 다음 인터럽트(주로 BQ ALERT)까지 무기한 블로킹된다
 *   - 그래서 이전 코드는 실제 경과 시간 대신 상수(63 ms / 5000 ms)를 더하고 있었다
 *     => 10분을 자도 63만 더해진다. 1시간/3일 임계가 실제 시간과 무관했다
 *
 * RTC는 백업 도메인에 있어 STOP1을 관통해 계속 돌기 때문에 두 가지를 동시에 해결한다:
 *
 *   1. 깨어나서 RTC를 읽으면 실제로 얼마나 잤는지 알 수 있다
 *   2. Wake-up Timer로 주기적으로 깨울 수 있다 -> 무기한 수면이 사라진다
 *
 * 2번은 BQ의 호스트 워치독(HWDF, 60초) 문제도 같이 해결한다. 60초 넘게 자면 양쪽 칩이
 * HWDF로 FET을 여는데, 주기적으로 깨어나 BQ를 읽으면 워치독이 계속 리셋된다.
 * (B_BMS_init.c 상단의 HWD 주석 참조)
 *
 * HAL을 쓰지 않는 이유
 * --------------------
 * 이 프로젝트에는 HAL RTC 모듈이 없다. CubeMX가 .ioc에서 활성화된 주변장치의 드라이버만
 * 복사해 넣는데 RTC가 꺼져 있어서 stm32u5xx_hal_rtc.c/h 자체가 존재하지 않는다.
 * CubeMX 없이 진행하기 위해 CMSIS 레지스터 직접 접근으로 필요한 최소 기능만 구현했다.
 * 덕분에 나중에 CubeMX로 재생성해도 이 파일은 영향을 받지 않는다.
 *
 * 나중에 CubeMX로 RTC를 켜고 싶다면 (권장 — 장기적으로는 그쪽이 유지보수가 쉽다)
 * -------------------------------------------------------------------------------
 * 이 헤더가 인터페이스 경계다. 호출부(B_BMS_power_mode.c, main.c)는 아래 6개 함수만 쓰므로
 * 전환할 때 그 파일들은 **한 줄도 고칠 필요가 없다.** 할 일은 이것뿐이다:
 *
 *   1. CubeMX에서 RTC 활성화 + LSE 켜기 + Wake-up Timer 설정 -> 코드 생성
 *      (Drivers 에 stm32u5xx_hal_rtc.c/h 가 복사되고 hal_conf 의 HAL_RTC_MODULE_ENABLED 가 켜진다)
 *   2. B_BMS_rtc.c 의 6개 함수 본문을 HAL 호출로 교체
 *      - BMS_RTC_Init        -> 생성된 MX_RTC_Init() 호출로 대체
 *      - BMS_RTC_NowMs       -> HAL_RTC_GetTime/GetDate (반드시 Time -> Date 순서)
 *      - BMS_RTC_StartWakeup -> HAL_RTCEx_SetWakeUpTimer_IT(RTC_WAKEUPCLOCK_CK_SPRE_16BITS)
 *      - BMS_RTC_StopWakeup  -> HAL_RTCEx_DeactivateWakeUpTimer
 *   3. stm32u5xx_it.c 의 RTC_IRQHandler 를 삭제 (CubeMX가 생성해 준다)
 *
 * 즉 지금 이 구현은 CubeMX 전환을 막지 않는다. CubeMX 없이도 당장 동작하게 하려는 것뿐이다.
 */

#ifndef INC_B_BMS_RTC_H_
#define INC_B_BMS_RTC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

/* LSE(실패 시 LSI)로 RTC를 1 Hz 기준으로 세운다. main()에서 한 번만 호출. */
void BMS_RTC_Init(void);

/* 0 이면 RTC를 못 세운 것. 호출부는 기존의 상수 누적 방식으로 폴백해야 한다. */
uint8_t BMS_RTC_IsReady(void);

/* RTC가 무엇으로 도는가. IsReady()는 LSE/LSI 둘 다 1이라 구분이 안 된다.
 * LSI는 RC라 ±5% — 3일 임계가 ±3.6시간, 1시간 임계가 ±3분 어긋난다. */
#define BMS_RTC_CLK_NONE   0U
#define BMS_RTC_CLK_LSE    1U
#define BMS_RTC_CLK_LSI    2U
uint8_t BMS_RTC_ClockSource(void);

/* 자정 기준 경과 밀리초 (0 ~ 86,399,999). RTC가 준비되지 않았으면 0. */
uint32_t BMS_RTC_NowMs(void);

/* since_ms 이후 경과한 밀리초. 자정을 한 번 넘는 경우까지 처리한다. */
uint32_t BMS_RTC_ElapsedMs(uint32_t since_ms);

/* 주기적 웨이크업 타이머. seconds 는 1 이상. STOP1도 관통한다. */
void BMS_RTC_StartWakeup(uint32_t seconds);
void BMS_RTC_StopWakeup(void);

/* RTC_IRQHandler 에서 호출 (stm32u5xx_it.c) */
void BMS_RTC_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_B_BMS_RTC_H_ */
