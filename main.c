#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "stm32f10x.h"

/* Scheduler includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#include "semphr.h"

#include "serial.h"
#include "rtc.h"
#include "readline.h"
#include "cmd.h"
#include "gpio.h"
#include "am2302.h"
#include "conf.h"
#include "dimmer.h"
#include "pid.h"
#include "fp.h"

#define DAYTIME_TIMER_PERIOD_MS 1000UL

#define CMD_PRIO tskIDLE_PRIORITY
#define CMD_STACK_SIZE (configMINIMAL_STACK_SIZE + 512)
#define CMD_SERIAL 0
#define CMD_PROMPT "> "

#define SENSOR_PRIO (tskIDLE_PRIORITY + 2)
#define SENSOR_STACK_SIZE (configMINIMAL_STACK_SIZE + 512)

#define SERIAL_BAUDRATE 57600
#define LEDS_NUM 2
#define BLINK_DELAY_MS 10UL
#define DHT_RESPONSE_LED 0

#ifndef ABS
#define ABS(x) ((x) < 0 ? -(x) : (x))
#endif

/* published sensor data, to be read from lo-pri thread */
typedef struct _sensor_data_t {
	unsigned long timestamp;
	unsigned long read_errors;
	int temperature;
	int humidity;
} sensor_data_t;

typedef int (*getter_proc_t)(char *buf, size_t size, int id, volatile void *data);
typedef int (*setter_proc_t)(const char *str, int id, volatile void *data);

typedef struct _conf_var_t {
	const char *key;
	const char *desc;

	getter_proc_t get;
	setter_proc_t set;
	int id;
	volatile void *data;
} conf_var_t;

/*-----------------------------------------------------------------------------*/
static void init_hardware();
static void daytime_cb(xTimerHandle handle);
static void blink_cb(xTimerHandle handle);
static void cmd_thread(void *arg);
static void dht_poll_thread(void *arg);
static void handle_daytime();
static void do_blink(int led, portTickType delay);

static int temp_proc(int sern, int argc, char **argv);
static int saveconf_proc(int sern, int argc, char **argv);
static int get_proc(int sern, int argc, char **argv);
static int set_proc(int sern, int argc, char **argv);
static int reset_proc(int sern, int argc, char **argv);

/* getters / setters */
static int gen_fp_get(char *buf, size_t size, int id, volatile void *data);
static int fan_mode_set(const char *buf, int id, volatile void *data);
static int fan_mode_get(char *buf, size_t size, int id, volatile void *data);
static int gen_fp_set(const char *buf, int id, volatile void *data);
static int fan_upper_limit_set(const char *buf, int id, volatile void *data);
static int fan_pid_set(const char *buf, int id, volatile void *data);

static int temp_get(char *buf, size_t size, int id, volatile void *data);
static int hum_get(char *buf, size_t size, int id, volatile void *data);

static int light_mode_set(const char *buf, int id, volatile void *data);
static int light_mode_get(char *buf, size_t size, int id, volatile void *data);
static int time_get(char *buf, size_t size, int id, volatile void *data);
static int daytime_set(const char *buf, int id, volatile void *data);

static int rtc_time_get(char *buf, size_t size, int id, volatile void *data);
static int rtc_time_set(const char *buf, int id, volatile void *data);
static int rtc_date_get(char *buf, size_t size, int id, volatile void *data);
static int rtc_date_set(const char *buf, int id, volatile void *data);

/* static variables */
static volatile sensor_data_t sensor_data;
static xSemaphoreHandle sensor_data_mutex;
static xSemaphoreHandle conf_mutex;
static xTimerHandle blink_timers[LEDS_NUM];
static pid_state_t fan_pid;
static volatile light_mode_t light_state = LIGHT_OFF; /* used for choosing temperature */

