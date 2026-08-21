/*
 * B_TEST_BMS_can.c
 *
 * CAN transmit helpers for normal run data and test-drive detailed data.
 */

#include "B_TEST_BMS.h"
#include "B_BMS_protect.h"
#include "B_BMS_fan.h"
#include "fdcan.h"

/*
 * CAN ID map per datasheets/bms_can_id_spec.csv.
 * IDs marked "legacy" have no matching entry in that spreadsheet and keep
 * their pre-existing base+offset addresses until a new ID is assigned.
 */
#define CANID_STATUS_BOT            0x040U
#define CANID_STATUS_TOP            0x044U
#define CANID_VOLTAGE_BOT           0x041U
#define CANID_VOLTAGE_TOP           0x045U
#define CANID_CURRENT_BOT           0x042U
#define CANID_CURRENT_TOP           0x046U
/* SOC/팬 — 팩 단위 프레임. 보드별 쌍이 아니라 하나만 보낸다.
 * SOC는 32S 직렬 스트링 하나의 속성이고(모든 셀에 같은 전류), 팬도 팩 단위다.
 * BMS_CAN_SendRunData()가 두 번 호출되므로 아래 보드의 호출에서만 송신한다. */
#define CANID_PACK_SOC_FAN          0x043U
#define BMS_CAN_PACK_FRAME_BOARD    BOT

/* 0x047 — 예약(미사용).
 * 이전에는 0x043과 바이트 단위로 완전히 동일한 SOC 프레임을 여기로도 보냈다. 중복이라 제거했고,
 * 뒤쪽 ID를 당기면 이미 배포된 명세가 전부 어긋나므로 자리는 비워 둔다. 재사용하지 말 것. */

#define CANID_CELLV_BASE_BOT        0x048U /* 4 frames: 0x048-0x04B */
#define CANID_CELLV_BASE_TOP        0x051U /* 4 frames: 0x051-0x054 */
#define CANID_STACKPACKV_TEST_BOT   0x111U /* legacy: duplicate of run-data voltage frame */
#define CANID_STACKPACKV_TEST_TOP   0x211U /* legacy */
#define CANID_CURRENT_DETAIL_BOT    0x112U /* legacy: CC1/CC3 current + CB active-cell count */
#define CANID_CURRENT_DETAIL_TOP    0x212U /* legacy */
#define CANID_TEMP1_BOT             0x04CU
#define CANID_TEMP1_TOP             0x055U
#define CANID_TEMP2_BOT             0x04DU
#define CANID_TEMP2_TOP             0x056U
#define CANID_STATUS_TEST_BOT       0x120U /* legacy: duplicate of run-data status frame */
#define CANID_STATUS_TEST_TOP       0x220U /* legacy */
#define CANID_SAFETY_BOT            0x05AU
#define CANID_SAFETY_TOP            0x06DU
#define CANID_PF_BOT                0x05BU
#define CANID_PF_TOP                0x06EU
#define CANID_FET_BOT               0x123U /* legacy: FET_Status/CHG/DSG/PDSG only */
#define CANID_FET_TOP               0x223U /* legacy */
#define CANID_CB_STATUS1_BOT        0x05CU
#define CANID_CB_STATUS1_TOP        0x06FU
#define CANID_COULOMB1_BOT          0x04EU
#define CANID_COULOMB1_TOP          0x057U
#define CANID_COULOMB2_BOT          0x04FU
#define CANID_COULOMB2_TOP          0x058U
#define CANID_COULOMB3_BOT          0x050U
#define CANID_COULOMB3_TOP          0x059U
/* TEST용 SOC — legacy. RUN 0x043과 마찬가지로 팩 단위라 하나만 보낸다.
 * 0x233은 예약(미사용). 이전에는 0x133과 바이트 단위로 완전히 동일한 프레임을 보냈다. */
