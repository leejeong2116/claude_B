/*
 * B_BMS.c
 *
 * Common BQ76972 read/write and runtime data update functions.
 */

#include "main.h"
#include "i2c.h"
#include "tim.h"
#include "gpio.h"

#include "B_BMS.h"
#include "B_BMS_init.h"
#include "B_BMS_soc.h"

unsigned int RX_CRC_Fail = 0;
unsigned int I2C_HAL_Fail = 0;
uint8_t LV_BMS_initOK = 0;
uint16_t LV_BMS_running = 0;
BMS_Unit BMS[STACK] = {0};

#define BMS_FAN_PWM_CHANNEL TIM_CHANNEL_2
#define BMS_FAN_OFF_TEMP_C 33.0f
#define BMS_FAN_ON_TEMP_C 35.0f
#define BMS_FAN_FULL_TEMP_C 55.0f
#define BMS_FAN_MIN_DUTY_PERCENT 20U
#define BMS_FAN_MAX_DUTY_PERCENT 100U
#define BMS_CURRENT_BOARD BOT
#define BMS_FET_BOARD TOP

static uint8_t BMS_FanDuty = 0;

static uint16_t u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static int16_t i16_le(const uint8_t *data)
{
    return (int16_t)u16_le(data);
}

static uint32_t u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static int32_t i32_le(const uint8_t *data)
{
    return (int32_t)u32_le(data);
}

static float temp_01k_to_c(uint16_t raw)
{
    return (0.1f * (float)raw) - 273.15f;
}

static uint8_t is_current_board(const BMS_Unit *unit)
{
    return (unit == &BMS[BMS_CURRENT_BOARD]) ? 1U : 0U;
}

static uint8_t is_fet_board(const BMS_Unit *unit)
{
    return (unit == &BMS[BMS_FET_BOARD]) ? 1U : 0U;
}

static uint8_t fan_temp_is_valid(float temp_c)
{
    return (temp_c > -40.0f && temp_c < 130.0f) ? 1U : 0U;
}

static float fan_max_valid_temp(const BMS_Unit *unit, uint8_t *valid)
{
    float max_temp = -1000.0f;
    const float temps[] = {
        unit->CELL_Temp,
        unit->Max_Cell_Temp,
        unit->Avg_Cell_Temp
    };

    *valid = 0U;
    for (uint32_t i = 0; i < (sizeof(temps) / sizeof(temps[0])); i++) {
        if (fan_temp_is_valid(temps[i])) {
            if (*valid == 0U || temps[i] > max_temp) {
                max_temp = temps[i];
            }
            *valid = 1U;
        }
    }

    if (is_fet_board(unit) && fan_temp_is_valid(unit->FET_Temp)) {
        if (*valid == 0U || unit->FET_Temp > max_temp) {
            max_temp = unit->FET_Temp;
        }
        *valid = 1U;
    }

    return max_temp;
}

static void sync_current_data(BMS_Unit *target, const BMS_Unit *source)
{
    target->Pack_Current = source->Pack_Current;
    target->CC1_Current = source->CC1_Current;
    target->CC3_Current = source->CC3_Current;
    target->CC2_Counts = source->CC2_Counts;
    target->CC3_Counts = source->CC3_Counts;
    target->AccumulatedCharge_Int = source->AccumulatedCharge_Int;
    target->AccumulatedCharge_Frac = source->AccumulatedCharge_Frac;
    target->AccumulatedCharge_Time = source->AccumulatedCharge_Time;
    target->SOC_Permille = source->SOC_Permille;
}

static void sync_fet_data(BMS_Unit *target, const BMS_Unit *source)
{
    target->FET_Status = source->FET_Status;
    target->CHG = source->CHG;
    target->DSG = source->DSG;
    target->PDSG = source->PDSG;
    target->FET_Temp = source->FET_Temp;
}

