/*
 * Iteration 1's done-criteria, as a suite (design.md §5).
 *
 * Pure: no device, no driver, no hardware. picture.c does the part that
 * needs RV-9's rasteriser, because a picture can be geometrically perfect
 * and still be wrong in a way only an eye catches.
 */
#include "whisker.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void ok(int cond, const char *what)
{
	printf("%s  %s\n", cond ? "ok  " : "FAIL", what);
	if (!cond) fails++;
}

/* A sink that fills an array: the buffer form, which is just a sink. */
typedef struct { char *b; int cap; } buf_t;

static void to_buf(wsk_sink_t *s, const char *b, int n)
{
	buf_t *o = (buf_t *)s->ctx;
	for (int i = 0; i < n; i++)
		if (s->len + i < o->cap - 1) o->b[s->len + i] = b[i];
}

static int render_into(const wsk_tree_t *t, char *out, int cap, int budget)
{
	buf_t o; wsk_sink_t s;
	o.b = out; o.cap = cap;
	s.put = to_buf; s.ctx = &o; s.len = 0;
	int r = wsk_render(t, &s, budget);
	out[(r > 0 && r < cap) ? r : 0] = '\0';
	return r;
}

static int clip_into(const wsk_tree_t *t, const char *const *ids, int n,
                     char *out, int cap, int budget)
{
	buf_t o; wsk_sink_t s;
	o.b = out; o.cap = cap;
	s.put = to_buf; s.ctx = &o; s.len = 0;
	int r = wsk_render_clipped(t, ids, n, &s, budget);
	out[(r > 0 && r < cap) ? r : 0] = '\0';
	return r;
}

static int has(const char *hay, const char *needle) { return strstr(hay, needle) != 0; }

static int count(const char *hay, const char *needle)
{
	int k = 0;
	for (const char *p = hay; (p = strstr(p, needle)) != 0; p++) k++;
	return k;
}

/*
 * Composition (design.md §5c). The body emits and does nothing else -- it
 * reads the context and never writes it, which is the constraint wsk_compose
 * names, since it is called twice and the count has to describe the document
 * that actually gets written.
 *
 * `part` is an array of trees, not of pointers to them: a table of addresses
 * in rodata is what a module cannot link (§4.1), and composition must not be
 * the thing that reintroduces one.
 */
typedef struct { wsk_doc_t d; const wsk_tree_t *part; int n; } comp_t;

static int comp_body(wsk_sink_t *s, void *ctx)
{
	comp_t *c = (comp_t *)ctx;

	wsk_open(&c->d, s);
	for (int i = 0; i < c->n; i++) {
		int e = wsk_part(&c->d, &c->part[i], s);
		if (e != WSK_OK) return e;
	}
	wsk_shut(s);
	return WSK_OK;
}

static int compose_into(comp_t *c, char *out, int cap, int budget)
{
	buf_t o; wsk_sink_t s;
	o.b = out; o.cap = cap;
	s.put = to_buf; s.ctx = &o; s.len = 0;
	out[0] = '\0';
	int r = wsk_compose(comp_body, c, &s, budget);
	out[(r > 0 && r < cap) ? r : 0] = '\0';
	return r;
}

/* ---- the panels ---- */

static const wsk_widget_t SETTINGS[] = {
	{ .kind = WSK_LABEL, .id = "title", .text = "Settings" },
	{ .kind = WSK_BUTTON, .id = "ok", .text = "OK" },
	{ .kind = WSK_BUTTON, .id = "cancel", .text = "Cancel" },
};

static const wsk_widget_t BIG[] = {
	{ .kind = WSK_BUTTON, .id = "pump", .text = "Pump" },
	{ .kind = WSK_BUTTON, .id = "valve_a", .text = "Valve A" },
	{ .kind = WSK_BUTTON, .id = "valve_b", .text = "Valve B" },
	{ .kind = WSK_BUTTON, .id = "heater", .text = "Heater" },
	{ .kind = WSK_BUTTON, .id = "fan", .text = "Fan" },
	{ .kind = WSK_BUTTON, .id = "alarm", .text = "Alarm" },
	{ .kind = WSK_BUTTON, .id = "purge", .text = "Purge" },
	{ .kind = WSK_BUTTON, .id = "drain", .text = "Drain" },
	{ .kind = WSK_BUTTON, .id = "fill", .text = "Fill" },
	{ .kind = WSK_BUTTON, .id = "vent", .text = "Vent" },
	{ .kind = WSK_BUTTON, .id = "reset", .text = "Reset" },
	{ .kind = WSK_BUTTON, .id = "start", .text = "Start" },
};

/* BIG at a size twelve of them can take in one column at 600 rows -- 44 each
   with the gaps and padding is 580. At the default 64 they need 820, and
   until wsk_check refused an overflowing stack (roadmap 4.3) the "dozen
   controls" checks below drew three and a half of them off the screen. */
static const wsk_widget_t *dozen(void)
{
	static wsk_widget_t d[12];
	memcpy(d, BIG, sizeof d);
	for (int i = 0; i < 12; i++) d[i].size = 44;
	return d;
}

static const wsk_widget_t TOOLBAR[] = {
	{ .kind = WSK_BUTTON, .id = "back", .text = "Back",   .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "stop", .text = "Stop",   .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "go",   .text = "Go",     .weight = 1 },
};

/* Two stacks that share one 320x172 screen: a menu down the left, readouts
   down the right. The sizes are chosen so that one band intersects both
   stacks and another intersects only the menu -- §5c criterion 2 needs both
   cases to occur, and a layout where they cannot is a test that proves
   nothing. */
static const wsk_widget_t MENU[] = {
	{ .kind = WSK_BUTTON, .id = "m_run",  .text = "Run",  .size = 48 },
	{ .kind = WSK_BUTTON, .id = "m_stop", .text = "Stop", .size = 48 },
	{ .kind = WSK_BUTTON, .id = "m_quit", .text = "Quit", .size = 48 },
};

static const wsk_widget_t SIDE[] = {
	{ .kind = WSK_LABEL, .id = "s_volts", .text = "12.4 V", .size = 40 },
	{ .kind = WSK_LABEL, .id = "s_temp",  .text = "41 C",   .size = 40 },
};

/* A keyboard, which is why §5c exists: 36 keys were 94 % of SRC_MAX as four
   separate documents (gbasic-gap.md). One row per stack, four stacks. */
