/*
 * wfont -- how small can text get on this panel and still be read?
 *
 * The last open item in the overflow ladder (`scale-sketch.md` §4). Rung 2 is
 * "scale down to a measured floor", and the floor for text is legibility,
 * which nothing measured so far has set:
 *
 *   - `tools/evidence/fit/` found the objective signal -- the ink's peak
 *     intensity holds to font 10 and falls off at 8, where the 10x20 bitmap
 *     is being averaged into its background faster than it is being drawn.
 *     That says where the renderer starts to struggle, not where a person
 *     stops reading.
 *   - A PPM on a development monitor cannot answer it either, and that is
 *     arithmetic rather than opinion: that monitor is 4.0 px/mm against this
 *     panel's 9.7, so it has **41 %** of the pitch. A font-10 cell is 1.03 mm
 *     here and 4.1 monitor pixels there. It would show a smudge and we would
 *     set the floor two sizes too high. `finger_size/` got away with the same
 *     trick because a button outline is a SHAPE; type is the fine detail that
 *     pitch carries.
 *
 * So: draw it on the glass. Each row is the same string at a smaller size,
 * with its own size in it -- a row whose number you cannot read is below your
 * floor by definition, which makes the card self-scoring.
 *
 *     wfont > /w0
 *
 * Then look at the panel from wherever you would actually read it, and the
 * smallest row you can read is the answer. It draws once and exits; the panel
 * holds the picture.
 */
#include "modlib.h"
#include "whisker.h"

#define ROWS 7

static const short SIZE[ROWS] = { 24, 20, 16, 14, 12, 10, 8 };

typedef struct { const rv9_mod_env_t *env; int path; } out_t;

static void to_path(wsk_sink_t *s, const char *b, int n)
{
	out_t *o = (out_t *)s->ctx;
	o->env->write(o->path, b, (unsigned int)n);
}

/* "24 Temp 45.3 C" -- the size first, so it is the first thing to go. */
static void label(char *dst, int size)
{
	static const char *T = " Temp 45.3 C";
	int n = 0, k;

	if (size >= 10) dst[n++] = (char)('0' + size / 10);
	dst[n++] = (char)('0' + size % 10);
	for (k = 0; T[k] != '\0' && n < WSK_TEXT_MAX - 1; k++) dst[n++] = T[k];
	dst[n] = '\0';
}

__attribute__((section(".text.entry")))
int rv9_module_entry(const rv9_mod_env_t *env)
{
	wsk_widget_t row[ROWS];
	wsk_tree_t t;
	wsk_sink_t s;
	out_t out;
	uint32_t size = 0;
	int i, n;

	if (env == NULL || env->abi_version < 11) return 1;

	/* RV9_GS_SIZE cannot answer "is this a window" -- RBF answers it with a
	   file's length. Probe RV9_SVG_SS_ROWS, which only svgwin answers. */
	{
		uint32_t none = 0;
		if (env->setstat(RV9_STDOUT, RV9_SVG_SS_ROWS, &none) < 0) {
			m_say(env, RV9_STDERR, "wfont: that is not a window\n");
			return 2;
		}
	}
	if (env->getstat(RV9_STDOUT, RV9_GS_SIZE, &size) < 0) return 2;

	for (i = 0; i < ROWS; i++) {
		int k;
		for (k = 0; k < (int)sizeof row[i].id; k++) row[i].id[k] = '\0';
		for (k = 0; k < (int)sizeof row[i].text; k++) row[i].text[k] = '\0';
		row[i].kind = WSK_LABEL;
		row[i].size = SIZE[i];
		row[i].weight = 0;
		row[i].fill = WSK_INHERIT;
		row[i].ink = WSK_INHERIT;
		row[i].mark = 0;
		row[i].id[0] = 'f';
		row[i].id[1] = (char)('0' + i);
		label(row[i].text, SIZE[i]);
	}

	wsk_init(&t, WSK_COL, (int)(size & 0xFFFFu), (int)(size >> 16));
	t.pad = 2;
	t.gap = 2;
	t.bg = 0x0b1420u;
	t.ink = 0xe8f0f8u;      /* near-white: the best case, so the floor found
	                           here is the kindest one the panel offers */
	t.item = row;
	t.n = ROWS;

	out.env = env;
	out.path = RV9_STDOUT;
	s.put = to_path; s.ctx = &out; s.len = 0;
	n = wsk_render(&t, &s, 4096);
	if (n < 0) {
		m_say(env, RV9_STDERR, "wfont: the card does not fit\n");
		return 3;
	}

	m_say(env, RV9_STDERR, "wfont: ");
	m_num(env, RV9_STDERR, n);
	m_say(env, RV9_STDERR, " bytes. Read it from where you would really read"
	                       " it; the smallest row you can read is the floor.\n");
	return 0;
}
