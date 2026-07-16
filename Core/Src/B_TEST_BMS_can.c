/*
 * B_TEST_BMS_can.c
 *
 * CAN transmit helpers for normal run data and test-drive detailed data.
 */

#include "B_TEST_BMS.h"
#include "fdcan.h"

static FDCAN_TxHeaderTypeDef BMS_TxHeader;
static uint8_t BMS_TxData[8];

static uint8_t msb16(uint16_t value)
{
    return (uint8_t)((value >> 8) & 0xffU);
}

static uint8_t lsb16(uint16_t value)
{
    return (uint8_t)(value & 0xffU);
}

static void put_u16_be(uint8_t *data, uint8_t offset, uint16_t value)
{
    data[offset] = msb16(value);
    data[offset + 1U] = lsb16(value);
}

static void put_i16_be(uint8_t *data, uint8_t offset, int16_t value)
{
    put_u16_be(data, offset, (uint16_t)value);
}

static void put_u32_be(uint8_t *data, uint8_t offset, uint32_t value)
{
    data[offset] = (uint8_t)((value >> 24) & 0xffU);
    data[offset + 1U] = (uint8_t)((value >> 16) & 0xffU);
    data[offset + 2U] = (uint8_t)((value >> 8) & 0xffU);
    data[offset + 3U] = (uint8_t)(value & 0xffU);
}

static int16_t temp_to_deci_c(float temp_c)
{
    return (int16_t)(temp_c * 10.0f);
}

static void bms_can_prepare_header(uint32_t identifier)
{
    BMS_TxHeader.Identifier = identifier;
    BMS_TxHeader.IdType = FDCAN_STANDARD_ID;
    BMS_TxHeader.TxFrameType = FDCAN_DATA_FRAME;
    BMS_TxHeader.DataLength = FDCAN_DLC_BYTES_8;
    BMS_TxHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    BMS_TxHeader.BitRateSwitch = FDCAN_BRS_OFF;
    BMS_TxHeader.FDFormat = FDCAN_CLASSIC_CAN;
    BMS_TxHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    BMS_TxHeader.MessageMarker = 0;
}

#define BMS_CAN_TX_TIMEOUT_MS 5U

static void bms_can_send(uint32_t identifier, const uint8_t data[8])
{
    bms_can_prepare_header(identifier);

    uint32_t start = HAL_GetTick();
    while (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan1) == 0) {
        if ((HAL_GetTick() - start) >= BMS_CAN_TX_TIMEOUT_MS) {
            // TX FIFO가 타임아웃 내에 비지 않음 (버스오프 등) - 이 프레임은 포기하고 나머지 루프(BQ 모니터링/팬/LED)가 멈추지 않게 함
            return;
        }
    }

    HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &BMS_TxHeader, (uint8_t *)data);
}

static uint32_t base_id(uint8_t board_type, uint32_t bot_base, uint32_t top_base)
{
    return (board_type == BOT) ? bot_base : top_base;
}

/*
 * Updated CAN ID assignments (2026-07-16 CAN ID spreadsheet, "can id" column only).
 * Frames not present in that spreadsheet (legacy 0x10/0x11/0x12/0x20/0x23/0x33 test-data
 * offsets and the run-data SOC frame) keep their previous base+offset IDs below.
 */
#define CANID_BOT_STATUS      0x040U
#define CANID_BOT_PACK_VOLT   0x041U
#define CANID_BOT_PACK_CURR   0x042U
#define CANID_TOP_STATUS      0x043U
#define CANID_TOP_PACK_VOLT   0x044U
#define CANID_TOP_PACK_CURR   0x045U

#define CANID_BOT_CELLV_BASE  0x046U /* 4 consecutive frames: 0x046-0x049 */
#define CANID_TOP_CELLV_BASE  0x04FU /* 0x04F-0x052 */

#define CANID_BOT_TEMP1       0x04AU
#define CANID_BOT_TEMP2       0x04BU
#define CANID_TOP_TEMP1       0x053U
#define CANID_TOP_TEMP2       0x054U

#define CANID_BOT_COULOMB1    0x04CU
#define CANID_BOT_COULOMB2    0x04DU
#define CANID_BOT_COULOMB3    0x04EU
#define CANID_TOP_COULOMB1    0x055U
#define CANID_TOP_COULOMB2    0x056U
#define CANID_TOP_COULOMB3    0x057U

#define CANID_BOT_SAFETY      0x058U
#define CANID_BOT_PF          0x059U
#define CANID_TOP_SAFETY      0x06BU
#define CANID_TOP_PF          0x06CU

#define CANID_BOT_CUV_BASE    0x05BU /* 0x05B-0x05E */
#define CANID_BOT_COV_BASE    0x05FU /* 0x05F-0x062 */
#define CANID_BOT_CB2_BASE    0x063U /* 0x063-0x066 */
#define CANID_BOT_CB3_BASE    0x067U /* 0x067-0x06A */