static const wsk_widget_t KEYS_1[] = {
	{ .kind = WSK_BUTTON, .id = "k_q", .text = "Q", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_w", .text = "W", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_e", .text = "E", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_r", .text = "R", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_t", .text = "T", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_y", .text = "Y", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_u", .text = "U", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_i", .text = "I", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_o", .text = "O", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_p", .text = "P", .weight = 1 },
};

static const wsk_widget_t KEYS_2[] = {
	{ .kind = WSK_BUTTON, .id = "k_a", .text = "A", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_s", .text = "S", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_d", .text = "D", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_f", .text = "F", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_g", .text = "G", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_h", .text = "H", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_j", .text = "J", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_k", .text = "K", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_l", .text = "L", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_ent", .text = "ENT", .weight = 1 },
};

static const wsk_widget_t KEYS_3[] = {
	{ .kind = WSK_BUTTON, .id = "k_z", .text = "Z", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_x", .text = "X", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_c", .text = "C", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_v", .text = "V", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_b", .text = "B", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_n", .text = "N", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_m", .text = "M", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_dot", .text = ".", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_del", .text = "DEL", .weight = 1 },
};

static const wsk_widget_t KEYS_4[] = {
	{ .kind = WSK_BUTTON, .id = "k_sh", .text = "SHIFT", .weight = 2 },
	{ .kind = WSK_BUTTON, .id = "k_num", .text = "123", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_spc", .text = "SPACE", .weight = 4 },
	{ .kind = WSK_BUTTON, .id = "k_lt", .text = "-", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_gt", .text = "+", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_esc", .text = "ESC", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "k_tab", .text = "TAB", .weight = 1 },
};

/* A field and the things around it (design.md §5d). The text is set at
   runtime, because a field's characters belong to the caller -- which is the
   whole of D5 showing up in a panel declaration. */
static const wsk_widget_t FORM[] = {
	{ .kind = WSK_LABEL, .id = "prompt", .text = "Name", .size = 28 },
	{ .kind = WSK_FIELD, .id = "name", .text = "", .size = 44 },
	{ .kind = WSK_BUTTON, .id = "save", .text = "Save", .size = 44 },
};

/* Deliberately not band-aligned: 30 rows from an odd offset, so the clip has
   to snap and the backing has to follow it. */
static const wsk_widget_t RAGGED[] = {
	{ .kind = WSK_BUTTON, .id = "a", .text = "A", .size = 30 },
	{ .kind = WSK_BUTTON, .id = "b", .text = "B", .size = 30 },
	{ .kind = WSK_BUTTON, .id = "c", .text = "C", .size = 30 },
};

/* §5f: a window that is a tree. A toolbar row, a body column holding a
   field, a button and a row of its own, and a status row -- each child tree
   named by its index in NEST, so these panels stay static (§5f). */
static const wsk_widget_t WIN[] = {
	{ .kind = WSK_STACK, .id = "toolbar", .sub = 0 },
	{ .kind = WSK_STACK, .id = "body",    .sub = 1, .weight = 1 },
	{ .kind = WSK_STACK, .id = "status",  .sub = 2, .fill = 0x203040u },
};
static const wsk_widget_t TB[] = {
	{ .kind = WSK_BUTTON, .id = "new",   .text = "New" },
	{ .kind = WSK_BUTTON, .id = "save",  .text = "Save" },
	{ .kind = WSK_LABEL,  .id = "title", .text = "Untitled", .weight = 1 },
};
static const wsk_widget_t BODY[] = {
	{ .kind = WSK_FIELD,  .id = "name" },
	{ .kind = WSK_BUTTON, .id = "ok",   .text = "OK" },
	{ .kind = WSK_STACK,  .id = "opts", .sub = 3 },
};
static const wsk_widget_t STATUS[] = {
	{ .kind = WSK_LABEL, .id = "msg", .text = "Ready", .weight = 1 },
	{ .kind = WSK_LABEL, .id = "pos", .text = "1:1" },
};
static const wsk_widget_t OPTS[] = {
	{ .kind = WSK_BUTTON, .id = "a", .text = "A" },
	{ .kind = WSK_BUTTON, .id = "b", .text = "B" },
};

static int rect_is(wsk_rect_t r, int x, int y, int w, int h)
{
	return r.x == x && r.y == y && r.w == w && r.h == h;
}

/* A widget's rectangle at any depth, through the same lookup wsk_rows uses. */
static wsk_rect_t rect_of(const wsk_tree_t *t, const char *key)
{
	wsk_tree_t at;
	wsk_rect_t r = { -1, -1, -1, -1 };
	int i = wsk__lookup(t, key, &at, 0);
	if (i >= 0) r = wsk_rect(&at, i);
	return r;
}

/* §5p: a program's own tree, as Studio keeps one -- the model is the
   program's, and what whisker is handed is the flat list of rows it shows. */
typedef struct { const char *name; unsigned char depth, kids, open; } tnode_t;
static tnode_t TNODES[] = {
	{ "src",       0, 1, 0 },
	{ "whisker.h", 1, 0, 0 },
	{ "test.c",    1, 0, 0 },
	{ "docs",      0, 1, 0 },
	{ "design.md", 1, 0, 0 },
	{ "old",       1, 1, 0 },
	{ "gtk.md",    2, 0, 0 },
	{ "README.md", 0, 0, 0 },
};
#define TN ((int)(sizeof TNODES / sizeof TNODES[0]))

/* The rows a closed parent hides are skipped; each row says + or - itself,
   in ASCII (§5p: the window draws 32..126). Returns how many, and which node
   each row is, which is the program's to keep. */
static int trows(wsk_widget_t *row, int *node)
{
	int n = 0, hide_below = 255;
	for (int i = 0; i < TN; i++) {
		tnode_t *t = &TNODES[i];
		if (t->depth > hide_below) continue;
		hide_below = 255;
		memset(&row[n], 0, sizeof row[n]);
		row[n].kind = WSK_BUTTON;
		row[n].size = 24;
		row[n].depth = t->depth;
		snprintf(row[n].id, WSK_ID_MAX, "n%d", i);
		snprintf(row[n].text, WSK_TEXT_MAX, "%s%s", t->kids ? (t->open ? "- " : "+ ") : "  ", t->name);
		node[n++] = i;
		if (t->kids && !t->open) hide_below = t->depth;
	}
	return n;
}

int main(void)
{
	static char doc[8192], clip[8192], other[8192];
	wsk_tree_t t;
	int n;

	/* ---- 1. the whole screen, and it fits ---- */
	wsk_init(&t, WSK_COL, 320, 172);
	t.pad = 4; t.gap = 4;
	t.item = SETTINGS; t.n = 3;

	n = render_into(&t, doc, sizeof doc, 4096);
	ok(n > 0, "the screen renders");
	ok(has(doc, "<svg viewBox=\"0 0 320 172\">") && has(doc, "</svg>"),
	   "a complete document, viewBox the device");
	ok(has(doc, ">Settings</text>") && has(doc, ">OK</text>") && has(doc, ">Cancel</text>"),
	   "every widget is in it");

	{
		wsk_tree_t big = t;
		big.item = dozen(); big.n = 12;
		big.w = 1024; big.h = 600;
		wsk_area(&big, 0, 0, 1024, 600);   /* t's area is 320x172 */
		n = render_into(&big, doc, sizeof doc, 4096);
		printf("     a dozen controls at 1024x600: %d bytes of the 4096 ceiling\n", n);
		ok(n > 0 && n < 4096, "a realistic panel of a dozen controls fits under SRC_MAX");
	}

	/* ---- 2. rows and the clipped document agree ---- */
	{
		const char *ids[1];
		int y0, y1, i;
		wsk_rect_t r;

		ids[0] = "ok";
		ok(wsk_rows(&t, ids, 1, &y0, &y1) == WSK_OK, "rows() answers for a known id");
		i = wsk_find(&t, "ok");
		r = wsk_rect(&t, i);
		ok(y0 <= r.y && y1 >= r.y + r.h, "the reported rows contain the widget");

		n = clip_into(&t, ids, 1, clip, sizeof clip, 4096);
		ok(n > 0 && n < render_into(&t, doc, sizeof doc, 4096),
		   "the clipped document is smaller than the screen");
		ok(has(clip, ">OK</text>"), "the widget asked for is in it");
		/* The control. Without it, "the clipped document is correct" is
		   satisfied by emitting the whole scene every time -- which is the
		   thing being optimised away. */
		ok(!has(clip, ">Settings</text>") && !has(clip, ">Cancel</text>"),
		   "CONTROL: a widget outside those rows is absent");

		{
			const char *two[2];
			two[0] = "title"; two[1] = "cancel";
			n = clip_into(&t, two, 2, clip, sizeof clip, 4096);
			ok(n > 0 && has(clip, ">Settings</text>") && has(clip, ">Cancel</text>")
			   && has(clip, ">OK</text>"),
			   "a set spanning two widgets carries what lies between them");
		}
	}

	/* ---- 3. the backing: its own rows, snapped, and not the screen ---- */
	{
		wsk_tree_t g;
		const char *ids[1];
		int y0, y1;
		char want[64];

		wsk_init(&g, WSK_COL, 320, 172);
		g.pad = 5; g.gap = 3;                 /* nothing lands on a band */
		g.item = RAGGED; g.n = 3;
		g.bg = 0x102030u;

		ids[0] = "b";
		wsk_rows(&g, ids, 1, &y0, &y1);
		{
			wsk_rect_t r = wsk_rect(&g, wsk_find(&g, "b"));
			ok(r.y % WSK_BAND != 0, "the widget deliberately does not start on a band");
			ok(y0 % WSK_BAND == 0 && y1 % WSK_BAND == 0 && y0 <= r.y && y1 >= r.y + r.h,
			   "rows() snaps outward to whole bands");
			printf("     widget rows %d..%d, snapped to %d..%d\n",
			       r.y, r.y + r.h, y0, y1);
		}

		n = clip_into(&g, ids, 1, clip, sizeof clip, 4096);
		snprintf(want, sizeof want, "<rect y=\"%d\" width=\"320\" height=\"%d\"",
		         y0, y1 - y0);
		ok(has(clip, want), "the backing covers exactly the snapped rows");
		snprintf(want, sizeof want, "height=\"%d\"", (int)g.h);
		ok(!has(clip, want), "CONTROL: and not the whole screen");
		ok(has(clip, "fill=\"#102030\""), "the backing carries the scene's colour, not the device's");
	}

	/* ---- 4. heights land on bands, and a button is a finger wide ---- */
	{
		wsk_tree_t g = t;
		g.item = SETTINGS; g.n = 3;
		for (int i = 0; i < g.n; i++) {
			wsk_rect_t r = wsk_rect(&g, i);
			char msg[96];
			snprintf(msg, sizeof msg, "%s is %d rows, a whole number of bands",
			         g.item[i].id, r.h);
			ok(r.h % WSK_BAND == 0, msg);
		}
		ok(wsk_rect(&g, 1).h == WSK_BTN_ROWS, "a button defaults to 64 rows, about a finger at 170 dpi");
	}

	/* ---- 5. the byte budget is refused before anything is written ---- */
	{
		wsk_tree_t g = t;
		int exact;
		g.item = dozen(); g.n = 12;
		g.w = 1024; g.h = 600;
		wsk_area(&g, 0, 0, 1024, 600);

		exact = render_into(&g, doc, sizeof doc, 0);
		ok(render_into(&g, doc, sizeof doc, exact - 1) == WSK_E_BUDGET,
		   "a document one byte over the budget is refused");
		ok(doc[0] == '\0', "and nothing was written -- not even a prefix");
		ok(render_into(&g, doc, sizeof doc, exact) == exact,
		   "CONTROL: the same document, given the bytes it needs, is written");
	}

	/* ---- 5b. a stack that does not fit its area is refused (roadmap 4.3) ----
	   Two buttons in 172 rows with pad 4 and gap 4: 80 + 4 + 80 is exactly
	   the 164 the area has. One row more and the second widget crosses the
	   end, which until 2026-10-24 was drawn past the area without a word. */
	{
		wsk_widget_t two[2];
		wsk_tree_t g;
		int which = -1;
		comp_t c;

		memset(two, 0, sizeof two);
		for (int i = 0; i < 2; i++) {
			two[i].kind = WSK_BUTTON;
			two[i].size = 80;
			two[i].fill = WSK_INHERIT;
			two[i].ink = WSK_INHERIT;
		}
		memcpy(two[0].id, "a", 2);
		memcpy(two[1].id, "b", 2);
		wsk_init(&g, WSK_COL, 320, 172);
		g.pad = 4; g.gap = 4;
		g.item = two; g.n = 2;

		ok(render_into(&g, doc, sizeof doc, 4096) > 0,
		   "CONTROL: a stack that exactly fills its area renders");

		two[1].size = 81;
		ok(render_into(&g, doc, sizeof doc, 4096) == WSK_E_FIT,
		   "one row more than the area holds is refused");
		ok(doc[0] == '\0', "and nothing was written");
		ok(wsk_check(&g, &which) == WSK_E_FIT && which == 1,
		   "and the refusal names the widget that crosses the end");

		two[1].size = 0;             /* the default: 64 rows */
		g.ah = 60;                   /* an area shorter than one button */
		g.n = 1;
		ok(render_into(&g, doc, sizeof doc, 4096) == WSK_E_FIT,
		   "an unsized button in an area shorter than 64 rows is refused -- wslop's marks");

		g.ah = 172; g.n = 2;
		two[1].size = 81;
		c.part = &g; c.n = 1;
		wsk_doc_init(&c.d, &g);
		ok(compose_into(&c, doc, sizeof doc, 4096) == WSK_E_FIT && doc[0] == '\0',
		   "a composite with one overflowing stack is refused whole");
		two[1].size = 80;
		ok(compose_into(&c, doc, sizeof doc, 4096) > 0,
		   "CONTROL: and the same composite, fitting, is written");
	}

	/* ---- 6. a label the window cannot draw ---- */
	{
		wsk_widget_t bad[2];
		wsk_tree_t g = t;

		memset(bad, 0, sizeof bad);
		bad[0].kind = WSK_BUTTON;    /* memset left everything inheriting */
		memcpy(bad[0].id, "ok", 3);
		memcpy(bad[0].text, "a < b", 6);
		bad[1] = bad[0];
		memcpy(bad[1].id, "two", 4);
		memcpy(bad[1].text, "caf\xc3\xa9", 6);

		g.item = bad; g.n = 1;
		ok(render_into(&g, doc, sizeof doc, 4096) == WSK_E_TEXT,
		   "a '<' in a label is refused (it hung /w0 until 2026-09-30)");
		{
			int which = -1;
			ok(wsk_check(&g, &which) == WSK_E_TEXT && which == 0,
			   "and wsk_check names which widget");
		}

		g.item = &bad[1]; g.n = 1;
		ok(render_into(&g, doc, sizeof doc, 4096) == WSK_E_TEXT,
		   "so is a byte outside 32..126 (it advances a cell and draws nothing)");

		memcpy(bad[0].text, "a &lt; b", 9);
		g.item = bad; g.n = 1;
		ok(render_into(&g, doc, sizeof doc, 4096) == WSK_E_TEXT,
		   "and so is the escaped form, which would draw four characters");

		memcpy(bad[0].text, "a to b", 7);
		ok(render_into(&g, doc, sizeof doc, 4096) > 0,
		   "CONTROL: the same label, legal, is drawn");
	}

	/* ---- 4a. the other axis, which D3 ratified and nothing had exercised ---- */
	{
		wsk_tree_t g;
		wsk_rect_t a, b, c;

		wsk_init(&g, WSK_ROW, 320, 80);
		g.pad = 4; g.gap = 4;
		g.item = TOOLBAR; g.n = 3;

		a = wsk_rect(&g, 0); b = wsk_rect(&g, 1); c = wsk_rect(&g, 2);
		ok(a.y == 4 && a.h == 72 && b.y == a.y && c.y == a.y,
		   "a row gives every widget the full height between the padding");
		ok(a.x == 4 && b.x == a.x + a.w + 4 && c.x == b.x + b.w + 4,
		   "and lays them out along the other axis, a gap apart");
		ok(c.x + c.w == 320 - 4,
		   "equal weights fill the width exactly, to the last pixel");
		printf("     row of three: %d + %d + %d = %d of %d usable\n",
		       a.w, b.w, c.w, a.w + b.w + c.w, 320 - 8 - 2 * 4);

		/* Unequal weights, and the rounding has to go somewhere. */
		{
			wsk_widget_t live[3];
			wsk_clone(live, TOOLBAR, 3);
			live[1].weight = 2;
			g.item = live;
			a = wsk_rect(&g, 0); b = wsk_rect(&g, 1); c = wsk_rect(&g, 2);
			ok(b.w > a.w && c.x + c.w == 320 - 4,
			   "a heavier child takes more, and the row still ends on the padding");
			g.item = TOOLBAR;
		}

		/* Sharing rows is what a row costs, and it is the whole of D3's
		   argument: repainting one button repaints its neighbours too,
		   because the clip is the full width of their rows. */
		{
			const char *ids[1];
			int y0, y1, bytes;
			ids[0] = "stop";
			wsk_rows(&g, ids, 1, &y0, &y1);
			bytes = clip_into(&g, ids, 1, clip, sizeof clip, 4096);
			ok(has(clip, ">Back</text>") && has(clip, ">Go</text>"),
			   "repainting one button in a row carries its neighbours -- they share its rows");
			printf("     one of a row: %d bytes, rows %d..%d (the whole row)\n",
			       bytes, y0, y1);
		}
	}

	/* ---- 4a2. the area, and why it is not the device ---- */
	{
		wsk_tree_t bar, full;
		wsk_rect_t in_bar, in_full;
		int y0, y1;
		const char *ids[1];

		wsk_init(&bar, WSK_ROW, 320, 172);
		bar.pad = 6; bar.gap = 6;
		bar.item = TOOLBAR; bar.n = 3;
		full = bar;                          /* the same stack, the whole screen */
		wsk_area(&bar, 0, 0, 320, 84);       /* a toolbar across the top */

		in_bar = wsk_rect(&bar, 1);
		in_full = wsk_rect(&full, 1);
		ok(in_bar.h == 84 - 12 && in_full.h == 172 - 12,
		   "the area bounds the stack; CONTROL: without it the stack is the screen");

		n = render_into(&bar, doc, sizeof doc, 4096);
		ok(has(doc, "<svg viewBox=\"0 0 320 172\">"),
		   "the viewBox is still the DEVICE, whatever the area says");
		/* This is the one that matters. If the viewBox followed the area, the
		   document would be scaled onto the panel and every number wsk_rows
		   returns would be a lie -- the wrong strip repainted, silently,
		   which is §2.2's warning and is how the bottom two corner marks
		   went missing the first time this was drawn. */
		ids[0] = "stop";
		wsk_rows(&bar, ids, 1, &y0, &y1);
		ok(y0 <= in_bar.y && y1 >= in_bar.y + in_bar.h && y1 <= 172,
		   "and rows() reports device rows, which is what the setstat wants");
	}

	/* ---- 4a3. across the axis, which had no rule until the P4 was drawn ---- */
	{
		wsk_tree_t wide;
		wsk_rect_t r;

		wsk_init(&wide, WSK_COL, 1024, 600);
		wide.pad = 4; wide.gap = 4;
		wide.item = SETTINGS; wide.n = 3;

		r = wsk_rect(&wide, 1);
		ok(r.w == 1024 - 8 && r.x == 4,
		   "CONTROL: with no cap a column takes the whole width -- 1016x64, a letterbox");

		wide.cross_max = 480;
		r = wsk_rect(&wide, 1);
		ok(r.w == 480, "a cap holds the widget to a shape a finger is");
		ok(r.x == (1024 - 480) / 2, "and centres what is left, without the caller computing it");
		ok(wsk_rect(&wide, 0).x == r.x && wsk_rect(&wide, 2).x == r.x,
		   "every widget in the stack lines up on the same edge");

		/* It only ever narrows: a cap wider than the screen changes nothing,
		   so the same tree is still right on the small panel. */
		{
			wsk_tree_t small;
			wsk_init(&small, WSK_COL, 320, 172);
			small.pad = 4; small.gap = 4;
			small.cross_max = 480;
			small.item = SETTINGS; small.n = 3;
			ok(wsk_rect(&small, 1).w == 320 - 8 && wsk_rect(&small, 1).x == 4,
			   "and a cap wider than the panel does nothing at all");
		}
	}

	/* ---- 4b. the pressed mark ---- */
	{
		wsk_widget_t live[3];
		wsk_tree_t g = t;
		int plain, marked;

		wsk_clone(live, SETTINGS, 3);
		g.item = live; g.n = 3;

		plain = render_into(&g, doc, sizeof doc, 4096);
		ok(!has(doc, "<path"), "CONTROL: an unmarked widget draws no corner marks");

		live[1].mark = WSK_MARK_CORNERS;
		marked = render_into(&g, doc, sizeof doc, 4096);
		ok(has(doc, "<path d=\"M"), "a marked one does");
		ok(has(doc, "stroke-width=\"2\"") && !has(doc, "fill=\"none\""),
		   "one path, stroked, and no fill=\"none\" -- a two-point contour has no area");
		printf("     the marks cost %d bytes on a %d-byte screen\n",
		       marked - plain, plain);

		/* The arm is a quarter of the height, capped, so it keeps its
		   proportion on a big panel and its presence on a small one. */
		{
			wsk_rect_t r = wsk_rect(&g, 1);
			char want[32];
			int arm = r.h / 4 > 16 ? 16 : r.h / 4;
			snprintf(want, sizeof want, "l%d %d", arm, arm);
			ok(has(doc, want), "the arm is a quarter of the widget height, capped at 16");
		}

		/* And below a few pixels it is not drawn at all: a mark nobody can
		   see is bytes re-parsed once per band for nothing. */
		live[1].size = 8;
		render_into(&g, doc, sizeof doc, 4096);
		ok(!has(doc, "<path"), "a widget too small to show one is not given one");
		live[1].size = 0;
	}

	/* ---- iteration 2: hit ---- */
	{
		wsk_tree_t g;
		int misses, hits;

		wsk_init(&g, WSK_COL, 320, 172);
		g.pad = 4; g.gap = 4;
		g.item = SETTINGS; g.n = 3;

		/* 1. every widget, at its centre and at all four of its corners. */
		{
			int all = 1;
			for (int i = 0; i < g.n; i++) {
				wsk_rect_t r = wsk_rect(&g, i);
				if (wsk_hit(&g, r.x + r.w / 2, r.y + r.h / 2) != i) all = 0;
				if (wsk_hit(&g, r.x, r.y) != i) all = 0;
				if (wsk_hit(&g, r.x + r.w - 1, r.y) != i) all = 0;
				if (wsk_hit(&g, r.x, r.y + r.h - 1) != i) all = 0;
				if (wsk_hit(&g, r.x + r.w - 1, r.y + r.h - 1) != i) all = 0;
			}
			ok(all, "every widget answers at its centre and at all four corners");
		}

		ok(wsk_hit_id(&g, 160, wsk_rect(&g, 1).y + 10) != 0
		   && wsk__same(wsk_hit_id(&g, 160, wsk_rect(&g, 1).y + 10), "ok"),
		   "and by name, which is the form the contract names");

		/*
		 * 2. THE CONTROL. Without this the whole suite is satisfied by
		 * "always return the first widget" -- which is a hit test that makes
		 * every gap into the thing above it, and which a person discovers by
		 * pressing one control and watching another light up.
		 */
		misses = 0;
		for (int i = 0; i + 1 < g.n; i++) {
			wsk_rect_t a = wsk_rect(&g, i), b = wsk_rect(&g, i + 1);
			for (int y = a.y + a.h; y < b.y; y++)
				if (wsk_hit(&g, 160, y) == WSK_E_NOTFOUND) misses++;
				else { printf("     a gap row %d answered %d\n", y, wsk_hit(&g, 160, y)); }
		}
		ok(misses == (g.gap) * (g.n - 1),
		   "CONTROL: every row of every gap is nothing at all");

		{
			wsk_rect_t first = wsk_rect(&g, 0);
			ok(wsk_hit(&g, 160, first.y - 1) == WSK_E_NOTFOUND
			   && wsk_hit(&g, first.x - 1, first.y + 2) == WSK_E_NOTFOUND,
			   "CONTROL: so is the padding above it and beside it");
			ok(wsk_hit(&g, -1, -1) == WSK_E_NOTFOUND
			   && wsk_hit(&g, 320, 86) == WSK_E_NOTFOUND
			   && wsk_hit(&g, 160, 172) == WSK_E_NOTFOUND
			   && wsk_hit_id(&g, 400, 400) == 0,
			   "CONTROL: and so is everywhere off the panel");
		}

		/* 3. half-open edges: with no gap, the shared pixel row belongs to
		   exactly one of them, and neither row is orphaned. */
		{
			wsk_tree_t tight = g;
			wsk_rect_t a, b;
			tight.gap = 0;
			a = wsk_rect(&tight, 0);
			b = wsk_rect(&tight, 1);
			ok(b.y == a.y + a.h, "with no gap the widgets touch");
			ok(wsk_hit(&tight, 160, a.y + a.h - 1) == 0
			   && wsk_hit(&tight, 160, b.y) == 1,
			   "and the pixel they share belongs to exactly one of them");
		}

		/* An exhaustive sweep: every row of the panel is either one widget or
		   nothing, and never two. */
		hits = 0; misses = 0;
		for (int y = 0; y < g.h; y++) {
			int found = WSK_E_NOTFOUND, count = 0;
			for (int i = 0; i < g.n; i++) {
				wsk_rect_t r = wsk_rect(&g, i);
				if (160 >= r.x && 160 < r.x + r.w && y >= r.y && y < r.y + r.h)
					{ found = i; count++; }
			}
			if (count > 1) { printf("     row %d is in two widgets\n", y); fails++; }
			if (wsk_hit(&g, 160, y) != found) { printf("     row %d disagrees\n", y); fails++; }
			if (found == WSK_E_NOTFOUND) misses++; else hits++;
		}
		ok(hits > 0 && misses > 0,
		   "swept every row of the panel: it agrees, and both answers occur");
		printf("     %d rows are a widget, %d rows are nothing\n", hits, misses);

		/* 6. the cap moves every widget sideways, and a hit test that ignored
		   it would be wrong only on the board that has touch. */
		{
			wsk_tree_t wide;
			wsk_rect_t r;
			wsk_init(&wide, WSK_COL, 1024, 600);
			wide.pad = 4; wide.gap = 4; wide.cross_max = 480;
			wide.item = SETTINGS; wide.n = 3;
			r = wsk_rect(&wide, 1);
			ok(wsk_hit(&wide, r.x + 10, r.y + 10) == 1,
			   "a capped column still answers inside the widget at 1024x600");
			ok(wsk_hit(&wide, 20, r.y + 10) == WSK_E_NOTFOUND,
			   "CONTROL: and nothing in the margin the cap created");
		}

		/* and the other axis */
		{
			wsk_tree_t row;
			wsk_rect_t a, b;
			wsk_init(&row, WSK_ROW, 320, 80);
			row.pad = 4; row.gap = 4;
			row.item = TOOLBAR; row.n = 3;
			a = wsk_rect(&row, 0);
			b = wsk_rect(&row, 1);
			ok(wsk_hit(&row, a.x + 2, 40) == 0 && wsk_hit(&row, b.x + 2, 40) == 1,
			   "a row answers along the other axis");
			ok(wsk_hit(&row, a.x + a.w, 40) == WSK_E_NOTFOUND,
			   "CONTROL: and its gaps are nothing too");
		}

		/* 4. the loop closes: a point becomes a name, the name becomes rows. */
		{
			const char *id = wsk_hit_id(&g, 160, wsk_rect(&g, 1).y + 5);
			const char *ids[1];
			int y0, y1;
			wsk_rect_t r = wsk_rect(&g, 1);
			ids[0] = id;
			ok(id != 0 && wsk_rows(&g, ids, 1, &y0, &y1) == WSK_OK
			   && y0 <= r.y && y1 >= r.y + r.h,
			   "a point becomes a name, and the name becomes the rows to repaint");
			n = clip_into(&g, ids, 1, clip, sizeof clip, 4096);
			ok(n > 0 && has(clip, ">OK</text>") && !has(clip, ">Cancel</text>"),
			   "and the document that comes back holds that widget and not its neighbour");
		}
	}

	/* ---- what it costs, for the record ---- */
	{
		wsk_tree_t g = t;
		const char *ids[1];
		int full, one;
		g.item = BIG; g.n = 8;
		g.w = 1024; g.h = 600;
		wsk_area(&g, 0, 0, 1024, 600);     /* t's area is 320x172 */
		full = render_into(&g, doc, sizeof doc, 4096);
		ids[0] = "heater";
		one = clip_into(&g, ids, 1, clip, sizeof clip, 4096);
		printf("\n  1024x600, eight controls:\n");
		printf("    the whole screen      %4d bytes   %2d bands\n", full, (g.h + 7) / 8);
		{
			int y0, y1;
			wsk_rows(&g, ids, 1, &y0, &y1);
			printf("    one button repainted  %4d bytes   %2d bands (rows %d..%d)\n",
			       one, (y1 - y0) / 8, y0, y1);
		}
	}

	/* ---- §5c. composition: more than one stack over one device ---- */
	printf("\n-- §5c  composition\n");
	{
		wsk_tree_t part[2];
		comp_t c;

		wsk_init(&part[0], WSK_COL, 320, 172);
		part[0].pad = 4; part[0].gap = 4; part[0].ids = 1;
		wsk_area(&part[0], 0, 0, 200, 172);
		part[0].item = MENU; part[0].n = 3;

		wsk_init(&part[1], WSK_COL, 320, 172);
		part[1].pad = 4; part[1].gap = 4; part[1].ids = 1;
		wsk_area(&part[1], 208, 0, 112, 172);
		part[1].item = SIDE; part[1].n = 2;

		wsk_doc_init(&c.d, &part[0]);
		c.part = part; c.n = 2;

		/* 1. both stacks appear, and there is one background */
		n = compose_into(&c, doc, sizeof doc, 4096);
		ok(n > 0, "two stacks compose into one document");
		ok(has(doc, ">Run</text>") && has(doc, ">Stop</text>") &&
		   has(doc, ">Quit</text>") && has(doc, "12.4 V") && has(doc, "41 C"),
		   "and every widget of both stacks is in it");
		ok(count(doc, "width=\"320\" height=\"172\"") == 1,
		   "one backing for the device, not one per stack");

		/* CONTROL, and it is the defect §5c exists for: rendered the way
		   §5 promised, the second document OPENS with a backing across the
		   whole device, which is what erases the first on the glass. The
		   suite sees the mechanism; picture.c sees the erasure. */
		{
			int b = render_into(&part[1], other, sizeof other, 4096);
			const char *back = strstr(other, "width=\"320\" height=\"172\"");
			const char *first = strstr(other, "12.4 V");
			ok(b > 0 && back != 0 && first != 0 && back < first,
			   "CONTROL: rendered alone, the side stack still backs the whole screen");
		}

		/* 2. a band carries the other stack's widgets that cross it */
		{
			const char *ids[1];
			int seen_crossing = 0, seen_clear = 0;

			for (int k = 0; k < 2; k++) {
				const char *who = (k == 0) ? "m_stop" : "m_quit";
				int agree = 1, carried = 0;

				ids[0] = who;
				if (wsk_doc_rows(&c.d, &part[0], ids, 1) != WSK_OK) { ok(0, who); break; }
				n = compose_into(&c, doc, sizeof doc, 4096);

				for (int i = 0; i < part[1].n; i++) {
					wsk_rect_t r = wsk_rect(&part[1], i);
					int crosses = !(r.y >= c.d.y1 || r.y + r.h <= c.d.y0);
					if (crosses != has(doc, SIDE[i].text)) agree = 0;
					if (crosses) carried++;
				}
				printf("     repaint %-6s rows %3d..%3d, carries %d of the %d readouts\n",
				       who, c.d.y0, c.d.y1, carried, part[1].n);
				ok(n > 0 && agree, k == 0
				   ? "a band carries exactly the readouts it crosses"
				   : "CONTROL: a band crossing none of them carries none");
				ok(has(doc, (k == 0) ? ">Stop</text>" : ">Quit</text>"),
				   "and the widget that was tapped");
				if (carried) seen_crossing = 1; else seen_clear = 1;
			}
			ok(seen_crossing && seen_clear,
			   "both cases occurred, so neither of those two is vacuous");
		}

		/* 3. refused over budget before a byte, as §5 criterion 5 */
		wsk_doc_init(&c.d, &part[0]);
		n = compose_into(&c, doc, sizeof doc, 4096);
		ok(compose_into(&c, doc, sizeof doc, n - 1) == WSK_E_BUDGET,
		   "a composite one byte over its budget is refused");
		ok(doc[0] == '\0', "and not a byte of it was written");
		ok(compose_into(&c, doc, sizeof doc, n) == n,
		   "CONTROL: the same composite at exactly its size is accepted");

		/* and composing costs no attributes when the stacks agree */
		ok(count(doc, "<g font-size=") == 1 && count(doc, "</g>") == 1,
		   "stacks that match the document share its one <g>");

		/* 4. a stack sized for the other board is refused, not laid out */
		{
			wsk_tree_t bad[2];
			comp_t b;

			bad[0] = part[0]; bad[1] = part[1];
			b.part = bad; b.n = 2;
			wsk_doc_init(&b.d, &bad[0]);

			bad[1].w = 1024; bad[1].h = 600;
			ok(compose_into(&b, doc, sizeof doc, 4096) == WSK_E_DEVICE,
			   "a stack sized for a different screen is refused");
			ok(doc[0] == '\0',
			   "and refused on the counting pass, so nothing was written");
			bad[1].w = 320;
			ok(compose_into(&b, doc, sizeof doc, 4096) == WSK_E_DEVICE,
			   "matching width alone is not enough");
			bad[1].h = 172;
			ok(compose_into(&b, doc, sizeof doc, 4096) > 0,
			   "CONTROL: the same two on one device compose");

			bad[1].font = 12;
			ok(compose_into(&b, doc, sizeof doc, 4096) > 0 &&
			   count(doc, "<g font-size=") == 2 && count(doc, "</g>") == 2,
			   "a stack with its own font gets a nested <g>, and it is closed");
		}

		/* 7. what composition saves, measured on the thing that asked for it */
		{
			wsk_tree_t kb[4];
			comp_t k;
			int sep = 0, one;

			for (int i = 0; i < 4; i++) {
				wsk_init(&kb[i], WSK_ROW, 1024, 600);
				kb[i].pad = 4; kb[i].gap = 4; kb[i].ids = 1;
				wsk_area(&kb[i], 0, 336 + i * 66, 1024, 62);
			}
			kb[0].item = KEYS_1; kb[0].n = 10;
			kb[1].item = KEYS_2; kb[1].n = 10;
			kb[2].item = KEYS_3; kb[2].n = 9;
			kb[3].item = KEYS_4; kb[3].n = 7;

			for (int i = 0; i < 4; i++)
				sep += render_into(&kb[i], other, sizeof other, 4096);

			wsk_doc_init(&k.d, &kb[0]);
			k.part = kb; k.n = 4;
			one = compose_into(&k, doc, sizeof doc, 4096);

			/* The interesting part is not the saving, it is that the
			   saving is the margin. Four documents are four framings and
			   erase each other; one document is correct and must fit in
			   one SRC_MAX. The four framings dropped are larger than the
			   room left over, so a 36-key keyboard does not fit without
			   composing -- which makes §5c a correctness iteration and
			   not a tidiness one. */
			printf("\n  36 keys at 1024x600, four rows:\n");
			printf("    as four documents   %4d bytes  (and they erase each other)\n", sep);
			printf("    as one composite    %4d bytes  %d under SRC_MAX\n",
			       one, 4096 - one);
			printf("    the three framings composing drops: %d bytes\n", sep - one);
			ok(one > 0 && one < sep,
			   "a composed keyboard is smaller than four documents of it");
			ok(one <= 4096, "and it fits under SRC_MAX");
			ok(sep > 4096 && sep - one > 4096 - one,
			   "and would not fit if it had to carry a framing per row");
			ok(has(doc, ">Q</text>") && has(doc, ">TAB</text>"),
			   "with the first row in it and the last");

			{
				FILE *f = fopen("/tmp/wsk_compose.svg", "wb");
				fputs(doc, f); fclose(f);
			}
		}
	}

	/* ---- §5d. a field: left-aligned, tail-scrolled, caret on request ---- */
	printf("\n-- §5d  input\n");
	{
		wsk_widget_t form[3];
		wsk_tree_t f;
		wsk_rect_t r;
		int adv, room, from;

		wsk_clone(form, FORM, 3);
		wsk_init(&f, WSK_COL, 320, 172);
		f.pad = 6; f.gap = 6; f.ids = 1; f.font = 16;
		f.item = form; f.n = 3;

		r = wsk_rect(&f, 1);
		adv = wsk_advance(f.font);

		/* 1. left-aligned, and the caret where the next character goes */
		{
			char want[64];
			wsk__scpy(form[1].text, "Ada", WSK_TEXT_MAX);
			form[1].mark = WSK_MARK_CARET;
			n = render_into(&f, doc, sizeof doc, 4096);
			ok(n > 0, "a field renders");
			ok(has(doc, "text-anchor=\"start\""),
			   "its text is anchored to the start, not centred like a label");
			sprintf(want, "x=\"%d\"", r.x + WSK_FIELD_PAD);
			ok(has(doc, want), "and starts one padding in from the trough");
			ok(has(doc, ">Ada</text>"), "with what the caller put there");
			sprintf(want, "width=\"%d\" height=", WSK_CARET_W);
			ok(has(doc, want), "and a caret after it");
			ok(count(doc, "<rect") == 4,
			   "four rects: the backing, the trough, the caret and the button");
		}

		/* CONTROL: no mark, no caret -- focus is the caller's, not ours */
		{
			char want[64];
			form[1].mark = 0;
			n = render_into(&f, doc, sizeof doc, 4096);
			sprintf(want, "width=\"%d\" height=", WSK_CARET_W);
			ok(n > 0 && !has(doc, want),
			   "CONTROL: unmarked, there is no caret at all");
			ok(count(doc, "<rect") == 3, "and one fewer rect");
			ok(has(doc, ">Ada</text>"), "though the text is still there");
			form[1].mark = WSK_MARK_CARET;
		}

		/* 2. it shows the tail, not the head.
		 *
		 * At font 16 this field holds 36 characters and WSK_TEXT_MAX is 32,
		 * so it CANNOT overflow -- which is worth knowing and is why the
		 * scroll is exercised at a touch-sized font instead. A field holds
		 * 31 characters and a NUL, full stop; the tail matters when the font
		 * is large or the field is narrow, which on a touch panel is both. */
		{
			room = wsk_field_view(&f, &form[1], r, &from);
			ok(from == 0 && room == 3,
			   "a string that fits is not scrolled at all");
			ok((r.w - 2 * WSK_FIELD_PAD - WSK_CARET_W) / adv >= WSK_TEXT_MAX - 1,
			   "and at font 16 this field cannot overflow: it outruns WSK_TEXT_MAX");
			printf("     a %d-wide field holds %d characters at font %d,"
			       " %d at font 24; a field carries %d\n",
			       r.w, (r.w - 2 * WSK_FIELD_PAD - WSK_CARET_W) / adv, f.font,
			       (r.w - 2 * WSK_FIELD_PAD - WSK_CARET_W) / wsk_advance(24),
			       WSK_TEXT_MAX - 1);

			f.font = 24;
			adv = wsk_advance(f.font);
			wsk__scpy(form[1].text, "abcdefghijklmnopqrstuvwxyz0123", WSK_TEXT_MAX);
			room = wsk_field_view(&f, &form[1], r, &from);
			ok(from > 0, "at a touch-sized font the same string is scrolled");
			n = render_into(&f, doc, sizeof doc, 4096);
			ok(n > 0 && has(doc, "0123</text>"),
			   "the TAIL is what is shown -- the characters just typed");
			ok(!has(doc, ">abcdefg"),
			   "and the head has scrolled off, which a label would have kept");
			printf("     %d characters, showing the last %d from offset %d\n",
			       wsk__len(form[1].text), room, from);
		}

		/* 3. the caret stays inside the trough however long the text is */
		{
			int cx;
			n = render_into(&f, doc, sizeof doc, 4096);
			cx = r.x + WSK_FIELD_PAD + room * adv;
			ok(n > 0 && cx + WSK_CARET_W <= r.x + r.w,
			   "the caret is still inside the trough on a full field");
			ok(has(doc, "fill=\"#081620\""),
			   "and the trough is not the accent, so it is not a button");
		}

		/* 5. a field and a whole keyboard, together, under SRC_MAX */
		{
			wsk_widget_t fld[1];
			wsk_tree_t kb[5];
			comp_t k;

			wsk_clone(fld, FORM + 1, 1);
			wsk__scpy(fld[0].text, "the quick brown fox jumps over", WSK_TEXT_MAX);
			fld[0].mark = WSK_MARK_CARET;

			wsk_init(&kb[0], WSK_COL, 1024, 600);
			kb[0].pad = 8; kb[0].gap = 8; kb[0].ids = 1;
			wsk_area(&kb[0], 0, 260, 1024, 70);
			kb[0].item = fld; kb[0].n = 1;

			for (int i = 0; i < 4; i++) {
				wsk_init(&kb[i + 1], WSK_ROW, 1024, 600);
				kb[i + 1].pad = 4; kb[i + 1].gap = 4; kb[i + 1].ids = 1;
				wsk_area(&kb[i + 1], 0, 336 + i * 66, 1024, 62);
			}
			kb[1].item = KEYS_1; kb[1].n = 10;
			kb[2].item = KEYS_2; kb[2].n = 10;
			kb[3].item = KEYS_3; kb[3].n = 9;
			kb[4].item = KEYS_4; kb[4].n = 7;

			wsk_doc_init(&k.d, &kb[0]);
			k.part = kb; k.n = 5;
			n = compose_into(&k, doc, sizeof doc, 4096);
			printf("\n  a field and 36 keys at 1024x600: %d bytes, %d under SRC_MAX\n",
			       n, 4096 - n);
			ok(n > 0, "a field and a full keyboard compose");
			ok(n <= 4096, "and they fit in SRC_MAX together");
			ok(has(doc, ">SPACE</text>") && has(doc, "text-anchor=\"start\""),
			   "with the keyboard and the field both in one document");

			{
				FILE *fp = fopen("/tmp/wsk_input.svg", "wb");
				fputs(doc, fp); fclose(fp);
			}
		}
	}

	/* The price of a row, in whisker's own output rather than by argument.
	   RV-9 measured a row repaint at 1.77x a column's on the rasteriser; this
	   is where those extra bands and bytes come from. */
	{
		wsk_tree_t col, row;
		const char *ids[1];
		int cy0, cy1, ry0, ry1, cb, rb;

		/* Three 64-row buttons are 200 rows with the gaps; a column at 172
		   cannot hold them, and wsk_check now says so. 48 each fits. */
		static wsk_widget_t tall[3];
		memcpy(tall, TOOLBAR, sizeof tall);
		for (int i = 0; i < 3; i++) tall[i].size = 48;

		wsk_init(&col, WSK_COL, 320, 172);
		col.pad = 4; col.gap = 4; col.item = tall; col.n = 3;
		wsk_init(&row, WSK_ROW, 320, 172);
		row.pad = 4; row.gap = 4; row.item = TOOLBAR; row.n = 3;

		ids[0] = "stop";
		wsk_rows(&col, ids, 1, &cy0, &cy1);
		wsk_rows(&row, ids, 1, &ry0, &ry1);
		cb = clip_into(&col, ids, 1, clip, sizeof clip, 4096);
		rb = clip_into(&row, ids, 1, doc, sizeof doc, 4096);
		printf("\n  repainting one of three, 320x172:\n");
		printf("    as a column  %3d bytes  %2d bands\n", cb, (cy1 - cy0) / 8);
		printf("    as a row     %3d bytes  %2d bands   (%d of the 22 on screen)\n",
		       rb, (ry1 - ry0) / 8, (ry1 - ry0) / 8);
	}

	/* ---- §7b. justify: where the leftover space goes (scale-sketch §3) ---- */
	printf("\n-- §5e  scrolling\n");
	{
		/*
		 * A list of buttons, each 24 rows with a 2-row gap: a pitch of 26,
		 * in a pane from row 40 to row 140 of a 320x172 screen. Band-aligned
		 * at the top on purpose, so criterion 7 can say which band a cut
		 * widget's repaint starts on.
		 */
		static wsk_widget_t list[200];
		wsk_tree_t p, q;
		int i, max, which;

		for (i = 0; i < 200; i++) {
			memset(&list[i], 0, sizeof list[i]);
			list[i].kind = WSK_BUTTON;
			list[i].size = 24;
			list[i].fill = WSK_INHERIT;
			list[i].ink = WSK_INHERIT;
			snprintf(list[i].id, WSK_ID_MAX, "i%03d", i);
			snprintf(list[i].text, WSK_TEXT_MAX, "Row %d", i);
		}
		wsk_init(&p, WSK_COL, 320, 172);
		p.pad = 2; p.gap = 2; p.ids = 1;
		wsk_area(&p, 0, 40, 320, 100);
		p.item = list; p.n = 200;

		/* the control on the whole section: without SCROLL this is D10 */
		ok(wsk_check(&p, &which) == WSK_E_FIT,
		   "CONTROL: 200 rows in a 100-row pane, not scrolling, is refused (D10)");
		p.overflow = WSK_SCROLL;
		ok(wsk_check(&p, 0) == WSK_OK, "and scrolling, the same tree is accepted");

		max = wsk_scroll_max(&p);
		ok(max == 2 + 200 * 24 + 199 * 2 + 2 - 100,
		   "scroll_max is the content less the pane: padding, rows, gaps, padding");

		/* 1. bytes track the viewport, not the list */
		{
			int n200, n20;
			q = p; q.n = 20;
			p.scroll = q.scroll = 13;
			n200 = render_into(&p, doc, sizeof doc, 4096);
			n20 = render_into(&q, other, sizeof other, 4096);
			ok(n200 > 0 && n200 == n20 && strcmp(doc, other) == 0,
			   "200 rows and 20 draw the same document at the same offset");
			printf("     %d bytes either way, %d boxes and %d labels for a "
			       "100-row pane\n", n200, count(doc, "<rect") - 1,
			       count(doc, "<text"));
			ok(count(doc, "<rect") - 1 < 8,
			   "and it is the pane's handful, not the list's two hundred");
		}

		/* 2. a list that fits lays out exactly as one that does not scroll */
		{
			wsk_tree_t a = p, b = p;
			a.n = b.n = 3;
			a.overflow = WSK_REFUSE; a.scroll = 0;
			b.overflow = WSK_SCROLL; b.scroll = 0;
			ok(wsk_scroll_max(&b) == 0, "three rows fit: nothing to scroll");
			render_into(&a, doc, sizeof doc, 4096);
			render_into(&b, other, sizeof other, 4096);
			ok(doc[0] != '\0' && strcmp(doc, other) == 0,
			   "a scrolling stack that fits is byte-identical to one that does not scroll");
			b.scroll = 1;
			ok(wsk_check(&b, 0) == WSK_E_SCROLL,
			   "and it may not be scrolled at all");
		}

		/* 4. a widget cut by the edge keeps its box and loses its text */
		{
			wsk_rect_t r;
			p.scroll = 0;
			render_into(&p, doc, sizeof doc, 4096);
			ok(has(doc, ">Row 0</text>"),
			   "CONTROL: in full view, row 0 has its text");
			p.scroll = 12;                   /* row 0 is now half above the pane */
			r = wsk_rect(&p, 0);
			ok(r.y < p.ay && r.y + r.h > p.ay, "at 12, row 0 straddles the top edge");
			render_into(&p, doc, sizeof doc, 4096);
			ok(has(doc, "<rect id=\"i000\" x=\"2\" y=\"40\" width=\"316\" height=\"14\""),
			   "its box is drawn cut: from the pane's top, 14 rows of 24");
			ok(!has(doc, ">Row 0</text>"), "and its text is not drawn at all");
			ok(has(doc, ">Row 1</text>"), "CONTROL: row 1, wholly in view, is");
			{
				/* the last visible row is cut at the bottom the same way */
				int last = -1;
				for (i = 0; i < 200; i++) {
					r = wsk_rect(&p, i);
					if (r.y < p.ay + p.ah) last = i;
				}
				r = wsk_rect(&p, last);
				ok(r.y + r.h > p.ay + p.ah, "and the last row in view hangs off the bottom");
				{
					char want[48];
					snprintf(want, sizeof want, ">%s</text>", list[last].text);
					ok(last == 4 && !has(doc, want), "with no text either");
				}
			}
			ok(count(doc, "<rect") - 1 == 5 && count(doc, "<text") == 3,
			   "five boxes, three of them whole enough to carry a label");
		}

		/* 5. hit sees the viewport */
		{
			wsk_rect_t r;
			p.scroll = 0;
			ok(wsk_hit(&p, 160, 50) == 0, "at 0, the pane's first row is under (160,50)");
			p.scroll = 26;
			ok(wsk_hit(&p, 160, 50) == 1, "scrolled one row, the same point names the next");
			r = wsk_rect(&p, 0);
			ok(r.y <= 25 && r.y + r.h > 25,
			   "CONTROL: row 0's rectangle is still laid out over (160,25)");
			ok(wsk_hit(&p, 160, 25) == WSK_E_NOTFOUND,
			   "but it is scrolled off, so nothing is under that point");
			ok(wsk_hit(&p, 160, 145) == WSK_E_NOTFOUND,
			   "nor under one just below the pane, where later rows are laid out");
		}

		/* 6. out of range is refused, before a byte */
		{
			p.scroll = -1;
			ok(render_into(&p, doc, sizeof doc, 4096) == WSK_E_SCROLL && doc[0] == '\0',
			   "scroll -1 is refused, and nothing is written");
			p.scroll = (short)(max + 1);
			ok(render_into(&p, doc, sizeof doc, 4096) == WSK_E_SCROLL,
			   "scroll max+1 is refused");
			p.scroll = (short)max;
			ok(render_into(&p, doc, sizeof doc, 4096) > 0 && has(doc, ">Row 199</text>"),
			   "scroll max is drawn, and shows the last row");
			p.scroll = 0;
			ok(render_into(&p, doc, sizeof doc, 4096) > 0, "scroll 0 is drawn");
		}

		/* 7. rows is clipped to the viewport */
		{
			const char *ids[2];
			int y0, y1;
			p.scroll = 26 * 5;
			ids[0] = "i000";
			ok(wsk_rows(&p, ids, 1, &y0, &y1) == WSK_E_EMPTY,
			   "a row scrolled wholly out has nothing to repaint");
			p.scroll = 12;
			ok(wsk_rows(&p, ids, 1, &y0, &y1) == WSK_OK && y0 == 40,
			   "a row cut by the top edge repaints from the pane's band, not its own");
			ids[1] = "i006";
			p.scroll = 26 * 5;
			ok(wsk_rows(&p, ids, 2, &y0, &y1) == WSK_OK,
			   "CONTROL: one visible in the set is enough to answer");
			ok(wsk_area_rows(&p, &y0, &y1) == WSK_OK && y0 == 40 && y1 == 144,
			   "a scroll step repaints the pane's rows, band-snapped: 40..144");
		}

		/* what a coordinate can say: a rectangle is four shorts */
		{
			static wsk_widget_t many[1400];
			wsk_tree_t m;
			for (i = 0; i < 1400; i++) many[i] = list[i % 200];
			m = p; m.item = many; m.scroll = 0;
			m.n = 1200;                      /* 2 + 1200*26 + 2 + 40 = 31244 */
			ok(wsk_check(&m, 0) == WSK_OK,
			   "CONTROL: 1,200 rows end at row 31,244, which a short can hold");
			m.n = 1400;
			ok(wsk_check(&m, &which) == WSK_E_FIT && which == 1258,
			   "1,400 do not: refused at the row that would wrap (1258), not drawn");
		}

		/* a long list a slice at a time draws the same document */
		{
			static const int AT[] = { 0, 1, 12, 25, 26, 27, 1000, 3333, -1 };
			wsk_tree_t sl;
			int k, same = 0, smax = wsk_slice_max(&p, 200, 24), first = 0;
			ok(smax == max, "the slice's range is the whole list's range");
			for (k = 0; k < 9; k++) {
				int at = (AT[k] < 0) ? max : AT[k];
				sl = p;
				if (wsk_slice(&sl, 200, 24, at, &first) != WSK_OK) continue;
				sl.item = &list[first];
				p.scroll = (short)at;
				render_into(&p, doc, sizeof doc, 4096);
				render_into(&sl, other, sizeof other, 4096);
				same += (doc[0] != '\0' && strcmp(doc, other) == 0 && sl.n < 8);
			}
			ok(same == 9, "a slice of a few rows draws the whole list's document, at nine offsets");
			sl = p;
			wsk_slice(&sl, 200, 24, 1000, &first);
			sl.item = &list[first + 1];          /* the slice one row out */
			p.scroll = 1000;
			render_into(&p, doc, sizeof doc, 4096);
			render_into(&sl, other, sizeof other, 4096);
			ok(strcmp(doc, other) != 0,
			   "CONTROL: a slice one row out does not, so the comparison can fail");
			sl = p;
			ok(wsk_slice(&sl, 200, 24, max + 1, &first) == WSK_E_SCROLL &&
			   wsk_slice(&sl, 200, 24, -1, &first) == WSK_E_SCROLL,
			   "and an offset past either end of the whole list is refused");
			sl = p;
			ok(wsk_slice(&sl, 0, 24, 0, &first) == WSK_OK && sl.n == 0 && first == 0,
			   "an empty list is an empty pane, not row -1");
			sl = p;
			ok(wsk_slice(&sl, 100000, 24, 2000000, &first) == WSK_OK &&
			   first == 2000000 / 26 && sl.n < 8,
			   "a hundred thousand rows, two million rows down: a handful handed over");
			p.scroll = 0;
		}

		/* across the other axis: a row of buttons, scrolled sideways */
		{
			static wsk_widget_t keys[12];
			wsk_tree_t h;
			for (i = 0; i < 12; i++) {
				memset(&keys[i], 0, sizeof keys[i]);
				keys[i].kind = WSK_BUTTON;
				keys[i].size = 60;
				keys[i].fill = WSK_INHERIT;
				keys[i].ink = WSK_INHERIT;
				snprintf(keys[i].id, WSK_ID_MAX, "k%d", i);
				snprintf(keys[i].text, WSK_TEXT_MAX, "K%d", i);
			}
			wsk_init(&h, WSK_ROW, 320, 172);
			h.pad = 0; h.gap = 4; h.ids = 1;
			wsk_area(&h, 20, 0, 200, 40);
			h.item = keys; h.n = 12;
			h.overflow = WSK_SCROLL;
			h.scroll = 30;
			render_into(&h, doc, sizeof doc, 4096);
			ok(has(doc, "<rect id=\"k0\" x=\"20\" width=\"30\""),
			   "ROW: the first key is cut at the pane's left edge");
			ok(!has(doc, ">K0</text>") && has(doc, ">K1</text>"),
			   "ROW: and its label is gone while the next one's is not");
			ok(wsk_hit(&h, 10, 20) == WSK_E_NOTFOUND && wsk_hit(&h, 25, 20) == 0,
			   "ROW: left of the pane is nothing; just inside it is the cut key");
		}
		p.scroll = 0;
	}

	printf("\n-- justify\n");
	{
		static wsk_widget_t three[3];
		wsk_tree_t j;
		wsk_rect_t a, b, c;
		int i, span, used, extra;

		for (i = 0; i < 3; i++) {
			memset(&three[i], 0, sizeof three[i]);
			three[i].kind = WSK_LABEL;
			three[i].size = 20;            /* fixed: nothing claims the surplus */
			three[i].fill = WSK_INHERIT;
			three[i].ink = WSK_INHERIT;
			three[i].id[0] = (char)('a' + i); three[i].id[1] = '\0';
			three[i].text[0] = (char)('A' + i); three[i].text[1] = '\0';
		}
		wsk_init(&j, WSK_COL, 320, 172);
		j.pad = 4; j.gap = 4; j.item = three; j.n = 3;

		span = j.ah - 2 * j.pad;
		used = 3 * 20 + 2 * j.gap;
		extra = span - used;

		/* START is the default and is what whisker did before justify
		   existed -- the control for every case below. */
		ok(j.justify == WSK_START, "a tree justifies to the start unless told otherwise");
		a = wsk_rect(&j, 0); c = wsk_rect(&j, 2);
		ok(a.y == j.pad, "START: the first child is at the padding");
		ok(c.y + c.h == j.pad + used,
		   "CONTROL: and the surplus is all after the last child");

		j.justify = WSK_END;
		a = wsk_rect(&j, 0); c = wsk_rect(&j, 2);
		ok(a.y == j.pad + extra, "END: the surplus is all before the first");
		ok(c.y + c.h == j.ah - j.pad,
		   "and the last child ends exactly on the far padding");

		j.justify = WSK_CENTER;
		a = wsk_rect(&j, 0); c = wsk_rect(&j, 2);
		ok(a.y == j.pad + extra / 2, "CENTER: half the surplus leads");
		ok((a.y - j.pad) - (j.ah - j.pad - (c.y + c.h)) <= 1 &&
		   (j.ah - j.pad - (c.y + c.h)) - (a.y - j.pad) <= 1,
		   "and both ends are equal to within the odd pixel");

		j.justify = WSK_BETWEEN;
		a = wsk_rect(&j, 0); b = wsk_rect(&j, 1); c = wsk_rect(&j, 2);
		ok(a.y == j.pad, "BETWEEN: the first child is still at the padding");
		ok(c.y + c.h == j.ah - j.pad, "the last still ends on the far padding");
		ok((b.y - (a.y + a.h)) == ((c.y - (b.y + b.h))),
		   "and the two gaps it made are equal -- no rounding drift");

		j.justify = WSK_AROUND;
		a = wsk_rect(&j, 0); c = wsk_rect(&j, 2);
		ok(a.y > j.pad && a.y < j.pad + extra / 2,
		   "AROUND: a half-share leads, less than CENTER's whole half");
		{
			/* The property that distinguishes AROUND from everything
			   else: the space at the two ends is equal, and it is half
			   what sits between the children. */
			int lead = a.y - j.pad;
			int trail = (j.ah - j.pad) - (c.y + c.h);
			int mid = b.y - (a.y + a.h);
			b = wsk_rect(&j, 1);
			mid = b.y - (a.y + a.h) - j.gap;
			ok(lead - trail <= 1 && trail - lead <= 1,
			   "and the space at the two ends is equal");
			ok(mid - 2 * lead <= 1 && 2 * lead - mid <= 1,
			   "and each end is half of what falls between the children");
			ok(trail > 0, "CONTROL: a trailing gap, which END and BETWEEN do not leave");
		}

		/* The interaction that could have been a bug: a weighted child
		   eats the surplus, so there is nothing left for a justify to
		   place and it must do nothing at all. */
		{
			wsk_rect_t w0, w1;
			three[1].weight = 1;
			j.justify = WSK_START;  w0 = wsk_rect(&j, 2);
			j.justify = WSK_AROUND; w1 = wsk_rect(&j, 2);
			ok(w0.y == w1.y && w0.h == w1.h,
			   "CONTROL: with a weighted child there is no surplus, so justify does nothing");
			three[1].weight = 0;
		}

		/* It is layout, not markup: the same elements, moved. */
		{
			char d0[2048], d1[2048];
			int n0, n1;
			j.justify = WSK_START;  n0 = render_into(&j, d0, sizeof d0, 4096);
			j.justify = WSK_BETWEEN; n1 = render_into(&j, d1, sizeof d1, 4096);
			ok(n0 > 0 && n1 > 0, "both justifies render");
			ok(count(d0, "<text") == count(d1, "<text") &&
			   count(d0, "<rect") == count(d1, "<rect"),
			   "justify adds no elements -- it only moves them");
			printf("    %d bytes at START, %d at BETWEEN: %+d, and that is "
			       "coordinate digits\n", n0, n1, n1 - n0);
		}
		j.justify = WSK_START;
	}

	/* ---- §5f. nesting: a window is a tree ---- */
	printf("\n-- §5f  nesting\n");
	{
		wsk_tree_t win, nest[4];
		const wsk_tree_t *in = 0;
		wsk_widget_t live[3], body[3];
		char nd[4096];
		int which = -2, y0 = 0, y1 = 0, n;

		wsk_init(&win, WSK_COL, 1024, 600);
		win.pad = 0; win.gap = 4; win.ids = 1;
		win.item = WIN; win.n = 3;
		win.nest = nest; win.nests = 4;
		wsk_nest_init(&nest[0], &win, WSK_ROW);
		nest[0].pad = 4; nest[0].gap = 4; nest[0].item = TB; nest[0].n = 3;
		wsk_nest_init(&nest[1], &win, WSK_COL);
		nest[1].item = BODY; nest[1].n = 3;           /* pad 8, gap 8: the defaults */
		wsk_nest_init(&nest[2], &win, WSK_ROW);
		nest[2].pad = 2; nest[2].gap = 8; nest[2].item = STATUS; nest[2].n = 2;
		wsk_nest_init(&nest[3], &win, WSK_ROW);
		nest[3].pad = 4; nest[3].font = 20; nest[3].item = OPTS; nest[3].n = 2;

		ok(wsk_check(&win, &which) == WSK_OK, "a window of four nested stacks is accepted");

		/* Rule 1, by hand. The toolbar row is as tall as its tallest child,
		   a 64-row button, plus 4 of padding each side: 72. The status row's
		   labels are band(16 + 8) = 24, plus 2 each side: 28. The body takes
		   the rest by weight: 600 - 72 - 28 - two gaps of 4 = 492. */
		ok(rect_is(rect_of(&win, "toolbar"), 0, 0, 1024, 72),
		   "the toolbar row is its tallest button plus padding: 0,0 1024x72");
		ok(rect_is(rect_of(&win, "body"), 0, 76, 1024, 492),
		   "the body takes what is left by weight: 0,76 1024x492");
		ok(rect_is(rect_of(&win, "status"), 0, 572, 1024, 28),
		   "the status row is its labels plus padding: 0,572 1024x28");
		/* Inside the toolbar, a row in the rectangle it was given: "New" is
		   3 cells of 8 plus 4 each side = 32 wide at x 4; "Save" 40 at 40;
		   the title takes the rest -- 1016 - (32+40+72) - 8 = 864 over its
		   own 72 -- and ends at 1020, inside the padding. Across, 72 - 8. */
		ok(rect_is(rect_of(&win, "new"), 4, 4, 32, 64), "new: 4,4 32x64");
		ok(rect_is(rect_of(&win, "save"), 40, 4, 40, 64), "save: 40,4 40x64");
		ok(rect_is(rect_of(&win, "title"), 84, 4, 936, 64),
		   "title: 84,4 936x64 -- the weighted child ends at the padding");
		/* And two deep: the body's own padding, then a row in a column. */
		ok(rect_is(rect_of(&win, "name"), 8, 84, 1008, 24), "name, in the body: 8,84 1008x24");
		ok(rect_is(rect_of(&win, "ok"), 8, 116, 1008, 64), "ok: 8,116 1008x64");
		ok(rect_is(rect_of(&win, "opts"), 8, 188, 1008, 72),
		   "opts, a row in the body, sized by its buttons and padding: 8,188 1008x72");
		/* a: 4 in, one cell of 10 at font 20 plus 4 each side = 18; b after
		   a gap of 8, at 38. Across, 72 - 8. */
		ok(rect_is(rect_of(&win, "b"), 38, 192, 18, 64),
		   "b, in a row in a column in a column, at font 20: 38,192 18x64");

		/* Rule 5: the deepest widget; a stack in its own gaps; nothing in
		   the root's. */
		ok(wsk_hit_id(&win, 50, 30) != 0 && strcmp(wsk_hit_id(&win, 50, 30), "save") == 0,
		   "hit, two deep: (50,30) is save");
		ok(wsk_hit_in(&win, 50, 30, &in) == 1 && in == &nest[0],
		   "and wsk_hit_in says which tree: save is item 1 of the toolbar's");
		ok(wsk_hit(&win, 50, 30) == 0,
		   "wsk_hit keeps its old answer, an index in the root: the toolbar");
		ok(wsk_hit_id(&win, 37, 30) != 0 && strcmp(wsk_hit_id(&win, 37, 30), "toolbar") == 0,
		   "a point in the toolbar's own gap is the toolbar -- a stack is a widget");
		ok(wsk_hit_id(&win, 40, 200) != 0 && strcmp(wsk_hit_id(&win, 40, 200), "b") == 0,
		   "three deep: (40,200) is b");
		ok(wsk_hit_id(&win, 500, 74) == 0 && wsk_hit_in(&win, 500, 74, &in) < 0,
		   "CONTROL: the root's own gap is nothing -- the root is not a widget");

		/* Rule 4: ids at any depth, paths checked link by link. */
		ok(wsk_find_in(&win, "save", &in) == 1 && in == &nest[0], "a bare id is found at any depth");
		ok(wsk_find_in(&win, "/toolbar/save", &in) == 1 && in == &nest[0],
		   "the path /toolbar/save names it too");
		ok(wsk_find_in(&win, "/body/opts/b", &in) == 1 && in == &nest[3],
		   "and /body/opts/b, three deep");
		ok(wsk_find_in(&win, "/body/save", &in) == WSK_E_NOTFOUND,
		   "a wrong chain is refused although the id exists: save is not in the body");
		ok(wsk_find_in(&win, "/save", &in) == WSK_E_NOTFOUND,
		   "and /save is refused: save is not at the root");
		ok(wsk_find_in(&win, "/toolbar", &in) == 0 && in == &win, "a path to a stack names the branch");
		{
			const char *k1[1] = { "name" }, *k2[1] = { "/toolbar" }, *k3[1] = { "opts" };
			ok(wsk_rows(&win, k1, 1, &y0, &y1) == WSK_OK && y0 == 80 && y1 == 112,
			   "rows of a nested leaf, band-snapped: name is 84..108, so 80..112");
			ok(wsk_rows(&win, k2, 1, &y0, &y1) == WSK_OK && y0 == 0 && y1 == 72,
			   "rows of a branch are its whole rectangle: /toolbar is 0..72");
			ok(wsk_rows(&win, k3, 1, &y0, &y1) == WSK_OK && y0 == 184 && y1 == 264,
			   "and of a nested row: opts 188..260, so 184..264");
		}

		/* Rule 7: a stack says only what it has to. */
		n = render_into(&win, nd, sizeof nd, 4096);
		ok(n > 0, "the window renders");
		printf("    %d bytes: four stacks, nine widgets\n", n);
		ok(has(nd, "<g id=\"toolbar\">") && has(nd, "<g id=\"body\">"),
		   "with ids on, a nested stack is a <g> with its id");
		ok(has(nd, "<g id=\"opts\" font-size=\"20\">"),
		   "a stack with its own font says so on its <g>");
		ok(has(nd, "<g id=\"status\"><rect y=\"572\" width=\"1024\" height=\"28\" fill=\"#203040\"/>"),
		   "a stack with a fill draws it behind its children, on its <g>");
		ok(count(nd, "<text") == 8, "eight labels: every leaf but the field's empty text");
		{
			FILE *f = fopen("/tmp/wsk_nest.svg", "wb");
			fputs(nd, f); fclose(f);
		}
		{
			wsk_tree_t q = win;
			q.ids = 0;
			for (int k = 0; k < 4; k++) nest[k].ids = 0;
			nest[3].font = 16;
			n = render_into(&q, nd, sizeof nd, 4096);
			ok(n > 0 && count(nd, "<g") == 1 && !has(nd, "id="),
			   "without ids, and nothing to say, nesting adds no <g> at all");
			for (int k = 0; k < 4; k++) nest[k].ids = 1;
			nest[3].font = 20;
		}
		{
			const char *k[1] = { "save" };
			n = clip_into(&win, k, 1, nd, sizeof nd, 4096);
			ok(n > 0 && has(nd, ">Save<") && !has(nd, ">Ready<") && !has(nd, ">OK<"),
			   "a clipped repaint of a nested widget draws its rows and not the others");
		}

		/* Rule 6: refusals, each beside the same tree minus the fault. */
		wsk_clone(live, WIN, 3);
		win.item = live;
		live[2].sub = 9;
		ok(wsk_check_in(&win, &which, &in) == WSK_E_KIND && which == 2 && in == &win,
		   "a sub out of range is refused, naming the widget and its tree");
		live[2].sub = 2;
		ok(wsk_check(&win, 0) == WSK_OK, "CONTROL: and in range it is accepted");
		win.nest = 0;
		ok(wsk_check(&win, &which) == WSK_E_KIND && which == 0,
		   "a stack with no nest to look in is refused");
		win.nest = nest;
		live[2].sub = 0;
		ok(wsk_check_in(&win, &which, &in) == WSK_E_KIND && in == &win && which == 2,
		   "one tree inside two stacks is refused: its ids would be drawn twice");
		live[2].sub = 2;
		wsk_clone(body, BODY, 3);
		nest[1].item = body;
		body[2].sub = 1;
		ok(wsk_check(&win, &which) == WSK_E_KIND,
		   "a tree that holds itself is inside two stacks, and refused as that");
		body[2].sub = 3;
		/* D10 at depth needs a stack given less than its content: one left
		   at size 0 is as big as its content by rule 1, and then it is the
		   WINDOW that does not fit -- refused at the root, rightly. */
		body[1].size = 500;
		ok(wsk_check_in(&win, &which, &in) == WSK_E_FIT && which == 1 && in == &win,
		   "a child grown past the window makes the window not fit: refused at the root");
		body[1].size = 200;
		live[1].weight = 0; live[1].size = 200;
		ok(wsk_check_in(&win, &which, &in) == WSK_E_FIT && which == 1 && in == &nest[1],
		   "D10 at depth: a body given 200 rows, whose ok wants 200, is refused in the body");
		nest[1].overflow = WSK_SCROLL;
		ok(wsk_check(&win, 0) == WSK_OK, "CONTROL: the same body, scrolling, is accepted");
		nest[1].overflow = WSK_REFUSE;
		body[1].size = 0;
		live[1].weight = 1; live[1].size = 0;
		ok(wsk_check(&win, 0) == WSK_OK, "CONTROL: and at its own sizes it fits");
		live[0].id[0] = 'a'; live[0].id[1] = '/'; live[0].id[2] = 'b'; live[0].id[3] = '\0';
		ok(wsk_check(&win, &which) == WSK_E_TEXT && which == 0,
		   "an id holding '/' is refused: it would be two links of a path");
		wsk__scpy(live[0].id, "toolbar", WSK_ID_MAX);
		ok(wsk_check(&win, 0) == WSK_OK, "CONTROL: renamed back, accepted");
	}

	/* Rule 6's last refusal: too deep. A chain of stacks, each holding the
	   next, ends in a label; WSK_DEPTH_MAX levels are refused. A cycle never
	   gets here -- every tree in one is inside two stacks -- so this is the
	   limit itself, and the control is the chain one shorter. */
	{
		static const wsk_widget_t LEAF[] = { { .kind = WSK_LABEL, .id = "leaf", .text = "x" } };
		wsk_widget_t link[WSK_DEPTH_MAX + 1];
		wsk_tree_t root, ch[WSK_DEPTH_MAX + 1];
		int which = -2;

		wsk_init(&root, WSK_COL, 320, 400);
		root.nest = ch; root.nests = WSK_DEPTH_MAX + 1;
		for (int j = 0; j <= WSK_DEPTH_MAX; j++) {
			link[j].kind = WSK_STACK; link[j].sub = (short)j;
			link[j].size = 0; link[j].weight = 0; link[j].fill = 0;
			link[j].ink = 0; link[j].mark = 0;
			link[j].id[0] = 'c'; link[j].id[1] = (char)('0' + j); link[j].id[2] = '\0';
			link[j].text[0] = '\0';
			wsk_nest_init(&ch[j], &root, WSK_COL);
			ch[j].pad = 0; ch[j].gap = 0;
		}
		root.item = &link[0]; root.n = 1;
		for (int j = 0; j < WSK_DEPTH_MAX; j++) { ch[j].item = &link[j + 1]; ch[j].n = 1; }
		ch[WSK_DEPTH_MAX].item = LEAF; ch[WSK_DEPTH_MAX].n = 1;
		ok(wsk_check(&root, &which) == WSK_E_DEPTH,
		   "WSK_DEPTH_MAX + 1 levels of stack are refused, WSK_E_DEPTH");
		ch[WSK_DEPTH_MAX - 2].item = LEAF;
		ok(wsk_check(&root, 0) == WSK_OK, "CONTROL: two shorter, accepted");
	}

	/* Rule 3: a row nested in a scrolled pane is cut by the pane. */
	{
		static const wsk_widget_t OUT[] = {
			{ .kind = WSK_LABEL, .id = "head", .text = "Head", .size = 48 },
			{ .kind = WSK_STACK, .id = "pane", .sub = 0, .weight = 1 },
			{ .kind = WSK_LABEL, .id = "foot", .text = "Foot", .size = 48 },
		};
		static const wsk_widget_t PANE[] = {
			{ .kind = WSK_STACK, .id = "row0", .sub = 1, .size = 48 },
			{ .kind = WSK_STACK, .id = "row1", .sub = 2, .size = 48 },
			{ .kind = WSK_STACK, .id = "row2", .sub = 3, .size = 48 },
			{ .kind = WSK_STACK, .id = "row3", .sub = 4, .size = 48 },
		};
		static const wsk_widget_t R0[] = { { .kind = WSK_BUTTON, .id = "a0", .text = "a0", .weight = 1 },
		                                   { .kind = WSK_BUTTON, .id = "b0", .text = "b0", .weight = 1 } };
		static const wsk_widget_t R1[] = { { .kind = WSK_BUTTON, .id = "a1", .text = "a1", .weight = 1 },
		                                   { .kind = WSK_BUTTON, .id = "b1", .text = "b1", .weight = 1 } };
		static const wsk_widget_t R2[] = { { .kind = WSK_BUTTON, .id = "a2", .text = "a2", .weight = 1 },
		                                   { .kind = WSK_BUTTON, .id = "b2", .text = "b2", .weight = 1 } };
		static const wsk_widget_t R3[] = { { .kind = WSK_BUTTON, .id = "a3", .text = "a3", .weight = 1 },
		                                   { .kind = WSK_BUTTON, .id = "b3", .text = "b3", .weight = 1 } };
		wsk_tree_t o, k[5];
		char sd[4096];
		int y0 = 0, y1 = 0;

		wsk_init(&o, WSK_COL, 320, 200);
		o.pad = 0; o.gap = 0; o.ids = 1; o.item = OUT; o.n = 3;
		o.nest = k; o.nests = 5;
		wsk_nest_init(&k[0], &o, WSK_COL);
		k[0].pad = 0; k[0].gap = 2; k[0].item = PANE; k[0].n = 4;
		k[0].overflow = WSK_SCROLL;
		for (int j = 1; j < 5; j++) {
			wsk_nest_init(&k[j], &o, WSK_ROW);
			k[j].pad = 0; k[j].gap = 0; k[j].n = 2;
		}
		k[1].item = R0; k[2].item = R1; k[3].item = R2; k[4].item = R3;

		/* The pane is 48..152; four rows of 48 and three gaps of 2 are 198,
		   so it may scroll 94. At 30: row0 is 18..66, row2 118..166, row3
		   168..216 -- cut to 48..66, 118..152, and nothing. */
		k[0].scroll = 30;
		ok(wsk_check(&o, 0) == WSK_OK, "a scrolled pane of nested rows is accepted");
		k[0].scroll = 95;
		ok(wsk_check(&o, 0) == WSK_E_SCROLL, "and one past its end is refused, at depth (D11)");
		k[0].scroll = 30;
		render_into(&o, sd, sizeof sd, 4096);
		ok(has(sd, "<rect id=\"a0\" y=\"48\" width=\"160\" height=\"18\""),
		   "a button in a row cut by the pane above it: a0 is 48..66");
		ok(!has(sd, ">a0<") && has(sd, ">a1<") && has(sd, ">a2<") && !has(sd, ">a3<"),
		   "its text is whole or not at all: a0 cut away, a2 whole in its cut row, a3 out");
		ok(!has(sd, "id=\"a3\"") && has(sd, "<g id=\"row3\">") == 0,
		   "a row wholly out of view draws nothing, not even its <g>");
		ok(wsk_hit_id(&o, 10, 60) != 0 && strcmp(wsk_hit_id(&o, 10, 60), "a0") == 0,
		   "hit in the cut part of a0 is a0");
		ok(wsk_hit_id(&o, 10, 40) != 0 && strcmp(wsk_hit_id(&o, 10, 40), "head") == 0,
		   "above the pane, where a0's uncut rectangle reaches, is the header (D13)");
		{
			const char *ka[1] = { "a0" }, *kb[1] = { "/pane/row3/a3" };
			ok(wsk_rows(&o, ka, 1, &y0, &y1) == WSK_OK && y0 == 48 && y1 == 72,
			   "rows of a0 are its cut rows: 48..66, so 48..72");
			ok(wsk_rows(&o, kb, 1, &y0, &y1) == WSK_E_EMPTY,
			   "and of a3, by path, out of view: nothing to repaint (D14)");
		}
	}

	/* A label exactly as wide as its text draws all of it. Until 2026-11-11
	   a label wanted two pixels of slack, so a row with no padding -- whose
	   widgets are exactly their text -- cut the last character of every
	   one: "OK" drew "O". Found building §5f. */
	{
		static const wsk_widget_t TIGHT[] = {
			{ .kind = WSK_BUTTON, .id = "k", .text = "OK" },
		};
		wsk_widget_t one[1];
		wsk_tree_t r;
		char td[512];

		wsk_init(&r, WSK_ROW, 320, 172);
		r.pad = 0; r.item = TIGHT; r.n = 1;
		ok(wsk_rect(&r, 0).w == 16, "a row with no padding: OK is 16 wide, its text exactly");
		render_into(&r, td, sizeof td, 4096);
		ok(has(td, ">OK<"), "and draws both characters");
		wsk_clone(one, TIGHT, 1);
		one[0].size = 15;
		r.item = one;
		render_into(&r, td, sizeof td, 4096);
		ok(has(td, ">O<") && !has(td, ">OK<"),
		   "CONTROL: a pixel narrower, it is still truncated to what fits");
	}

	/* ---- §5g. wrap: onto a new line when it does not fit ---- */
	printf("\n-- §5g  wrap\n");
	{
		static const wsk_widget_t TAGS[] = {
			{ .kind = WSK_BUTTON, .id = "t0", .text = "Alpha" },
			{ .kind = WSK_BUTTON, .id = "t1", .text = "Beta" },
			{ .kind = WSK_BUTTON, .id = "t2", .text = "Gamma" },
			{ .kind = WSK_BUTTON, .id = "t3", .text = "Delta" },
			{ .kind = WSK_BUTTON, .id = "t4", .text = "Epsilon" },
			{ .kind = WSK_BUTTON, .id = "t5", .text = "Zeta" },
			{ .kind = WSK_BUTTON, .id = "t6", .text = "Eta" },
		};
		wsk_widget_t tags[7];
		wsk_tree_t w;
		int which = -2, y0 = 0, y1 = 0;

		wsk_clone(tags, TAGS, 7);
		wsk_init(&w, WSK_ROW, 320, 172);
		w.pad = 4; w.gap = 4; w.ids = 1; w.item = tags; w.n = 7;
		ok(wsk_check(&w, &which) == WSK_E_FIT, "CONTROL: unwrapped, seven buttons do not fit a 320 row");
		w.overflow = WSK_WRAP;
		ok(wsk_check(&w, 0) == WSK_OK, "wrapping, they are accepted");

		/* By hand: each is its text (8 a cell) plus 4 each side. Along a
		   312 span, 48+40+48+48+64+40 and five gaps of 4 is 308; Eta's 32
		   would make 344, so it starts a line 64 + 4 below the first. */
		ok(rect_is(wsk_rect(&w, 0), 4, 4, 48, 64), "Alpha starts the first line: 4,4 48x64");
		ok(rect_is(wsk_rect(&w, 4), 204, 4, 64, 64), "Epsilon: 204,4 64x64");
		ok(rect_is(wsk_rect(&w, 5), 272, 4, 40, 64), "Zeta ends the first line at 312, inside the padding");
		ok(rect_is(wsk_rect(&w, 6), 4, 72, 32, 64), "Eta starts the second: 4,72 32x64");

		/* Rule 3: justify and weight, a line at a time. */
		w.justify = WSK_CENTER;
		ok(wsk_rect(&w, 0).x == 6 && wsk_rect(&w, 6).x == 144,
		   "centred, each line by its own leftover: 4 on the first, 280 on the second");
		w.justify = WSK_START;
		tags[6].weight = 1;
		ok(rect_is(wsk_rect(&w, 6), 4, 72, 312, 64) && wsk_rect(&w, 0).x == 4 &&
		   wsk_rect(&w, 5).w == 40,
		   "a weighted item takes its own line's leftover, and the line with none is untouched");
		tags[6].weight = 0;

		/* Lines have a gap of their own, and WSK_SAME is the word gap. */
		ok(w.lgap == WSK_SAME, "a new stack's line gap is its gap");
		w.lgap = 0;
		ok(wsk_rect(&w, 6).y == 68 && wsk_rect(&w, 5).x == 272,
		   "lgap 0: the second line sits right under the first, words unmoved");
		w.lgap = WSK_SAME;

		/* hit and rows ask wsk_rect, so they follow the lines. */
		ok(wsk_hit(&w, 10, 100) == 6, "hit on the second line is Eta");
		ok(wsk_hit(&w, 10, 70) == WSK_E_NOTFOUND, "CONTROL: the gap between lines is nothing");
		{
			const char *k[1] = { "t6" };
			ok(wsk_rows(&w, k, 1, &y0, &y1) == WSK_OK && y0 == 72 && y1 == 136,
			   "rows of Eta are its line: 72..136");
		}

		/* Rule 5. */
		wsk_area(&w, 0, 0, 60, 172);
		ok(wsk_check(&w, &which) == WSK_E_FIT && which == 4,
		   "an item wider than a whole line is refused, naming it: Epsilon's 64 in 52");
		wsk_area(&w, 0, 0, 320, 100);
		ok(wsk_check(&w, &which) == WSK_E_FIT && which == 6,
		   "lines deeper than the area are refused at the first item past it: Eta");
		wsk_area(&w, 0, 0, 320, 140);
		ok(wsk_check(&w, 0) == WSK_OK, "CONTROL: 132 of lines in 132 rows, accepted");
	}

	/* Rule 4: a wrapping row in a column is as deep as its lines at the
	   width the column gives it -- and at another width, another depth. */
	{
		static const wsk_widget_t COLM[] = {
			{ .kind = WSK_STACK, .id = "tags",  .sub = 0 },
			{ .kind = WSK_LABEL, .id = "after", .text = "after", .size = 24 },
		};
		static const wsk_widget_t TAGS[] = {
			{ .kind = WSK_BUTTON, .id = "t0", .text = "Alpha" },
			{ .kind = WSK_BUTTON, .id = "t1", .text = "Beta" },
			{ .kind = WSK_BUTTON, .id = "t2", .text = "Gamma" },
			{ .kind = WSK_BUTTON, .id = "t3", .text = "Delta" },
			{ .kind = WSK_BUTTON, .id = "t4", .text = "Epsilon" },
			{ .kind = WSK_BUTTON, .id = "t5", .text = "Zeta" },
			{ .kind = WSK_BUTTON, .id = "t6", .text = "Eta" },
		};
		wsk_tree_t c, k[1];

		wsk_init(&c, WSK_COL, 320, 300);
		c.pad = 0; c.gap = 0; c.item = COLM; c.n = 2; c.nest = k; c.nests = 1;
		wsk_nest_init(&k[0], &c, WSK_ROW);
		k[0].pad = 4; k[0].gap = 4; k[0].item = TAGS; k[0].n = 7; k[0].overflow = WSK_WRAP;
		ok(wsk_check(&c, 0) == WSK_OK, "a wrapping row in a column is accepted");
		ok(rect_is(rect_of(&c, "tags"), 0, 0, 320, 140) && rect_of(&c, "after").y == 140,
		   "at 320 wide its two lines are 132 deep, plus padding: 140");
		ok(rect_is(rect_of(&c, "t6"), 4, 72, 32, 64), "and Eta, inside it, where the flat row put it");
		wsk_area(&c, 0, 0, 200, 300);
		ok(rect_of(&c, "tags").h == 208 && rect_of(&c, "after").y == 208,
		   "at 200 wide it wraps into three lines: 3 x 64 + 2 x 4 + 8 = 208");
		ok(rect_is(rect_of(&c, "t3"), 4, 72, 48, 64), "and Delta now starts the second line");
		{
			char wd[4096];
			FILE *f;
			ok(render_into(&c, wd, sizeof wd, 4096) > 0, "it renders");
			f = fopen("/tmp/wsk_wrap.svg", "wb"); fputs(wd, f); fclose(f);
		}
	}

	/* ---- §5h. a paragraph as one item ---- */
	printf("\n-- §5h  text items\n");
	{
		static const char P1[] = "Alpha Beta Gamma Delta Epsilon Zeta Eta Theta";
		static const wsk_widget_t AS_LABELS[] = {
			{ .kind = WSK_LABEL, .id = "a", .text = "Alpha" },
			{ .kind = WSK_LABEL, .id = "b", .text = "Beta" },
			{ .kind = WSK_LABEL, .id = "c", .text = "Gamma" },
			{ .kind = WSK_LABEL, .id = "d", .text = "Delta" },
			{ .kind = WSK_LABEL, .id = "e", .text = "Epsilon" },
			{ .kind = WSK_LABEL, .id = "f", .text = "Zeta" },
			{ .kind = WSK_LABEL, .id = "g", .text = "Eta" },
			{ .kind = WSK_LABEL, .id = "h", .text = "Theta" },
		};
		static const wsk_widget_t AS_TEXT[] = {
			{ .kind = WSK_TEXT, .id = "p", .sub = 0 },
		};
		const char *strs[4];
		wsk_tree_t L, T;
		int same = 1, which = -2, word = -9;
		const wsk_tree_t *in = 0;
		char d1[2048], d2[2048];
		int n1, n2;

		strs[0] = P1;
		wsk_init(&L, WSK_ROW, 320, 172);
		L.pad = 0; L.gap = 8; L.lgap = 0; L.overflow = WSK_WRAP; L.ids = 0;
		L.item = AS_LABELS; L.n = 8;
		T = L;
		T.item = AS_TEXT; T.n = 1; T.str = strs; T.strs = 1;

		ok(wsk_check(&T, 0) == WSK_OK, "a paragraph as one text item is accepted");
		/* Rule 2: each word where the same word as a label was, at two
		   widths -- the same layout from one item and one string. */
		for (int width = 320; width >= 120; width -= 200) {
			wsk_area(&L, 0, 0, width, 172);
			wsk_area(&T, 0, 0, width, 172);
			for (int k = 0; k < 8; k++) {
				wsk_rect_t a = wsk_rect(&L, k), b = wsk__wrap_unit(&T, 0, k);
				if (!rect_is(b, a.x, a.y, a.w, a.h)) same = 0;
			}
		}
		ok(same, "every word lands where the same word as a label did, at 320 and at 120 wide");
		wsk_area(&L, 0, 0, 320, 172);
		wsk_area(&T, 0, 0, 320, 172);
		ok(rect_is(wsk__wrap_unit(&T, 0, 7), 0, 24, 40, 24),
		   "by hand: Theta starts the second line, 0,24 40x24");
		ok(rect_is(wsk_rect(&T, 0), 0, 0, 312, 48),
		   "the item's rectangle is the box round all its words: two lines");

		/* Rule 5: runs. One <text> a line rather than one a word. */
		n1 = render_into(&L, d1, sizeof d1, 4096);
		n2 = render_into(&T, d2, sizeof d2, 4096);
		ok(count(d2, "<text") == 2 && count(d1, "<text") == 8,
		   "the paragraph is two runs, one a line; as labels it was eight");
		ok(has(d2, ">Alpha Beta Gamma Delta Epsilon Zeta Eta</text>") && has(d2, ">Theta</text>"),
		   "a run says the line's words as the string says them");
		printf("    %d bytes as eight labels, %d as one text item\n", n1, n2);
		ok(n2 < n1, "and the document is smaller");
		T.gap = 9;
		render_into(&T, d2, sizeof d2, 4096);
		ok(count(d2, "<text") == 8,
		   "CONTROL: a gap that is not one advance draws a word at a time");
		T.gap = 8;

		/* Rule 6: hit names the item only on a word, and says which. */
		ok(wsk_hit(&T, 50, 10) == 0, "hit on Beta is the paragraph");
		ok(wsk_hit(&T, 44, 10) == WSK_E_NOTFOUND,
		   "CONTROL: the gap between two words is nothing");
		ok(wsk_hit(&T, 300, 30) == WSK_E_NOTFOUND,
		   "CONTROL: past the end of the second line is nothing, inside the box");
		ok(wsk_hit_word(&T, 50, 10, &in, &word) == 0 && word == 1 && in == &T,
		   "wsk_hit_word: Beta is word 1");
		ok(wsk_hit_word(&T, 10, 30, &in, &word) == 0 && word == 7, "and Theta is word 7");
		{
			const char *k[1] = { "p" };
			int y0 = 0, y1 = 0;
			ok(wsk_rows(&T, k, 1, &y0, &y1) == WSK_OK && y0 == 0 && y1 == 48,
			   "rows of a paragraph are every line it is on: 0..48");
		}

		/* An item among the words: a button inline. */
		{
			static const char A[] = "one two", B[] = "three";
			static const wsk_widget_t MIX[] = {
				{ .kind = WSK_TEXT,   .id = "t1", .sub = 1 },
				{ .kind = WSK_BUTTON, .id = "ok", .text = "OK" },
				{ .kind = WSK_TEXT,   .id = "t2", .sub = 2 },
			};
			wsk_tree_t M = T;
			strs[1] = A; strs[2] = B;
			M.item = MIX; M.n = 3; M.strs = 3;
			ok(wsk_check(&M, 0) == WSK_OK, "words, a button, words: accepted");
			/* one 24, two 24, OK 16, three 40, gaps of 8; the line is as
			   deep as the button, 64, and the words sit in it. */
			ok(rect_is(wsk__wrap_unit(&M, 0, 1), 32, 0, 24, 64) &&
			   rect_is(wsk_rect(&M, 1), 64, 0, 16, 64) &&
			   rect_is(wsk__wrap_unit(&M, 2, 0), 88, 0, 40, 64),
			   "a button inline among words, by hand: two 32, OK 64, three 88, all 64 deep");
		}

		/* Rule 4: refusals beside their controls. */
		wsk_area(&T, 0, 0, 50, 172);
		ok(wsk_check(&T, &which) == WSK_E_FIT && which == 0,
		   "a word wider than the line is refused, naming the item: Epsilon's 56 in 50");
		wsk_area(&T, 0, 0, 320, 172);
		{
			static const char BAD[] = "a <b> c";
			strs[3] = BAD;
			wsk_widget_t one[1];
			wsk_clone(one, AS_TEXT, 1);
			T.item = one; T.strs = 4;
			one[0].sub = 3;
			ok(wsk_check(&T, &which) == WSK_E_TEXT && which == 0,
			   "a string the window cannot draw is refused, WSK_E_TEXT");
			one[0].sub = 4;
			ok(wsk_check(&T, &which) == WSK_E_KIND && which == 0,
			   "a string index out of range is refused");
			one[0].sub = 0;
			ok(wsk_check(&T, 0) == WSK_OK, "CONTROL: the good string, accepted");
			T.axis = WSK_COL;
			ok(wsk_check(&T, &which) == WSK_E_KIND,
			   "a text item in a wrapping column is refused: its words would stand one a line");
			T.axis = WSK_ROW;
			T.item = AS_TEXT;
		}

		/* Rule 3: outside a wrapping row, a long label. */
		{
			static const char LONG[] = "A label longer than thirty-two characters";
			wsk_tree_t C;
			char d3[1024];
			wsk_init(&C, WSK_COL, 320, 172);
			strs[0] = LONG;
			C.item = AS_TEXT; C.n = 1; C.str = strs; C.strs = 1;
			ok(wsk_check(&C, 0) == WSK_OK, "a text item in a plain column is accepted");
			render_into(&C, d3, sizeof d3, 4096);
			ok(has(d3, ">A label longer than thirty-two charact</text>"),
			   "and drawn as a label: one line, centred, truncated to the 38 cells that fit");
			strs[0] = P1;
		}
	}

	/* ---- §5i. the toolkit: an event in, what changed out ---- */
	printf("\n-- §5i  the toolkit\n");
	{
		static const wsk_widget_t TOP[] = {
			{ .kind = WSK_STACK, .id = "bar",  .sub = 0, .size = 64 },
			{ .kind = WSK_STACK, .id = "list", .sub = 1, .weight = 1 },
			{ .kind = WSK_LABEL, .id = "foot", .text = "foot", .size = 24 },
		};
		static const wsk_widget_t BAR[] = {
			{ .kind = WSK_BUTTON, .id = "a", .text = "A", .weight = 1 },
			{ .kind = WSK_BUTTON, .id = "b", .text = "B", .weight = 1 },
		};
		static wsk_widget_t rows[10];
		wsk_tree_t w, k[2];
		wsk_ui_t ui;
		wsk_note_t nt[4];
		wsk_event_t ev;
		int n, y0 = -1, y1 = -1;

		for (int i = 0; i < 10; i++) {
			memset(&rows[i], 0, sizeof rows[i]);
			rows[i].kind = WSK_BUTTON;
			rows[i].size = 40;
			snprintf(rows[i].id, WSK_ID_MAX, "r%d", i);
			snprintf(rows[i].text, WSK_TEXT_MAX, "Row %d", i);
		}
		wsk_init(&w, WSK_COL, 320, 300);
		w.pad = 0; w.gap = 0; w.item = TOP; w.n = 3; w.nest = k; w.nests = 2;
		wsk_nest_init(&k[0], &w, WSK_ROW); k[0].pad = 0; k[0].gap = 0; k[0].item = BAR; k[0].n = 2;
		wsk_nest_init(&k[1], &w, WSK_COL); k[1].pad = 0; k[1].gap = 0; k[1].item = rows; k[1].n = 10;
		k[1].overflow = WSK_SCROLL;
		ok(wsk_check(&w, 0) == WSK_OK, "a bar, a scrolling list of ten, a footer");
		wsk_ui_init(&ui, &w);
		memset(&ev, 0, sizeof ev);

#define FEED(K, X, Y) (ev.kind = (K), ev.x = (X), ev.y = (Y), \
                       n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1))

		/* Press and click. */
		FEED(WSK_EV_DOWN, 50, 30);
		ok(n == 0 && w.held == &BAR[0] && y0 == 0 && y1 == 64,
		   "DOWN on a button holds it -- on the root, not the widget -- and asks for its rows");
		FEED(WSK_EV_UP, 60, 20);
		ok(n == 1 && nt[0].kind == WSK_NOTE_CLICK && nt[0].in == &k[0] && nt[0].item == 0 &&
		   nt[0].value == -1, "UP on it is a click: the bar's item 0");
		ok(w.held == 0 && y0 == 0 && y1 == 64, "and the hold ends, with its rows to repaint");
		FEED(WSK_EV_DOWN, 250, 30);
		FEED(WSK_EV_UP, 50, 30);
		ok(n == 0 && w.held == 0, "CONTROL: DOWN on B, UP on A, is no click");
		FEED(WSK_EV_DOWN, 160, 64 + 24);
		FEED(WSK_EV_UP, 160, 300 - 10);
		ok(n == 0, "nor is DOWN on a row and UP on the footer");

		/* A tap with jitter: inside the slop it is still a tap. */
		FEED(WSK_EV_DOWN, 100, 154);
		ok(w.held == &rows[2], "DOWN on row 2 of the list holds it");
		FEED(WSK_EV_MOVE, 100, 154 - 10);
		ok(n == 0 && k[1].scroll == 0, "a MOVE of 10, inside the slop of 16, scrolls nothing");
		FEED(WSK_EV_UP, 100, 145);
		ok(n == 1 && nt[0].kind == WSK_NOTE_CLICK && nt[0].in == &k[1] && nt[0].item == 2,
		   "and the UP is a click on row 2");

		/* A drag: past the slop it scrolls, clamped, and is no click. */
		FEED(WSK_EV_DOWN, 100, 200);
		FEED(WSK_EV_MOVE, 100, 150);
		ok(n == 1 && nt[0].kind == WSK_NOTE_SCROLL && nt[0].in == &k[1] && nt[0].value == 50 &&
		   k[1].scroll == 50, "a MOVE of 50 drags the list: SCROLL to 50");
		ok(w.held == 0 && y0 == 64 && y1 == 280,
		   "the hold lets go, and the rows are the list's area: 64..276, so 64..280");
		FEED(WSK_EV_MOVE, 100, -300);
		ok(n == 1 && nt[0].value == 188 && k[1].scroll == 188,
		   "dragged far past the end it stops at the end: 400 of rows in 212 is 188");
		ok(wsk_check(&w, 0) == WSK_OK, "and the tree it left is one layer 1 accepts (D11)");
		FEED(WSK_EV_MOVE, 100, 600);
		ok(n == 1 && nt[0].value == 0, "and past the start, at 0");
		FEED(WSK_EV_MOVE, 100, 650);
		ok(n == 0 && y0 == y1, "CONTROL: further past the start changes nothing and repaints nothing");
		FEED(WSK_EV_UP, 100, 650);
		ok(n == 0, "an UP after a drag is not a click");

		/* The wheel. */
		ev.d = 30;
		FEED(WSK_EV_WHEEL, 100, 150);
		ok(n == 1 && nt[0].value == 30 && k[1].scroll == 30, "the wheel over the list scrolls it 30");
		FEED(WSK_EV_WHEEL, 100, 30);
		ok(n == 0 && y0 == y1, "CONTROL: over the bar, which does not scroll, nothing");
		ev.d = 0;
		k[1].scroll = 0;
#undef FEED
	}

	/* ---- §5j. fields that edit, and focus ---- */
	printf("\n-- §5j  editing\n");
	{
		static const wsk_widget_t FORMW[] = {
			{ .kind = WSK_LABEL, .id = "l1",   .text = "Name", .size = 24 },
			{ .kind = WSK_FIELD, .id = "name", .sub = 0, .size = 32 },
			{ .kind = WSK_LABEL, .id = "l2",   .text = "City", .size = 24 },
			{ .kind = WSK_FIELD, .id = "city", .sub = 1, .size = 32 },
			{ .kind = WSK_STACK, .id = "row",  .sub = 0, .size = 40 },
		};
		static const wsk_widget_t ROWW[] = {
			{ .kind = WSK_BUTTON, .id = "q",    .text = "Q", .size = 40 },
			{ .kind = WSK_FIELD,  .id = "note", .sub = 2, .weight = 1 },
			{ .kind = WSK_BUTTON, .id = "w",    .text = "W", .size = 40 },
		};
		char nb[8] = "", cb[48] = "", tb[32] = "";
		wsk_buf_t buf[3];
		wsk_tree_t w, k[1];
		wsk_ui_t ui;
		wsk_note_t nt[4];
		wsk_event_t ev;
		int n, y0 = 0, y1 = 0;
		char dd[4096];

		memset(buf, 0, sizeof buf);             /* a buffer grows fields: zero it first */
		buf[0].b = nb; buf[0].cap = 8;  buf[0].len = 0; buf[0].caret = 0;
		buf[1].b = cb; buf[1].cap = 48; buf[1].len = 0; buf[1].caret = 0;
		buf[2].b = tb; buf[2].cap = 32; buf[2].len = 0; buf[2].caret = 0;
		wsk_init(&w, WSK_COL, 320, 300);
		w.pad = 4; w.gap = 4; w.item = FORMW; w.n = 5; w.nest = k; w.nests = 1;
		w.buf = buf; w.bufs = 3;
		wsk_nest_init(&k[0], &w, WSK_ROW); k[0].pad = 0; k[0].gap = 4; k[0].item = ROWW; k[0].n = 3;
		ok(wsk_check(&w, 0) == WSK_OK, "a form of three editable fields, one two deep, is accepted");
		wsk_ui_init(&ui, &w);
		memset(&ev, 0, sizeof ev);

#define TYPE(T) (ev.kind = WSK_EV_TEXT, snprintf(ev.text, sizeof ev.text, "%s", (T)), \
                 n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1))
#define KEY(K)  (ev.kind = WSK_EV_KEY, ev.key = (K), n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1))
#define TAP(X, Y) (ev.kind = WSK_EV_DOWN, ev.x = (X), ev.y = (Y), \
                   n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1), \
                   ev.kind = WSK_EV_UP, n += wsk_feed(&ui, &ev, nt + 1, 3, &y0, &y1))

		TYPE("x");
		ok(n == 0 && nb[0] == '\0', "CONTROL: with nothing focused, typing goes nowhere");
		ok(wsk_ui_focus(&ui, "name", &y0, &y1) == WSK_OK && w.focus == &FORMW[1] &&
		   y0 == 32 && y1 == 64, "a program focuses a field by id; its rows come back");
		render_into(&w, dd, sizeof dd, 4096);
		ok(count(dd, "width=\"2\"") == 1, "and the focused field, empty, draws its caret");
		ok(wsk_ui_focus(&ui, "l1", &y0, &y1) == WSK_E_NOTFOUND, "CONTROL: a label cannot take focus");

		TYPE("ab");
		ok(n == 1 && nt[0].kind == WSK_NOTE_TEXT && nt[0].value == 2 && strcmp(nb, "ab") == 0 &&
		   buf[0].caret == 2 && y0 == 32 && y1 == 64, "TEXT inserts at the caret: \"ab\", a TEXT notice, the field's rows");
		TYPE("<");
		ok(n == 0 && strcmp(nb, "ab") == 0, "a character the window cannot draw is refused");
		TYPE("cdefg");
		ok(strcmp(nb, "abcdefg") == 0 && buf[0].len == 7, "seven characters fill a buffer of 8");
		TYPE("h");
		ok(n == 0 && strcmp(nb, "abcdefg") == 0, "and the eighth is refused: all of it or none of it");
		KEY(WSK_KEY_LEFT); KEY(WSK_KEY_LEFT);
		ok(buf[0].caret == 5 && n == 0 && y1 > y0, "LEFT twice: the caret at 5, rows to repaint, no notice");
		KEY(WSK_KEY_BACKSPACE);
		ok(strcmp(nb, "abcdfg") == 0 && buf[0].caret == 4 && n == 1, "BACKSPACE takes the e");
		KEY(WSK_KEY_DELETE);
		ok(strcmp(nb, "abcdg") == 0 && buf[0].caret == 4, "DELETE takes the f");
		KEY(WSK_KEY_HOME);
		TYPE("X");
		ok(strcmp(nb, "Xabcdg") == 0 && buf[0].caret == 1, "HOME, then X goes in at the start");
		KEY(WSK_KEY_END);
		ok(buf[0].caret == 6, "END");
		KEY(WSK_KEY_HOME); KEY(WSK_KEY_LEFT);
		ok(n == 0 && y0 == y1 && buf[0].caret == 0, "CONTROL: LEFT at the start changes and repaints nothing");
		KEY(WSK_KEY_ENTER);
		ok(n == 1 && nt[0].kind == WSK_NOTE_ENTER && nt[0].in == &w && nt[0].item == 1,
		   "ENTER is a notice, naming the field");
		ok(wsk_check(&w, 0) == WSK_OK, "the buffer the toolkit left is one wsk_check accepts");

		/* Tab order: declaration order, depth first, round and back. */
		KEY(WSK_KEY_TAB);
		ok(n == 1 && nt[0].kind == WSK_NOTE_FOCUS && w.focus == &FORMW[3], "TAB: name to city");
		/* Since §5m, Tab reaches buttons too: the row's Q and W are in the
		   order -- a deliberate change to these three checks, which were
		   written when only fields took focus. */
		KEY(WSK_KEY_TAB);
		ok(w.focus == &ROWW[0], "TAB: city to the Q button, two deep in the row (§5m)");
		KEY(WSK_KEY_TAB);
		ok(w.focus == &ROWW[1] && nt[0].in == &k[0] && nt[0].item == 1,
		   "TAB: to note, beside it");
		KEY(WSK_KEY_TAB);
		KEY(WSK_KEY_TAB);
		ok(w.focus == &FORMW[1], "TAB past W, the last, goes round to the first");
		KEY(WSK_KEY_BACKTAB);
		KEY(WSK_KEY_BACKTAB);
		ok(w.focus == &ROWW[1], "and BACKTAB back round past W to note");

		/* A tap: on a field, focus and a caret where it landed; on a key,
		   focus stays where it was. */
		snprintf(cb, sizeof cb, "hello"); buf[1].len = 5; buf[1].caret = 5;
		TAP(4 + WSK_FIELD_PAD + 2 * 8 + 1, 96 + 16);
		ok(w.focus == &FORMW[3] && buf[1].caret == 2,
		   "a tap on city, two characters in: focus, and the caret at 2");
		TAP(20, 160);
		ok(w.focus == &FORMW[3] && n == 1 && nt[1].kind == WSK_NOTE_CLICK && nt[1].item == 0,
		   "a tap on the Q key clicks it and leaves focus in city -- the keyboard's control");
		TYPE("Q");
		ok(strcmp(cb, "heQllo") == 0, "so a key's click can type into the field it left focused");

		/* A long field keeps its caret in sight. */
		snprintf(cb, sizeof cb, "abcdefghijklmnopqrstuvwxyz0123456789");
		buf[1].len = 36; buf[1].caret = 20;
		render_into(&w, dd, sizeof dd, 4096);
		{
			/* city is 312 wide: (312 - 12 - 2) / 8 = 37 cells, so it all
			   fits; narrow it to see the window slide. */
			wsk_widget_t form2[5];
			char want[64];
			wsk_clone(form2, FORMW, 5);
			form2[3].weight = 0;
			w.item = form2;
			w.axis = WSK_COL;
			wsk_area(&w, 0, 0, 104, 300);
			k[0].item = ROWW;
			/* 104 - 8 = 96 wide: (96 - 12 - 2) / 8 = 10 cells. The caret at
			   20 is at the right edge: characters 10..19 show. */
			w.focus = &form2[3];
			render_into(&w, dd, sizeof dd, 4096);
			snprintf(want, sizeof want, ">%s</text>", "klmnopqrst");
			ok(has(dd, want), "a long field with the caret at 20 shows 10..19, the caret at its edge");
			buf[1].caret = 36;
			render_into(&w, dd, sizeof dd, 4096);
			ok(has(dd, ">0123456789</text>"), "and with the caret at the end, the tail -- §5d's view");
			w.item = FORMW;
			wsk_area(&w, 0, 0, 320, 300);
		}
