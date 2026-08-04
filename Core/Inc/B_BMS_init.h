/*
 * B_BMS_init.h
 *
 *  Created on: Mar 30, 2026
 *      Author: user
 */

#ifndef INC_B_BMS_INIT_H_
#define INC_B_BMS_INIT_H_

//#include "B_BMS.h"
typedef struct _BMS_Unit BMS_Unit;

/* ---------------------------------------------------------------------------
 * CHG/DSG FET Protections 마스크
 *
 * BQ769x2가 "이 보호가 뜨면 어느 FET을 끌지"를 결정하는 마스크다. B_BMS_init.c가 칩에 쓰는 값이자,
 * B_BMS_protect.c가 SafetyStatus A/B/C에 AND를 걸어 "지금 CHG/DSG는 꺼져 있어야 하는가"를
 * 계산하는 데 쓰는 값이다. 두 곳이 반드시 같은 값을 봐야 하므로 여기 한 곳에만 정의한다.
 *
 * >>> 이 값을 바꾸면 실제 보호 동작이 바뀐다. 반드시 BQStudio 덤프로 대조할 것. <<<
 *
 * 비트 배치 (B_BMS.c의 BQ769x2_ReadSafetyStatus() 파싱과 동일):
 *   SafetyStatus A: b7 SCD  b6 OCD2  b5 OCD1  b4 OCC   b3 COV    b2 CUV
 *   SafetyStatus B: b7 OTF  b6 OTINT b5 OTD   b4 OTC   b2 UTINT  b1 UTD   b0 UTC
 *   SafetyStatus C: b7 OCD3 b6 SCDL  b5 OCDL  b4 COVL  b2 PTO    b1 HWDF
 *
 * 정리하면:
 *   CHG만 OFF : COV, OCC, OTC, UTC, COVL, PTO
 *   DSG만 OFF : CUV, OCD1, OCD2, OCD3, OTD, UTD, OCDL
 *   둘 다 OFF : SCD, SCDL, OTF, OTINT, UTINT, HWDF
 *
 * 어느 칩에서 뜨는지도 중요하다 (B_BMS_protect.c가 양쪽 칩을 OR로 합치는 이유):
 *   - 셀 전압(CUV/COV/COVL) / 온도(OTC/OTD/OTF/UTC/UTD) : 각 칩이 자기 16셀·자기 서미스터로 판단
 *   - 전류(OCC/OCD1/OCD2/OCD3/SCD/OCDL/SCDL)            : BOT만 (TOP은 SRP/SRN 미사용)
 *   - OTINT/UTINT/HWDF                                   : 각 칩 독립
 * ------------------------------------------------------------------------- */
#define CHGFET_MASK_A 0x98U  /* SCD, OCC, COV */
#define CHGFET_MASK_B 0xD5U  /* OTF, OTINT, OTC, UTINT, UTC */
#define CHGFET_MASK_C 0x56U  /* SCDL, COVL, PTO, HWDF */
#define DSGFET_MASK_A 0xE4U  /* SCD, OCD2, OCD1, CUV */
#define DSGFET_MASK_B 0xE6U  /* OTF, OTINT, OTD, UTINT, UTD */
#define DSGFET_MASK_C 0xE2U  /* OCD3, SCDL, OCDL, HWDF */

/* Exported Functions Prototypes ----------------------------------------------*/
void BQ769x2_Init(BMS_Unit* unit);


#endif /* INC_B_BMS_INIT_H_ */
