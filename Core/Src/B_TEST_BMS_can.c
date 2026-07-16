/*
 * B_TEST_BMS_can.c
 *
 * CAN transmit helpers for normal run data and test-drive detailed data.
 */

#include "B_TEST_BMS.h"
#include "fdcan.h"

/*
 * CAN ID map per datasheets/bms_can_id_spec.csv.
 * IDs marked "legacy" have no matching entry in that spreadsheet and keep
 * their pre-existing base+offset addresses until a new ID is assigned.
 */
#define CANID_STATUS_BOT            0x040U
#define CANID_STATUS_TOP            0x043U
#define CANID_VOLTAGE_BOT           0x041U
#define CANID_VOLTAGE_TOP           0x044U
#define CANID_CURRENT_BOT           0x042U
#define CANID_CURRENT_TOP           0x045U
#define CANID_SOC_RUN_BOT           0x503U /* legacy */
#define CANID_SOC_RUN_TOP           0x603U /* legacy */

#define CANID_CELLV_BASE_BOT        0x046U /* 4 frames: 0x046-0x049 */
#define CANID_CELLV_BASE_TOP        0x04FU /* 4 frames: 0x04F-0x052 */
#define CANID_STACKPACKV_TEST_BOT   0x111U /* legacy: duplicate of run-data voltage frame */
#define CANID_STACKPACKV_TEST_TOP   0x211U /* legacy */
#define CANID_CURRENT_DETAIL_BOT    0x112U /* legacy: CC1/CC3 current + CB active-cell count */
#define CANID_CURRENT_DETAIL_TOP    0x212U /* legacy */
#define CANID_TEMP1_BOT             0x04AU
#define CANID_TEMP1_TOP             0x053U
#define CANID_TEMP2_BOT             0x04BU
#define CANID_TEMP2_TOP             0x054U
#define CANID_STATUS_TEST_BOT       0x120U /* legacy: duplicate of run-data status frame */
#define CANID_STATUS_TEST_TOP       0x220U /* legacy */
#define CANID_SAFETY_BOT            0x058U
#define CANID_SAFETY_TOP            0x06BU
#define CANID_PF_BOT                0x059U
#define CANID_PF_TOP                0x06CU
#define CANID_FET_BOT               0x123U /* legacy: FET_Status/CHG/DSG/PDSG only */
#define CANID_FET_TOP               0x223U /* legacy */
#define CANID_CB_STATUS1_BOT        0x05AU
#define CANID_CB_STATUS1_TOP        0x06DU
#define CANID_COULOMB1_BOT          0x04CU
#define CANID_COULOMB1_TOP          0x055U
#define CANID_COULOMB2_BOT          0x04DU
#define CANID_COULOMB2_TOP          0x056U
#define CANID_COULOMB3_BOT          0x04EU
#define CANID_COULOMB3_TOP          0x057U
#define CANID_SOC_TEST_BOT          0x133U /* legacy */
#define CANID_SOC_TEST_TOP          0x233U /* legacy */
#define CANID_CUV_BASE_BOT          0x05BU /* 4 frames: 0x05B-0x05E */
#define CANID_CUV_BASE_TOP          0x06EU /* 4 frames: 0x06E-0x071 */
#define CANID_COV_BASE_BOT          0x05FU /* 4 frames: 0x05F-0x062 */
#define CANID_COV_BASE_TOP          0x072U /* 4 frames: 0x072-0x075 */
#define CANID_CB_STATUS2_BASE_BOT   0x063U /* 4 frames: 0x063-0x066 */
#define CANID_CB_STATUS2_BASE_TOP   0x076U /* 4 frames: 0x076-0x079 */
#define CANID_CB_STATUS3_BASE_BOT   0x067U /* 4 frames: 0x067-0x06A */
#define CANID_CB_STATUS3_BASE_TOP   0x07AU /* 4 frames: 0x07A-0x07D */

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

static void bms_can_send_len(uint32_t identifier, const uint8_t *data, uint32_t dlc)
{
    bms_can_prepare_header(identifier);
    BMS_TxHeader.DataLength = dlc;

    uint32_t start = HAL_GetTick();
    while (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan1) == 0) {
        if ((HAL_GetTick() - start) >= BMS_CAN_TX_TIMEOUT_MS) {
            // TX FIFO가 타임아웃 내에 비지 않음 (버스오프 등) - 이 프레임은 포기하고 나머지 루프(BQ 모니터링/팬/LED)가 멈추지 않게 함
            return;
        }
    }

    HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &BMS_TxHeader, (uint8_t *)data);
}

static void bms_can_send(uint32_t identifier, const uint8_t data[8])
{
    bms_can_send_len(identifier, data, FDCAN_DLC_BYTES_8);
}