#define CANID_TOP_CUV_BASE    0x06EU /* 0x06E-0x071 */
#define CANID_TOP_COV_BASE    0x072U /* 0x072-0x075 */
#define CANID_TOP_CB2_BASE    0x076U /* 0x076-0x079 */
#define CANID_TOP_CB3_BASE    0x07AU /* 0x07A-0x07D */

static void send_u16_array(uint32_t start_id, const uint16_t *values)
{
    for (int frame = 0; frame < 4; frame++) {
        for (int i = 0; i < 4; i++) {
            put_u16_be(BMS_TxData, (uint8_t)(i * 2), values[(frame * 4) + i]);
        }
        bms_can_send(start_id + (uint32_t)frame, BMS_TxData);
    }
}

void BMS_CAN_SendRunData(uint8_t board_type)
{
    if (board_type >= STACK) {
        return;
    }

    BMS_Unit *unit = &BMS[board_type];
    BMS_Unit *current_unit = &BMS[BOT];
    uint32_t base = base_id(board_type, 0x500U, 0x600U);

    put_u32_be(BMS_TxData, 0, unit->Global_Fault_Flags);
    put_u16_be(BMS_TxData, 4, unit->BattStat);
    put_u16_be(BMS_TxData, 6, unit->AlarmRawBits);
    bms_can_send(base_id(board_type, CANID_BOT_STATUS, CANID_TOP_STATUS), BMS_TxData);

    put_u32_be(BMS_TxData, 0, unit->Stack_Voltage);
    put_u32_be(BMS_TxData, 4, unit->Pack_Voltage);
    bms_can_send(base_id(board_type, CANID_BOT_PACK_VOLT, CANID_TOP_PACK_VOLT), BMS_TxData);

    put_i16_be(BMS_TxData, 0, current_unit->Pack_Current);
    put_u16_be(BMS_TxData, 2, unit->MaxCellVolatge);
    put_u16_be(BMS_TxData, 4, unit->MinCellVolatge);
    put_i16_be(BMS_TxData, 6, temp_to_deci_c(unit->CELL_Temp));
    bms_can_send(base_id(board_type, CANID_BOT_PACK_CURR, CANID_TOP_PACK_CURR), BMS_TxData);

    put_u16_be(BMS_TxData, 0, current_unit->SOC_Permille);
    put_u16_be(BMS_TxData, 2, 0);
    put_u16_be(BMS_TxData, 4, 0);
    put_u16_be(BMS_TxData, 6, 0);
    bms_can_send(base + 0x03U, BMS_TxData);
}

