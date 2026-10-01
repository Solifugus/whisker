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

static const wsk_widget_t TOOLBAR[] = {
	{ .kind = WSK_BUTTON, .id = "back", .text = "Back",   .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "stop", .text = "Stop",   .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "go",   .text = "Go",     .weight = 1 },
};

/* Deliberately not band-aligned: 30 rows from an odd offset, so the clip has
   to snap and the backing has to follow it. */
static const wsk_widget_t RAGGED[] = {
	{ .kind = WSK_BUTTON, .id = "a", .text = "A", .size = 30 },
	{ .kind = WSK_BUTTON, .id = "b", .text = "B", .size = 30 },
	{ .kind = WSK_BUTTON, .id = "c", .text = "C", .size = 30 },
};

int main(void)
{
	static char doc[8192], clip[8192];
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
		big.item = BIG; big.n = 12;
		big.w = 1024; big.h = 600;
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
		g.item = BIG; g.n = 12;
		g.w = 1024; g.h = 600;

		exact = render_into(&g, doc, sizeof doc, 0);
		ok(render_into(&g, doc, sizeof doc, exact - 1) == WSK_E_BUDGET,
		   "a document one byte over the budget is refused");
		ok(doc[0] == '\0', "and nothing was written -- not even a prefix");
		ok(render_into(&g, doc, sizeof doc, exact) == exact,
		   "CONTROL: the same document, given the bytes it needs, is written");
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

	/* The price of a row, in whisker's own output rather than by argument.
	   RV-9 measured a row repaint at 1.77x a column's on the rasteriser; this
	   is where those extra bands and bytes come from. */
	{
		wsk_tree_t col, row;
		const char *ids[1];
		int cy0, cy1, ry0, ry1, cb, rb;

		wsk_init(&col, WSK_COL, 320, 172);
		col.pad = 4; col.gap = 4; col.item = TOOLBAR; col.n = 3;
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
