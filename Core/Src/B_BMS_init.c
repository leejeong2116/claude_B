/*
 * B_BMS_init.c
 *
 *  Created on: Mar 30, 2026
 *      Author: 2027 WSC KUST
 */
#include "B_BMS_init.h"
#include "B_BMS.h"
#include "B_BMS_cmd.h"

// ##################################################################################################################################################
// ############################################################## 보드별 설정 (TOP / BOT) ###########################################################
// ##################################################################################################################################################
//
// 2027WSC 보고서(datasheets/2027WSC_보고서_통합본.docx)의 BQ76972 핀 설정 표에 따르면 TOP/BOT는 서로 다른
// 역할을 하므로 아래 항목들은 반드시 보드별로 달라야 한다. 이 파일은 원래 TOP/BOT 분기가 없어 두 칩에
// 동일한 값을 썼고, 그 결과 아래 문제가 있었다.
//
//   - TOP.CFETOFF가 온도(0x07)로 설정되어, BOT의 DCHG가 High로 올라가도 TOP이 CHG FET을 끄지 않았다.
//     => BOT 측 충전 보호(COV/OCC/OTC/UTC)가 실제 FET까지 전달되지 않음.
//   - TOP.DFETOFF가 BOTHOFF(0x6A)로 설정되어, BOT의 DDSG(방전 보호) 하나에 충전까지 함께 끊겼다.
//   - TOP.DCHG/DDSG가 푸시풀 출력(0x22)으로 설정되어, 서미스터가 붙은 핀을 IC가 구동하고 있었다.
//   - TS1/TS3/HDQ가 전부 셀 온도(0x07)라 FET 온도 채널이 없어 OTF 보호가 트립될 소스가 없었다.
//
// 신호 경로 (보고서 "2. 절연" / 핀 설정 표):
//
//     MCU PA15 ──▶ BOT.DFETOFF(BOTHOFF, Active-H)
//                     ├─ BOT.DCHG(Active-H) ──G3VM──▶ TOP.CFETOFF ──▶ CHG FET OFF
//                     └─ BOT.DDSG(Active-H) ──G3VM──▶ TOP.DFETOFF ──▶ DSG FET OFF
//
// !! 플래시 전 확인 필요 !!
// 아래 핀 설정 바이트는 TRM 핀 설정 표를 직접 확인하거나 BQStudio에서 TOP/BOT를 각각 덤프해
// (.gg.csv) 대조한 뒤 사용할 것. 특히 TOP의 0x2A는 다음 근거로 유도한 값이다:
//   - 기존 BOT.DFETOFF = 0x6A = BOTHOFF 비트(0x40) + 옵션 비트(0x2A)
//   - ALERT 핀도 동일한 옵션 비트 0x2A를 쓰고 있음 (Active-H, weak pull 없음, REG1 기준)
//   - 따라서 BOTHOFF 비트만 뺀 0x2A = "CFETOFF/DFETOFF 본래 기능, Active-H, weak pull 없음"
//   - 참고로 TI 예제(main_STM32F103_I2C.c:421)는 BOTHOFF에 0x42 = 0x40 + 0x02를 쓴다(EVM 고유 옵션 비트).
typedef struct {
	uint8_t cfetoff_pin;   // CFETOFF Pin Config
	uint8_t dfetoff_pin;   // DFETOFF Pin Config
	uint8_t dchg_pin;      // DCHG Pin Config
	uint8_t ddsg_pin;      // DDSG Pin Config
	uint8_t ts3_pin;       // TS3 Config (0x07 = 셀 온도, 0x0F = FET 온도)
	uint8_t chg_pump;      // Chg Pump Control
} BQ_BoardConfig_t;

