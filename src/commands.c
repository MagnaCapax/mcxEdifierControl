/* The commands. Arguments are checked before connecting, so a typo costs no
 * scan or connect; the session layer (session.c) does the link and the gate. */
#include "commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bluez.h"
#include "decode.h"
#include "session.h"

static int no_reply(const struct ed_cmd *c, int r)
{
	if (r == 0 && ed_link_stop)
		fprintf(stderr, "edifierctl: interrupted\n");
	else if (r == 0)
		fprintf(stderr, "edifierctl: no reply to %s (%02X %02X)\n", c->name, c->app, c->query);
	return ed_session_close(ED_EXIT_FAIL);
}

/* Writes a set opcode, then reads the value back and prints it. */
static int set_and_confirm(const struct ed_cmd *c, const uint8_t *payload, size_t len)
{
	size_t n;
	int r;

	if (ed_exchange(c->app, (uint8_t)c->set, payload, len, ED_NONE, NULL) < 0)
		return ed_session_close(ED_EXIT_FAIL);
	r = ed_query(c, &n);
	if (r != 1)
		return no_reply(c, r);
	printf("%s\n", ed_text);
	return ed_session_close(ED_EXIT_OK);
}

static void print_scan(const char *address, const char *name, int rssi, void *ctx)
{
	(void)ctx;
	printf("%s %4d dBm  %s\n", address, rssi, name);
	fflush(stdout);
}

int ed_cmd_scan(const struct ed_opts *o)
{
	char err[512];

	if (ed_scan(o->model, print_scan, NULL, err, sizeof(err)) < 0) {
		fprintf(stderr, "edifierctl: %s\n", err);
		return ED_EXIT_FAIL;
	}
	return ED_EXIT_OK;
}

int ed_cmd_info(const struct ed_opts *o)
{
	int answered = 0;

	if (ed_session_open(o) < 0)
		return ED_EXIT_FAIL;
	for (size_t i = 0; i < o->model->ncmds && !ed_link_stop; i++) {
		const struct ed_cmd *c = &o->model->cmds[i];
		size_t n;
		int r;

		if (c->query == ED_NONE)
			continue;
		r = ed_query(c, &n);
		if (r < 0)
			return ed_session_close(ED_EXIT_FAIL);
		printf("%-12s %s\n", c->name, r == 1 ? ed_text : "(no reply)");
		fflush(stdout);
		answered += r;
	}
	return ed_session_close(answered > 0 ? ED_EXIT_OK : ED_EXIT_FAIL);
}

int ed_cmd_get(const struct ed_opts *o, const char *name)
{
	const struct ed_cmd *c = ed_cmd_find(o->model, name);
	size_t n;
	int r;

	if (c == NULL || c->query == ED_NONE) {
		fprintf(stderr, "edifierctl: no query named '%s' (see edifierctl -h)\n", name);
		return ED_EXIT_USAGE;
	}
	if (ed_session_open(o) < 0)
		return ED_EXIT_FAIL;
	r = ed_query(c, &n);
	if (r != 1)
		return no_reply(c, r);
	printf("%s\n", ed_text);
	return ed_session_close(ED_EXIT_OK);
}

int ed_cmd_volume(const struct ed_opts *o, const char *arg)
{
	const struct ed_cmd *c = ed_cmd_find(o->model, "volume");
	unsigned max, before, target;
	uint8_t value;
	size_t n;
	int r;

	/* Syntax first (any max will do); the real clamp needs the speaker's max. */
	if (arg != NULL && !ed_volume_target(arg, 0, 255, &target)) {
		fprintf(stderr, "edifierctl: volume takes N, +N or -N\n");
		return ED_EXIT_USAGE;
	}
	if (ed_session_open(o) < 0)
		return ED_EXIT_FAIL;
	/* Every change starts from a fresh read: relative steps need the current
	 * value, and every target is clamped to the max the speaker reports. */
	r = ed_query(c, &n);
	if (r != 1)
		return no_reply(c, r);
	if (arg == NULL) {
		printf("%s\n", ed_text);
		return ed_session_close(ED_EXIT_OK);
	}
	if (n != 2) {
		fprintf(stderr, "edifierctl: unexpected volume reply: %s\n", ed_text);
		return ed_session_close(ED_EXIT_FAIL);
	}
	max = ed_reply[0];
	before = ed_reply[1];
	if (!ed_volume_target(arg, before, max, &target)) {
		fprintf(stderr, "edifierctl: the speaker reports a maximum volume of 0\n");
		return ed_session_close(ED_EXIT_FAIL);
	}
	value = (uint8_t)target;
	if (ed_exchange(c->app, (uint8_t)c->set, &value, 1, ED_NONE, NULL) < 0)
		return ed_session_close(ED_EXIT_FAIL);
	r = ed_query(c, &n);
	if (r != 1)
		return no_reply(c, r);
	printf("%u -> %s\n", before, ed_text);
	if (n != 2 || (unsigned)ed_reply[1] != target) {
		fprintf(stderr, "edifierctl: speaker reports %s, wanted %u\n", ed_text, target);
		return ed_session_close(ED_EXIT_FAIL);
	}
	return ed_session_close(ED_EXIT_OK);
}

