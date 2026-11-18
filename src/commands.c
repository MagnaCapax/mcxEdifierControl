/* Command implementations: one BLE session per run, every outgoing frame
 * through the gate, every speaker frame either answers the query or is
 * logged as unsolicited. Payloads of unsolicited frames are shown only with
 * -v: cmd 01/02 carry the user's now-playing titles. */
#include "commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bluez.h"
#include "decode.h"
#include "frame.h"

struct session {
	const struct ed_opts *o;
	struct ed_link *link;
	struct ed_parser parser;
};

/* Both are large (a frame can carry 64 KiB): static, one session per process. */
static struct session sess;
static char text[3 * ED_PAYLOAD_MAX + 1];

static const char *op_name(const struct ed_model *m, uint8_t app, uint8_t cmd)
{
	const struct ed_cmd *c = ed_cmd_by_opcode(m, app, cmd);

	return c ? c->name : "unknown";
}

static void print_frame(FILE *to, const char *tag, const struct ed_frame *f)
{
	fprintf(to, "%s %02X %02X len %u %s", tag, f->app, f->cmd, f->len,
		op_name(sess.o->model, f->app, f->cmd));
	if (sess.o->verbose && f->len > 0) {
		ed_hex(f->payload, f->len, text, sizeof(text));
		fprintf(to, " : %s", text);
	}
	fputc('\n', to);
}

static int session_open(const struct ed_opts *o)
{
	char err[512];

	sess.o = o;
	ed_parser_init(&sess.parser);
	sess.link = ed_link_open(o->model, o->address, err, sizeof(err));
	if (sess.link == NULL) {
		fprintf(stderr, "edifierctl: %s\n", err);
		return -1;
	}
	return 0;
}

static int session_close(int rc)
{
	if (sess.parser.skipped || sess.parser.bad_sum)
		fprintf(stderr, "edifierctl: dropped %zu stray bytes, %zu frames with a bad checksum\n",
			sess.parser.skipped, sess.parser.bad_sum);
	ed_link_close(sess.link);
	sess.link = NULL;
	return rc;
}

/* Sends one frame through the gate. If expect is not ED_NONE, waits for the
 * speaker frame with that opcode and copies its payload to reply.
 * Returns 1 replied (or sent, when not waiting), 0 no reply, -1 refused or link failure. */
static int exchange(uint8_t app, uint8_t cmd, const uint8_t *payload, size_t len, int expect,
		    uint8_t *reply, size_t *reply_len)
{
	static uint8_t out[ED_FRAME_OVERHEAD + ED_PAYLOAD_MAX];
	uint8_t chunk[1024]; /* > largest notification (ATT MTU 517 - 3) */
	enum ed_gate g = ed_gate_check(sess.o->model, app, cmd);
	size_t n = ed_frame_build(app, cmd, payload, len, out, sizeof(out));
	struct timespec t0;

	if (!ed_gate_allows(g, sess.o->force)) {
		fprintf(stderr, "edifierctl: refused %02X %02X: %s\n", app, cmd, ed_gate_reason(g));
		return -1;
	}
	if (n == 0 || ed_link_write(sess.link, out, n) < 0) {
		fprintf(stderr, "edifierctl: write of %02X %02X failed (%zu bytes, max %zu)\n", app, cmd,
			n, ed_link_max_write(sess.link));
		return -1;
	}
	if (expect == ED_NONE)
		return 1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (;;) {
		struct timespec t;
		struct ed_frame f;
		long left;
		ssize_t got;

		clock_gettime(CLOCK_MONOTONIC, &t);
		left = sess.o->timeout_ms - ((long)(t.tv_sec - t0.tv_sec) * 1000 +
					     (t.tv_nsec - t0.tv_nsec) / 1000000);
		if (left <= 0 || ed_link_stop)
			return 0;
		got = ed_link_read(sess.link, chunk, sizeof(chunk), (int)left);
		if (got < 0) {
			fprintf(stderr, "edifierctl: link lost\n");
			return -1;
		}
		ed_parser_feed(&sess.parser, chunk, (size_t)got);
		while (ed_parser_next(&sess.parser, &f)) {
			if (f.app == app && f.cmd == expect) {
				memcpy(reply, f.payload, f.len);
				*reply_len = f.len;
				return 1;
			}
			print_frame(stderr, "unsolicited", &f);
		}
	}
}

