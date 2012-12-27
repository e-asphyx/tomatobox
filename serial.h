#ifndef _SERIAL_H_
#define _SERIAL_H_

#define SERIAL_NUM 2

int serial_init(int n, unsigned int baudrate);
void serial_enabled(int n, int enabled);
int serial_rcv_char(int n, char *ch, unsigned long timeout);
int serial_send_char(int n, int ch, unsigned long timeout);
int serial_send_str(int n, const char *str, int length, unsigned long timeout);
int serial_iprintf(int n, unsigned long timeout, const char *format, ...)
	__attribute__ ((format (printf, 3, 4)));

#endif