/* configuration variables */
static const conf_var_t cfgvars[] = {
	/* date */
	{.key = "rtc.time", .desc = "System time, hh:mm[:ss]", .get = rtc_time_get, .set = rtc_time_set,},
	{.key = "rtc.date", .desc = "System date, dd:mm:yyyy", .get = rtc_date_get, .set = rtc_date_set,},

	/* light */
	{.key = "light.mode", .desc = "Light control On/Off/Daytime", .get = light_mode_get, .set = light_mode_set,},
	{.key = "light.sdt", .desc = "Daytime start, hh:mm[:ss]",
		.get = time_get, .set = daytime_set, .data = &conf_data.daytime_start,},
	{.key = "light.edt", .desc = "Daytime end, hh:mm[:ss]",
		.get = time_get, .set = daytime_set, .data = &conf_data.daytime_end,},

	/* fan control */
	{.key = "fan.mode", .desc = "Fan control PID/Manual", .get = fan_mode_get, .set = fan_mode_set,},
	{.key = "fan.min", .desc = "Fan min (in PID mode) or permanent (in manual mode) duty cycle, percent",
		.get = gen_fp_get, .set = gen_fp_set, .data = &conf_data.fan_lower_limit},
	{.key = "fan.max", .desc = "Fan mxn duty cycle in PID mode, percent",
		.get = gen_fp_get, .set = fan_upper_limit_set, .data = &conf_data.fan_upper_limit},
	{.key = "fan.pid.kp", .desc = "Fan PID Kp", .get = gen_fp_get, .set = fan_pid_set, .data = &conf_data.fan_coef.k_p},
	{.key = "fan.pid.ki", .desc = "Fan PID Ki", .get = gen_fp_get, .set = fan_pid_set, .data = &conf_data.fan_coef.k_i},
	{.key = "fan.pid.kd", .desc = "Fan PID Kd", .get = gen_fp_get, .set = fan_pid_set, .data = &conf_data.fan_coef.k_d},

	/* temperature setpoint */
	{.key = "tsetp.d", .desc = "Temperature setpoint (light switched on)",
		.get = gen_fp_get, .set = gen_fp_set, .data = &conf_data.temperature[LIGHT_ON]},
	{.key = "tsetp.n", .desc = "Temperature setpoint (light switched off)",
		.get = gen_fp_get, .set = gen_fp_set, .data = &conf_data.temperature[LIGHT_OFF]},

	/* measured values */
	{.key = "temp", .desc = "Measured temperature", .get = temp_get,},
	{.key = "hum", .desc = "Measured humidity", .get = hum_get,},

	{.key = NULL,},
};

static const cmd_handler_t cmdroot[] = {
	/* getters/setters interface */
	{.type = CMD_PROC, .cmd = "get", .h = {.proc = get_proc},},
	{.type = CMD_PROC, .cmd = "set", .h = {.proc = set_proc},},

	/* temperature monitor */
	{.type = CMD_PROC, .cmd = "temp", .h = {.proc = temp_proc},},

	{.type = CMD_PROC, .cmd = "saveconf", .h = {.proc = saveconf_proc},},
	{.type = CMD_PROC, .cmd = "reset", .h = {.proc = reset_proc},},

	{.type = CMD_END},
};

/*-----------------------------------------------------------------------------*/
/* compare time values */
static inline bool daytime_less(struct tm *t1, struct tm *t2) {
	return (t1->tm_hour < t2->tm_hour) || ((t1->tm_hour == t2->tm_hour) &&
			((t1->tm_min < t2->tm_min) || ((t1->tm_min == t2->tm_min) &&
			(t1->tm_sec < t2->tm_sec))));
}

/* switch light on and off setup "day" or "night" temperature */
static void handle_daytime() {
	struct tm tim;
	rtc_to_time(RTC_GetCounter(), &tim);

	if(xSemaphoreTake(conf_mutex, DAYTIME_TIMER_PERIOD_MS / portTICK_RATE_MS)) {
		if(conf_data.light_mode == LIGHT_DAYTIME) {
			struct tm daytime_start = conf_data.daytime_start;
			struct tm daytime_end = conf_data.daytime_end;

			light_mode_t state;
			if(daytime_less(&daytime_start, &daytime_end)) {
				state = !daytime_less(&tim, &daytime_start) &&
						daytime_less(&tim, &daytime_end);
			} else {
				/* cross midnight */
				state = !daytime_less(&tim, &daytime_start) ||
						daytime_less(&tim, &daytime_end);
			}

			if(light_state != state) {
				light_state = state;
				gpio_set(GPIO_RELAY_LIGHT, light_state);
			}

		} else if(light_state != conf_data.light_mode) {
			/* manual light control */
			light_state = conf_data.light_mode;
			gpio_set(GPIO_RELAY_LIGHT, light_state);
		}
		xSemaphoreGive(conf_mutex);
	}
}