static uint32_t pick_id(uint8_t board_type, uint32_t bot_id, uint32_t top_id)
{
    return (board_type == BOT) ? bot_id : top_id;
}

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

    put_u32_be(BMS_TxData, 0, unit->Global_Fault_Flags);
    put_u16_be(BMS_TxData, 4, unit->BattStat);
    put_u16_be(BMS_TxData, 6, unit->AlarmRawBits);
    bms_can_send(pick_id(board_type, CANID_STATUS_BOT, CANID_STATUS_TOP), BMS_TxData);

    put_u32_be(BMS_TxData, 0, unit->Stack_Voltage);
    put_u32_be(BMS_TxData, 4, unit->Pack_Voltage);
    bms_can_send(pick_id(board_type, CANID_VOLTAGE_BOT, CANID_VOLTAGE_TOP), BMS_TxData);

    /* TOP has no current-sense pins (SRP/SRN unused per WSC pin-usage table); send 0 instead of BOT's reading */
    put_i16_be(BMS_TxData, 0, (board_type == BOT) ? current_unit->Pack_Current : 0);
    put_u16_be(BMS_TxData, 2, unit->MaxCellVolatge);
    put_u16_be(BMS_TxData, 4, unit->MinCellVolatge);
    put_i16_be(BMS_TxData, 6, temp_to_deci_c(unit->CELL_Temp));
    bms_can_send(pick_id(board_type, CANID_CURRENT_BOT, CANID_CURRENT_TOP), BMS_TxData);

    put_u16_be(BMS_TxData, 0, current_unit->SOC_Permille);
    put_u16_be(BMS_TxData, 2, 0);
    put_u16_be(BMS_TxData, 4, 0);
    put_u16_be(BMS_TxData, 6, 0);
    bms_can_send(pick_id(board_type, CANID_SOC_RUN_BOT, CANID_SOC_RUN_TOP), BMS_TxData);
}

