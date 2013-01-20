#include <stdint.h>
#include "stm32f10x.h"
#include "gpio.h"

#define GPIO_CLOCKS_EN (RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC)

typedef struct _gpio_pin_t {
	GPIO_TypeDef *base;
	uint16_t pin;
	unsigned char init;
} gpio_pin_t;

static const gpio_pin_t pins[GPIO_NUM_PINS] = {
	[GPIO_LED_0] = {GPIOB, GPIO_Pin_9, 1},
	[GPIO_LED_1] = {GPIOB, GPIO_Pin_8, 1},
	[GPIO_RELAY_LIGHT] = {GPIOC, GPIO_Pin_0, 0},
};

void gpio_init() {
	RCC_APB2PeriphClockCmd(GPIO_CLOCKS_EN, ENABLE);

	GPIO_InitTypeDef gpconf = {
		.GPIO_Mode = GPIO_Mode_Out_PP,
		.GPIO_Speed = GPIO_Speed_50MHz,
	};

	int i;
	for(i = 0; i < GPIO_NUM_PINS; i++) {
		/* Set initial state */
		if(pins[i].init) {
			pins[i].base->BSRR = pins[i].pin;
		} else {
			pins[i].base->BRR = pins[i].pin;
		}
		gpconf.GPIO_Pin = pins[i].pin;
		GPIO_Init(pins[i].base, &gpconf);
	}
}

void gpio_set(int n, int on) {
	if(n >= GPIO_NUM_PINS) return;

	if(on) {
		pins[n].base->BSRR = pins[n].pin;
	} else {
		pins[n].base->BRR = pins[n].pin;
	}
}
