/* Frame build, checksum and reassembly, including the negative controls:
 * a frame with a wrong checksum is rejected and parsing recovers after it,
 * and a stray header with a bogus length does not stall the parser. */
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "frame.h"

/* A speaker frame observed on the unit: A2DP state 0x0D. */
static const uint8_t seen_c3[] = {0xBB, 0xEC, 0xC3, 0x00, 0x01, 0x0D, 0x78};

static struct ed_parser *new_parser(void)
{
	struct ed_parser *p = malloc(sizeof(*p));

	ed_parser_init(p);
	return p;
}

static void test_checksum_and_build(void)
{
	uint8_t out[16];
	uint8_t vol = 0x0C;

	CHECK(ed_sum8(seen_c3, 6) == 0x78);
	CHECK(ed_sum8(NULL, 0) == 0);
	/* The app's first frame after connect: capability query. */
	CHECK(ed_frame_build(0xEC, 0xD8, NULL, 0, out, sizeof(out)) == 6);
	CHECK(memcmp(out, (uint8_t[]){0xAA, 0xEC, 0xD8, 0x00, 0x00, 0x6E}, 6) == 0);
	CHECK(ed_frame_build(0xEC, 0x67, &vol, 1, out, sizeof(out)) == 7);
	CHECK(out[3] == 0 && out[4] == 1 && out[5] == 0x0C && out[6] == ed_sum8(out, 6));
	CHECK(ed_frame_build(0xEC, 0x67, &vol, 1, out, 6) == 0); /* does not fit */
	CHECK(ed_frame_build(0xEC, 0x67, &vol, ED_PAYLOAD_MAX + 1, out, sizeof(out)) == 0);
}

static void test_parse_whole_split_packed(void)
{
	struct ed_parser *p = new_parser();
	struct ed_frame f;
	uint8_t two[14];

	ed_parser_feed(p, seen_c3, sizeof(seen_c3));
	CHECK(ed_parser_next(p, &f));
	CHECK(f.header == 0xBB && f.app == 0xEC && f.cmd == 0xC3 && f.len == 1 && f.payload[0] == 0x0D);
	CHECK(!ed_parser_next(p, &f));
	/* split across three notifications */
	ed_parser_feed(p, seen_c3, 2);
	CHECK(!ed_parser_next(p, &f));
	ed_parser_feed(p, seen_c3 + 2, 3);
	CHECK(!ed_parser_next(p, &f));
	ed_parser_feed(p, seen_c3 + 5, 2);
	CHECK(ed_parser_next(p, &f) && f.cmd == 0xC3);
	/* two frames in one notification */
	memcpy(two, seen_c3, 7);
	memcpy(two + 7, seen_c3, 7);
	ed_parser_feed(p, two, sizeof(two));
	CHECK(ed_parser_next(p, &f) && ed_parser_next(p, &f));
	CHECK(!ed_parser_next(p, &f));
	CHECK(p->skipped == 0 && p->bad_sum == 0);
	free(p);
}

static void test_parse_headers_and_garbage(void)
{
	struct ed_parser *p = new_parser();
	struct ed_frame f;
	uint8_t cc[7], host[6], junk[] = {0x00, 0x11, 0x22};

	memcpy(cc, seen_c3, 7);
	cc[0] = 0xCC;
	cc[6] = ed_sum8(cc, 6);
	ed_parser_feed(p, cc, 7);
	CHECK(ed_parser_next(p, &f) && f.header == 0xCC);
	/* AA is the host header, never a speaker frame: skipped */
	ed_frame_build(0xEC, 0xD8, NULL, 0, host, sizeof(host));
	ed_parser_feed(p, host, sizeof(host));
	CHECK(!ed_parser_next(p, &f));
	ed_parser_feed(p, junk, sizeof(junk));
	ed_parser_feed(p, seen_c3, sizeof(seen_c3));
	CHECK(ed_parser_next(p, &f) && f.cmd == 0xC3);
	CHECK(p->skipped == sizeof(host) + sizeof(junk));
	free(p);
}

static void test_parse_bad_checksum(void)
{
	struct ed_parser *p = new_parser();
	struct ed_frame f;
	uint8_t bad[7];

	memcpy(bad, seen_c3, 7);
	bad[6] ^= 0xFF;
	ed_parser_feed(p, bad, 7);
	CHECK(!ed_parser_next(p, &f)); /* negative control: rejected */
	CHECK(p->bad_sum == 1);
	ed_parser_feed(p, seen_c3, 7);
	CHECK(ed_parser_next(p, &f) && f.cmd == 0xC3 && f.payload[0] == 0x0D);
	free(p);
}

