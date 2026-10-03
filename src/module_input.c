/*
 * A field, under the module rules.
 *
 * It is NOT staged in RV-9's tree and it is not meant to run on the C5: D6
 * says `input` goes only to hosts with an input device, and the C5 has none.
 * It is here so that the field emitter is held to the same build as everything
 * else -- no libc, no .data, no .bss, no 64-bit division, and the same bytes
 * at two link bases -- because the moment whisker ships a widget kind that has
 * never been through that build is the moment it stops being a thing a module
 * may use. The P4 runs RV-9 too.
 *
 * What it adds over module_compose.c: wsk__field, which does arithmetic on a
 * string the program owns and writes a <text> whose contents come from RAM
 * rather than rodata. That is the first time whisker emits something it did
 * not read out of a constant, and it is worth knowing it links.
 */
#include "modlib.h"
#include "whisker.h"

static const wsk_widget_t FORM[] = {
	{ .kind = WSK_LABEL,  .id = "prompt", .text = "Name", .size = 28 },
	{ .kind = WSK_FIELD,  .id = "entry",  .text = "",     .size = 48 },
	{ .kind = WSK_BUTTON, .id = "save",   .text = "Save", .size = 48 },
};

typedef struct { const rv9_mod_env_t *env; int path; } out_t;

static void to_path(wsk_sink_t *s, const char *b, int n)
{
	out_t *o = (out_t *)s->ctx;
	o->env->write(o->path, b, (unsigned int)n);
}

/* A character appended by the program, which is where a field's text comes
   from: whisker holds none of it (D5). */
static void append(wsk_widget_t *w, char c)
{
	int n = 0;
	while (w->text[n] != '\0') n++;
	if (n >= WSK_TEXT_MAX - 1) return;
	w->text[n] = c;
	w->text[n + 1] = '\0';
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
	int y0 = 0, y1 = 0, i;

	if (env == NULL || env->abi_version < 11) return 1;
	/*
	 * "Is this a window" is not a question RV9_GS_SIZE can answer -- it is
	 * the generic how-big-is-this code, and RBF answers it with a file's
	 * length, so `winput > /r0/x` laid itself out on a 0x0 screen and
	 * wrote nonsense. Probe RV9_SVG_SS_ROWS, which only svgwin answers, and
	 * which 0,0 means "all of it" to -- so the probe changes nothing.
	 * (RV-9's session found this in wflick on 2026-10-02 and fixed it in
	 * their copies; these are the originals those were taken from.)
	 */
	{
		uint32_t none = 0;
		if (env->setstat(RV9_STDOUT, RV9_SVG_SS_ROWS, &none) < 0) {
			m_say(env, RV9_STDERR, "winput: that is not a window\n");
			return 2;
		}
	}
	if (env->getstat(RV9_STDOUT, RV9_GS_SIZE, &size) < 0) return 2;

	wsk_clone(live, FORM, 3);
	wsk_init(&t, WSK_COL, (int)(size & 0xFFFFu), (int)(size >> 16));
	t.pad = 6;
	t.gap = 6;
	t.bg = 0x10203au;
	t.cross_max = 480;
	t.item = live;
	t.n = 3;
	live[1].mark = WSK_MARK_CARET;

	out.env = env;
	out.path = RV9_STDOUT;
	s.put = to_path; s.ctx = &out; s.len = 0;
	if (wsk_render(&t, &s, 4096) < 0) {
		m_say(env, RV9_STDERR, "winput: the panel does not fit\n");
		return 3;
	}

	ids[0] = "entry";
	if (wsk_rows(&t, ids, 1, &y0, &y1) != WSK_OK) return 4;

	/* Typed, one character at a time, each one a banded repaint of the field
	   alone -- the loop host/linux/input.c runs from a mouse. */
	for (i = 0; i < 8; i++) {
		uint32_t v = ((uint32_t)y0 << 16) | ((uint32_t)y1 & 0xFFFFu);
		if (env->setstat(RV9_STDOUT, RV9_SVG_SS_ROWS, &v) < 0) {
			m_say(env, RV9_STDERR, "winput: this window cannot clip rows\n");
			return 5;
		}
		append(&live[1], (char)('a' + i));
		s.put = to_path; s.ctx = &out; s.len = 0;
		wsk_render_clipped(&t, ids, 1, &s, 4096);
		env->sleep_ms(120);
	}

	return 0;
}
