/*
 * `flick`, generated.
 *
 * RV-9's own driver and rasteriser, built natively through tools/hosttest's
 * stubs, drawing a whisker panel: the whole screen, then one button repainted
 * in a pressed colour through the clip, then back. That is the loop a tap
 * will be, and it is the loop `flick` hand-wrote in three string literals.
 *
 * It checks at the pixels what no geometric test can see -- that the band
 * comes back carrying the scene rather than the device's black -- and it
 * leaves two PPMs for a person to look at, which is the check that found the
 * lumpy circles and the black strips.
 */
#include "drv_svgwin.c"
#include "whisker.h"

#include <stdio.h>
#include <string.h>

#ifndef PW
#define PW 320
#endif
#ifndef PH
#define PH 172
#endif

static uint16_t screen[PW * PH];
static int blits, fails;

rv9_io_err_t rv9_panel_open(bool l, int *w, int *h)
{ (void)l; if (w) *w = PW; if (h) *h = PH; return RV9_IO_OK; }
void rv9_panel_size(int *w, int *h) { if (w) *w = PW; if (h) *h = PH; }
void rv9_panel_backlight(uint32_t p) { (void)p; }
uint32_t rv9_panel_backlight_get(void) { return 100; }
bool rv9_panel_take(const void *o) { (void)o; return false; }
/* RV-9 split the panel in two on 2026-10-11: the C5's SPI ST7789 wants
   its pixels byte-swapped and the P4's MIPI-DSI does not. These harnesses
   have always emulated the C5, and every swap in this file assumes it, so
   say so rather than inherit whichever the linker finds. */
bool rv9_panel_swaps_bytes(void) { return true; }
void rv9_panel_blit(int x0, int y0, int x1, int y1, const uint16_t *px)
{
	blits++;
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++)
			screen[y * PW + x] = px[(y - y0) * (x1 - x0) + (x - x0)];
}

static uint16_t at(int x, int y)
{
	return (uint16_t)((screen[y * PW + x] >> 8) | (screen[y * PW + x] << 8));
}

static void ok(int cond, const char *what)
{
	printf("%s  %s\n", cond ? "ok  " : "FAIL", what);
	if (!cond) fails++;
}

/* The leftmost and rightmost pixel of a given colour inside a rectangle.
   Used to ask where a field's text actually landed, which is the only way to
   tell "left-aligned" from "centred and happens to be long". */
static int span(wsk_rect_t r, uint16_t want, int *lo, int *hi)
{
	uint16_t sw = (uint16_t)((want >> 8) | (want << 8));
	int found = 0;
	*lo = PW; *hi = -1;
	for (int y = r.y; y < r.y + r.h && y < PH; y++)
		for (int x = r.x; x < r.x + r.w && x < PW; x++)
			if (screen[y * PW + x] == sw) {
				if (x < *lo) *lo = x;
				if (x > *hi) *hi = x;
				found++;
			}
	return found;
}

static uint16_t pix565(unsigned int c)
{
	return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
}

static void shot(const char *name)
{
	FILE *o = fopen(name, "wb");
	fprintf(o, "P6\n%d %d\n255\n", PW, PH);
	for (int i = 0; i < PW * PH; i++) {
		uint16_t v = (uint16_t)((screen[i] >> 8) | (screen[i] << 8));
		fputc(((v >> 11) & 0x1F) * 255 / 31, o);
		fputc(((v >> 5) & 0x3F) * 255 / 63, o);
		fputc((v & 0x1F) * 255 / 31, o);
	}
	fclose(o);
	printf("     wrote %s\n", name);
}

/* The sink that writes straight to the window. Nothing is buffered: the
   document goes to the device as it is produced. */
static void to_window(wsk_sink_t *s, const char *b, int n)
{
	rv9_dev_t *dev = (rv9_dev_t *)s->ctx;
	size_t done;
	svgwin_write(dev, b, (size_t)n, &done);
}

/* Composition (design.md §5c): the body wsk_compose runs twice. `part` is an
   array of trees rather than of pointers to them, which is the module rule
   doing its job even here, where nothing links as a module. */
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

static const wsk_widget_t TOOLBAR[] = {
	{ .kind = WSK_BUTTON, .id = "back", .text = "Back", .weight = 1 },
	{ .kind = WSK_BUTTON, .id = "stop", .text = "STOP", .weight = 1,
	  .fill = 0x8f2a2au },
	{ .kind = WSK_BUTTON, .id = "go",   .text = "Go",   .weight = 1 },
};

static const wsk_widget_t PANEL[] = {
	{ .kind = WSK_LABEL, .id = "title", .text = "tap test" },
	{ .kind = WSK_BUTTON, .id = "confirm", .text = "CONFIRM" },
	{ .kind = WSK_BUTTON, .id = "cancel", .text = "Cancel" },
};

