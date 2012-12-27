#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"

/* allocation routines for newlib */

typedef union _mem_block_t {
	size_t size;
	unsigned char _align[portBYTE_ALIGNMENT];
} mem_block_t;

_PTR _realloc_r(struct _reent *re, _PTR old, size_t size) {
	void *new = _malloc_r(re, size);
	if(new == NULL) return NULL;

	size_t copy_size = ((mem_block_t*)old - 1)->size;
	if(copy_size > size) copy_size = size;

	memcpy(new, old, copy_size);
	_free_r(re, old);

	return new;
}

_PTR _calloc_r(struct _reent *re, size_t num, size_t size) {
	size *= num;
	void *ret = _malloc_r(re, size);
	if(ret) memset(ret, 0, size);
	return ret;
}

_PTR _malloc_r(struct _reent *re, size_t size) {
	mem_block_t *ptr = pvPortMalloc(size + sizeof(mem_block_t));
	if(ptr == NULL) return NULL;

	ptr->size = size;
	return (void*)(ptr + 1);
}

_VOID _free_r(struct _reent *re, _PTR ptr) {
	vPortFree((void*)((mem_block_t*)ptr - 1));
}

