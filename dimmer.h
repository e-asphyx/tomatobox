#ifndef _DIMMER_H_
#define _DIMMER_H_

int dimmer_init();
int dimmer_read(unsigned int *period, unsigned int *high);
void dimmer_set(unsigned int val);

#endif
