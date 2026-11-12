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