#define CANID_PACK_SOC_TEST         0x133U
#define CANID_CUV_BASE_BOT          0x05DU /* 4 frames: 0x05D-0x060 */
#define CANID_CUV_BASE_TOP          0x070U /* 4 frames: 0x070-0x073 */
#define CANID_COV_BASE_BOT          0x061U /* 4 frames: 0x061-0x064 */
#define CANID_COV_BASE_TOP          0x074U /* 4 frames: 0x074-0x077 */
#define CANID_CB_STATUS2_BASE_BOT   0x065U /* 4 frames: 0x065-0x068 */
#define CANID_CB_STATUS2_BASE_TOP   0x078U /* 4 frames: 0x078-0x07B */
#define CANID_CB_STATUS3_BASE_BOT   0x069U /* 4 frames: 0x069-0x06C */
#define CANID_CB_STATUS3_BASE_TOP   0x07CU /* 4 frames: 0x07C-0x07F */
/* MCU 보호 감시자 상태 — 팩 단위 (보드별 아님).
 *
 * 0x080에 있었으나 0x300으로 옮겼다. 두 가지 이유:
 *   - BMS 측정 데이터 블록(0x040~0x07F)과 성격이 다르다. 이건 진단 프레임이다.
 *   - CAN은 ID가 낮을수록 우선순위가 높다. 진단 프레임이 실측 데이터보다 앞설 이유가 없다.
 *
 * !! 확인 필요 !! 0x300은 "0x310보다 작은 ID"라는 지시에 맞춰 그 바로 아래로 잡은 값이다.
 * 차량 전체 CAN 할당표와 대조해 다른 노드와 겹치지 않는지 확인할 것. 겹치면 이 한 줄만 고치면 된다. */
#define CANID_PROTECT_DIAG          0x300U

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
    BMS_Unit *current_unit = &BMS[BMS_CURRENT_BOARD];

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

    /* SOC/팬 프레임은 팩 단위라 BOT 호출에서 한 번만 보낸다.
     *
     * 이전에는 board_type과 무관하게 BMS[BOT]의 값을 담아 0x043과 0x047 두 ID로 보냈다.
     * 두 프레임이 바이트 단위로 완전히 동일해 대역폭만 낭비했다. SOC는 32S 직렬 스트링 하나의
     * 속성이라 보드별로 나눌 수 없고(모든 셀에 같은 전류가 흐른다), 팬도 팩 단위다.
     *
     * byte 2~7은 원래 0으로 비어 있던 자리다. 팬은 이 보드에서 압도적으로 큰 소비원인데
     * (100% = 8.52 W, MCU는 8.6 mW) 밖에서 몇 %로 도는지 볼 방법이 없어 여기에 실었다.
     * temp_valid = 0이면 유효 온도를 한 번도 못 읽은 상태 = 서미스터 미실장 의심. */
    if (board_type == BMS_CAN_PACK_FRAME_BOARD) {
        put_u16_be(BMS_TxData, 0, current_unit->SOC_Permille);
        BMS_TxData[2] = BMS_FanControl_GetDuty();        /* 팬 duty [%] */
        BMS_TxData[3] = BMS_FanControl_TempEverValid();  /* 0 = 유효 온도 미수신 */
        put_u16_be(BMS_TxData, 4, 0);
        put_u16_be(BMS_TxData, 6, 0);
        bms_can_send(CANID_PACK_SOC_FAN, BMS_TxData);
    }
}

static uint8_t clamp_u8(uint16_t value)
{
    return (value > 255U) ? 255U : (uint8_t)value;
}

