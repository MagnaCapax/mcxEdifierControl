/* Human-readable text for a speaker frame. Only layouts documented in
 * docs/PROTOCOL.md are decoded; everything else is printed as hex. */
#ifndef ED_DECODE_H
#define ED_DECODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "frame.h"

/* Payload bytes as "0d 78 ..." (space-separated hex). Always NUL-terminates. */
void ed_hex(const uint8_t *bytes, size_t n, char *out, size_t cap);
/* Decoded payload of a reply, e.g. "12 (max 30)". Always NUL-terminates. */
void ed_decode(const struct ed_frame *f, char *out, size_t cap);

/* Parses "0c", "0x0c" or "c" into a byte. */
bool ed_parse_byte(const char *s, uint8_t *out);
/* Parses hex digits into out; spaces between bytes are allowed. Returns the
 * byte count, or -1 for an odd digit count, a non-hex character or more than
 * cap bytes. */
long ed_parse_hex(const char *s, uint8_t *out, size_t cap);

#endif
