/* The R2750DB MKII command table, the send gate and the volume rule.
 * Command layouts come from analysis of the official app and are NOT yet
 * verified on hardware unless docs/PROTOCOL.md says so. */
#include "model.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* name, app, query, set, push, about */
static const struct ed_cmd r2750dbmk2_cmds[] = {
	{"caps", ED_APP_MAIN, 0xD8, ED_NONE, ED_NONE, "supported features (raw hex)"},
	{"volume", ED_APP_MAIN, 0x66, 0x67, ED_NONE, "master volume [max, current]"},
	{"input", ED_APP_MAIN, 0x61, 0x62, ED_NONE, "input source [group, source]"},
	{"eco", ED_APP_MAIN, 0xB1, 0xB2, ED_NONE, "ECO / power-saving mode"},
	{"autooff", ED_APP_MAIN, 0xD7, 0xD6, ED_NONE, "auto power-off without audio"},
	{"timer", ED_APP_MAIN, 0xD3, 0xD1, ED_NONE, "shutdown timer (16-bit, unit unknown)"},
	{"timer-cancel", ED_APP_MAIN, ED_NONE, 0xD2, ED_NONE, "cancel the shutdown timer"},
	{"ldac", ED_APP_MAIN, 0x48, 0x49, ED_NONE, "LDAC setting"},
	{"multipoint", ED_APP_MAIN, 0x7C, 0x7D, ED_NONE, "multipoint setting"},
	{"eq", ED_APP_MAIN, 0xD5, 0xC4, ED_NONE, "EQ preset (write back only bytes read)"},
	{"sound", ED_APP_MAIN, 0xBB, 0xBC, ED_NONE, "sound setting [index, config]"},
	{"subout", ED_APP_SUB, 0x13, 0x14, ED_NONE, "sub-out [index, value]"},
	{"firmware", ED_APP_MAIN, 0xC6, ED_NONE, ED_NONE, "firmware version"},
	{"mac", ED_APP_MAIN, 0xC8, ED_NONE, ED_NONE, "Bluetooth address"},
	{"name", ED_APP_MAIN, 0xC9, ED_NONE, ED_NONE, "device name"},
	{"a2dp", ED_APP_MAIN, 0xC3, ED_NONE, 0xC3, "A2DP state (also pushed)"},
	{"now-playing-title", ED_APP_MAIN, ED_NONE, ED_NONE, 0x01, "pushed only"},
	{"now-playing-artist", ED_APP_MAIN, ED_NONE, ED_NONE, 0x02, "pushed only"},
};

static const struct ed_model r2750dbmk2 = {
	.name = "Edifier R2750DB MKII",
	.adv_uuid = "00002c03-0000-1000-8000-00805f9b34fb",
	.service_uuid = "48092c03-1a48-11e9-ab14-d663bd873d93",
	.rx_uuid = "48090001-1a48-11e9-ab14-d663bd873d93",
	.tx_uuid = "48090002-1a48-11e9-ab14-d663bd873d93",
	.cmds = r2750dbmk2_cmds,
	.ncmds = sizeof(r2750dbmk2_cmds) / sizeof(r2750dbmk2_cmds[0]),
};

/* Firmware update and bulk transfer: never sent, not even with force. */
static const uint8_t never_ops[] = {0x10, 0x11, 0x12, 0x79, 0x7A, 0x7B, 0x88, 0x89, 0x8A};
/* Factory reset, power off, disconnect, re-pair, clear pairing records,
 * connect/disconnect by address, delete log, rename: only with force. */
static const uint8_t destructive_ops[] = {0x07, 0xCE, 0xCD, 0xCF, 0xC7, 0xAE, 0xAF, 0xB0, 0xCA};

const struct ed_model *ed_model_default(void)
{
	return &r2750dbmk2;
}

const struct ed_cmd *ed_cmd_find(const struct ed_model *m, const char *name)
{
	for (size_t i = 0; i < m->ncmds; i++)
		if (strcmp(m->cmds[i].name, name) == 0)
			return &m->cmds[i];
	return NULL;
}

const struct ed_cmd *ed_cmd_by_opcode(const struct ed_model *m, uint8_t app, uint8_t op)
{
	for (size_t i = 0; i < m->ncmds; i++) {
		const struct ed_cmd *c = &m->cmds[i];

		if (c->app == app && (c->query == op || c->set == op || c->push == op))
			return c;
	}
	return NULL;
}

static bool in_list(const uint8_t *list, size_t n, uint8_t op)
{
	for (size_t i = 0; i < n; i++)
		if (list[i] == op)
			return true;
	return false;
}

enum ed_gate ed_gate_check(const struct ed_model *m, uint8_t app, uint8_t op)
{
	/* The lists are checked first and ignore the app code on purpose. */
	if (in_list(never_ops, sizeof(never_ops), op))
		return ED_GATE_NEVER;
	if (in_list(destructive_ops, sizeof(destructive_ops), op))
		return ED_GATE_NEEDS_FORCE;
	for (size_t i = 0; i < m->ncmds; i++) {
		const struct ed_cmd *c = &m->cmds[i];

		if (c->app == app && (c->query == op || c->set == op))
			return ED_GATE_OK;
	}
	return ED_GATE_UNKNOWN;
}

bool ed_gate_allows(enum ed_gate g, bool force)
{
	return g == ED_GATE_OK || (g == ED_GATE_NEEDS_FORCE && force);
}

const char *ed_gate_reason(enum ed_gate g)
{
	switch (g) {
	case ED_GATE_OK:
		return "allowed";
	case ED_GATE_NEEDS_FORCE:
		return "destructive opcode: needs -f";
	case ED_GATE_NEVER:
		return "firmware update or bulk transfer: never sent";
	case ED_GATE_UNKNOWN:
		break;
	}
	return "opcode not in this model's command table";
}

bool ed_volume_target(const char *arg, unsigned current, unsigned max, unsigned *out)
{
	const char *digits = arg;
	char *end;

	if (max == 0 || arg == NULL)
		return false;
	if (*arg == '+' || *arg == '-')
		digits++;
	if (!isdigit((unsigned char)*digits))
		return false;
	unsigned long n = strtoul(digits, &end, 10);

	if (*end != '\0' || n > 0xFFFFu)
		return false;
	long target = *arg == '+' ? (long)current + (long)n
		    : *arg == '-' ? (long)current - (long)n
				  : (long)n;
	if (target < 0)
		target = 0;
	if (target > (long)max)
		target = (long)max;
	*out = (unsigned)target;
	return true;
}

bool ed_raw_reserved(const struct ed_model *m, uint8_t app, uint8_t op)
{
	const struct ed_cmd *vol = ed_cmd_find(m, "volume");

	return vol != NULL && vol->app == app && vol->set == op;
}
