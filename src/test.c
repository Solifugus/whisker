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
	{ WSK_LABEL,  "title",  "Settings", 0, 0, WSK_INHERIT, WSK_INHERIT },
	{ WSK_BUTTON, "ok",     "OK",       0, 0, WSK_INHERIT, WSK_INHERIT },
	{ WSK_BUTTON, "cancel", "Cancel",   0, 0, WSK_INHERIT, WSK_INHERIT },
};

static const wsk_widget_t BIG[] = {
	{ WSK_BUTTON, "pump",    "Pump",    0, 0, WSK_INHERIT, WSK_INHERIT },
	{ WSK_BUTTON, "valve_a", "Valve A", 0, 0, WSK_INHERIT, WSK_INHERIT },
	{ WSK_BUTTON, "valve_b", "Valve B", 0, 0, WSK_INHERIT, WSK_INHERIT },
	{ WSK_BUTTON, "heater",  "Heater",  0, 0, WSK_INHERIT, WSK_INHERIT },
	{ WSK_BUTTON, "fan",     "Fan",     0, 0, WSK_INHERIT, WSK_INHERIT },
	{ WSK_BUTTON, "alarm",   "Alarm",   0, 0, WSK_INHERIT, WSK_INHERIT },
	{ WSK_BUTTON, "purge",   "Purge",   0, 0, WSK_INHERIT, WSK_INHERIT },
	{ WSK_BUTTON, "drain",   "Drain",   0, 0, WSK_INHERIT, WSK_INHERIT },
	{ WSK_BUTTON, "fill",    "Fill",    0, 0, WSK_INHERIT, WSK_INHERIT },
	{ WSK_BUTTON, "vent",    "Vent",    0, 0, WSK_INHERIT, WSK_INHERIT },
	{ WSK_BUTTON, "reset",   "Reset",   0, 0, WSK_INHERIT, WSK_INHERIT },
	{ WSK_BUTTON, "start",   "Start",   0, 0, WSK_INHERIT, WSK_INHERIT },
};

/* Deliberately not band-aligned: 30 rows from an odd offset, so the clip has
   to snap and the backing has to follow it. */
static const wsk_widget_t RAGGED[] = {
	{ WSK_BUTTON, "a", "A", 30, 0, WSK_INHERIT, WSK_INHERIT },
	{ WSK_BUTTON, "b", "B", 30, 0, WSK_INHERIT, WSK_INHERIT },
	{ WSK_BUTTON, "c", "C", 30, 0, WSK_INHERIT, WSK_INHERIT },
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
		bad[0].kind = WSK_BUTTON; bad[0].fill = WSK_INHERIT; bad[0].ink = WSK_INHERIT;
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
