/* AC dimmer control */
#include <stdint.h>
#include <stdbool.h>

#include "stm32f10x.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/*-----------------------------------------------------------------------------*/
/*
Timer chain:
TIM1 -> (ITR0) TIM4 -> (ITR3) TIM2
*/
/*-----------------------------------------------------------------------------*/
/* Use TIM1_CH1 (PA8) */
#define ZC_GPIO GPIOA
#define ZC_PIN GPIO_Pin_8
#define ZC_CLK_ENABLE \
	RCC_APB2PeriphClockCmd((RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO | RCC_APB2Periph_TIM1), ENABLE)
#define ZC_TIMER TIM1 /* Uses APB2 clock */
#define ZC_TIMER_CHANNEL TIM_Channel_1
#define ZC_IRQN TIM1_CC_IRQn
#define ZC_IRQ_HANDLER TIM1_CC_IRQHandler
#define ZC_GET_PCLK_FREQ(x) (((RCC->CFGR >> 11) & 0x7) >= 4 ? (x)->PCLK2_Frequency * 2 : (x)->PCLK2_Frequency)
#define ZC_IRQ_PRIO (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY + 1)

/*-----------------------------------------------------------------------------*/
/* Use TIM4 for phase control */
#define PHASE_CLK_ENABLE RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE)
#define PHASE_TIMER TIM4 /* Uses APB1 clock */
#define PHASE_GET_PCLK_FREQ(x) (((RCC->CFGR >> 8) & 0x7) >= 4 ? (x)->PCLK1_Frequency * 2 : (x)->PCLK1_Frequency)
#define PHASE_TRIGGER TIM_TS_ITR0 /* <- TIM1 */

/*-----------------------------------------------------------------------------*/
/* Use TIM2_CH4 (remapped to PA0) for PWM */
#define PWM_GPIO GPIOA
#define PWM_PIN GPIO_Pin_0
#define PWM_CLK_ENABLE \
do { \
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE); \
	RCC_APB2PeriphClockCmd((RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO), ENABLE); \
} while(0)
#define PWM_TIMER TIM2 /* Uses APB1 clock */
#define PWM_TIMER_CHANNEL_INIT(x) TIM_OC1Init(PWM_TIMER, (x))
#define PWM_TIMER_CHANNEL_REG CCR1
#define PWM_GET_PCLK_FREQ(x) (((RCC->CFGR >> 8) & 0x7) >= 4 ? (x)->PCLK1_Frequency * 2 : (x)->PCLK1_Frequency)
#define PWM_TRIGGER TIM_TS_ITR3 /* <- TIM4 */

#define BASE_FREQ 1000000
#define MAX_VALUE 100
#define TRIAC_TRIGGER_PULSE_US 100

/*-----------------------------------------------------------------------------*/
static volatile unsigned int dimmer_phase = 0;

