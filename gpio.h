#ifndef _GPIO_H_
#define _GPIO_H_

enum {
	GPIO_LED_0 = 0,
	GPIO_LED_1,
	GPIO_RELAY_0,

	GPIO_NUM_PINS,
};

void gpio_init();
void gpio_set(int n, int on);

#endif