// TOP = FET 보드. CFETOFF/DFETOFF는 BOT에서 오는 차단 신호를 받는 "입력"이고,
// DCHG/DDSG 핀에는 서미스터가 붙어 있으며, CHG/DSG(43/45번)로 실제 게이트를 구동한다.
//
// chg_pump: 보고서 TOP 47번 CP1은 "Charge Pump"로 되어 있으나, FETOptions(0x2C) 주석은
// "차지펌프 사용 안하기 위해 CHG/DSG 병렬"이라고 되어 있어 서로 모순된다. 하드웨어에서
// 하이사이드 게이트 전압을 실측해 확정하기 전까지는 기존 동작(OFF)을 유지한다.
// 확정되면 아래 값을 0x01(또는 TRM 권장값)로 바꿀 것.
static const BQ_BoardConfig_t BQ_CFG_TOP = {
	.cfetoff_pin = 0x2A,   // CFETOFF 입력, Active-H  (BOT.DCHG ← G3VM ←)
	.dfetoff_pin = 0x2A,   // DFETOFF 입력, Active-H, BOTHOFF 아님 (DSG만 차단)
	.dchg_pin    = 0x07,   // 서미스터 (셀 온도)
	.ddsg_pin    = 0x07,   // 서미스터 (셀 온도)
	.ts3_pin     = 0x0F,   // FET 온도 — OTF 보호의 유일한 소스. TI 예제도 TS3에 0x0F 사용
	.chg_pump    = 0x00,   // TODO: 하이사이드 게이트 전압 실측 후 확정
};

// BOT = 전류 보드. CHG/DSG(43/45번)는 미사용이고, 내부 FET 드라이버 상태가 DCHG/DDSG 핀으로
// 나가서 TOP의 CFETOFF/DFETOFF를 구동한다. DFETOFF는 MCU가 BOTHOFF로 쓴다.
// BOT에는 FET이 없으므로 ts3는 셀 온도 유지 (OTF 소스 없음 = 정상).
static const BQ_BoardConfig_t BQ_CFG_BOT = {
	.cfetoff_pin = 0x07,   // 서미스터 (셀 온도)
	.dfetoff_pin = 0x6A,   // BOTHOFF 입력, Active-H — MCU PA15가 구동
	.dchg_pin    = 0x22,   // 출력, Active-H, CHG FET OFF 시 High → TOP.CFETOFF
	.ddsg_pin    = 0x22,   // 출력, Active-H, DSG FET OFF 시 High → TOP.DFETOFF
	.ts3_pin     = 0x07,   // 서미스터 (셀 온도)
	.chg_pump    = 0x00,   // BOT은 CHG/DSG 핀 미사용 → 차지펌프 불필요
};

// ##################################################################################################################################################
// ################################################# HWD(호스트 워치독) 보호 — 슬립과의 충돌 ########################################################
// ##################################################################################################################################################
//
// HWDF는 MCU가 HWDDelay(현재 60s) 동안 통신하지 않으면 트립되며, CHGFETProtectionsC/DSGFETProtectionsC
// 양쪽에 들어 있어 CHG/DSG를 모두 연다.
//
// 문제: Enter_Sleep_Sequence()의 SLEEP/STOP1은 SysTick을 끄고 다음 인터럽트(주로 BQ ALERT)까지
// 무기한 블로킹된다. 60초 넘게 자면 양쪽 칩이 HWDF 트립 → FET 개방. 게다가 기존 SFAlertMaskC(0xF4)는
// HWDF 비트(bit1)가 0이라 ALERT조차 올라오지 않아 MCU가 깨지도 못했다.
//
// 이 파일에서는 두 가지만 처리한다:
//   1) SFAlertMaskC에 HWDF 비트를 추가해, 트립 시 최소한 ALERT로 MCU를 깨운다 (아래 참조).
//   2) 아래 스위치로 HWD 보호 자체를 끌 수 있게 한다. 기본값은 기존 동작(활성) 유지 —
//      안전 설정을 조용히 약화시키지 않기 위함이며, 켜고 끄는 판단은 아래 근거로 직접 할 것.
//
// 근본 해결책 두 가지 (둘 다 .ioc 변경 필요 → 여기서는 구현하지 않음):
//   (a) RTC Wake-up Timer를 HWDDelay보다 짧은 주기로 돌려 STOP1을 관통해 주기적으로 깨운 뒤
//       BQ를 폴링(=워치독 급이기). B_BMS_power_mode.c:42의 TODO와 동일한 작업이며, 그 TODO가
//       지적하는 no_load_time_ms 부정확 문제도 같이 해결된다. ← 권장
//   (b) HWD를 끄는 대신 MCU IWDG를 켠다. MCU 행(hang) 시 리셋 → BOT_DFETOFF가 Hi-Z가 되고
//       외부 풀업이 High로 끌어 BOTHOFF가 걸린다(페일세이프). 단 디버깅 시 주의.
//
// 참고: "MCU가 죽으면 팩을 열어야 한다"는 HWD의 목적 자체는 이 하드웨어에서 이미 부분적으로
// 보장된다 — MCU 리셋 시 BOT_DFETOFF(오픈드레인)가 Hi-Z → 외부 풀업 → BOTHOFF. 다만 리셋 없는
// 행(hang)은 커버되지 않으므로 (a) 또는 (b)가 필요하다.
// MCU 쪽에서 본 같은 고장(=칩과 통신이 안 됨)은 B_BMS_protect.c의 TRIP_COMM이 담당한다.
#define B_BMS_HWD_PROTECTION_ENABLE 1