static uint8_t fan_duty_from_temp(float temp_c, uint8_t valid)
{
    if (valid == 0U) {
        return (LV_BMS_running > 0U) ? BMS_FAN_MAX_DUTY_PERCENT : 0U;
    }

    if (temp_c <= BMS_FAN_OFF_TEMP_C) {
        return 0U;
    }

    if (BMS_FanDuty == 0U && temp_c < BMS_FAN_ON_TEMP_C) {
        return 0U;
    }

    if (temp_c >= BMS_FAN_FULL_TEMP_C) {
        return BMS_FAN_MAX_DUTY_PERCENT;
    }

    float range = BMS_FAN_FULL_TEMP_C - BMS_FAN_ON_TEMP_C;
    float ratio = (temp_c - BMS_FAN_ON_TEMP_C) / range;
    if (ratio < 0.0f) {
        ratio = 0.0f;
    }

    float duty = (float)BMS_FAN_MIN_DUTY_PERCENT +
                 (ratio * (float)(BMS_FAN_MAX_DUTY_PERCENT - BMS_FAN_MIN_DUTY_PERCENT));

    return (uint8_t)(duty + 0.5f);
}

void LV_BMS_MAIN_RUN(void)
{
    BMS[TOP].hi2c = &hi2c2;
    BMS[TOP].dev_addr = 0x10;
    BMS[BOT].hi2c = &hi2c4;
    BMS[BOT].dev_addr = 0x10;

    for (int i = 0; i < STACK; i++) {
        HAL_I2C_Init(BMS[i].hi2c);
        delayUS(10000);
        CommandSubcommands(&BMS[i], BQ769x2_RESET);
        delayUS(60000);
        BQ769x2_Init(&BMS[i]);
        delayUS(10000);
    }

    LV_BMS_initOK++;
    delayUS(60000);
    delayUS(60000);
    delayUS(60000);
    delayUS(60000);
}

void LV_BMS_WHILE_RUN(void)
{
    for (int i = 0; i < STACK; i++) {
        BQ769x2_ReadData(&BMS[i]);
        BQ769x2_ReadAlarmRawStatus(&BMS[i]);
        BQ769x2_ReadSafetyStatus(&BMS[i]);
        BQ769x2_ReadPFStatus(&BMS[i]);
        BQ769x2_ReadBATTStatus(&BMS[i]);
        if (i == BMS_FET_BOARD) {
            BQ769x2_ReadFETStatus(&BMS[i]);
        }
        BQ769x2_ReadDASTATUS5(&BMS[i]);
        BQ769x2_ReadDASTATUS6(&BMS[i]);
    }

    BMS_SOC_Update();
    BMS_SyncSharedHardwareData();
    LV_BMS_running++;
}

void CopyArray(uint8_t *source, uint8_t *dest, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++) {
        dest[i] = source[i];
    }
}

unsigned char Checksum(unsigned char *ptr, unsigned char len)
{
    unsigned char checksum = 0;

    for (unsigned char i = 0; i < len; i++) {
        checksum += ptr[i];
    }

    return (unsigned char)(0xff & ~checksum);
}

unsigned char CRC8(unsigned char *ptr, unsigned char len)
{
    unsigned char crc = 0;

    while (len-- != 0) {
        for (unsigned char i = 0x80; i != 0; i /= 2) {
            if ((crc & 0x80) != 0) {
                crc *= 2;
                crc ^= 0x107;
            } else {
                crc *= 2;
            }

            if ((*ptr & i) != 0) {
                crc ^= 0x107;
            }
        }
        ptr++;
    }

    return crc;
}

void I2C_WriteReg(BMS_Unit *unit, uint8_t reg_addr, uint8_t *reg_data, uint8_t count)
{
    uint8_t TX_Buffer[MAX_BUFFER_SIZE] = {0};

#if CRC_Mode
    uint8_t crc_count = count * 2U;
    uint8_t crc1stByteBuffer[3] = {unit->dev_addr, reg_addr, reg_data[0]};
    unsigned int j = 2;

    TX_Buffer[0] = reg_data[0];
    TX_Buffer[1] = CRC8(crc1stByteBuffer, 3);

    for (unsigned int i = 1; i < count; i++) {
        TX_Buffer[j++] = reg_data[i];
        TX_Buffer[j++] = CRC8(&reg_data[i], 1);
    }

    if (HAL_I2C_Mem_Write(unit->hi2c, unit->dev_addr, reg_addr, 1, TX_Buffer, crc_count, 1000) != HAL_OK) {
        I2C_HAL_Fail++;
    }
#else
    if (HAL_I2C_Mem_Write(unit->hi2c, unit->dev_addr, reg_addr, 1, reg_data, count, 1000) != HAL_OK) {
        I2C_HAL_Fail++;
    }
#endif
}

