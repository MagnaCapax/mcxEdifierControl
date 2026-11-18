/* Decoders and hex parsing. Layouts with a wrong size fall back to hex; D8
 * is always hex; device text cannot carry terminal control sequences. */
#include <string.h>

#include "check.h"
#include "decode.h"

static void decode(uint8_t app, uint8_t cmd, const uint8_t *p, uint16_t n, char *out, size_t cap)
{
	struct ed_frame f = {0xBB, app, cmd, n, p};

	ed_decode(&f, out, cap);
}

static void test_decoders(void)
{
	char s[128];

	decode(0xEC, 0x66, (uint8_t[]){30, 12}, 2, s, sizeof(s));
	CHECK(strcmp(s, "12 (max 30)") == 0);
	decode(0xEC, 0x66, (uint8_t[]){30}, 1, s, sizeof(s));
	CHECK(strcmp(s, "1e") == 0); /* wrong size: hex */
	decode(0xEC, 0x61, (uint8_t[]){1, 4}, 2, s, sizeof(s));
	CHECK(strcmp(s, "group 1 source 4") == 0);
	decode(0xEC, 0xB1, (uint8_t[]){1}, 1, s, sizeof(s));
	CHECK(strcmp(s, "on") == 0);
	decode(0xEC, 0xB1, (uint8_t[]){7}, 1, s, sizeof(s));
	CHECK(strcmp(s, "07") == 0);
	decode(0xEC, 0xD3, (uint8_t[]){0x01, 0x2C}, 2, s, sizeof(s));
	CHECK(strcmp(s, "300") == 0);
	decode(0xEC, 0xC6, (uint8_t[]){1, 2, 30}, 3, s, sizeof(s));
	CHECK(strcmp(s, "1.2.30") == 0);
	decode(0xEC, 0xC3, (uint8_t[]){0x0D}, 1, s, sizeof(s));
	CHECK(strcmp(s, "0x0d (playing)") == 0);
	decode(0xEC, 0xC9, (const uint8_t *)"Spk\x1b[2J", 7, s, sizeof(s));
	CHECK(strcmp(s, "Spk\\x1b[2J") == 0);
	decode(0xEC, 0xD8, (uint8_t[]){0xAB, 0x01}, 2, s, sizeof(s));
	CHECK(strcmp(s, "ab 01") == 0);
	decode(0xED, 0x66, (uint8_t[]){30, 12}, 2, s, sizeof(s)); /* other app: hex */
	CHECK(strcmp(s, "1e 0c") == 0);
	decode(0xEC, 0x48, NULL, 0, s, sizeof(s));
	CHECK(strcmp(s, "") == 0);
	/* small buffers stay terminated */
	decode(0xEC, 0xD8, (uint8_t[]){1, 2, 3, 4}, 4, s, 6);
	CHECK(strcmp(s, "01 02") == 0); /* whole bytes only */
	decode(0xEC, 0xC9, (const uint8_t *)"abcdef", 6, s, 4);
	CHECK(strlen(s) < 4);
}

static void test_hex_parsing(void)
{
	uint8_t b = 0, buf[4];

	CHECK(ed_parse_byte("ec", &b) && b == 0xEC);
	CHECK(ed_parse_byte("0x0C", &b) && b == 0x0C);
	CHECK(ed_parse_byte("7", &b) && b == 0x07);
	CHECK(!ed_parse_byte("", &b));
	CHECK(!ed_parse_byte("0x", &b));
	CHECK(!ed_parse_byte("123", &b));
	CHECK(!ed_parse_byte("g1", &b));
	CHECK(ed_parse_hex("010c", buf, sizeof(buf)) == 2 && buf[0] == 1 && buf[1] == 0x0C);
	CHECK(ed_parse_hex("01 0c ff", buf, sizeof(buf)) == 3 && buf[2] == 0xFF);
	CHECK(ed_parse_hex("", buf, sizeof(buf)) == 0);
	CHECK(ed_parse_hex("0", buf, sizeof(buf)) == -1);
	CHECK(ed_parse_hex("0 1", buf, sizeof(buf)) == -1);
	CHECK(ed_parse_hex("zz", buf, sizeof(buf)) == -1);
	CHECK(ed_parse_hex("0102030405", buf, sizeof(buf)) == -1); /* more than cap */
}

void test_decode(void)
{
	test_decoders();
	test_hex_parsing();
}