int main(void)
{
	rv9_dev_t dev;
	wsk_tree_t t;
	wsk_widget_t live[3];
	wsk_sink_t s;
	const char *ids[1];
	int y0, y1;
	uint16_t scene_left, pressed_left;

	memset(&dev, 0, sizeof dev);
	dev.opt[OPT_ROTATE] = 1;
	dev.opt[OPT_BG] = 0x0000;            /* the device clears bands to black */
	svgwin_init(&dev);
	svgwin_open(&dev, 3);

	/* The tree holds a pointer, so it is built at runtime; the widgets are
	   copied out of rodata only because this one changes a fill. */
	wsk_clone(live, PANEL, 3);
	wsk_init(&t, WSK_COL, PW, PH);
	t.pad = 4;
	t.gap = 4;
	t.bg = 0x10203au;
	t.item = live;
	t.n = 3;

	s.put = to_window; s.ctx = &dev; s.len = 0;
	blits = 0;
	{
		int n = wsk_render(&t, &s, 4096);
		printf("  the whole screen: %d bytes, %d bands\n", n, blits);
	}
	shot("panel_full.ppm");

	/* x=1 is inside the padding, left of every widget: scene, not button. */
	scene_left = at(1, 50);
	ok(scene_left != 0x0000, "the scene painted the margin beside the buttons");

	/*
	 * A tap, resolved rather than assumed. This is the loop: a point from
	 * the panel becomes a name, the name becomes a row range, the range
	 * becomes one clipped document. Nothing else in whisker is involved and
	 * nothing in it holds state.
	 */
	{
		const char *tapped = wsk_hit_id(&t, PW / 2, 60);
		ok(tapped != 0 && tapped[0] == 'c' && tapped[1] == 'o',
		   "a point inside the button resolves to its name");
		ids[0] = tapped;
	}
	wsk_rows(&t, ids, 1, &y0, &y1);
	{
		uint32_t v = ((uint32_t)y0 << 16) | (uint32_t)y1;
		ok(svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v) == RV9_IO_OK,
		   "the window took the row range whisker computed");
	}
	{
		uint16_t before = at(PW / 2, (y0 + y1) / 2);
		int n;
		live[1].fill = 0x8fd0ffu;
		live[1].ink = 0x112233u;    /* dark on the pale fill, as flick does */
		live[1].mark = WSK_MARK_CORNERS;
		s.put = to_window; s.ctx = &dev; s.len = 0;
		blits = 0;
		n = wsk_render_clipped(&t, ids, 1, &s, 4096);
		printf("  the press: %d bytes, %d bands of %d (rows %d..%d)\n",
		       n, blits, (PH + 7) / 8, y0, y1);
		shot("panel_pressed.ppm");

		ok(at(PW / 2, (y0 + y1) / 2) != before, "the button changed colour");
	}

	pressed_left = at(1, 50);
	ok(pressed_left == scene_left,
	   "the margin beside the button survived the repaint (it is not the device's black)");
	ok(at(1, y0 - 2) == scene_left, "and so did the row just above the clip");
	ok(at(1, y1 + 2) == scene_left, "and the row just below it");

	/* Release: back to the original fill, same rows. */
	{
		uint32_t v = ((uint32_t)y0 << 16) | (uint32_t)y1;
		svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v);
	}
	live[1].fill = WSK_INHERIT;
	live[1].ink = WSK_INHERIT;
	live[1].mark = 0;
	s.put = to_window; s.ctx = &dev; s.len = 0;
	wsk_render_clipped(&t, ids, 1, &s, 4096);
	ok(at(1, 50) == scene_left, "and again after the release");
	ok(at(1, 20) == scene_left, "the title's rows were never touched");

	/*
	 * And the tap that lands in a gap. There is no name, so there is no row
	 * range and nothing is written -- which has to be true at the pixels and
	 * not only in the return value, because a repaint of "nothing" would
	 * still clear its bands to the device colour.
	 */
	{
		static uint16_t before[PW * PH];
		int same = 1;
		wsk_rect_t a = wsk_rect(&t, 1);

		for (int i = 0; i < PW * PH; i++) before[i] = screen[i];
		blits = 0;
		{
			const char *nothing = wsk_hit_id(&t, PW / 2, a.y + a.h + 1);
			ok(nothing == 0, "a point in the gap resolves to no name at all");
			if (nothing != 0) {
				const char *miss[1];
				miss[0] = nothing;
				s.put = to_window; s.ctx = &dev; s.len = 0;
				wsk_render_clipped(&t, miss, 1, &s, 4096);
			}
		}
		for (int i = 0; i < PW * PH; i++) if (screen[i] != before[i]) { same = 0; break; }
		ok(blits == 0 && same,
		   "so nothing was written and not one pixel moved -- at the pixels, not the return value");
	}

	/* The other axis, which nothing had drawn. A row of three, and one of
	   them pressed -- the case where repainting one carries its neighbours,
	   because they share its rows. */
	{
		wsk_widget_t bar[3];
		wsk_tree_t rt;
		int n;

		wsk_clone(bar, TOOLBAR, 3);
		wsk_init(&rt, WSK_ROW, PW, PH);
		rt.pad = 6;
		rt.gap = 6;
		rt.bg = 0x10203au;
		wsk_area(&rt, 0, 0, PW, 84);     /* a toolbar across the top, not a screen */
		rt.item = bar;
		rt.n = 3;

		memset(screen, 0, sizeof screen);
		s.put = to_window; s.ctx = &dev; s.len = 0;
		blits = 0;
		n = wsk_render(&rt, &s, 4096);
		printf("  a row of three: %d bytes, %d bands\n", n, blits);

		bar[1].mark = WSK_MARK_CORNERS;
		bar[1].ink = 0xffe0e0u;
		ids[0] = "stop";
		wsk_rows(&rt, ids, 1, &y0, &y1);
		{
			uint32_t v = ((uint32_t)y0 << 16) | (uint32_t)y1;
			svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v);
		}
		s.put = to_window; s.ctx = &dev; s.len = 0;
		n = wsk_render_clipped(&rt, ids, 1, &s, 4096);
		printf("  one of them pressed: %d bytes, rows %d..%d -- the whole row\n",
		       n, y0, y1);
		shot("panel_row.ppm");

		ok(at(1, 40) != 0x0000, "the row's margin survived its repaint too");
	}

	/*
	 * §5c. Two stacks over one device -- the keyboard's problem in
	 * miniature: a toolbar across the top and a pair of buttons below.
	 *
	 * Drawn the WRONG way first, deliberately, because the control for
	 * "both stacks appear" is a picture in which one of them does not.
	 * That is the whole finding: every rect in both stacks is in the right
	 * place and the toolbar is simply not on the glass.
	 */
	{
		wsk_widget_t bar[3], pair[2];
		wsk_tree_t part[2];
		comp_t c;
		wsk_rect_t rs, rc;
		int sx, sy, cx, cy, n;
		uint16_t wrong_stop, wrong_bg, right_stop, right_confirm;

		wsk_clone(bar, TOOLBAR, 3);
		wsk_init(&part[0], WSK_ROW, PW, PH);
		part[0].pad = 6; part[0].gap = 6; part[0].bg = 0x10203au;
		wsk_area(&part[0], 0, 0, PW, 60);
		part[0].item = bar; part[0].n = 3;

		wsk_clone(pair, PANEL + 1, 2);              /* CONFIRM and Cancel */
		/* 112 rows of area, 6 of padding each end, 6 of gap: two of 47.
		   Sized rather than defaulted because two 64-row buttons do not fit
		   that area -- which used to run off the bottom of the panel without
		   a word and is now refused with WSK_E_FIT (D10). */
		pair[0].size = 47;
		pair[1].size = 47;
		wsk_init(&part[1], WSK_COL, PW, PH);
		part[1].pad = 6; part[1].gap = 6; part[1].bg = 0x10203au;
		wsk_area(&part[1], 0, 60, PW, PH - 60);
		part[1].item = pair; part[1].n = 2;

		rs = wsk_rect(&part[0], 1);                 /* STOP, in the toolbar */
		rc = wsk_rect(&part[1], 0);                 /* CONFIRM, below it */
		sx = rs.x + rs.w / 2; sy = rs.y + rs.h / 2;
		cx = rc.x + rc.w / 2; cy = rc.y + rc.h / 2;

		/* the way §5 promised: two documents, each backing the whole screen */
		memset(screen, 0, sizeof screen);
		s.put = to_window; s.ctx = &dev; s.len = 0;
		wsk_render(&part[0], &s, 4096);
		s.put = to_window; s.ctx = &dev; s.len = 0;
		wsk_render(&part[1], &s, 4096);
		wrong_stop = at(sx, sy);
		wrong_bg = at(1, sy);
		shot("compose_wrong.ppm");
		ok(wrong_stop == wrong_bg,
		   "CONTROL: rendered as two documents, the toolbar is gone -- the "
		   "second backing erased it");

		/* and composed */
		memset(screen, 0, sizeof screen);
		wsk_doc_init(&c.d, &part[0]);
		c.part = part; c.n = 2;
		s.put = to_window; s.ctx = &dev; s.len = 0;
		blits = 0;
		n = wsk_compose(comp_body, &c, &s, 4096);
		right_stop = at(sx, sy);
		right_confirm = at(cx, cy);
		printf("  two stacks composed: %d bytes, %d bands\n", n, blits);
		shot("compose_right.ppm");
		ok(n > 0 && right_stop != wrong_bg,
		   "composed, the toolbar button is on the glass");
		ok(right_confirm != wrong_bg,
		   "and so is the stack below it, in the same document");
		ok(at(1, sy) != 0x0000 && at(1, cy) != 0x0000,
		   "with one scene behind both, not the device's black");

		/* a banded repaint of a widget in one stack, with the other stack
		   still in the document: the band must come back complete, and
		   nothing outside it may move */
		{
			static uint16_t before[PW * PH];
			const char *who[1];
			int moved_outside = 0;

			memcpy(before, screen, sizeof before);
			bar[1].mark = WSK_MARK_CORNERS;
			bar[1].ink = 0xffe0e0u;
			who[0] = "stop";
			ok(wsk_doc_rows(&c.d, &part[0], who, 1) == WSK_OK,
			   "the pressed widget gives the document its rows");
			{
				uint32_t v = ((uint32_t)c.d.y0 << 16) | (uint32_t)c.d.y1;
				svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v);
			}
			s.put = to_window; s.ctx = &dev; s.len = 0;
			blits = 0;
			n = wsk_compose(comp_body, &c, &s, 4096);
			printf("  pressed, through the clip: %d bytes, rows %d..%d, %d bands\n",
			       n, c.d.y0, c.d.y1, blits);
			shot("compose_pressed.ppm");

			for (int y = 0; y < PH; y++) {
				if (y >= c.d.y0 && y < c.d.y1) continue;
				for (int x = 0; x < PW; x++)
					if (screen[y * PW + x] != before[y * PW + x]) moved_outside = 1;
			}
			ok(n > 0 && at(sx, sy) != right_stop, "the press is visible");
			ok(!moved_outside,
			   "and not one pixel outside the band moved -- the stack below is intact");
			ok(at(1, sy) != 0x0000,
			   "the band came back carrying the scene, not the device background");
		}

	}

	/*
	 * §5d. A field, at the pixels.
	 *
	 * Every claim here is one the document would pass while the picture was
	 * wrong: "left-aligned" is indistinguishable from "centred" in an
	 * attribute if the text happens to fill the trough, and a caret is two
	 * columns of pixels that an assertion on <rect> cannot place.
	 */
	{
		wsk_widget_t form[3];
		wsk_tree_t f;
		wsk_rect_t r;
		wsk_sink_t fs;
		uint16_t ink = pix565(0xffffffu);
		int lo, hi, lo2, hi2, nc, n;

		wsk_clone(form, PANEL, 3);
		form[0].kind = WSK_LABEL;
		wsk__scpy(form[0].text, "Name", WSK_TEXT_MAX);
		form[1].kind = WSK_FIELD;
		wsk__scpy(form[1].text, "Ada", WSK_TEXT_MAX);
		form[1].fill = WSK_INHERIT;
		form[1].ink = WSK_INHERIT;
		form[1].mark = 0;
		wsk__scpy(form[2].text, "Save", WSK_TEXT_MAX);

		wsk_init(&f, PW >= 600 ? WSK_COL : WSK_COL, PW, PH);
		f.pad = 6; f.gap = 6; f.font = 20;
		f.bg = 0x10203au;
		f.item = form; f.n = 3;
		r = wsk_rect(&f, 1);

		memset(screen, 0, sizeof screen);
		fs.put = to_window; fs.ctx = &dev; fs.len = 0;
		n = wsk_render(&f, &fs, 4096);
		span(r, ink, &lo, &hi);
		shot("field_plain.ppm");

		/* 1. left-aligned: the text starts where the padding puts it, not in
		   the middle of the trough. A centred "Ada" would start near r.w/2. */
		ok(n > 0 && lo >= r.x && lo < r.x + WSK_FIELD_PAD + 2 * wsk_advance(f.font),
		   "the field's text starts at its left padding, not centred");
		ok(hi < r.x + r.w / 2,
		   "and all of it is in the left half, which centred text would not be");
		printf("  field text ink spans x %d..%d of a trough %d..%d\n",
		       lo, hi, r.x, r.x + r.w);

		/* 3. the caret, at the pixels, and only when marked */
		nc = span(r, ink, &lo, &hi);
		form[1].mark = WSK_MARK_CARET;
		memset(screen, 0, sizeof screen);
		fs.put = to_window; fs.ctx = &dev; fs.len = 0;
		wsk_render(&f, &fs, 4096);
		{
			int want = r.x + WSK_FIELD_PAD + 3 * wsk_advance(f.font);
			wsk_rect_t at;
			at.x = (short)(want - 1); at.y = r.y;
			at.w = (short)(WSK_CARET_W + 2); at.h = r.h;
			ok(span(at, ink, &lo2, &hi2) > 0,
			   "marked, the caret is drawn just past the last character");
			printf("  caret found at x %d..%d, three characters in from %d\n",
			       lo2, hi2, r.x + WSK_FIELD_PAD);
			span(r, ink, &lo2, &hi2);
			ok(hi2 > hi, "and it adds ink to the right of the text");

			form[1].mark = 0;
			memset(screen, 0, sizeof screen);
			fs.put = to_window; fs.ctx = &dev; fs.len = 0;
			wsk_render(&f, &fs, 4096);
			at.x = (short)(want - 1);
			ok(span(at, ink, &lo2, &hi2) == 0,
			   "CONTROL: unmarked, there is nothing at all where the caret was");
			ok(span(r, ink, &lo2, &hi2) == nc,
			   "and the field is back to exactly the ink the text alone makes");
			form[1].mark = WSK_MARK_CARET;
		}

		/* 2. the tail, at the pixels: a long string reaches the right edge */
		wsk__scpy(form[1].text, "abcdefghijklmnopqrstuvwxyz0123", WSK_TEXT_MAX);
		memset(screen, 0, sizeof screen);
		fs.put = to_window; fs.ctx = &dev; fs.len = 0;
		n = wsk_render(&f, &fs, 4096);
		span(r, ink, &lo2, &hi2);
		shot("field_full.ppm");
		ok(n > 0 && hi2 > r.x + r.w * 3 / 4,
		   "a full field reaches its right-hand end");
		ok(hi2 < r.x + r.w,
		   "and the caret has not run out of the trough");
		{
			int from, vis = wsk_field_view(&f, &form[1], r, &from);
			printf("  %d characters typed, the last %d shown, ink to x %d of %d\n",
			       wsk__len(form[1].text), vis, hi2, r.x + r.w);
			ok(from > 0, "and it is the tail that is shown, not the head");
		}
	}

	/*
	 * Justify, looked at.
	 *
	 * The suite asserts the arithmetic; this is the part an assertion cannot
	 * do -- whether five ways of placing the same three boxes are actually
	 * distinguishable on a panel this size, which is the only thing that
	 * makes the option worth having.
	 */
	{
		static wsk_widget_t cell[5][3];
		static wsk_tree_t part[5];
		static const char *NAME[5] = { "Sta", "End", "Cen", "Btw", "Arn" };
		comp_t c;
		wsk_sink_t s;
		int m, i, n;

		for (m = 0; m < 5; m++) {
			for (i = 0; i < 3; i++) {
				memset(&cell[m][i], 0, sizeof cell[m][i]);
				cell[m][i].kind = WSK_BUTTON;
				cell[m][i].size = 24;
				cell[m][i].fill = WSK_INHERIT;
				cell[m][i].ink = WSK_INHERIT;
				cell[m][i].id[0] = (char)('a' + m);
				cell[m][i].id[1] = (char)('0' + i);
				cell[m][i].id[2] = '\0';
				if (i == 0) {
					int k = 0;
					while (NAME[m][k] != '\0') { cell[m][i].text[k] = NAME[m][k]; k++; }
					cell[m][i].text[k] = '\0';
				}
			}
			wsk_init(&part[m], WSK_COL, PW, PH);
			part[m].pad = 4; part[m].gap = 4; part[m].font = 16;
			part[m].justify = (unsigned char)m;     /* START..AROUND, in order */
			wsk_area(&part[m], m * (PW / 5), 0, PW / 5, PH);
			part[m].item = cell[m]; part[m].n = 3;
		}
		wsk_doc_init(&c.d, &part[0]);
		c.part = part; c.n = 5;
		s.put = to_window; s.ctx = &dev; s.len = 0;
		{
			uint32_t v = 0;
			svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v);
		}
		n = wsk_compose(comp_body, &c, &s, 4096);
		ok(n > 0, "five justifies draw in one document");
		shot("justify.ppm");
		{
			/* Each column's first box must start lower than the one to its
			   left for START/CENTER/END, which is the whole visible point. */
			wsk_rect_t a0 = wsk_rect(&part[0], 0);
			wsk_rect_t a2 = wsk_rect(&part[2], 0);
			wsk_rect_t a1 = wsk_rect(&part[1], 0);
			ok(a0.y < a2.y && a2.y < a1.y,
			   "START sits above CENTER, which sits above END");
			printf("    first box at y: START %d, CENTER %d, END %d\n",
			       a0.y, a2.y, a1.y);
		}
	}

	/*
	 * Scrolling, at the pixels (design.md §5e).
	 *
	 * A toolbar above, a status line below, and a 200-row list in the pane
	 * between them. The suite asserts the arithmetic; what only pixels can
	 * say is that nothing a scroll draws lands outside the pane -- there is
	 * no clipPath, so whisker cutting by arithmetic is the only thing
	 * keeping a half-scrolled row off the toolbar -- and that a banded
	 * scroll step leaves the screen exactly as a full redraw would.
	 */
	{
		static wsk_widget_t list[200];
		static uint16_t full0[PW * PH], full1[PW * PH];
		static const wsk_widget_t STATUS[] = {
			{ .kind = WSK_LABEL, .id = "st", .text = "200 rows" },
		};
		wsk_tree_t part[3];
		comp_t c;
		wsk_sink_t s;
		int i, outside = 0, inside = 0, y0, y1, ink0, ink1;
		uint32_t v;

		for (i = 0; i < 200; i++) {
			memset(&list[i], 0, sizeof list[i]);
			list[i].kind = WSK_BUTTON;
			list[i].size = 24;
			list[i].fill = (i % 2) ? 0x2a6b8fu : 0x3a7b4fu;
			list[i].ink = WSK_INHERIT;
			snprintf(list[i].id, WSK_ID_MAX, "i%03d", i);
			snprintf(list[i].text, WSK_TEXT_MAX, "Row %d", i);
		}
		wsk_init(&part[0], WSK_ROW, PW, PH);
		part[0].pad = 4; part[0].gap = 4;
		wsk_area(&part[0], 0, 0, PW, 40);
		part[0].item = TOOLBAR; part[0].n = 3;

		wsk_init(&part[1], WSK_COL, PW, PH);
		part[1].pad = 2; part[1].gap = 2;
		wsk_area(&part[1], 0, 40, PW, 100);
		part[1].item = list; part[1].n = 200;
		part[1].overflow = WSK_SCROLL;

		wsk_init(&part[2], WSK_COL, PW, PH);
		part[2].pad = 4; part[2].gap = 0;
		wsk_area(&part[2], 0, 140, PW, 32);
		part[2].item = STATUS; part[2].n = 1;

		wsk_doc_init(&c.d, &part[0]);
		c.part = part; c.n = 3;
		s.put = to_window; s.ctx = &dev; s.len = 0;

		v = 0;
		svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v);
		ok(wsk_compose(comp_body, &c, &s, 4096) > 0, "a pane between a toolbar and a status line");
		memcpy(full0, screen, sizeof screen);

		part[1].scroll = 12;                 /* row 0 half out at the top */
		v = 0;
		svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v);
		s.len = 0;
		ok(wsk_compose(comp_body, &c, &s, 4096) > 0, "and the same, scrolled by 12");
		memcpy(full1, screen, sizeof screen);
		shot("scroll_pane.ppm");

		for (int y = 0; y < PH; y++)
			for (int x = 0; x < PW; x++) {
				int diff = full0[y * PW + x] != full1[y * PW + x];
				if (y >= 40 && y < 140) inside += diff; else outside += diff;
			}
		ok(inside > 0, "CONTROL: the scroll changed pixels inside the pane");
		ok(outside == 0, "and not one pixel outside it, toolbar and status untouched");
		printf("    %d pixels changed inside the pane, %d outside\n", inside, outside);

		/* A cut row keeps its box and loses its text, at the pixels: the
		   label's ink is absent from row 0's visible strip and present
		   in row 1's. */
		{
			wsk_rect_t r0 = wsk_rect(&part[1], 0), r1 = wsk_rect(&part[1], 1);
			int lo, hi;
			/* Ink is whatever is not the row's own fill: RV-9 antialiases
			   text, so a label is a spread of blends and never pure white. */
			r0.h = (short)(r0.y + r0.h - 40); r0.y = 40;
			ink0 = r0.w * r0.h - span(r0, pix565(0x3a7b4fu), &lo, &hi);
			ink1 = r1.w * r1.h - span(r1, pix565(0x2a6b8fu), &lo, &hi);
			ok(span(r0, pix565(0x3a7b4fu), &lo, &hi) > 0,
			   "row 0's cut box is on the glass, in its own fill");
			ok(ink0 == 0, "with no ink in it -- its text is not drawn");
			ok(ink1 > 0, "CONTROL: row 1, whole, has its label's ink");
		}

		/* A scroll step is a banded repaint of the pane's rows, carrying
		   the status line wherever it shares a band -- and must come out
		   exactly as the full redraw did. */
		part[1].scroll = 0;
		v = 0;
		svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v);
		s.len = 0;
		wsk_compose(comp_body, &c, &s, 4096);
		part[1].scroll = 12;
		ok(wsk_area_rows(&part[1], &y0, &y1) == WSK_OK, "the pane's rows, band-snapped");
		c.d.y0 = (short)y0; c.d.y1 = (short)y1;
		v = ((uint32_t)y0 << 16) | ((uint32_t)y1 & 0xFFFFu);
		svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v);
		s.len = 0;
		blits = 0;
		{
			int n = wsk_compose(comp_body, &c, &s, 4096);
			printf("    a scroll step: rows %d..%d, %d bytes\n", y0, y1, n);
		}
		ok(memcmp(screen, full1, sizeof screen) == 0,
		   "a banded scroll step leaves exactly what a full redraw does");
		c.d.y0 = 0; c.d.y1 = PH;
	}

	/*
	 * Nesting, at the pixels (design.md §5f).
	 *
	 * A window that is one tree: a toolbar row, a body row holding a
	 * scrolling list beside a column of buttons, and a filled status row.
	 * The suite asserts the rectangles; what only pixels can say is that a
	 * scroll inside a NESTED pane stays inside it -- the column of buttons
	 * beside it shares every one of its rows -- and that repainting a branch
	 * or one widget two deep leaves exactly what a full redraw does.
	 */
	{
		static wsk_widget_t list[6];
		static uint16_t before[PW * PH], full[PW * PH];
		static const wsk_widget_t WIN[] = {
			{ .kind = WSK_STACK, .id = "tb",   .sub = 0, .size = 40 },
			{ .kind = WSK_STACK, .id = "body", .sub = 1, .weight = 1 },
			{ .kind = WSK_STACK, .id = "st",   .sub = 4, .size = 24, .fill = 0x203040u },
		};
		static const wsk_widget_t BODY[] = {
			{ .kind = WSK_STACK, .id = "left",  .sub = 2, .weight = 1 },
			{ .kind = WSK_STACK, .id = "right", .sub = 3, .weight = 1 },
		};
		static const wsk_widget_t RIGHT[] = {
			{ .kind = WSK_BUTTON, .id = "yes", .text = "Yes", .size = 40 },
			{ .kind = WSK_BUTTON, .id = "no",  .text = "No",  .size = 40 },
		};
		static const wsk_widget_t ST[] = {
			{ .kind = WSK_LABEL, .id = "msg", .text = "nested" },
		};
		wsk_widget_t tb[3];
		wsk_tree_t w, k[5];
		wsk_sink_t s;
		const char *ids[1];
		int i, y0, y1, outside = 0, inside = 0, lo, hi;
		uint32_t v;
		wsk_rect_t pane;

		for (i = 0; i < 6; i++) {
			memset(&list[i], 0, sizeof list[i]);
			list[i].kind = WSK_BUTTON;
			list[i].size = 24;
			list[i].fill = (i % 2) ? 0x2a6b8fu : 0x3a7b4fu;
			snprintf(list[i].id, WSK_ID_MAX, "L%d", i);
			snprintf(list[i].text, WSK_TEXT_MAX, "Item %d", i);
		}
		wsk_clone(tb, TOOLBAR, 3);
		wsk_init(&w, WSK_COL, PW, PH);
		w.pad = 0; w.gap = 0; w.bg = 0x10203au;
		w.item = WIN; w.n = 3; w.nest = k; w.nests = 5;
		wsk_nest_init(&k[0], &w, WSK_ROW); k[0].pad = 4; k[0].gap = 4; k[0].item = tb; k[0].n = 3;
		wsk_nest_init(&k[1], &w, WSK_ROW); k[1].pad = 4; k[1].gap = 4; k[1].item = BODY; k[1].n = 2;
		wsk_nest_init(&k[2], &w, WSK_COL); k[2].pad = 2; k[2].gap = 2; k[2].item = list; k[2].n = 6;
		k[2].overflow = WSK_SCROLL;
		wsk_nest_init(&k[3], &w, WSK_COL); k[3].pad = 0; k[3].gap = 4; k[3].item = RIGHT; k[3].n = 2;
		wsk_nest_init(&k[4], &w, WSK_ROW); k[4].pad = 2; k[4].item = ST; k[4].n = 1;

		printf("\n  nesting (§5f)\n");
		ok(wsk_check(&w, 0) == WSK_OK, "a window of five nested stacks is accepted");
		/* Where whisker put the pane, rather than where arithmetic in a
		   comment said it would be: a weighted child gets its natural width
		   plus its share, so the list, whose labels are longer, is wider
		   than the column beside it. */
		{
			wsk_tree_t at;
			int li = wsk__lookup(&w, "/body/left", &at, 0);
			pane = wsk_rect(&at, li);
			printf("    the nested pane: %d,%d %dx%d\n", pane.x, pane.y, pane.w, pane.h);
		}
		s.put = to_window; s.ctx = &dev; s.len = 0;
		v = 0;
		svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v);
		{
			int n = wsk_render(&w, &s, 4096);
			printf("    the whole window: %d bytes\n", n);
			ok(n > 0, "it renders");
		}
		memcpy(before, screen, sizeof screen);

		/* Scrolled 10, drawn whole: the reference. */
		k[2].scroll = 10;
		svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v);
		s.len = 0;
		wsk_render(&w, &s, 4096);
		memcpy(full, screen, sizeof screen);
		shot("nest_window.ppm");
		for (int y = 0; y < PH; y++)
			for (int x = 0; x < PW; x++) {
				int d = before[y * PW + x] != full[y * PW + x];
				if (x >= pane.x && x < pane.x + pane.w && y >= pane.y && y < pane.y + pane.h)
					inside += d;
				else
					outside += d;
			}
		ok(inside > 0, "CONTROL: scrolling the nested pane changed pixels inside it");
		ok(outside == 0, "and none outside it -- not the column of buttons sharing its rows");
		printf("    %d pixels changed inside the nested pane, %d outside\n", inside, outside);

		/* Its first row, cut by the pane two levels down: its fill is at the
		   pane's top edge, and none of its label's ink. */
		{
			wsk_rect_t top = { (short)(pane.x + 2), pane.y, (short)(pane.w - 4), 14 };
			int fill = span(top, pix565(0x3a7b4fu), &lo, &hi);
			ok(fill == top.w * top.h, "row 0's cut box fills the pane's top strip, in its own fill, and no ink");
		}

		/* The same scroll as a banded repaint of the branch, by path. */
		memcpy(screen, before, sizeof screen);
		k[2].scroll = 10;
		ids[0] = "/body/left";
		ok(wsk_rows(&w, ids, 1, &y0, &y1) == WSK_OK, "the branch's rows, by path");
		v = ((uint32_t)y0 << 16) | ((uint32_t)y1 & 0xFFFFu);
		svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v);
		s.len = 0;
		{
			int n = wsk_render_clipped(&w, ids, 1, &s, 4096);
			printf("    the branch repainted: rows %d..%d, %d bytes\n", y0, y1, n);
		}
		ok(memcmp(screen, full, sizeof screen) == 0,
		   "a banded repaint of a nested branch leaves exactly what a full redraw does");

		/* And one widget two deep, pressed. */
		tb[1].fill = 0xff8040u;
		v = 0;
		svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v);
		s.len = 0;
		wsk_render(&w, &s, 4096);
		memcpy(full, screen, sizeof screen);
		tb[1].fill = 0x8f2a2au;
		svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v);
		s.len = 0;
		wsk_render(&w, &s, 4096);
		tb[1].fill = 0xff8040u;
		ids[0] = "stop";
		wsk_rows(&w, ids, 1, &y0, &y1);
		v = ((uint32_t)y0 << 16) | ((uint32_t)y1 & 0xFFFFu);
		svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v);
		s.len = 0;
		wsk_render_clipped(&w, ids, 1, &s, 4096);
		ok(memcmp(screen, full, sizeof screen) == 0,
		   "a nested button repainted alone leaves exactly what a full redraw does");
		shot("nest_pressed.ppm");
		v = 0;
		svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v);
	}

	/*
	 * Wrap, at the pixels (design.md §5g): a text window by composition.
	 * One label per word in a wrapping row, inside a scrolling column, under
	 * a title -- no text widget, no special case, which is D18's "text does
	 * not need to be special". What only pixels can say: that it reads as a
	 * paragraph, which a person must look at, and that one word repainted
	 * alone leaves exactly what a full redraw does.
	 */
	{
		static const char *WORDS =
			"whisker lays out a paragraph the way it lays out a toolbar: "
			"each word is an item in a row that wraps, so a word can be "
			"tapped like a button and repainted alone, and nothing about "
			"text is special. A text window is that row inside a column "
			"that scrolls.";
		static wsk_widget_t word[64];
		static uint16_t full[PW * PH];
		static const wsk_widget_t WIN[] = {
			{ .kind = WSK_LABEL, .id = "title", .text = "Wrap", .size = 24 },
			{ .kind = WSK_STACK, .id = "page",  .sub = 0, .weight = 1 },
		};
		static const wsk_widget_t PAGE[] = {
			{ .kind = WSK_STACK, .id = "para", .sub = 1 },
		};
		wsk_tree_t w, k[2];
		wsk_sink_t s;
		const char *ids[1];
		int n = 0, y0, y1, tapped = -1;
		uint32_t v;

		for (const char *p = WORDS; *p != '\0' && n < 64; ) {
			int len = 0;
			while (*p == ' ') p++;
			if (*p == '\0') break;
			memset(&word[n], 0, sizeof word[n]);
			word[n].kind = WSK_LABEL;
			while (p[len] != '\0' && p[len] != ' ' && len < WSK_TEXT_MAX - 1) {
				word[n].text[len] = p[len];
				len++;
			}
			snprintf(word[n].id, WSK_ID_MAX, "w%d", n);
			if (strcmp(word[n].text, "tapped") == 0) tapped = n;
			p += len;
			n++;
		}

		wsk_init(&w, WSK_COL, PW, PH);
		w.pad = 0; w.gap = 0; w.bg = 0x10203au; w.ids = 1;
		w.item = WIN; w.n = 2; w.nest = k; w.nests = 2;
		wsk_nest_init(&k[0], &w, WSK_COL);
		k[0].pad = 6; k[0].gap = 0; k[0].item = PAGE; k[0].n = 1;
		k[0].overflow = WSK_SCROLL;
		wsk_nest_init(&k[1], &w, WSK_ROW);
		k[1].pad = 0; k[1].gap = 8; k[1].lgap = 0; k[1].item = word; k[1].n = n;
		k[1].overflow = WSK_WRAP;

		printf("\n  wrap (§5g)\n");
		printf("    %d words\n", n);
		ok(wsk_check(&w, 0) == WSK_OK, "a paragraph of word labels, wrapping, in a scrolling page");
		{
			wsk_tree_t at;
			int i = wsk__lookup(&w, "para", &at, 0);
			wsk_rect_t r = wsk_rect(&at, i);
			printf("    the paragraph: %dx%d in a page of %d rows\n", r.w, r.h, PH - 24);
			ok(r.h > PH - 24 - 12, "it is longer than its page, so the page scrolls");
		}
		k[0].scroll = 16;
		ok(wsk_check(&w, 0) == WSK_OK, "scrolled 16, accepted");

		s.put = to_window; s.ctx = &dev; s.len = 0;
		v = 0;
		svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v);
		word[tapped].ink = 0xffd040u;
		{
			int bytes = wsk_render(&w, &s, 4096);
			printf("    the whole window: %d bytes\n", bytes);
			ok(bytes > 0, "it renders under the budget");
		}
		memcpy(full, screen, sizeof screen);
		shot("wrap_text.ppm");

		/* One word, repainted alone, against the full redraw. */
		word[tapped].ink = WSK_INHERIT;
		svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v);
		s.len = 0;
		wsk_render(&w, &s, 4096);
		ok(memcmp(screen, full, sizeof screen) != 0, "CONTROL: without the colour, the screen differs");
		word[tapped].ink = 0xffd040u;
		ids[0] = word[tapped].id;
		ok(wsk_rows(&w, ids, 1, &y0, &y1) == WSK_OK, "the word's rows");
		v = ((uint32_t)y0 << 16) | ((uint32_t)y1 & 0xFFFFu);
		svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v);
		s.len = 0;
		{
			int bytes = wsk_render_clipped(&w, ids, 1, &s, 4096);
			printf("    one word repainted: rows %d..%d, %d bytes\n", y0, y1, bytes);
		}
		ok(memcmp(screen, full, sizeof screen) == 0,
		   "one word repainted alone leaves exactly what a full redraw does");

		/* §5h: the same paragraph as ONE text item, against the 51 labels.
		   Same characters at the same places, so the same pixels -- from
		   one widget and one string, in runs of a line rather than a
		   <text> a word. */
		{
			static const wsk_widget_t ONE[] = {
				{ .kind = WSK_TEXT, .id = "para", .sub = 0 },
			};
			const char *strs[1];
			int b1, b2;
			word[tapped].ink = WSK_INHERIT;
			v = 0;
			svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v);
			s.len = 0;
			b1 = wsk_render(&w, &s, 4096);
			memcpy(full, screen, sizeof screen);
			strs[0] = WORDS;
			w.str = strs; w.strs = 1;
			k[1].item = ONE; k[1].n = 1;
			ok(wsk_check(&w, 0) == WSK_OK, "the paragraph as one text item is accepted");
			svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v);
			s.len = 0;
			b2 = wsk_render(&w, &s, 4096);
			printf("    %d words as labels: %d bytes; as one text item: %d bytes\n", n, b1, b2);
			ok(memcmp(screen, full, sizeof screen) == 0,
			   "one text item draws exactly the pixels 51 word labels did");
			ok(b2 < b1, "in fewer bytes");
			shot("text_item.ppm");
		}
		v = 0;
		svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v);
	}

	printf(fails ? "\n%d pixel check(s) failed\n" : "\nall pixel checks passed\n", fails);
	return fails != 0;
}
