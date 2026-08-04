/*
 * B_BMS_protect.h
 *
 * MCU 측 보호 감시자 (BQ769x2 자체 보호의 상위 감시 계층).
 *
 * 설계 원칙
 * ---------
 * 선택적 차단(CHG만 / DSG만)은 전부 BQ 하드웨어 경로가 담당한다:
 *
 *     BOT.DCHG ──G3VM──▶ TOP.CFETOFF ──▶ CHG FET OFF
 *     BOT.DDSG ──G3VM──▶ TOP.DFETOFF ──▶ DSG FET OFF
 *
 * MCU가 가진 차단 수단은 BOT.DFETOFF(BOTHOFF) 한 핀뿐이라 애초에 선택적 차단이 불가능하다.
 * 따라서 이 모듈은 "BQ가 못 보는 것"과 "BQ 차단 경로 자체가 고장난 것"만 판정하고,
 * 액션은 항상 BOTHOFF(전부 차단) 하나로 통일한다.
 *
 * 정상적인 보호 트립(COV/CUV/OCC/OCD/OT/UT ...)에는 이 모듈이 개입하지 않는다.
 */

#ifndef INC_B_BMS_PROTECT_H_
#define INC_B_BMS_PROTECT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "B_BMS.h"

typedef enum {
    BMS_TRIP_NONE = 0,
    BMS_TRIP_INIT            = 1,  /* 초기화 미완료 / 초기 통신 실패 */
    BMS_TRIP_CHG_PATH        = 2,  /* CHG를 꺼야 하는데 TOP의 CHG FET이 계속 켜져 있음 (절연 경로 고장) */
    BMS_TRIP_DSG_PATH        = 3,  /* DSG를 꺼야 하는데 TOP의 DSG FET이 계속 켜져 있음 */
    BMS_TRIP_COMM            = 4,  /* 한쪽 칩과 연속 통신 실패 — 상태를 못 읽음 */
    BMS_TRIP_PF              = 5,  /* 영구 고장 (latch, 자동 해제 없음) */
    BMS_TRIP_STACK_MISMATCH  = 6,  /* 셀 전압 합 vs 스택 전압 괴리 — 측정계 이상 */
    BMS_TRIP_SOFT_CELLV      = 7,  /* MCU가 본 셀 전압이 한계를 넘었는데 어느 칩도 안 껐음 */
    BMS_TRIP_SOFT_TEMP       = 8,  /* MCU가 본 온도가 한계를 넘었는데 어느 칩도 안 껐음 */
    BMS_TRIP_RETRY_EXHAUSTED = 9   /* 재시도 횟수 초과 (latch) */
} BMS_TripReason_t;

typedef struct {
    BMS_TripReason_t reason;        /* 마지막(또는 현재) 트립 사유 */
    uint8_t bothoff_asserted;       /* MCU가 BOTHOFF를 걸고 있는가 */
    uint8_t latched;                /* 자동 해제 금지 상태 */
    uint8_t retry_count;            /* BOTHOFF 해제를 시도한 횟수 */

    /* 크로스체크 관측값 — CAN 진단 프레임으로 그대로 나간다 */
    uint8_t chg_expected_off;       /* SafetyStatus + 마스크로 계산한 기대 상태 */
    uint8_t dsg_expected_off;
    uint8_t chg_observed_on;        /* TOP FETStatus 실측 */
    uint8_t dsg_observed_on;
    uint16_t chg_mismatch_cycles;   /* 기대=OFF인데 실측=ON인 연속 사이클 수 */
    uint16_t dsg_mismatch_cycles;
    uint16_t stuck_off_cycles;      /* 기대=ON인데 실측=OFF (안전측 — 로깅만) */
    uint16_t stack_mismatch_cycles;
    uint16_t soft_cellv_cycles;     /* 소프트 셀전압 한계 초과 연속 사이클 */
    uint16_t soft_temp_cycles;      /* 소프트 온도 한계 초과 연속 사이클 */
    uint16_t clean_cycles;          /* 아무 트립 조건도 없는 상태가 이어진 사이클 수 */
} BMS_ProtectState_t;

extern BMS_ProtectState_t BMS_Protect;

void BMS_Protect_Init(void);
void BMS_Protect_Update(void);

/* LED/CAN용: MCU가 개입했거나 개입해야 하는 상태인가 */
uint8_t BMS_Protect_IsFaulted(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_B_BMS_PROTECT_H_ */