static void daytime_cb(xTimerHandle handle) {
	handle_daytime();
}

static void blink_cb(xTimerHandle handle) {
	int led = (int)pvTimerGetTimerID(handle);
	gpio_set(GPIO_LED_0 + led, 1);
}

/* perform short blink */
static void do_blink(int led, portTickType delay) {
	if(led < LEDS_NUM) {
		gpio_set(GPIO_LED_0 + led, 0);
		xTimerStart(blink_timers[led], delay);
	}
}

static void dht_poll_thread(void *arg) {
	xSemaphoreHandle read_sem;
	vSemaphoreCreateBinary(read_sem);
	if(!read_sem) vTaskDelete(NULL);

	sensor_data_t data = {.read_errors = 0};

	portTickType last_wake = xTaskGetTickCount();
	while(1) {
		vTaskDelayUntil(&last_wake, DHT_COLLECTION_PERIOD_MS / portTICK_RATE_MS);

		dht_error_t err;
		if(dht_read(read_sem, &data.temperature, &data.humidity, &err) == 0) {
			data.timestamp = xTaskGetTickCount();
			do_blink(DHT_RESPONSE_LED, DHT_COLLECTION_PERIOD_MS / portTICK_RATE_MS);

			if(xSemaphoreTake(conf_mutex, DHT_COLLECTION_PERIOD_MS / portTICK_RATE_MS / 2)) {
				if(conf_data.fan_mode == FAN_PID) {
					/* compute PID */
					fixed_t input = (data.temperature * FP_ONE) / 10;
					fixed_t out = pid_compute(&fan_pid,	input, conf_data.temperature[light_state],
							data.timestamp * portTICK_RATE_MS);

					fixed_t ll = conf_data.fan_lower_limit;
					/* Fan torque may be too small at low values, avoid them */
					if(out > 0 && out < ll)	out = ll;

					/* Adjust fan */
					dimmer_set(FP_ROUND(out));
				}
				xSemaphoreGive(conf_mutex);
			}
		} else {
			data.read_errors++;
		}

		/* update sensor data */
		if(xSemaphoreTake(sensor_data_mutex, DHT_COLLECTION_PERIOD_MS / portTICK_RATE_MS / 2)) {
			sensor_data = data;
			xSemaphoreGive(sensor_data_mutex);
		}
	}
}
/*-----------------------------------------------------------------------------*/
/* show temperature and humidity */
static int temp_proc(int sern, int argc, char **argv) {
	bool follow = (argc > 0) && !strcmp(argv[0], "-f");
	do {
		sensor_data_t data;

		/* get last measurement */
		xSemaphoreTake(sensor_data_mutex, portMAX_DELAY);
		data = sensor_data;
		xSemaphoreGive(sensor_data_mutex);

		serial_iprintf(sern, portMAX_DELAY, "T: %d.%1d degrees C, RH: %d.%1d%%, Timestamp: %lu, Errors count: %lu\r",
					data.temperature / 10, ABS(data.temperature) % 10,
					data.humidity / 10, data.humidity % 10,
					data.timestamp, data.read_errors);
		/* loop until "q" pressed */
		if(follow) {
			char ch;
			if(!serial_rcv_char(sern, &ch, DHT_COLLECTION_PERIOD_MS / portTICK_RATE_MS) && ch == 'q') follow = false;
		}

	} while(follow);
	serial_send_str(sern, "\r\n", -1, portMAX_DELAY);

	return 0;
}

static int saveconf_proc(int sern, int argc, char **argv) {
	if(!conf_write()) {
		serial_send_str(sern, "Ok\r\n", -1, portMAX_DELAY);
		return 0;
	} else {
		serial_send_str(sern, "Failed\r\n", -1, portMAX_DELAY);
		return 1;
	}
}

