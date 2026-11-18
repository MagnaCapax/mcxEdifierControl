/* Session layer. Every outgoing frame passes the gate; every speaker frame
 * either answers the query or is logged as unsolicited (app, opcode,
 * length; the payload only with -v, since 01/02 carry what is playing). */
#include "session.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "bluez.h"
#include "decode.h"
#include "frame.h"

/* Large buffers are static: one session per process. */
uint8_t ed_reply[ED_PAYLOAD_MAX];
char ed_text[3 * ED_PAYLOAD_MAX + 1];

static const struct ed_opts *opts;
static struct ed_link *conn;
static struct ed_parser parser;

static void print_frame(FILE *to, const char *tag, const struct ed_frame *f)
{
	const struct ed_cmd *c = ed_cmd_by_opcode(opts->model, f->app, f->cmd);

	fprintf(to, "%s %02X %02X len %u %s", tag, f->app, f->cmd, f->len, c ? c->name : "unknown");
	if (opts->verbose && f->len > 0) {
		ed_hex(f->payload, f->len, ed_text, 3 * ED_PAYLOAD_MAX + 1);
		fprintf(to, " : %s", ed_text);
	}
	fputc('\n', to);
}

/* Reads whatever the speaker has already sent, without waiting, and logs
 * every complete frame. Run before each write, so a reply that arrives after
 * it can only be an answer to it (or a push of the same opcode), and before
 * closing, so no frame goes unlogged. Returns -1 when the link is gone. */
static int drain(void)
{
	uint8_t chunk[1024]; /* larger than any notification (ATT allows 512 bytes) */
	struct ed_frame f;
	ssize_t got;

	while (ed_parser_next(&parser, &f))
		print_frame(stderr, "unsolicited", &f);
	while ((got = ed_link_read(conn, chunk, sizeof(chunk), 0)) > 0) {
		ed_parser_feed(&parser, chunk, (size_t)got);
		while (ed_parser_next(&parser, &f))
			print_frame(stderr, "unsolicited", &f);
	}
	return got < 0 ? -1 : 0;
}

int ed_session_open(const struct ed_opts *o)
{
	char err[512];

	opts = o;
	ed_parser_init(&parser);
	conn = ed_link_open(o->model, o->address, err, sizeof(err));
	if (conn == NULL) {
		fprintf(stderr, "edifierctl: %s\n", err);
		return -1;
	}
	return 0;
}

int ed_session_close(int rc)
{
	drain();
	if (parser.skipped || parser.bad_sum)
		fprintf(stderr, "edifierctl: dropped %zu stray bytes, %zu frames with a bad checksum\n",
			parser.skipped, parser.bad_sum);
	ed_link_close(conn);
	conn = NULL;
	return rc;
}

static long elapsed_ms(const struct timespec *t0)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (long)(t.tv_sec - t0->tv_sec) * 1000 + (t.tv_nsec - t0->tv_nsec) / 1000000;
}

int ed_exchange(uint8_t app, uint8_t cmd, const uint8_t *payload, size_t len, int expect,
		size_t *reply_len)
{
	static uint8_t out[ED_FRAME_OVERHEAD + ED_PAYLOAD_MAX];
	uint8_t chunk[1024];
	enum ed_gate g = ed_gate_check(opts->model, app, cmd);
	size_t n = ed_frame_build(app, cmd, payload, len, out, sizeof(out));
	struct timespec t0;

	/* The gate is checked here too, so no command can send around it. */
	if (!ed_gate_allows(g, opts->force)) {
		fprintf(stderr, "edifierctl: refused %02X %02X: %s\n", app, cmd, ed_gate_reason(g));
		return -1;
	}
	if (drain() < 0) {
		fprintf(stderr, "edifierctl: link lost\n");
		return -1;
	}
	if (n == 0 || ed_link_write(conn, out, n) < 0) {
		fprintf(stderr, "edifierctl: write of %02X %02X failed (%zu bytes, max %zu)\n", app, cmd, n,
			ed_link_max_write(conn));
		return -1;
	}
	if (expect == ED_NONE)
		return 1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (;;) {
		struct ed_frame f;
		long left;
		ssize_t got;

		/* Check what is buffered before waiting: one notification can hold several frames. */
		while (ed_parser_next(&parser, &f)) {
			if (f.app == app && f.cmd == expect) {
				memcpy(ed_reply, f.payload, f.len);
				*reply_len = f.len;
				return 1;
			}
			print_frame(stderr, "unsolicited", &f);
		}
		left = opts->timeout_ms - elapsed_ms(&t0);
		if (left <= 0 || ed_link_stop)
			return 0;
		got = ed_link_read(conn, chunk, sizeof(chunk), (int)left);
		if (got < 0) {
			fprintf(stderr, "edifierctl: link lost\n");
			return -1;
		}
		ed_parser_feed(&parser, chunk, (size_t)got);
	}
}

int ed_query(const struct ed_cmd *c, size_t *reply_len)
{
	int r = ed_exchange(c->app, (uint8_t)c->query, NULL, 0, c->query, reply_len);

	if (r == 1) {
		struct ed_frame f = {ED_HDR_REPLY, c->app, (uint8_t)c->query, (uint16_t)*reply_len, ed_reply};

		ed_decode(&f, ed_text, 3 * ED_PAYLOAD_MAX + 1);
	}
	return r;
}

int ed_session_monitor(void)
{
	uint8_t chunk[1024];

	fprintf(stderr, "edifierctl: monitoring, Ctrl-C to stop\n");
	while (!ed_link_stop) {
		ssize_t got = ed_link_read(conn, chunk, sizeof(chunk), 1000);
		struct ed_frame f;

		if (got < 0) {
			fprintf(stderr, "edifierctl: link lost\n");
			return ED_EXIT_FAIL;
		}
		ed_parser_feed(&parser, chunk, (size_t)got);
		while (ed_parser_next(&parser, &f)) {
			char stamp[16];
			time_t now = time(NULL);

			strftime(stamp, sizeof(stamp), "%H:%M:%S", localtime(&now));
			print_frame(stdout, stamp, &f);
			fflush(stdout);
		}
	}
	return ED_EXIT_OK;
}
