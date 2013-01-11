#ifndef _CONF_H_
#define _CONF_H_

#include <time.h>

typedef enum {
	LIGHT_OFF = 0,
	LIGHT_ON,
	LIGHT_DAYTIME, /* use on and off time */
} light_mode_t;

typedef struct _sys_conf_data_t sys_conf_data_t;

struct _sys_conf_data_t {
	light_mode_t light_mode;
	struct tm daytime_start;
	struct tm daytime_end;
};

extern volatile sys_conf_data_t conf_data;

void conf_init();
int conf_write();

#endif /* _CONF_H_ */