static void test_parse_sizes(void)
{
	struct ed_parser *p = new_parser();
	struct ed_frame f;
	uint8_t empty[6] = {0xBB, 0xEC, 0xD8, 0x00, 0x00, 0};
	uint8_t big[6 + 0x37];

	empty[5] = ed_sum8(empty, 5);
	ed_parser_feed(p, empty, 6);
	CHECK(ed_parser_next(p, &f) && f.len == 0);
	/* a 61-byte frame (length 0x0037), the size seen on the unit */
	memset(big, 0x41, sizeof(big));
	big[0] = 0xBB, big[1] = 0xEC, big[2] = 0x01, big[3] = 0x00, big[4] = 0x37;
	big[sizeof(big) - 1] = ed_sum8(big, sizeof(big) - 1);
	ed_parser_feed(p, big, sizeof(big));
	CHECK(ed_parser_next(p, &f) && f.len == 0x37 && f.payload[0x36] == 0x41);
	free(p);
}

static void test_parse_overflow(void)
{
	struct ed_parser *p = new_parser();
	struct ed_frame f;
	/* A header claiming the largest length, then more data than the buffer
	 * holds: the parser must stay bounded and still find a later frame. */
	uint8_t claim[5] = {0xBB, 0xEC, 0x01, 0xFF, 0xFF};
	size_t fill = sizeof(p->buf);
	uint8_t *junk = calloc(1, fill);

	ed_parser_feed(p, claim, 5);
	CHECK(!ed_parser_next(p, &f));
	ed_parser_feed(p, junk, fill); /* does not fit: buffer restarts */
	CHECK(p->used <= sizeof(p->buf));
	ed_parser_feed(p, seen_c3, 7);
	CHECK(ed_parser_next(p, &f) && f.cmd == 0xC3);
	free(junk);
	free(p);
}

static void test_parse_stall_resync(void)
{
	struct ed_parser *p = new_parser();
	struct ed_frame f;
	/* A stray reply header with a huge length, then a valid frame: the parser
	 * must not wait for 64 KiB that never comes, it skips to the valid frame. */
	uint8_t stall[5 + sizeof(seen_c3)] = {0xBB, 0xEC, 0x01, 0xFF, 0xFF};

	memcpy(stall + 5, seen_c3, sizeof(seen_c3));
	ed_parser_feed(p, stall, sizeof(stall));
	CHECK(ed_parser_next(p, &f) && f.cmd == 0xC3 && f.payload[0] == 0x0D);
	CHECK(p->skipped == 5);
	CHECK(!ed_parser_next(p, &f));
	/* negative control: an incomplete frame with nothing valid behind it waits */
	ed_parser_feed(p, stall, 5);
	CHECK(!ed_parser_next(p, &f) && p->used == 5);
	free(p);
}

/* A now-playing-sized frame (01, 47-byte payload) between two C3 pushes,
 * delivered in notifications of every size from 1 byte to the whole stream.
 * The payload is UTF-8 holding the bytes BB and CC, the two reply headers,
 * so a resync on a header inside a partial frame would show up here as
 * skipped bytes. Every split must give the same three frames and drop nothing. */
static void test_parse_long_frame_every_split(void)
{
	static const char text[] = "Artist \xC2\xBB Title \xCC\x80 MK\xE2\x85\xA1";
	uint8_t stream[7 + 6 + 47 + 7];
	uint8_t *lf = stream + 7;
	size_t total = sizeof(stream);
	int bad_splits = 0;

	memcpy(stream, seen_c3, 7);
	lf[0] = 0xBB, lf[1] = 0xEC, lf[2] = 0x01, lf[3] = 0x00, lf[4] = 47;
	memset(lf + 5, 'x', 47);
	memcpy(lf + 5, text, sizeof(text) - 1);
	lf[5 + 47] = ed_sum8(lf, 5 + 47);
	memcpy(stream + 7 + 6 + 47, seen_c3, 7);

	for (size_t chunk = 1; chunk <= total; chunk++) {
		struct ed_parser *p = new_parser();
		struct ed_frame f;
		uint8_t cmds[4];
		size_t got = 0;
		bool payload_ok = false;

		for (size_t off = 0; off < total; off += chunk) {
			ed_parser_feed(p, stream + off, off + chunk > total ? total - off : chunk);
			while (ed_parser_next(p, &f)) {
				if (got < sizeof(cmds))
					cmds[got] = f.cmd;
				if (f.cmd == 0x01)
					payload_ok = f.len == 47 && memcmp(f.payload, lf + 5, 47) == 0;
				got++;
			}
		}
		if (got != 3 || cmds[0] != 0xC3 || cmds[1] != 0x01 || cmds[2] != 0xC3 || !payload_ok ||
		    p->skipped != 0 || p->bad_sum != 0)
			bad_splits++;
		free(p);
	}
	CHECK(bad_splits == 0);
}

void test_frame(void)
{
	test_checksum_and_build();
	test_parse_whole_split_packed();
	test_parse_headers_and_garbage();
	test_parse_bad_checksum();
	test_parse_sizes();
	test_parse_overflow();
	test_parse_stall_resync();
	test_parse_long_frame_every_split();
}
