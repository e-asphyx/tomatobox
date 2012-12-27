#include <string.h>
#include "FreeRTOS.h"
#include "serial.h"
#include "cmd.h"

#define ISBLANK(x) ((x) == ' ' || (x) == '\t')
#define MAX_ARGS 8

int cmd_exec(int sern, const cmd_handler_t *handlers, char *cmd) {
	/* parse command line */
	while(ISBLANK(*cmd)) cmd++;
	if(!*cmd) return -1;

	char *start = cmd;
	while(*cmd && !ISBLANK(*cmd)) cmd++;
	if(*cmd) *(cmd++) = '\0';

	const cmd_handler_t *hnd;
	for(hnd = handlers; hnd->type != CMD_END; hnd++) {
		if(strcmp(start, hnd->cmd) == 0) {
			if(hnd->type == CMD_PROC) {
				/* split args */
				char *args[MAX_ARGS];
				int cnt = 0;

				while(ISBLANK(*cmd)) cmd++;
				while(*cmd && cnt < MAX_ARGS) {
					args[cnt++] = cmd;

					while(*cmd && !ISBLANK(*cmd)) cmd++;
					if(*cmd) *(cmd++) = '\0';

					while(ISBLANK(*cmd)) cmd++;
				}

				if(hnd->h.proc != NULL) return hnd->h.proc(sern, cnt, args);
			} else {
				/* subcommand */
				return cmd_exec(sern, hnd->h.sub, cmd);
			}
		}
	}

	serial_send_str(sern, "\r\nAvailable commands:\r\n", -1, portMAX_DELAY);
	for(hnd = handlers; hnd->type != CMD_END; hnd++) {
		serial_iprintf(sern, portMAX_DELAY, "%s\r\n", hnd->cmd);
	}
	serial_send_str(sern, "\r\n", -1, portMAX_DELAY);

	return -1;
}
