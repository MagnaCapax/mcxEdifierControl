/* Per-model data and the send gate.
 *
 * A model is its BLE identifiers plus its command table. Only the Edifier
 * R2750DB MKII is described; adding another model means adding one table in
 * model.c (docs/adr/0001). The gate is protocol-wide and applies to every
 * app code: some opcodes need an explicit force, some are never sent. */
#ifndef ED_MODEL_H
#define ED_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ED_APP_MAIN 0xEC /* default app code */
#define ED_APP_SUB 0xED  /* used by the sub-out commands */
#define ED_NONE (-1)     /* no query or no set opcode */

struct ed_cmd {
	const char *name;  /* CLI name, e.g. "volume" */
	uint8_t app;       /* ED_APP_MAIN or ED_APP_SUB */
	int query;         /* opcode that reads it, or ED_NONE */
	int set;           /* opcode that writes it, or ED_NONE */
	int push;          /* opcode the speaker sends unasked, or ED_NONE; never sent by us */
	const char *about; /* one line for --help */
};

struct ed_model {
	const char *name;
	const char *adv_uuid;     /* 16-bit service UUID in the advertisement */
	const char *service_uuid; /* GATT control service */
	const char *rx_uuid;      /* speaker -> host, notify */
	const char *tx_uuid;      /* host -> speaker, write without response */
	const struct ed_cmd *cmds;
	size_t ncmds;
};

enum ed_gate {
	ED_GATE_OK,          /* in the model's command table */
	ED_GATE_NEEDS_FORCE, /* destructive: sent only with an explicit force */
	ED_GATE_NEVER,       /* firmware update or bulk transfer: never sent */
	ED_GATE_UNKNOWN,     /* not in the table: refused */
};

const struct ed_model *ed_model_default(void);
const struct ed_cmd *ed_cmd_find(const struct ed_model *m, const char *name);
/* The table entry whose query, set or push opcode is (app, op), or NULL. */
const struct ed_cmd *ed_cmd_by_opcode(const struct ed_model *m, uint8_t app, uint8_t op);

enum ed_gate ed_gate_check(const struct ed_model *m, uint8_t app, uint8_t op);
bool ed_gate_allows(enum ed_gate g, bool force);
const char *ed_gate_reason(enum ed_gate g);

/* Volume target for "N", "+N" or "-N" given a fresh read of [max, current].
 * The result is clamped to 0..max. Returns false for a malformed argument or
 * a reported max of 0. */
bool ed_volume_target(const char *arg, unsigned current, unsigned max, unsigned *out);

#endif
