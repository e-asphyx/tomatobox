#ifndef _CONF_H_
#define _CONF_H_

#include <time.h>
#include "pid.h"

typedef enum {
	LIGHT_OFF = 0,
	LIGHT_ON,
	LIGHT_DAYTIME, /* use on and off time */
} light_mode_t;

typedef enum {
	FAN_MANUAL,
	FAN_PID,
} fan_mode_t;

typedef struct _sys_conf_data_t sys_conf_data_t;

struct _sys_conf_data_t {
	light_mode_t light_mode;
	struct tm daytime_start;
	struct tm daytime_end;

	/* "Night" and "Day" Temperature */
	fixed_t temperature[LIGHT_ON + 1];

	/* Fan PID */
	fan_mode_t fan_mode;
	fixed_t fan_lower_limit;
	fixed_t fan_upper_limit;

	pid_coef_t fan_coef;
} __attribute__((aligned(4)));

extern volatile sys_conf_data_t conf_data;

void conf_init();
int conf_write();

#endif /* _CONF_H_ */
