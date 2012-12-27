/* DHT22 / AM2302 driver */
#include <stdbool.h>
#include "stm32f10x.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "am2302.h"

#define DHT_BIT_TIMEOUT_US (80 * 4) /* one bit timeout */
#define DHT_START_PULSE_MS 2
#define DHT_IRQ_TIMEOUT_MS 2 /* irq timeout */
#define DHT_PKT_SIZE 5
#define DHT_PKT_TIMEOUT_MS 10

/*-----------------------------------------------------------------------------*/
/* Use TIM3_CH1 */
#define DHT_GPIO GPIOC
#define DHT_PIN GPIO_Pin_6
#define DHT_TIM_CLK RCC_APB1Periph_TIM3
#define DHT_GPIO_CLK (RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO)
#define DHT_TIMER TIM3 /* Uses ABP1 clock */
#define DHT_TIMER_CHANNEL TIM_Channel_1
#define DHT_IRQN TIM3_IRQn
#define DHT_GPIO_REMAP GPIO_FullRemap_TIM3
#define DHT_IRQ_HANDLER TIM3_IRQHandler

#define DHT_GET_PCLK_FREQ(x) (((RCC->CFGR >> 8) & 0x7) >= 4 ? (x)->PCLK1_Frequency * 2 : (x)->PCLK1_Frequency)

#define DHT_IRQ_PRIO configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY
#define DHT_PRIO (configMAX_PRIORITIES - 1)

#define ERROR_DIV 2
#define PERIOD_OK(x, l, h) \
	((x)->low >= ((l) - (l) / ERROR_DIV) && \
	(x)->low < ((l) + (l) / ERROR_DIV) && \
	((x)->period - (x)->low) >= ((h) - (h) / ERROR_DIV) && \
	((x)->period - (x)->low) < ((h) + (h) / ERROR_DIV))
/*-----------------------------------------------------------------------------*/

typedef struct _pwm_capture_t pwm_capture_t;
typedef struct _dht_read_t dht_read_t;

struct _pwm_capture_t {
	unsigned int period;
	unsigned int low;
};

struct _dht_read_t {
	xSemaphoreHandle sem; /* in */
	dht_error_t error; /* out */
	uint8_t data[DHT_PKT_SIZE]; /* out */
};

static void dht_thread(void *data);
static inline void start_timer();
static inline void stop_timer();
/*-----------------------------------------------------------------------------*/
static xQueueHandle cmd_msgbox; /* read requests */
xSemaphoreHandle irq_sem;
static volatile pwm_capture_t pwm_data;
/*-----------------------------------------------------------------------------*/

static void gpio_input() {
	/* Pin configuration: input floating */
	GPIO_InitTypeDef gpconf = {
		.GPIO_Pin = DHT_PIN,
		.GPIO_Mode = GPIO_Mode_IN_FLOATING,
		.GPIO_Speed = GPIO_Speed_50MHz,
	};
	GPIO_Init(DHT_GPIO, &gpconf);
}

static void gpio_output() {
	/* Pin configuration: output open-drain */
	GPIO_InitTypeDef gpconf = {
		.GPIO_Pin = DHT_PIN,
		.GPIO_Mode = GPIO_Mode_Out_OD,
		.GPIO_Speed = GPIO_Speed_50MHz,
	};
	GPIO_Init(DHT_GPIO, &gpconf);
}

int dht_init() {
	if((cmd_msgbox = xQueueCreate(1, sizeof(dht_read_t*))) == NULL) return -1;
	vSemaphoreCreateBinary(irq_sem);
	if(irq_sem == NULL) return -1;
	xSemaphoreTake(irq_sem, 0);

	xTaskCreate(dht_thread, (const signed char *)"DHT", configMINIMAL_STACK_SIZE, NULL, DHT_PRIO, NULL);

	/* Enable clocks */
	RCC_APB1PeriphClockCmd(DHT_TIM_CLK, ENABLE);
	RCC_APB2PeriphClockCmd(DHT_GPIO_CLK, ENABLE);

	/* Pin configuration */
	gpio_input();

#ifdef DHT_GPIO_REMAP
	GPIO_PinRemapConfig(DHT_GPIO_REMAP, ENABLE);
#endif

	/* Enable the TIM global Interrupt */
	NVIC_InitTypeDef itconf = {
		.NVIC_IRQChannel = DHT_IRQN,
		.NVIC_IRQChannelPreemptionPriority = DHT_IRQ_PRIO,
		.NVIC_IRQChannelSubPriority = 0,
		.NVIC_IRQChannelCmd = ENABLE,
	};
	NVIC_Init(&itconf);

	/* Give 1us resolution */
	RCC_ClocksTypeDef clocks;
	RCC_GetClocksFreq(&clocks);
	unsigned long freq = DHT_GET_PCLK_FREQ(&clocks);

	TIM_PrescalerConfig(DHT_TIMER, freq / 1000000 - 1, TIM_PSCReloadMode_Immediate);

	/* PWM capture configuration */
	TIM_ICInitTypeDef icconf = {
		.TIM_Channel = DHT_TIMER_CHANNEL,
		.TIM_ICPolarity = TIM_ICPolarity_Falling,
		.TIM_ICSelection = TIM_ICSelection_DirectTI,
		.TIM_ICPrescaler = TIM_ICPSC_DIV1,
		.TIM_ICFilter = 0x3,
	};
	TIM_PWMIConfig(DHT_TIMER, &icconf);

	TIM_SelectInputTrigger(DHT_TIMER, TIM_TS_TI1FP1); /* Select the TIM3 Input Trigger: TI1FP1 */
	TIM_SelectSlaveMode(DHT_TIMER, TIM_SlaveMode_Reset); /* Select the slave Mode: Reset Mode */
	TIM_SelectMasterSlaveMode(DHT_TIMER, TIM_MasterSlaveMode_Enable); /* Enable the Master/Slave Mode */
	/* Configures the TIM Update Request Interrupt source: counter overflow */
	TIM_UpdateRequestConfig(DHT_TIMER, TIM_UpdateSource_Regular);

	DHT_TIMER->CNT = 0;
	DHT_TIMER->ARR = DHT_BIT_TIMEOUT_US; /* Set the TIM auto-reload register */
	DHT_TIMER->SR = ~(TIM_FLAG_Update | TIM_FLAG_CC1);

	return 0;
}

