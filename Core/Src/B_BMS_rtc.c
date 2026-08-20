/*
 * B_BMS_rtc.c
 *
 * CMSIS 레지스터 직접 접근 RTC 드라이버. 배경은 B_BMS_rtc.h 참조.
 */

#include "B_BMS_rtc.h"

#define RTC_WPR_KEY1   0xCAU
#define RTC_WPR_KEY2   0x53U
#define RTC_WPR_LOCK   0xFFU

/* LSE 32768 Hz 기준: 32768 / ((127+1) * (255+1)) = 1 Hz */
#define RTC_PREDIV_A_LSE   127U
#define RTC_PREDIV_S_LSE   255U
/* LSI 32000 Hz 기준: 32000 / ((127+1) * (249+1)) = 1 Hz
 * LSI는 정밀도가 낮아(±수 %) 시간 오차가 크다. 어디까지나 LSE 실패 시의 폴백이다. */
#define RTC_PREDIV_A_LSI   127U
#define RTC_PREDIV_S_LSI   249U

#define MS_PER_DAY     86400000UL

/* 클럭 안정화/모드 전환 대기 한도. 무한 루프에 빠지지 않게 반드시 유한값을 쓴다.
 *
 * LSE만 ms로 재는 이유: 크리스탈 기동은 0.5~2초(최악 수 초)인데 공회전 횟수로는
 * 실제 대기 시간을 알 수 없다. 5,000,000회는 160 MHz에서 수백 ms라 크리스탈이 뜨기
 * 전에 끝나 조용히 LSI로 폴백됐다. 5000 ms는 HAL의 LSE_STARTUP_TIMEOUT과 같은 값.
 * LSIRDY(~200 us)·INITF·RSF는 내부 회로라 공회전 횟수로도 여유가 충분하다. */
#define RTC_LSE_TIMEOUT_MS        5000UL
#define RTC_FLAG_TIMEOUT_LOOPS   100000UL

static uint8_t rtc_ready = 0;
static uint8_t rtc_clk_src = BMS_RTC_CLK_NONE;
static uint32_t rtc_prediv_s = RTC_PREDIV_S_LSE;

static void rtc_unlock(void)
{
    RTC->WPR = RTC_WPR_KEY1;
    RTC->WPR = RTC_WPR_KEY2;
}

static void rtc_lock(void)
{
    RTC->WPR = RTC_WPR_LOCK;
}

/* 플래그가 설 때까지 대기. 1 = 성공, 0 = 타임아웃 */
static uint8_t rtc_wait_flag(volatile uint32_t *reg, uint32_t mask, uint8_t want_set)
{
    for (uint32_t i = 0; i < RTC_FLAG_TIMEOUT_LOOPS; i++) {
        uint8_t is_set = ((*reg & mask) != 0U) ? 1U : 0U;
        if (is_set == want_set) {
            return 1U;
        }
    }
    return 0U;
}

static uint8_t bcd2dec(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4) * 10U) + (bcd & 0x0FU));
}

void BMS_RTC_Init(void)
{
    rtc_ready = 0;
    rtc_clk_src = BMS_RTC_CLK_NONE;

    /* 백업 도메인은 쓰기 보호가 걸려 있다. 풀지 않으면 RCC->BDCR 쓰기가 무시된다. */
    HAL_PWR_EnableBkUpAccess();

    /* RTC 레지스터에 접근하려면 APB 인터페이스 클럭이 필요하다 */
    __HAL_RCC_RTCAPB_CLK_ENABLE();

    /* 이미 세워져 있으면(예: 리셋 후 백업 도메인이 살아있는 경우) 다시 세우지 않는다.
     * RTC를 재초기화하면 시간이 0으로 돌아가 경과 시간 계산이 깨진다. */
    if ((RCC->BDCR & RCC_BDCR_RTCEN) != 0U) {
        rtc_prediv_s = (RTC->PRER & RTC_PRER_PREDIV_S);
        uint32_t sel = (RCC->BDCR & RCC_BDCR_RTCSEL);   /* 이미 도는 RTC의 소스 */
        if (sel == RCC_BDCR_RTCSEL_0) {
            rtc_clk_src = BMS_RTC_CLK_LSE;
        } else if (sel == RCC_BDCR_RTCSEL_1) {
            rtc_clk_src = BMS_RTC_CLK_LSI;
        } else {
            rtc_clk_src = BMS_RTC_CLK_NONE;
        }
        rtc_ready = 1U;
        return;
    }

    /* --- 클럭 소스: LSE 우선, 실패 시 LSI --- */
    uint32_t rtcsel;

    RCC->BDCR |= RCC_BDCR_LSEON;

    uint32_t t0 = HAL_GetTick();
    while ((RCC->BDCR & RCC_BDCR_LSERDY) == 0U) {
        if ((HAL_GetTick() - t0) > RTC_LSE_TIMEOUT_MS) {
            break;
        }
    }
    uint8_t lse_ok = ((RCC->BDCR & RCC_BDCR_LSERDY) != 0U) ? 1U : 0U;

    if (lse_ok) {
        rtcsel = RCC_BDCR_RTCSEL_0;                 /* 01 = LSE */
        rtc_prediv_s = RTC_PREDIV_S_LSE;
        rtc_clk_src = BMS_RTC_CLK_LSE;
    } else {
        /* LSE 크리스탈(PC14/PC15)이 없거나 기동 실패. 정밀도는 떨어지지만
         * 시간을 아예 못 재는 것보다는 낫다. */
        RCC->BDCR &= ~RCC_BDCR_LSEON;
        RCC->BDCR |= RCC_BDCR_LSION;
        if (!rtc_wait_flag(&RCC->BDCR, RCC_BDCR_LSIRDY, 1U)) {
            return;                                  /* 둘 다 실패 -> rtc_ready = 0 */
        }
        rtcsel = RCC_BDCR_RTCSEL_1;                 /* 10 = LSI */
        rtc_prediv_s = RTC_PREDIV_S_LSI;
        rtc_clk_src = BMS_RTC_CLK_LSI;
    }

    RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_RTCSEL) | rtcsel;
    RCC->BDCR |= RCC_BDCR_RTCEN;

    /* --- RTC 초기화 --- */
    rtc_unlock();

    RTC->ICSR |= RTC_ICSR_INIT;
    if (!rtc_wait_flag(&RTC->ICSR, RTC_ICSR_INITF, 1U)) {
        rtc_lock();
        return;
    }

    uint32_t prediv_a = (rtcsel == RCC_BDCR_RTCSEL_0) ? RTC_PREDIV_A_LSE : RTC_PREDIV_A_LSI;
    RTC->PRER = (prediv_a << RTC_PRER_PREDIV_A_Pos) | rtc_prediv_s;

    RTC->TR = 0U;          /* 00:00:00 을 기준시각으로 삼는다 (달력 기능은 쓰지 않는다) */
    RTC->DR = 0x2101U;     /* 유효한 날짜여야 하므로 임의값 (01-01-21, 요일 무관) */
    RTC->CR &= ~RTC_CR_FMT; /* 24시간 형식 */

    RTC->ICSR &= ~RTC_ICSR_INIT;

    /* 섀도 레지스터가 갱신될 때까지 기다린다. 안 기다리면 첫 읽기가 쓰레기값이다. */
    RTC->ICSR &= ~RTC_ICSR_RSF;
    (void)rtc_wait_flag(&RTC->ICSR, RTC_ICSR_RSF, 1U);

    rtc_lock();
    rtc_ready = 1U;
}