#undef TYPE
#undef KEY
#undef TAP
	}

	/* ---- §5l. overlays: a menu over the window ---- */
	printf("\n-- §5l  overlays\n");
	{
		static const wsk_widget_t TOPO[] = {
			{ .kind = WSK_STACK,  .id = "bar",   .sub = 0, .size = 40 },
			{ .kind = WSK_BUTTON, .id = "under", .text = "Under", .weight = 1 },
		};
		static const wsk_widget_t BARO[] = {
			{ .kind = WSK_BUTTON, .id = "file", .text = "File", .weight = 1 },
			{ .kind = WSK_BUTTON, .id = "edit", .text = "Edit", .weight = 1 },
			{ .kind = WSK_BUTTON, .id = "view", .text = "View", .weight = 1 },
		};
		static const wsk_widget_t MENU1[] = {
			{ .kind = WSK_BUTTON, .id = "open", .text = "Open", .size = 32 },
			{ .kind = WSK_BUTTON, .id = "save", .text = "Save", .size = 32 },
			{ .kind = WSK_BUTTON, .id = "quit", .text = "Quit", .size = 32 },
		};
		static wsk_widget_t tall[12];
		wsk_tree_t w, k[4];
		wsk_ui_t ui;
		wsk_note_t nt[4];
		wsk_event_t ev;
		char od[4096];
		int n, y0 = 0, y1 = 0, which = -2;
		wsk_rect_t a = { 300, 250, 20, 40 };

		for (int i = 0; i < 12; i++) {
			memset(&tall[i], 0, sizeof tall[i]);
			tall[i].kind = WSK_BUTTON; tall[i].size = 32;
			snprintf(tall[i].id, WSK_ID_MAX, "m%d", i);
			snprintf(tall[i].text, WSK_TEXT_MAX, "Item %d", i);
		}
		wsk_init(&w, WSK_COL, 320, 300);
		w.pad = 0; w.gap = 0; w.ids = 1; w.item = TOPO; w.n = 2; w.nest = k; w.nests = 4;
		wsk_nest_init(&k[0], &w, WSK_ROW); k[0].pad = 0; k[0].gap = 0; k[0].item = BARO; k[0].n = 3;
		wsk_nest_init(&k[1], &w, WSK_COL); k[1].pad = 4; k[1].gap = 2; k[1].item = MENU1; k[1].n = 3;
		k[1].bg = 0x303840u;
		wsk_nest_init(&k[2], &w, WSK_COL); k[2].pad = 4; k[2].gap = 2; k[2].item = tall; k[2].n = 12;
		k[3] = k[2]; k[3].overflow = WSK_SCROLL;
		ok(wsk_check(&w, 0) == WSK_OK && w.over == WSK_NONE, "a window with nothing open");

		/* Rule 1, by hand. File is 0..106 of the bar (three weighted
		   buttons in 320). The menu's content is its widest button, 4 + 32
		   + 4 = 40, and its padding: 48 -- narrower than File, so File's
		   106 wide; and three of 32, two gaps and padding: 108 deep. */
		ok(wsk_over_open(&w, 1, rect_of(&w, "file")) == WSK_OK &&
		   w.ox == 0 && w.oy == 40 && w.ow == 106 && w.oh == 108,
		   "under File: 0,40 106x108 -- as wide as its anchor, as deep as its items");
		ok(wsk_check(&w, 0) == WSK_OK, "and the tree with it open is accepted");
		ok(wsk_over_open(&w, 1, a) == WSK_OK && w.ox == 272 && w.oy == 142,
		   "from a small anchor low on the right: above it (142), and in from the edge (272)");
		ok(wsk_over_open(&w, 2, rect_of(&w, "file")) == WSK_E_FIT,
		   "a menu taller than the screen is refused, WSK_E_FIT");
		ok(wsk_over_open(&w, 3, rect_of(&w, "file")) == WSK_OK && w.oh == 300 && w.oy == 0,
		   "CONTROL: the same menu, scrolling, is given the screen's height");
		k[3].scroll = 0;
		wsk_over_open(&w, 1, rect_of(&w, "file"));

		/* Rule 2: drawn after the root, on its own backing. */
		render_into(&w, od, sizeof od, 4096);
		ok(has(od, "<rect y=\"40\" width=\"106\" height=\"108\" fill=\"#303840\"/>"),
		   "the open menu has a backing of its own, the size of the menu");
		ok(strstr(od, "fill=\"#303840\"") > strstr(od, "id=\"under\"") &&
		   strstr(od, "id=\"open\"") > strstr(od, "fill=\"#303840\""),
		   "and it and its items come after the root's widgets: on top");

		/* Rule 3: hit asks it first, and nothing falls through. */
		ok(wsk_hit_id(&w, 10, 60) != 0 && strcmp(wsk_hit_id(&w, 10, 60), "open") == 0,
		   "a point on Open, over Under, is Open");
		ok(wsk_hit_id(&w, 2, 42) == 0, "a point in the menu's padding is nothing...");
		wsk_over_close(&w);
		ok(wsk_hit_id(&w, 2, 42) != 0 && strcmp(wsk_hit_id(&w, 2, 42), "under") == 0,
		   "CONTROL: ...where, with it closed, Under is");
		wsk_over_open(&w, 1, rect_of(&w, "file"));
		ok(strcmp(wsk_hit_id(&w, 200, 200), "under") == 0, "and off the menu, Under is Under");
		ok(rect_of(&w, "save").y == 78, "an overlay's ids are found as any are: save at 78");
		wsk_over_close(&w);

		/* Rule 4, the toolkit. */
		wsk_ui_init(&ui, &w);
		memset(&ev, 0, sizeof ev);
#define FEED(K, X, Y) (ev.kind = (K), ev.x = (X), ev.y = (Y), \
                       n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1))
		ok(wsk_ui_open(&ui, 1, "file", &y0, &y1) == WSK_OK && w.over == 1 && y0 == 40 && y1 == 152,
		   "wsk_ui_open: the menu under File, and its rows to repaint: 40..152");
		FEED(WSK_EV_DOWN, 50, 82);
		FEED(WSK_EV_UP, 50, 82);
		ok(n == 2 && nt[0].kind == WSK_NOTE_CLICK && nt[0].in == &k[1] && nt[0].item == 1 &&
		   nt[1].kind == WSK_NOTE_CLOSE && w.over == WSK_NONE && y0 == 40 && y1 == 152,
		   "choosing Save: a CLICK on it, then CLOSE -- the menu is gone and its rows repaint");
		wsk_ui_open(&ui, 1, "file", &y0, &y1);
		FEED(WSK_EV_DOWN, 200, 200);
		ok(n == 1 && nt[0].kind == WSK_NOTE_CLOSE && w.over == WSK_NONE && w.held == 0,
		   "a press off the menu closes it, and holds nothing beneath");
		FEED(WSK_EV_UP, 200, 200);
		ok(n == 0, "CONTROL: and its release is no click on Under");
		wsk_ui_open(&ui, 1, "file", &y0, &y1);
		FEED(WSK_EV_DOWN, 50, 60);
		FEED(WSK_EV_MOVE, 50, 200);
		FEED(WSK_EV_UP, 50, 200);
		ok(n == 0 && w.over == 1, "pressing Open and sliding off: no click, and the menu stays");
		ev.key = WSK_KEY_ESCAPE;
		FEED(WSK_EV_KEY, 0, 0);
		ok(n == 1 && nt[0].kind == WSK_NOTE_CLOSE && w.over == WSK_NONE, "Escape closes it");