int I2C_ReadReg(BMS_Unit *unit, uint8_t reg_addr, uint8_t *reg_data, uint8_t count)
{
    uint8_t RX_Buffer[MAX_BUFFER_SIZE] = {0};

#if CRC_Mode
    uint8_t crc_count = count * 2U;
    uint8_t ReceiveBuffer[MAX_BUFFER_SIZE] = {0};
    unsigned int j = 2;

    if (HAL_I2C_Mem_Read(unit->hi2c, unit->dev_addr, reg_addr, 1, ReceiveBuffer, crc_count, 1000) != HAL_OK) {
        I2C_HAL_Fail++;
        return -1;
    }

    uint8_t crc1stByteBuffer[4] = {unit->dev_addr, reg_addr, (uint8_t)(unit->dev_addr + 1U), ReceiveBuffer[0]};
    uint8_t crc_ok = 1U;
    if (CRC8(crc1stByteBuffer, 4) != ReceiveBuffer[1]) {
        RX_CRC_Fail++;
        crc_ok = 0U;
    }

    RX_Buffer[0] = ReceiveBuffer[0];

    for (unsigned int i = 1; i < count; i++) {
        RX_Buffer[i] = ReceiveBuffer[j];
        if (CRC8(&ReceiveBuffer[j], 1) != ReceiveBuffer[j + 1U]) {
            RX_CRC_Fail++;
            crc_ok = 0U;
        }
        j += 2U;
    }

    // CRC 실패 시 손상된 값을 reg_data에 반영하지 않고 이전 값을 그대로 유지한다.
    if (!crc_ok) {
        return -1;
    }

    CopyArray(RX_Buffer, reg_data, count);
#else
    if (HAL_I2C_Mem_Read(unit->hi2c, unit->dev_addr, reg_addr, 1, reg_data, count, 2000) != HAL_OK) {
        I2C_HAL_Fail++;
        return -1;
    }
#endif

    return 0;
}

void BQ769x2_SetRegister(BMS_Unit *unit, uint16_t reg_addr, uint32_t reg_data, uint8_t datalen)
{
    uint8_t TX_Buffer[2] = {0};
    uint8_t TX_RegData[6] = {0};

    TX_RegData[0] = (uint8_t)(reg_addr & 0xffU);
    TX_RegData[1] = (uint8_t)((reg_addr >> 8) & 0xffU);
    for (int i = 0; i < datalen; i++) {
        TX_RegData[i + 2] = (uint8_t)((reg_data >> (8 * i)) & 0xffU);
    }

    I2C_WriteReg(unit, 0x3E, TX_RegData, (uint8_t)(2U + datalen));
    delayUS(2000);
    TX_Buffer[0] = Checksum(TX_RegData, (unsigned char)(2U + datalen));
    TX_Buffer[1] = (uint8_t)(0x04U + datalen);
    I2C_WriteReg(unit, 0x60, TX_Buffer, 2);
    delayUS(2000);
}

void CommandSubcommands(BMS_Unit *unit, uint16_t command)
{
    uint8_t TX_Reg[2] = {
        (uint8_t)(command & 0xffU),
        (uint8_t)((command >> 8) & 0xffU)
    };

    I2C_WriteReg(unit, 0x3E, TX_Reg, 2);
    delayUS(2000);
}

void Subcommands(BMS_Unit *unit, uint16_t command, uint16_t data, uint8_t type)
{
    uint8_t TX_Reg[4] = {
        (uint8_t)(command & 0xffU),
        (uint8_t)((command >> 8) & 0xffU),
        0,
        0
    };
    uint8_t TX_Buffer[2] = {0};

    if (type == R) {
        I2C_WriteReg(unit, 0x3E, TX_Reg, 2);
        delayUS(2000);
        I2C_ReadReg(unit, 0x40, unit->RX_SubData, 32);
    } else if (type == W) {
        TX_Reg[2] = (uint8_t)(data & 0xffU);
        I2C_WriteReg(unit, 0x3E, TX_Reg, 3);
        delayUS(1000);
        TX_Buffer[0] = Checksum(TX_Reg, 3);
        TX_Buffer[1] = 0x05;
        I2C_WriteReg(unit, 0x60, TX_Buffer, 2);
        delayUS(1000);
    } else if (type == W2) {
        TX_Reg[2] = (uint8_t)(data & 0xffU);
        TX_Reg[3] = (uint8_t)((data >> 8) & 0xffU);
        I2C_WriteReg(unit, 0x3E, TX_Reg, 4);
        delayUS(1000);
        TX_Buffer[0] = Checksum(TX_Reg, 4);
        TX_Buffer[1] = 0x06;
        I2C_WriteReg(unit, 0x60, TX_Buffer, 2);
        delayUS(1000);
    }
}

