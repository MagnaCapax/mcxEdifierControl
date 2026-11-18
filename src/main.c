/* edifierctl: control an Edifier R2750DB MKII speaker over Bluetooth LE.
 * Options come before the command, so "volume -1" is never read as an option. */
#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bluez.h"
#include "commands.h"

#define ED_VERSION "0.1.0"

static void usage(FILE *to, const struct ed_model *m)
{
	fprintf(to,
		"usage: edifierctl [-a ADDRESS] [-t SECONDS] [-v] [-f] COMMAND [ARGS]\n"
		"\n"
		"Controls the %s over Bluetooth LE through BlueZ.\n"
		"Command layouts come from analysis of the official app; see docs/PROTOCOL.md\n"
		"for which ones are verified on hardware.\n"
		"\n"
		"commands:\n"
		"  scan                 list speakers advertising the control service (30 s)\n"
		"  info                 run every read-only query, one line each\n"
		"  get NAME             run one query (names below)\n"
		"  volume [N|+N|-N]     read, or set: reads first, clamps to the reported max\n"
		"  input SOURCE         set the input source (0-255), keeping the reported group\n"
		"  eco on|off           ECO / power-saving mode\n"
		"  raw APP CMD [HEX]    send one frame; opcodes outside the table are refused,\n"
		"                       destructive ones need -f, firmware/bulk ones never go\n"
		"  monitor              stay connected and print every frame the speaker sends\n"
		"\n"
		"options:\n"
		"  -a ADDRESS   the speaker's BLE address (default: find it by its UUID)\n"
		"  -t SECONDS   reply wait per query (default 3)\n"
		"  -v           also print payloads of unsolicited frames (may include\n"
		"               the title and artist of what is playing)\n"
		"  -f           allow destructive opcodes in raw\n"
		"  -h, -V       help, version\n"
		"\n"
		"names:",
		m->name);
	for (size_t i = 0; i < m->ncmds; i++)
		if (m->cmds[i].query != ED_NONE)
			fprintf(to, " %s", m->cmds[i].name);
	fputs("\n\nexit status: 0 ok, 1 no reply or link failure, 2 usage error or refused\n", to);
}

static void on_signal(int sig)
{
	(void)sig;
	ed_link_stop = 1;
}

static bool valid_address(const char *s)
{
	if (strlen(s) != 17)
		return false;
	for (int i = 0; i < 17; i++)
		if (i % 3 == 2 ? s[i] != ':' : !isxdigit((unsigned char)s[i]))
			return false;
	return true;
}

/* Parses the options before COMMAND into o. Returns argv index of COMMAND,
 * 0 when the program should exit with *rc. */
static int parse_options(int argc, char **argv, struct ed_opts *o, int *rc)
{
	int i = 1;

	for (; i < argc && argv[i][0] == '-'; i++) {
		const char *a = argv[i];

		if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
			usage(stdout, o->model);
			*rc = ED_EXIT_OK;
			return 0;
		} else if (strcmp(a, "-V") == 0 || strcmp(a, "--version") == 0) {
			printf("edifierctl %s\n", ED_VERSION);
			*rc = ED_EXIT_OK;
			return 0;
		} else if (strcmp(a, "-v") == 0) {
			o->verbose = true;
		} else if (strcmp(a, "-f") == 0) {
			o->force = true;
		} else if (strcmp(a, "-a") == 0 && i + 1 < argc && valid_address(argv[i + 1])) {
			o->address = argv[++i];
		} else if (strcmp(a, "-t") == 0 && i + 1 < argc) {
			char *end;
			long s = strtol(argv[++i], &end, 10);

			/* Upper bound only keeps the millisecond count inside an int. */
			if (*end != '\0' || s < 1 || s > 3600)
				break;
			o->timeout_ms = (int)s * 1000;
		} else {
			break;
		}
	}
	if (i >= argc || argv[i][0] == '-') {
		usage(stderr, o->model);
		*rc = ED_EXIT_USAGE;
		return 0;
	}
	return i;
}

int main(int argc, char **argv)
{
	struct ed_opts o = {.model = ed_model_default(), .timeout_ms = 3000};
	struct sigaction sa;
	int rc = ED_EXIT_USAGE;
	int at = parse_options(argc, argv, &o, &rc);
	const char *cmd;
	int nargs;

	if (at == 0)
		return rc;
	cmd = argv[at];
	nargs = argc - at - 1;
	/* No SA_RESTART: a signal ends the current wait, then the link is closed cleanly. */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	if (strcmp(cmd, "scan") == 0 && nargs == 0)
		return ed_cmd_scan(&o);
	if (strcmp(cmd, "info") == 0 && nargs == 0)
		return ed_cmd_info(&o);
	if (strcmp(cmd, "get") == 0 && nargs == 1)
		return ed_cmd_get(&o, argv[at + 1]);
	if (strcmp(cmd, "volume") == 0 && nargs <= 1)
		return ed_cmd_volume(&o, nargs ? argv[at + 1] : NULL);
	if (strcmp(cmd, "input") == 0 && nargs == 1)
		return ed_cmd_input(&o, argv[at + 1]);
	if (strcmp(cmd, "eco") == 0 && nargs == 1)
		return ed_cmd_eco(&o, argv[at + 1]);
	if (strcmp(cmd, "raw") == 0)
		return ed_cmd_raw(&o, nargs, argv + at + 1);
	if (strcmp(cmd, "monitor") == 0 && nargs == 0)
		return ed_cmd_monitor(&o);
	usage(stderr, o.model);
	return ED_EXIT_USAGE;
}