uint8_t BMS_RTC_IsReady(void)
{
    return rtc_ready;
}

uint8_t BMS_RTC_ClockSource(void)
{
    /* 초기화가 중간에 실패하면(INITF/RSF 타임아웃) 클럭 소스는 의미가 없다 */
    return rtc_ready ? rtc_clk_src : BMS_RTC_CLK_NONE;
}

uint32_t BMS_RTC_NowMs(void)
{
    if (!rtc_ready) {
        return 0U;
    }

    /* 읽는 순서가 중요하다: SSR -> TR -> DR.
     * DR을 읽어야 섀도 레지스터 잠금이 풀리므로, 마지막에 읽지 않으면 다음 갱신이 멈춘다. */
    uint32_t ssr = RTC->SSR & RTC_SSR_SS;
    uint32_t tr  = RTC->TR;
    (void)RTC->DR;

    uint32_t hour = bcd2dec((uint8_t)((tr >> RTC_TR_HU_Pos) & 0x3FU));
    uint32_t min  = bcd2dec((uint8_t)((tr >> RTC_TR_MNU_Pos) & 0x7FU));
    uint32_t sec  = bcd2dec((uint8_t)((tr >> RTC_TR_SU_Pos) & 0x7FU));

    /* SSR은 PREDIV_S 에서 0으로 감소한다. 경과분 = (PREDIV_S - SSR) / (PREDIV_S + 1) */
    uint32_t frac_ms = 0U;
    if (ssr <= rtc_prediv_s) {
        frac_ms = ((rtc_prediv_s - ssr) * 1000UL) / (rtc_prediv_s + 1UL);
    }

    return ((((hour * 60UL) + min) * 60UL) + sec) * 1000UL + frac_ms;
}

uint32_t BMS_RTC_ElapsedMs(uint32_t since_ms)
{
    if (!rtc_ready) {
        return 0U;
    }

    uint32_t now = BMS_RTC_NowMs();

    if (now >= since_ms) {
        return now - since_ms;
    }
    /* 자정을 넘겼다. 하루 이상 잔 경우는 구분할 수 없지만, 이 시스템에서 한 번의 웨이크
     * 간격이 24시간을 넘는 일은 없다 (웨이크업 타이머가 훨씬 짧은 주기로 깨운다). */
    return (MS_PER_DAY - since_ms) + now;
}

void BMS_RTC_StartWakeup(uint32_t seconds)
{
    if (!rtc_ready || seconds == 0U) {
        return;
    }

    rtc_unlock();

    /* 재설정하려면 먼저 꺼야 하고, WUTWF가 설 때까지 기다려야 쓰기가 먹는다 */
    RTC->CR &= ~RTC_CR_WUTE;
    if (!rtc_wait_flag(&RTC->ICSR, RTC_ICSR_WUTWF, 1U)) {
        rtc_lock();
        return;
    }

    RTC->WUTR = seconds - 1UL;

    /* WUCKSEL = 100b : ck_spre(1 Hz) 를 카운트 소스로 사용 */
    RTC->CR = (RTC->CR & ~RTC_CR_WUCKSEL) | RTC_CR_WUCKSEL_2;
    RTC->CR |= RTC_CR_WUTE | RTC_CR_WUTIE;

    rtc_lock();

    /* STM32U5는 RTC 인터럽트가 NVIC에 직접 연결되어 있어 EXTI 설정이 필요 없다 */
    HAL_NVIC_SetPriority(RTC_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(RTC_IRQn);
}

void BMS_RTC_StopWakeup(void)
{
    if (!rtc_ready) {
        return;
    }

    rtc_unlock();
    RTC->CR &= ~(RTC_CR_WUTE | RTC_CR_WUTIE);
    rtc_lock();

    RTC->SCR = RTC_SCR_CWUTF;
}

void BMS_RTC_IRQHandler(void)
{
    if ((RTC->MISR & RTC_MISR_WUTMF) != 0U) {
        RTC->SCR = RTC_SCR_CWUTF;
    }
}
