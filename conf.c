#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "stm32f10x.h"
#include "conf.h"

extern char _eimage; /* from linker */

#if defined(STM32F10X_HD) || defined(STM32F10X_HD_VL) || defined(STM32F10X_CL) || defined(STM32F10X_XL)
	#define FLASH_PAGE_SIZE 0x800
#else
	#define FLASH_PAGE_SIZE 0x400
#endif

#if defined(STM32F10X_LD) || defined(STM32F10X_LD_VL)
	#define FLASH_SIZE 0x008000
#elif defined(STM32F10X_MD) || defined(STM32F10X_MD_VL)
	#define FLASH_SIZE 0x020000
#elif defined(STM32F10X_CL)
	#define FLASH_SIZE 0x040000
#elif defined(STM32F10X_HD) || defined(STM32F10X_HD_VL)
	#define FLASH_SIZE 0x080000
#elif defined(STM32F10X_XL)
	#define FLASH_SIZE 0x100000
#endif

#define CONF_AREA_START_ADDR (((unsigned long)&_eimage + FLASH_PAGE_SIZE - 1) & ~((unsigned long)FLASH_PAGE_SIZE - 1))
#define CONF_AREA_SIZE (FLASH_SIZE + FLASH_BASE - CONF_AREA_START_ADDR)
#define FLASH_END (FLASH_BASE + FLASH_SIZE)

#define CONF_MAGIC 0xAA55AA55

typedef struct _conf_img_t {
	uint32_t magic;
	sys_conf_data_t data;
	uint32_t crc;
} conf_img_t __attribute__((aligned(4)));

/* default config */
volatile sys_conf_data_t conf_data = {
	.light_mode = LIGHT_OFF,
};

static unsigned long conf_address = 0;

static uint32_t calc_crc32(uint32_t *data, unsigned int size) {
	CRC->CR = CRC_CR_RESET;
	while(size--) CRC->DR = *(data++);
	return CRC->DR;
}

void conf_init() {
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_CRC, ENABLE);

	unsigned long addr = CONF_AREA_START_ADDR;
	conf_img_t *found = NULL;

	/* find last valid block */
	while(addr < FLASH_END) {
		conf_img_t *img = (conf_img_t*)addr;

		/* find first free block */
		int cnt = FLASH_PAGE_SIZE / sizeof(conf_img_t);
		while(cnt && img->magic != 0xffffffff) {
			found = img++;
			cnt--;
		};
		if(cnt) break;

		addr += FLASH_PAGE_SIZE;
	}

	if(found && found->magic == CONF_MAGIC) {
		/* verify checksum */
		uint32_t crc = calc_crc32((uint32_t*)&found->data, sizeof(sys_conf_data_t) / 4);
		if(crc == found->crc) {
			conf_data = found->data;
			conf_address = (unsigned long)found;
		}
	}
}

int conf_write() {
	conf_img_t img;
	img.magic = CONF_MAGIC;
	img.data = conf_data;

	/* compute checksum */
	img.crc = calc_crc32((uint32_t*)&img.data, sizeof(sys_conf_data_t) / 4);

	unsigned long page_addr = CONF_AREA_START_ADDR;
	unsigned long erase_page_addr = 0; /* don't erase */
	int idx = 0;

	if(conf_address) {
		page_addr = conf_address & ~((unsigned long)FLASH_PAGE_SIZE - 1);
		idx = (conf_address - page_addr) / sizeof(conf_img_t) + 1;

		/* next page */
		if(idx == FLASH_PAGE_SIZE / sizeof(conf_img_t)) {
			idx = 0;
			page_addr += FLASH_PAGE_SIZE;
			if(page_addr == FLASH_END) page_addr = CONF_AREA_START_ADDR;
		} else if(idx == FLASH_PAGE_SIZE / sizeof(conf_img_t) - 1) {
			/* erase next page */
			erase_page_addr = page_addr + FLASH_PAGE_SIZE;
			if(erase_page_addr == FLASH_END) erase_page_addr = CONF_AREA_START_ADDR;
		}
	} else {
		erase_page_addr = CONF_AREA_START_ADDR;
	}

	conf_address = page_addr + idx * sizeof(conf_img_t);

	/* perform write */
	FLASH_Unlock();

	FLASH_Status status = FLASH_COMPLETE;
	if(erase_page_addr) status = FLASH_ErasePage(erase_page_addr);

	uint32_t *ptr32 = (uint32_t*)&img;
	unsigned long addr = conf_address;
	unsigned int cnt = sizeof(conf_img_t) >> 2;

	while(status == FLASH_COMPLETE && cnt) {
		status = FLASH_ProgramWord(addr, *(ptr32++));
		addr += 4;
		cnt--;
	}

	FLASH_Lock();

	/* verify */
	if(memcmp((void*)conf_address, &img, sizeof(conf_img_t))) status = FLASH_ERROR_PG;

	return (status == FLASH_COMPLETE) ? 0 : -1;
}
