/*
 * B_BMS_soc.h
 *
 * Pack-level state-of-charge (SOC) estimation.
 */

#ifndef INC_B_BMS_SOC_H_
#define INC_B_BMS_SOC_H_

#ifdef __cplusplus
extern "C" {
#endif

void BMS_SOC_Init(void);
void BMS_SOC_Update(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_B_BMS_SOC_H_ */
