/*
 * B_BMS.h
 *
 * Common BQ76972 data and command interface.
 */

#ifndef INC_B_BMS_H_
#define INC_B_BMS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "i2c.h"
#include "B_BMS_cmd.h"

#define TOP 0
#define BOT 1
#define STACK 2

#define CRC_Mode 1
#define MAX_BUFFER_SIZE 64

#define R 0
#define W 1
#define W2 2
#define R1 3
#define W1 4

typedef struct _BMS_Unit {
    I2C_HandleTypeDef *hi2c;
    uint8_t dev_addr;

    uint8_t RX_data[2];
    uint8_t RX_SubData[32];

    uint16_t CellVoltage[16];
    uint16_t Stack_Voltage_Raw;
    uint16_t Pack_Voltage_Raw;
    uint16_t LD_Voltage_Raw;
    uint32_t Stack_Voltage;
    uint32_t Pack_Voltage;
    uint32_t LD_Voltage;

    int16_t Pack_Current;
    int16_t CC1_Current;
    int16_t CC3_Current;
    int32_t CC2_Counts;
    int32_t CC3_Counts;

    float CELL_Temp;
    float FET_Temp;
    float Int_Temp;
    float CFETOFF_Temp;
    float HDQ_Temp;
    float Max_Cell_Temp;
    float Min_Cell_Temp;
    float Avg_Cell_Temp;

    uint16_t AlarmBits;
    uint16_t AlarmRawBits;
    uint16_t BattStat;

    uint8_t value_SafetyAlertA;
    uint8_t value_SafetyAlertB;
    uint8_t value_SafetyAlertC;
    uint8_t value_SafetyStatusA;
    uint8_t value_SafetyStatusB;
    uint8_t value_SafetyStatusC;

    uint8_t value_PFAlertA;
    uint8_t value_PFAlertB;
    uint8_t value_PFAlertC;
    uint8_t value_PFAlertD;
    uint8_t value_PFStatusA;
    uint8_t value_PFStatusB;
    uint8_t value_PFStatusC;
    uint8_t value_PFStatusD;

    uint8_t CUV_Fault;
    uint8_t COV_Fault;
    uint8_t UTC_Fault;
    uint8_t UTD_Fault;
    uint8_t UTINT_Fault;
    uint8_t OTC_Fault;
    uint8_t OTD_Fault;
    uint8_t OTINT_Fault;
    uint8_t HWDF_Fault;
    uint8_t OCD3_Fault;
    uint8_t SOV_PF;
    uint8_t SUV_PF;
    uint8_t VIMA_PF;
    uint8_t VIMR_PF;
    uint8_t PF_ProtectionsTriggered;
    uint8_t ProtectionsTriggered;

    uint8_t FET_Status;
    uint8_t OTF_Fault;
    uint8_t PDSG;
    uint8_t DSG;
    uint8_t CHG;
    uint8_t SCD_Fault;
    uint8_t OCD1_Fault;
    uint8_t OCD2_Fault;
    uint8_t OCC_Fault;
    uint8_t COVL_Fault;
    uint8_t OCDL_Fault;
    uint8_t SCDL_Fault;

    uint16_t MaxCellVolatge;
    uint16_t MinCellVolatge;
    uint16_t Battery_Voltage_Sum;
    uint16_t REG1_Voltage;

    uint16_t CUV_Snapshot[16];
    uint16_t COV_Snapshot[16];
    uint16_t CB_ActiveCells;
    uint16_t CB_Status1;
    uint16_t CB_Status2[16];
    uint16_t CB_Status3[16];

    int32_t AccumulatedCharge_Int;
    uint32_t AccumulatedCharge_Frac;
    uint32_t AccumulatedCharge_Time;

    uint16_t SOC_Permille;

    uint32_t Global_Fault_Flags;
} BMS_Unit;

extern BMS_Unit BMS[STACK];
extern uint8_t LV_BMS_initOK;
extern uint16_t LV_BMS_running;
extern unsigned int RX_CRC_Fail;
extern unsigned int I2C_HAL_Fail;

void LV_BMS_MAIN_RUN(void);
void LV_BMS_WHILE_RUN(void);

void BQ769x2_BOTHOFF(void);
void BQ769x2_BOTHOFF_RESET(void);
void BQ769x2_LOWV_SHUTDOWN(void);
void BQ769x2_Hard_Shutdown(void);
void BQ769x2_Wake_Up(void);

void CopyArray(uint8_t *source, uint8_t *dest, uint8_t count);
unsigned char Checksum(unsigned char *ptr, unsigned char len);
unsigned char CRC8(unsigned char *ptr, unsigned char len);
void I2C_WriteReg(BMS_Unit *unit, uint8_t reg_addr, uint8_t *reg_data, uint8_t count);
int I2C_ReadReg(BMS_Unit *unit, uint8_t reg_addr, uint8_t *reg_data, uint8_t count);
void BQ769x2_SetRegister(BMS_Unit *unit, uint16_t reg_addr, uint32_t reg_data, uint8_t datalen);
void DirectCommands(BMS_Unit *unit, uint8_t command, uint16_t data, uint8_t type);
void CommandSubcommands(BMS_Unit *unit, uint16_t command);
void Subcommands(BMS_Unit *unit, uint16_t command, uint16_t data, uint8_t type);

uint16_t BQ769x2_ReadAlarmStatus(BMS_Unit *unit);
void BQ769x2_ReadAlarmRawStatus(BMS_Unit *unit);
void BQ769x2_ReadSafetyStatus(BMS_Unit *unit);
void BQ769x2_ReadPFStatus(BMS_Unit *unit);
void BQ769x2_ReadFETStatus(BMS_Unit *unit);
void BQ769x2_ReadBATTStatus(BMS_Unit *unit);
uint32_t BQ769x2_ReadVoltage(BMS_Unit *unit, uint8_t command);
void BQ769x2_ReadAllVoltages(BMS_Unit *unit);
int16_t BQ769x2_ReadCurrent(BMS_Unit *unit);
float BQ769x2_ReadTemperature(BMS_Unit *unit, uint8_t command);
void BQ769x2_ReadData(BMS_Unit *unit);
void BQ769x2_ReadDASTATUS5(BMS_Unit *unit);
void BQ769x2_ReadDASTATUS6(BMS_Unit *unit);
void BMS_SyncSharedHardwareData(void);
void BQ769x2_ReadLargeSubcommand(BMS_Unit *unit, uint16_t command, uint16_t *dest_array);
void BQ769x2_Read_Extra_Direct(BMS_Unit *unit);
void BQ769x2_Read_Snapshots(BMS_Unit *unit);

void BMS_FanControl_Init(void);
void BMS_FanControl_Update(void);
void BMS_FanControl_SetDuty(uint8_t duty_percent);
uint8_t BMS_FanControl_GetDuty(void);

uint8_t Check_BMS_Sleep_State(BMS_Unit *unit);
void delayUS(uint32_t us);

#ifdef __cplusplus
}
#endif

#endif /* INC_B_BMS_H_ */
