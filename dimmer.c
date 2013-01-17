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
/* Use TIM2_CH4 (remapped to PB11) for PWM */
#define PWM_GPIO GPIOB
#define PWM_PIN GPIO_Pin_11
#define PWM_CLK_ENABLE \
do { \
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE); \
	RCC_APB2PeriphClockCmd((RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO), ENABLE); \
} while(0)
#define PWM_TIMER TIM2 /* Uses APB1 clock */
#define PWM_TIMER_CHANNEL_INIT(x) TIM_OC4Init(PWM_TIMER, (x))
#define PWM_TIMER_CHANNEL_REG CCR4
#define PWM_GPIO_REMAP GPIO_FullRemap_TIM2
#define PWM_GET_PCLK_FREQ(x) (((RCC->CFGR >> 8) & 0x7) >= 4 ? (x)->PCLK1_Frequency * 2 : (x)->PCLK1_Frequency)
#define PWM_TRIGGER TIM_TS_ITR3 /* <- TIM4 */

#define BASE_FREQ 1000000
#define MAX_VALUE 255
#define TRIAC_TRIGGER_PULSE_US 50

typedef struct _pwm_capture_t pwm_capture_t;
struct _pwm_capture_t {
	unsigned int period;
	unsigned int high;
};
/*-----------------------------------------------------------------------------*/
static volatile unsigned int dimmer_phase = 0;
static xSemaphoreHandle irq_sem;
static volatile pwm_capture_t pwm_data;

static const uint8_t acostab[MAX_VALUE + 1] = {
	255, 245, 241, 237, 235, 232, 230, 228, 226, 224, 223, 221, 220, 218, 217, 215,
	214, 213, 211, 210, 209, 208, 207, 205, 204, 203, 202, 201, 200, 199, 198, 197,
	196, 195, 194, 193, 192, 192, 191, 190, 189, 188, 187, 186, 185, 185, 184, 183,
	182, 181, 181, 180, 179, 178, 177, 177, 176, 175, 174, 174, 173, 172, 171, 171,
	170, 169, 168, 168, 167, 166, 165, 165, 164, 163, 163, 162, 161, 161, 160, 159,
	158, 158, 157, 156, 156, 155, 154, 154, 153, 152, 152, 151, 150, 150, 149, 148,
	148, 147, 146, 146, 145, 144, 144, 143, 143, 142, 141, 141, 140, 139, 139, 138,
	137, 137, 136, 135, 135, 134, 134, 133, 132, 132, 131, 130, 130, 129, 128, 128,
	127, 127, 126, 125, 125, 124, 123, 123, 122, 121, 121, 120, 120, 119, 118, 118,
	117, 116, 116, 115, 114, 114, 113, 112, 112, 111, 111, 110, 109, 109, 108, 107,
	107, 106, 105, 105, 104, 103, 103, 102, 101, 101, 100, 99, 99, 98, 97, 97,
	96, 95, 94, 94, 93, 92, 92, 91, 90, 90, 89, 88, 87, 87, 86, 85,
	84, 84, 83, 82, 81, 81, 80, 79, 78, 78, 77, 76, 75, 74, 74, 73,
	72, 71, 70, 70, 69, 68, 67, 66, 65, 64, 63, 63, 62, 61, 60, 59,
	58, 57, 56, 55, 54, 53, 52, 51, 50, 48, 47, 46, 45, 44, 42, 41,
	40, 38, 37, 35, 34, 32, 31, 29, 27, 25, 23, 20, 18, 14, 10, 0
};
/*-----------------------------------------------------------------------------*/
int dimmer_init() {
	vSemaphoreCreateBinary(irq_sem);
	if(irq_sem == NULL) return -1;
	xSemaphoreTake(irq_sem, 0);

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

	return 0;
}

int dimmer_read(unsigned int *period, unsigned int *high) {
	xSemaphoreTake(irq_sem, 0);

	if(xSemaphoreTake(irq_sem, 100 / portTICK_RATE_MS)) {
		*period = pwm_data.period;
		*high = pwm_data.high;

		return 0;
	}

	return -1;
}

void dimmer_set(unsigned int val) {
	if(val >= 255) {
		/* always on */
		PWM_TIMER->PWM_TIMER_CHANNEL_REG = 0xffff;
	} else if(!val) {
		/* always off */
		PWM_TIMER->PWM_TIMER_CHANNEL_REG = 0;
	} else {
		PWM_TIMER->PWM_TIMER_CHANNEL_REG = TRIAC_TRIGGER_PULSE_US;
		dimmer_phase = acostab[val & 0xff];
	}
}

/*-----------------------------------------------------------------------------*/
void ZC_IRQ_HANDLER(void) {
	portBASE_TYPE preempt = pdFALSE;

	if(ZC_TIMER->SR & TIM_FLAG_CC1) {
		unsigned int high = pwm_data.high = ZC_TIMER->CCR2;
		unsigned int period = pwm_data.period = ZC_TIMER->CCR1;
		unsigned int half = period >> 1;

		PWM_TIMER->ARR = half - 1; /* correct period */
		PHASE_TIMER->ARR = ((high - half) >> 1) + ((dimmer_phase * half) >> 8);

		ZC_TIMER->SR = ~TIM_FLAG_CC1;

		xSemaphoreGiveFromISR(irq_sem, &preempt);
	}

	portEND_SWITCHING_ISR(preempt);
}
