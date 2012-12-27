#include <stdio.h>
#include <stdarg.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

#include "stm32f10x.h"
#include "serial.h"

#define IRQ_PRIO configLIBRARY_KERNEL_INTERRUPT_PRIORITY
#define QUEUE_LENGTH 256
#define STR_BUF_LEN 256

typedef struct _usart_t usart_t;
typedef struct _usart_params_t usart_params_t;

struct _usart_params_t {
	USART_TypeDef *base;
	GPIO_TypeDef *gpio;
	unsigned int tx_pin;
	unsigned int rx_pin;
	unsigned int clocks;
	unsigned int irq;
};

struct _usart_t {
	const usart_params_t* const params;
	xQueueHandle tx_queue;
	xQueueHandle rx_queue;
};

static void handle_interrupt(int n);
/*-----------------------------------------------------------------------------*/

static const usart_params_t usart_params[SERIAL_NUM] = {
	/* USART0 */
	{
		.base = USART1,
		.gpio = GPIOA,
		.tx_pin = (1UL << 9),
		.rx_pin = (1UL << 10),
		.clocks = RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA,
		.irq = USART1_IRQn,
	},
	/* USART1 */
	{
		.base = USART2,
		.gpio = GPIOA,
		.tx_pin = (1UL << 2),
		.rx_pin = (1UL << 3),
		.clocks = RCC_APB1Periph_USART2 | RCC_APB2Periph_GPIOA,
		.irq = USART2_IRQn,
	},
};

static usart_t usarts[SERIAL_NUM] = {
	/* USART0 */
	{
		.params = &usart_params[0],
		.tx_queue = NULL,
		.rx_queue = NULL,
	},
	/* USART1 */
	{
		.params = &usart_params[1],
		.tx_queue = NULL,
		.rx_queue = NULL,
	},
};
/*-----------------------------------------------------------------------------*/

int serial_init(int n, unsigned int baudrate) {
	if(n < 0 || n >= SERIAL_NUM) return -1;

	usart_t *usart = &usarts[n];
	if(usart->tx_queue || usart->rx_queue) return -1;

	const usart_params_t *params = usart->params;

	/* Create the queues used to hold Rx/Tx characters. */
	if((usart->rx_queue = xQueueCreate(QUEUE_LENGTH, sizeof(char))) == NULL) return -1;
	if((usart->tx_queue = xQueueCreate(QUEUE_LENGTH + 1, sizeof(char))) == NULL) return -1;

	/* Enable USART clock */
	RCC_APB2PeriphClockCmd(params->clocks, ENABLE);

	/* Configure pins */
	GPIO_InitTypeDef gpinit;
	gpinit.GPIO_Pin = params->rx_pin;
	gpinit.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_Init(params->gpio, &gpinit);

	gpinit.GPIO_Pin = params->tx_pin;
	gpinit.GPIO_Speed = GPIO_Speed_50MHz;
	gpinit.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_Init(params->gpio, &gpinit);

	/* Configure USART */
	USART_InitTypeDef usinit = {
		.USART_BaudRate = baudrate,
		.USART_WordLength = USART_WordLength_8b,
		.USART_StopBits = USART_StopBits_1,
		.USART_Parity = USART_Parity_No,
		.USART_HardwareFlowControl = USART_HardwareFlowControl_None,
		.USART_Mode = USART_Mode_Rx | USART_Mode_Tx,
	};

	USART_Init(params->base, &usinit);
	USART_ITConfig(params->base, USART_IT_RXNE, ENABLE);

	/* Configure NVIC */
	NVIC_InitTypeDef nvinit = {
		.NVIC_IRQChannel = params->irq,
		.NVIC_IRQChannelPreemptionPriority = IRQ_PRIO,
		.NVIC_IRQChannelSubPriority = 0,
		.NVIC_IRQChannelCmd = ENABLE,
	};
	NVIC_Init(&nvinit);

	return 0;
}

void serial_enabled(int n, int enabled) {
	if(n < 0 || n >= SERIAL_NUM) return;
	usart_t *usart = &usarts[n];
	if(!usart->tx_queue || !usart->rx_queue) return;

	/* Enable USART */
	USART_Cmd(usart->params->base, enabled);
}

int serial_rcv_char(int n, char *ch, unsigned long timeout) {
	if(n < 0 || n >= SERIAL_NUM) return -1;
	usart_t *usart = &usarts[n];
	if(!usart->tx_queue || !usart->rx_queue) return -1;

	return xQueueReceive(usart->rx_queue, ch, timeout) ? 0 : -1;
}

int serial_send_char(int n, int ch, unsigned long timeout) {
	if(n < 0 || n >= SERIAL_NUM) return -1;
	usart_t *usart = &usarts[n];
	if(!usart->tx_queue || !usart->rx_queue) return -1;

	char out = ch;
	if(xQueueSend(usart->tx_queue, &out, timeout)) {
		/* Enable interrupt */
		USART_ITConfig(usart->params->base, USART_IT_TXE, ENABLE);
		return 0;
	}

	return -1;
}

int serial_send_str(int n, const char *str, int length, unsigned long timeout) {
	if(n < 0 || n >= SERIAL_NUM) return -1;
	usart_t *usart = &usarts[n];
	if(!usart->tx_queue || !usart->rx_queue) return -1;

	int cnt = 0;
	while(length && (length > 0 || *str) && serial_send_char(n, *str, timeout) == 0) {
		str++;
		cnt++;
		if(length > 0) length--;
	}

	return cnt;
}

int serial_iprintf(int n, unsigned long timeout, const char *format, ...) {
	char buf[STR_BUF_LEN];
	va_list ap;

	va_start(ap, format);
	int ln = vsniprintf(buf, sizeof(buf), format, ap);
	va_end(ap);

	return serial_send_str(n, buf, ln, timeout);
}

/*-----------------------------------------------------------------------------*/
static void handle_interrupt(int n) {
	usart_t *usart = &usarts[n];
	if(!usart->tx_queue || !usart->rx_queue) return;

	const usart_params_t *params = usart->params;
	portBASE_TYPE preempt = pdFALSE;

	if(USART_GetITStatus(params->base, USART_IT_TXE)) {
		/* The interrupt was caused by the THR becoming empty.  Are there any
		more characters to transmit? */
		char ch;
		if(xQueueReceiveFromISR(usart->tx_queue, &ch, &preempt) == pdTRUE) {
			USART_SendData(params->base, ch);
		} else {
			USART_ITConfig(params->base, USART_IT_TXE, DISABLE);
		}
	}

	if(USART_GetITStatus(params->base, USART_IT_RXNE)) {
		char ch = USART_ReceiveData(params->base);
		xQueueSendFromISR(usart->rx_queue, &ch, &preempt);
	}

	portEND_SWITCHING_ISR(preempt);
}

void USART1_IRQHandler(void) {
	handle_interrupt(0);
}

void USART2_IRQHandler(void) {
	handle_interrupt(1);
}
