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

	printf(fails ? "\n%d pixel check(s) failed\n" : "\nall pixel checks passed\n", fails);
	return fails != 0;
}
