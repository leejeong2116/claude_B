/*
 * B_TEST_BMS.c
 *
 * Test-drive extra readout functions built on the common BQ76972 driver.
 */

#include "B_TEST_BMS.h"

static uint16_t test_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

void BQ769x2_ReadDiagnostics(BMS_Unit *unit)
{
    BQ769x2_ReadLargeSubcommand(unit, CUV_SNAPSHOT, unit->CUV_Snapshot);
    BQ769x2_ReadLargeSubcommand(unit, COV_SNAPSHOT, unit->COV_Snapshot);
    BQ769x2_ReadLargeSubcommand(unit, CBSTATUS2, unit->CB_Status2);
    BQ769x2_ReadLargeSubcommand(unit, CBSTATUS3, unit->CB_Status3);

    Subcommands(unit, CBSTATUS1, 0x00, R);
    unit->CB_Status1 = test_u16_le(&unit->RX_SubData[0]);

    Subcommands(unit, CB_ACTIVE_CELLS, 0x00, R);
    unit->CB_ActiveCells = test_u16_le(&unit->RX_SubData[0]);
}

void BQ769x2_ReadTestExtraData(BMS_Unit *unit)
{
    BQ769x2_Read_Extra_Direct(unit);
    BQ769x2_ReadDiagnostics(unit);
}

void BMS_Test_ReadAll(void)
{
    for (int i = 0; i < STACK; i++) {
        BQ769x2_ReadData(&BMS[i]);
        BQ769x2_ReadAlarmRawStatus(&BMS[i]);
        BQ769x2_ReadSafetyStatus(&BMS[i]);
        BQ769x2_ReadPFStatus(&BMS[i]);
        BQ769x2_ReadBATTStatus(&BMS[i]);
        BQ769x2_ReadFETStatus(&BMS[i]);
        BQ769x2_ReadDASTATUS5(&BMS[i]);
        BQ769x2_ReadDASTATUS6(&BMS[i]);
        BQ769x2_ReadTestExtraData(&BMS[i]);
    }

    BMS_SyncSharedHardwareData();
}

void T_LV_BMS_MAIN_RUN(void)
{
    LV_BMS_MAIN_RUN();
}

void T_LV_BMS_WHILE_RUN(void)
{
    BMS_Test_ReadAll();
}
