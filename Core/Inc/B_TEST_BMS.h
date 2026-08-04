/*
 * B_TEST_BMS.h
 *
 * Test-drive readout and CAN transmit helpers.
 */

#ifndef INC_B_TEST_BMS_H_
#define INC_B_TEST_BMS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "B_BMS.h"

void BQ769x2_ReadDiagnostics(BMS_Unit *unit);
void BQ769x2_ReadTestExtraData(BMS_Unit *unit);
void BMS_Test_ReadAll(void);

void BMS_CAN_SendRunData(uint8_t board_type);
void BMS_CAN_SendTestData(uint8_t board_type);
void BMS_CAN_SendProtectDiag(void);   /* 0x080 — MCU 보호 감시자 상태 (팩 단위) */

/* Compatibility wrappers for older test calls. */
void T_LV_BMS_MAIN_RUN(void);
void T_LV_BMS_WHILE_RUN(void);
void T_FDCAN_Send_BMS_Data(uint8_t board_type);

#ifdef __cplusplus
}
#endif

#endif /* INC_B_TEST_BMS_H_ */