void DirectCommands(BMS_Unit *unit, uint8_t command, uint16_t data, uint8_t type)
{
    uint8_t TX_data[2] = {
        (uint8_t)(data & 0xffU),
        (uint8_t)((data >> 8) & 0xffU)
    };

    if (type == R) {
        I2C_ReadReg(unit, command, unit->RX_data, 2);
        delayUS(2000);
    } else if (type == R1) {
        I2C_ReadReg(unit, command, unit->RX_data, 1);
        unit->RX_data[1] = 0;
        delayUS(2000);
    } else if (type == W) {
        I2C_WriteReg(unit, command, TX_data, 2);
        delayUS(2000);
    } else if (type == W1) {
        I2C_WriteReg(unit, command, TX_data, 1);
        delayUS(2000);
    }
}

void BQ769x2_BOTHOFF(void)
{
    HAL_GPIO_WritePin(BOT_DFETOFF_GPIO_Port, BOT_DFETOFF_Pin, GPIO_PIN_SET);
}

void BQ769x2_BOTHOFF_RESET(void)
{
    HAL_GPIO_WritePin(BOT_DFETOFF_GPIO_Port, BOT_DFETOFF_Pin, GPIO_PIN_RESET);
}

void BQ769x2_ReadFETStatus(BMS_Unit *unit)
{
    if (!is_fet_board(unit)) {
        return;
    }

    DirectCommands(unit, FETStatus, 0x00, R1);
    unit->FET_Status = unit->RX_data[0];
    unit->CHG = (uint8_t)(unit->RX_data[0] & 0x01U);
    unit->DSG = (uint8_t)((unit->RX_data[0] & 0x04U) >> 2);
    unit->PDSG = (uint8_t)((unit->RX_data[0] & 0x08U) >> 3);
}

void BQ769x2_LOWV_SHUTDOWN(void)
{
    if (BMS[TOP].Stack_Voltage <= 43200U || BMS[BOT].Stack_Voltage <= 43200U) {
        CommandSubcommands(&BMS[TOP], SHUTDOWN);
        CommandSubcommands(&BMS[BOT], SHUTDOWN);
    }
}

