#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "serial.h"
#include "readline.h"

#define ISCNTRL(x) ((x) < 0x20)

#define HISTORY_BUF_MASK (HISTORY_BUF_SZ - 1)

#define BUF_USED(x) ((x)->w_idx - (x)->r_idx)
#define BUF_FREE(x) (HISTORY_BUF_SZ - BUF_USED(x))

#define BUF_PUSH(x, c) do {(x)->buf[((x)->w_idx++) & HISTORY_BUF_MASK] = c;} while(0)
#define BUF_AT(x, i) ((x)->buf[(i) & HISTORY_BUF_MASK])

static bool history_prev(const history_t *buf, unsigned int *cursor) {
	if(*cursor == buf->r_idx) return false;

	(*cursor)--; /* skip \0 */
	while(*cursor != buf->r_idx && BUF_AT(buf, *cursor - 1) != '\0') (*cursor)--; /* skip current word */

	return true;
}

static bool history_next(const history_t *buf, unsigned int *cursor) {
	if(*cursor == buf->w_idx) return false;

	while(BUF_AT(buf, *cursor) != '\0') (*cursor)++; /* skip current word */
	(*cursor)++; /* skip \0 */

	return (*cursor != buf->w_idx);
}

static int history_get(char *dest, int sz, const history_t *his, unsigned int idx) {
	int i = 0;
	while(i < sz && BUF_AT(his, idx)) {
		*(dest++) = BUF_AT(his, idx++);
		i++;
	}

	return i;
}

static int history_strcmp(const history_t *his, unsigned int idx, const char *s) {
	while(BUF_AT(his, idx) != '\0' && BUF_AT(his, idx) == *s) {
      idx++;
      s++;
	}
	return (unsigned char)BUF_AT(his, idx) - (unsigned char)*s;
}

static void history_add(history_t *buf, const char *str, unsigned int *cursor) {
	int len = strlen(str);

	if(len + 1 > HISTORY_BUF_SZ) {
		*cursor = buf->w_idx;
		return;
	}

	while(BUF_FREE(buf) < len + 1) {
		/* remove first word */
		while(BUF_AT(buf, buf->r_idx++) != '\0');
	}

	/* push new word */
	while(*str != '\0') BUF_PUSH(buf, *(str++));
	BUF_PUSH(buf, '\0');

	*cursor = buf->w_idx;
}

int read_line(int sern, char *buf, int size, history_t *his, const char *prompt) {
	serial_send_str(sern, prompt, -1, portMAX_DELAY);

	unsigned int cursor = his->w_idx;
	bool ret = false;
	int esc = 0;
	int cnt = 0;

	while(!ret) {
		char c;
		/* wait for next char */
		serial_rcv_char(sern, &c, portMAX_DELAY);

		if(esc > 1) {
			switch(c) {
				case 'A': /* up */
					if(history_prev(his, &cursor)) {
						if(cnt) serial_iprintf(sern, portMAX_DELAY, "\x1b[%dD\x1b[K", cnt);

						cnt = history_get(buf, size - 1, his, cursor);
						serial_send_str(sern, buf, cnt, portMAX_DELAY);
					}
					break;

				case 'B': /* down */
					if(cnt && cursor != his->w_idx) {
						serial_iprintf(sern, portMAX_DELAY, "\x1b[%dD\x1b[K", cnt);
						cnt = 0;
					}

					if(history_next(his, &cursor)) {
						cnt = history_get(buf, size - 1, his, cursor);
						serial_send_str(sern, buf, cnt, portMAX_DELAY);
					}
					break;
			}
			esc = 0;
		} else if(esc > 0) {
			if(c == '[') {
				esc++;
			} else {
				esc = 0;
			}

		} else {
			/* regular mode */
			switch(c) {
				case '\r': /* cr */
				case '\n': /* lf */
					serial_send_str(sern, "\r\n", -1, portMAX_DELAY);
					buf[cnt++] = '\0';

					/* add to history */
					if(cnt > 1) {
						/* check for duplicate */
						unsigned int tmp = his->w_idx;
						if(!history_prev(his, &tmp) || history_strcmp(his, tmp, buf) != 0) {
							history_add(his, buf, &cursor);
						}
					}

					ret = true;
					break;

				case '\b': /* backspace */
				case '\x7f': /* del */
					if(cnt) {
						serial_send_str(sern, "\b\x1b[K", -1, portMAX_DELAY);
						cnt--;
					}
					break;

				case '\x15': /* ctrl-u */
					if(cnt) {
						serial_iprintf(sern, portMAX_DELAY, "\x1b[%dD\x1b[K", cnt);
						cnt = 0;
					}
					break;

				case '\x10': /* ctrl-p */
					if(history_prev(his, &cursor)) {
						if(cnt) serial_iprintf(sern, portMAX_DELAY, "\x1b[%dD\x1b[K", cnt);

						cnt = history_get(buf, size - 1, his, cursor);
						serial_send_str(sern, buf, cnt, portMAX_DELAY);
					}
					break;

				case '\x0e': /* ctrl-n */
					if(cnt && cursor != his->w_idx) {
						serial_iprintf(sern, portMAX_DELAY, "\x1b[%dD\x1b[K", cnt);
						cnt = 0;
					}

					if(history_next(his, &cursor)) {
						cnt = history_get(buf, size - 1, his, cursor);
						serial_send_str(sern, buf, cnt, portMAX_DELAY);
					}
					break;

				case '\x1b': /* esc */
					esc = 1;
					break;

				default:
					if(cnt < size - 1 && !ISCNTRL(c)) {
						buf[cnt++] = c;
						serial_send_char(sern, c, portMAX_DELAY);
					}
			}
		}
	}

	return cnt;
}

