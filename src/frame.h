/* Frame format of the Edifier BLE control protocol (v2): build, checksum, reassemble.
 *
 *   host -> speaker   AA <app> <cmd> <len_hi> <len_lo> <payload...> <sum8>
 *   speaker -> host   BB (or CC) <app> <cmd> <len_hi> <len_lo> <payload...> <sum8>
 *   sum8 = sum of all preceding bytes, modulo 256
 *
 * Pure code: no I/O, no allocation. See docs/PROTOCOL.md. */
#ifndef ED_FRAME_H
#define ED_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ED_HDR_HOST 0xAA      /* every frame we send */
#define ED_HDR_REPLY 0xBB     /* speaker frames seen on the unit */
#define ED_HDR_REPLY_ALT 0xCC /* also accepted as a reply header by the app */
#define ED_FRAME_OVERHEAD 6   /* header, app, cmd, two length bytes, checksum */
#define ED_PAYLOAD_MAX 0xFFFF /* the length field is 16 bits */

/* One parsed speaker frame. payload points into the parser's buffer and stays
 * valid until the next ed_parser_feed() or ed_parser_next() call. */
struct ed_frame {
	uint8_t header, app, cmd;
	uint16_t len;
	const uint8_t *payload;
};

/* Reassembles speaker frames that arrive split across, or packed into, BLE
 * notifications. The buffer holds the largest frame the length field allows,
 * so no frame is ever cut short. */
struct ed_parser {
	uint8_t buf[ED_FRAME_OVERHEAD + ED_PAYLOAD_MAX];
	size_t used;    /* bytes held */
	size_t pending; /* bytes of the frame returned last, removed on the next call */
	size_t skipped; /* bytes dropped while searching for a header */
	size_t bad_sum; /* complete frames rejected for a wrong checksum */
};

uint8_t ed_sum8(const uint8_t *bytes, size_t n);

/* Writes a host frame into out. Returns its length, or 0 if it does not fit
 * in cap or the payload is longer than the length field allows. */
size_t ed_frame_build(uint8_t app, uint8_t cmd, const uint8_t *payload, size_t len,
		      uint8_t *out, size_t cap);

void ed_parser_init(struct ed_parser *p);
void ed_parser_feed(struct ed_parser *p, const uint8_t *data, size_t n);
/* Returns true and fills f when a complete frame with a valid checksum is
 * buffered; false when more bytes are needed. */
bool ed_parser_next(struct ed_parser *p, struct ed_frame *f);

#endif