#undef FEED

		/* A place set by hand is checked like everything else. */
		w.over = 1; w.ox = 300; w.oy = 40; w.ow = 106; w.oh = 108;
		ok(wsk_check(&w, &which) == WSK_E_KIND && which == -1,
		   "an overlay placed off the screen by hand is refused");
		w.over = WSK_NONE;
	}

	/* ---- §5m. check, selection, focus on buttons ---- */
	printf("\n-- §5m  check, selection, focus\n");
	{
		static const wsk_widget_t FORMC[] = {
			{ .kind = WSK_CHECK,  .id = "wifi", .text = "WiFi", .sub = 0, .size = 32 },
			{ .kind = WSK_CHECK,  .id = "bt",   .text = "Bluetooth", .sub = 1, .size = 32 },
			{ .kind = WSK_BUTTON, .id = "ok",   .text = "OK", .size = 40 },
			{ .kind = WSK_STACK,  .id = "list", .sub = 0, .weight = 1 },
		};
		static const wsk_widget_t LISTC[] = {
			{ .kind = WSK_BUTTON, .id = "a0", .text = "Apple",  .size = 30 },
			{ .kind = WSK_BUTTON, .id = "a1", .text = "Banana", .size = 30 },
			{ .kind = WSK_BUTTON, .id = "a2", .text = "Cherry", .size = 30 },
			{ .kind = WSK_BUTTON, .id = "a3", .text = "Date",   .size = 30 },
			{ .kind = WSK_BUTTON, .id = "a4", .text = "Elder",  .size = 30 },
		};
		unsigned char st[2] = { 0, 1 };
		wsk_tree_t w, k[1];
		wsk_ui_t ui;
		wsk_note_t nt[4];
		wsk_event_t ev;
		char cd[4096];
		int n, y0 = 0, y1 = 0;

		wsk_init(&w, WSK_COL, 320, 300);
		w.pad = 4; w.gap = 4; w.ids = 1; w.item = FORMC; w.n = 4; w.nest = k; w.nests = 1;
		wsk_nest_init(&k[0], &w, WSK_COL); k[0].pad = 0; k[0].gap = 2;
		k[0].item = LISTC; k[0].n = 5; k[0].select = 1;
		ok(wsk_check(&w, 0) == WSK_OK, "two checks, a button and a selecting list");

		/* Rule 1, without state: drawn unticked, read-only. */
		render_into(&w, cd, sizeof cd, 4096);
		ok(has(cd, "<rect id=\"bt\" x=\"8\" y=\"50\" width=\"12\" height=\"12\" fill=\"#fff\"/>"
		           "<rect x=\"10\" y=\"52\" width=\"8\" height=\"8\" fill=\"#081620\"/>") &&
		   !has(cd, "width=\"6\" height=\"6\""),
		   "with no state, a check is a box -- a border in the ink, the trough inside -- and nothing in it");
		/* With state: Bluetooth's byte is 1. */
		w.state = st; w.states = 2;
		render_into(&w, cd, sizeof cd, 4096);
		ok(has(cd, "<rect id=\"wifi\" x=\"8\" y=\"14\" width=\"12\" height=\"12\" fill=\"#fff\"/>"),
		   "WiFi, unticked: a 12-pixel box (three quarters of the font), in from the padding");
		ok(has(cd, "<rect x=\"11\" y=\"53\" width=\"6\" height=\"6\" fill=\"#2a6b8f\"/>") &&
		   count(cd, "width=\"6\" height=\"6\"") == 1,
		   "Bluetooth, ticked: a smaller box inside its box, in the accent -- and only it");
		ok(has(cd, "<text text-anchor=\"start\" x=\"26\""),
		   "and the label starts after the box: 4 + 4 + 12 + 6 = 26");
		ok(!has(cd, "<path"), "in rectangles only: no <path>, so nothing the C5 cannot draw");

		/* Rules 2 and 3, by the toolkit. */
		wsk_ui_init(&ui, &w);
		memset(&ev, 0, sizeof ev);
#define TAP(X, Y) (ev.kind = WSK_EV_DOWN, ev.x = (X), ev.y = (Y), \
                   wsk_feed(&ui, &ev, nt, 4, &y0, &y1), \
                   ev.kind = WSK_EV_UP, n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1))
#define KEY(K)  (ev.kind = WSK_EV_KEY, ev.key = (K), n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1))
		TAP(20, 20);
		ok(n == 2 && nt[0].kind == WSK_NOTE_CLICK && nt[1].kind == WSK_NOTE_CHANGE &&
		   nt[1].value == 1 && st[0] == 1 && y0 == 0 && y1 == 40,
		   "a tap on WiFi: CLICK, then CHANGE to 1 -- in the caller's byte -- and its rows");
		TAP(20, 20);
		ok(st[0] == 0 && nt[1].value == 0, "and again: unticked");
		/* the list: OK is 76..116, so the list starts at 120; rows of 30 + 2 */
		TAP(100, 200);
		ok(n == 2 && nt[1].kind == WSK_NOTE_SELECT && nt[1].in == &k[0] && nt[1].value == 2 &&
		   k[0].sel == 2 && y0 == 184 && y1 == 216,
		   "a tap on Cherry selects it: SELECT 2, the list's sel, its rows 184..216");
		TAP(100, 130);
		ok(k[0].sel == 0 && y0 == 120 && y1 == 216,
		   "a tap on Apple: the old choice's rows and the new one's both repaint");
		render_into(&w, cd, sizeof cd, 4096);
		ok(has(cd, "<rect id=\"a0\" x=\"4\" y=\"120\" width=\"312\" height=\"30\" fill=\"#b06a2a\"/>"),
		   "and Apple is drawn in the list's selection fill");
		k[0].select = 0;
		TAP(100, 200);
		ok(n == 1 && k[0].sel == 0, "CONTROL: in a list that does not select, a tap is only a click");
		k[0].select = 1;
		w.state = 0; w.states = 0;
		TAP(20, 20);
		ok(n == 1, "CONTROL: a check with no state is read-only -- a click, no change");
		w.state = st; w.states = 2;

		/* Rule 4: focus reaches buttons and checks. */
		KEY(WSK_KEY_TAB);
		ok(w.focus == &FORMC[0], "TAB from nothing: the first focusable, the WiFi check");
		KEY(WSK_KEY_TAB);
		KEY(WSK_KEY_TAB);
		ok(w.focus == &FORMC[2], "and on to the OK button");
		render_into(&w, cd, sizeof cd, 4096);
		ok(has(cd, "<rect x=\"6\" y=\"78\" width=\"308\" height=\"2\" fill=\"#fff\"/>") &&
		   has(cd, "<rect x=\"312\" y=\"78\" width=\"2\" height=\"36\" fill=\"#fff\"/>"),
		   "the focused button has a ring: thin rectangles inset 2, in the ink");
		KEY(WSK_KEY_ENTER);
		ok(n == 1 && nt[0].kind == WSK_NOTE_CLICK && nt[0].in == &w && nt[0].item == 2,
		   "ENTER on the focused button is its click");
		KEY(WSK_KEY_BACKTAB);
		KEY(WSK_KEY_ENTER);
		ok(n == 2 && nt[1].kind == WSK_NOTE_CHANGE && st[1] == 0,
		   "BACKTAB to Bluetooth, ENTER: it toggles off");
		KEY(WSK_KEY_TAB);
		KEY(WSK_KEY_TAB);
		/* changed 2026-11-15, deliberately (§5z, Matthew: "a list or tree
		   should be one tab stop"): was "on into the list, depth first",
		   focus on its first button */
		ok(w.focus == &FORMC[3], "and Tab goes on to the list -- one stop, the list itself, not its rows");
		KEY(WSK_KEY_TAB);
		ok(w.focus == &FORMC[0], "and the next Tab is round to WiFi: the list's rows are not stops");
#undef TAP
#undef KEY
	}

	/* ---- §5n. table columns: the header decides ---- */
	printf("\n-- §5n  tables\n");
	{
		static const wsk_widget_t TOPT[] = {
			{ .kind = WSK_STACK, .id = "hdr",  .sub = 0, .size = 32 },
			{ .kind = WSK_STACK, .id = "body", .sub = 1, .weight = 1 },
		};
		static const wsk_widget_t HDR[] = {
			{ .kind = WSK_BUTTON, .id = "h_name", .text = "Name", .weight = 2 },
			{ .kind = WSK_BUTTON, .id = "h_size", .text = "Size", .size = 80 },
			{ .kind = WSK_BUTTON, .id = "h_date", .text = "Date", .weight = 1 },
		};
		static const wsk_widget_t BODYT[] = {
			{ .kind = WSK_STACK, .id = "r0", .sub = 2, .size = 28 },
			{ .kind = WSK_STACK, .id = "r1", .sub = 3, .size = 28 },
			{ .kind = WSK_STACK, .id = "r2", .sub = 4, .size = 28 },
			{ .kind = WSK_STACK, .id = "r3", .sub = 5, .size = 28 },
			{ .kind = WSK_STACK, .id = "r4", .sub = 6, .size = 28 },
			{ .kind = WSK_STACK, .id = "r5", .sub = 7, .size = 28 },
		};
		static const wsk_widget_t C0[] = { { .kind = WSK_LABEL, .id = "c00", .text = "alpha.txt" },
		                                   { .kind = WSK_LABEL, .id = "c01", .text = "12K" },
		                                   { .kind = WSK_LABEL, .id = "c02", .text = "Nov 1" } };
		static const wsk_widget_t C1[] = { { .kind = WSK_LABEL, .id = "c10", .text = "b" },
		                                   { .kind = WSK_LABEL, .id = "c11", .text = "1M" },
		                                   { .kind = WSK_LABEL, .id = "c12", .text = "Oct 30" } };
		static const wsk_widget_t CX[] = { { .kind = WSK_LABEL, .text = "x" },
		                                   { .kind = WSK_LABEL, .text = "y" } };
		static const wsk_widget_t C4[] = { { .kind = WSK_LABEL, .text = "1" }, { .kind = WSK_LABEL, .text = "2" },
		                                   { .kind = WSK_LABEL, .text = "3" }, { .kind = WSK_LABEL, .text = "4" } };
		wsk_tree_t w, k[8];
		wsk_ui_t ui;
		wsk_note_t nt[4];
		wsk_event_t ev;
		char td[8192];
		int which = -2, y0 = 0, y1 = 0, n;
		const wsk_tree_t *in = 0;

		wsk_init(&w, WSK_COL, 400, 120);
		w.pad = 0; w.gap = 0; w.ids = 1; w.item = TOPT; w.n = 2; w.nest = k; w.nests = 8;
		wsk_nest_init(&k[0], &w, WSK_ROW);
		k[0].pad = 0; k[0].gap = 0; k[0].item = HDR; k[0].n = 3; k[0].talign = WSK_ALIGN_START;
		wsk_nest_init(&k[1], &w, WSK_COL);
		k[1].pad = 0; k[1].gap = 2; k[1].item = BODYT; k[1].n = 6;
		k[1].overflow = WSK_SCROLL; k[1].select = 1; k[1].head = 0;
		for (int j = 2; j < 8; j++) {
			wsk_nest_init(&k[j], &w, WSK_ROW);
			k[j].pad = 0; k[j].gap = 0; k[j].talign = WSK_ALIGN_START;
			k[j].item = CX; k[j].n = 2;
		}
		k[2].item = C0; k[2].n = 3;
		k[3].item = C1; k[3].n = 3;
		ok(wsk_check(&w, 0) == WSK_OK, "a header over a scrolling, selecting body of six rows");

		/* By hand: the header's buttons are their text (8 a cell, no
		   padding) -- 32, the fixed 80, 32 -- leaving 256 shared 2:1: Name
		   gets 170 (202), Date the rest, 86 (118). So the columns are
		   0..202, 202..282, 282..400. */
		ok(rect_is(rect_of(&w, "h_size"), 202, 0, 80, 32), "the header's fixed column: 202, 80 wide");
		ok(rect_is(rect_of(&w, "c00"), 0, 32, 202, 28) && rect_is(rect_of(&w, "c01"), 202, 32, 80, 28) &&
		   rect_is(rect_of(&w, "c02"), 282, 32, 118, 28),
		   "row 0's cells sit under the header's: 0/202, 202/80, 282/118");
		ok(rect_of(&w, "c11").x == 202 && rect_of(&w, "c12").w == 118,
		   "and row 1's, whose texts are different lengths, sit in the same columns");
		k[1].head = WSK_NONE;
		ok(rect_of(&w, "c01").x == 72 && rect_of(&w, "c11").x == 8,
		   "CONTROL: without the header, each row sizes its own cells, and they do not line up");
		k[1].head = 0;

		/* Scrolled, the cells keep their columns. */
		k[1].scroll = 30;
		ok(wsk_check(&w, 0) == WSK_OK && rect_of(&w, "c11").x == 202 && rect_of(&w, "c11").y == 32,
		   "scrolled 30, row 1 is at the top of the body, its cells still under the header");
		k[1].scroll = 0;

		/* Text at the start of a cell. */
		render_into(&w, td, sizeof td, 8192);
		ok(has(td, "<text text-anchor=\"start\" x=\"202\"") && has(td, ">12K</text>"),
		   "cells read as a table: text from the start of the cell, not centred");

		/* A row with more cells than columns. */
		k[4].item = C4; k[4].n = 4;
		ok(wsk_check_in(&w, &which, &in) == WSK_E_FIT && which == 3 && in == &k[4],
		   "a row with four cells under three columns is refused, at its fourth");
		k[4].item = CX; k[4].n = 2;
		ok(wsk_check(&w, 0) == WSK_OK, "CONTROL: with two, accepted -- fewer cells than columns is fine");

		/* A tap on a cell chooses its row. */
		wsk_ui_init(&ui, &w);
		memset(&ev, 0, sizeof ev);
		ev.kind = WSK_EV_DOWN; ev.x = 300; ev.y = 70;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ev.kind = WSK_EV_UP;
		n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(n == 2 && nt[0].kind == WSK_NOTE_CLICK && nt[0].in == &k[3] &&
		   nt[1].kind == WSK_NOTE_SELECT && nt[1].in == &k[1] && nt[1].value == 1 && k[1].sel == 1,
		   "a tap on row 1's date cell: a CLICK on the cell, and SELECT row 1 of the body");
		render_into(&w, td, sizeof td, 8192);
		ok(has(td, "<g id=\"r1\"><rect y=\"62\" width=\"400\" height=\"28\" fill=\"#b06a2a\"/>"),
		   "and the chosen row is drawn on the selection fill, behind its cells");
	}

	/* ---- §5o. a memo: many lines, edited ---- */
	printf("\n-- §5o  memo\n");
	{
		static const wsk_widget_t FM[] = {
			{ .kind = WSK_MEMO,  .id = "note", .sub = 0, .size = 120 },
			{ .kind = WSK_FIELD, .id = "one",  .sub = 1, .size = 32 },
		};
		char mb[64] = "", fb[16] = "";
		wsk_buf_t buf[2];
		wsk_tree_t w;
		wsk_ui_t ui;
		wsk_note_t nt[4];
		wsk_event_t ev;
		char md[4096];
		int n, y0 = 0, y1 = 0, which = -2;

		memset(buf, 0, sizeof buf);
		buf[0].b = mb; buf[0].cap = 64;
		buf[1].b = fb; buf[1].cap = 16;
		wsk_init(&w, WSK_COL, 320, 300);
		w.pad = 4; w.gap = 4; w.item = FM; w.n = 2; w.buf = buf; w.bufs = 2;
		ok(wsk_check(&w, 0) == WSK_OK, "a memo and a field");
		wsk_ui_init(&ui, &w);
		memset(&ev, 0, sizeof ev);
#define TYPE(T) (ev.kind = WSK_EV_TEXT, snprintf(ev.text, sizeof ev.text, "%s", (T)), \
                 n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1))
#define KEY(K)  (ev.kind = WSK_EV_KEY, ev.key = (K), n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1))
#define SET(S, C) (snprintf(mb, sizeof mb, "%s", (S)), buf[0].len = (short)strlen(mb), \
                   buf[0].caret = (short)(C), buf[0].top = 0)

		wsk_ui_focus(&ui, "note", &y0, &y1);
		TYPE("ab"); KEY(WSK_KEY_ENTER); TYPE("cd");
		ok(strcmp(mb, "ab\ncd") == 0 && buf[0].caret == 5, "typing, Enter, typing: \"ab\\ncd\", the caret at 5");
		KEY(WSK_KEY_ENTER);
		ok(n == 1 && nt[0].kind == WSK_NOTE_TEXT, "in a memo Enter is a line break, a TEXT notice -- not ENTER");
		KEY(WSK_KEY_BACKSPACE);
		KEY(WSK_KEY_UP);
		ok(buf[0].caret == 2, "UP: from line 1 column 2 to line 0 column 2");
		KEY(WSK_KEY_DOWN);
		ok(buf[0].caret == 5, "DOWN: back");

		SET("abcdef\nxy", 5);
		KEY(WSK_KEY_DOWN);
		ok(buf[0].caret == 9, "DOWN from column 5 onto a line of two: its end, not past it");
		KEY(WSK_KEY_UP);
		ok(buf[0].caret == 2, "and UP keeps the column it has, 2");
		SET("abcdef\nxyz", 8);
		KEY(WSK_KEY_HOME);
		ok(buf[0].caret == 7, "HOME: the start of the caret's line, not of the memo");
		KEY(WSK_KEY_END);
		ok(buf[0].caret == 10, "END: the end of the line");
		SET("abcdef\nxyz", 7);
		KEY(WSK_KEY_BACKSPACE);
		ok(strcmp(mb, "abcdefxyz") == 0 && buf[0].caret == 6, "BACKSPACE at a line's start joins it to the one above");

		/* The view: 120 - 12 is 108, five lines of 20. */
		SET("1\n2\n3\n4\n5\n6\n7\n8", 15);
		render_into(&w, md, sizeof md, 4096);
		ok(has(md, ">4</text>") && has(md, ">8</text>") && !has(md, ">3</text>"),
		   "eight lines and the caret on the last: lines 4..8 shown, the caret in sight");
		ok(buf[0].top == 0, "and drawing did not write the buffer's top -- it drew the nearest that shows the caret");
		for (int i = 0; i < 5; i++) KEY(WSK_KEY_UP);
		ok(buf[0].top == 2 && buf[0].caret == 5, "UP five lines, column 1 kept: the caret on line 2, and top follows it to 2");
		KEY(WSK_KEY_DOWN);
		ok(buf[0].top == 2, "DOWN one: the view stays -- it moves only as far as it must");

		/* A tap: line 1, column 2 -- 4 + 6 + 2 x 8 across, 4 + 6 + 20 down. */
		SET("abcdef\nxyz\nlast", 0);
		ev.kind = WSK_EV_DOWN; ev.x = 4 + 6 + 2 * 8 + 1; ev.y = 4 + 6 + 20 + 5;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(buf[0].caret == 7 + 2, "a tap on line 1, column 2, puts the caret there");
		ev.kind = WSK_EV_UP;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);

		/* What the window cannot draw. */
		SET("tab\there", 0);
		ok(wsk_check(&w, &which) == WSK_E_TEXT && which == 0, "a memo holding a tab is refused");
		SET("ok\nfine", 0);
		ok(wsk_check(&w, 0) == WSK_OK, "CONTROL: lines are fine in a memo");
		snprintf(fb, sizeof fb, "a\nb"); buf[1].len = 3;
		ok(wsk_check(&w, &which) == WSK_E_TEXT && which == 1, "and a line break in a one-line field is not");
		fb[0] = '\0'; buf[1].len = 0;
#undef TYPE
#undef KEY
#undef SET
	}

	/* ---- §5p. a tree: depths over a flat list ---- */
	printf("\n-- §5p  trees\n");
	{
		static const wsk_widget_t LIST[] = {
			{ .kind = WSK_LABEL, .id = "d0", .text = "a",   .depth = 0, .size = 24 },
			{ .kind = WSK_LABEL, .id = "d1", .text = "b",   .depth = 1, .size = 24 },
			{ .kind = WSK_LABEL, .id = "d2", .text = "c",   .depth = 2, .size = 24 },
			{ .kind = WSK_LABEL, .id = "d3", .text = "d",   .depth = 1, .size = 24 },
			{ .kind = WSK_LABEL, .id = "d4", .text = "e",   .depth = 0, .size = 24 },
		};
		wsk_tree_t t;
		wsk_init(&t, WSK_COL, 320, 172);
		t.pad = 0; t.gap = 0; t.item = LIST; t.n = 5;
		ok(rect_is(wsk_rect(&t, 2), 0, 48, 320, 24), "CONTROL: with no indent, depth changes nothing");
		t.indent = 16;
		ok(rect_is(wsk_rect(&t, 0), 0, 0, 320, 24) && rect_is(wsk_rect(&t, 1), 16, 24, 304, 24) &&
		   rect_is(wsk_rect(&t, 2), 32, 48, 288, 24) && rect_is(wsk_rect(&t, 3), 16, 72, 304, 24) &&
		   rect_is(wsk_rect(&t, 4), 0, 96, 320, 24),
		   "depths 0, 1, 2, 1, 0 under an indent of 16: in by 0, 16, 32, 16, 0, and narrower by as much");
	}
	{
		/* In a table, the first cell only. */
		static const wsk_widget_t TOPT[] = {
			{ .kind = WSK_STACK, .id = "hdr",  .sub = 0, .size = 24 },
			{ .kind = WSK_STACK, .id = "body", .sub = 1, .weight = 1 },
		};
		static const wsk_widget_t HDR[] = {
			{ .kind = WSK_BUTTON, .id = "hn", .text = "Name", .weight = 1 },
			{ .kind = WSK_BUTTON, .id = "hs", .text = "Size", .size = 80 },
		};
		static const wsk_widget_t BODYT[] = {
			{ .kind = WSK_STACK, .id = "r0", .sub = 2, .size = 24, .depth = 0 },
			{ .kind = WSK_STACK, .id = "r1", .sub = 3, .size = 24, .depth = 2 },
		};
		static const wsk_widget_t R0[] = { { .kind = WSK_LABEL, .id = "a0", .text = "src" },
		                                   { .kind = WSK_LABEL, .id = "a1", .text = "-" } };
		static const wsk_widget_t R1[] = { { .kind = WSK_LABEL, .id = "b0", .text = "gtk.md" },
		                                   { .kind = WSK_LABEL, .id = "b1", .text = "2K" } };
		wsk_tree_t w, k[4];
		wsk_init(&w, WSK_COL, 320, 172);
		w.pad = 0; w.gap = 0; w.item = TOPT; w.n = 2; w.nest = k; w.nests = 4;
		wsk_nest_init(&k[0], &w, WSK_ROW); k[0].pad = 0; k[0].gap = 0; k[0].item = HDR; k[0].n = 2;
		wsk_nest_init(&k[1], &w, WSK_COL); k[1].pad = 0; k[1].gap = 0; k[1].item = BODYT; k[1].n = 2;
		k[1].head = 0; k[1].indent = 20;
		wsk_nest_init(&k[2], &w, WSK_ROW); k[2].pad = 0; k[2].gap = 0; k[2].item = R0; k[2].n = 2;
		wsk_nest_init(&k[3], &w, WSK_ROW); k[3].pad = 0; k[3].gap = 0; k[3].item = R1; k[3].n = 2;
		ok(wsk_check(&w, 0) == WSK_OK, "a table whose rows have depths");
		/* columns: Name 0..240, Size 240..320 */
		ok(rect_is(rect_of(&w, "a0"), 0, 24, 240, 24) && rect_is(rect_of(&w, "b0"), 40, 48, 200, 24),
		   "a row at depth 2 under an indent of 20 has its first cell in by 40");
		ok(rect_is(rect_of(&w, "b1"), 240, 48, 80, 24) && rect_of(&w, "a1").x == 240 &&
		   rect_of(&w, "r1").x == 0,
		   "and its other cells stay under the header -- the row itself is not indented");
	}
	{
		/* Expansion is the program's (§5p rule 3): a tap selects a row, the
		   program toggles that node and rebuilds the rows it shows. */
		static wsk_widget_t rows[TN];
		int node[TN], n, y0, y1;
		wsk_tree_t t;
		wsk_ui_t ui;
		wsk_note_t nt[4];
		wsk_event_t ev;
		char td[2048];

		for (int i = 0; i < TN; i++) TNODES[i].open = 0;
		wsk_init(&t, WSK_COL, 320, 300);
		t.pad = 0; t.gap = 0; t.indent = 16; t.select = 1; t.talign = WSK_ALIGN_START;
		t.item = rows; t.n = trows(rows, node);
		ok(t.n == 3, "closed, the tree shows its three top rows: src, docs, README.md");
		wsk_ui_init(&ui, &t);
		memset(&ev, 0, sizeof ev);
#define TAPROW(R) do { ev.kind = WSK_EV_DOWN; ev.x = 100; ev.y = (R) * 24 + 10; \
                       wsk_feed(&ui, &ev, nt, 4, &y0, &y1); ev.kind = WSK_EV_UP; \
                       n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1); \
                       for (int j = 0; j < n && j < 4; j++) if (nt[j].kind == WSK_NOTE_SELECT) { \
                           tnode_t *tn = &TNODES[node[nt[j].value]]; \
                           if (tn->kids) tn->open = !tn->open; \
                           t.n = trows(rows, node); t.sel = WSK_NONE; } } while (0)
		TAPROW(1);                          /* docs */
		ok(t.n == 5 && strcmp(rows[2].text, "  design.md") == 0 && strcmp(rows[3].text, "+ old") == 0 &&
		   rows[3].depth == 1, "a tap on docs opens it: design.md and old, at depth 1 -- old still closed");
		TAPROW(3);                          /* old */
		ok(t.n == 6 && strcmp(rows[4].text, "  gtk.md") == 0 && rows[4].depth == 2,
		   "a tap on old opens it: gtk.md at depth 2");
		ok(rect_is(wsk_rect(&t, 4), 32, 96, 288, 24), "drawn two indents in");
		TAPROW(1);                          /* docs again */
		ok(t.n == 3 && strcmp(rows[1].text, "+ docs") == 0,
		   "and docs closed again hides old and gtk.md with it -- old stays open, in the program's model");
		render_into(&t, td, sizeof td, 4096);
		ok(has(td, ">+ docs</text>") && !has(td, "gtk.md"), "what is drawn is what the program handed over");
