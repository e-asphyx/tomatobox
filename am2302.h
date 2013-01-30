#ifndef _AM2302_H_
#define _AM2302_H_

#include <stdint.h>
#include "FreeRTOS.h"
#include "semphr.h"

#define DHT_COLLECTION_PERIOD_MS 2000UL

typedef enum {
	DHT_NO_ERROR,
	DHT_IRQ_TIMEOUT,
	DHT_TIMEOUT,
	DHT_RCV_TIMEOUT,
	DHT_DECODE_ERROR,
	DHT_CHECKSUM_ERROR,
} dht_error_t;

/*-----------------------------------------------------------------------------*/
int dht_init();
/* temperature in 1/10 deg C, humidity in 1/10 % */
int dht_read(xSemaphoreHandle read_sem, int *temperature, int *humidity, dht_error_t *error);

#endif