int ed_cmd_input(const struct ed_opts *o, const char *source)
{
	const struct ed_cmd *c = ed_cmd_find(o->model, "input");
	uint8_t payload[2];
	char *end;
	unsigned long value = strtoul(source, &end, 10);
	size_t n;
	int r;

	/* Decimal, like the "group G source S" that get input prints. */
	if (*source < '0' || *source > '9' || *end != '\0' || value > 0xFF) {
		fprintf(stderr, "edifierctl: input SOURCE is a number 0-255\n");
		return ED_EXIT_USAGE;
	}
	payload[1] = (uint8_t)value;
	if (ed_session_open(o) < 0)
		return ED_EXIT_FAIL;
	/* Keep the group the speaker reports: [group, source]. */
	r = ed_query(c, &n);
	if (r != 1)
		return no_reply(c, r);
	if (n != 2) {
		fprintf(stderr, "edifierctl: unexpected input reply: %s\n", ed_text);
		return ed_session_close(ED_EXIT_FAIL);
	}
	payload[0] = ed_reply[0];
	return set_and_confirm(c, payload, 2);
}

int ed_cmd_eco(const struct ed_opts *o, const char *onoff)
{
	uint8_t value;

	if (strcmp(onoff, "on") == 0)
		value = 1;
	else if (strcmp(onoff, "off") == 0)
		value = 0;
	else {
		fprintf(stderr, "edifierctl: eco takes on or off\n");
		return ED_EXIT_USAGE;
	}
	if (ed_session_open(o) < 0)
		return ED_EXIT_FAIL;
	return set_and_confirm(ed_cmd_find(o->model, "eco"), &value, 1);
}

int ed_cmd_raw(const struct ed_opts *o, int argc, char **argv)
{
	static uint8_t payload[ED_PAYLOAD_MAX];
	uint8_t app, cmd;
	enum ed_gate g;
	long len = 0;
	size_t n;
	int r;

	if (argc < 2 || argc > 3 || !ed_parse_byte(argv[0], &app) || !ed_parse_byte(argv[1], &cmd) ||
	    (argc == 3 && (len = ed_parse_hex(argv[2], payload, sizeof(payload))) < 0)) {
		fprintf(stderr, "edifierctl: raw APP CMD [HEX], e.g. raw ec 66\n");
		return ED_EXIT_USAGE;
	}
	/* Refuse before connecting, so a refused opcode costs nothing. */
	if (ed_raw_reserved(o->model, app, cmd)) {
		fprintf(stderr, "edifierctl: refused %02X %02X: use the volume command (it reads first and clamps)\n",
			app, cmd);
		return ED_EXIT_USAGE;
	}
	g = ed_gate_check(o->model, app, cmd);
	if (!ed_gate_allows(g, o->force)) {
		fprintf(stderr, "edifierctl: refused %02X %02X: %s\n", app, cmd, ed_gate_reason(g));
		return ED_EXIT_USAGE;
	}
	if (ed_session_open(o) < 0)
		return ED_EXIT_FAIL;
	r = ed_exchange(app, cmd, payload, (size_t)len, cmd, &n);
	if (r < 0)
		return ed_session_close(ED_EXIT_FAIL);
	if (r == 0) {
		fprintf(stderr, "edifierctl: frame sent, no reply with opcode %02X\n", cmd);
		return ed_session_close(ED_EXIT_FAIL);
	}
	ed_hex(ed_reply, n, ed_text, 3 * ED_PAYLOAD_MAX + 1);
	printf("%s\n", ed_text);
	return ed_session_close(ED_EXIT_OK);
}

int ed_cmd_monitor(const struct ed_opts *o)
{
	if (ed_session_open(o) < 0)
		return ED_EXIT_FAIL;
	return ed_session_close(ed_session_monitor());
}