#undef TAPROW
	}

	/* ---- §5r. a host that has more says so ---- */
	printf("\n-- §5r  capabilities\n");
	{
		static const wsk_widget_t U[] = {
			{ .kind = WSK_LABEL, .id = "u0", .text = "\xe2\x96\xbe src" },        /* ▾ src */
			{ .kind = WSK_LABEL, .id = "u1", .text = "caf\xc3\xa9" },             /* café */
		};
		static const wsk_widget_t G[] = {
			{ .kind = WSK_LABEL, .id = "g0", .text = "\xce\xb1\xce\xb2\xce\xb3\xce\xb4\xce\xb5", .size = 24 },
		};
		wsk_tree_t t;
		char cd[2048];
		int which = -2;

		wsk_init(&t, WSK_ROW, 320, 172);
		t.pad = 0; t.gap = 0; t.item = U; t.n = 2;
		ok(wsk_check(&t, &which) == WSK_E_TEXT && which == 0,
		   "without WSK_CAP_UTF8, \"\\u25be src\" is refused, as it always was -- never a ? for it");
		t.caps = WSK_CAP_UTF8;
		ok(wsk_check(&t, 0) == WSK_OK, "with it, accepted");
		ok(wsk_rect(&t, 0).w == 40 && wsk_rect(&t, 1).w == 32,
		   "measured in characters: \"\\u25be src\" is 5 wide (40), not 7 bytes; \"caf\\u00e9\" 4 (32)");
		render_into(&t, cd, sizeof cd, 4096);
		ok(has(cd, ">\xe2\x96\xbe src</text>") && has(cd, ">caf\xc3\xa9</text>"), "and drawn as written");

		/* truncation never splits a character: five Greek letters, 10 bytes,
		   in a column 3 cells wide */
		wsk_init(&t, WSK_COL, 24, 172);
		t.pad = 0; t.caps = WSK_CAP_UTF8; t.item = G; t.n = 1;
		render_into(&t, cd, sizeof cd, 4096);
		ok(has(cd, ">\xce\xb1\xce\xb2\xce\xb3</text>"),
		   "truncated to the three characters that fit -- six bytes, never five");

		/* malformed UTF-8 is refused even by a host that draws UTF-8 */
		{
			wsk_widget_t bad[1];
			wsk_clone(bad, G, 1);
			bad[0].text[0] = (char)0x80; bad[0].text[1] = 'x'; bad[0].text[2] = '\0';
			t.item = bad;
			ok(wsk_check(&t, 0) == WSK_E_TEXT, "a byte that starts no character is refused");
			bad[0].text[0] = (char)0xC0; bad[0].text[1] = (char)0x80; bad[0].text[2] = '\0';
			ok(wsk_check(&t, 0) == WSK_E_TEXT, "and a character spelled the long way round");
			bad[0].text[0] = (char)0xE2; bad[0].text[1] = (char)0x96; bad[0].text[2] = '\0';
			ok(wsk_check(&t, 0) == WSK_E_TEXT, "and one that ends too soon");
		}
	}
	{
		/* Images. */
		static const wsk_widget_t I[] = {
			{ .kind = WSK_LABEL, .id = "cap", .text = "logo", .size = 24 },
			{ .kind = WSK_IMAGE, .id = "pic", .sub = 0, .size = 64 },
		};
		const char *strs[1];
		wsk_tree_t t;
		char id[2048];
		int which = -2;

		strs[0] = "/static/logo.svg";
		wsk_init(&t, WSK_COL, 320, 172);
		t.pad = 0; t.gap = 0; t.item = I; t.n = 2; t.str = strs; t.strs = 1;
		ok(wsk_check(&t, &which) == WSK_E_HOST && which == 1,
		   "an image for a host that has not said it can draw one: refused, WSK_E_HOST, naming it");
		t.caps = WSK_CAP_IMAGE;
		ok(wsk_check(&t, 0) == WSK_OK, "with WSK_CAP_IMAGE, accepted");
		render_into(&t, id, sizeof id, 4096);
		ok(has(id, "<image href=\"/static/logo.svg\" x=\"0\" y=\"24\" width=\"320\" height=\"64\"/>"),
		   "and drawn: an <image> in its widget's rectangle");
		strs[0] = "a\"onload=\"x";
		ok(wsk_check(&t, &which) == WSK_E_TEXT && which == 1,
		   "an href that would end its attribute is refused");
		strs[0] = "/static/logo.svg";
	}
	{
		/* Clipping: a scrolled pane, its first row half out of view. */
		static wsk_widget_t rows[6];
		wsk_tree_t p;
		char cd[4096];
		for (int i = 0; i < 6; i++) {
			memset(&rows[i], 0, sizeof rows[i]);
			rows[i].kind = WSK_BUTTON; rows[i].size = 30;
			snprintf(rows[i].id, WSK_ID_MAX, "c%d", i);
			snprintf(rows[i].text, WSK_TEXT_MAX, "Row %d", i);
		}
		wsk_init(&p, WSK_COL, 320, 172);
		p.pad = 0; p.gap = 0; p.ids = 1; p.item = rows; p.n = 6;
		wsk_area(&p, 0, 40, 320, 100);
		p.overflow = WSK_SCROLL; p.scroll = 15;
		render_into(&p, cd, sizeof cd, 4096);
		ok(!has(cd, "clipPath") && has(cd, "<rect id=\"c0\" y=\"40\" width=\"320\" height=\"15\"") &&
		   !has(cd, ">Row 0</text>"),
		   "CONTROL: without WSK_CAP_CLIP, the half row is cut by arithmetic and its text not drawn (D12)");
		p.caps = WSK_CAP_CLIP;
		render_into(&p, cd, sizeof cd, 4096);
		ok(has(cd, "<clipPath id=\"wsk-c0-40\">") && has(cd, "clip-path=\"url(#wsk-c0-40)\""),
		   "with it, the pane's widgets are inside a <g> clipped to the pane");
		ok(has(cd, "<rect id=\"c0\" y=\"25\" width=\"320\" height=\"30\"") && has(cd, ">Row 0</text>"),
		   "and the half row is drawn whole -- its box and its label -- and the clip cuts it: it slides");
		ok(wsk_hit(&p, 10, 30) == WSK_E_NOTFOUND && wsk_hit(&p, 10, 45) == 0,
		   "hit still cuts by arithmetic: above the pane is nothing, inside it row 0");
	}

	/* ---- §5s. a desktop's plumbing: right-click, selection, clipboard, shortcuts ---- */
	printf("\n-- §5s  right-click, selection, the clipboard, shortcuts\n");
	{
		static const wsk_widget_t SW[] = {
			{ .kind = WSK_FIELD,  .id = "f",    .sub = 0, .size = 32 },    /*   4..36  */
			{ .kind = WSK_MEMO,   .id = "m",    .sub = 1, .size = 80 },    /*  40..120 */
			{ .kind = WSK_BUTTON, .id = "b",    .text = "Go", .size = 32 },/* 124..156 */
			{ .kind = WSK_STACK,  .id = "list", .sub = 0, .size = 100 },   /* 160..260 */
		};
		static const wsk_widget_t SL[] = {
			{ .kind = WSK_FIELD, .id = "lf", .sub = 2, .size = 40 },
			{ .kind = WSK_LABEL, .id = "la", .text = "a", .size = 40 },
			{ .kind = WSK_LABEL, .id = "lb", .text = "b", .size = 40 },
			{ .kind = WSK_LABEL, .id = "lc", .text = "c", .size = 40 },
		};
		static const wsk_widget_t SM[] = {
			{ .kind = WSK_BUTTON, .id = "cut",  .text = "Cut",  .size = 32 },
			{ .kind = WSK_BUTTON, .id = "copy", .text = "Copy", .size = 32 },
		};
		char fb[16] = "", mb[64] = "", lb[16] = "", clip[8] = "";
		wsk_buf_t buf[3];
		wsk_tree_t w, k[2];
		wsk_ui_t ui;
		wsk_note_t nt[4];
		wsk_event_t ev;
		char sd[4096];
		int n, y0 = 0, y1 = 0;
		const char *SEL = "fill=\"#b06a2a\"";

		memset(buf, 0, sizeof buf);
		buf[0].b = fb; buf[0].cap = 16;
		buf[1].b = mb; buf[1].cap = 64;
		buf[2].b = lb; buf[2].cap = 16;
		buf[0].span = 7;                        /* stale: a program's leftover */
		wsk_init(&w, WSK_COL, 320, 300);
		w.pad = 4; w.gap = 4; w.item = SW; w.n = 4; w.nest = k; w.nests = 2;
		w.buf = buf; w.bufs = 3;
		wsk_nest_init(&k[0], &w, WSK_COL);
		k[0].pad = 0; k[0].gap = 4; k[0].item = SL; k[0].n = 4; k[0].overflow = WSK_SCROLL;
		wsk_nest_init(&k[1], &w, WSK_COL);
		k[1].pad = 4; k[1].gap = 4; k[1].item = SM; k[1].n = 2;
		ok(wsk_check(&w, 0) == WSK_OK, "a field, a memo, a button, and a scrolling list with a field in it");
		wsk_ui_init(&ui, &w);
		ok(buf[0].span == 0, "wsk_ui_init clears every buffer's selection, whatever it held");
		memset(&ev, 0, sizeof ev);

#define TYPE(T) (ev.kind = WSK_EV_TEXT, snprintf(ev.text, sizeof ev.text, "%s", (T)), \
                 n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1))
#define KEY(K)  (ev.kind = WSK_EV_KEY, ev.key = (unsigned short)(K), n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1))
#define PT(E, X, Y, K) (ev.kind = (E), ev.x = (X), ev.y = (Y), ev.key = (unsigned short)(K), \
                        n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1))
#define SET(S, C) (snprintf(fb, sizeof fb, "%s", (S)), buf[0].len = (short)strlen(fb), \
                   buf[0].caret = (short)(C), buf[0].span = 0)

		/* The field: x 4, text from 10, 8 a character; its text's top at 12. */
		wsk_ui_focus(&ui, "f", &y0, &y1);
		TYPE("hello");
		KEY(WSK_KEY_LEFT | WSK_MOD_SHIFT); KEY(WSK_KEY_LEFT | WSK_MOD_SHIFT);
		ok(buf[0].caret == 3 && buf[0].span == 2 && n == 0 && y0 == 0 && y1 == 40,
		   "Shift-Left twice: the caret at 3, the selection two on (\"lo\"), the field's rows, no notice");
		render_into(&w, sd, sizeof sd, 4096);
		ok(count(sd, SEL) == 1 && has(sd, "<rect x=\"34\" y=\"12\" width=\"16\" height=\"16\" fill=\"#b06a2a\"/>"),
		   "drawn: one box of selfill behind characters 3 and 4 -- x 34, 16 wide, the text's height");
		ok(strstr(sd, "fill=\"#b06a2a\"") < strstr(sd, ">hello</text>"), "and behind the text, not over it");
		KEY(WSK_KEY_RIGHT);
		ok(buf[0].caret == 5 && buf[0].span == 0, "Right, unshifted: the selection collapses to its end");
		render_into(&w, sd, sizeof sd, 4096);
		ok(count(sd, SEL) == 0, "CONTROL: and no box is drawn without one");
		KEY(WSK_KEY_HOME | WSK_MOD_SHIFT);
		ok(buf[0].caret == 0 && buf[0].span == 5, "Shift-Home: everything back to the start");
		KEY(WSK_KEY_LEFT | WSK_MOD_SHIFT);
		ok(buf[0].caret == 0 && buf[0].span == 5, "Shift-Left at the start: nothing moves, and the selection stays");
		TYPE("J");
		ok(n == 1 && nt[0].kind == WSK_NOTE_TEXT && strcmp(fb, "J") == 0 && buf[0].caret == 1 &&
		   buf[0].span == 0, "typing replaces the selection: \"J\", one TEXT notice");
		SET("hello world", 11);
		KEY('a' | WSK_MOD_CTRL);
		ok(n == 0 && buf[0].caret == 11 && buf[0].span == -11, "Ctrl-A selects all, and is the field's: no notice");
		KEY(WSK_KEY_BACKSPACE);
		ok(n == 1 && nt[0].kind == WSK_NOTE_TEXT && fb[0] == '\0' && buf[0].len == 0 && buf[0].caret == 0,
		   "Backspace deletes the selection, all of it");
		SET("hello world", 0);
		KEY(WSK_KEY_END | WSK_MOD_SHIFT); KEY(WSK_KEY_LEFT | WSK_MOD_SHIFT);
		KEY(WSK_KEY_DELETE);
		ok(strcmp(fb, "d") == 0 && buf[0].caret == 0, "Delete deletes it too: \"hello worl\" gone, \"d\" left");

		/* The clipboard. */
		SET("hello world", 11);
		for (int i = 0; i < 5; i++) KEY(WSK_KEY_LEFT | WSK_MOD_SHIFT);
		KEY('c' | WSK_MOD_CTRL);
		ok(n == 1 && nt[0].kind == WSK_NOTE_KEY && nt[0].value == ('c' | WSK_MOD_CTRL),
		   "with no clip buffer, Ctrl-C is not the field's: a KEY notice, for the program");
		ui.clip = clip; ui.clipcap = sizeof clip;
		KEY('c' | WSK_MOD_CTRL);
		ok(n == 1 && nt[0].kind == WSK_NOTE_COPY && nt[0].value == 5 && strcmp(clip, "world") == 0 &&
		   strcmp(fb, "hello world") == 0 && buf[0].caret == 6 && buf[0].span == 5,
		   "with one, Ctrl-C: \"world\" copied into it, a COPY notice of 5, the text and selection as they were");
		KEY('a' | WSK_MOD_CTRL);
		KEY('c' | WSK_MOD_CTRL);
		ok(n == 0 && strcmp(clip, "world") == 0,
		   "eleven characters into a buffer of 8: nothing copied, not even a part, and no notice");
		SET("hello world", 11);
		for (int i = 0; i < 5; i++) KEY(WSK_KEY_LEFT | WSK_MOD_SHIFT);
		KEY('x' | WSK_MOD_CTRL);
		ok(n == 2 && nt[0].kind == WSK_NOTE_COPY && nt[1].kind == WSK_NOTE_TEXT && nt[1].value == 6 &&
		   strcmp(fb, "hello ") == 0 && strcmp(clip, "world") == 0,
		   "Ctrl-X: COPY, then TEXT -- \"world\" copied before it was deleted");
		KEY('v' | WSK_MOD_CTRL);
		ok(n == 1 && nt[0].kind == WSK_NOTE_PASTE && nt[0].item == 0, "Ctrl-V: a PASTE notice, naming the field");
		ok(wsk_ui_paste(&ui, "a<b", 3, nt, 4, &y0, &y1) == WSK_E_TEXT && strcmp(fb, "hello ") == 0,
		   "pasting a character the window cannot draw is refused, the field as it was");
		ok(wsk_ui_paste(&ui, "x\ny", 3, nt, 4, &y0, &y1) == WSK_E_TEXT,
		   "and a line break, in a one-line field");
		ok(wsk_ui_paste(&ui, "0123456789", 10, nt, 4, &y0, &y1) == WSK_E_BUDGET && strcmp(fb, "hello ") == 0,
		   "and ten more than a buffer of 16 holds after six: refused whole, not trimmed");
		ok(wsk_ui_paste(&ui, "012345678", 9, nt, 4, &y0, &y1) == 1 && nt[0].kind == WSK_NOTE_TEXT &&
		   strcmp(fb, "hello 012345678") == 0 && buf[0].caret == 15 && y0 == 0 && y1 == 40,
		   "nine fit: pasted at the caret, a TEXT notice, the field's rows");
		KEY(WSK_KEY_HOME); KEY(WSK_KEY_END | WSK_MOD_SHIFT);
		ok(wsk_ui_paste(&ui, "bye", 3, nt, 4, &y0, &y1) == 1 && strcmp(fb, "bye") == 0,
		   "and in place of a selection: all fifteen selected, \"bye\" left");

		/* The pointer: a press at a character, a drag to another. */
		SET("hello world", 0);
		PT(WSK_EV_DOWN, 10 + 2 * 8 + 1, 20, 0);
		PT(WSK_EV_MOVE, 10 + 7 * 8 + 1, 20, 0);
		ok(buf[0].caret == 7 && buf[0].span == -5 && y1 > y0, "press at 2, drag to 7: \"llo w\" selected");
		PT(WSK_EV_UP, 10 + 7 * 8 + 1, 20, 0);
		PT(WSK_EV_DOWN, 10 + 9 * 8 + 1, 20, WSK_MOD_SHIFT);
		ok(buf[0].caret == 9 && buf[0].span == -7, "Shift-press at 9 extends from the anchor at 2");
		PT(WSK_EV_UP, 10 + 9 * 8 + 1, 20, 0);
		PT(WSK_EV_DOWN, 10 + 4 * 8 + 1, 20, 0);
		ok(buf[0].caret == 4 && buf[0].span == 0, "a press without Shift lets it go: the caret at 4");
		PT(WSK_EV_MOVE, 400, 20, 0);
		ok(buf[0].caret == 11 && buf[0].span == -7, "a drag off the end of the field selects to its end");
		PT(WSK_EV_UP, 400, 20, 0);

		/* A memo's selection is a box on each line it covers: lines 20 apart
		   from 46, characters from x 10. */
		{
			wsk_rect_t r = rect_of(&w, "m");
			PT(WSK_EV_DOWN, r.x + 8, r.y + 8, 0);
			PT(WSK_EV_UP, r.x + 8, r.y + 8, 0);
		}
		snprintf(mb, sizeof mb, "abc\ndef\nghi"); buf[1].len = 11; buf[1].caret = 1; buf[1].span = 0;
		KEY(WSK_KEY_DOWN | WSK_MOD_SHIFT);
		ok(buf[1].caret == 5 && buf[1].span == -4, "in a memo, Shift-Down: from line 0 column 1 to line 1 column 1");
		render_into(&w, sd, sizeof sd, 4096);
		ok(count(sd, SEL) == 2 && has(sd, "<rect x=\"18\" y=\"46\" width=\"16\" height=\"16\" fill=\"#b06a2a\"/>") &&
		   has(sd, "<rect x=\"10\" y=\"66\" width=\"8\" height=\"16\" fill=\"#b06a2a\"/>"),
		   "drawn as two boxes: \"bc\" on line 0, \"d\" on line 1");
		TYPE("-");
		ok(strcmp(mb, "a-ef\nghi") == 0, "and typing replaces it, the line break with it");
		buf[1].caret = 0; buf[1].span = 4;
		buf[0].span = 0;
		wsk_ui_focus(&ui, "f", &y0, &y1);
		render_into(&w, sd, sizeof sd, 4096);
		ok(count(sd, SEL) == 0, "a selection is drawn only in the focused field: the memo's is not");

		/* A field in a scrolling list: lf at 160..200. A drag in its rows
		   selects; out of them by the slop, it scrolls the list. */
		snprintf(lb, sizeof lb, "abcdefgh"); buf[2].len = 8; buf[2].caret = 0;
		PT(WSK_EV_DOWN, 20, 195, 0);
		PT(WSK_EV_MOVE, 60, 162, 0);
		ok(buf[2].caret == 6 && buf[2].span == -5 && k[0].scroll == 0,
		   "33 rows up but still in the field: it selects, and the list does not move");
		PT(WSK_EV_MOVE, 60, 130, 0);
		ok(n == 1 && nt[0].kind == WSK_NOTE_SCROLL && k[0].scroll == 65,
		   "out of the field's rows by more than the slop: the drag is the list's again, scrolled 65");
		PT(WSK_EV_UP, 60, 130, 0);
		k[0].scroll = 0;

		/* Right-click. */
		PT(WSK_EV_MENU, 100, 140, 0);
		ok(n == 1 && nt[0].kind == WSK_NOTE_MENU && nt[0].in == &w && nt[0].item == 2 && nt[0].value == -1,
		   "MENU on the button names it");
		PT(WSK_EV_MENU, 100, 38, 0);
		ok(n == 1 && nt[0].kind == WSK_NOTE_MENU && nt[0].in == &w && nt[0].item == -1,
		   "MENU in a gap names nothing: -1, the root");
		wsk_ui_focus(&ui, "m", &y0, &y1);
		SET("hello", 5);
		PT(WSK_EV_MENU, 10 + 2 * 8 + 1, 20, 0);
		ok(n == 2 && nt[0].kind == WSK_NOTE_FOCUS && nt[1].kind == WSK_NOTE_MENU && nt[1].item == 0 &&
		   w.focus == &SW[0] && buf[0].caret == 2, "MENU on a field without focus: FOCUS, the caret there, then MENU");
		KEY(WSK_KEY_END | WSK_MOD_SHIFT);
		PT(WSK_EV_MENU, 10 + 1, 20, 0);
		ok(n == 1 && nt[0].kind == WSK_NOTE_MENU && buf[0].caret == 5 && buf[0].span == -3,
		   "MENU on the focused field leaves its caret and selection, for a Copy");
		PT(WSK_EV_DOWN, 100, 140, 0);
		ok(w.held == &SW[2], "a press holds the button");
		PT(WSK_EV_MENU, 100, 140, 0);
		ok(w.held == 0 && n == 1 && nt[0].kind == WSK_NOTE_MENU && y1 > y0,
		   "a MENU during it -- a long press -- lets it go, and repaints it");
		PT(WSK_EV_UP, 100, 140, 0);
		ok(n == 0, "and the release is not a click");
		KEY(WSK_KEY_MENU);
		ok(n == 1 && nt[0].kind == WSK_NOTE_MENU && nt[0].in == &w && nt[0].item == 0,
		   "the Menu key: MENU on the focused widget");
		ok(wsk_ui_open_at(&ui, 1, 100, 50, &y0, &y1) == WSK_OK && w.over == 1 && w.ox == 100 &&
		   w.oy == 50 && y0 == 48 && y1 <= 136, "wsk_ui_open_at hangs a menu from the point");
		PT(WSK_EV_MENU, 10, 280, 0);
		ok(n == 2 && nt[0].kind == WSK_NOTE_CLOSE && nt[1].kind == WSK_NOTE_MENU && w.over == WSK_NONE,
		   "MENU off the open menu: CLOSE it, then MENU where the pointer is");

		/* Shortcuts. */
		KEY('s' | WSK_MOD_CTRL);
		ok(n == 1 && nt[0].kind == WSK_NOTE_KEY && nt[0].value == ('s' | WSK_MOD_CTRL) &&
		   nt[0].in == &w && nt[0].item == -1, "Ctrl-S, a field focused: not the field's, so the program's -- a KEY notice");
		KEY('q' | WSK_MOD_ALT);
		ok(n == 1 && nt[0].kind == WSK_NOTE_KEY && nt[0].value == ('q' | WSK_MOD_ALT), "and Alt-Q");
		wsk_ui_init(&ui, &w);
		KEY('s' | WSK_MOD_CTRL);
		ok(n == 1 && nt[0].kind == WSK_NOTE_KEY, "with nothing focused, the same");
		KEY(WSK_KEY_LEFT);
		ok(n == 0, "CONTROL: a plain key nobody used is still nothing");
		KEY(WSK_KEY_LEFT | WSK_MOD_SHIFT);
		ok(n == 0, "and Shift with it is not a chord");
#undef TYPE
#undef KEY
#undef PT
#undef SET
	}

	/* ---- §5t. a tree's arrows, drawn rather than spelled ---- */
	printf("\n-- §5t  arrows\n");
	{
		static const wsk_widget_t TR[] = {
			{ .kind = WSK_LABEL,  .id = "a0", .text = "src",  .size = 32, .mark = WSK_MARK_SHUT },
			{ .kind = WSK_LABEL,  .id = "a1", .text = "docs", .size = 32, .mark = WSK_MARK_OPEN },
			{ .kind = WSK_LABEL,  .id = "a2", .text = "a.c",  .size = 32, .mark = WSK_MARK_LEAF, .depth = 1 },
			{ .kind = WSK_BUTTON, .id = "a3", .text = "Go",   .size = 32, .mark = WSK_MARK_SHUT },
		};
		static const wsk_widget_t BAD[] = {
			{ .kind = WSK_FIELD, .id = "f", .size = 32, .mark = WSK_MARK_OPEN },
		};
		wsk_widget_t row[2];
		wsk_tree_t t;
		char ad[4096];
		int which = -2;

		/* labels at the start, font 16: the mark's cell is 16 wide from
		   x0 = 4 + pad 4 = 8; the text's top is 4 + (32 - 16) / 2 = 12, and the
		   triangle 8 high in the cell, 4 in from each side: x 12, y 16 */
		wsk_init(&t, WSK_COL, 320, 172);
		t.pad = 4; t.gap = 4; t.item = TR; t.n = 3; t.talign = WSK_ALIGN_START; t.indent = 16;
		ok(wsk_check(&t, 0) == WSK_OK, "a closed folder, an open one, a leaf under it");
		render_into(&t, ad, sizeof ad, 4096);
		ok(has(ad, "<path d=\"M13 16L13 24L20 20Z\"/>"),
		   "the closed one: a triangle pointing right, 8 high, in the mark's cell -- in the ink, by inheritance");
		ok(has(ad, "<text text-anchor=\"start\" x=\"24\" y=\"") && has(ad, ">src</text>"),
		   "and its text after the cell, at 8 + 16 = 24");
		ok(has(ad, "<path d=\"M12 53L20 53L16 60Z\"/>"), "the open one: pointing down, on the next row (40..72)");
		ok(count(ad, "<path") == 2, "the leaf draws no triangle");
		ok(has(ad, "x=\"40\" y=\"") && has(ad, ">a.c</text>"),
		   "but keeps the room: its text at 8 + 16 indent + 16 = 40, under its folder's name");
		t.item = &TR[3]; t.n = 1; t.talign = WSK_MIDDLE;
		render_into(&t, ad, sizeof ad, 4096);
		/* centred: "Go" is 16 wide, the mark 16 more: 32, centred on 160 */
		ok(has(ad, "<path d=\"M149 16L149 24L156 20Z\"/>") && has(ad, "x=\"160\" y=\"") && has(ad, ">Go</text>"),
		   "on a centred button: the mark and its text centred together, 144..176");

		/* the room: a row, a label 80 wide -- (80 - 8) / 8 = 9 characters, 7 with a mark */
		memset(row, 0, sizeof row);
		row[0].kind = WSK_LABEL; snprintf(row[0].id, WSK_ID_MAX, "r0");
		snprintf(row[0].text, WSK_TEXT_MAX, "abcdefghij"); row[0].size = 80;
		wsk_init(&t, WSK_ROW, 320, 172);
		t.pad = 4; t.gap = 4; t.item = row; t.n = 1; t.talign = WSK_ALIGN_START;
		render_into(&t, ad, sizeof ad, 4096);
		ok(has(ad, ">abcdefghi</text>"), "CONTROL: unmarked, nine characters fit");
		row[0].mark = WSK_MARK_SHUT;
		render_into(&t, ad, sizeof ad, 4096);
		ok(has(ad, ">abcdefg</text>"), "marked, seven: the mark is two characters' room");
		row[0].size = 0; snprintf(row[0].text, WSK_TEXT_MAX, "abc");
		row[1] = row[0]; row[1].mark = 0; snprintf(row[1].id, WSK_ID_MAX, "r1");
		t.n = 2;
		ok(wsk_rect(&t, 0).w == wsk_rect(&t, 1).w + 16,
		   "a label sized by its text is the mark's 16 wider than the same one unmarked");
		render_into(&t, ad, sizeof ad, 4096);
		ok(count(ad, ">abc</text>") == 2, "and so loses no characters to it");

		/* refused where there is no text to put it before */
		wsk_init(&t, WSK_COL, 320, 172);
		t.item = BAD; t.n = 1;
		ok(wsk_check(&t, &which) == WSK_E_KIND && which == 0, "a tree's mark on a field is refused, by name");
	}

	/* ---- §5u. a memo that wraps ---- */
	printf("\n-- §5u  wrap in a memo\n");
	{
		static const wsk_widget_t FM[] = {
			{ .kind = WSK_MEMO, .id = "m", .sub = 0, .size = 120 },
		};
		char mb[64] = "";
		wsk_buf_t buf[1];
		wsk_tree_t w;
		wsk_ui_t ui;
		wsk_note_t nt[4];
		wsk_event_t ev;
		char md[4096];
		int n, y0 = 0, y1 = 0;
		const char *SEL = "fill=\"#b06a2a\"";

		/* 120 wide: the memo is 112, (112 - 12 - 2) / 8 = 12 characters a row;
		   (120 - 12) / 20 = 5 rows show, from y 10, 20 apart */
		memset(buf, 0, sizeof buf);
		buf[0].b = mb; buf[0].cap = 64;
		wsk_init(&w, WSK_COL, 120, 200);
		w.pad = 4; w.gap = 4; w.item = FM; w.n = 1; w.buf = buf; w.bufs = 1;
		wsk_ui_init(&ui, &w);
		wsk_ui_focus(&ui, "m", &y0, &y1);
		memset(&ev, 0, sizeof ev);
#define TYPE(T) (ev.kind = WSK_EV_TEXT, snprintf(ev.text, sizeof ev.text, "%s", (T)), \
                 n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1))
#define KEY(K)  (ev.kind = WSK_EV_KEY, ev.key = (unsigned short)(K), n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1))
#define SET(S, C) (snprintf(mb, sizeof mb, "%s", (S)), buf[0].len = (short)strlen(mb), \
                   buf[0].caret = (short)(C), buf[0].top = 0, buf[0].span = 0)

		SET("the quick brown fox jumps", 25);
		render_into(&w, md, sizeof md, 4096);
		ok(!has(md, ">brown fox</text>"), "CONTROL: unwrapped, a long line is one line, as in §5o");
		buf[0].wrap = 1;
		render_into(&w, md, sizeof md, 4096);
		ok(has(md, ">the quick</text>") && has(md, ">brown fox</text>") && has(md, ">jumps</text>"),
		   "wrapped: three rows, each broken at its last space that fits -- and the space not drawn");
		SET("abcdefghijklmnopq", 0);
		render_into(&w, md, sizeof md, 4096);
		ok(has(md, ">abcdefghijkl</text>") && has(md, ">mnopq</text>"),
		   "a word longer than the row breaks where the row ends");
		SET("abcdefghijkl", 0);
		render_into(&w, md, sizeof md, 4096);
		ok(has(md, ">abcdefghijkl</text>"), "and a line that fits exactly is not broken");
		SET("ab\ncd", 0);
		render_into(&w, md, sizeof md, 4096);
		ok(has(md, ">ab</text>") && has(md, ">cd</text>"), "a line break is still a line");

		/* the caret moves on rows */
		SET("the quick brown fox jumps", 25);
		KEY(WSK_KEY_UP);
		ok(buf[0].caret == 15, "UP from the end of row 2, column 5: row 1 column 5, offset 15");
		KEY(WSK_KEY_UP);
		ok(buf[0].caret == 5, "UP again: row 0 column 5");
		KEY(WSK_KEY_END);
		ok(buf[0].caret == 9, "END: the row's end, before the space it broke at -- not the line's");
		KEY(WSK_KEY_HOME);
		ok(buf[0].caret == 0, "HOME: the row's start");
		buf[0].caret = 9;
		KEY(WSK_KEY_DOWN);
		ok(buf[0].caret == 19, "DOWN from column 9: \"brown fox\" ends at 19");
		buf[0].caret = 10;
		render_into(&w, md, sizeof md, 4096);
		ok(has(md, "<rect x=\"10\" y=\"32\" width=\"2\""),
		   "a caret at a soft break is at the next row's start: x 10, row 1");
		ev.kind = WSK_EV_DOWN; ev.x = 10 + 2 * 8 + 1; ev.y = 4 + 6 + 40 + 5;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ev.kind = WSK_EV_UP;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(buf[0].caret == 22, "a tap on row 2, column 2: \"jumps\" from 20, so 22");

		/* a selection across a soft break: a box on each row */
		buf[0].caret = 5; buf[0].span = 0;
		KEY(WSK_KEY_DOWN | WSK_MOD_SHIFT);
		render_into(&w, md, sizeof md, 4096);
		ok(buf[0].caret == 15 && count(md, SEL) == 2 &&
		   has(md, "<rect x=\"50\" y=\"10\" width=\"32\" height=\"16\" fill=\"#b06a2a\"/>") &&
		   has(md, "<rect x=\"10\" y=\"30\" width=\"40\" height=\"16\" fill=\"#b06a2a\"/>"),
		   "Shift-Down from 5 to 15: \"uick\" on row 0, \"brown\" on row 1");

		/* the view counts rows: twelve words of four, two a row, six rows */
		SET("", 0);
		for (int i = 0; i < 12; i++) TYPE("abcd ");
		render_into(&w, md, sizeof md, 4096);
		ok(strlen(mb) == 60 && buf[0].top == 1 && count(md, ">abcd abcd</text>") == 4 &&
		   count(md, ">abcd abcd </text>") == 1,
		   "typed past the fifth row: top follows to row 1, and rows 1..5 show");
		(void)n;
