/*
 * B_BMS_init.c
 *
 *  Created on: Mar 30, 2026
 *      Author: 2027 WSC KUST
 */
#include "B_BMS_init.h"
#include "B_BMS.h"
#include "B_BMS_cmd.h"

void BQ769x2_Init(BMS_Unit* unit)
{
	if(unit == &BMS[TOP]){

	}
	else{
	CommandSubcommands(unit, SET_CFGUPDATE);

// ##################################################################################################################################################
// ###################################################################### FUSE ######################################################################
// ##################################################################################################################################################
	BQ769x2_SetRegister(unit, MinBlowFuseVoltage, 0x0CE4, 2); // [33V] (3300 * 10mV)
	BQ769x2_SetRegister(unit, FuseBlowTimeout, 0x05, 1); // [5s] MCU에 신호전달

// ##################################################################################################################################################
// ##################################################################### CONFIG #####################################################################
// ##################################################################################################################################################
	BQ769x2_SetRegister(unit, PowerConfig, 0x2D80, 2);  //deep sleep 중에도 칩 내부 온도 감시 활성/ deep sleep 중 LD 핀에 충전기 전압 감지되면 wake / deep sleep 진입 시 REG1/2(LDO) 전원을 켜둠 (BOT BMS는 MCU전원) --- loop normal adc (3ms 사용) - 21슬롯  FAST모드 시 정확성 하락 / normal 모드 중 가장 빠른 것으로/ 2880 과 다른점 CC2 EN -> 빠른 전류 데이터 , AUTO_PSS_EN 자동절전 -> 칩이 알아서 전력소력 최적화
	BQ769x2_SetRegister(unit, REG12Config, 0xCD, 1); //REG1 3.3V 출력
	BQ769x2_SetRegister(unit, REG0Config, 0x01, 1); //BREG 제어 ON (REGIN 입력 활성화), REG2는 사용 안 함, REG18은 내부 참조 1.8V (하드웨어 연결)
	BQ769x2_SetRegister(unit, HWDRegulatorOptions, 0x00, 1); // - [When Watchdog No Action] // 워치독 만료(통신 신호 끊김)되어도 LDO유지

	BQ769x2_SetRegister(unit, CommType, 0x00, 1); // I2C Basic // &hi2c2, &hi2c4 (TOP, BOT) 핸들러 정해주기, 동일한 통신 속도 설정
	BQ769x2_SetRegister(unit, I2CAddress, 0x00, 1); // I2C Default
	BQ769x2_SetRegister(unit, SPIConfiguration, 0x00, 1); // [not use]
	BQ769x2_SetRegister(unit, CommIdleTime, 0x00, 1); // NO clock stretch // 클락 스트레치: 통신에서 슬레이브가 처리 시간이 더 필요할 때 클럭을 잡아 마스터를 기다리게 하는 기능
	BQ769x2_SetRegister(unit, CFETOFFPinConfig, 0x07, 1); //cell // 지금은 셀 온도/ 차후 조정
	BQ769x2_SetRegister(unit, DFETOFFPinConfig, 0x6A, 1); //[Active high, BOTHOFF, NO week pull-up/pull-down, REG1]
	BQ769x2_SetRegister(unit, ALERTPinConfig, 0xAA, 1); // [Active low, ALERT, NO week pull-up/pull-down]
	BQ769x2_SetRegister(unit, TS1Config, 0x07, 1); //cell // 지금은 셀 온도(셀, FET, 버스바/인터커넥트 설정가능)/ 차후 조정 / 18k
	BQ769x2_SetRegister(unit, TS2Config, 0x00, 1); // OFF(Wake)
	BQ769x2_SetRegister(unit, TS3Config, 0x07, 1); //cell // 지금은 셀 온도/ 차후 조정
	BQ769x2_SetRegister(unit, HDQPinConfig, 0x07, 1); //cell // 지금은 셀 온도/ 차후 조정
	BQ769x2_SetRegister(unit, DCHGPinConfig, 0x22, 1); //DCHG: Active High, CHG FET OFF 시 High (TOPBMS 연결) , REG1, NO week pull-up/pull-down
	BQ769x2_SetRegister(unit, DDSGPinConfig, 0x22, 1); //DDSG: Active High, DSG FET OFF 시 High (TOPBMS 연결), REG1, NO week pull-up/pull-down
	BQ769x2_SetRegister(unit, DAConfiguration, 0x02, 1);//1mV, 10mA 단위 (전류 오버플로우 방지)
	BQ769x2_SetRegister(unit, VCellMode, 0x0000, 2); //enable 16 cells (VC0 기준, NC 사용 안 함)
	BQ769x2_SetRegister(unit, CC3Samples, 0x1E, 1); // [Average fillter samples 30EA ]

// ##################################################################################################################################################
// ################################################################### PROTECTION ###################################################################
// ##################################################################################################################################################
	BQ769x2_SetRegister(unit, ProtectionConfiguration, 0x0602, 2); //SCDL/ OCDL 전류 기반 복귀, FET 차단 시 Fuse 차단 안함, Pack전압 기준 Fuse, PF 기록 안함, PF 발생 시 퓨즈 차단 안함, PF 발생해도 Deepsleep 안 들어감, REG유지, PF 발생 시 FET 차단
	BQ769x2_SetRegister(unit, EnabledProtectionsA, 0xFC, 1); // [SCD, OCD2, OCD1, OCC, COV, CUV]
	BQ769x2_SetRegister(unit, EnabledProtectionsB, 0xF7, 1); // [OTF, OTINT, OTD, OTC, UTINT, UTD, UTC]
	BQ769x2_SetRegister(unit, EnabledProtectionsC, 0xF6, 1); // [OCD3, SDCL, OCDL, COVL, HWDF,  PTO ]
	BQ769x2_SetRegister(unit, CHGFETProtectionsA, 0x98, 1); // [SCD, OCC, COV]
	BQ769x2_SetRegister(unit, CHGFETProtectionsB, 0xD5, 1); // [OTF, OTINT, OTC, UTINT, UTC]
	BQ769x2_SetRegister(unit, CHGFETProtectionsC, 0x56, 1); // [SCDL, COVL, HWDF, PTO]
	BQ769x2_SetRegister(unit, DSGFETProtectionsA, 0xE4, 1); // [SCD, OCD2, OCD1, CUV]
	BQ769x2_SetRegister(unit, DSGFETProtectionsB, 0xE6, 1); // [OTF, OTINT, OTD, UTINT, UTD]
	BQ769x2_SetRegister(unit, DSGFETProtectionsC, 0xE2, 1); // [OCD3,SCDL, OCDL, HWDF]
	BQ769x2_SetRegister(unit, BodyDiodeThreshold, 0x03E8, 2); // 1A // Vsd (Reversediode): max 1.0V, P = 3.8W (W당 40도씩 올라감), 그런데 이 P는 그냥 기판에 올라간 상태라서 차의 온도도 고려해야할 것 같음((152 - max온도 ) / 40 ) = 바디 다이오드 전류
	BQ769x2_SetRegister(unit, DefaultAlarmMask, 0xF882, 2); // 'Default Alarm Mask' - 0x..82 Enables the FullScan and ADScan bits, default value = 0xF800
	BQ769x2_SetRegister(unit, SFAlertMaskA, 0xFC, 1); // [SCD, OCD2, OCD1, OCC, COV, CUV]
	BQ769x2_SetRegister(unit, SFAlertMaskB, 0xF7, 1); // [OTF, OTINT, OTD, OTC, UTINT, UTD, UTC]
	BQ769x2_SetRegister(unit, SFAlertMaskC, 0xF4, 1); // [OCD3, SCDL, OCDL, COVL,PTO]
	BQ769x2_SetRegister(unit, PFAlertMaskA, 0xDF, 1); // [CUDEP, SOTF, SOT, SOCD, SOCC, SOV, SUV]
	BQ769x2_SetRegister(unit, PFAlertMaskB, 0x9F, 1); // [SCDL, VIMA, VIMR, 2LVL, DFETF, CFETF]
	BQ769x2_SetRegister(unit, PFAlertMaskC, 0x78, 1); // [HWMX, VSSF, VREF, LFOF]
	BQ769x2_SetRegister(unit, PFAlertMaskD, 0x01, 1); // [TOSF]

// ##################################################################################################################################################
// ################################################################ PERMANENT FAILURE ###############################################################
// ##################################################################################################################################################
	BQ769x2_SetRegister(unit, EnabledPFA, 0xDF, 1); // [CUDEP, SOTF, SOT, SOCD, SOCC, SOV, SUV]
	BQ769x2_SetRegister(unit, EnabledPFB, 0x9F, 1); // [SCDL, VIMA, VIMR, 2LVL, DFETF, CFETF]
	BQ769x2_SetRegister(unit, EnabledPFC, 0xFF, 1); // [CMDF, HWMX, VSSF, VREF, LFOF, IRMF, DRMF, OTPF]
	BQ769x2_SetRegister(unit, EnabledPFD, 0x01, 1); // [TOSF]

// ##################################################################################################################################################
// ###################################################################### FET #######################################################################
// ##################################################################################################################################################
	BQ769x2_SetRegister(unit, FETOptions, 0x2C, 1);//부팅 시 FET 수동 제어, PDSG 사용 X, 디바이스 FET제어, MCU FET제어, Sleep 시 GHG FET OFF, CHG/DSG FET 병렬(차지펌프 사용 안하기 위해) -- 원래 0x1D
	BQ769x2_SetRegister(unit, ChgPumpControl, 0x00, 1); // (BOT용) 차지펌프 사용 X
	BQ769x2_SetRegister(unit, PrechargeStartVoltage, 0x0000, 2); // 사용 X
	BQ769x2_SetRegister(unit, PrechargeStopVoltage, 0x0000, 2); // 사용 X
	BQ769x2_SetRegister(unit, PredischargeTimeout, 0x00, 1);
	BQ769x2_SetRegister(unit, PredischargeStopDelta, 0x00, 1);

// ##################################################################################################################################################
// ################################################################ CURRENT THRESHOLDS ##############################################################
// ##################################################################################################################################################
	BQ769x2_SetRegister(unit, DsgCurrentThreshold, 0x0064, 2); // [1A] // 1p -> 5p기준으로 변경
	BQ769x2_SetRegister(unit, ChgCurrentThreshold, 0x007D, 2);// [1.25A] // 1p -> 5p기준으로 변경

// ##################################################################################################################################################
// ################################################################# CELL OPEN-WIRE #################################################################
// ##################################################################################################################################################
	BQ769x2_SetRegister(unit, CheckTime, 0x05, 1); // [5s] // default

// ##################################################################################################################################################
// ############################################################ INTERCONNECT RESISTANCES ############################################################
// ##################################################################################################################################################
	BQ769x2_SetRegister(unit, Cell1Interconnect, 0x0000, 2); // [ALL NOT USE]
	BQ769x2_SetRegister(unit, Cell2Interconnect, 0x0000, 2); // [ALL NOT USE]
	BQ769x2_SetRegister(unit, Cell3Interconnect, 0x0000, 2); // [ALL NOT USE]
	BQ769x2_SetRegister(unit, Cell4Interconnect, 0x0000, 2); // [ALL NOT USE]
	BQ769x2_SetRegister(unit, Cell5Interconnect, 0x0000, 2); // [ALL NOT USE]
	BQ769x2_SetRegister(unit, Cell6Interconnect, 0x0000, 2); // [ALL NOT USE]
	BQ769x2_SetRegister(unit, Cell7Interconnect, 0x0000, 2); // [ALL NOT USE]
	BQ769x2_SetRegister(unit, Cell8Interconnect, 0x0000, 2); // [ALL NOT USE]
	BQ769x2_SetRegister(unit, Cell9Interconnect, 0x0000, 2); // [ALL NOT USE]
	BQ769x2_SetRegister(unit, Cell10Interconnect, 0x0000, 2); // [ALL NOT USE]
	BQ769x2_SetRegister(unit, Cell11Interconnect, 0x0000, 2); // [ALL NOT USE]
	BQ769x2_SetRegister(unit, Cell12Interconnect, 0x0000, 2); // [ALL NOT USE]
	BQ769x2_SetRegister(unit, Cell13Interconnect, 0x0000, 2); // [ALL NOT USE]
	BQ769x2_SetRegister(unit, Cell14Interconnect, 0x0000, 2); // [ALL NOT USE]
	BQ769x2_SetRegister(unit, Cell15Interconnect, 0x0000, 2); // [ALL NOT USE]
	BQ769x2_SetRegister(unit, Cell16Interconnect, 0x0000, 2); // [ALL NOT USE]

// ##################################################################################################################################################
// ################################################################# MANUFACTURING ##################################################################
// ##################################################################################################################################################
	BQ769x2_SetRegister(unit, MfgStatusInit, 0x0050, 2); // [PF_EN, FET_EN]  동작 중 OTP설정 금지, PF 비트 쓰여짐, FET 자동으로 제어됨

// ##################################################################################################################################################
// ############################################################## CELL BALANCING CONFIG #############################################################
// ##################################################################################################################################################
	BQ769x2_SetRegister(unit, BalancingConfiguration, 0x07, 1);  // MCU가 CB 명령 내릴 수 있음, CB 중 SLEEP MODE 허용, SLEEP 중에서도 CB 수행가능, Relax/CHG 중에도 CB 동작
	BQ769x2_SetRegister(unit, MinCellTemp, 0xEC, 1); // [-20`C]
	BQ769x2_SetRegister(unit, MaxCellTemp, 0x3C, 1); // [60`C]
	BQ769x2_SetRegister(unit, MaxInternalTemp, 0x46, 1); // [70`C]
	BQ769x2_SetRegister(unit, CellBalanceInterval, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, CellBalanceMaxCells, 0x05, 1); // [5EA] // 보드 뜨거워지면 개수 낮추기
	BQ769x2_SetRegister(unit, CellBalanceMinCellVCharge, 0x0D48, 2); // [3.4V]
	BQ769x2_SetRegister(unit, CellBalanceMinDeltaCharge, 0x14, 1); // [20mV]
	BQ769x2_SetRegister(unit, CellBalanceStopDeltaCharge, 0x0A, 1); // [10mV]
	BQ769x2_SetRegister(unit, CellBalanceMinCellVRelax, 0x0D16, 2); // [3.35V]
	BQ769x2_SetRegister(unit, CellBalanceMinDeltaRelax, 0x0A, 1); // [10mV]
	BQ769x2_SetRegister(unit, CellBalanceStopDeltaRelax, 0x05, 1); // [5mV]

// ##################################################################################################################################################
// ##################################################################### SHUTDOWN ###################################################################
// ##################################################################################################################################################
	BQ769x2_SetRegister(unit, ShutdownCellVoltage, 0x07D0, 2); // [2V]
	BQ769x2_SetRegister(unit, ShutdownStackVoltage, 0x0D48, 2); // [34V] // 8s 기준 -> 16s 기준으로 변경
	BQ769x2_SetRegister(unit, LowVShutdownDelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, ShutdownTemperature, 0x6E, 1); // [110`C]
	BQ769x2_SetRegister(unit, ShutdownTemperatureDelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, FETOffDelay, 0x00, 1); // [NOT USE]
	BQ769x2_SetRegister(unit, ShutdownCommandDelay, 0x00, 1); // [NOT USE]
	BQ769x2_SetRegister(unit, AutoShutdownTime, 0x00, 1); // [NOT USE]
	BQ769x2_SetRegister(unit, RAMFailShutdownTime, 0x05, 1); // [5s]

// ##################################################################################################################################################
// ###################################################################### SLEEP #####################################################################
// ##################################################################################################################################################
	BQ769x2_SetRegister(unit, SleepCurrent, 0x03E8, 2); // [1A] // 모터 컨트롤러의 대기 전류보다 약간 높게? 모터 가동하면 빠져나오도록
	BQ769x2_SetRegister(unit, VoltageTime, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, WakeComparatorCurrent, 0x01F4, 2); // [500mA] // 0.4m옴 * 500mA = 0.2mV
	BQ769x2_SetRegister(unit, SleepHysteresisTime, 0x0A, 1); // [10s]
	BQ769x2_SetRegister(unit, SleepChargerVoltageThreshold, 0x1A40, 2); // [67.2V] // 배터리가 만충되면(더 이상 충전 할 필요 없을 때) Sleep 모드 진입 가능
	BQ769x2_SetRegister(unit, SleepChargerPACKTOSDelta, 0x00C8, 2); // [2V] // PACK 핀 전압이 배터리(TOS) 전압보다 2V 이상 높을 때 충전기로 확실히 인식

// ##################################################################################################################################################
// ################################################################### PROTECTIONS ##################################################################
// ##################################################################################################################################################
	BQ769x2_SetRegister(unit, CUVThreshold, 50, 1);// [2.53V] // (50 * 50.6mV) / 2.5V 방전 전압
	BQ769x2_SetRegister(unit, CUVDelay, 0x012F, 2);// [1s]
	BQ769x2_SetRegister(unit, CUVRecoveryHysteresis, 0x04, 1); //[202.4mV] // 실험 후 노이즈 영향 확인 후 조정
	BQ769x2_SetRegister(unit, COVThreshold, 83, 1); // [4.1998V]
	BQ769x2_SetRegister(unit, COVDelay, 0x012F, 2);// [1s]
	BQ769x2_SetRegister(unit, COVRecoveryHysteresis, 0x02, 1); // [101.2mV] // 실험 후 노이즈 영향 확인 후 조정
	BQ769x2_SetRegister(unit, COVLLatchLimit, 0x05, 1); // [5CNT] 실험 후 사용 안함
	BQ769x2_SetRegister(unit, COVLCounterDecDelay, 0x0A, 1); // [10s] 실험 후 사용 안함
	BQ769x2_SetRegister(unit, COVLRecoveryTime, 0x14, 1); // [20s] 실험 후 사용 안함
	BQ769x2_SetRegister(unit, OCCThreshold, 0x0A, 1); // [20mV] // max charge current 10A -> 5p -> 50A ,shunt register 0.4m -> 50*0.4m = 20mV
	BQ769x2_SetRegister(unit, OCCDelay, 0x7F, 1); // [419.1+6.6 = 425.7ms]
	BQ769x2_SetRegister(unit, OCCRecoveryThreshold, 0xFC17, 2); // [-1A]
	BQ769x2_SetRegister(unit, OCCPACKTOSDelta, 0x00C8, 2); // [2V = 200 × 10mV]
	BQ769x2_SetRegister(unit, OCD1Threshold, 0x0D, 1); // 65 * 0.4m [26mV]
	BQ769x2_SetRegister(unit, OCD1Delay, 0x7F, 1); // [419.1+6.6 = 425.7ms]
	BQ769x2_SetRegister(unit, OCD2Threshold, 0x19, 1); // 125 * 0.4m [50mV]
	BQ769x2_SetRegister(unit, OCD2Delay, 0x01, 1); // [6.6 + 3.3(1) = 9.9ms]
	BQ769x2_SetRegister(unit, SCDThreshold, 0x04, 1); // 225 * 0.4m = 90mV -> 0x04 = [80mV] -> 200A
	BQ769x2_SetRegister(unit, SCDDelay, 0x1F, 1); // 0x1F [450 us] Enabled with a delay of (value - 1) * 15 µs; min value of 1
	BQ769x2_SetRegister(unit, SCDRecoveryTime, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, OCD3Threshold, 0xE69C, 2); // [-65A] = [65536-6500*10mA]
	BQ769x2_SetRegister(unit, OCD3Delay, 0x01, 1); // [1s]
	BQ769x2_SetRegister(unit, OCDRecoveryThreshold, 0xFE0C, 2); // [-500mA] = [65536 - 500] // 방전 범추고 전류가 -0.5A보다 커지면
	BQ769x2_SetRegister(unit, OCDLLatchLimit, 0x02, 1); // [2CNT]
	BQ769x2_SetRegister(unit, OCDLCounterDecDelay, 0x0A, 1); // [10s]
	BQ769x2_SetRegister(unit, OCDLRecoveryTime, 0x14, 1); // [20s]
	BQ769x2_SetRegister(unit, OCDLRecoveryThreshold, 0xFE0C, 2); // [-500mA]
	BQ769x2_SetRegister(unit, SCDLLatchLimit, 0x02, 1); // [2CNT]
	BQ769x2_SetRegister(unit, SCDLCounterDecDelay, 0x0A, 1); // [10s]
	BQ769x2_SetRegister(unit, SCDLRecoveryTime, 0x14, 1); // [20s]
	BQ769x2_SetRegister(unit, SCDLRecoveryThreshold, 0xFE0C, 2); // [-500mA]
	BQ769x2_SetRegister(unit, OTCThreshold, 0x3C, 1); // [60`C]
	BQ769x2_SetRegister(unit, OTCDelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, OTCRecovery, 0x37, 1); // [55`C] // 셀 권장 재충전 온도 45
	BQ769x2_SetRegister(unit, OTDThreshold, 0x41, 1); // [65`C]
	BQ769x2_SetRegister(unit, OTDDelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, OTDRecovery, 0x3C, 1); // [60`C]
	BQ769x2_SetRegister(unit, OTFThreshold, 0x64, 1); // [100`C]
	BQ769x2_SetRegister(unit, OTFDelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, OTFRecovery, 0x5A, 1); // [90`C] // recovery 95면 금방 복구되고 다시 반복할 것 같아서 좀 더 내림
	BQ769x2_SetRegister(unit, OTINTThreshold, 0x55, 1); // [85`C]
	BQ769x2_SetRegister(unit, OTINTDelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, OTINTRecovery, 0x50, 1); // [80`C]
	BQ769x2_SetRegister(unit, UTCThreshold, 0x00, 1); // [0`C]
	BQ769x2_SetRegister(unit, UTCDelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, UTCRecovery, 0x05, 1); // [5`C]
	BQ769x2_SetRegister(unit, UTDThreshold, 0xEB, 1); // [-20`C]
	BQ769x2_SetRegister(unit, UTDDelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, UTDRecovery, 0xF0, 1); // [-15`C]
	BQ769x2_SetRegister(unit, UTINTThreshold, 0xE1, 1); // [-30`C]
	BQ769x2_SetRegister(unit, UTINTDelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, UTINTRecovery, 0xE6, 1); // [-25`C]
	BQ769x2_SetRegister(unit, ProtectionsRecoveryTime, 0x03, 1); // [3s]
	BQ769x2_SetRegister(unit, HWDDelay, 0x003C, 2); // [60s = 1min]
	BQ769x2_SetRegister(unit, LoadDetectActiveTime, 0x06, 1); // [6s]
	BQ769x2_SetRegister(unit, LoadDetectRetryDelay, 0x32, 1); // [50s]
	BQ769x2_SetRegister(unit, LoadDetectTimeout, 0x18, 1); // [24hrs]
	BQ769x2_SetRegister(unit, PTOChargeThreshold, 0, 2);
	BQ769x2_SetRegister(unit, PTODelay, 0, 2);
	BQ769x2_SetRegister(unit, PTOReset, 0, 2); // 프리차지 없어서 비활성화

// ##################################################################################################################################################
// ################################################################ PERMANENT FAIL ##################################################################
// ##################################################################################################################################################
	BQ769x2_SetRegister(unit, CUDEPThreshold, 0x05DC, 2); // [1.5V]
	BQ769x2_SetRegister(unit, CUDEPDelay, 0x02, 1); // [2s]
	BQ769x2_SetRegister(unit, SUVThreshold, 0x0898, 2); // [2.2V]
	BQ769x2_SetRegister(unit, SUVDelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, SOVThreshold, 0x10CC, 2); // [4.3V]
	BQ769x2_SetRegister(unit, SOVDelay, 0x03, 1); // [3s]
	BQ769x2_SetRegister(unit, TOSSThreshold, 0x0096, 2); // [150mV] (16S 기준 허용 오차 2.4V)
	BQ769x2_SetRegister(unit, TOSSDelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, SOCCThreshold, 0x1B58, 2); // [70A] // OCC보다 높게
	BQ769x2_SetRegister(unit, SOCCDelay, 0x05, 1); //[5s]
	BQ769x2_SetRegister(unit, SOCDThreshold, 0xC568, 2); // [150A] // OCD 보다 높게 SCD 보다 낮게
	BQ769x2_SetRegister(unit, SOCDDelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, SOTThreshold, 0x46, 1); // [70`C]
	BQ769x2_SetRegister(unit, SOTDelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, SOTFThreshold, 0x69, 1); // [105`C]
	BQ769x2_SetRegister(unit, SOTFDelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, VIMRCheckVoltage, 0x0C80, 2); // [3.2V]
	BQ769x2_SetRegister(unit, VIMRMaxRelaxCurrent, 0x0032, 2); // [50mA]
	BQ769x2_SetRegister(unit, VIMRThreshold, 0x01F4, 2); // [0.5V]
	BQ769x2_SetRegister(unit, VIMRDelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, VIMRRelaxMinDuration, 0x012C, 2); // [300s]
	BQ769x2_SetRegister(unit, VIMACheckVoltage, 0x0D48, 2); // [3.4V]
	BQ769x2_SetRegister(unit, VIMAMinActiveCurrent, 0x01F4, 2); // [500mA]
	BQ769x2_SetRegister(unit, VIMAThreshold, 0x01F4, 2); // [0.5V]
	BQ769x2_SetRegister(unit, VIMADelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, CFETFOFFThreshold, 0x0064, 2); // [0.1A]
	BQ769x2_SetRegister(unit, CFETFOFFDelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, DFETFOFFThreshold, 0xFE0B, 2); // [-0.5A]
	BQ769x2_SetRegister(unit, DFETFOFFDelay, 0x05, 1); // [5S]
	BQ769x2_SetRegister(unit, VSSFFailThreshold, 0x0064, 2); //[100]
	BQ769x2_SetRegister(unit, VSSFDelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, PF2LVLDelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, LFOFDelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, HWMXDelay, 0x05, 1); // [5s]

// ##################################################################################################################################################
// ################################################################### SECTION 9 ####################################################################
// ##################################################################################################################################################
	BQ769x2_SetRegister(unit, CCGain, 0x41975E35, 4); //[ 전류 센서 - 7.5684/ 0.4mOhm = 18.921]
	BQ769x2_SetRegister(unit, CapacityGain, 0x4AAC3920, 4); //[ CCGain(18.921) * 298261.6178 = 5,643,408 ->0x4AAC3920 (이전 0x50282FC9는 약 113억으로 max 4.19억 초과)]

	CommandSubcommands(unit, EXIT_CFGUPDATE); // Exit CONFIGUPDATE mode  - Subcommand 0x0092
	}
}

