/*
 * The same composite as src/module_compose.c, with the ONE difference this
 * measures: a sink that buffers the whole document and writes it once.
 *
 * It exists because of something RV-9's session found on 2026-10-02 while
 * bisecting two stack declarations that were both wrong. Drawing to /w0
 * renders on the WRITER's stack, and whisker streams -- so the put() that
 * completes a document runs RV-9's entire rasteriser with every one of
 * whisker's layout frames still live beneath it. The two depths add. Their
 * closing note was "a buffering sink would pay the two depths separately and
 * could declare a smaller stack. Worth a measurement rather than a decision."
 *
 * This is that measurement, and the buffer is deliberately NOT a local array
 * and NOT .bss. A module may have neither 4 KB of stack to spare nor a .bss
 * at all, so the only place it can go is the per-instance storage the loader
 * zeroes and hands over in env->statics -- which is declared in build.conf
 * as static_size, and which counts against the same process footprint the
 * stack does (rv9/module.h: "its own stack, statics and descriptor"). That
 * is what makes this a trade rather than a saving, and what the numbers are
 * for.
 *
 * Everything else is module_compose.c verbatim. `./run.sh` prints the diff
 * so that claim is checkable rather than asserted.
 */
#include "modlib.h"
#include "whisker.h"

static const wsk_widget_t BAR[] = {
	{ .kind = WSK_BUTTON, .id = "back", .text = "Back", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "stop", .text = "STOP", .weight = 1,
	  .fill = 0x8f2a2au },
	{ .kind = WSK_BUTTON, .id = "go",   .text = "Go",   .weight = 1 },
};

static const wsk_widget_t BODY[] = {
	{ .kind = WSK_BUTTON, .id = "confirm", .text = "CONFIRM", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "cancel",  .text = "Cancel",  .weight = 1 },
};

typedef struct {
	const rv9_mod_env_t *env;
	int                  path;
	char                *buf;    /* env->statics, static_size bytes */
	int                  cap;
	int                  len;
} out_t;

/*
 * The buffering half of the trade. It copies rather than writes, so no frame
 * of RV-9's rasteriser is ever live beneath whisker's layout frames.
 *
 * It cannot overrun: wsk_compose counts the document with put == 0 and
 * refuses it against the budget before this is called once, so the caller
 * passes a budget no larger than cap. The check is here anyway, because a
 * sink that trusts its caller about a length is the shape of the bug it
 * would be hiding.
 */
static void to_buf(wsk_sink_t *s, const char *b, int n)
{
	out_t *o = (out_t *)s->ctx;
	int i;

	if (n > o->cap - o->len) n = o->cap - o->len;
	for (i = 0; i < n; i++) o->buf[o->len + i] = b[i];
	o->len += n;
}

/* And the write, from the top, with nothing of whisker's left on the stack. */
static void flush(out_t *o)
{
	if (o->len > 0) o->env->write(o->path, o->buf, (unsigned int)o->len);
	o->len = 0;
}

/* What wsk_compose runs twice. It reads the context and never writes it,
   because the byte count of the first run is what the second is admitted on. */
typedef struct { wsk_doc_t d; const wsk_tree_t *part; int n; } comp_t;

static int comp_body(wsk_sink_t *s, void *ctx)
{
	comp_t *c = (comp_t *)ctx;
	int i, e;

	wsk_open(&c->d, s);
	for (i = 0; i < c->n; i++) {
		e = wsk_part(&c->d, &c->part[i], s);
		if (e != WSK_OK) return e;
	}
	wsk_shut(s);
	return WSK_OK;
}

/*
 * The same, minus the second stack -- what a panel too short for both would
 * draw, and the reason there are two bodies at all.
 *
 * With one body wsk_compose const-propagates to a direct call and the address
 * is never materialised, so the double-link proves nothing about the one
 * construct this shape exists to check. Checked by disassembling it: `jal
 * comp_body` inside wsk_compose.constprop.0, no auipc in sight. Choosing
 * between two bodies on something only the window knows forces the address
 * into a register, which is the thing that has to be position independent.
 *
 * That is the house rule about controls -- a test satisfied by a harness that
 * ran nothing is not a test -- applied to a build rather than to an assertion.
 */
static int bar_body(wsk_sink_t *s, void *ctx)
{
	comp_t *c = (comp_t *)ctx;

	wsk_open(&c->d, s);
	{
		int e = wsk_part(&c->d, &c->part[0], s);
		if (e != WSK_OK) return e;
	}
	wsk_shut(s);
	return WSK_OK;
}

