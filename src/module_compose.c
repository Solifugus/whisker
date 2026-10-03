/*
 * The §5c constructs, under the module rules.
 *
 * module_shape.c proves the renderer links; this proves that composing does,
 * and it is a separate shape on purpose. Composition introduces exactly two
 * things a module has to be asked about:
 *
 *   - a FUNCTION POINTER passed as an argument. The sink has always worked
 *     this way, but wsk_compose takes the address of a static function, and
 *     a module may not hold a table of addresses in rodata (§4.1). An
 *     address materialised into a register with auipc+addi is fine; the same
 *     address stored in rodata is not, and only the double-link says which
 *     this compiled to.
 *   - an ARRAY OF TREES on the stack, assigned element by element. An array
 *     of POINTERS to trees would be the natural way to write a composite and
 *     is the construct that cost RV-9 a rewrite, twice -- so the API takes
 *     trees and this shape is where that choice gets checked rather than
 *     asserted.
 *
 * It is a program as well as a check: a toolbar across the top and a column
 * under it, in one document, with one background.
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

typedef struct { const rv9_mod_env_t *env; int path; } out_t;

static void to_path(wsk_sink_t *s, const char *b, int n)
{
	out_t *o = (out_t *)s->ctx;
	o->env->write(o->path, b, (unsigned int)n);
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
			m_say(env, RV9_STDERR, "wcompose: that is not a window\n");
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

	wsk_doc_init(&c.d, &part[0]);
	c.part = part;
	c.n = 2;

	/* From the window, so the compiler cannot fold it away. See bar_body. */
	body = (h >= top + 80) ? comp_body : bar_body;

	s.put = to_path; s.ctx = &out; s.len = 0;
	if (wsk_compose(body, &c, &s, 4096) < 0) {
		m_say(env, RV9_STDERR, "wcompose: the panel does not fit\n");
		return 3;
	}

	/* And a banded repaint of a widget in ONE stack, with both stacks still
	   offered to the document -- which is the thing §5c is actually for. */
	ids[0] = "stop";
	if (wsk_doc_rows(&c.d, &part[0], ids, 1) != WSK_OK) return 4;
	{
		uint32_t v = ((uint32_t)c.d.y0 << 16) | ((uint32_t)c.d.y1 & 0xFFFFu);
		if (env->setstat(RV9_STDOUT, RV9_SVG_SS_ROWS, &v) < 0) {
			m_say(env, RV9_STDERR, "wcompose: this window cannot clip rows\n");
			return 5;
		}
	}
	bar[1].mark = WSK_MARK_CORNERS;
	bar[1].ink = 0xffe0e0u;
	s.put = to_path; s.ctx = &out; s.len = 0;
	wsk_compose(body, &c, &s, 4096);

	env->sleep_ms(60);

	{
		uint32_t v = ((uint32_t)c.d.y0 << 16) | ((uint32_t)c.d.y1 & 0xFFFFu);
		env->setstat(RV9_STDOUT, RV9_SVG_SS_ROWS, &v);
	}
	bar[1].mark = 0;
	bar[1].ink = WSK_INHERIT;
	s.put = to_path; s.ctx = &out; s.len = 0;
	wsk_compose(body, &c, &s, 4096);

	return 0;
}