/* System reset */
static int reset_proc(int sern, int argc, char **argv) {
	NVIC_SystemReset();
	return 0;
}

static void list_vars(int sern, const conf_var_t *vars) {
	serial_send_str(sern, "\r\nAvailable variables:\r\n", -1, portMAX_DELAY);

	while(vars->key != NULL) {
		serial_iprintf(sern, portMAX_DELAY, "%s\t\t%s\r\n", vars->key, vars->desc ? vars->desc : "");
		vars++;
	}
	serial_send_str(sern, "\r\n", -1, portMAX_DELAY);
}

static const conf_var_t *var_handler(const conf_var_t *vars, const char *key) {
	while(vars->key != NULL) {
		if(strcmp(vars->key, key) == 0) return vars;
		vars++;
	}

	return NULL;
}

static int get_proc(int sern, int argc, char **argv) {
	if(!argc) {
		serial_send_str(sern, "Missed variable name\r\n", -1, portMAX_DELAY);
		list_vars(sern, cfgvars);
		return 1;
	}

	const conf_var_t *var = var_handler(cfgvars, argv[0]);
	if(!var) {
		list_vars(sern, cfgvars);
		return 1;
	}
	if(!var->get) return 1;

	char buf[64];
	if(var->get(buf, sizeof(buf), var->id, var->data) < 0) {
		serial_send_str(sern, "Failed\r\n", -1, portMAX_DELAY);
		return 1;
	}

	serial_iprintf(sern, portMAX_DELAY, "%s\r\n", buf);

	return 0;
}

static int set_proc(int sern, int argc, char **argv) {
	if(!argc) {
		serial_send_str(sern, "Missed variable name\r\n", -1, portMAX_DELAY);
		list_vars(sern, cfgvars);
		return 1;
	} else if(argc == 1) {
		serial_send_str(sern, "Missed argument\r\n", -1, portMAX_DELAY);
		return 1;
	}

	const conf_var_t *var = var_handler(cfgvars, argv[0]);
	if(!var) {
		list_vars(sern, cfgvars);
		return 1;
	}
	if(!var->set) return 1;

	if(var->set(argv[1], var->id, var->data) < 0) {
		serial_send_str(sern, "Failed\r\n", -1, portMAX_DELAY);
		return 1;
	}

	return 0;
}

/*-----------------------------------------------------------------------------*/
/* generic fixed-point getter */
static int gen_fp_get(char *buf, size_t size, int id, volatile void *data) {
	fixed_t val = *((volatile fixed_t*)data);

	if(sniprintf(buf, size, "%d.%03d",
				(int)FP_TRUNC(val),
				(int)FP_TRUNC(FP_FRAC(FP_ABS(val)) * 1000)) == size) buf[size - 1] = 0;

	return 0;
}

/* generic fixed-point setter */
static int gen_fp_set(const char *buf, int id, volatile void *data) {
	*((volatile fixed_t*)data) = str_to_fp(buf, NULL);
	return 0;
}

static int fan_mode_set(const char *buf, int id, volatile void *data) {
	fan_mode_t mode = (!strcmp(buf, "PID") || !strcmp(buf, "Pid") || !strcmp(buf, "pid") || !strcmp(buf, "1")) ?
		FAN_PID : FAN_MANUAL;

	if(xSemaphoreTake(conf_mutex, portMAX_DELAY)) {
		conf_data.fan_mode = mode;
		if(mode == FAN_MANUAL) {
			/* Update fan dimmer */
			dimmer_set(FP_ROUND(conf_data.fan_lower_limit));
		}
		xSemaphoreGive(conf_mutex);
		return 0;
	}

	return -1;
}

static int fan_mode_get(char *buf, size_t size, int id, volatile void *data) {
	strncpy(buf, conf_data.fan_mode == FAN_PID ? "PID" : "Manual", size);
	return 0;
}