#undef TYPE
#undef KEY
#undef SET
	}

	/* ---- §5v. undo ---- */
	printf("\n-- §5v  undo and redo\n");
	{
		static const wsk_widget_t FU[] = {
			{ .kind = WSK_FIELD, .id = "f", .sub = 0, .size = 32 },
			{ .kind = WSK_MEMO,  .id = "m", .sub = 1, .size = 80 },
		};
		char fb[32] = "", mb[32] = "", ub[256], small[48], clip[16];
		wsk_buf_t buf[2];
		wsk_undo_t u, us;
		wsk_tree_t w;
		wsk_ui_t ui;
		wsk_note_t nt[4];
		wsk_event_t ev;
		int n, y0 = 0, y1 = 0;

		memset(buf, 0, sizeof buf);
		memset(&u, 0, sizeof u);
		u.b = ub; u.cap = sizeof ub;
		buf[0].b = fb; buf[0].cap = 32; buf[0].undo = &u;
		buf[1].b = mb; buf[1].cap = 32;
		wsk_init(&w, WSK_COL, 320, 200);
		w.pad = 4; w.gap = 4; w.item = FU; w.n = 2; w.buf = buf; w.bufs = 2;
		wsk_ui_init(&ui, &w);
		ui.clip = clip; ui.clipcap = sizeof clip;
		wsk_ui_focus(&ui, "f", &y0, &y1);
		memset(&ev, 0, sizeof ev);
#define TYPE(T) (ev.kind = WSK_EV_TEXT, snprintf(ev.text, sizeof ev.text, "%s", (T)), \
                 n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1))
#define KEY(K)  (ev.kind = WSK_EV_KEY, ev.key = (unsigned short)(K), n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1))
#define UNDO    KEY('z' | WSK_MOD_CTRL)
#define REDO    KEY('y' | WSK_MOD_CTRL)

		for (const char *p = "hello world"; *p; p++) { char c[2] = { *p, 0 }; TYPE(c); }
		ok(strcmp(fb, "hello world") == 0, "eleven characters typed one at a time");
		UNDO;
		ok(n == 1 && nt[0].kind == WSK_NOTE_TEXT && strcmp(fb, "hello") == 0 && buf[0].caret == 5,
		   "Ctrl-Z: \" world\" goes in one step -- a word and the space before it -- and a TEXT notice");
		UNDO;
		ok(fb[0] == '\0' && buf[0].caret == 0, "Ctrl-Z again: \"hello\", the word before");
		UNDO;
		ok(n == 0 && fb[0] == '\0', "and again: nothing left to undo, nothing changes, no notice");
		REDO;
		ok(strcmp(fb, "hello") == 0 && buf[0].caret == 5, "Ctrl-Y redoes \"hello\", the caret after it");
		KEY('z' | WSK_MOD_CTRL | WSK_MOD_SHIFT);
		ok(strcmp(fb, "hello world") == 0 && buf[0].caret == 11, "and Ctrl-Shift-Z \" world\"");

		KEY(WSK_KEY_BACKSPACE); KEY(WSK_KEY_BACKSPACE);
		ok(strcmp(fb, "hello wor") == 0, "two Backspaces");
		UNDO;
		ok(strcmp(fb, "hello worl") == 0 && buf[0].caret == 10, "each is a step: one undo gives back the l");
		UNDO;
		KEY(WSK_KEY_HOME); KEY(WSK_KEY_DELETE);
		UNDO;
		ok(strcmp(fb, "hello world") == 0 && buf[0].caret == 1, "Delete, undone: the h back, the caret after it");

		/* a selection replaced, cut, pasted: each one step */
		KEY(WSK_KEY_HOME); for (int i = 0; i < 5; i++) KEY(WSK_KEY_RIGHT | WSK_MOD_SHIFT);
		TYPE("J");
		ok(strcmp(fb, "J world") == 0, "\"hello\" selected and typed over");
		UNDO;
		ok(strcmp(fb, "hello world") == 0 && buf[0].caret == 5, "one undo puts \"hello\" back -- not the J and the deletion apart");
		KEY(WSK_KEY_HOME); for (int i = 0; i < 6; i++) KEY(WSK_KEY_RIGHT | WSK_MOD_SHIFT);
		KEY('x' | WSK_MOD_CTRL);
		ok(strcmp(fb, "world") == 0, "a cut");
		UNDO;
		ok(strcmp(fb, "hello world") == 0, "undone");
		KEY(WSK_KEY_END);
		ok(wsk_ui_paste(&ui, "!!", 2, nt, 4, &y0, &y1) == 1 && strcmp(fb, "hello world!!") == 0, "a paste");
		UNDO;
		ok(strcmp(fb, "hello world") == 0, "undone in one step");

		/* a new edit ends redo */
		UNDO;
		TYPE("x");
		REDO;
		ok(strstr(fb, "x") != 0 && strstr(fb, "world") == 0 && n == 0,
		   "an edit after an undo ends redo: Ctrl-Y changes nothing");

		/* a small array drops its oldest steps */
		memset(&us, 0, sizeof us);
		us.b = small; us.cap = sizeof small;
		snprintf(fb, sizeof fb, "%s", ""); buf[0].len = 0; buf[0].caret = 0; buf[0].span = 0;
		buf[0].undo = &us;
		TYPE("aaaaa");                      /* 21 bytes: a step of five */
		TYPE("bb");                         /* 18: 39 */
		TYPE("cc");                         /* 18: 57 > 48, so the oldest goes */
		ok(us.used == 36, "three steps into 48 bytes: the first dropped, two kept (36)");
		UNDO; UNDO; UNDO;
		ok(strcmp(fb, "aaaaa") == 0, "so two undos are all there are: \"aaaaa\" stays");

		/* without an array, the chords are the program's */
		buf[0].undo = 0;
		UNDO;
		ok(n == 1 && nt[0].kind == WSK_NOTE_KEY && nt[0].value == ('z' | WSK_MOD_CTRL),
		   "with no undo array, Ctrl-Z is a KEY notice, for the program");

		/* a memo's line break is a step too */
		wsk_ui_focus(&ui, "m", &y0, &y1);
		memset(&u, 0, sizeof u); u.b = ub; u.cap = sizeof ub;
		buf[1].undo = &u;
		TYPE("ab"); KEY(WSK_KEY_ENTER); TYPE("cd");
		UNDO; UNDO;
		ok(strcmp(mb, "ab") == 0 && buf[1].caret == 2, "in a memo: \"cd\" undone, then the line break");
#undef TYPE
#undef KEY
#undef UNDO
#undef REDO
	}

	/* ---- §5v. source: a gutter, colour runs ---- */
	printf("\n-- §5v  source\n");
	{
		static const wsk_widget_t FS[] = {
			{ .kind = WSK_SOURCE, .id = "src", .sub = 0, .size = 120 },
		};
		static char big[320000];
		char sb[128] = "";
		wsk_ink_t ik[4];
		wsk_buf_t buf[1];
		wsk_tree_t w;
		wsk_ui_t ui;
		wsk_note_t nt[4];
		wsk_event_t ev;
		char sd[4096];
		int n, y0 = 0, y1 = 0, which = -2;

		/* 320 wide, pad 4: the widget 4..316, 120 high from 4. Under 100
		   lines the gutter is 3 characters, 24 px; the text's column 0 at
		   4 + 24 + 6 = 34; a number's last digit ends at 4 + 24 - 4 = 24 */
		memset(buf, 0, sizeof buf);
		buf[0].b = sb; buf[0].cap = sizeof sb;
		wsk_init(&w, WSK_COL, 320, 200);
		w.pad = 4; w.gap = 4; w.item = FS; w.n = 1; w.buf = buf; w.bufs = 1;
		wsk_ui_init(&ui, &w);
		memset(&ev, 0, sizeof ev);
#define SET(S, C) (snprintf(sb, sizeof sb, "%s", (S)), buf[0].len = (int)strlen(sb), \
                   buf[0].caret = (C), buf[0].top = 0, buf[0].span = 0)

		SET("print \"hi\" ' note\ny = 2", 0);
		ok(wsk_check(&w, 0) == WSK_OK, "a source widget of two lines");
		render_into(&w, sd, sizeof sd, 4096);
		ok(has(sd, "<rect x=\"4\" y=\"4\" width=\"24\" height=\"120\""),
		   "its gutter: three characters wide, the widget's height, in the window's background");
		ok(has(sd, "x=\"16\" y=\"") && has(sd, ">1</text>") && has(sd, ">2</text>"),
		   "the lines numbered, the last digit at 24");
		ok(has(sd, ">print \"hi\" ' note</text>") && has(sd, "x=\"34\""),
		   "CONTROL: with no colour runs, a line is one <text> in the widget's ink, at x 34");
		ik[0].at = 0;  ik[0].len = 5; ik[0].ink = 0x569cd6u;      /* print   */
		ik[1].at = 6;  ik[1].len = 4; ik[1].ink = 0xce9178u;      /* "hi"    */
		ik[2].at = 11; ik[2].len = 6; ik[2].ink = 0x6a9955u;      /* ' note  */
		buf[0].inks = ik; buf[0].ninks = 3;
		render_into(&w, sd, sizeof sd, 4096);
		ok(has(sd, "x=\"34\" y=\"") && has(sd, "fill=\"#569cd6\">print</text>") &&
		   has(sd, "x=\"82\" y=\"") && has(sd, "fill=\"#ce9178\">\"hi\"</text>") &&
		   has(sd, "x=\"122\" y=\"") && has(sd, "fill=\"#6a9955\">' note</text>"),
		   "with runs: each a <text> at its column -- print at 34, \"hi\" at 34 + 6 x 8, the comment at 34 + 11 x 8");
		ok(has(sd, ">y = 2</text>"), "and a line no run touches is drawn as before");
		SET("a = b", 0);
		ik[0].at = 0; ik[0].len = 1;                               /* a */
		buf[0].ninks = 1;
		render_into(&w, sd, sizeof sd, 4096);
		ok(has(sd, "fill=\"#569cd6\">a</text>") && has(sd, "x=\"50\" y=\"") && has(sd, ">= b</text>"),
		   "the gap after a run is the widget's ink, its opening space not sent: \"= b\" at column 2, x 50");
		ik[0].at = 5; ik[0].len = 2; ik[1].at = 0; ik[1].len = 3;
		buf[0].ninks = 2;
		ok(wsk_check(&w, &which) == WSK_E_TEXT && which == 0, "runs out of order are refused");
		ik[0].at = 0; ik[0].len = 5; ik[1].at = 3; ik[1].len = 4;
		ok(wsk_check(&w, 0) == WSK_E_TEXT, "and runs over each other");
		ik[0].at = 0; ik[0].len = 1; ik[1].at = 40; ik[1].len = 4;
		ok(wsk_check(&w, 0) == WSK_OK && render_into(&w, sd, sizeof sd, 4096) > 0,
		   "a run past the end is not refused: the text may just have shrunk");
		buf[0].inks = 0; buf[0].ninks = 0;

		/* never wraps, whatever the buffer says: a long line slides */
		SET("abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ", 62);
		buf[0].wrap = 1;
		render_into(&w, sd, sizeof sd, 4096);
		ok(!has(sd, ">abc") && has(sd, "XYZ</text>") && count(sd, "</text>") == 2,
		   "source never wraps: 62 characters, the view slid to the caret -- one row and its number");
		buf[0].wrap = 0;

		/* a press in the gutter: the line's start, and on release its line */
		SET("one\ntwo\nthree", 0);
		ev.kind = WSK_EV_DOWN; ev.x = 10; ev.y = 4 + 6 + 20 + 5;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(buf[0].caret == 4, "a press in the gutter by line 1: the caret at its start, 4");
		ev.kind = WSK_EV_UP;
		n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(n == 2 && nt[0].kind == WSK_NOTE_CLICK && nt[1].kind == WSK_NOTE_GUTTER && nt[1].value == 1,
		   "and its release: CLICK, then GUTTER with the line, 1 -- Studio's breakpoint");
		ev.kind = WSK_EV_DOWN; ev.x = 34 + 2 * 8 + 1;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ev.kind = WSK_EV_UP;
		n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(n == 1 && nt[0].kind == WSK_NOTE_CLICK && buf[0].caret == 6,
		   "CONTROL: a press in the text is a click, no GUTTER, the caret at column 2");

		/* the size of Studio's own file */
		{
			int L = 0, last = 0;
			for (int i = 0; L < (int)sizeof big - 40; i++)
				L += snprintf(big + L, sizeof big - (size_t)L, "line %05d of a long file\n", i + 1);
			big[--L] = '\0';                /* no line break at the end */
			memset(buf, 0, sizeof buf);
			buf[0].b = big; buf[0].cap = sizeof big; buf[0].len = L; buf[0].caret = L;
			wsk_ui_init(&ui, &w);
			for (int i = 0; i < L; i++) if (big[i] == '\n') last++;
			snprintf(sb, sizeof sb, ">%d</text>", last + 1);
			ok(L > 300000 && wsk_check(&w, 0) == WSK_OK && render_into(&w, sd, sizeof sd, 4096) > 0 &&
			   has(sd, sb) && has(sd, "<rect x=\"4\" y=\"4\" width=\"48\" height=\"120\""),
			   "a 300 KB buffer, its caret at the end: the last line's number drawn, the gutter 6 wide for 5 digits");
			wsk_ui_focus(&ui, "src", &y0, &y1);
			ev.kind = WSK_EV_TEXT; snprintf(ev.text, sizeof ev.text, "!");
			n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
			ok(n == 1 && buf[0].len == L + 1 && big[L] == '!', "and typed into at its end, past offset 32,767 by ten times");
			ev.kind = WSK_EV_KEY; ev.key = WSK_KEY_HOME;
			wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
			ok(buf[0].caret == L + 1 - 26, "HOME goes to that line's start: 25 characters and the !");
		}
#undef SET
	}

	/* ---- §5v. < > & in text, for a host that decodes entities ---- */
	printf("\n-- §5v  entities\n");
	{
		static const wsk_widget_t FE[] = {
			{ .kind = WSK_LABEL,  .id = "l", .text = "if a < b & c > d", .size = 32 },
			{ .kind = WSK_SOURCE, .id = "s", .sub = 0, .size = 80 },
		};
		char sb[64] = "while i < 10\n    i = i & 3", ed[4096];
		wsk_buf_t buf[1];
		wsk_tree_t w;
		wsk_ui_t ui;
		wsk_note_t nt[4];
		wsk_event_t ev;
		int which = -2, y0, y1;

		memset(buf, 0, sizeof buf);
		buf[0].b = sb; buf[0].cap = sizeof sb; buf[0].len = (int)strlen(sb);
		wsk_init(&w, WSK_COL, 320, 200);
		w.pad = 4; w.gap = 4; w.ids = 1; w.item = FE; w.n = 2; w.buf = buf; w.bufs = 1;
		ok(wsk_check(&w, &which) == WSK_E_TEXT && which == 0,
		   "without WSK_CAP_ENTITY, a label holding < & > is refused, as on /w0 it must be");
		w.n = 1; w.item = &FE[1];
		ok(wsk_check(&w, &which) == WSK_E_TEXT, "and so is code holding them, in a source widget");
		w.caps = WSK_CAP_ENTITY; w.item = FE; w.n = 2;
		ok(wsk_check(&w, 0) == WSK_OK, "with it, both are accepted");
		render_into(&w, ed, sizeof ed, 4096);
		ok(has(ed, ">if a &lt; b &amp; c &gt; d</text>") && has(ed, ">while i &lt; 10</text>") &&
		   has(ed, ">i = i &amp; 3</text>"), "and written escaped: &lt; &amp; &gt;");
		{
			wsk_tree_t row;
			wsk_widget_t one = FE[0];
			one.size = 0;
			wsk_init(&row, WSK_ROW, 320, 200);
			row.pad = 4; row.gap = 4; row.item = &one; row.n = 1; row.caps = WSK_CAP_ENTITY;
			ok(wsk_rect(&row, 0).w == 16 * 8 + 2 * 4,
			   "measured in characters: sized by its text, \"if a < b & c > d\" is 16 wide, not the 26 written");
		}
		{
			FILE *f = fopen("/tmp/wsk_entity.svg", "wb");
			fputs(ed, f); fclose(f);
		}
		wsk_ui_init(&ui, &w);
		wsk_ui_focus(&ui, "s", &y0, &y1);
		memset(&ev, 0, sizeof ev);
		ev.kind = WSK_EV_TEXT; snprintf(ev.text, sizeof ev.text, "<");
		ok(wsk_feed(&ui, &ev, nt, 4, &y0, &y1) == 1 && sb[buf[0].caret - 1] == '<',
		   "and typed: < goes into the text as itself");
		w.caps = 0;
		snprintf(ev.text, sizeof ev.text, ">");
		ok(wsk_feed(&ui, &ev, nt, 4, &y0, &y1) == 0, "CONTROL: for a host without the bit, typing > is refused");
	}

	/* ---- §5v step 3. UTF-8 in fields, memos and source ---- */
	printf("\n-- §5v  UTF-8 editing\n");
	{
		/* é is 2 bytes, — 3, 😀 4: positions are bytes, columns characters */
#define E_ "\xc3\xa9"
#define D_ "\xe2\x80\x94"
#define S_ "\xf0\x9f\x98\x80"
		static const wsk_widget_t FU8[] = {
			{ .kind = WSK_FIELD,  .id = "f", .sub = 0, .size = 32 },
			{ .kind = WSK_MEMO,   .id = "m", .sub = 1, .size = 60 },
			{ .kind = WSK_SOURCE, .id = "s", .sub = 2, .size = 60 },
		};
		char fb[64] = "", mb[128] = "", sb[64] = "", ub[512], clip[32], ud[4096], dashes[80];
		wsk_buf_t buf[3];
		wsk_undo_t u;
		wsk_ink_t ik[2];
		wsk_tree_t w;
		wsk_ui_t ui;
		wsk_note_t nt[4];
		wsk_event_t ev;
		int n, y0 = 0, y1 = 0, which = -2;

		memset(buf, 0, sizeof buf);
		memset(&u, 0, sizeof u);
		u.b = ub; u.cap = sizeof ub;
		buf[0].b = fb; buf[0].cap = sizeof fb; buf[0].undo = &u;
		buf[1].b = mb; buf[1].cap = sizeof mb;
		buf[2].b = sb; buf[2].cap = sizeof sb;
		wsk_init(&w, WSK_COL, 320, 200);
		w.pad = 4; w.gap = 4; w.item = FU8; w.n = 3; w.buf = buf; w.bufs = 3;
#define SETB(B, S, C) (snprintf((B).b, (size_t)(B).cap, "%s", (S)), (B).len = (int)strlen((B).b), \
                       (B).caret = (C), (B).top = 0, (B).span = 0)
#define TYPE(T) (ev.kind = WSK_EV_TEXT, snprintf(ev.text, sizeof ev.text, "%s", (T)), \
                 n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1))
#define KEY(K)  (ev.kind = WSK_EV_KEY, ev.key = (unsigned short)(K), n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1))

		SETB(buf[0], "caf" E_ " " D_ " ok", 0);
		ok(wsk_check(&w, &which) == WSK_E_TEXT && which == 0,
		   "CONTROL: without WSK_CAP_UTF8, a field holding \"caf\xc3\xa9 \xe2\x80\x94 ok\" is refused, as before");
		w.caps = WSK_CAP_UTF8;
		ok(wsk_check(&w, 0) == WSK_OK, "with it, accepted");
		buf[0].caret = 4;
		ok(wsk_check(&w, &which) == WSK_E_TEXT && which == 0,
		   "but not with the caret inside a character: byte 4 is the middle of \xc3\xa9");
		buf[0].caret = buf[0].len;
		fb[3] = '\xe9';                    /* Latin-1's é, not UTF-8's */
		ok(wsk_check(&w, 0) == WSK_E_TEXT, "nor a byte that starts no character");
		SETB(buf[0], "caf" E_ " " D_ " ok", 12);

		/* drawn in columns: the caret after 10 characters, not 12 bytes */
		render_into(&w, ud, sizeof ud, 4096);
		wsk_ui_init(&ui, &w);
		wsk_ui_focus(&ui, "f", &y0, &y1);
		render_into(&w, ud, sizeof ud, 4096);
		ok(has(ud, ">caf\xc3\xa9 \xe2\x80\x94 ok</text>") && has(ud, "<rect x=\"82\" y=\"14\" width=\"2\""),
		   "a field: the text whole, its caret at 10 + 9 x 8 = 82 -- nine characters, though twelve bytes");

		/* the caret steps a character at a time */
		KEY(WSK_KEY_LEFT); KEY(WSK_KEY_LEFT); KEY(WSK_KEY_LEFT); KEY(WSK_KEY_LEFT);
		ok(buf[0].caret == 6, "Left four times from the end: o, k, a space, and the 3-byte dash -- byte 6");
		KEY(WSK_KEY_LEFT); KEY(WSK_KEY_LEFT);
		ok(buf[0].caret == 3, "twice more: the space and the 2-byte \xc3\xa9, byte 3");
		KEY(WSK_KEY_RIGHT);
		ok(buf[0].caret == 5, "Right: over \xc3\xa9 whole, byte 5");
		KEY(WSK_KEY_BACKSPACE);
		ok(n == 1 && strcmp(fb, "caf " D_ " ok") == 0 && buf[0].caret == 3,
		   "Backspace takes the whole \xc3\xa9, both bytes");
		KEY(WSK_KEY_RIGHT);
		KEY(WSK_KEY_DELETE);
		ok(strcmp(fb, "caf  ok") == 0, "Delete the whole dash, all three");
		KEY('z' | WSK_MOD_CTRL); KEY('z' | WSK_MOD_CTRL);
		ok(strcmp(fb, "caf" E_ " " D_ " ok") == 0, "and both undone, whole");

		/* typing beyond ASCII, and undo's step */
		SETB(buf[0], "", 0);
		memset(&u, 0, sizeof u); u.b = ub; u.cap = sizeof ub;
		TYPE("n"); TYPE("a"); TYPE(E_); TYPE("v"); TYPE("e");
		ok(strcmp(fb, "na" E_ "ve") == 0 && buf[0].caret == 6, "typed: n, a, \xc3\xa9, v, e -- the caret after six bytes");
		TYPE(" "); TYPE(S_);
		ok(strcmp(fb, "na" E_ "ve " S_) == 0, "and a space and a 4-byte character");
		KEY('z' | WSK_MOD_CTRL);
		ok(strcmp(fb, "na" E_ "ve") == 0, "one undo: \" \xf0\x9f\x98\x80\", the space and the word, as with ASCII");
		KEY('z' | WSK_MOD_CTRL);
		ok(fb[0] == '\0', "another: \"na\xc3\xa9ve\" was one step -- \xc3\xa9 joined the word it was typed in");
		TYPE("\xe2\x80");
		ok(n == 0 && fb[0] == '\0', "a character cut off before its end is refused");
		w.caps = 0;
		TYPE(E_);
		ok(n == 0 && fb[0] == '\0', "CONTROL: and for a host without UTF-8, so is \xc3\xa9");
		w.caps = WSK_CAP_UTF8;

		/* paste, and a selection measured in columns */
		ok(wsk_ui_paste(&ui, "x" D_ "y", 5, nt, 4, &y0, &y1) == 1 && strcmp(fb, "x" D_ "y") == 0,
		   "a paste beyond ASCII");
		ok(wsk_ui_paste(&ui, "\xe2\x80", 2, nt, 4, &y0, &y1) == WSK_E_TEXT && strcmp(fb, "x" D_ "y") == 0,
		   "and a paste ending inside a character refused, the buffer as it was");
		KEY(WSK_KEY_LEFT);
		KEY(WSK_KEY_LEFT | WSK_MOD_SHIFT);
		render_into(&w, ud, sizeof ud, 4096);
		ok(buf[0].caret == 1 && buf[0].span == 3 && has(ud, "<rect x=\"18\" y=\"12\" width=\"8\" height=\"16\""),
		   "Shift-Left selects the dash: three bytes, one column -- a box 8 wide at column 1");
		ui.clip = clip; ui.clipcap = sizeof clip;
		KEY('c' | WSK_MOD_CTRL);
		ok(n == 1 && nt[0].kind == WSK_NOTE_COPY && nt[0].value == 3 && strcmp(clip, D_) == 0,
		   "copied whole: three bytes");

		/* a click: the column under the pointer, as a byte */
		ev.kind = WSK_EV_DOWN; ev.x = 10 + 2 * 8 + 1; ev.y = 20; ev.key = 0;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ev.kind = WSK_EV_UP;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(buf[0].caret == 4, "a click at column 2, after the dash: byte 4");

		/* a memo: Up and Down keep the column in characters */
		SETB(buf[1], E_ D_ "ab\nwxyz", 6);
		wsk_ui_focus(&ui, "m", &y0, &y1);
		KEY(WSK_KEY_DOWN);
		ok(buf[1].caret == 11, "a memo: from column 3 (byte 6, after \xc3\xa9\xe2\x80\x94" "a), Down keeps column 3 -- byte 11, after wxy");
		KEY(WSK_KEY_UP);
		ok(buf[1].caret == 6, "and Up comes back to byte 6, not to 3 bytes in");
		KEY(WSK_KEY_END); KEY(WSK_KEY_HOME);
		ok(buf[1].caret == 0, "End, then Home");
		buf[1].wrap = 1;
		{
			/* the memo is 312 wide: (312 - 12 - 2) / 8 = 37 characters a row */
			char *p = mb;
			for (int i = 0; i < 20; i++) { memcpy(p, D_, 3); p += 3; }
			memcpy(p, " abcdefghijklmnopqrstu", 23);
			buf[1].len = (int)strlen(mb); buf[1].caret = 0; buf[1].top = 0; buf[1].span = 0;
			render_into(&w, ud, sizeof ud, 4096);
			for (int i = 0; i < 20; i++) memcpy(dashes + 1 + 3 * i, D_, 3);
			dashes[0] = '>';
			memcpy(dashes + 61, "</text>", 8);
			ok(has(ud, ">abcdefghijklmnopqrstu</text>") && has(ud, dashes),
			   "wrapping counts characters: 20 dashes (60 bytes), a space, 21 letters -- 42 characters break at the space");
			buf[1].wrap = 0;
			SETB(buf[1], "", 0);
		}

		/* source: runs at their columns */
		SETB(buf[2], "s = \"" E_ D_ "\" + x", 0);
		ik[0].at = 4; ik[0].len = 7; ik[0].ink = 0xce9178u;       /* "é—" */
		buf[2].inks = ik; buf[2].ninks = 1;
		ok(wsk_check(&w, 0) == WSK_OK, "a source line with a string holding \xc3\xa9\xe2\x80\x94, its run over the string");
		render_into(&w, ud, sizeof ud, 4096);
		ok(has(ud, "fill=\"#ce9178\">\"\xc3\xa9\xe2\x80\x94\"</text>") && has(ud, "x=\"106\" y=\"") &&
		   has(ud, ">+ x</text>"),
		   "and the gap after it at column 9 -- x 34 + 9 x 8 = 106, not byte 12's 130");
		ik[0].len = 4;                                             /* into the dash */
		ok(wsk_check(&w, &which) == WSK_E_TEXT && which == 2,
		   "a run ending inside a character is refused: its <text> would split one");
		buf[2].inks = 0; buf[2].ninks = 0;
#undef SETB
#undef TYPE
#undef KEY
#undef E_
#undef D_
#undef S_
	}

	/* ---- §5v step 4. marks in a source's gutter ---- */
	printf("\n-- §5v  gutter marks\n");
	{
		static const wsk_widget_t FM[] = {
			{ .kind = WSK_SOURCE, .id = "src", .sub = 0, .size = 120 },
		};
		char sb[128] = "", sd[4096];
		wsk_gmark_t gm[4];
		wsk_buf_t buf[1];
		wsk_tree_t w;
		wsk_ui_t ui;
		wsk_note_t nt[4];
		wsk_event_t ev;
		int n, y0 = 0, y1 = 0, which = -2;

		/* as §5v's source: 320 wide, rows 20 apart from y 10, the gutter 24
		   for three characters -- and 16 more, a font, for the marks */
		memset(buf, 0, sizeof buf);
		buf[0].b = sb; buf[0].cap = sizeof sb;
		wsk_init(&w, WSK_COL, 320, 200);
		w.pad = 4; w.gap = 4; w.item = FM; w.n = 1; w.buf = buf; w.bufs = 1;
#define SET(S, C) (snprintf(sb, sizeof sb, "%s", (S)), buf[0].len = (int)strlen(sb), \
                   buf[0].caret = (C), buf[0].top = 0, buf[0].span = 0)
		SET("a\nb\nc\nd\ne", 0);
		render_into(&w, sd, sizeof sd, 4096);
		ok(has(sd, "<rect x=\"4\" y=\"4\" width=\"24\" height=\"120\"") && has(sd, "x=\"34\" y=\"22\">a</text>"),
		   "CONTROL: with no marks pointer, the gutter is 24 and the text at 34, as before");
		buf[0].marks = gm; buf[0].nmarks = 0;
		render_into(&w, sd, sizeof sd, 4096);
		ok(has(sd, "<rect x=\"4\" y=\"4\" width=\"40\" height=\"120\"") && has(sd, "x=\"50\" y=\"22\">a</text>") &&
		   has(sd, "x=\"32\" y=\"22\">1</text>"),
		   "given marks, even none: the gutter 40, a font wider -- the text at 50, the numbers still end at 40");

		gm[0].line = 0; gm[0].shape = WSK_GM_DOT;   gm[0].ink = 0xe51400u;
		gm[1].line = 1; gm[1].shape = WSK_GM_BAR;   gm[1].ink = 0x404000u;
		gm[2].line = 3; gm[2].shape = WSK_GM_ARROW; gm[2].ink = 0x60a917u;
		buf[0].nmarks = 3;
		ok(wsk_check(&w, 0) == WSK_OK, "three marks: a dot on line 0, a bar on 1, an arrow on 3");
		render_into(&w, sd, sizeof sd, 4096);
		ok(has(sd, "<rect x=\"8\" y=\"14\" width=\"8\" height=\"8\" fill=\"#e51400\""),
		   "the dot: a square half the font, centred in the mark column, at line 0");
		ok(has(sd, "<rect x=\"4\" y=\"28\" width=\"40\" height=\"20\" fill=\"#404000\"") &&
		   strstr(sd, "fill=\"#404000\"") < strstr(sd, ">2</text>"),
		   "the bar: line 1's whole gutter cell, a pitch high so bars touch -- drawn before its number, which is over it");
		ok(has(sd, "<path d=\"M9 74L9 82L16 78Z\" fill=\"#60a917\"/>"),
		   "the arrow: §5t's closed-folder triangle, at line 3");
		ok(count(sd, "width=\"8\" height=\"8\"") == 1 && count(sd, "<path") == 1,
		   "CONTROL: and nothing on lines 2 and 4, which have no mark");

		gm[1].line = 0;
		ok(wsk_check(&w, &which) == WSK_E_TEXT && which == 0, "two marks on one line are refused: which wins is the program's");
		gm[1].line = 4; gm[2].line = 3;
		ok(wsk_check(&w, 0) == WSK_E_TEXT, "and marks out of order");
		gm[1].line = 1; gm[0].line = -1;
		ok(wsk_check(&w, 0) == WSK_E_TEXT, "and a line before 0");
		gm[0].line = 0; gm[0].shape = 4;
		ok(wsk_check(&w, 0) == WSK_E_TEXT, "and a shape whisker does not draw");
		gm[0].shape = WSK_GM_DOT; gm[2].line = 40;
		ok(wsk_check(&w, 0) == WSK_OK && render_into(&w, sd, sizeof sd, 4096) > 0 && count(sd, "<path") == 0,
		   "a mark past the last line is ignored, not refused: the text may just have shrunk");

		/* scrolled: only the lines in view are marked, each in its row */
		SET("0\n1\n2\n3\n4\n5\n6\n7\n8\n9", 18);    /* the caret on line 9: lines 5..9 shown */
		gm[0].line = 2; gm[1].line = 7; gm[1].shape = WSK_GM_DOT; buf[0].nmarks = 2;
		render_into(&w, sd, sizeof sd, 4096);
		ok(count(sd, "width=\"8\" height=\"8\"") == 1 && has(sd, "<rect x=\"8\" y=\"54\" width=\"8\" height=\"8\""),
		   "scrolled to lines 5..9: line 7's dot in the third row (y 54), line 2's not drawn");

		/* a press in the mark column is the gutter's */
		SET("a\nb\nc\nd\ne", 0);
		wsk_ui_init(&ui, &w);
		memset(&ev, 0, sizeof ev);
		ev.kind = WSK_EV_DOWN; ev.x = 8; ev.y = 50 + 8;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ev.kind = WSK_EV_UP;
		n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(n == 2 && nt[1].kind == WSK_NOTE_GUTTER && nt[1].value == 2 && buf[0].caret == 4,
		   "a press in the mark column by line 2: GUTTER, line 2 -- setting a breakpoint is the program's");
		ev.kind = WSK_EV_DOWN; ev.x = 50 + 1;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ev.kind = WSK_EV_UP;
		n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(n == 1 && nt[0].kind == WSK_NOTE_CLICK, "CONTROL: at x 50, the text's first column, a click and no GUTTER");
#undef SET
	}

	/* ---- §5w. words: Ctrl's moves, double- and triple-click ---- */
	printf("\n-- §5w  words\n");
	{
		static const wsk_widget_t FW[] = {
			{ .kind = WSK_FIELD, .id = "f", .sub = 0, .size = 32 },
			{ .kind = WSK_MEMO,  .id = "m", .sub = 1, .size = 80 },
		};
		char fb[64] = "", mb[64] = "", ub[512];
		wsk_buf_t buf[2];
		wsk_undo_t u;
		wsk_tree_t w;
		wsk_ui_t ui;
		wsk_note_t nt[4];
		wsk_event_t ev;
		int n, y0 = 0, y1 = 0;

		memset(buf, 0, sizeof buf);
		memset(&u, 0, sizeof u);
		u.b = ub; u.cap = sizeof ub;
		buf[0].b = fb; buf[0].cap = sizeof fb; buf[0].undo = &u;
		buf[1].b = mb; buf[1].cap = sizeof mb;
		wsk_init(&w, WSK_COL, 320, 200);
		w.pad = 4; w.gap = 4; w.item = FW; w.n = 2; w.buf = buf; w.bufs = 2; w.caps = WSK_CAP_UTF8;
		wsk_ui_init(&ui, &w);
		wsk_ui_focus(&ui, "f", &y0, &y1);
		memset(&ev, 0, sizeof ev);
#define SETB(B, S, C) (snprintf((B).b, (size_t)(B).cap, "%s", (S)), (B).len = (int)strlen((B).b), \
                       (B).caret = (C), (B).top = 0, (B).span = 0)
