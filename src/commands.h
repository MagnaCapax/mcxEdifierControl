/* The edifierctl commands. Each returns the process exit code:
 * 0 success, 1 no reply or a link failure, 2 usage error or refused by the gate. */
#ifndef ED_COMMANDS_H
#define ED_COMMANDS_H

#include <stdbool.h>

#include "model.h"

#define ED_EXIT_OK 0
#define ED_EXIT_FAIL 1
#define ED_EXIT_USAGE 2

struct ed_opts {
	const struct ed_model *model;
	const char *address; /* NULL: discover by advertised UUID */
	int timeout_ms;      /* reply wait per query */
	bool verbose;        /* print payloads of unsolicited frames */
	bool force;          /* allow destructive opcodes in raw */
};

int ed_cmd_scan(const struct ed_opts *o);
int ed_cmd_info(const struct ed_opts *o);
int ed_cmd_get(const struct ed_opts *o, const char *name);
int ed_cmd_volume(const struct ed_opts *o, const char *arg);
int ed_cmd_input(const struct ed_opts *o, const char *source);
int ed_cmd_eco(const struct ed_opts *o, const char *onoff);
int ed_cmd_raw(const struct ed_opts *o, int argc, char **argv);
int ed_cmd_monitor(const struct ed_opts *o);

#endif