void BMS_CAN_SendTestData(uint8_t board_type)
{
    if (board_type >= STACK) {
        return;
    }

    BMS_Unit *unit = &BMS[board_type];
    BMS_Unit *current_unit = &BMS[BOT];
    BMS_Unit *fet_unit = &BMS[TOP];

    send_u16_array(pick_id(board_type, CANID_CELLV_BASE_BOT, CANID_CELLV_BASE_TOP), unit->CellVoltage);

    put_u32_be(BMS_TxData, 0, unit->Stack_Voltage);
    put_u32_be(BMS_TxData, 4, unit->Pack_Voltage);
    bms_can_send(pick_id(board_type, CANID_STACKPACKV_TEST_BOT, CANID_STACKPACKV_TEST_TOP), BMS_TxData);

    /* TOP has no current-sense pins (SRP/SRN unused per WSC pin-usage table); send 0 instead of BOT's reading */
    put_i16_be(BMS_TxData, 0, (board_type == BOT) ? current_unit->Pack_Current : 0);
    put_i16_be(BMS_TxData, 2, (board_type == BOT) ? current_unit->CC1_Current : 0);
    put_i16_be(BMS_TxData, 4, (board_type == BOT) ? current_unit->CC3_Current : 0);
    put_u16_be(BMS_TxData, 6, unit->CB_ActiveCells);
    bms_can_send(pick_id(board_type, CANID_CURRENT_DETAIL_BOT, CANID_CURRENT_DETAIL_TOP), BMS_TxData);

    put_i16_be(BMS_TxData, 0, temp_to_deci_c(unit->CELL_Temp));
    put_i16_be(BMS_TxData, 2, temp_to_deci_c(fet_unit->FET_Temp));
    put_i16_be(BMS_TxData, 4, temp_to_deci_c(unit->Int_Temp));
    put_i16_be(BMS_TxData, 6, temp_to_deci_c(unit->CFETOFF_Temp));
    bms_can_send(pick_id(board_type, CANID_TEMP1_BOT, CANID_TEMP1_TOP), BMS_TxData);

    put_i16_be(BMS_TxData, 0, temp_to_deci_c(unit->HDQ_Temp));
    put_i16_be(BMS_TxData, 2, temp_to_deci_c(unit->Max_Cell_Temp));
    put_i16_be(BMS_TxData, 4, temp_to_deci_c(unit->Min_Cell_Temp));
    put_i16_be(BMS_TxData, 6, temp_to_deci_c(unit->Avg_Cell_Temp));
    bms_can_send(pick_id(board_type, CANID_TEMP2_BOT, CANID_TEMP2_TOP), BMS_TxData);

    put_u32_be(BMS_TxData, 0, unit->Global_Fault_Flags);
    put_u16_be(BMS_TxData, 4, unit->BattStat);
    put_u16_be(BMS_TxData, 6, unit->AlarmRawBits);
    bms_can_send(pick_id(board_type, CANID_STATUS_TEST_BOT, CANID_STATUS_TEST_TOP), BMS_TxData);

    BMS_TxData[0] = unit->value_SafetyAlertA;
    BMS_TxData[1] = unit->value_SafetyAlertB;
    BMS_TxData[2] = unit->value_SafetyAlertC;
    BMS_TxData[3] = unit->value_SafetyStatusA;
    BMS_TxData[4] = unit->value_SafetyStatusB;
    BMS_TxData[5] = unit->value_SafetyStatusC;
    BMS_TxData[6] = unit->ProtectionsTriggered;
    BMS_TxData[7] = unit->PF_ProtectionsTriggered;
    bms_can_send(pick_id(board_type, CANID_SAFETY_BOT, CANID_SAFETY_TOP), BMS_TxData);

    BMS_TxData[0] = unit->value_PFAlertA;
    BMS_TxData[1] = unit->value_PFAlertB;
    BMS_TxData[2] = unit->value_PFAlertC;
    BMS_TxData[3] = unit->value_PFAlertD;
    BMS_TxData[4] = unit->value_PFStatusA;
    BMS_TxData[5] = unit->value_PFStatusB;
    BMS_TxData[6] = unit->value_PFStatusC;
    BMS_TxData[7] = unit->value_PFStatusD;
    bms_can_send(pick_id(board_type, CANID_PF_BOT, CANID_PF_TOP), BMS_TxData);

    /* FET status only. CB_Status1 now has its own frame below, and AlarmBits
     * is dropped here since the equivalent Alarm field is already sent in
     * the status frame (CANID_STATUS_BOT/TOP). */
    BMS_TxData[0] = fet_unit->FET_Status;
    BMS_TxData[1] = fet_unit->CHG;
    BMS_TxData[2] = fet_unit->DSG;
    BMS_TxData[3] = fet_unit->PDSG;
    BMS_TxData[4] = 0;
    BMS_TxData[5] = 0;
    BMS_TxData[6] = 0;
    BMS_TxData[7] = 0;
    bms_can_send(pick_id(board_type, CANID_FET_BOT, CANID_FET_TOP), BMS_TxData);

    put_u16_be(BMS_TxData, 0, unit->CB_Status1);
    put_u16_be(BMS_TxData, 2, 0);
    put_u16_be(BMS_TxData, 4, 0);
    put_u16_be(BMS_TxData, 6, 0);
    bms_can_send(pick_id(board_type, CANID_CB_STATUS1_BOT, CANID_CB_STATUS1_TOP), BMS_TxData);

    put_u32_be(BMS_TxData, 0, (uint32_t)current_unit->AccumulatedCharge_Int);
    put_u32_be(BMS_TxData, 4, current_unit->AccumulatedCharge_Frac);
    bms_can_send(pick_id(board_type, CANID_COULOMB1_BOT, CANID_COULOMB1_TOP), BMS_TxData);

    put_u32_be(BMS_TxData, 0, current_unit->AccumulatedCharge_Time);
    put_u32_be(BMS_TxData, 4, (uint32_t)current_unit->CC2_Counts);
    bms_can_send(pick_id(board_type, CANID_COULOMB2_BOT, CANID_COULOMB2_TOP), BMS_TxData);

    /* LD pin unused on both boards per WSC pin-usage table; frame shrunk to 6 bytes, LD_Voltage dropped */
    put_u32_be(BMS_TxData, 0, (uint32_t)current_unit->CC3_Counts);
    put_u16_be(BMS_TxData, 4, unit->Battery_Voltage_Sum);
    bms_can_send_len(pick_id(board_type, CANID_COULOMB3_BOT, CANID_COULOMB3_TOP), BMS_TxData, FDCAN_DLC_BYTES_6);

    put_u16_be(BMS_TxData, 0, current_unit->SOC_Permille);
    put_u16_be(BMS_TxData, 2, 0);
    put_u16_be(BMS_TxData, 4, 0);
    put_u16_be(BMS_TxData, 6, 0);
    bms_can_send(pick_id(board_type, CANID_SOC_TEST_BOT, CANID_SOC_TEST_TOP), BMS_TxData);

    send_u16_array(pick_id(board_type, CANID_CUV_BASE_BOT, CANID_CUV_BASE_TOP), unit->CUV_Snapshot);
    send_u16_array(pick_id(board_type, CANID_COV_BASE_BOT, CANID_COV_BASE_TOP), unit->COV_Snapshot);
    send_u16_array(pick_id(board_type, CANID_CB_STATUS2_BASE_BOT, CANID_CB_STATUS2_BASE_TOP), unit->CB_Status2);
    send_u16_array(pick_id(board_type, CANID_CB_STATUS3_BASE_BOT, CANID_CB_STATUS3_BASE_TOP), unit->CB_Status3);
}

void T_FDCAN_Send_BMS_Data(uint8_t board_type)
{
    BMS_CAN_SendTestData(board_type);
}