void DHT_IRQ_HANDLER(void) {
	portBASE_TYPE preempt = pdFALSE;

	if(DHT_TIMER->SR & TIM_FLAG_CC1) {
		pwm_data.low = DHT_TIMER->CCR2;
		pwm_data.period = DHT_TIMER->CCR1;

		DHT_TIMER->SR = ~TIM_FLAG_CC1;

		xSemaphoreGiveFromISR(irq_sem, &preempt);

	} else if(DHT_TIMER->SR & TIM_FLAG_Update) {
		/* timeout */

		DHT_TIMER->SR = ~TIM_FLAG_Update;
		stop_timer();

		pwm_data.period = 0;
		xSemaphoreGiveFromISR(irq_sem, &preempt);
	}

	portEND_SWITCHING_ISR(preempt);
}

static inline void start_timer() {
	DHT_TIMER->DIER |= (TIM_IT_Update | TIM_IT_CC1);
	DHT_TIMER->CNT = 0;
	DHT_TIMER->SR = ~(TIM_FLAG_Update | TIM_FLAG_CC1);
    DHT_TIMER->CR1 |= TIM_CR1_CEN;
}

static inline void stop_timer() {
	DHT_TIMER->DIER &= ~(TIM_IT_Update | TIM_IT_CC1);
    DHT_TIMER->CR1 &= ~TIM_CR1_CEN;
}

static void dht_thread(void *data) {
	while(1) {
		/* wait for read request */
		dht_read_t *req;
		if(!xQueueReceive(cmd_msgbox, &req, portMAX_DELAY)) continue;

		/* send start pulse */
		gpio_output();
		DHT_GPIO->BRR = DHT_PIN; /* 0 */
		vTaskDelay(DHT_START_PULSE_MS / portTICK_RATE_MS);
		DHT_GPIO->BSRR = DHT_PIN; /* Hi-Z */
		gpio_input();

		start_timer();

		/* skip first falling edge */
		int i;
		for(i = 0; i < 2; i++) {
			/* IRQ timeout or receive timeout */
			if(!xSemaphoreTake(irq_sem, DHT_IRQ_TIMEOUT_MS / portTICK_RATE_MS)) {
				req->error = DHT_IRQ_TIMEOUT;
				goto reply;
			}
			if(!pwm_data.period) {
				req->error = DHT_TIMEOUT;
				goto reply;
			}
		}
		/* start sequence received */
		if(!PERIOD_OK(&pwm_data, 80, 80)) {
			req->error = DHT_DECODE_ERROR;
			goto reply;
		}

		for(i = 0; i < DHT_PKT_SIZE; i++) {
			unsigned int mask = 0x80;
			uint8_t byte = 0;
			while(mask) {
				if(!xSemaphoreTake(irq_sem, DHT_IRQ_TIMEOUT_MS / portTICK_RATE_MS)) {
					req->error = DHT_IRQ_TIMEOUT;
					goto reply;
				}
				if(!pwm_data.period) {
					req->error = DHT_TIMEOUT;
					goto reply;
				}

				/* next bit received */
				if(PERIOD_OK(&pwm_data, 50, 70)) {
					byte |= mask; /* 1 */
				} else if(!PERIOD_OK(&pwm_data, 50, 27)) {
					req->error = DHT_DECODE_ERROR;
					goto reply;
				}

				mask >>= 1;
			}
			req->data[i] = byte;
		}
		req->error = DHT_NO_ERROR;
reply:
		stop_timer();
		xSemaphoreGive(req->sem);
	}
}

int dht_read(xSemaphoreHandle read_sem, int *temperature, int *humidity, dht_error_t *error) {
	dht_read_t rd;
	dht_read_t *rd_p = &rd;

	xSemaphoreTake(read_sem, 0); /* to be sure */
	rd_p->sem = read_sem;
	xQueueSend(cmd_msgbox, &rd_p, portMAX_DELAY);

	/* wait for reply */
	if(!xSemaphoreTake(read_sem, DHT_PKT_TIMEOUT_MS / portTICK_RATE_MS)) {
		*error = DHT_RCV_TIMEOUT;
		return -1;
	}

	if(rd.error != DHT_NO_ERROR) {
		*error = rd.error;
		return -1;
	}

	/* compute checksum */
	unsigned int sum = 0;
	int i;
	for(i = 0; i < DHT_PKT_SIZE - 1; i++) sum += rd.data[i];
	if((sum & 0xff) != rd.data[i]) {
		*error = DHT_CHECKSUM_ERROR;
		return -1;
	}

	/* read 16 bit humidity value */
	*humidity = ((unsigned int)rd.data[0] << 8) |
				(unsigned int)rd.data[1];

	/* read 16 bit temperature value */
	int val = ((unsigned int)rd.data[2] << 8) |
				(unsigned int)rd.data[3];
	*temperature = val & 0x8000 ? -(val & ~0x8000) : val;

	return 0;
}
