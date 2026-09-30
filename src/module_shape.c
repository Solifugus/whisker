/*
 * flick, generated -- and the check that decides whether any of this may run
 * on the board.
 *
 *     wflick > /w0        a panel drawn, a button flashed, and drawn again
 *
 * It is a build as much as a program: no libc, no .data, no .bss, no 64-bit
 * division, and the same blob whatever address it is linked at. A widget
 * dispatch table would fail here, and so would a panel held behind
 * `const char *` labels -- which is why a widget carries its text inline and
 * the tree that points at it is built on the stack.
 *
 * Nothing is buffered: the sink hands each piece to env->write as whisker
 * produces it, so a 4 KB document never needs 4 KB of a program's memory.
 */
#include "modlib.h"
#include "whisker.h"

/* rodata: inline text, no addresses, so this links (review §3.1). */
static const wsk_widget_t PANEL[] = {
	{ WSK_LABEL,  "title",   "tap test", 0, 0, WSK_INHERIT, WSK_INHERIT },
	{ WSK_BUTTON, "confirm", "CONFIRM",  0, 0, WSK_INHERIT, WSK_INHERIT },
	{ WSK_BUTTON, "cancel",  "Cancel",   0, 0, WSK_INHERIT, WSK_INHERIT },
};

typedef struct { const rv9_mod_env_t *env; int path; } out_t;

static void to_path(wsk_sink_t *s, const char *b, int n)
{
	out_t *o = (out_t *)s->ctx;
	o->env->write(o->path, b, (unsigned int)n);
}

static int clip_rows(const rv9_mod_env_t *env, int y0, int y1)
{
	uint32_t v = ((uint32_t)y0 << 16) | ((uint32_t)y1 & 0xFFFFu);
	return env->setstat(RV9_STDOUT, RV9_SVG_SS_ROWS, &v);
}

__attribute__((section(".text.entry")))
int rv9_module_entry(const rv9_mod_env_t *env)
{
	wsk_widget_t live[3];
	wsk_tree_t t;
	wsk_sink_t s;
	out_t out;
	const char *ids[1];
	uint32_t size = 0;
	int y0 = 0, y1 = 0;

	if (env == NULL || env->abi_version < 11) return 1;

	/* Pixels, not characters, and not assumed: a whisker that guesses works
	   at 320x172 and clips the wrong strip at 1024x600. */
	if (env->getstat(RV9_STDOUT, RV9_GS_SIZE, &size) < 0) {
		m_say(env, RV9_STDERR, "wflick: that is not a window\n");
		return 2;
	}

	wsk_clone(live, PANEL, 3);

	wsk_init(&t, WSK_COL, (int)(size & 0xFFFFu), (int)(size >> 16));
	t.pad = 4;
	t.gap = 4;
	t.bg = 0x10203au;
	t.item = live;
	t.n = 3;

	out.env = env;
	out.path = RV9_STDOUT;
	s.put = to_path; s.ctx = &out; s.len = 0;
	if (wsk_render(&t, &s, 4096) < 0) {
		m_say(env, RV9_STDERR, "wflick: the panel does not fit\n");
		return 3;
	}

	ids[0] = "confirm";
	if (wsk_rows(&t, ids, 1, &y0, &y1) != WSK_OK) return 4;

	/* If the window will not clip, say so rather than reporting a strip time
	   that is really another full redraw. */
	if (clip_rows(env, y0, y1) < 0) {
		m_say(env, RV9_STDERR, "wflick: this window cannot clip rows\n");
		return 5;
	}

	live[1].fill = 0x8fd0ffu;
	live[1].ink = 0x112233u;
	s.put = to_path; s.ctx = &out; s.len = 0;
	wsk_render_clipped(&t, ids, 1, &s, 4096);

	env->sleep_ms(60);                      /* long enough for an eye */

	clip_rows(env, y0, y1);
	live[1].fill = WSK_INHERIT;
	live[1].ink = WSK_INHERIT;
	s.put = to_path; s.ctx = &out; s.len = 0;
	wsk_render_clipped(&t, ids, 1, &s, 4096);

	return 0;
}
