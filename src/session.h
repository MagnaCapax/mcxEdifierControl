/* One BLE session per run: open the link, send frames through the gate, wait
 * for the reply, log everything else the speaker sends. */
#ifndef ED_SESSION_H
#define ED_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "commands.h"

/* Holds the payload of the last reply; a frame can carry up to 64 KiB. */
extern uint8_t ed_reply[];
/* Decoded or hex text of the last reply. */
extern char ed_text[];

int ed_session_open(const struct ed_opts *o);
/* Logs frames still queued, reports parser drops, disconnects. Returns rc. */
int ed_session_close(int rc);

/* Sends one frame through the gate. With expect != ED_NONE, waits for the
 * frame with that opcode and copies its payload to ed_reply.
 * Returns 1 replied (or sent, when not waiting), 0 no reply, -1 refused or link failure. */
int ed_exchange(uint8_t app, uint8_t cmd, const uint8_t *payload, size_t len, int expect,
		size_t *reply_len);
/* Runs a table query; on a reply, ed_text holds its decoded form. */
int ed_query(const struct ed_cmd *c, size_t *reply_len);
/* Prints frames as they arrive until a signal or link loss. */
int ed_session_monitor(void);

#endif