void BMS_CAN_SendTestData(uint8_t board_type)
{
    if (board_type >= STACK) {
        return;
    }

    BMS_Unit *unit = &BMS[board_type];
    BMS_Unit *current_unit = &BMS[BOT];
    BMS_Unit *fet_unit = &BMS[TOP];
    uint32_t base = base_id(board_type, 0x100U, 0x200U);

    send_u16_array(base_id(board_type, CANID_BOT_CELLV_BASE, CANID_TOP_CELLV_BASE), unit->CellVoltage);

    put_u16_be(BMS_TxData, 0, unit->MaxCellVolatge);
    put_u16_be(BMS_TxData, 2, unit->MinCellVolatge);
    put_u16_be(BMS_TxData, 4, unit->Stack_Voltage_Raw);
    put_u16_be(BMS_TxData, 6, unit->Pack_Voltage_Raw);
    bms_can_send(base + 0x10U, BMS_TxData);

    put_u32_be(BMS_TxData, 0, unit->Stack_Voltage);
    put_u32_be(BMS_TxData, 4, unit->Pack_Voltage);
    bms_can_send(base + 0x11U, BMS_TxData);

    put_i16_be(BMS_TxData, 0, current_unit->Pack_Current);
    put_i16_be(BMS_TxData, 2, current_unit->CC1_Current);
    put_i16_be(BMS_TxData, 4, current_unit->CC3_Current);
    put_u16_be(BMS_TxData, 6, unit->CB_ActiveCells);
    bms_can_send(base + 0x12U, BMS_TxData);

    put_i16_be(BMS_TxData, 0, temp_to_deci_c(unit->CELL_Temp));
    put_i16_be(BMS_TxData, 2, temp_to_deci_c(fet_unit->FET_Temp));
    put_i16_be(BMS_TxData, 4, temp_to_deci_c(unit->Int_Temp));
    put_i16_be(BMS_TxData, 6, temp_to_deci_c(unit->CFETOFF_Temp));
    bms_can_send(base_id(board_type, CANID_BOT_TEMP1, CANID_TOP_TEMP1), BMS_TxData);

    put_i16_be(BMS_TxData, 0, temp_to_deci_c(unit->HDQ_Temp));
    put_i16_be(BMS_TxData, 2, temp_to_deci_c(unit->Max_Cell_Temp));
    put_i16_be(BMS_TxData, 4, temp_to_deci_c(unit->Min_Cell_Temp));
    put_i16_be(BMS_TxData, 6, temp_to_deci_c(unit->Avg_Cell_Temp));
    bms_can_send(base_id(board_type, CANID_BOT_TEMP2, CANID_TOP_TEMP2), BMS_TxData);

    put_u32_be(BMS_TxData, 0, unit->Global_Fault_Flags);
    put_u16_be(BMS_TxData, 4, unit->BattStat);
    put_u16_be(BMS_TxData, 6, unit->AlarmRawBits);
    bms_can_send(base + 0x20U, BMS_TxData);

    BMS_TxData[0] = unit->value_SafetyAlertA;
    BMS_TxData[1] = unit->value_SafetyAlertB;
    BMS_TxData[2] = unit->value_SafetyAlertC;
    BMS_TxData[3] = unit->value_SafetyStatusA;
    BMS_TxData[4] = unit->value_SafetyStatusB;
    BMS_TxData[5] = unit->value_SafetyStatusC;
    BMS_TxData[6] = unit->ProtectionsTriggered;
    BMS_TxData[7] = unit->PF_ProtectionsTriggered;
    bms_can_send(base_id(board_type, CANID_BOT_SAFETY, CANID_TOP_SAFETY), BMS_TxData);

    BMS_TxData[0] = unit->value_PFAlertA;
    BMS_TxData[1] = unit->value_PFAlertB;
    BMS_TxData[2] = unit->value_PFAlertC;
    BMS_TxData[3] = unit->value_PFAlertD;
    BMS_TxData[4] = unit->value_PFStatusA;
    BMS_TxData[5] = unit->value_PFStatusB;
    BMS_TxData[6] = unit->value_PFStatusC;
    BMS_TxData[7] = unit->value_PFStatusD;
    bms_can_send(base_id(board_type, CANID_BOT_PF, CANID_TOP_PF), BMS_TxData);

    BMS_TxData[0] = fet_unit->FET_Status;
    BMS_TxData[1] = fet_unit->CHG;
    BMS_TxData[2] = fet_unit->DSG;
    BMS_TxData[3] = fet_unit->PDSG;
    BMS_TxData[4] = unit->CB_Status1 >> 8;
    BMS_TxData[5] = unit->CB_Status1 & 0xffU;
    BMS_TxData[6] = unit->AlarmBits >> 8;
    BMS_TxData[7] = unit->AlarmBits & 0xffU;
    bms_can_send(base + 0x23U, BMS_TxData);

    put_u32_be(BMS_TxData, 0, (uint32_t)current_unit->AccumulatedCharge_Int);
    put_u32_be(BMS_TxData, 4, current_unit->AccumulatedCharge_Frac);
    bms_can_send(base_id(board_type, CANID_BOT_COULOMB1, CANID_TOP_COULOMB1), BMS_TxData);

    put_u32_be(BMS_TxData, 0, current_unit->AccumulatedCharge_Time);
    put_u32_be(BMS_TxData, 4, (uint32_t)current_unit->CC2_Counts);
    bms_can_send(base_id(board_type, CANID_BOT_COULOMB2, CANID_TOP_COULOMB2), BMS_TxData);

    put_u32_be(BMS_TxData, 0, (uint32_t)current_unit->CC3_Counts);
    put_u16_be(BMS_TxData, 4, unit->Battery_Voltage_Sum);
    put_u16_be(BMS_TxData, 6, unit->LD_Voltage_Raw);
    bms_can_send(base_id(board_type, CANID_BOT_COULOMB3, CANID_TOP_COULOMB3), BMS_TxData);

    put_u16_be(BMS_TxData, 0, current_unit->SOC_Permille);
    put_u16_be(BMS_TxData, 2, 0);
    put_u16_be(BMS_TxData, 4, 0);
    put_u16_be(BMS_TxData, 6, 0);
    bms_can_send(base + 0x33U, BMS_TxData);

    send_u16_array(base_id(board_type, CANID_BOT_CUV_BASE, CANID_TOP_CUV_BASE), unit->CUV_Snapshot);
    send_u16_array(base_id(board_type, CANID_BOT_COV_BASE, CANID_TOP_COV_BASE), unit->COV_Snapshot);
    send_u16_array(base_id(board_type, CANID_BOT_CB2_BASE, CANID_TOP_CB2_BASE), unit->CB_Status2);
    send_u16_array(base_id(board_type, CANID_BOT_CB3_BASE, CANID_TOP_CB3_BASE), unit->CB_Status3);
}

void T_FDCAN_Send_BMS_Data(uint8_t board_type)
{
    BMS_CAN_SendTestData(board_type);
}