static int fan_pid_set(const char *buf, int id, volatile void *data) {
	volatile fixed_t *ptr = (volatile fixed_t*)data;
	fixed_t val = str_to_fp(buf, NULL);

	if(xSemaphoreTake(conf_mutex, portMAX_DELAY)) {
		*ptr = val;
		/* update PID tuning */
		pid_coef_t fan_coef = conf_data.fan_coef;
		pid_set_coef(&fan_pid, &fan_coef);
		xSemaphoreGive(conf_mutex);
		return 0;
	}
	return -1;
}

static int fan_upper_limit_set(const char *buf, int id, volatile void *data) {
	fixed_t val = str_to_fp(buf, NULL);

	if(xSemaphoreTake(conf_mutex, portMAX_DELAY)) {
		conf_data.fan_upper_limit = val;
		/* update PID limits */
		pid_set_limits(&fan_pid, DIMMER_MIN, val);
		xSemaphoreGive(conf_mutex);
		return 0;
	}
	return -1;
}

static int temp_get(char *buf, size_t size, int id, volatile void *data) {
	/* get last measurement */
	int t = sensor_data.temperature;
	if(sniprintf(buf, size, "%d.%1d", t / 10, ABS(t) % 10) == size) buf[size - 1] = 0;
	return 0;
}

static int hum_get(char *buf, size_t size, int id, volatile void *data) {
	/* get last measurement */
	int h = sensor_data.humidity;
	if(sniprintf(buf, size, "%d.%1d", h / 10, ABS(h) % 10) == size) buf[size - 1] = 0;
	return 0;
}

static int light_mode_set(const char *buf, int id, volatile void *data) {
	light_mode_t mode;

	if(!strcmp(buf, "Off") || !strcmp(buf, "off")) {
		mode = LIGHT_OFF;
	} else if(!strcmp(buf, "On") || !strcmp(buf, "on")) {
		mode = LIGHT_ON;
	} else if(!strcmp(buf, "Daytime") || !strcmp(buf, "daytime") || !strcmp(buf, "dt")) {
		mode = LIGHT_DAYTIME;
	} else {
		mode = strtol(buf, NULL, 0);
	}

	if(xSemaphoreTake(conf_mutex, portMAX_DELAY)) {
		conf_data.light_mode = mode;
		xSemaphoreGive(conf_mutex);
		handle_daytime();
		return 0;
	}
	return -1;
}

static int light_mode_get(char *buf, size_t size, int id, volatile void *data) {
	light_mode_t mode = conf_data.light_mode;
	const char *str;
	switch(mode) {
		case LIGHT_ON:
			str = "On";
			break;

		case LIGHT_OFF:
			str = "Off";
			break;

		default:
			str = "Daytime";
			break;
	}
	strncpy(buf, str, size);

	return 0;
}

static int time_get(char *buf, size_t size, int id, volatile void *data) {
	volatile struct tm *tim = (volatile struct tm*)data;

	if(sniprintf(buf, size, "%02d:%02d:%02d", tim->tm_hour, tim->tm_min, tim->tm_sec) == size)
		buf[size - 1] = 0;

	return 0;
}

static int daytime_set(const char *buf, int id, volatile void *data) {
	volatile struct tm *tim = (volatile struct tm*)data;

	struct tm tmp;
	memset(&tmp, 0, sizeof(struct tm));

	if(!parse_time(buf, &tmp)) return -1;

	if(xSemaphoreTake(conf_mutex, DAYTIME_TIMER_PERIOD_MS / portTICK_RATE_MS)) {
		*tim = tmp;
		xSemaphoreGive(conf_mutex);

		handle_daytime();
		return 0;
	}

	return -1;
}

/* get/set date and time */
static int rtc_time_get(char *buf, size_t size, int id, volatile void *data) {
	struct tm tim;
	rtc_to_time(RTC_GetCounter(), &tim);

	if(sniprintf(buf, size, "%02d:%02d:%02d", tim.tm_hour, tim.tm_min, tim.tm_sec) == size)
		buf[size - 1] = 0;

	return 0;
}