#define KEY(K)  (ev.kind = WSK_EV_KEY, ev.key = (unsigned short)(K), n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1))
#define CTRL(K) KEY((K) | WSK_MOD_CTRL)

		/*            0         1         2
		              0123456789012345678901234567 */
		SETB(buf[0], "if x_1 := f(y)  then caf\xc3\xa9", 0);
		CTRL(WSK_KEY_RIGHT);
		ok(n == 0 && buf[0].caret == 2, "Ctrl-Right from the start: the end of \"if\", 2 -- and no KEY notice, the field used it");
		CTRL(WSK_KEY_RIGHT);
		ok(buf[0].caret == 6, "again: over the space, to the end of \"x_1\" -- '_' and digits are in a word");
		CTRL(WSK_KEY_RIGHT);
		ok(buf[0].caret == 9, "again: \":=\", marks are a word of their own");
		CTRL(WSK_KEY_RIGHT); CTRL(WSK_KEY_RIGHT);
		ok(buf[0].caret == 12, "f, then \"(\" -- 12");
		CTRL(WSK_KEY_RIGHT); CTRL(WSK_KEY_RIGHT); CTRL(WSK_KEY_RIGHT);
		ok(buf[0].caret == 20, "y, \")\", then over two spaces to the end of \"then\", 20");
		CTRL(WSK_KEY_RIGHT);
		ok(buf[0].caret == 26 && buf[0].caret == buf[0].len, "\"caf\xc3\xa9\" is one word, its \xc3\xa9 inside it: the end, byte 26");
		CTRL(WSK_KEY_RIGHT);
		ok(n == 0 && buf[0].caret == 26, "at the end, it stays");
		CTRL(WSK_KEY_LEFT);
		ok(buf[0].caret == 21, "Ctrl-Left: the start of \"caf\xc3\xa9\"");
		CTRL(WSK_KEY_LEFT);
		ok(buf[0].caret == 16, "over the space to the start of \"then\"");
		buf[0].caret = 18;
		CTRL(WSK_KEY_LEFT);
		ok(buf[0].caret == 16, "from inside a word: its own start");
		buf[0].caret = 2;
		CTRL(WSK_KEY_LEFT); CTRL(WSK_KEY_LEFT);
		ok(buf[0].caret == 0, "and to 0, where it stays");

		/* with Shift, a selection */
		buf[0].caret = 3;
		KEY(WSK_KEY_RIGHT | WSK_MOD_CTRL | WSK_MOD_SHIFT);
		ok(buf[0].caret == 6 && buf[0].span == -3, "Ctrl-Shift-Right selects \"x_1\"");
		KEY(WSK_KEY_RIGHT | WSK_MOD_CTRL | WSK_MOD_SHIFT);
		ok(buf[0].caret == 9 && buf[0].span == -6, "and extends over \" :=\"");
		CTRL(WSK_KEY_RIGHT);
		ok(buf[0].span == 0, "Ctrl-Right without Shift lets it go");

		/* deleting words */
		SETB(buf[0], "print total  + 1", 13);
		memset(&u, 0, sizeof u); u.b = ub; u.cap = sizeof ub;
		CTRL(WSK_KEY_BACKSPACE);
		ok(n == 1 && strcmp(fb, "print + 1") == 0 && buf[0].caret == 6,
		   "Ctrl-Backspace after \"total  \": the spaces and the word, back to 6");
		CTRL(WSK_KEY_DELETE);
		ok(strcmp(fb, "print  1") == 0, "Ctrl-Delete: the \"+\" after, a word of marks");
		KEY('z' | WSK_MOD_CTRL);
		ok(strcmp(fb, "print + 1") == 0, "each is one undo step: the \"+\" back");
		KEY('z' | WSK_MOD_CTRL);
		ok(strcmp(fb, "print total  + 1") == 0 && buf[0].caret == 13, "and \"total  \" back, the caret after it");
		buf[0].caret = 0; buf[0].span = 5;
		CTRL(WSK_KEY_DELETE);
		ok(strcmp(fb, " total  + 1") == 0, "with a selection, Ctrl-Delete takes the selection, not a word");
		buf[0].caret = 0; buf[0].span = 0;
		CTRL(WSK_KEY_BACKSPACE);
		ok(n == 0, "Ctrl-Backspace at 0: nothing, and no notice");

		/* the whole text's ends, in a memo */
		wsk_ui_focus(&ui, "m", &y0, &y1);
		SETB(buf[1], "one\ntwo\nthree", 5);
		KEY(WSK_KEY_HOME);
		ok(buf[1].caret == 4, "CONTROL: in a memo, Home is the line's start, 4");
		CTRL(WSK_KEY_HOME);
		ok(buf[1].caret == 0, "Ctrl-Home the text's, 0");
		KEY(WSK_KEY_END | WSK_MOD_CTRL | WSK_MOD_SHIFT);
		ok(buf[1].caret == 13 && buf[1].span == -13, "Ctrl-Shift-End selects to the text's end");
		buf[1].caret = 4; buf[1].span = 0;
		CTRL(WSK_KEY_LEFT);
		ok(buf[1].caret == 0, "Ctrl-Left passes a line break as a space: from \"two\" to \"one\"'s start");

		/* clicks: the memo is at y 40, its first row's text at 46..62, x 10 a column 0 */
		SETB(buf[1], "say hello_world(x) now\nnext line", 0);
		ev.kind = WSK_EV_DOWN; ev.key = 0; ev.x = 10 + 8 * 7 + 3; ev.y = 50; ev.d = 1;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(buf[1].caret == 7 && buf[1].span == 0, "CONTROL: a single click (d 1) puts the caret, selects nothing");
		ev.kind = WSK_EV_UP; wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ev.kind = WSK_EV_DOWN; ev.d = 2;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(buf[1].caret == 15 && buf[1].span == -11, "a double-click (d 2) in \"hello_world\" selects it, 4..15");
		ev.kind = WSK_EV_MOVE; ev.x += 3;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(buf[1].caret == 15 && buf[1].span == -11, "a jitter before the release keeps the word");
		ev.x = 10 + 8 * 20 + 1;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(buf[1].caret == 20 && buf[1].span == -16, "a drag on to column 20 extends from the word's start");
		ev.x = 10 + 8 * 1;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(buf[1].caret == 1 && buf[1].span == 14, "and back to column 1, from its end: the word stays inside");
		ev.kind = WSK_EV_UP; wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ev.kind = WSK_EV_DOWN; ev.x = 10 + 8 * 15 + 1; ev.d = 2;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(buf[1].caret == 16 && buf[1].span == -1, "on the \"(\": a word of marks, and \"x\" after it is not one -- 15..16");
		ev.kind = WSK_EV_UP; wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ev.kind = WSK_EV_DOWN; ev.x = 10 + 8 * 5 + 1; ev.d = 3;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(buf[1].caret == 22 && buf[1].span == -22, "a triple-click: the line as typed, 0..22, not its break");
		ev.kind = WSK_EV_UP; wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ev.kind = WSK_EV_DOWN; ev.x = 10 + 8 * 30; ev.d = 2;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(buf[1].caret == 22 && buf[1].span == -3, "a double-click past a line's end: the word it ends, \"now\"");
		ev.kind = WSK_EV_UP; wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ev.d = 0;

		/* with no field focused, the chords are still the program's */
		ui.root->focus = 0; ui.focus_b = 0;
		CTRL(WSK_KEY_LEFT);
		ok(n == 1 && nt[0].kind == WSK_NOTE_KEY && nt[0].value == (WSK_KEY_LEFT | WSK_MOD_CTRL),
		   "with no field focused, Ctrl-Left is a KEY notice, as before");
#undef SETB
#undef KEY
#undef CTRL
	}

	/* ---- §5x. a divider a person can drag ---- */
	printf("\n-- §5x  dividers\n");
	{
		static const wsk_widget_t FIXW[] = {        /* a fixed pane, a weighted one */
			{ .kind = WSK_LABEL,   .id = "nav", .text = "nav", .size = 100 },
			{ .kind = WSK_DIVIDER, .id = "div", .size = 6 },
			{ .kind = WSK_LABEL,   .id = "ed",  .text = "editor", .weight = 1 },
		};
		static const wsk_widget_t WFIX[] = {        /* weighted, then fixed */
			{ .kind = WSK_LABEL,   .id = "ed",  .text = "editor", .weight = 1 },
			{ .kind = WSK_DIVIDER, .id = "div", .size = 6 },
			{ .kind = WSK_LABEL,   .id = "out", .text = "out", .size = 80 },
		};
		static const wsk_widget_t WW[] = {          /* two weighted */
			{ .kind = WSK_LABEL,   .id = "a", .text = "a", .weight = 1 },
			{ .kind = WSK_DIVIDER, .id = "div", .size = 6 },
			{ .kind = WSK_LABEL,   .id = "b", .text = "b", .weight = 1 },
		};
		static const wsk_widget_t FF[] = {          /* two fixed */
			{ .kind = WSK_LABEL,   .id = "a", .text = "a", .size = 100 },
			{ .kind = WSK_DIVIDER, .id = "div", .size = 6 },
			{ .kind = WSK_LABEL,   .id = "b", .text = "b", .size = 100 },
		};
		short sz[3];
		wsk_tree_t w;
		wsk_ui_t ui;
		wsk_note_t nt[4];
		wsk_event_t ev;
		int n, y0 = 0, y1 = 0, which = -2;
		char sd[2048];

		/* a row 320 wide, padding 4, no gap: 312 to share; the font 16, so
		   no pane goes below 32 */
		wsk_init(&w, WSK_ROW, 320, 200);
		w.pad = 4; w.gap = 0; w.item = FIXW; w.n = 3;
		ok(wsk_check(&w, &which) == WSK_E_KIND && which == 1,
		   "a divider in a stack with no sizes array is refused: it could not move anything");
		memset(sz, 0, sizeof sz);
		w.sizes = sz;
		ok(wsk_check(&w, 0) == WSK_OK && rect_is(wsk_rect(&w, 0), 4, 4, 100, 192) &&
		   rect_is(wsk_rect(&w, 1), 104, 4, 6, 192) && rect_is(wsk_rect(&w, 2), 110, 4, 206, 192),
		   "with one, all zero, the layout is the widgets': nav 100, the divider 6, the editor the rest, 206");
		render_into(&w, sd, sizeof sd, 2048);
		ok(has(sd, "<rect x=\"104\" y=\"4\" width=\"6\" height=\"192\" fill=\"#081620\"/>"),
		   "the divider is drawn as its bar, in the trough's colour");
		sz[0] = 150;
		ok(rect_is(wsk_rect(&w, 0), 4, 4, 150, 192) && rect_is(wsk_rect(&w, 2), 160, 4, 156, 192),
		   "an entry in sizes is that item's extent: nav 150, and the weighted editor takes up the difference");
		sz[0] = 0;

		/* drag it: the pane before, fixed, grows */
		wsk_ui_init(&ui, &w);
		memset(&ev, 0, sizeof ev);
		ev.kind = WSK_EV_DOWN; ev.x = 107; ev.y = 100;
		n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(n == 0, "a press on the divider says nothing");
		ev.kind = WSK_EV_MOVE; ev.x = 137;
		n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(n == 1 && nt[0].kind == WSK_NOTE_SPLIT && nt[0].item == 1 && nt[0].value == 130 && sz[0] == 130,
		   "dragged 30 right: SPLIT, the divider, nav now 130 -- in the caller's sizes");
		ok(y0 == 0 && y1 == 200 && rect_is(wsk_rect(&w, 1), 134, 4, 6, 192),
		   "the stack's rows to repaint, and the divider at 134, under the pointer");
		ev.x = 0;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(sz[0] == 32, "dragged far left: nav stops at 32, two lines of the font");
		ev.x = 319;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(sz[0] == 312 - 6 - 56 - 32, "far right: nav stops where the editor keeps its natural 56 and 32 more -- 218");
		ev.x = 320;
		n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(n == 0 && y1 == 0, "past the clamp, nothing changes: no notice, nothing to repaint");
		ev.kind = WSK_EV_UP;
		n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(n == 0, "the release: no CLICK -- a divider is not a button");
		ev.kind = WSK_EV_MOVE; ev.x = 100;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(sz[0] == 218, "CONTROL: after the release, a move moves nothing");

		/* weighted, then fixed: the fixed pane after it moves, inversely */
		memset(sz, 0, sizeof sz);
		w.item = WFIX;
		wsk_ui_init(&ui, &w);
		ok(rect_is(wsk_rect(&w, 1), 4 + 312 - 80 - 6, 4, 6, 192), "the divider by a fixed output pane of 80, at 230");
		ev.kind = WSK_EV_DOWN; ev.x = 233;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ev.kind = WSK_EV_MOVE; ev.x = 213;
		n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(n == 1 && sz[2] == 100 && sz[0] == 0 && rect_is(wsk_rect(&w, 1), 210, 4, 6, 192),
		   "dragged 20 left: the output pane after it grows to 100, the weighted editor before it gives way");
		ev.kind = WSK_EV_UP; wsk_feed(&ui, &ev, nt, 4, &y0, &y1);

		/* two weighted: the one before becomes fixed where it was */
		memset(sz, 0, sizeof sz);
		w.item = WW;
		wsk_ui_init(&ui, &w);
		{
			int a = wsk_rect(&w, 0).w, dx = wsk_rect(&w, 1).x + 3;
			ev.kind = WSK_EV_DOWN; ev.x = (short)dx;
			wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
			ev.kind = WSK_EV_MOVE; ev.x = (short)(dx + 10);
			wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
			ok(sz[0] == a + 10 && sz[2] == 0 && wsk_rect(&w, 2).x == 4 + a + 10 + 6 &&
			   wsk_rect(&w, 2).x + wsk_rect(&w, 2).w == 316,
			   "two weighted panes: the first becomes fixed at its width and 10 more; the second, weighted, fills to 316");
			ev.kind = WSK_EV_UP; wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		}

		/* two fixed: one grows as the other shrinks */
		memset(sz, 0, sizeof sz);
		w.item = FF;
		wsk_ui_init(&ui, &w);
		ev.kind = WSK_EV_DOWN; ev.x = 107;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ev.kind = WSK_EV_MOVE; ev.x = 127;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(sz[0] == 120 && sz[2] == 80, "two fixed panes of 100: dragged 20, 120 and 80 -- their total kept");
		ev.x = 300;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(sz[0] == 168 && sz[2] == 32, "and the second stops at 32");
		ev.kind = WSK_EV_UP; wsk_feed(&ui, &ev, nt, 4, &y0, &y1);

		/* a column, nested: the same, along y */
		{
			static const wsk_widget_t TOP[] = {
				{ .kind = WSK_LABEL, .id = "title", .text = "title", .size = 40 },
				{ .kind = WSK_STACK, .id = "body", .sub = 0, .weight = 1 },
			};
			static const wsk_widget_t BODYW[] = {
				{ .kind = WSK_LABEL,   .id = "ed",  .text = "editor", .weight = 1 },
				{ .kind = WSK_DIVIDER, .id = "div", .size = 6 },
				{ .kind = WSK_LABEL,   .id = "out", .text = "out", .size = 50 },
			};
			wsk_tree_t nest[1], root;
			short bs[3] = { 0, 0, 0 };
			wsk_rect_t d0;
			wsk_init(&root, WSK_COL, 320, 200);
			root.pad = 0; root.gap = 0; root.item = TOP; root.n = 2; root.nest = nest; root.nests = 1;
			wsk_init(&nest[0], WSK_COL, 320, 200);
			nest[0].pad = 0; nest[0].gap = 0; nest[0].item = BODYW; nest[0].n = 3; nest[0].sizes = bs;
			ok(wsk_check(&root, 0) == WSK_OK, "a column nested in a column, its divider between an editor and output");
			wsk_ui_init(&ui, &root);
			d0 = rect_of(&root, "/body/div");
			ev.kind = WSK_EV_DOWN; ev.x = 100; ev.y = (short)(d0.y + 3);
			wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
			ev.kind = WSK_EV_MOVE; ev.y = (short)(d0.y + 3 - 40);
			n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
			printf("      n %d, in %s, bs %d %d %d, divider at %d, was %d\n", n,
			       n > 0 ? (nt[0].in == &nest[0] ? "nest" : nt[0].in == &root ? "root" : "?") : "-",
			       bs[0], bs[1], bs[2], rect_of(&root, "/body/div").y, d0.y);
			ok(n == 1 && nt[0].in == &nest[0] && bs[2] == 90 && rect_of(&root, "/body/div").y == d0.y - 40,
			   "dragged 40 up: SPLIT names the nested stack, output is 90, the divider 40 higher");
			ev.kind = WSK_EV_UP; wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		}

		/* refused where it cannot work */
		{
			static const wsk_widget_t FIRST[] = {
				{ .kind = WSK_DIVIDER, .id = "div", .size = 6 },
				{ .kind = WSK_LABEL,   .id = "a", .text = "a", .weight = 1 },
			};
			w.item = FIRST; w.n = 2;
			ok(wsk_check(&w, &which) == WSK_E_KIND && which == 0, "a divider first in its stack is refused: no pane before it");
			w.item = FF; w.n = 3; w.overflow = WSK_WRAP;
			ok(wsk_check(&w, &which) == WSK_E_KIND, "and sizes on a wrapping stack");
			w.overflow = WSK_REFUSE;
		}

		/* the divider's branch was first written between the selection's
		   `} else` and the box it guards, which drew every chosen button's
		   plain box over its selection -- found by the byte comparison,
		   passed by every check; this is the check */
		{
			static const wsk_widget_t CH[] = {
				{ .kind = WSK_BUTTON, .id = "p", .text = "p", .size = 30 },
				{ .kind = WSK_BUTTON, .id = "q", .text = "q", .size = 30 },
			};
			wsk_tree_t c;
			wsk_init(&c, WSK_COL, 320, 200);
			c.pad = 4; c.gap = 4; c.item = CH; c.n = 2; c.select = 1; c.sel = 1;
			render_into(&c, sd, sizeof sd, 2048);
			ok(has(sd, "<rect x=\"4\" y=\"38\" width=\"312\" height=\"30\" fill=\"#b06a2a\"/>") &&
			   !has(sd, "<rect x=\"4\" y=\"38\" width=\"312\" height=\"30\" fill=\"#2a6b8f\"/>") &&
			   has(sd, "<rect x=\"4\" y=\"4\" width=\"312\" height=\"30\" fill=\"#2a6b8f\"/>"),
			   "a chosen button is drawn in the selection's fill and nothing over it; the other in the accent (the control)");
		}
	}

	/* ---- §5y. the keyboard in a menu ---- */
	printf("\n-- §5y  menu keys\n");
	{
		static const wsk_widget_t WIN5[] = {
			{ .kind = WSK_BUTTON, .id = "pick", .text = "Pick", .size = 40 },
			{ .kind = WSK_MEMO,   .id = "m", .sub = 0, .size = 80 },
		};
		static const wsk_widget_t MENU5[] = {
			{ .kind = WSK_LABEL,  .id = "head", .text = "Colours", .size = 30 },
			{ .kind = WSK_BUTTON, .id = "red",  .text = "Red", .size = 30 },
			{ .kind = WSK_BUTTON, .id = "green", .text = "Green", .size = 30 },
			{ .kind = WSK_LABEL,  .id = "sep", .text = "", .size = 4 },
			{ .kind = WSK_BUTTON, .id = "blue", .text = "Blue", .size = 30 },
		};
		static const wsk_widget_t LONG5[] = {
			{ .kind = WSK_BUTTON, .id = "l0", .text = "zero", .size = 40 },
			{ .kind = WSK_BUTTON, .id = "l1", .text = "one", .size = 40 },
			{ .kind = WSK_BUTTON, .id = "l2", .text = "two", .size = 40 },
			{ .kind = WSK_BUTTON, .id = "l3", .text = "three", .size = 40 },
			{ .kind = WSK_BUTTON, .id = "l4", .text = "four", .size = 40 },
			{ .kind = WSK_BUTTON, .id = "l5", .text = "five", .size = 40 },
		};
		char mb[64];
		wsk_buf_t buf[1];
		wsk_tree_t w, k[2];
		wsk_ui_t ui;
		wsk_note_t nt[4];
		wsk_event_t ev;
		char md[4096];
		int n, y0 = 0, y1 = 0;

		memset(buf, 0, sizeof buf);
		snprintf(mb, sizeof mb, "one\ntwo");
		buf[0].b = mb; buf[0].cap = sizeof mb; buf[0].len = 7; buf[0].caret = 0;
		wsk_init(&w, WSK_COL, 320, 200);
		w.pad = 4; w.gap = 4; w.ids = 1; w.item = WIN5; w.n = 2; w.buf = buf; w.bufs = 1;
		w.nest = k; w.nests = 2;
		wsk_nest_init(&k[0], &w, WSK_COL); k[0].pad = 2; k[0].gap = 2; k[0].item = MENU5; k[0].n = 5;
		k[0].bg = 0x303840u;
		wsk_nest_init(&k[1], &w, WSK_COL); k[1].pad = 2; k[1].gap = 2; k[1].item = LONG5; k[1].n = 6;
		k[1].overflow = WSK_SCROLL;
		wsk_ui_init(&ui, &w);
		wsk_ui_focus(&ui, "m", &y0, &y1);
		memset(&ev, 0, sizeof ev);
#define KEY(K)  (ev.kind = WSK_EV_KEY, ev.key = (unsigned short)(K), n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1))

		KEY(WSK_KEY_DOWN);
		ok(buf[0].caret == 4, "CONTROL: with no menu open, Down moves the memo's caret, to 4");
		buf[0].caret = 0;
		wsk_ui_open(&ui, 0, "pick", &y0, &y1);
		ok(w.ohi == WSK_NONE, "a menu opens with nothing reached");
		KEY(WSK_KEY_DOWN);
		ok(w.ohi == 1 && buf[0].caret == 0 && n == 0 && y1 > y0,
		   "Down: Red, the first button -- the label above it passed over; the memo's caret not moved; the menu's rows to repaint");
		KEY(WSK_KEY_DOWN);
		ok(w.ohi == 2, "Down: Green");
		KEY(WSK_KEY_DOWN);
		ok(w.ohi == 4, "Down: Blue, past the separator");
		KEY(WSK_KEY_DOWN);
		ok(w.ohi == 1, "Down from the last: round to Red");
		KEY(WSK_KEY_UP);
		ok(w.ohi == 4, "Up from the first: round to Blue");
		KEY(WSK_KEY_HOME);
		ok(w.ohi == 1, "Home: Red");
		KEY(WSK_KEY_END);
		ok(w.ohi == 4, "End: Blue");
		render_into(&w, md, sizeof md, 4096);
		{
			wsk_rect_t b = rect_of(&w, "blue");
			char ring[96];
			/* §5m's ring: thin rectangles two pixels in from the button */
			snprintf(ring, sizeof ring, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"2\"", b.x + 2, b.y + 2, b.w - 4);
			ok(has(md, ring) && count(md, "height=\"2\"") == 2,
			   "Blue wears the focus ring, top and bottom -- and nothing else does");
		}
		KEY(WSK_KEY_UP); KEY(WSK_KEY_UP);
		KEY(WSK_KEY_ENTER);
		ok(n == 2 && nt[0].kind == WSK_NOTE_CLICK && nt[0].in == &k[0] && nt[0].item == 1 && nt[0].value == -1 &&
		   nt[1].kind == WSK_NOTE_CLOSE && w.over == WSK_NONE && w.ohi == WSK_NONE,
		   "Up, Up, Enter: CLICK on Red and CLOSE -- what a click on Red gives");
		render_into(&w, md, sizeof md, 4096);
		ok(count(md, "height=\"2\"") == 0 && (const void *)w.focus == (const void *)&WIN5[1],
		   "closed: no ring, and the memo still has focus");
		KEY(WSK_KEY_DOWN);
		ok(buf[0].caret == 4, "and Down is the memo's again");

		buf[0].caret = 0;
		wsk_ui_open(&ui, 0, "pick", &y0, &y1);
		KEY(WSK_KEY_ENTER);
		ok(n == 0 && w.over == 0 && strcmp(mb, "one\ntwo") == 0,
		   "Enter with nothing reached: nothing -- no line break in the memo, the menu stays");
		KEY(WSK_KEY_UP);
		ok(w.ohi == 4, "Up with nothing reached starts at the last, Blue");
		KEY(WSK_KEY_ESCAPE);

		/* a menu that scrolls: six of 40 in a screen of 200 */
		wsk_ui_open(&ui, 1, "pick", &y0, &y1);
		ok(w.over == 1 && k[1].scroll == 0, "a long menu opens, scrolling, at its top");
		for (int i = 0; i < 6; i++) KEY(WSK_KEY_DOWN);
		{
			wsk_rect_t five = rect_of(&w, "l5");
			ok(w.ohi == 5 && k[1].scroll > 0 && five.y + five.h <= w.oy + w.oh - k[1].pad,
			   "Down to the sixth: the menu scrolled until it is whole in sight");
		}
		KEY(WSK_KEY_HOME);
		ok(w.ohi == 0 && k[1].scroll == 0, "Home: the first, scrolled back to the top");
		KEY(WSK_KEY_ESCAPE);
#undef KEY
	}

	/* ---- §5q step 1. a world: placed items, a view, a zoom ---- */
	printf("\n-- §5q  a world, step 1\n");
	{
		static const wsk_widget_t WI[] = {
			{ .kind = WSK_BUTTON, .id = "a", .text = "Ann" },
			{ .kind = WSK_BUTTON, .id = "b", .text = "Bo" },
			{ .kind = WSK_LABEL,  .id = "c", .text = "poster" },
		};
		wsk_place_t pl[3] = {
			{ .x = 10,  .y = 20, .w = 100, .h = 30, .scale = 256 },
			{ .x = 60,  .y = 30, .w = 100, .h = 30, .scale = 256 },     /* over a, later: on top */
			{ .x = 250, .y = 150, .w = 120, .h = 30, .scale = 256 },    /* running off the right */
		};
		wsk_tree_t w;
		wsk_ui_t ui;
		wsk_note_t nt[4];
		wsk_event_t ev;
		char wd[4096];
		int which = -2, wx = 0, wy = 0, y0, y1, n;

		wsk_init(&w, WSK_COL, 320, 200);
		w.ids = 1; w.item = WI; w.n = 3; w.overflow = WSK_WORLD;
		ok(wsk_check(&w, &which) == WSK_E_KIND && which == -1, "a world with no places is refused");
		w.place = pl;
		ok(wsk_check(&w, 0) == WSK_OK, "with them, accepted -- overlapping, and running off the view");
		ok(rect_is(wsk_rect(&w, 0), 10, 20, 100, 30) && rect_is(wsk_rect(&w, 1), 60, 30, 100, 30),
		   "at 1:1 from 0,0, a place is its rectangle");
		render_into(&w, wd, sizeof wd, 4096);
		ok(strstr(wd, "id=\"a\"") != 0 && strstr(wd, "id=\"a\"") < strstr(wd, "id=\"b\""),
		   "drawn in array order: Bo after Ann, on top of it");
		ok(!has(wd, ">poster</text>"), "the poster running off the right edge: its text is not whole, so not drawn");
		pl[2].x = 150;
		render_into(&w, wd, sizeof wd, 4096);
		ok(has(wd, ">poster</text>"), "CONTROL: moved 100 left, wholly in view, it is");
		pl[2].x = 250;
		ok(strcmp(wsk_hit_id(&w, 80, 40), "b") == 0 && strcmp(wsk_hit_id(&w, 20, 25), "a") == 0,
		   "hit asks the topmost first: where they overlap, Bo; where only Ann is, Ann");
		ok(wsk_hit_id(&w, 200, 100) == 0, "CONTROL: bare floor is nothing");

		w.vx = 50;
		ok(rect_is(wsk_rect(&w, 0), -40, 20, 100, 30), "the view moved 50 right: Ann at -40");
		render_into(&w, wd, sizeof wd, 4096);
		ok(has(wd, "<rect id=\"a\" y=\"20\" width=\"60\" height=\"30\"") && !has(wd, ">Ann</text>"),
		   "cut at the view's edge to 60 wide, its text not whole and so not drawn");
		w.vx = 0;

		w.zoom = 512;
		ok(rect_is(wsk_rect(&w, 0), 20, 40, 200, 60), "zoom 2: Ann at 20,40, 200 x 60");
		render_into(&w, wd, sizeof wd, 4096);
		ok(has(wd, "<g font-size=\"32\" fill=\"#fff\"><rect id=\"a\""),
		   "and its text twice the font, 32, in a <g> of its own");
		w.zoom = 128;
		ok(rect_is(wsk_rect(&w, 0), 5, 10, 50, 15), "zoom 1/2: 5,10, 50 x 15");
		w.zoom = 256;
		pl[0].scale = 512;
		ok(rect_is(wsk_rect(&w, 0), 10, 20, 200, 60), "an item's own scale, 2: its size doubled where it stands");
		render_into(&w, wd, sizeof wd, 4096);
		ok(has(wd, "<g font-size=\"32\" fill=\"#fff\"><rect id=\"a\"") && !has(wd, "<g font-size=\"32\" fill=\"#fff\"><rect id=\"b\""),
		   "and its font, 32 -- Bo, at 1:1, has none of its own");
		pl[0].scale = 256;

		/* far from the origin: past what a short can say */
		pl[0].x = 2000000000; pl[0].y = 1000000;
		w.vx = 2000000000 - 10; w.vy = 1000000 - 20;
		ok(wsk_check(&w, 0) == WSK_OK && rect_is(wsk_rect(&w, 0), 10, 20, 100, 30),
		   "an open world two billion units out: Ann at 10,20 in a view there");
		ok(wsk_rect(&w, 1).w == 0 && wsk_hit_id(&w, 70, 40) != 0 && strcmp(wsk_hit_id(&w, 70, 40), "a") == 0,
		   "and Bo, two billion away, is nowhere -- not wrapped onto the screen, not hit");
		ok(wsk_world_at(&w, 100, 50, &wx, &wy) == WSK_OK && wx == 2000000000 + 90 && wy == 1000000 + 30,
		   "wsk_world_at: a screen point back to the world, 32-bit");
		pl[0].x = 10; pl[0].y = 20; w.vx = 0; w.vy = 0;
		w.zoom = 512; w.vx = 1000;
		ok(wsk_world_at(&w, 100, 50, &wx, &wy) == WSK_OK && wx == 1050 && wy == 25,
		   "zoomed 2 with the view at 1000: screen 100,50 is world 1050,25");
		ok(wsk_world_at(&w, 400, 50, &wx, &wy) == WSK_E_NOTFOUND, "off the area, nothing");
		w.zoom = 256; w.vx = 0;

		/* bounded */
		w.wspace = WSK_BOUNDED; w.ww = 1000; w.wh = 500;
		w.vx = 680;
		ok(wsk_check(&w, 0) == WSK_OK, "bounded, 1000 x 500: a view from 680 is inside");
		w.vx = 681;
		ok(wsk_check(&w, &which) == WSK_E_SCROLL && which == -1, "from 681 it would leave the world: refused, as a scroll past its end is");
		w.vx = 0; w.wspace = WSK_OPEN;

		/* refusals */
		w.zoom = 0;
		ok(wsk_check(&w, 0) == WSK_E_KIND, "a zoom of 0 is refused");
		w.zoom = 256; pl[1].scale = 0;
		ok(wsk_check(&w, &which) == WSK_E_KIND && which == 1, "and a scale of 0, naming the item");
		pl[1].scale = 256;
		{
			static const wsk_widget_t WS[] = {
				{ .kind = WSK_STACK, .id = "poster", .sub = 0 },
			};
			wsk_tree_t k[1];
			w.item = WS; w.n = 1; w.nest = k; w.nests = 1;
			wsk_nest_init(&k[0], &w, WSK_COL);
			ok(wsk_check(&w, &which) == WSK_OK,
			   "a stack inside a world is a poster (step 2b), accepted -- an empty one fits anywhere");
			w.item = WI; w.n = 3; w.nest = 0; w.nests = 0;
		}

		/* a host that clips is given a clip, and draws what is cut */
		w.caps = WSK_CAP_CLIP;
		render_into(&w, wd, sizeof wd, 4096);
		ok(has(wd, "<clipPath id=\"wsk-c0-0\">") && has(wd, ">poster</text>"),
		   "for a host that clips: the world in a clip, and the poster drawn whole for it to cut");
		w.caps = 0;

		/* the toolkit: a click is the topmost's */
		wsk_ui_init(&ui, &w);
		memset(&ev, 0, sizeof ev);
		ev.kind = WSK_EV_DOWN; ev.x = 80; ev.y = 40;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ev.kind = WSK_EV_UP;
		n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(n == 1 && nt[0].kind == WSK_NOTE_CLICK && nt[0].item == 1,
		   "a click where Ann and Bo overlap: CLICK on Bo, the one on top");
	}

	/* ---- §5q step 2. the torus, and fade ---- */
	printf("\n-- §5q  a world, step 2: the torus and fade\n");
	{
		static const wsk_widget_t WT[] = {
			{ .kind = WSK_BUTTON, .id = "s", .text = "S", .fill = 0x405060u },
		};
		static const wsk_widget_t WF[] = {
			{ .kind = WSK_BUTTON, .id = "f0", .text = "near", .fill = 0x808080u },
			{ .kind = WSK_BUTTON, .id = "f1", .text = "far",  .fill = 0x808080u },
		};
		wsk_place_t pl[2] = {
			{ .x = 380, .y = 50, .w = 40, .h = 30, .scale = 256 },    /* across the x seam at 400 */
		};
		wsk_tree_t w;
		char td[4096];
		int which = -2, wx, wy;

		wsk_init(&w, WSK_COL, 320, 200);
		w.ids = 1; w.item = WT; w.n = 1; w.overflow = WSK_WORLD; w.place = pl;
		w.wspace = WSK_TORUS; w.ww = 400; w.wh = 300;
		ok(wsk_check(&w, 0) == WSK_OK, "a torus 400 x 300, viewed 320 x 200 from 0,0");
		w.vx = 100;
		/* from the view at 100, the item at 380 is 280 along, and runs to
		   420: past the seam, so a copy at 280 - 400 = -120 runs to -80 --
		   off this view; but from 300 the item is at 80 and its copy at -320 */
		ok(rect_is(wsk_rect(&w, 0), 280, 50, 40, 30), "the item at 380, from a view at 100: 280");
		w.vx = 390;
		/* 380 - 390 wraps to 390: drawn at 390 (off the right) and a world
		   back at -10, straddling the left edge */
		render_into(&w, td, sizeof td, 4096);
		ok(count(td, "id=\"s\"") == 1 && has(td, "<rect id=\"s\" y=\"50\" width=\"30\" height=\"30\""),
		   "from a view at 390 it wraps: its copy a world back, at -10, cut to 0..30 -- the seam crossed");
		ok(strcmp(wsk_hit_id(&w, 10, 60), "s") == 0, "and hit there, on the copy");
		w.vx = 370;
		ok(wsk_hit_id(&w, 12, 60) != 0 && wsk_hit_id(&w, 48, 60) != 0 && wsk_hit_id(&w, 5, 60) == 0,
		   "from 370: the item at 10..50, whole, hit at both ends and not at 5 -- the seam seen from the other side");
		pl[0].x = 390; pl[0].y = 290; w.vx = 0; w.vy = 0;
		render_into(&w, td, sizeof td, 4096);
		ok(count(td, "id=\"s\"") == 1 && has(td, "<rect id=\"s\" width=\"30\" height=\"20\""),
		   "an item at a corner, 390,290: its copy a world back both ways, at 0,0, cut to 30 x 20");
		{
			static const wsk_widget_t W4[] = {
				{ .kind = WSK_BUTTON, .id = "s", .text = "S", .fill = 0x405060u },
			};
			wsk_place_t p4[1] = { { .x = 300, .y = 180, .w = 40, .h = 40, .scale = 256 } };
			wsk_tree_t q;
			wsk_init(&q, WSK_COL, 320, 200);
			q.ids = 1; q.item = W4; q.n = 1; q.overflow = WSK_WORLD; q.place = p4;
			q.wspace = WSK_TORUS; q.ww = 330; q.wh = 210; q.vx = 0; q.vy = 0;
			render_into(&q, td, sizeof td, 4096);
			ok(count(td, "id=\"s\"") == 4,
			   "in a world barely bigger than its view, an item across both seams is drawn four times");
			ok(wsk_hit_id(&q, 2, 2) != 0 && wsk_hit_id(&q, 310, 190) != 0 && wsk_hit_id(&q, 2, 190) != 0 &&
			   wsk_hit_id(&q, 310, 2) != 0 && wsk_hit_id(&q, 100, 100) == 0,
			   "and each of the four is hit; the middle, the control, is bare floor");
		}
		w.vx = 350; w.vy = 0;
		ok(wsk_world_at(&w, 100, 10, &wx, &wy) == WSK_OK && wx == 50 && wy == 10,
		   "wsk_world_at wraps: 350 + 100 is 450, which is 50 in a world of 400");
		w.vx = -1000;
		ok(wsk_check(&w, 0) == WSK_OK, "the view may be anywhere, even -1000: it wraps");
		w.zoom = 128;
		ok(wsk_check(&w, &which) == WSK_E_FIT && which == -1,
		   "but zoomed out to see 640 x 400 of a 400 x 300 world: refused, WSK_E_FIT");
		w.zoom = 256;

		/* fade */
		{
			wsk_place_t pf[2] = {
				{ .x = 10,  .y = 10, .w = 100, .h = 30, .scale = 256, .fade = 0 },
				{ .x = 150, .y = 10, .w = 100, .h = 30, .scale = 256, .fade = 128 },
			};
			wsk_tree_t f;
			wsk_init(&f, WSK_COL, 320, 200);
			f.ids = 1; f.item = WF; f.n = 2; f.overflow = WSK_WORLD; f.place = pf;
			f.bg = 0x000000u + 0x102030u; f.ink = 0xffffffu;
			render_into(&f, td, sizeof td, 4096);
			ok(has(td, "id=\"f0\" x=\"10\" y=\"10\" width=\"100\" height=\"30\" fill=\"#808080\""),
			   "fade 0: the fill exactly as given");
			/* 128 is a = 129 of 256: each channel (c x 127 + bg x 129) >> 8 */
			ok(has(td, "id=\"f1\" x=\"150\" y=\"10\" width=\"100\" height=\"30\" fill=\"#474f57\""),
			   "fade 128: halfway to the background -- 80,80,80 toward 10,20,30 is 47,4f,57");
			ok(has(td, "<g font-size=\"16\" fill=\"#868e96\"><rect id=\"f1\""),
			   "and its text, inheriting the window's white, half faded too: ff,ff,ff to 86,8e,96");
			pf[1].fade = 255;
			render_into(&f, td, sizeof td, 4096);
			ok(has(td, "id=\"f1\" x=\"150\" y=\"10\" width=\"100\" height=\"30\" fill=\"#102030\""),
			   "fade 255: the background itself");
		}
	}

	/* ---- §5q step 3. pan and zoom ---- */
	printf("\n-- §5q  a world, step 3: pan and zoom\n");
	{
		static const wsk_widget_t WP[] = {
			{ .kind = WSK_BUTTON, .id = "p", .text = "Pat" },
		};
		wsk_place_t pl[1] = { { .x = 100, .y = 100, .w = 60, .h = 30, .scale = 256 } };
		wsk_tree_t w;
		wsk_ui_t ui;
		wsk_note_t nt[4];
		wsk_event_t ev;
		int n, y0, y1, wx, wy, wx2, wy2;

		wsk_init(&w, WSK_COL, 320, 200);
		w.ids = 1; w.item = WP; w.n = 1; w.overflow = WSK_WORLD; w.place = pl;
		w.wspace = WSK_BOUNDED; w.ww = 1000; w.wh = 800; w.vx = 200; w.vy = 200;
		wsk_ui_init(&ui, &w);
		memset(&ev, 0, sizeof ev);
#define EV(K, X, Y) (ev.kind = (K), ev.x = (short)(X), ev.y = (short)(Y), n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1))

		EV(WSK_EV_DOWN, 150, 100);
		EV(WSK_EV_MOVE, 160, 100);
		ok(n == 0 && w.vx == 200, "a press on bare floor, moved 10 -- inside the slop: the view stays");
		EV(WSK_EV_MOVE, 110, 70);
		ok(n == 1 && nt[0].kind == WSK_NOTE_VIEW && nt[0].in == &w && nt[0].value == 256 &&
		   w.vx == 240 && w.vy == 230 && y0 == 0 && y1 == 200,
		   "dragged 40 left and 30 up: VIEW, and the view 40 right and 30 down -- the floor follows the pointer");
		EV(WSK_EV_MOVE, 2000, 100);
		ok(w.vx == 0, "dragged far right: the bounded view stops at the world's left edge, 0 -- clamped, not refused");
		EV(WSK_EV_UP, 2000, 100);
		ok(n == 0, "the release after a drag: no click");

		/* a click on an item without moving is still a click */
		w.vx = 0; w.vy = 0;
		EV(WSK_EV_DOWN, 120, 110);
		EV(WSK_EV_UP, 120, 110);
		ok(n == 1 && nt[0].kind == WSK_NOTE_CLICK && nt[0].item == 0, "CONTROL: a press and release on Pat is a click on Pat");

		/* the wheel zooms about the pointer */
		w.vx = 100; w.vy = 100;
		wsk_world_at(&w, 160, 120, &wx, &wy);
		ev.d = -40;
		EV(WSK_EV_WHEEL, 160, 120);
		wsk_world_at(&w, 160, 120, &wx2, &wy2);
		ok(n == 1 && nt[0].kind == WSK_NOTE_VIEW && w.zoom == 256 + 40 && nt[0].value == 296 &&
		   wx2 >= wx - 1 && wx2 <= wx + 1 && wy2 >= wy - 1 && wy2 <= wy + 1,
		   "a wheel notch up: zoom in by 40/256, and the world point under the pointer stays under it");
		ev.d = 40;
		for (int i = 0; i < 40; i++) EV(WSK_EV_WHEEL, 160, 120);
		ok(w.zoom == 82 && w.vx == 0 && w.vy == 0,
		   "zoomed far out: it stops where the whole 1000 x 800 world fits the view -- 320 / 1000 of 1:1, 82 -- inside it");
		EV(WSK_EV_WHEEL, 160, 120);
		ok(n == 0, "and past that, nothing changes and nothing is said");
		ev.d = 0;

		/* a torus's view wraps as it is dragged */
		w.wspace = WSK_TORUS; w.ww = 400; w.wh = 300; w.zoom = 256; w.vx = 10; w.vy = 10;
		EV(WSK_EV_DOWN, 100, 100);
		EV(WSK_EV_MOVE, 150, 100);
		ok(w.vx == 360 && w.vy == 10, "a torus dragged 50 right from a view at 10: the view wraps to 360");
		EV(WSK_EV_UP, 150, 100);

		/* bare floor: a root world says so, and where */
		EV(WSK_EV_DOWN, 250, 20);
		EV(WSK_EV_UP, 250, 20);
		ok(n == 1 && nt[0].kind == WSK_NOTE_CLICK && nt[0].in == &w && nt[0].item == -1,
		   "a click on a root world's bare floor: CLICK, item -1, in the world");
		{
			const wsk_tree_t *wd = 0;
			ok(wsk_world_point(&w, 250, 20, &wd, &wx, &wy) == WSK_OK && wd == &w && wx == 210 && wy == 30,
			   "and wsk_world_point: that world, and the point 360 + 250 = 610, wrapped to 210, and 30");
		}
		{
			/* a world nested in a window: its own widget answers for its floor */
			static const wsk_widget_t TOPW[] = {
				{ .kind = WSK_LABEL, .id = "bar", .text = "bar", .size = 40 },
				{ .kind = WSK_STACK, .id = "floor", .sub = 0, .weight = 1 },
			};
			wsk_tree_t root, k[1];
			const wsk_tree_t *wd = 0;
			wsk_init(&root, WSK_COL, 320, 200);
			root.pad = 0; root.gap = 0; root.item = TOPW; root.n = 2; root.nest = k; root.nests = 1;
			wsk_nest_init(&k[0], &root, WSK_COL);
			k[0].pad = 0; k[0].item = WP; k[0].n = 1; k[0].overflow = WSK_WORLD; k[0].place = pl;
			k[0].vx = 1000; k[0].vy = 2000;
			ok(wsk_check(&root, 0) == WSK_OK, "a world as a pane under a 40-px bar");
			wsk_ui_init(&ui, &root);
			EV(WSK_EV_DOWN, 250, 140);
			EV(WSK_EV_UP, 250, 140);
			ok(n == 1 && nt[0].kind == WSK_NOTE_CLICK && nt[0].in == &root && nt[0].item == 1,
			   "nested, a click on its bare floor is a CLICK on the world's own widget");
			ok(wsk_world_point(&root, 250, 140, &wd, &wx, &wy) == WSK_OK && wd == &k[0] &&
			   wx == 1250 && wy == 2100,
			   "and wsk_world_point finds the world inside the window: 1000 + 250, 2000 + (140 - 40)");
			ok(wsk_world_point(&root, 250, 20, &wd, &wx, &wy) == WSK_E_NOTFOUND, "CONTROL: on the bar, no world");
		}
#undef EV
	}

	/* ---- §5z. a list or a world: one Tab stop, and its keys ---- */
	printf("\n-- §5z  lists and worlds from the keyboard\n");
	{
		static const wsk_widget_t TOPZ[] = {
			{ .kind = WSK_BUTTON, .id = "go",    .text = "Go", .size = 30 },
			{ .kind = WSK_STACK,  .id = "list",  .sub = 0, .size = 100 },
			{ .kind = WSK_STACK,  .id = "world", .sub = 1, .weight = 1 },
		};
		static const wsk_widget_t ROWS[] = {
			{ .kind = WSK_LABEL, .id = "r0", .text = "zero",  .size = 30 },
			{ .kind = WSK_LABEL, .id = "r1", .text = "one",   .size = 30 },
			{ .kind = WSK_LABEL, .id = "r2", .text = "two",   .size = 30 },
			{ .kind = WSK_LABEL, .id = "r3", .text = "three", .size = 30 },
			{ .kind = WSK_LABEL, .id = "r4", .text = "four",  .size = 30 },
		};
		static const wsk_widget_t SPR[] = { { .kind = WSK_BUTTON, .id = "spr", .text = "S" } };
		wsk_place_t pl[1] = { { .x = 50, .y = 20, .w = 40, .h = 20, .scale = 256 } };
		wsk_tree_t w, k[2];
		wsk_ui_t ui;
		wsk_note_t nt[4];
		wsk_event_t ev;
		char zd[4096];
		int n, y0, y1;

		wsk_init(&w, WSK_COL, 320, 240);
		w.pad = 0; w.gap = 0; w.item = TOPZ; w.n = 3; w.nest = k; w.nests = 2;
		wsk_nest_init(&k[0], &w, WSK_COL);
		k[0].pad = 0; k[0].gap = 0; k[0].item = ROWS; k[0].n = 5; k[0].select = 1;
		k[0].overflow = WSK_SCROLL;                         /* 150 of rows in 100 */
		wsk_nest_init(&k[1], &w, WSK_COL);
		k[1].pad = 0; k[1].item = SPR; k[1].n = 1; k[1].overflow = WSK_WORLD; k[1].place = pl;
		ok(wsk_check(&w, 0) == WSK_OK, "a button, a scrolling list of five rows, and a world");
		wsk_ui_init(&ui, &w);
		memset(&ev, 0, sizeof ev);
#define KEY(K)  (ev.kind = WSK_EV_KEY, ev.key = (unsigned short)(K), n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1))
#define TEXT(T) (ev.kind = WSK_EV_TEXT, snprintf(ev.text, sizeof ev.text, "%s", (T)), \
                 n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1))
		KEY(WSK_KEY_TAB); KEY(WSK_KEY_TAB);
		ok(w.focus == &TOPZ[1], "Tab, Tab: Go, then the list -- one stop");
		render_into(&w, zd, sizeof zd, 4096);
		ok(has(zd, "<rect x=\"2\" y=\"32\" width=\"316\" height=\"2\" fill=\"#fff\"/>"),
		   "the list wears the focus ring, two pixels in from its edge");
		KEY(WSK_KEY_DOWN);
		ok(n == 1 && nt[0].kind == WSK_NOTE_SELECT && nt[0].in == &k[0] && nt[0].item == 0 && k[0].sel == 0,
		   "Down with nothing chosen: the first row, SELECT");
		KEY(WSK_KEY_DOWN); KEY(WSK_KEY_DOWN); KEY(WSK_KEY_DOWN);
		ok(k[0].sel == 3 && k[0].scroll == 20, "Down three more: row 3, at 90..120 of a 100-row list -- scrolled 20 to show it whole");
		KEY(WSK_KEY_DOWN); KEY(WSK_KEY_DOWN);
		ok(k[0].sel == 4 && n == 0, "Down at the last row: it stays, and says nothing");
		KEY(WSK_KEY_HOME);
		ok(k[0].sel == 0 && k[0].scroll == 0, "Home: the first, scrolled back");
		KEY(WSK_KEY_END);
		ok(k[0].sel == 4 && k[0].scroll == 50, "End: the last, scrolled to the end, 50");
		KEY(WSK_KEY_ENTER);
		ok(n == 1 && nt[0].kind == WSK_NOTE_CLICK && nt[0].in == &k[0] && nt[0].item == 4,
		   "Enter: a CLICK on the chosen row, as a tap on it gives");
		KEY(WSK_KEY_LEFT);
		ok(n == 0 && k[0].sel == 4, "CONTROL: Left means nothing to a list");

		KEY(WSK_KEY_TAB);
		ok(w.focus == &TOPZ[2], "Tab: the world -- one stop too");
		KEY(WSK_KEY_RIGHT);
		ok(n == 1 && nt[0].kind == WSK_NOTE_VIEW && k[1].vx == 40 && k[1].vy == 0,
		   "Right pans an eighth of the 320-wide view: 40");
		KEY(WSK_KEY_DOWN);
		ok(k[1].vy == 13, "Down an eighth of its 110 rows: 13");
		KEY(WSK_KEY_LEFT); KEY(WSK_KEY_UP);
		ok(k[1].vx == 0 && k[1].vy == 0, "Left and Up: back");
		TEXT("+");
		ok(n == 1 && k[1].zoom == 320 && k[1].vx == 32 && k[1].vy == 11,
		   "+ zooms in a quarter, about the view's middle: 160,55 stays where it was");
		TEXT("-");
		ok(k[1].zoom == 256, "- zooms back out");
		TEXT("x");
		ok(n == 0, "CONTROL: other typing means nothing to a world");

		wsk_ui_init(&ui, &w);
		ok(wsk_ui_focus(&ui, "world", &y0, &y1) == WSK_OK && w.focus == &TOPZ[2],
		   "a program gives a world focus by its id, as the floor demo does at the start");
		ok(wsk_ui_focus(&ui, "go", &y0, &y1) == WSK_E_NOTFOUND, "CONTROL: a button is not one to give it to");
