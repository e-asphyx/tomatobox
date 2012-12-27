/*-----------------------------------------------------------------------------*/
/* imported symbols */
/*-----------------------------------------------------------------------------*/

extern unsigned long _etext;
extern unsigned long _sidata;   /* start address for the initialization values of the .data section */
extern unsigned long _sdata;    /* start address for the .data section. defined in linker script */
extern unsigned long _edata;    /* end address for the .data section. defined in linker script */
extern unsigned long _sbss;     /* start address for the .bss section. defined in linker script */
extern unsigned long _ebss;     /* end address for the .bss section. defined in linker script */
extern unsigned long _estack;   /* init value for the stack pointer. defined in linker script */

extern int main(void);

/* Hardware initialization */
extern void SystemInit(void);

/* FreeRTOS handlers */
extern void xPortPendSVHandler(void);
extern void xPortSysTickHandler(void);
extern void vPortSVCHandler(void);

/*-----------------------------------------------------------------------------*/
/* function prototypes */
/*-----------------------------------------------------------------------------*/
#define __weak __attribute__((weak))

void Reset_Handler(void) __attribute__((__interrupt__));
void __default_exception_handler(void) __weak;

#define __default_handler __attribute__((weak, alias("__default_exception_handler")))

void NMIException(void) __default_handler;
void HardFaultException(void) __default_handler;
void MemManageException(void) __default_handler;
void BusFaultException(void) __default_handler;
void UsageFaultException(void) __default_handler;
void DebugMonitor(void) __default_handler;
void SVCHandler(void) __default_handler;
void PendSVC(void) __default_handler;
void SysTickHandler(void) __default_handler;
void WWDG_IRQHandler(void) __default_handler;
void PVD_IRQHandler(void) __default_handler;
void TAMPER_IRQHandler(void) __default_handler;
void RTC_IRQHandler(void) __default_handler;
void FLASH_IRQHandler(void) __default_handler;
void RCC_IRQHandler(void) __default_handler;
void EXTI0_IRQHandler(void) __default_handler;
void EXTI1_IRQHandler(void) __default_handler;
void EXTI2_IRQHandler(void) __default_handler;
void EXTI3_IRQHandler(void) __default_handler;
void EXTI4_IRQHandler(void) __default_handler;
void DMAChannel1_IRQHandler(void) __default_handler;
void DMAChannel2_IRQHandler(void) __default_handler;
void DMAChannel3_IRQHandler(void) __default_handler;
void DMAChannel4_IRQHandler(void) __default_handler;
void DMAChannel5_IRQHandler(void) __default_handler;
void DMAChannel6_IRQHandler(void) __default_handler;
void DMAChannel7_IRQHandler(void) __default_handler;
void ADC_IRQHandler(void) __default_handler;
void USB_HP_CAN_TX_IRQHandler(void) __default_handler;
void USB_LP_CAN_RX0_IRQHandler(void) __default_handler;
void CAN_RX1_IRQHandler(void) __default_handler;
void CAN_SCE_IRQHandler(void) __default_handler;
void EXTI9_5_IRQHandler(void) __default_handler;
void TIM1_BRK_IRQHandler(void) __default_handler;
void TIM1_UP_IRQHandler(void) __default_handler;
void TIM1_TRG_COM_IRQHandler(void) __default_handler;
void TIM1_CC_IRQHandler(void) __default_handler;
void TIM2_IRQHandler(void) __default_handler;
void TIM3_IRQHandler(void) __default_handler;
void TIM4_IRQHandler(void) __default_handler;
void I2C1_EV_IRQHandler(void) __default_handler;
void I2C1_ER_IRQHandler(void) __default_handler;
void I2C2_EV_IRQHandler(void) __default_handler;
void I2C2_ER_IRQHandler(void) __default_handler;
void SPI1_IRQHandler(void) __default_handler;
void SPI2_IRQHandler(void) __default_handler;
void USART1_IRQHandler(void) __default_handler;
void USART2_IRQHandler(void) __default_handler;
void USART3_IRQHandler(void) __default_handler;
void EXTI15_10_IRQHandler(void) __default_handler;
void RTCAlarm_IRQHandler(void) __default_handler;
void USBWakeUp_IRQHandler(void) __default_handler;

/*-----------------------------------------------------------------------------*/
/* Exception vectors */
/*-----------------------------------------------------------------------------*/

typedef void (*isr_vector_t) (void);
static const isr_vector_t g_pfnVectors[] __attribute__((section(".isr_vector"), used)) = {
	(isr_vector_t)&_estack,                 /* The initial stack pointer */
	Reset_Handler,                          /* The reset handler */
	NMIException,
	HardFaultException,
	MemManageException,
	BusFaultException,
	UsageFaultException,
	(void*)0, (void*)0, (void*)0, (void*)0, /* Reserved */
	vPortSVCHandler,
	DebugMonitor,
	(void*)0,                               /* Reserved */
	xPortPendSVHandler,
	xPortSysTickHandler,
	WWDG_IRQHandler,
	PVD_IRQHandler,
	TAMPER_IRQHandler,
	RTC_IRQHandler,
	FLASH_IRQHandler,
	RCC_IRQHandler,
	EXTI0_IRQHandler,
	EXTI1_IRQHandler,
	EXTI2_IRQHandler,
	EXTI3_IRQHandler,
	EXTI4_IRQHandler,
	DMAChannel1_IRQHandler,
	DMAChannel2_IRQHandler,
	DMAChannel3_IRQHandler,
	DMAChannel4_IRQHandler,
	DMAChannel5_IRQHandler,
	DMAChannel6_IRQHandler,
	DMAChannel7_IRQHandler,
	ADC_IRQHandler,
	USB_HP_CAN_TX_IRQHandler,
	USB_LP_CAN_RX0_IRQHandler,
	CAN_RX1_IRQHandler,
	CAN_SCE_IRQHandler,
	EXTI9_5_IRQHandler,
	TIM1_BRK_IRQHandler,
	TIM1_UP_IRQHandler,
	TIM1_TRG_COM_IRQHandler,
	TIM1_CC_IRQHandler,
	TIM2_IRQHandler,
	TIM3_IRQHandler,
	TIM4_IRQHandler,
	I2C1_EV_IRQHandler,
	I2C1_ER_IRQHandler,
	I2C2_EV_IRQHandler,
	I2C2_ER_IRQHandler,
	SPI1_IRQHandler,
	SPI2_IRQHandler,
	USART1_IRQHandler,
	USART2_IRQHandler,
	USART3_IRQHandler,
	EXTI15_10_IRQHandler,
	RTCAlarm_IRQHandler,
	USBWakeUp_IRQHandler,
	(void*)0,
	(void*)0,
	(void*)0,
	(void*)0,
	(void*)0,
	(void*)0,
	(void*)0,
	(void*)0xF108F85F /* this is a workaround for boot in RAM mode. */
};

void __default_exception_handler(void) {
	while(1);
}

void Reset_Handler(void) {
	unsigned long *pulSrc, *pulDest;

	/* Copy the data segment initializers from flash to SRAM. */
	pulSrc = &_sidata;
	for(pulDest = &_sdata; pulDest < &_edata;) *(pulDest++) = *(pulSrc++);

	/* Zero fill the bss segment. */
	for(pulDest = &_sbss; pulDest < &_ebss;) *(pulDest++) = 0;

	/* Initialize basic hardware (PLLs, clocks etc) */
	SystemInit();
	/* Call the application's entry point. */
	main();

	while(1);
}
