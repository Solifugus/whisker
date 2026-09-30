/*
 * The notation, inside an RV-9 module.
 *
 * This is the check that decides whether any of it may run on the board, and
 * it is a build rather than a test: no libc, no .data, no .bss, no 64-bit
 * division, and the same blob whatever address it is linked at. A widget
 * dispatch table would fail here, and so would a `static const char *` of
 * kind names -- which is why wsk_kind_name returns its literals from code.
 *
 * It parses a panel, applies a patch, and writes out the ids that moved,
 * which is the whole loop a program on RV-9 would run.
 */
#include "modlib.h"
#include "wsk_note.h"

__attribute__((section(".text.entry")))
int rv9_module_entry(const rv9_mod_env_t *env)
{
	wsk_node_t nodes[16], pnodes[4];
	char pool[256], ppool[64], out[512];
	wsk_doc_t panel, patch;
	wsk_err_t err;
	int changed[8], nc, n;

	if (env == NULL) return 1;

	panel.node = nodes; panel.node_cap = 16; panel.nodes = 0;
	panel.pool = pool;  panel.pool_cap = (int)sizeof pool; panel.pool_len = 0;
	patch.node = pnodes; patch.node_cap = 4; patch.nodes = 0;
	patch.pool = ppool; patch.pool_cap = (int)sizeof ppool; patch.pool_len = 0;

	n = wsk_parse(".col pad=4 gap=4\n"
	              "..lbl title \"Settings\"\n"
	              "..btn ok h=64 \"OK\"\n"
	              "..btn cancel h=64 \"Cancel\"\n", 82, &panel, &err);
	if (n < 0) { m_say(env, RV9_STDERR, "panel: bad line\n"); return 2; }

	n = wsk_parse_patch("..btn ok \"Confirm\"\n", 19, &patch, &err);
	if (n < 0) { m_say(env, RV9_STDERR, "patch: bad line\n"); return 3; }

	nc = wsk_merge(&panel, &patch, changed, 8, &err);
	if (nc < 0) { m_say(env, RV9_STDERR, "patch names nothing here\n"); return 4; }

	for (int i = 0; i < nc; i++) {
		m_say(env, RV9_STDOUT, wsk_str(&panel, panel.node[changed[i]].name));
		m_say(env, RV9_STDOUT, "\n");
	}

	n = wsk_emit(&panel, out, (int)sizeof out);
	if (n > 0 && n < (int)sizeof out) env->write(RV9_STDOUT, out, (unsigned)n);
	return 0;
}
