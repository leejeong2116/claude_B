/*
 * B_BMS_power_mode.h
 *
 *  Created on: May 6, 2026
 *      Author: user
 */

#ifndef INC_B_BMS_POWER_MODE_H_
#define INC_B_BMS_POWER_MODE_H_

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    WAKEUP_NONE = 0,
    WAKEUP_BY_ALERT
} WakeupReason_t;

extern volatile WakeupReason_t wakeup_reason;
extern uint32_t no_load_time_ms;

bool Is_No_Load(void);

//void Configure_BMS_Sleep_Mask(void);
//void Configure_BMS_Normal_Mask(void);

void Enter_Sleep_Sequence(void);
bool Handle_Wakeup_Event(void);

#endif /* INC_B_BMS_POWER_MODE_H_ */
