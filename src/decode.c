/* Reply decoders. Each layout here is listed in docs/PROTOCOL.md; a frame
 * whose payload does not match the expected size falls back to hex. */
#include "decode.h"

#include <stdio.h>

#include "model.h"

void ed_hex(const uint8_t *bytes, size_t n, char *out, size_t cap)
{
	size_t pos = 0;

	if (cap == 0)
		return;
	out[0] = '\0';
	for (size_t i = 0; i < n && pos + 4 <= cap; i++)
		pos += (size_t)snprintf(out + pos, cap - pos, i ? " %02x" : "%02x", bytes[i]);
}

/* Device text (a name) with anything that is not printable ASCII escaped,
 * so a speaker cannot send terminal control sequences to the user. */
static void safe_text(const uint8_t *bytes, size_t n, char *out, size_t cap)
{
	size_t pos = 0;

	for (size_t i = 0; i < n && pos + 5 <= cap; i++) {
		uint8_t b = bytes[i];

		if (b >= 0x20 && b < 0x7F && b != '\\')
			out[pos++] = (char)b;
		else
			pos += (size_t)snprintf(out + pos, cap - pos, "\\x%02x", b);
	}
	out[pos] = '\0';
}

static void join_dotted(const uint8_t *bytes, size_t n, char *out, size_t cap)
{
	size_t pos = 0;

	out[0] = '\0';
	for (size_t i = 0; i < n && pos + 5 <= cap; i++)
		pos += (size_t)snprintf(out + pos, cap - pos, i ? ".%u" : "%u", bytes[i]);
}

void ed_decode(const struct ed_frame *f, char *out, size_t cap)
{
	const uint8_t *p = f->payload;
	size_t n = f->len;

	if (cap == 0)
		return;
	if (f->app == ED_APP_MAIN) {
		switch (f->cmd) {
		case 0x66: /* [max, current] */
			if (n == 2) {
				snprintf(out, cap, "%u (max %u)", p[1], p[0]);
				return;
			}
			break;
		case 0x61: /* [group, source] */
			if (n == 2) {
				snprintf(out, cap, "group %u source %u", p[0], p[1]);
				return;
			}
			break;
		case 0xB1: /* [0|1] */
			if (n == 1 && p[0] <= 1) {
				snprintf(out, cap, "%s", p[0] ? "on" : "off");
				return;
			}
			break;
		case 0xD3: /* big-endian 16-bit, unit unknown */
			if (n == 2) {
				snprintf(out, cap, "%u", (unsigned)((p[0] << 8) | p[1]));
				return;
			}
			break;
		case 0xC6: /* version bytes joined with '.' */
			join_dotted(p, n, out, cap);
			return;
		case 0xC9: /* UTF-8 name */
			safe_text(p, n, out, cap);
			return;
		case 0xC3: /* [state], 0x0D = playing */
			if (n == 1) {
				snprintf(out, cap, "0x%02x%s", p[0], p[0] == 0x0D ? " (playing)" : "");
				return;
			}
			break;
		default:
			break;
		}
	}
	ed_hex(p, n, out, cap);
}

static int hex_digit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

bool ed_parse_byte(const char *s, uint8_t *out)
{
	int hi, lo;

	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		s += 2;
	if (s[0] == '\0' || (s[1] != '\0' && s[2] != '\0'))
		return false;
	hi = hex_digit(s[0]);
	lo = s[1] ? hex_digit(s[1]) : 0;
	if (hi < 0 || lo < 0)
		return false;
	*out = (uint8_t)(s[1] ? hi * 16 + lo : hi);
	return true;
}

long ed_parse_hex(const char *s, uint8_t *out, size_t cap)
{
	size_t n = 0;
	int hi = -1;

	for (; *s; s++) {
		int d;

		if (*s == ' ' && hi < 0)
			continue;
		d = hex_digit(*s);
		if (d < 0)
			return -1;
		if (hi < 0) {
			hi = d;
			continue;
		}
		if (n == cap)
			return -1;
		out[n++] = (uint8_t)(hi * 16 + d);
		hi = -1;
	}
	return hi < 0 ? (long)n : -1;
}