static const uint16_t acostab[MAX_VALUE - 1] = {
	959, 931, 911, 893, 877, 863, 849, 837, 825, 814, 804, 793, 784, 774,
	765, 756, 747, 738, 730, 722, 714, 706, 698, 690, 683, 675, 668, 661, 653, 646,
	639, 632, 625, 618, 611, 605, 598, 591, 584, 578, 571, 564, 558, 551, 545, 538,
	532, 525, 519, 512, 505, 499, 492, 486, 479, 473, 466, 460, 453, 446, 440, 433,
	426, 419, 413, 406, 399, 392, 385, 378, 371, 363, 356, 349, 341, 334, 326, 318,
	310, 302, 294, 286, 277, 268, 259, 250, 240, 231, 220, 210, 199, 187, 175, 161,
	147, 131, 113, 93, 65
};
/*-----------------------------------------------------------------------------*/
void dimmer_init() {
	/* Enable clocks */
	ZC_CLK_ENABLE;
	PHASE_CLK_ENABLE;
	PWM_CLK_ENABLE;

	/* Pin configuration */
	GPIO_InitTypeDef gpconf = {
		.GPIO_Pin = ZC_PIN,
		.GPIO_Mode = GPIO_Mode_IN_FLOATING,
		.GPIO_Speed = GPIO_Speed_50MHz,
	};
	GPIO_Init(ZC_GPIO, &gpconf);
#ifdef ZC_GPIO_REMAP
	GPIO_PinRemapConfig(ZC_GPIO_REMAP, ENABLE);
#endif

	gpconf.GPIO_Pin = PWM_PIN;
	gpconf.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_Init(PWM_GPIO, &gpconf);
#ifdef PWM_GPIO_REMAP
	GPIO_PinRemapConfig(PWM_GPIO_REMAP, ENABLE);
#endif

	/* Enable the TIM global Interrupt */
	NVIC_InitTypeDef itconf = {
		.NVIC_IRQChannel = ZC_IRQN,
		.NVIC_IRQChannelPreemptionPriority = ZC_IRQ_PRIO,
		.NVIC_IRQChannelSubPriority = 0,
		.NVIC_IRQChannelCmd = ENABLE,
	};
	NVIC_Init(&itconf);

	/* PWM capture configuration */
	/* Give 1us resolution */
	RCC_ClocksTypeDef clocks;
	RCC_GetClocksFreq(&clocks);
	unsigned long freq = ZC_GET_PCLK_FREQ(&clocks);
	TIM_PrescalerConfig(ZC_TIMER, freq / BASE_FREQ - 1, TIM_PSCReloadMode_Immediate);

	TIM_ICInitTypeDef icconf = {
		.TIM_Channel = ZC_TIMER_CHANNEL,
		.TIM_ICPolarity = TIM_ICPolarity_Rising,
		.TIM_ICSelection = TIM_ICSelection_DirectTI,
		.TIM_ICPrescaler = TIM_ICPSC_DIV1,
		.TIM_ICFilter = 0x7,
	};
	TIM_PWMIConfig(ZC_TIMER, &icconf);

	TIM_SelectInputTrigger(ZC_TIMER, TIM_TS_TI1FP1); /* Select the Input Trigger: TI1FP1 */
	TIM_SelectOutputTrigger(ZC_TIMER, TIM_TRGOSource_Reset);
	TIM_SelectSlaveMode(ZC_TIMER, TIM_SlaveMode_Reset); /* Select the slave Mode: Reset Mode */
	TIM_SelectMasterSlaveMode(ZC_TIMER, TIM_MasterSlaveMode_Enable); /* Enable the Master/Slave Mode */

	ZC_TIMER->CNT = 0;
	ZC_TIMER->ARR = 0xffff;

	/* Setup phase timer */
	freq = PHASE_GET_PCLK_FREQ(&clocks);
	TIM_PrescalerConfig(PHASE_TIMER, freq / BASE_FREQ - 1, TIM_PSCReloadMode_Immediate);
	TIM_SelectInputTrigger(PHASE_TIMER, PHASE_TRIGGER); /* connect to capture timer */
	TIM_SelectOutputTrigger(PHASE_TIMER, TIM_TRGOSource_Update);
	TIM_SelectSlaveMode(PHASE_TIMER, TIM_SlaveMode_Trigger);
	TIM_SelectMasterSlaveMode(PHASE_TIMER, TIM_MasterSlaveMode_Enable);
	TIM_SelectOnePulseMode(PHASE_TIMER, TIM_OPMode_Single);

	PHASE_TIMER->CNT = 0;
	PHASE_TIMER->ARR = 0;

	/* Setup PWM output */
	/* Give 1us resolution */
	freq = PWM_GET_PCLK_FREQ(&clocks);
	TIM_PrescalerConfig(PWM_TIMER, freq / BASE_FREQ - 1, TIM_PSCReloadMode_Immediate);

	/* PWM1 Mode configuration: Channel1 */
	TIM_OCInitTypeDef occonf = {
		.TIM_OCMode = TIM_OCMode_PWM1,
		.TIM_OutputState = TIM_OutputState_Enable,
		.TIM_Pulse = 0, /* disabled initially */
		.TIM_OCPolarity = TIM_OCPolarity_High,
	};
	PWM_TIMER_CHANNEL_INIT(&occonf);

	TIM_SelectInputTrigger(PWM_TIMER, PWM_TRIGGER); /* connect to phase timer */
	TIM_SelectSlaveMode(PWM_TIMER, TIM_SlaveMode_Reset); /* Select the slave Mode: Reset Mode */
	TIM_SelectMasterSlaveMode(PWM_TIMER, TIM_MasterSlaveMode_Enable); /* Enable the Master/Slave Mode */

	PWM_TIMER->CNT = 0;
	PWM_TIMER->ARR = 0xffff; /* initial value */

	/* Start ZC timer */
	ZC_TIMER->DIER |= TIM_IT_CC1;
	ZC_TIMER->SR = ~TIM_FLAG_CC1;
    ZC_TIMER->CR1 |= TIM_CR1_CEN;

	/* Start PWM timer */
    PWM_TIMER->CR1 |= TIM_CR1_CEN;
}

void dimmer_set(unsigned int val) {
	if(val >= MAX_VALUE) {
		/* always on */
		PWM_TIMER->PWM_TIMER_CHANNEL_REG = 0xffff;
	} else if(!val) {
		/* always off */
		PWM_TIMER->PWM_TIMER_CHANNEL_REG = 0;
	} else {
		PWM_TIMER->PWM_TIMER_CHANNEL_REG = TRIAC_TRIGGER_PULSE_US;
		dimmer_phase = acostab[val - 1];
	}
}

/*-----------------------------------------------------------------------------*/
void ZC_IRQ_HANDLER(void) {
	if(ZC_TIMER->SR & TIM_FLAG_CC1) {
		unsigned int half = ZC_TIMER->CCR1 >> 1;
		PWM_TIMER->ARR = half - 1; /* correct period */
		PHASE_TIMER->ARR = ((ZC_TIMER->CCR2 - half) >> 1) + ((dimmer_phase * half) >> 10);

		ZC_TIMER->SR = ~TIM_FLAG_CC1;
	}
}