#if B_BMS_HWD_PROTECTION_ENABLE
  #define ENABLED_PROTECTIONS_C 0xF6   // [OCD3, SCDL, OCDL, COVL, HWDF, PTO]
#else
  #define ENABLED_PROTECTIONS_C 0xF4   // HWDF(bit1) 제외
#endif

void BQ769x2_Init(BMS_Unit* unit)
{
	const BQ_BoardConfig_t *cfg = (unit == &BMS[TOP]) ? &BQ_CFG_TOP : &BQ_CFG_BOT;

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
	BQ769x2_SetRegister(unit, CFETOFFPinConfig, cfg->cfetoff_pin, 1); // 보드별: TOP=차단 입력, BOT=서미스터 (파일 상단 BQ_CFG_* 참조)
	BQ769x2_SetRegister(unit, DFETOFFPinConfig, cfg->dfetoff_pin, 1); // 보드별: TOP=DSG 차단 입력, BOT=BOTHOFF 입력
	BQ769x2_SetRegister(unit, ALERTPinConfig, 0x2A, 1); // [Active high, ALERT, NO week pull-up/pull-down] (rising-edge EXTI on ALERT assertion, see gpio.c GPIO_MODE_IT_RISING)
	BQ769x2_SetRegister(unit, TS1Config, 0x07, 1); //cell // 지금은 셀 온도(셀, FET, 버스바/인터커넥트 설정가능)/ 차후 조정 / 18k
	BQ769x2_SetRegister(unit, TS2Config, 0x00, 1); // OFF(Wake)
	BQ769x2_SetRegister(unit, TS3Config, cfg->ts3_pin, 1); // 보드별: TOP=FET 온도(OTF 소스), BOT=셀 온도
	BQ769x2_SetRegister(unit, HDQPinConfig, 0x07, 1); //cell // 지금은 셀 온도/ 차후 조정
	BQ769x2_SetRegister(unit, DCHGPinConfig, cfg->dchg_pin, 1); // 보드별: BOT=출력(→TOP.CFETOFF), TOP=서미스터
	BQ769x2_SetRegister(unit, DDSGPinConfig, cfg->ddsg_pin, 1); // 보드별: BOT=출력(→TOP.DFETOFF), TOP=서미스터
	// 0x06 = USER_VOLTS_CV=1(센티볼트/10mV) + USER_AMPS=2(센티암페어/10mA)
	//
	// 이전 값 0x02는 USER_VOLTS_CV=0(밀리볼트/1mV)이었는데, TRM Table 13-15(SLUUCW9 p.159)가 명시적으로
	// 경고한다: Top-of-Stack / PACK / LD 전압은 부호있는 16비트에 담기므로 밀리볼트를 쓰면 32767 mV에서
	// 포화되며, "32 V를 넘지 않는 응용에서만 밀리볼트를 쓸 것"이라고 되어 있다.
	// 본 설계는 칩당 16S(최대 67.2 V)라 32 V를 크게 넘으므로 0x34/0x36/0x38 판독값이 계속 32767에
	// 포화되어 있었다. 센티볼트로 바꾸면 6720(=67.2 V)이라 여유가 충분하다.
	//
	// 전류 단위(비트 1:0)는 건드리지 않았다. 10mA 단위를 가정하는 코드가 여럿 있다
	// (Is_No_Load()의 1000=10A, B_BMS_soc.c의 userAh->mAh 환산, OCC/OCD/SCD 임계 주석 등).
	//
	// 주의: 이 비트를 바꾸면 B_BMS.c의 stack_userV_to_mV()도 반드시 같이 바꿔야 한다.
	// 반대로 Min Blow Fuse Voltage / Shutdown Stack Voltage / Sleep Charger Voltage Threshold /
	// PACK-TOS Delta 계열은 userV가 아니라 고정 10mV 단위(TRM 13.3.1.x, 13.4.2.x)라 영향받지 않는다.
	BQ769x2_SetRegister(unit, DAConfiguration, 0x06, 1);// 10mV, 10mA 단위
	BQ769x2_SetRegister(unit, VCellMode, 0x0000, 2); //enable 16 cells (VC0 기준, NC 사용 안 함)
	BQ769x2_SetRegister(unit, CC3Samples, 0x1E, 1); // [Average fillter samples 30EA ]

// ##################################################################################################################################################
// ################################################################### PROTECTION ###################################################################
// ##################################################################################################################################################
	BQ769x2_SetRegister(unit, ProtectionConfiguration, 0x0602, 2); //SCDL/ OCDL 전류 기반 복귀, FET 차단 시 Fuse 차단 안함, Pack전압 기준 Fuse, PF 기록 안함, PF 발생 시 퓨즈 차단 안함, PF 발생해도 Deepsleep 안 들어감, REG유지, PF 발생 시 FET 차단
	BQ769x2_SetRegister(unit, EnabledProtectionsA, 0xFC, 1); // [SCD, OCD2, OCD1, OCC, COV, CUV]
	BQ769x2_SetRegister(unit, EnabledProtectionsB, 0xF7, 1); // [OTF, OTINT, OTD, OTC, UTINT, UTD, UTC]
	BQ769x2_SetRegister(unit, EnabledProtectionsC, ENABLED_PROTECTIONS_C, 1); // [OCD3, SDCL, OCDL, COVL, HWDF, PTO] — HWDF는 파일 상단 B_BMS_HWD_PROTECTION_ENABLE로 제어
	// 아래 6개 마스크는 B_BMS_protect.c가 "지금 CHG/DSG는 꺼져 있어야 하는가"를 계산할 때 그대로 재사용한다.
	// 값을 바꾸면 B_BMS_init.h의 CHGFET_MASK_*/DSGFET_MASK_*도 반드시 같이 바꿀 것.
	BQ769x2_SetRegister(unit, CHGFETProtectionsA, CHGFET_MASK_A, 1); // [SCD, OCC, COV]
	BQ769x2_SetRegister(unit, CHGFETProtectionsB, CHGFET_MASK_B, 1); // [OTF, OTINT, OTC, UTINT, UTC]
	BQ769x2_SetRegister(unit, CHGFETProtectionsC, CHGFET_MASK_C, 1); // [SCDL, COVL, HWDF, PTO]
	BQ769x2_SetRegister(unit, DSGFETProtectionsA, DSGFET_MASK_A, 1); // [SCD, OCD2, OCD1, CUV]
	BQ769x2_SetRegister(unit, DSGFETProtectionsB, DSGFET_MASK_B, 1); // [OTF, OTINT, OTD, UTINT, UTD]
	BQ769x2_SetRegister(unit, DSGFETProtectionsC, DSGFET_MASK_C, 1); // [OCD3, SCDL, OCDL, HWDF]
	BQ769x2_SetRegister(unit, BodyDiodeThreshold, 0x03E8, 2); // 1A // Vsd (Reversediode): max 1.0V, P = 3.8W (W당 40도씩 올라감), 그런데 이 P는 그냥 기판에 올라간 상태라서 차의 온도도 고려해야할 것 같음((152 - max온도 ) / 40 ) = 바디 다이오드 전류
	BQ769x2_SetRegister(unit, DefaultAlarmMask, 0xF882, 2); // 'Default Alarm Mask' - 0x..82 Enables the FullScan and ADScan bits, default value = 0xF800
	BQ769x2_SetRegister(unit, SFAlertMaskA, 0xFC, 1); // [SCD, OCD2, OCD1, OCC, COV, CUV]
	BQ769x2_SetRegister(unit, SFAlertMaskB, 0xF7, 1); // [OTF, OTINT, OTD, OTC, UTINT, UTD, UTC]
	// HWDF(bit1) 추가: HWDF가 트립되면 CHG/DSG가 모두 열리는데 기존 마스크(0xF4)로는 ALERT가 올라오지
	// 않아 저전력 모드의 MCU가 깨지도 못했다. 0xF6 = 0xF4 | HWDF. 파일 상단 HWD 주석 참조.
	BQ769x2_SetRegister(unit, SFAlertMaskC, 0xF6, 1); // [OCD3, SCDL, OCDL, COVL, HWDF, PTO]
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
	BQ769x2_SetRegister(unit, ChgPumpControl, cfg->chg_pump, 1); // 보드별 (파일 상단 BQ_CFG_TOP.chg_pump의 TODO 참조)
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
	BQ769x2_SetRegister(unit, OCCRecoveryThreshold, 0xFC18, 2); // [-1A] = [65536-1000mA]
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
	BQ769x2_SetRegister(unit, UTDThreshold, 0xEC, 1); // [-20`C]
	BQ769x2_SetRegister(unit, UTDDelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, UTDRecovery, 0xF1, 1); // [-15`C]
	BQ769x2_SetRegister(unit, UTINTThreshold, 0xE2, 1); // [-30`C]
	BQ769x2_SetRegister(unit, UTINTDelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, UTINTRecovery, 0xE7, 1); // [-25`C]
	BQ769x2_SetRegister(unit, ProtectionsRecoveryTime, 0x03, 1); // [3s]
	BQ769x2_SetRegister(unit, HWDDelay, 0x003C, 2); // [60s = 1min]
	BQ769x2_SetRegister(unit, LoadDetectActiveTime, 0x06, 1); // [6s]
	BQ769x2_SetRegister(unit, LoadDetectRetryDelay, 0x32, 1); // [50s]
	BQ769x2_SetRegister(unit, LoadDetectTimeout, 0x0018, 2); // [24hrs] (U2 register, TRM Table 13.6.21.3)
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
	BQ769x2_SetRegister(unit, DFETFOFFThreshold, 0xFE0C, 2); // [-0.5A] = [65536-500mA]
	BQ769x2_SetRegister(unit, DFETFOFFDelay, 0x05, 1); // [5S]
	BQ769x2_SetRegister(unit, VSSFFailThreshold, 0x0064, 2); //[100]
	BQ769x2_SetRegister(unit, VSSFDelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, PF2LVLDelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, LFOFDelay, 0x05, 1); // [5s]
	BQ769x2_SetRegister(unit, HWMXDelay, 0x05, 1); // [5s]

// ##################################################################################################################################################
// ################################################################### SECTION 9 ####################################################################
// ##################################################################################################################################################
	// TOP은 SRP/SRN 미사용(보고서 핀 표)이라 아래 두 값은 TOP에서 의미가 없지만, 쓰더라도 해롭지 않고
	// 코드가 이미 is_current_board()로 TOP의 전류 필드를 사용하지 않으므로 분기하지 않는다.
	BQ769x2_SetRegister(unit, CCGain, 0x41975E35, 4); //[ 전류 센서 - 7.5684/ 0.4mOhm = 18.921]
	BQ769x2_SetRegister(unit, CapacityGain, 0x4AAC3920, 4); //[ CCGain(18.921) * 298261.6178 = 5,643,408 ->0x4AAC3920 (이전 0x50282FC9는 약 113억으로 max 4.19억 초과)]

	CommandSubcommands(unit, EXIT_CFGUPDATE); // Exit CONFIGUPDATE mode  - Subcommand 0x0092
}