/* Runs one query; on a reply, prints and returns its decoded text in text[]. */
static int query(const struct ed_cmd *c, uint8_t *reply, size_t *len)
{
	int r = exchange(c->app, (uint8_t)c->query, NULL, 0, c->query, reply, len);

	if (r == 1) {
		struct ed_frame f = {ED_HDR_REPLY, c->app, (uint8_t)c->query, (uint16_t)*len, reply};

		ed_decode(&f, text, sizeof(text));
	}
	return r;
}

static uint8_t reply_buf[ED_PAYLOAD_MAX];

static int fail_no_reply(const struct ed_cmd *c, int r)
{
	if (r == 0)
		fprintf(stderr, "edifierctl: no reply to %s (%02X %02X)\n", c->name, c->app, c->query);
	return session_close(ED_EXIT_FAIL);
}

/* Writes a set opcode, then reads the value back and prints it. */
static int set_and_confirm(const struct ed_cmd *c, const uint8_t *payload, size_t len)
{
	size_t n;
	int r;

	if (exchange(c->app, (uint8_t)c->set, payload, len, ED_NONE, NULL, NULL) < 0)
		return session_close(ED_EXIT_USAGE);
	r = query(c, reply_buf, &n);
	if (r != 1)
		return fail_no_reply(c, r);
	printf("%s\n", text);
	return session_close(ED_EXIT_OK);
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

	if (session_open(o) < 0)
		return ED_EXIT_FAIL;
	for (size_t i = 0; i < o->model->ncmds && !ed_link_stop; i++) {
		const struct ed_cmd *c = &o->model->cmds[i];
		size_t n;
		int r;

		if (c->query == ED_NONE)
			continue;
		r = query(c, reply_buf, &n);
		if (r < 0)
			return session_close(ED_EXIT_FAIL);
		printf("%-12s %s\n", c->name, r == 1 ? text : "(no reply)");
		fflush(stdout);
		answered += r;
	}
	return session_close(answered > 0 ? ED_EXIT_OK : ED_EXIT_FAIL);
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
	if (session_open(o) < 0)
		return ED_EXIT_FAIL;
	r = query(c, reply_buf, &n);
	if (r != 1)
		return fail_no_reply(c, r);
	printf("%s\n", text);
	return session_close(ED_EXIT_OK);
}

int ed_cmd_volume(const struct ed_opts *o, const char *arg)
{
	const struct ed_cmd *c = ed_cmd_find(o->model, "volume");
	unsigned max, before, target;
	uint8_t value;
	size_t n;
	int r;

	if (session_open(o) < 0)
		return ED_EXIT_FAIL;
	/* Every change starts from a fresh read: relative steps need the current
	 * value and every target is clamped to the max the speaker reports. */
	r = query(c, reply_buf, &n);
	if (r != 1)
		return fail_no_reply(c, r);
	if (arg == NULL) {
		printf("%s\n", text);
		return session_close(ED_EXIT_OK);
	}
	if (n != 2) {
		fprintf(stderr, "edifierctl: unexpected volume reply: %s\n", text);
		return session_close(ED_EXIT_FAIL);
	}
	max = reply_buf[0];
	before = reply_buf[1];
	if (!ed_volume_target(arg, before, max, &target)) {
		fprintf(stderr, "edifierctl: bad volume '%s' or speaker max is 0\n", arg);
		return session_close(ED_EXIT_USAGE);
	}
	value = (uint8_t)target;
	if (exchange(c->app, (uint8_t)c->set, &value, 1, ED_NONE, NULL, NULL) < 0)
		return session_close(ED_EXIT_FAIL);
	r = query(c, reply_buf, &n);
	if (r != 1)
		return fail_no_reply(c, r);
	printf("%u -> %s\n", before, text);
	if (n != 2 || (unsigned)reply_buf[1] != target) {
		fprintf(stderr, "edifierctl: speaker reports %s, wanted %u\n", text, target);
		return session_close(ED_EXIT_FAIL);
	}
	return session_close(ED_EXIT_OK);
}