void BQ769x2_Hard_Shutdown(void)
{
    HAL_GPIO_WritePin(BOT_RST_SHUT_GPIO_Port, BOT_RST_SHUT_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(TOP_RST_SHUT_GPIO_Port, TOP_RST_SHUT_Pin, GPIO_PIN_SET);
    HAL_Delay(3000);
    HAL_GPIO_WritePin(BOT_RST_SHUT_GPIO_Port, BOT_RST_SHUT_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TOP_RST_SHUT_GPIO_Port, TOP_RST_SHUT_Pin, GPIO_PIN_RESET);
}

void BQ769x2_Wake_Up(void)
{
    HAL_GPIO_WritePin(BOT_WAKE_GPIO_Port, BOT_WAKE_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(TOP_WAKE_GPIO_Port, TOP_WAKE_Pin, GPIO_PIN_SET);
    delayUS(2000);
    HAL_GPIO_WritePin(BOT_WAKE_GPIO_Port, BOT_WAKE_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TOP_WAKE_GPIO_Port, TOP_WAKE_Pin, GPIO_PIN_RESET);
    HAL_Delay(10);
}

uint16_t BQ769x2_ReadAlarmStatus(BMS_Unit *unit)
{
    DirectCommands(unit, AlarmStatus, 0x00, R);
    return u16_le(unit->RX_data);
}

void BQ769x2_ReadAlarmRawStatus(BMS_Unit *unit)
{
    DirectCommands(unit, AlarmRawStatus, 0x00, R);
    unit->AlarmRawBits = u16_le(unit->RX_data);
}

void BQ769x2_ReadSafetyStatus(BMS_Unit *unit)
{
    DirectCommands(unit, SafetyStatusA, 0x00, R1);
    unit->value_SafetyStatusA = unit->RX_data[0];
    DirectCommands(unit, SafetyAlertA, 0x00, R1);
    unit->value_SafetyAlertA = unit->RX_data[0];

    unit->CUV_Fault = (uint8_t)((unit->value_SafetyStatusA & 0x04U) >> 2);
    unit->COV_Fault = (uint8_t)((unit->value_SafetyStatusA & 0x08U) >> 3);
    unit->OCC_Fault = (uint8_t)((unit->value_SafetyStatusA & 0x10U) >> 4);
    unit->OCD1_Fault = (uint8_t)((unit->value_SafetyStatusA & 0x20U) >> 5);
    unit->OCD2_Fault = (uint8_t)((unit->value_SafetyStatusA & 0x40U) >> 6);
    unit->SCD_Fault = (uint8_t)((unit->value_SafetyStatusA & 0x80U) >> 7);

    DirectCommands(unit, SafetyStatusB, 0x00, R1);
    unit->value_SafetyStatusB = unit->RX_data[0];
    DirectCommands(unit, SafetyAlertB, 0x00, R1);
    unit->value_SafetyAlertB = unit->RX_data[0];

    unit->UTC_Fault = (uint8_t)(unit->value_SafetyStatusB & 0x01U);
    unit->UTD_Fault = (uint8_t)((unit->value_SafetyStatusB & 0x02U) >> 1);
    unit->UTINT_Fault = (uint8_t)((unit->value_SafetyStatusB & 0x04U) >> 2);
    unit->OTC_Fault = (uint8_t)((unit->value_SafetyStatusB & 0x10U) >> 4);
    unit->OTD_Fault = (uint8_t)((unit->value_SafetyStatusB & 0x20U) >> 5);
    unit->OTINT_Fault = (uint8_t)((unit->value_SafetyStatusB & 0x40U) >> 6);
    unit->OTF_Fault = (uint8_t)((unit->value_SafetyStatusB & 0x80U) >> 7);

    DirectCommands(unit, SafetyStatusC, 0x00, R1);
    unit->value_SafetyStatusC = unit->RX_data[0];
    DirectCommands(unit, SafetyAlertC, 0x00, R1);
    unit->value_SafetyAlertC = unit->RX_data[0];

    unit->HWDF_Fault = (uint8_t)((unit->value_SafetyStatusC & 0x02U) >> 1);
    unit->COVL_Fault = (uint8_t)((unit->value_SafetyStatusC & 0x10U) >> 4);
    unit->OCDL_Fault = (uint8_t)((unit->value_SafetyStatusC & 0x20U) >> 5);
    unit->SCDL_Fault = (uint8_t)((unit->value_SafetyStatusC & 0x40U) >> 6);
    unit->OCD3_Fault = (uint8_t)((unit->value_SafetyStatusC & 0x80U) >> 7);

    unit->ProtectionsTriggered = (unit->value_SafetyStatusA ||
                                  unit->value_SafetyStatusB ||
                                  unit->value_SafetyStatusC) ? 1U : 0U;

    unit->Global_Fault_Flags = (uint32_t)unit->value_SafetyStatusA |
                               ((uint32_t)unit->value_SafetyStatusB << 8) |
                               ((uint32_t)unit->value_SafetyStatusC << 16);
}

void BQ769x2_ReadBATTStatus(BMS_Unit *unit)
{
    DirectCommands(unit, BatteryStatus, 0x00, R);
    unit->BattStat = u16_le(unit->RX_data);
}

void BQ769x2_ReadPFStatus(BMS_Unit *unit)
{
    DirectCommands(unit, PFStatusA, 0x00, R1);
    unit->value_PFStatusA = unit->RX_data[0];
    DirectCommands(unit, PFAlertA, 0x00, R1);
    unit->value_PFAlertA = unit->RX_data[0];

    unit->SUV_PF = (uint8_t)(unit->value_PFStatusA & 0x01U);
    unit->SOV_PF = (uint8_t)((unit->value_PFStatusA & 0x02U) >> 1);

    DirectCommands(unit, PFStatusB, 0x00, R1);
    unit->value_PFStatusB = unit->RX_data[0];
    DirectCommands(unit, PFAlertB, 0x00, R1);
    unit->value_PFAlertB = unit->RX_data[0];

    unit->VIMR_PF = (uint8_t)((unit->value_PFStatusB & 0x08U) >> 3);
    unit->VIMA_PF = (uint8_t)((unit->value_PFStatusB & 0x10U) >> 4);

    DirectCommands(unit, PFStatusC, 0x00, R1);
    unit->value_PFStatusC = unit->RX_data[0];
    DirectCommands(unit, PFAlertC, 0x00, R1);
    unit->value_PFAlertC = unit->RX_data[0];

    DirectCommands(unit, PFStatusD, 0x00, R1);
    unit->value_PFStatusD = unit->RX_data[0];
    DirectCommands(unit, PFAlertD, 0x00, R1);
    unit->value_PFAlertD = unit->RX_data[0];

    unit->PF_ProtectionsTriggered = (unit->value_PFStatusA ||
                                     unit->value_PFStatusB ||
                                     unit->value_PFStatusC ||
                                     unit->value_PFStatusD) ? 1U : 0U;

    if (unit->PF_ProtectionsTriggered) {
        unit->Global_Fault_Flags |= (1UL << 31);
    }
}

uint32_t BQ769x2_ReadVoltage(BMS_Unit *unit, uint8_t command)
{
    uint16_t raw;

    DirectCommands(unit, command, 0x00, R);
    raw = u16_le(unit->RX_data);

    if (command >= Cell1Voltage && command <= Cell16Voltage) {
        return raw;
    }

    return 10UL * (uint32_t)raw;
}

void BQ769x2_ReadAllVoltages(BMS_Unit *unit)
{
    uint8_t command = Cell1Voltage;

    for (int i = 0; i < 16; i++) {
        unit->CellVoltage[i] = (uint16_t)BQ769x2_ReadVoltage(unit, command);
        command = (uint8_t)(command + 2U);
    }

    DirectCommands(unit, StackVoltage, 0x00, R);
    unit->Stack_Voltage_Raw = u16_le(unit->RX_data);
    unit->Stack_Voltage = 10UL * (uint32_t)unit->Stack_Voltage_Raw;

    DirectCommands(unit, PACKPinVoltage, 0x00, R);
    unit->Pack_Voltage_Raw = u16_le(unit->RX_data);
    unit->Pack_Voltage = 10UL * (uint32_t)unit->Pack_Voltage_Raw;

    DirectCommands(unit, LDPinVoltage, 0x00, R);
    unit->LD_Voltage_Raw = u16_le(unit->RX_data);
    unit->LD_Voltage = 10UL * (uint32_t)unit->LD_Voltage_Raw;
}

int16_t BQ769x2_ReadCurrent(BMS_Unit *unit)
{
    DirectCommands(unit, CC2Current, 0x00, R);
    return i16_le(unit->RX_data);
}

float BQ769x2_ReadTemperature(BMS_Unit *unit, uint8_t command)
{
    DirectCommands(unit, command, 0x00, R);
    return temp_01k_to_c(u16_le(unit->RX_data));
}

void BQ769x2_ReadData(BMS_Unit *unit)
{
    unit->AlarmBits = BQ769x2_ReadAlarmStatus(unit);

    if (unit->AlarmBits & 0x0080U) {
        BQ769x2_ReadAllVoltages(unit);
        if (is_current_board(unit)) {
            unit->Pack_Current = BQ769x2_ReadCurrent(unit);
        }
        unit->CELL_Temp = BQ769x2_ReadTemperature(unit, TS1Temperature);
        unit->FET_Temp = BQ769x2_ReadTemperature(unit, TS3Temperature);
        DirectCommands(unit, AlarmStatus, 0x0080, W);
    }
}

void BQ769x2_ReadDASTATUS5(BMS_Unit *unit)
{
    Subcommands(unit, DASTATUS5, 0x00, R);

    unit->MaxCellVolatge = u16_le(&unit->RX_SubData[4]);
    unit->MinCellVolatge = u16_le(&unit->RX_SubData[6]);
    unit->Battery_Voltage_Sum = u16_le(&unit->RX_SubData[8]);
    unit->Avg_Cell_Temp = temp_01k_to_c(u16_le(&unit->RX_SubData[18]));
    unit->FET_Temp = temp_01k_to_c(u16_le(&unit->RX_SubData[12]));
    unit->Max_Cell_Temp = temp_01k_to_c(u16_le(&unit->RX_SubData[14]));
    unit->Min_Cell_Temp = temp_01k_to_c(u16_le(&unit->RX_SubData[16]));

    if (is_current_board(unit)) {
        unit->CC3_Current = i16_le(&unit->RX_SubData[20]);
        unit->CC1_Current = i16_le(&unit->RX_SubData[22]);
        unit->CC2_Counts = i32_le(&unit->RX_SubData[24]);
        unit->CC3_Counts = i32_le(&unit->RX_SubData[28]);
    }
}

void BQ769x2_ReadDASTATUS6(BMS_Unit *unit)
{
    Subcommands(unit, DASTATUS6, 0x00, R);

    if (is_current_board(unit)) {
        unit->AccumulatedCharge_Int = i32_le(&unit->RX_SubData[0]);
        unit->AccumulatedCharge_Frac = u32_le(&unit->RX_SubData[4]);
        unit->AccumulatedCharge_Time = u32_le(&unit->RX_SubData[8]);
    }
}

void BMS_SyncSharedHardwareData(void)
{
    sync_current_data(&BMS[TOP], &BMS[BMS_CURRENT_BOARD]);
    sync_current_data(&BMS[BOT], &BMS[BMS_CURRENT_BOARD]);

    sync_fet_data(&BMS[TOP], &BMS[BMS_FET_BOARD]);
    sync_fet_data(&BMS[BOT], &BMS[BMS_FET_BOARD]);
}

void BQ769x2_ReadLargeSubcommand(BMS_Unit *unit, uint16_t command, uint16_t *dest_array)
{
    uint8_t TX_Reg[2] = {
        (uint8_t)(command & 0xffU),
        (uint8_t)((command >> 8) & 0xffU)
    };
    uint8_t RX_Buffer[32] = {0};

    I2C_WriteReg(unit, 0x3E, TX_Reg, 2);
    delayUS(2000);
    I2C_ReadReg(unit, 0x40, RX_Buffer, 32);

    for (int i = 0; i < 16; i++) {
        dest_array[i] = u16_le(&RX_Buffer[i * 2]);
    }
}

void BQ769x2_Read_Extra_Direct(BMS_Unit *unit)
{
    unit->Int_Temp = BQ769x2_ReadTemperature(unit, IntTemperature);
    unit->CFETOFF_Temp = BQ769x2_ReadTemperature(unit, CFETOFFTemperature);
    unit->HDQ_Temp = BQ769x2_ReadTemperature(unit, HDQTemperature);
}

void BQ769x2_Read_Snapshots(BMS_Unit *unit)
{
    BQ769x2_ReadLargeSubcommand(unit, CUV_SNAPSHOT, unit->CUV_Snapshot);
    BQ769x2_ReadLargeSubcommand(unit, COV_SNAPSHOT, unit->COV_Snapshot);
}

void BMS_FanControl_SetDuty(uint8_t duty_percent)
{
    if (duty_percent > BMS_FAN_MAX_DUTY_PERCENT) {
        duty_percent = BMS_FAN_MAX_DUTY_PERCENT;
    }

    uint32_t period = __HAL_TIM_GET_AUTORELOAD(&htim2) + 1U;
    uint32_t pulse = (period * (uint32_t)duty_percent) / 100U;

    if (duty_percent >= BMS_FAN_MAX_DUTY_PERCENT) {
        pulse = period;
    }

    __HAL_TIM_SET_COMPARE(&htim2, BMS_FAN_PWM_CHANNEL, pulse);
    BMS_FanDuty = duty_percent;
}

uint8_t BMS_FanControl_GetDuty(void)
{
    return BMS_FanDuty;
}

void BMS_FanControl_Init(void)
{
    BMS_FanControl_SetDuty(0U);
    HAL_TIM_PWM_Start(&htim2, BMS_FAN_PWM_CHANNEL);
}

void BMS_FanControl_Update(void)
{
    uint8_t any_valid = 0U;
    float max_temp = -1000.0f;

    for (uint32_t i = 0; i < STACK; i++) {
        uint8_t valid = 0U;
        float board_temp = fan_max_valid_temp(&BMS[i], &valid);

        if (valid != 0U) {
            if (any_valid == 0U || board_temp > max_temp) {
                max_temp = board_temp;
            }
            any_valid = 1U;
        }
    }

    BMS_FanControl_SetDuty(fan_duty_from_temp(max_temp, any_valid));
}

uint8_t Check_BMS_Sleep_State(BMS_Unit *unit)
{
    DirectCommands(unit, BatteryStatus, 0x00, R);
    return (u16_le(unit->RX_data) & 0x8000U) ? 1U : 0U;
}