static int rtc_date_get(char *buf, size_t size, int id, volatile void *data) {
	struct tm tim;
	rtc_to_time(RTC_GetCounter(), &tim);

	if(sniprintf(buf, size, "%02d-%02d-%d",
				tim.tm_mday, tim.tm_mon + 1, 1900 + tim.tm_year) == size)
		buf[size - 1] = 0;

	return 0;
}

static int rtc_time_set(const char *buf, int id, volatile void *data) {
	struct tm tim;
	rtc_to_time(RTC_GetCounter(), &tim);

	if(!parse_time(buf, &tim)) return -1;

	/* set time */
	rtc_set(rtc_from_time(&tim));
	return 0;
}

static int rtc_date_set(const char *buf, int id, volatile void *data) {
	struct tm tim;
	rtc_to_time(RTC_GetCounter(), &tim);

	if(!parse_date(buf, &tim)) return -1;

	/* set time */
	rtc_set(rtc_from_time(&tim));
	return 0;
}

/*-----------------------------------------------------------------------------*/
/* history buffer */
static history_t history;
char cmdbuf[64];

static void cmd_thread(void *arg) {
	int sern = (int)arg;

	serial_send_str(sern, "Welcome to TomatoBox\r\n", -1, portMAX_DELAY);
	if(!rtc_valid()) {
		serial_send_str(sern,
				"RTC power has been lost. Please set date and time\r\n", -1, portMAX_DELAY);
	}

	history.r_idx = history.w_idx = 0;
	while(1) {
		if(read_line(sern, cmdbuf, sizeof(cmdbuf), &history, CMD_PROMPT) > 1) {
			cmd_exec(sern, cmdroot, cmdbuf);
		}
	}
}

static void init_hardware() {
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

	gpio_init();
    rtc_init();
    serial_init(CMD_SERIAL, SERIAL_BAUDRATE);
	serial_enabled(CMD_SERIAL, 1); /* enable */
}

/*-----------------------------------------------------------------------------*/
int main(void) {
	init_hardware();

	/* load configuration */
	conf_init();

	/* init drivers */
	dht_init();
	dimmer_init();

	/* Fan PID */
	pid_coef_t fan_coef = conf_data.fan_coef;
	pid_init(&fan_pid, &fan_coef, DIMMER_MIN, conf_data.fan_upper_limit);

	/* Daylight control timer */
	xTimerHandle daytime_timer = xTimerCreate((const signed char*)"Daytime", DAYTIME_TIMER_PERIOD_MS / portTICK_RATE_MS,
									pdTRUE, NULL, daytime_cb);
	xTimerStart(daytime_timer, portMAX_DELAY);

	int i;
	for(i = 0; i < LEDS_NUM; i++) {
		blink_timers[i] = xTimerCreate((const signed char*)"Blink", BLINK_DELAY_MS / portTICK_RATE_MS,
									pdFALSE, (void*)i, blink_cb);
	}

	/* Command interpreter */
	xTaskCreate(cmd_thread, (const signed char *)"Cmd", CMD_STACK_SIZE, (void*)CMD_SERIAL, CMD_PRIO, NULL);

	/* Sensor polling */
	sensor_data_mutex = xSemaphoreCreateMutex();
	xTaskCreate(dht_poll_thread, (const signed char *)"Poll", SENSOR_STACK_SIZE, (void*)CMD_SERIAL, SENSOR_PRIO, NULL);

	/* System configuration */
	conf_mutex = xSemaphoreCreateMutex();

	/* Set light state */
	handle_daytime();

	vTaskStartScheduler();

	return 0;
}
/*-----------------------------------------------------------------------------*/

void vApplicationStackOverflowHook(xTaskHandle pxTask, signed char *pcTaskName) {
	(void)pcTaskName;
	(void)pxTask;

	taskDISABLE_INTERRUPTS();

	int st = 0;
	while(1) {
		gpio_set(GPIO_LED_0, st);
		gpio_set(GPIO_LED_1, st);
		st = !st;

		volatile int cnt = 1000000;
		while(cnt--);
	}
}

void vApplicationMallocFailedHook(void) {
	serial_send_str(0, "malloc failed!\r\n", -1, portMAX_DELAY);
}
