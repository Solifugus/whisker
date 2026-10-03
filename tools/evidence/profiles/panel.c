/*
 * The subject of the measurement: the smallest honest whisker program.
 *
 * A label and two buttons, laid out and written to /w0 once. It names no
 * field, no mark, no hit test, no composition and no clipped repaint -- so
 * every byte of whisker in the linked binary is either something this
 * program uses or something it is being made to carry.
 *
 * The tree is cloned into RAM rather than drawn straight from rodata, which
 * is what a real caller does and what stops gcc specialising the renderer
 * into something no caller would get.
 */
#include "modlib.h"
#include "whisker.h"

static const wsk_widget_t PANEL[] = {
	{ .kind = WSK_LABEL,  .id = "title",  .text = "Settings" },
	{ .kind = WSK_BUTTON, .id = "ok",     .text = "OK" },
	{ .kind = WSK_BUTTON, .id = "cancel", .text = "Cancel" },
};

typedef struct { const rv9_mod_env_t *env; int path; } out_t;

static void to_path(wsk_sink_t *s, const char *b, int n)
{
	out_t *o = (out_t *)s->ctx;
	o->env->write(o->path, b, (unsigned int)n);
}

__attribute__((section(".text.entry")))
int rv9_module_entry(const rv9_mod_env_t *env)
{
	wsk_widget_t live[3];
	wsk_tree_t t;
	wsk_sink_t s;
	out_t out;
	uint32_t size = 0;

	if (env == NULL || env->abi_version < 11) return 1;
	{
		uint32_t none = 0;
		if (env->setstat(RV9_STDOUT, RV9_SVG_SS_ROWS, &none) < 0) return 2;
	}
	if (env->getstat(RV9_STDOUT, RV9_GS_SIZE, &size) < 0) return 2;

	wsk_clone(live, PANEL, 3);
	wsk_init(&t, WSK_COL, (int)(size & 0xFFFFu), (int)(size >> 16));
	t.pad = 4; t.gap = 4; t.item = live; t.n = 3;

	out.env = env; out.path = RV9_STDOUT;
	s.put = to_path; s.ctx = &out; s.len = 0;
	if (wsk_render(&t, &s, 4096) < 0) return 3;
	return 0;
}
