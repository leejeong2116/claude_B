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

// 두 칩이 물리 하드웨어를 나눠 쓴다. 션트는 BOT에만, FET 게이트는 TOP에만 붙어 있어
// 전류/쿨롱카운터는 BOT, FET 상태/온도는 TOP 값만 유효하다 (자세한 내용은 CLAUDE.md).
#define BMS_CURRENT_BOARD BOT
#define BMS_FET_BOARD TOP

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

    // 보드별 통신 상태. RX_CRC_Fail/I2C_HAL_Fail은 전역이라 어느 보드가 죽었는지 구분할 수 없어
    // 별도로 둔다. comm_fail_this_cycle은 I2C_ReadReg()가 실패할 때마다 세팅되고,
    // LV_BMS_WHILE_RUN()이 사이클 시작에 지우고 끝에 comm_fail_cycles로 집계한다.
    uint8_t comm_fail_this_cycle;
    uint16_t comm_fail_cycles;   // 연속 실패 사이클 수 (성공하면 0)

    // 타당한 온도를 한 번이라도 읽었는가. BMS[]는 0 초기화라 온도 초기값 0.0f가
    // 타당 범위(-40~130) 안이고, 그래서 값만 봐서는 미읽음과 0도를 구별할 수 없다.
    uint8_t temp_ever_valid;
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
// 반환값: 0 = 성공, -1 = I2C/CRC 실패(레지스터 미갱신, 이전 값 유지) — 호출부는 반드시 확인할 것
int DirectCommands(BMS_Unit *unit, uint8_t command, uint16_t data, uint8_t type);
void CommandSubcommands(BMS_Unit *unit, uint16_t command);
int Subcommands(BMS_Unit *unit, uint16_t command, uint16_t data, uint8_t type);

// ok가 NULL이 아니면 읽기 성공 여부(1/0)를 기록한다. ok==0이면 반환값은 신뢰할 수 없는(스크래치 버퍼 잔존) 값이므로 사용하지 말 것.
uint16_t BQ769x2_ReadAlarmStatus(BMS_Unit *unit, int *ok);
void BQ769x2_ReadAlarmRawStatus(BMS_Unit *unit);
void BQ769x2_ReadSafetyStatus(BMS_Unit *unit);
void BQ769x2_ReadPFStatus(BMS_Unit *unit);
void BQ769x2_ReadFETStatus(BMS_Unit *unit);
void BQ769x2_ReadBATTStatus(BMS_Unit *unit);
uint32_t BQ769x2_ReadVoltage(BMS_Unit *unit, uint8_t command, int *ok);
void BQ769x2_ReadAllVoltages(BMS_Unit *unit);
int16_t BQ769x2_ReadCurrent(BMS_Unit *unit, int *ok);
float BQ769x2_ReadTemperature(BMS_Unit *unit, uint8_t command, int *ok);
void BQ769x2_ReadData(BMS_Unit *unit);
void BQ769x2_ReadDASTATUS5(BMS_Unit *unit);
void BQ769x2_ReadDASTATUS6(BMS_Unit *unit);
void BMS_SyncSharedHardwareData(void);
void BQ769x2_ReadLargeSubcommand(BMS_Unit *unit, uint16_t command, uint16_t *dest_array);
void BQ769x2_Read_Extra_Direct(BMS_Unit *unit);
void BQ769x2_Read_Snapshots(BMS_Unit *unit);

/* 팬 제어는 B_BMS_fan.h 로 분리했다. */

uint8_t Check_BMS_Sleep_State(BMS_Unit *unit);
void delayUS(uint32_t us);

#ifdef __cplusplus
}
#endif

#endif /* INC_B_BMS_H_ */