/*
 * MCU 보호 감시자 진단 프레임 (0x080).
 *
 * "왜 끊겼는지"를 차 밖에서 바로 읽기 위한 프레임이다. 기존 프레임들은 SafetyStatus 원본만 나가서
 * 사람이 비트를 손으로 디코딩해야 했고, MCU가 개입해서 끊은 경우는 아예 관측할 수 없었다.
 *
 *  byte 0 : 트립 사유 (BMS_TripReason_t)
 *  byte 1 : 비트 플래그
 *           b0 BOTHOFF 어서트  b1 latch
 *           b2 CHG 기대=OFF    b3 DSG 기대=OFF
 *           b4 CHG 실측=ON     b5 DSG 실측=ON
 *           => b2와 b4가 동시에 1이면 차단 경로 고장 (b3/b5도 동일)
 *  byte 2 : BOTHOFF 해제 재시도 횟수
 *  byte 3 : CHG 불일치 연속 사이클
 *  byte 4 : DSG 불일치 연속 사이클
 *  byte 5 : 스택 측정 불일치 연속 사이클
 *  byte 6 : TOP 연속 통신 실패 사이클
 *  byte 7 : BOT 연속 통신 실패 사이클
 */
void BMS_CAN_SendProtectDiag(void)
{
    uint8_t flags = 0U;

    if (BMS_Protect.bothoff_asserted) { flags |= 0x01U; }
    if (BMS_Protect.latched)          { flags |= 0x02U; }
    if (BMS_Protect.chg_expected_off) { flags |= 0x04U; }
    if (BMS_Protect.dsg_expected_off) { flags |= 0x08U; }
    if (BMS_Protect.chg_observed_on)  { flags |= 0x10U; }
    if (BMS_Protect.dsg_observed_on)  { flags |= 0x20U; }

    BMS_TxData[0] = (uint8_t)BMS_Protect.reason;
    BMS_TxData[1] = flags;
    BMS_TxData[2] = BMS_Protect.retry_count;
    BMS_TxData[3] = clamp_u8(BMS_Protect.chg_mismatch_cycles);
    BMS_TxData[4] = clamp_u8(BMS_Protect.dsg_mismatch_cycles);
    BMS_TxData[5] = clamp_u8(BMS_Protect.stack_mismatch_cycles);
    BMS_TxData[6] = clamp_u8(BMS[TOP].comm_fail_cycles);
    BMS_TxData[7] = clamp_u8(BMS[BOT].comm_fail_cycles);
    bms_can_send(CANID_PROTECT_DIAG, BMS_TxData);
}

void BMS_CAN_SendTestData(uint8_t board_type)
{
    if (board_type >= STACK) {
        return;
    }

    BMS_Unit *unit = &BMS[board_type];
    BMS_Unit *current_unit = &BMS[BMS_CURRENT_BOARD];
    BMS_Unit *fet_unit = &BMS[BMS_FET_BOARD];

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

    /* RUN 쪽 0x043과 같은 이유로 팩 단위다 — BOT 호출에서 한 번만 보낸다. 0x233은 예약. */
    if (board_type == BMS_CAN_PACK_FRAME_BOARD) {
        put_u16_be(BMS_TxData, 0, current_unit->SOC_Permille);
        put_u16_be(BMS_TxData, 2, 0);
        put_u16_be(BMS_TxData, 4, 0);
        put_u16_be(BMS_TxData, 6, 0);
        bms_can_send(CANID_PACK_SOC_TEST, BMS_TxData);
    }

    send_u16_array(pick_id(board_type, CANID_CUV_BASE_BOT, CANID_CUV_BASE_TOP), unit->CUV_Snapshot);
    send_u16_array(pick_id(board_type, CANID_COV_BASE_BOT, CANID_COV_BASE_TOP), unit->COV_Snapshot);
    send_u16_array(pick_id(board_type, CANID_CB_STATUS2_BASE_BOT, CANID_CB_STATUS2_BASE_TOP), unit->CB_Status2);
    send_u16_array(pick_id(board_type, CANID_CB_STATUS3_BASE_BOT, CANID_CB_STATUS3_BASE_TOP), unit->CB_Status3);
}

void T_FDCAN_Send_BMS_Data(uint8_t board_type)
{
    BMS_CAN_SendTestData(board_type);
}
