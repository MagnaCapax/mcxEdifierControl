/* Command table, send gate and the volume rule. Negative controls: a
 * destructive opcode is refused without force, firmware/bulk opcodes are
 * refused even with it, and the gate ignores the app code. */
#include "check.h"
#include "model.h"

static void test_table(void)
{
	const struct ed_model *m = ed_model_default();
	const struct ed_cmd *c = ed_cmd_find(m, "volume");

	CHECK(c != NULL && c->app == ED_APP_MAIN && c->query == 0x66 && c->set == 0x67);
	c = ed_cmd_find(m, "subout");
	CHECK(c != NULL && c->app == ED_APP_SUB && c->query == 0x13 && c->set == 0x14);
	CHECK(ed_cmd_find(m, "nope") == NULL);
	CHECK(ed_cmd_find(m, "") == NULL);
	CHECK(ed_cmd_by_opcode(m, ED_APP_MAIN, 0x67) == ed_cmd_find(m, "volume"));
	CHECK(ed_cmd_by_opcode(m, ED_APP_MAIN, 0x01) == ed_cmd_find(m, "now-playing-title"));
	CHECK(ed_cmd_by_opcode(m, ED_APP_MAIN, 0x55) == NULL);
	/* every name is unique */
	for (size_t i = 0; i < m->ncmds; i++)
		CHECK(ed_cmd_find(m, m->cmds[i].name) == &m->cmds[i]);
}

static void test_gate(void)
{
	const struct ed_model *m = ed_model_default();
	const uint8_t never[] = {0x10, 0x11, 0x12, 0x79, 0x7A, 0x7B, 0x88, 0x89, 0x8A};
	const uint8_t destructive[] = {0x07, 0xCE, 0xCD, 0xCF, 0xC7, 0xAE, 0xAF, 0xB0, 0xCA};

	CHECK(ed_gate_check(m, ED_APP_MAIN, 0x66) == ED_GATE_OK);
	CHECK(ed_gate_check(m, ED_APP_MAIN, 0x67) == ED_GATE_OK);
	CHECK(ed_gate_check(m, ED_APP_SUB, 0x13) == ED_GATE_OK);
	CHECK(ed_gate_check(m, ED_APP_SUB, 0x66) == ED_GATE_UNKNOWN); /* 66 is an EC opcode */
	CHECK(ed_gate_check(m, ED_APP_MAIN, 0x55) == ED_GATE_UNKNOWN);
	CHECK(ed_gate_check(m, 0x00, 0x66) == ED_GATE_UNKNOWN);
	/* pushed-only opcodes are logged, never sent */
	CHECK(ed_gate_check(m, ED_APP_MAIN, 0x01) == ED_GATE_UNKNOWN);
	for (size_t i = 0; i < sizeof(never); i++) {
		CHECK(ed_gate_check(m, ED_APP_MAIN, never[i]) == ED_GATE_NEVER);
		CHECK(ed_gate_check(m, ED_APP_SUB, never[i]) == ED_GATE_NEVER);
		CHECK(ed_gate_check(m, 0x42, never[i]) == ED_GATE_NEVER);
		CHECK(!ed_gate_allows(ed_gate_check(m, ED_APP_MAIN, never[i]), true));
	}
	for (size_t i = 0; i < sizeof(destructive); i++) {
		enum ed_gate g = ed_gate_check(m, ED_APP_MAIN, destructive[i]);

		CHECK(g == ED_GATE_NEEDS_FORCE);
		CHECK(ed_gate_check(m, ED_APP_SUB, destructive[i]) == ED_GATE_NEEDS_FORCE);
		CHECK(!ed_gate_allows(g, false));
		CHECK(ed_gate_allows(g, true));
	}
	CHECK(ed_gate_allows(ED_GATE_OK, false));
	CHECK(!ed_gate_allows(ED_GATE_UNKNOWN, true));
	/* no table entry may sit on a gated opcode */
	for (size_t i = 0; i < m->ncmds; i++) {
		const struct ed_cmd *c = &m->cmds[i];

		if (c->query != ED_NONE)
			CHECK(ed_gate_check(m, c->app, (uint8_t)c->query) == ED_GATE_OK);
		if (c->set != ED_NONE)
			CHECK(ed_gate_check(m, c->app, (uint8_t)c->set) == ED_GATE_OK);
	}
}

static void test_volume(void)
{
	unsigned v = 999;

	CHECK(ed_volume_target("+1", 10, 30, &v) && v == 11);
	CHECK(ed_volume_target("-1", 10, 30, &v) && v == 9);
	CHECK(ed_volume_target("12", 10, 30, &v) && v == 12);
	CHECK(ed_volume_target("-5", 3, 30, &v) && v == 0);  /* clamp low */
	CHECK(ed_volume_target("+9", 28, 30, &v) && v == 30); /* clamp high */
	CHECK(ed_volume_target("99", 10, 30, &v) && v == 30); /* absolute clamp */
	CHECK(ed_volume_target("0", 10, 30, &v) && v == 0);
	v = 999;
	CHECK(!ed_volume_target("+1", 10, 0, &v) && v == 999); /* max 0: refuse */
	CHECK(!ed_volume_target("", 10, 30, &v));
	CHECK(!ed_volume_target("+", 10, 30, &v));
	CHECK(!ed_volume_target("--1", 10, 30, &v));
	CHECK(!ed_volume_target("1x", 10, 30, &v));
	CHECK(!ed_volume_target(" 1", 10, 30, &v));
	CHECK(!ed_volume_target("+99999999999999999999", 10, 30, &v));
	CHECK(!ed_volume_target(NULL, 10, 30, &v));
}

void test_model(void)
{
	test_table();
	test_gate();
	test_volume();
}
