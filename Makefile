##########################################################
CROSS_COMPILE = arm-none-eabi-

# ST sources
SOURCES = \
		st_lib/system_stm32f10x.c \
		st_lib/core_cm3.c \
		st_lib/stm32f10x_rcc.c \
		st_lib/stm32f10x_gpio.c \
		st_lib/stm32f10x_usart.c \
		st_lib/stm32f10x_pwr.c \
		st_lib/stm32f10x_rtc.c \
		st_lib/stm32f10x_tim.c \
		st_lib/stm32f10x_flash.c \
		st_lib/misc.c

# FreeRTOS sources
SOURCES += \
		freertos/tasks.c \
		freertos/queue.c \
		freertos/timers.c \
		freertos/list.c \
		freertos/heap_2.c \
		freertos/port/port.c

# Application sources
SOURCES += \
		crt0.c \
		serial.c \
		rtc.c \
		malloc.c \
		readline.c \
		cmd.c \
		am2302.c \
		gpio.c \
		conf.c \
		dimmer.c \
		main.c

INCLUDE = \
		-I./ \
		-I./st_lib \
		-I./freertos/include \
		-I./freertos/port

DEFS = \
		-DSTM32F10X_MD \
		-DUSE_STDPERIPH_DRIVER

BIN = firmware.elf

OBJCOPY = $(CROSS_COMPILE)objcopy
TTY = /dev/ttyUSB0
ARCH = -mcpu=cortex-m3 -mthumb

firmware.bin: $(BIN)
	$(OBJCOPY) -S -O binary $< $@

flash: firmware.bin
	stm32flash -v -w $< $(TTY)

firmware.elf_CFLAGS := -g -Wall -fno-common -ffunction-sections -O2 -std=c99 $(ARCH) $(INCLUDE) $(DEFS)
firmware.elf_LDFLAGS := -Tstm32_flash.ld -nostartfiles -Wl,--cref,--gc-sections,-Map=firmware.map $(ARCH)

##########################################################

include common.mk