int ed_cmd_input(const struct ed_opts *o, const char *source)
{
	const struct ed_cmd *c = ed_cmd_find(o->model, "input");
	uint8_t payload[2];
	size_t n;
	int r;

	char *end;
	unsigned long value = strtoul(source, &end, 10);

	/* Decimal, like the "group G source S" that get input prints. */
	if (*source < '0' || *source > '9' || *end != '\0' || value > 0xFF) {
		fprintf(stderr, "edifierctl: input SOURCE is a number 0-255\n");
		return ED_EXIT_USAGE;
	}
	payload[1] = (uint8_t)value;
	if (session_open(o) < 0)
		return ED_EXIT_FAIL;
	/* Keep the group the speaker reports: [group, source]. */
	r = query(c, reply_buf, &n);
	if (r != 1)
		return fail_no_reply(c, r);
	if (n != 2) {
		fprintf(stderr, "edifierctl: unexpected input reply: %s\n", text);
		return session_close(ED_EXIT_FAIL);
	}
	payload[0] = reply_buf[0];
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
	if (session_open(o) < 0)
		return ED_EXIT_FAIL;
	return set_and_confirm(ed_cmd_find(o->model, "eco"), &value, 1);
}

int ed_cmd_raw(const struct ed_opts *o, int argc, char **argv)
{
	static uint8_t payload[ED_PAYLOAD_MAX];
	uint8_t app, cmd;
	long len = 0;
	size_t n;
	int r;

	if (argc < 2 || argc > 3 || !ed_parse_byte(argv[0], &app) || !ed_parse_byte(argv[1], &cmd) ||
	    (argc == 3 && (len = ed_parse_hex(argv[2], payload, sizeof(payload))) < 0)) {
		fprintf(stderr, "edifierctl: raw APP CMD [HEX], e.g. raw ec 66\n");
		return ED_EXIT_USAGE;
	}
	/* Refuse before connecting, so a refused opcode costs nothing. */
	if (!ed_gate_allows(ed_gate_check(o->model, app, cmd), o->force)) {
		fprintf(stderr, "edifierctl: refused %02X %02X: %s\n", app, cmd,
			ed_gate_reason(ed_gate_check(o->model, app, cmd)));
		return ED_EXIT_USAGE;
	}
	if (session_open(o) < 0)
		return ED_EXIT_FAIL;
	r = exchange(app, cmd, payload, (size_t)len, cmd, reply_buf, &n);
	if (r < 0)
		return session_close(ED_EXIT_FAIL);
	if (r == 0) {
		fprintf(stderr, "edifierctl: frame sent, no reply with opcode %02X\n", cmd);
		return session_close(ED_EXIT_FAIL);
	}
	ed_hex(reply_buf, n, text, sizeof(text));
	printf("%s\n", text);
	return session_close(ED_EXIT_OK);
}

int ed_cmd_monitor(const struct ed_opts *o)
{
	uint8_t chunk[1024]; /* > largest notification (ATT MTU 517 - 3) */

	if (session_open(o) < 0)
		return ED_EXIT_FAIL;
	fprintf(stderr, "edifierctl: monitoring, Ctrl-C to stop\n");
	while (!ed_link_stop) {
		ssize_t got = ed_link_read(sess.link, chunk, sizeof(chunk), 1000);
		struct ed_frame f;

		if (got < 0) {
			fprintf(stderr, "edifierctl: link lost\n");
			return session_close(ED_EXIT_FAIL);
		}
		ed_parser_feed(&sess.parser, chunk, (size_t)got);
		while (ed_parser_next(&sess.parser, &f)) {
			char stamp[16];
			time_t now = time(NULL);

			strftime(stamp, sizeof(stamp), "%H:%M:%S", localtime(&now));
			print_frame(stdout, stamp, &f);
			fflush(stdout);
		}
	}
	return session_close(ED_EXIT_OK);
}
