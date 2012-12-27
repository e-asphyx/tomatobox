#ifndef _CMD_H_
#define _CMD_H_

typedef enum {
	CMD_END,
	CMD_PROC,
	CMD_SUB,
} handler_type_t;

typedef int (*cmd_handler_proc_t)(int sern, int argc, char **argv);
typedef struct _cmd_handler_t cmd_handler_t;

struct _cmd_handler_t {
	handler_type_t type;
	const char *cmd;
	union {
		cmd_handler_proc_t proc;
		const struct _cmd_handler_t *sub;
	} h;
};

int cmd_exec(int sern, const cmd_handler_t *handlers, char *cmd);

#endif
