/* Frame build, checksum and reassembly. See frame.h for the wire format. */
#include "frame.h"

#include <string.h>

uint8_t ed_sum8(const uint8_t *bytes, size_t n)
{
	unsigned sum = 0;

	for (size_t i = 0; i < n; i++)
		sum += bytes[i];
	return (uint8_t)(sum & 0xFFu);
}

size_t ed_frame_build(uint8_t app, uint8_t cmd, const uint8_t *payload, size_t len,
		      uint8_t *out, size_t cap)
{
	size_t total = len + ED_FRAME_OVERHEAD;

	if (len > ED_PAYLOAD_MAX || total > cap)
		return 0;
	out[0] = ED_HDR_HOST;
	out[1] = app;
	out[2] = cmd;
	out[3] = (uint8_t)(len >> 8);
	out[4] = (uint8_t)(len & 0xFFu);
	if (len > 0)
		memcpy(out + 5, payload, len);
	out[total - 1] = ed_sum8(out, total - 1);
	return total;
}

void ed_parser_init(struct ed_parser *p)
{
	p->used = p->pending = p->skipped = p->bad_sum = 0;
}

/* Remove n bytes from the front of the buffer. */
static void drop_front(struct ed_parser *p, size_t n)
{
	memmove(p->buf, p->buf + n, p->used - n);
	p->used -= n;
}

/* Release the frame handed out by the previous ed_parser_next() call. */
static void release_pending(struct ed_parser *p)
{
	if (p->pending > 0) {
		drop_front(p, p->pending);
		p->pending = 0;
	}
}

void ed_parser_feed(struct ed_parser *p, const uint8_t *data, size_t n)
{
	release_pending(p);
	/* Only a corrupt length field can fill the buffer: start over rather than
	 * grow. A single notification larger than the buffer keeps its tail. */
	if (n > sizeof(p->buf) - p->used) {
		p->skipped += p->used;
		p->used = 0;
		if (n > sizeof(p->buf)) {
			p->skipped += n - sizeof(p->buf);
			data += n - sizeof(p->buf);
			n = sizeof(p->buf);
		}
	}
	memcpy(p->buf + p->used, data, n);
	p->used += n;
}

static bool is_reply_header(uint8_t b)
{
	return b == ED_HDR_REPLY || b == ED_HDR_REPLY_ALT;
}

/* True when a complete frame with a valid checksum starts at offset i. */
static bool valid_frame_at(const struct ed_parser *p, size_t i)
{
	size_t len, total;

	if (!is_reply_header(p->buf[i]) || p->used - i < ED_FRAME_OVERHEAD)
		return false;
	len = ((size_t)p->buf[i + 3] << 8) | p->buf[i + 4];
	total = len + ED_FRAME_OVERHEAD;
	return p->used - i >= total && ed_sum8(p->buf + i, total - 1) == p->buf[i + total - 1];
}

bool ed_parser_next(struct ed_parser *p, struct ed_frame *f)
{
	release_pending(p);
	for (;;) {
		size_t start = 0;

		while (start < p->used && !is_reply_header(p->buf[start]))
			start++;
		p->skipped += start;
		drop_front(p, start);
		if (p->used < ED_FRAME_OVERHEAD)
			return false;

		size_t len = ((size_t)p->buf[3] << 8) | p->buf[4];
		size_t total = len + ED_FRAME_OVERHEAD;

		if (p->used < total) {
			/* Still incomplete. If a valid frame is already buffered behind it, the
			 * header byte was garbage with a bogus length: skip to that frame
			 * instead of waiting for up to 64 KiB that will never come. */
			size_t next = 1;

			while (next < p->used && !valid_frame_at(p, next))
				next++;
			if (next == p->used)
				return false;
			p->skipped += next;
			drop_front(p, next);
			continue;
		}
		if (ed_sum8(p->buf, total - 1) != p->buf[total - 1]) {
			/* A header byte inside garbage or a corrupt frame: skip one byte and
			 * search again, so a valid frame later in the buffer is still found. */
			p->bad_sum++;
			p->skipped++;
			drop_front(p, 1);
			continue;
		}
		f->header = p->buf[0];
		f->app = p->buf[1];
		f->cmd = p->buf[2];
		f->len = (uint16_t)len;
		f->payload = p->buf + 5;
		p->pending = total;
		return true;
	}
}
