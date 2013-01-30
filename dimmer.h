#ifndef _DIMMER_H_
#define _DIMMER_H_

#define DIMMER_MAX 100
#define DIMMER_MIN 0

void dimmer_init();
void dimmer_set(unsigned int val); /* 0..100 */

#endif