#undef KEY
#undef TEXT
	}

	/* ---- §5q step 4. what touches what ---- */
	printf("\n-- §5q  a world, step 4: what touches what\n");
	{
		static const wsk_widget_t WC[] = {
			{ .kind = WSK_BUTTON, .id = "a", .text = "a" },
			{ .kind = WSK_BUTTON, .id = "b", .text = "b" },
			{ .kind = WSK_BUTTON, .id = "c", .text = "c" },
		};
		wsk_place_t pl[3] = {
			{ .x = 0,   .y = 0, .w = 50, .h = 20, .scale = 256 },
			{ .x = 40,  .y = 10, .w = 50, .h = 20, .scale = 256 },    /* overlaps a */
			{ .x = 100, .y = 0, .w = 20, .h = 20, .scale = 512 },     /* 40 x 40 at its scale */
		};
		wsk_tree_t w;
		int out[4], n;

		wsk_init(&w, WSK_COL, 320, 200);
		w.item = WC; w.n = 3; w.overflow = WSK_WORLD; w.place = pl;
		n = wsk_world_overlaps(&w, 0, out, 4);
		ok(n == 1 && out[0] == 1, "a touches b, and only b -- itself left out");
		n = wsk_world_overlaps(&w, 1, out, 4);
		ok(n == 1 && out[0] == 0, "and b touches a");
		ok(wsk_world_in(&w, 95, 30, 10, 10, out, 4) == 1 && out[0] == 2,
		   "c, 20 x 20 at scale 2, covers 100..140: a box at 95..105, 30..40 touches it -- its own scale counts");
		ok(wsk_world_in(&w, 0, 25, 10, 10, out, 4) == 0, "CONTROL: a box under a, above nothing, touches nothing");
		ok(wsk_world_in(&w, 0, 0, 200, 50, out, 2) == 3 && out[0] == 2 && out[1] == 1,
		   "a box over all three: three, the topmost first, and only as many written as asked");
		pl[1].x = 50;
		ok(wsk_world_overlaps(&w, 0, out, 4) == 0, "edges that meet do not overlap: a ends at 50, b starts there");

		/* a torus: touching across the seam */
		w.wspace = WSK_TORUS; w.ww = 400; w.wh = 300;
		pl[0].x = 390; pl[0].y = 0; pl[1].x = 20; pl[1].y = 0;     /* a: 390..440, which is 390..400 and 0..40 */
		n = wsk_world_overlaps(&w, 0, out, 4);
		ok(n == 1 && out[0] == 1, "on a torus, a at 390 runs past the seam to 40, and touches b at 20");
		ok(wsk_world_overlaps(&w, 1, out, 4) == 1, "and b, seen from the other side, touches a");
		w.wspace = WSK_OPEN;
		ok(wsk_world_overlaps(&w, 0, out, 4) == 0, "CONTROL: the same places in an open world do not touch");
	}

	/* ---- §5q step 5. a view that follows ---- */
	printf("\n-- §5q  a world, step 5: a view that follows\n");
	{
		static const wsk_widget_t WF5[] = { { .kind = WSK_BUTTON, .id = "me", .text = "Me" } };
		wsk_place_t pl[1] = { { .x = 100, .y = 80, .w = 40, .h = 20, .scale = 256 } };
		wsk_tree_t w;
		int y0, y1, r, calls = 0;

		/* a root world, 320 x 200 at 1:1, bounded 1000 x 800 */
		wsk_init(&w, WSK_COL, 320, 200);
		w.item = WF5; w.n = 1; w.overflow = WSK_WORLD; w.place = pl;
		w.wspace = WSK_BOUNDED; w.ww = 1000; w.wh = 800;
		r = wsk_world_follow(&w, 0, 0, 30, 256, &y0, &y1);
		ok(r == 0 && w.vx == 0 && w.vy == 0,
		   "margin 30%: an item at 100,80 is 96 and 60 from the edges it must keep -- the view does not move");
		pl[0].x = 250;
		r = wsk_world_follow(&w, 0, 0, 30, 256, &y0, &y1);
		ok(r == 1 && w.vx == 66 && w.vy == 0 && y0 == 0 && y1 == 200,
		   "walked to 250: past 320 - 96 - 40 = 184, so the view moves 66 -- and the world's rows to repaint");
		pl[0].x = 500; pl[0].y = 300;
		wsk_world_follow(&w, 0, 0, 50, 256, &y0, &y1);
		ok(w.vx == 360 && w.vy == 210, "margin 50: centred -- 500 - (320 - 40) / 2 = 360, 300 - (200 - 20) / 2 = 210");
		w.vx = 0; w.vy = 0;
		wsk_world_follow(&w, 0, 0, 50, 64, &y0, &y1);
		ok(w.vx == 90 && w.vy == 52, "ease 64: a quarter of the way this call -- 90 of 360, 52 of 210");
		while (wsk_world_follow(&w, 0, 0, 50, 64, &y0, &y1) == 1 && calls < 100) calls++;
		ok(w.vx == 360 && w.vy == 210 && calls < 40,
		   "and called again each frame it arrives exactly, not stopping a unit short");
		pl[0].x = 980; pl[0].y = 790;
		wsk_world_follow(&w, 0, 0, 50, 256, &y0, &y1);
		ok(w.vx == 680 && w.vy == 600, "near the world's corner: the bounded view stops at its edge, 680,600");
		w.zoom = 512;
		pl[0].x = 500; pl[0].y = 300;
		wsk_world_follow(&w, 0, 0, 50, 256, &y0, &y1);
		ok(w.vx == 500 - (160 - 40) / 2, "zoomed 2, the view is 160 wide: centred at 440");
		w.zoom = 256;

		/* a torus: the short way round the seam */
		w.wspace = WSK_TORUS; w.vx = 900; w.vy = 0;
		pl[0].x = 50; pl[0].y = 90;
		wsk_world_follow(&w, 0, 0, 50, 256, &y0, &y1);
		ok(w.vx == 910, "a torus, the view at 900, the item at 50: 150 ahead across the seam, so 10 on -- not 850 back");
		w.wspace = WSK_OPEN;

		ok(wsk_world_follow(&w, 0, 3, 50, 256, &y0, &y1) == WSK_E_NOTFOUND, "an item that is not there: WSK_E_NOTFOUND");
		{
			static const wsk_widget_t TOPF[] = {
				{ .kind = WSK_LABEL, .id = "bar", .text = "bar", .size = 40 },
				{ .kind = WSK_STACK, .id = "floor", .sub = 0, .weight = 1 },
			};
			wsk_tree_t root, k[1];
			wsk_init(&root, WSK_COL, 320, 200);
			root.pad = 0; root.gap = 0; root.item = TOPF; root.n = 2; root.nest = k; root.nests = 1;
			wsk_nest_init(&k[0], &root, WSK_COL);
			k[0].pad = 0; k[0].item = WF5; k[0].n = 1; k[0].overflow = WSK_WORLD; k[0].place = pl;
			pl[0].x = 500; pl[0].y = 300;
			r = wsk_world_follow(&root, "floor", 0, 50, 256, &y0, &y1);
			ok(r == 1 && k[0].vx == 360 && k[0].vy == 300 - (160 - 20) / 2 && y0 == 40 && y1 == 200,
			   "a world in a window, by its id: centred in its 320 x 160 -- and only its rows, 40..200, to repaint");
			ok(wsk_world_follow(&root, "bar", 0, 50, 256, &y0, &y1) == WSK_E_NOTFOUND,
			   "CONTROL: asked of something that is not a world, WSK_E_NOTFOUND");
		}
	}

	printf("\n-- §5q  a world, step 2b: posters\n");
	{
		static const wsk_widget_t WP[] = {
			{ .kind = WSK_STACK, .id = "poster", .sub = 0, .fill = 0x404040 },
		};
		static const wsk_widget_t PI[] = {
			{ .kind = WSK_LABEL,  .id = "title", .text = "Title", .size = 24 },
			{ .kind = WSK_BUTTON, .id = "go",    .text = "Go",    .size = 32 },
		};
		static const wsk_widget_t PJ[] = {
			{ .kind = WSK_LABEL,  .id = "title", .text = "Title", .size = 24 },
			{ .kind = WSK_BUTTON, .id = "go",    .text = "Go",    .size = 52 },
		};
		static const wsk_widget_t PF[] = { { .kind = WSK_FIELD, .id = "f", .text = "x", .size = 24 } };
		wsk_place_t pl[1] = { { .x = 20, .y = 20, .w = 100, .h = 80, .scale = 256 } };
		wsk_tree_t w, k[1];
		const wsk_tree_t *in = 0;
		wsk_ui_t ui;
		wsk_note_t nt[4];
		wsk_event_t ev;
		char wd[4096];
		int which = -2, y0 = 0, y1 = 0, n;

		wsk_init(&w, WSK_COL, 320, 200);
		w.ids = 1; w.item = WP; w.n = 1; w.overflow = WSK_WORLD; w.place = pl;
		w.nest = k; w.nests = 1;
		wsk_nest_init(&k[0], &w, WSK_COL);
		k[0].item = PI; k[0].n = 2; k[0].pad = 4; k[0].gap = 4;
		ok(wsk_check(&w, &which) == WSK_OK, "a poster of 4 + 24 + 4 + 32 + 4 = 68 rows in an 80-row place: accepted");
		render_into(&w, wd, sizeof wd, 4096);
		ok(has(wd, "<rect x=\"20\" y=\"20\" width=\"100\" height=\"80\" fill=\"#404040\"/><text x=\"70\" y=\"40\">Title</text>"
		           "<rect id=\"go\" x=\"24\" y=\"52\" width=\"92\" height=\"32\""),
		   "at 1:1 the poster is its place, 20,20 100 x 80, and lays out as a stack does: Go at 24,52, 92 x 32");
		w.zoom = 512;
		render_into(&w, wd, sizeof wd, 4096);
		ok(has(wd, "<g font-size=\"32\" fill=\"#fff\"><g id=\"poster\"><rect x=\"40\" y=\"40\" width=\"200\" height=\"160\"")
		   && has(wd, "<rect id=\"go\" x=\"48\" y=\"104\" width=\"184\" height=\"64\""),
		   "zoom 2: all of it twice -- the place, the padding, the gap, Go's size, and the font, 32");
		w.zoom = 128;
		render_into(&w, wd, sizeof wd, 4096);
		ok(has(wd, "<g font-size=\"8\" fill=\"#fff\">") && has(wd, "<rect id=\"go\" x=\"12\" y=\"26\" width=\"46\" height=\"16\""),
		   "zoom 1/2: all of it half -- Go at 12,26, 46 x 16, in font 8");
		w.zoom = 256; pl[0].scale = 512;
		render_into(&w, wd, sizeof wd, 4096);
		ok(has(wd, "<rect id=\"go\" x=\"28\" y=\"84\" width=\"184\" height=\"64\""),
		   "its own scale 2 at zoom 1: twice its size from its own corner, 20,20 -- Go at 28,84");
		pl[0].scale = 256;

		/* hit and press */
		ok(wsk_hit_in(&w, 50, 60, &in) == 1 && in == &k[0], "hit descends: 50,60 is Go, in the poster's tree");
		ok(wsk_hit_in(&w, 22, 22, &in) == 0 && in == &w, "in its padding, the poster itself, in the world");
		ok(wsk_hit_in(&w, 200, 150, &in) < 0, "CONTROL: bare floor is nothing");
		w.zoom = 512;
		ok(wsk_hit_in(&w, 100, 120, &in) == 1 && in == &k[0], "zoomed 2: Go where it is drawn, 48..232 x 104..168");
		wsk_ui_init(&ui, &w);
		memset(&ev, 0, sizeof ev);
		ev.kind = WSK_EV_DOWN; ev.x = 100; ev.y = 120;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(w.held == &PI[1] && y0 <= 104 && y1 >= 168 && y1 <= 176,
		   "pressed: Go is held, and its own scaled rows, 104..168, repaint");
		ev.kind = WSK_EV_UP;
		n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(n == 1 && nt[0].kind == WSK_NOTE_CLICK && nt[0].in == &k[0] && nt[0].item == 1,
		   "released: CLICK on Go, in the poster");
		w.zoom = 256;

		/* fit is the poster's design, checked at 1:1 */
		k[0].item = PJ;
		ok(wsk_check_in(&w, &which, &in) == WSK_E_FIT && which == 1 && in == &k[0],
		   "a poster of 88 rows in an 80-row place: refused, WSK_E_FIT, naming Go");
		w.zoom = 512;
		ok(wsk_check(&w, 0) == WSK_E_FIT, "and refused zoomed 2, though 176 rows would be drawn in 160 -- it is checked as declared");
		w.zoom = 77;
		ok(wsk_check(&w, 0) == WSK_E_FIT, "and at 0.3: no zoom makes a poster fit that does not");
		k[0].item = PI;
		ok(wsk_check(&w, 0) == WSK_OK, "CONTROL: the 68-row poster at 0.3 is accepted");
		render_into(&w, wd, sizeof wd, 4096);
		ok(has(wd, "<g font-size=\"4\""), "and drawn, in font 16 x 0.3 = 4");
		w.zoom = 256;

		/* cut by the view */
		w.vx = 60;
		render_into(&w, wd, sizeof wd, 4096);
		ok(has(wd, "<rect id=\"go\" y=\"52\" width=\"56\" height=\"32\"") && !has(wd, ">Title</text>"),
		   "the view moved 60 right: Go cut at its edge to 0..56, Title not whole and not drawn");
		w.vx = 0;

		/* fade reaches inside */
		pl[0].fade = 128;
		render_into(&w, wd, sizeof wd, 4096);
		ok(has(wd, "fill=\"#272f37\"") && has(wd, "<rect id=\"go\" x=\"24\" y=\"52\" width=\"92\" height=\"32\" fill=\"#1c455f\""),
		   "fade 128: the poster's fill 40,40,40 to 27,2f,37 and Go's accent 2a,6b,8f to 1c,45,5f -- each (c x 127 + bg x 129) >> 8");
		pl[0].fade = 0;

		/* refused inside a poster, by name */
		k[0].item = PF; k[0].n = 1;
		ok(wsk_check_in(&w, &which, &in) == WSK_E_KIND && which == 0 && in == &k[0],
		   "a field inside a poster is refused, as in a world, until a caret is placed at a scale");
		k[0].item = PI; k[0].n = 2;
		ok(wsk_check(&w, 0) == WSK_OK, "CONTROL: the poster again, accepted");
	}

	/* a poster with an abstract: wrapped text reflows in proportion */
	{
		static const wsk_widget_t WP[] = { { .kind = WSK_STACK, .id = "poster", .sub = 0 } };
		static const wsk_widget_t PA[] = {
			{ .kind = WSK_LABEL, .id = "title", .text = "Damage", .size = 24 },
			{ .kind = WSK_STACK, .id = "abstract", .sub = 1, .weight = 1 },
		};
		static const wsk_widget_t AB[] = { { .kind = WSK_TEXT, .id = "ab", .sub = 0 } };
		const char *strs[1];
		wsk_place_t pl[1] = { { .x = 0, .y = 0, .w = 160, .h = 120, .scale = 256 } };
		wsk_tree_t w, k[2];
		char wd[4096];
		int lines1, lines2, lines3;

		strs[0] = "rows that changed are drawn again and nothing else";
		wsk_init(&w, WSK_COL, 320, 200);
		w.item = WP; w.n = 1; w.overflow = WSK_WORLD; w.place = pl;
		w.nest = k; w.nests = 2; w.str = strs; w.strs = 1;
		wsk_nest_init(&k[0], &w, WSK_COL);
		k[0].item = PA; k[0].n = 2; k[0].pad = 4; k[0].gap = 4;
		wsk_nest_init(&k[1], &w, WSK_ROW);
		k[1].item = AB; k[1].n = 1; k[1].overflow = WSK_WRAP; k[1].pad = 0; k[1].gap = 8; k[1].lgap = 0;
		ok(wsk_check(&w, 0) == WSK_OK, "a poster with a title and a wrapping abstract");
		render_into(&w, wd, sizeof wd, 4096);
		lines1 = count(wd, "<text");
		w.zoom = 512;
		ok(wsk_check(&w, 0) == WSK_OK, "zoomed 2");
		render_into(&w, wd, sizeof wd, 4096);
		lines2 = count(wd, "<text");
		w.zoom = 128;
		render_into(&w, wd, sizeof wd, 4096);
		lines3 = count(wd, "<text");
		ok(lines1 > 2 && lines1 == lines2 && lines1 == lines3,
		   "the abstract breaks into the same lines at 1, 2 and 1/2 -- it reflows in proportion, not to the screen");
	}

	/* ---- what an independent read of the night's code found ---- */
	printf("\n-- review fixes, 2026-11-15\n");
	{
		static const wsk_widget_t B1[] = { { .kind = WSK_BUTTON, .id = "b", .text = "B" } };
		static const wsk_widget_t FL[] = { { .kind = WSK_FIELD, .id = "f", .sub = 0 } };
		wsk_place_t pl[1] = { { .x = 380, .y = 280, .w = 60, .h = 40, .scale = 256 } };
		wsk_tree_t w;
		wsk_ui_t ui;
		wsk_note_t nt[4];
		wsk_event_t ev;
		int y0 = 0, y1 = 0, n, which = -2;

		/* a torus's button pressed through its wrapped copy repaints there */
		wsk_init(&w, WSK_COL, 320, 200);
		w.item = B1; w.n = 1; w.overflow = WSK_WORLD; w.place = pl;
		w.wspace = WSK_TORUS; w.ww = 400; w.wh = 300;
		wsk_ui_init(&ui, &w);
		memset(&ev, 0, sizeof ev);
		ev.kind = WSK_EV_DOWN; ev.x = 10; ev.y = 10;
		wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(w.held == &B1[0] && y0 == 0 && y1 >= 20,
		   "a torus's button pressed where its copy shows (0..20): those rows repaint, not its first copy's off the screen");
		ev.kind = WSK_EV_UP;
		n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(n == 1 && nt[0].kind == WSK_NOTE_CLICK && y0 == 0 && y1 >= 20, "and the release clicks it and repaints the same rows");

		/* a floor bigger than a short, zoomed in, still covers the view */
		pl[0].x = 0; pl[0].y = 0; pl[0].w = 2000; pl[0].h = 2000;
		w.wspace = WSK_OPEN; w.zoom = 4096; w.vx = 1100; w.vy = 1100;
		ok(wsk_hit(&w, 100, 100) == 0, "a 2000-unit floor at zoom 16, seen from its middle: hit, not nowhere");
		w.vx = 990; w.vy = 990;
		ok(wsk_hit(&w, 300, 190) == 0, "and seen near its corner: covering the view to its far edge, not stopping short");
		w.zoom = 256; w.vx = w.vy = 0;

		/* a bounded world of no size, a wheel that does not turn */
		w.wspace = WSK_BOUNDED; w.ww = 0; w.wh = 0;
		ok(wsk_check(&w, 0) == WSK_E_KIND, "a bounded world of size 0 is refused -- no division by it");
		w.wspace = WSK_OPEN;
		ev.kind = WSK_EV_WHEEL; ev.d = 0; ev.x = 100; ev.y = 100;
		n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		ok(n == 0 && w.zoom == 256, "a wheel event of 0 zooms nothing");

		/* a field in a world: refused until its caret is placed at the item's scale */
		{
			char fb[8] = "";
			wsk_buf_t buf[1];
			memset(buf, 0, sizeof buf);
			buf[0].b = fb; buf[0].cap = 8;
			w.item = FL; w.buf = buf; w.bufs = 1;
			ok(wsk_check(&w, &which) == WSK_E_KIND && which == 0, "an editable field in a world is refused, for now, by name");
			w.item = B1; w.buf = 0; w.bufs = 0;
		}

		/* two small fixed panes: a drag that has no room moves nothing */
		{
			static const wsk_widget_t SM[] = {
				{ .kind = WSK_LABEL,   .id = "a", .text = "a", .size = 10 },
				{ .kind = WSK_DIVIDER, .id = "d", .size = 6 },
				{ .kind = WSK_LABEL,   .id = "c", .text = "c", .size = 10 },
			};
			short sz[3] = { 0, 0, 0 };
			wsk_tree_t c;
			wsk_init(&c, WSK_COL, 320, 200);
			c.pad = 0; c.gap = 0; c.item = SM; c.n = 3; c.sizes = sz;
			wsk_ui_init(&ui, &c);
			ev.kind = WSK_EV_DOWN; ev.x = 100; ev.y = 13; ev.d = 0;
			wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
			ev.kind = WSK_EV_MOVE; ev.y = 14;
			n = wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
			ok(n == 0 && sz[0] == 0 && sz[2] == 0,
			   "two fixed panes of 10, under the 32 a pane keeps: a drag moves nothing, and writes no negative size");
			ev.kind = WSK_EV_UP; wsk_feed(&ui, &ev, nt, 4, &y0, &y1);
		}

		/* UTF-8 the long way round, a surrogate, past U+10FFFF; a span inside a character */
		ok(!wsk__ok_n("\xe0\x80\x80", 3, WSK_CAP_UTF8, 0) && !wsk__ok_n("\xed\xa0\x80", 3, WSK_CAP_UTF8, 0) &&
		   !wsk__ok_n("\xf4\x90\x80\x80", 4, WSK_CAP_UTF8, 0) && wsk__ok_n("\xe2\x80\x94\xf4\x8f\xbf\xbf", 7, WSK_CAP_UTF8, 0),
		   "an overlong, a surrogate and a code point past U+10FFFF refused; a dash and U+10FFFF, the control, accepted");
		{
			static const wsk_widget_t F1[] = { { .kind = WSK_FIELD, .id = "f", .sub = 0, .size = 32 } };
			char fb[16] = "a\xc3\xa9";
			wsk_buf_t buf[1];
			wsk_tree_t c;
			memset(buf, 0, sizeof buf);
			buf[0].b = fb; buf[0].cap = 16; buf[0].len = 3; buf[0].caret = 0; buf[0].span = 2;
			wsk_init(&c, WSK_COL, 320, 200);
			c.item = F1; c.n = 1; c.buf = buf; c.bufs = 1; c.caps = WSK_CAP_UTF8;
			ok(wsk_check(&c, 0) == WSK_E_TEXT, "a selection ending inside a character is refused");
			buf[0].span = 3;
			ok(wsk_check(&c, 0) == WSK_OK, "CONTROL: ending after it, accepted");
		}
	}

	/* Leave the documents where run.sh can hand them to a real XML parser. */
	{
		wsk_tree_t g = t;
		const char *ids[1];
		FILE *f;
		g.item = SETTINGS; g.n = 3;
		render_into(&g, doc, sizeof doc, 4096);
		f = fopen("/tmp/wsk_full.svg", "wb"); fputs(doc, f); fclose(f);
		ids[0] = "ok";
		clip_into(&g, ids, 1, clip, sizeof clip, 4096);
		f = fopen("/tmp/wsk_clip.svg", "wb"); fputs(clip, f); fclose(f);
	}

	printf(fails ? "\n%d check(s) failed\n" : "\nall checks passed\n", fails);
	return fails != 0;
}
