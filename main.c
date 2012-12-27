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

#define DAYTIME_TIMER_PERIOD_MS 1000UL

#define CMD_PRIO tskIDLE_PRIORITY
#define CMD_STACK_SIZE (configMINIMAL_STACK_SIZE + 512)
#define CMD_SERIAL 0

#define SENSOR_PRIO (tskIDLE_PRIORITY + 2)
#define SENSOR_STACK_SIZE (configMINIMAL_STACK_SIZE + 512)

#define SERIAL_BAUDRATE 57600

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

typedef struct _sys_conf_data_t {
	struct tm daytime_start;
	struct tm daytime_end;
} sys_conf_data_t;

typedef struct _sys_conf_t {
	sys_conf_data_t *data;
	xSemaphoreHandle mutex;
} sys_conf_t;

/*-----------------------------------------------------------------------------*/
static void init_hardware();
static void daytime_cb(xTimerHandle handle);
static void cmd_thread(void *arg);
static void dht_poll_thread(void *arg);

static sys_conf_data_t conf_data;
static sys_conf_t conf = {
	.data = &conf_data,
};
static int led_state = 0;
static volatile sensor_data_t sensor_data;
static xSemaphoreHandle sensor_data_mutex;
/*-----------------------------------------------------------------------------*/

int main(void) {
	init_hardware();

	gpio_set(GPIO_LED_0, 1);
	gpio_set(GPIO_LED_1, 0);

	/* Daylight control timer */
	xTimerHandle daytime_timer = xTimerCreate((const signed char*)"Daytime", DAYTIME_TIMER_PERIOD_MS / portTICK_RATE_MS,
									pdTRUE,	NULL, daytime_cb);

	xTimerStart(daytime_timer, portMAX_DELAY);

	/* Command interpreter */
	xTaskCreate(cmd_thread, (const signed char *)"Cmd", CMD_STACK_SIZE, (void*)CMD_SERIAL, CMD_PRIO, NULL);

	/* Sensor polling */
	sensor_data_mutex = xSemaphoreCreateMutex();
	xTaskCreate(dht_poll_thread, (const signed char *)"Poll", SENSOR_STACK_SIZE, (void*)CMD_SERIAL, SENSOR_PRIO, NULL);

	/* System configuration */
	conf.mutex = xSemaphoreCreateMutex();

	vTaskStartScheduler();

	return 0;
}

static void daytime_cb(xTimerHandle handle) {
	gpio_set(GPIO_LED_0, led_state);
	gpio_set(GPIO_LED_1, !led_state);

	led_state = !led_state;

	/* TODO */
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

			/* TODO: PID here */

		} else {
			data.read_errors++;
		}

		/* update (maybe) sensor data */
		if(xSemaphoreTake(sensor_data_mutex, 0)) {
			sensor_data = data;
			xSemaphoreGive(sensor_data_mutex);
		}
	}
}
/*-----------------------------------------------------------------------------*/
/* get/set date and time */
static int date_proc(int sern, int argc, char **argv) {
	struct tm tim;
	rtc_to_time(RTC_GetCounter(), &tim);

	if(argc) {
		char *arg = argv[0];
		char *end;
		/* hours */
		tim.tm_hour = strtol(arg, &end, 10);
		if(end == arg || tim.tm_hour < 0 || tim.tm_hour > 23) return 1;
		if(*(arg = end)) arg++;

		/* minutes */
		tim.tm_min = strtol(arg, &end, 10);
		if(end == arg || tim.tm_min < 0 || tim.tm_min > 59) return 1;
		if(*(arg = end)) arg++;

		/* seconds */
		tim.tm_sec = strtol(arg, &end, 10);

		if(argc > 1) {
			arg = argv[1];

			/* day */
			tim.tm_mday = strtol(arg, &end, 10);
			if(end == arg || tim.tm_mday < 1 || tim.tm_mday > 31) return 1;
			if(*(arg = end)) arg++;

			/* month */
			tim.tm_mon = strtol(arg, &end, 10);
			if(end == arg || tim.tm_mon < 1 || tim.tm_mon > 12) return 1;
			if(*(arg = end)) arg++;

			/* year */
			tim.tm_year = strtol(arg, &end, 10);
			if(end == arg) return 1;

			tim.tm_mon--;
			tim.tm_year -= 1900;
		}

		/* set time */
		rtc_set(rtc_from_time(&tim));
	} else {
		/* print */
		char buf[32];
		time_to_str(buf, sizeof(buf), &tim);
		serial_iprintf(sern, portMAX_DELAY, "%s\r\n", buf);
	}
	return 0;
}

/* show temperature and humidity */
static int temp_proc(int sern, int argc, char **argv) {
	bool follow = (argc > 0) && !strcmp(argv[0], "-f");
	do {
		sensor_data_t data;

		/* get last measurement */
		xSemaphoreTake(sensor_data_mutex, portMAX_DELAY);
		data = sensor_data;
		xSemaphoreGive(sensor_data_mutex);

		serial_iprintf(sern, portMAX_DELAY, "T: %d.%d degrees C, RH: %d.%d%%, Timestamp: %lu, Errors count: %lu\r",
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

/* show temperature and humidity */
static int light_proc(int sern, int argc, char **argv) {
	/* TODO */
	int on = (argc > 0) && !strcmp(argv[0], "on");
	gpio_set(GPIO_RELAY_0, on);

	return 0;
}

static const cmd_handler_t cmdroot[] = {
	{.type = CMD_PROC, .cmd = "date", .h = {.proc = date_proc},},
	{.type = CMD_PROC, .cmd = "temp", .h = {.proc = temp_proc},},
	{.type = CMD_PROC, .cmd = "light", .h = {.proc = light_proc},},
	{.type = CMD_END},
};

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
		if(read_line(sern, cmdbuf, sizeof(cmdbuf), &history, "> ") > 1) {
			cmd_exec(sern, cmdroot, cmdbuf);
		}
	}
}

/*-----------------------------------------------------------------------------*/
static void init_hardware() {
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

	gpio_init();
    rtc_init();
    serial_init(CMD_SERIAL, SERIAL_BAUDRATE);
	serial_enabled(CMD_SERIAL, 1); /* enable */
	dht_init();
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