__attribute__((section(".text.entry")))
int rv9_module_entry(const rv9_mod_env_t *env)
{
	wsk_widget_t bar[3], keys[2];
	wsk_tree_t part[2];
	wsk_sink_t s;
	comp_t c;
	out_t out;
	const char *ids[1];
	wsk_body_t body;
	uint32_t size = 0;
	int w, h, top;

	if (env == NULL || env->abi_version < 11) return 1;
	/*
	 * "Is this a window" is not a question RV9_GS_SIZE can answer -- it is
	 * the generic how-big-is-this code, and RBF answers it with a file's
	 * length, so `wcompose > /r0/x` laid itself out on a 0x0 screen and
	 * wrote nonsense. Probe RV9_SVG_SS_ROWS, which only svgwin answers, and
	 * which 0,0 means "all of it" to -- so the probe changes nothing.
	 * (RV-9's session found this in wflick on 2026-10-02 and fixed it in
	 * their copies; these are the originals those were taken from.)
	 */
	{
		uint32_t none = 0;
		if (env->setstat(RV9_STDOUT, RV9_SVG_SS_ROWS, &none) < 0) {
			m_say(env, RV9_STDERR, "wbuffer: that is not a window\n");
			return 2;
		}
	}
	if (env->getstat(RV9_STDOUT, RV9_GS_SIZE, &size) < 0) return 2;
	w = (int)(size & 0xFFFFu);
	h = (int)(size >> 16);
	top = (h < 200) ? 60 : 96;           /* a toolbar, not a proportion (§5) */

	wsk_clone(bar, BAR, 3);
	wsk_clone(keys, BODY, 2);

	wsk_init(&part[0], WSK_ROW, w, h);
	part[0].pad = 6; part[0].gap = 6; part[0].bg = 0x10203au;
	wsk_area(&part[0], 0, 0, w, top);
	part[0].item = bar; part[0].n = 3;

	wsk_init(&part[1], WSK_COL, w, h);
	part[1].pad = 6; part[1].gap = 6; part[1].bg = 0x10203au;
	part[1].cross_max = 420;
	wsk_area(&part[1], 0, top, w, h - top);
	part[1].item = keys; part[1].n = 2;

	out.env = env;
	out.path = RV9_STDOUT;
	out.buf = (char *)env->statics;
	out.cap = (int)env->statics_size;
	out.len = 0;
	if (out.buf == 0 || out.cap < 4096) {
		m_say(env, RV9_STDERR, "wbuffer: needs static_size=4096\n");
		return 6;
	}

	wsk_doc_init(&c.d, &part[0]);
	c.part = part;
	c.n = 2;

	/* From the window, so the compiler cannot fold it away. See bar_body. */
	body = (h >= top + 80) ? comp_body : bar_body;

	s.put = to_buf; s.ctx = &out; s.len = 0;
	if (wsk_compose(body, &c, &s, out.cap) < 0) {
		m_say(env, RV9_STDERR, "wbuffer: the panel does not fit\n");
		return 3;
	}
	flush(&out);

	/* And a banded repaint of a widget in ONE stack, with both stacks still
	   offered to the document -- which is the thing §5c is actually for. */
	ids[0] = "stop";
	if (wsk_doc_rows(&c.d, &part[0], ids, 1) != WSK_OK) return 4;
	{
		uint32_t v = ((uint32_t)c.d.y0 << 16) | ((uint32_t)c.d.y1 & 0xFFFFu);
		if (env->setstat(RV9_STDOUT, RV9_SVG_SS_ROWS, &v) < 0) {
			m_say(env, RV9_STDERR, "wbuffer: this window cannot clip rows\n");
			return 5;
		}
	}
	bar[1].mark = WSK_MARK_CORNERS;
	bar[1].ink = 0xffe0e0u;
	s.put = to_buf; s.ctx = &out; s.len = 0;
	wsk_compose(body, &c, &s, out.cap);
	flush(&out);

	env->sleep_ms(60);

	{
		uint32_t v = ((uint32_t)c.d.y0 << 16) | ((uint32_t)c.d.y1 & 0xFFFFu);
		env->setstat(RV9_STDOUT, RV9_SVG_SS_ROWS, &v);
	}
	bar[1].mark = 0;
	bar[1].ink = WSK_INHERIT;
	s.put = to_buf; s.ctx = &out; s.len = 0;
	wsk_compose(body, &c, &s, out.cap);
	flush(&out);

	return 0;
}
