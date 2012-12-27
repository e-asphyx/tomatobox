#ifndef _READLINE_H_
#define _READLINE_H_

#define HISTORY_BUF_SZ 256

typedef struct _history_t {
	unsigned int r_idx;
	unsigned int w_idx;
	char buf[HISTORY_BUF_SZ];
} history_t;

int read_line(int sern, char *buf, int size, history_t *his, const char *prompt);

#endif
