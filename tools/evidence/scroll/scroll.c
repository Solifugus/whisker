/*
 * What does a scroll cost against the band model?
 *
 * `studio-gap.md` §5 says this is the first of the four Studio capabilities to
 * price, because the other three need it and because it is the one that
 * interacts with the band model rather than sitting on top of it. This is that
 * pricing, and it needs no board: bytes come from the emitter and bands come
 * from RV-9's own rasteriser, built natively.
 *
 * NOTHING IN WHISKER CHANGES TO RUN THIS. A scroll is already expressible:
 * put the stack's area at y = -offset with its full content height, and clip
 * the document to the viewport's rows. `wsk_part` skips any widget that does
 * not intersect those rows, so the items scrolled off either end simply are
 * not emitted. That is worth knowing before anything is designed -- the
 * question is not "can whisker scroll" but "what does it cost when it does".
 *
 * Three things are measured, and the third is the one that decides it:
 *
 *   1. Do the bytes track the VIEWPORT or the CONTENT? A list of 200 items in
 *      a 10-item viewport must not emit 200 items, or SRC_MAX ends it.
 *   2. What does one scroll step DAMAGE? A value changing in place dirties one
 *      widget's bands. A scroll moves everything, so the expectation is the
 *      whole viewport -- but by how much, and does step size matter?
 *   3. What is that in TIME, against the 19 ms a finger waits (§2.2)?
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
static uint16_t before[PW * PH];
static int blits;

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

static void to_window(wsk_sink_t *s, const char *b, int n)
{
	size_t done;
	svgwin_write((rv9_dev_t *)s->ctx, b, (size_t)n, &done);
}

/* How many 8-row bands differ between the last two frames? That is the
   damage a real repaint would have to carry, counted at the pixels rather
   than reasoned about from the geometry. */
static int bands_changed(void)
{
	int n = 0;
	for (int b = 0; b * WSK_BAND < PH; b++) {
		int y0 = b * WSK_BAND, y1 = y0 + WSK_BAND, hit = 0;
		if (y1 > PH) y1 = PH;
		for (int y = y0; y < y1 && !hit; y++)
			for (int x = 0; x < PW; x++)
				if (screen[y * PW + x] != before[y * PW + x]) { hit = 1; break; }
		n += hit;
	}
	return n;
}

#define ITEMS 200
static wsk_widget_t LIST[ITEMS];
static char TEXT[ITEMS][WSK_TEXT_MAX];

static void build_list(wsk_tree_t *t, int n, int row_h, int view_y, int view_h,
                       int offset)
{
	for (int i = 0; i < n; i++) {
		int v = i, d = 0;
		memset(&LIST[i], 0, sizeof LIST[i]);
		LIST[i].kind = WSK_LABEL;
		LIST[i].size = (short)row_h;
		LIST[i].fill = WSK_INHERIT;
		LIST[i].ink = WSK_INHERIT;
		LIST[i].id[0] = 'i';
		LIST[i].id[1] = (char)('0' + (i / 100) % 10);
		LIST[i].id[2] = (char)('0' + (i / 10) % 10);
		LIST[i].id[3] = (char)('0' + i % 10);
		LIST[i].id[4] = '\0';
		TEXT[i][d++] = 'R';
		TEXT[i][d++] = 'o';
		TEXT[i][d++] = 'w';
		TEXT[i][d++] = ' ';
		if (v >= 100) TEXT[i][d++] = (char)('0' + v / 100);
		if (v >= 10)  TEXT[i][d++] = (char)('0' + (v / 10) % 10);
		TEXT[i][d++] = (char)('0' + v % 10);
		TEXT[i][d] = '\0';
		for (int k = 0; k <= d; k++) LIST[i].text[k] = TEXT[i][k];
	}
	wsk_init(t, WSK_COL, PW, PH);
	t->pad = 2; t->gap = 2; t->font = 16; t->ids = 1;
	/* the content box, taller than the viewport and lifted by the offset */
	wsk_area(t, 0, view_y - offset, PW, n * (row_h + 2) + 4);
	t->item = LIST; t->n = (short)n;
	(void)view_h;
}

/*
 * Emit the viewport's rows -- and TELL THE DRIVER so.
 *
 * Those are two different things and the first version of this set only the
 * first: `d.y0/d.y1` is what whisker emits, `RV9_SVG_SS_ROWS` is what the
 * window paints. A clipped document written without the setstat is painted
 * across the whole screen, so everything outside it goes to the device
 * background -- which is exactly the defect design.md §2.2 records `flick`
 * shipping for months, reproduced here by my own harness inside an hour.
 */
static int frame(wsk_tree_t *t, rv9_dev_t *dev, int view_y, int view_h, int real)
{
	wsk_doc_t d;
	wsk_sink_t s;

	wsk_doc_init(&d, t);
	d.y0 = (short)view_y;
	d.y1 = (short)(view_y + view_h);
	if (real) {
		uint32_t v = ((uint32_t)d.y0 << 16) | ((uint32_t)d.y1 & 0xFFFFu);
		svgwin_setstat(dev, RV9_SVG_SS_ROWS, &v);
	}
	s.put = real ? to_window : 0;
	s.ctx = dev;
	s.len = 0;
	wsk_open(&d, &s);
	wsk_part(&d, t, &s);
	wsk_shut(&s);
	return s.len;
}

int main(void)
{
	rv9_dev_t dev;
	wsk_tree_t t;
	const int ROW = 24, VY = 0, VH = PH;
	const int visible = VH / (ROW + 2);

	memset(&dev, 0, sizeof dev);
	dev.opt[OPT_BG] = 0x0000;
	svgwin_init(&dev);
	svgwin_open(&dev, 3);

	printf("a scrolling list at %dx%d: %d-row items, a %d-row viewport\n",
	       PW, PH, ROW, VH);
	printf("  about %d items visible of %d\n\n", visible, ITEMS);

	/* 1. bytes track the viewport, not the content */
	printf("  1. do the bytes track the viewport or the content?\n");
	printf("     %-10s %-8s %s\n", "items", "bytes", "");
	for (int n = 10; n <= ITEMS; n *= 2) {
		if (n > ITEMS) n = ITEMS;
		build_list(&t, n, ROW, VY, VH, 0);
		printf("     %-10d %-8d %s\n", n, frame(&t, &dev, VY, VH, 0),
		       n > visible ? "longer than the viewport" : "");
	}
	printf("     (so a list is priced by what is on screen, not by what it holds:\n");
	printf("      wsk_part skips every widget that misses the document's rows)\n\n");

	/* 2. what one scroll step damages, at the pixels */
	printf("  2. what does one scroll step damage?\n");
	printf("     %-18s %-8s %-7s %s\n", "step", "bytes", "bands", "of");
	{
		static const int STEP[] = { 1, 8, 26, 104 };
		static const char *WHAT[] = { "1 px", "8 px (one band)",
		                              "26 px (one item)", "104 px (four items)" };
		int total = (PH + WSK_BAND - 1) / WSK_BAND;

		for (int k = 0; k < 4; k++) {
			build_list(&t, ITEMS, ROW, VY, VH, 0);
			blits = 0; frame(&t, &dev, VY, VH, 1);
			memcpy(before, screen, sizeof before);
			build_list(&t, ITEMS, ROW, VY, VH, STEP[k]);
			blits = 0;
			int bytes = frame(&t, &dev, VY, VH, 1);
			printf("     %-18s %-8d %-7d %d\n", WHAT[k], bytes,
			       bands_changed(), total);
		}
		printf("\n     and for comparison, what wstat does -- one value changing:\n");
		build_list(&t, ITEMS, ROW, VY, VH, 0);
		frame(&t, &dev, VY, VH, 1);
		memcpy(before, screen, sizeof before);
		LIST[2].text[4] = 'X';
		{
			const char *ids[1];
			wsk_doc_t d;
			wsk_sink_t s;
			int y0, y1;
			ids[0] = LIST[2].id;
			wsk_doc_init(&d, &t);
			wsk_rows(&t, ids, 1, &y0, &y1);
			d.y0 = (short)y0; d.y1 = (short)y1;
			{
				uint32_t v = ((uint32_t)y0 << 16) | ((uint32_t)y1 & 0xFFFFu);
				svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v);
			}
			s.put = to_window; s.ctx = &dev; s.len = 0;
			wsk_open(&d, &s); wsk_part(&d, &t, &s); wsk_shut(&s);
			printf("     %-18s %-8d %-7d %d\n", "one row's text", s.len,
			       bands_changed(), total);
		}
		shot("scroll.ppm");
	}

	/*
	 * 3. What that is in time.
	 *
	 * Derived, not measured here: the per-band cost comes from RV-9's own
	 * board figures in design.md SS2.2 -- flick's full frame is 57 ms over 22
	 * bands and its press 19 ms over 8, which agree at about 2.5 ms a band
	 * at 320x172. At 1024x600 the P4's own measurement is 175 ms over 75
	 * bands, which is ~2.3 -- NOT the ~8.5 this programme used until
	 * 2026-10-11, when a real panel replaced an extrapolation that had been
	 * scaled for width, board and clock and had compounded its pessimism at
	 * every step. Marked derived because this programme still has no board.
	 */
	{
		const int MS10 = (PW <= 320) ? 25 : 23;      /* tenths of a ms a band */
		int total = (PH + WSK_BAND - 1) / WSK_BAND;

		printf("\n  3. and in time, at %d.%d ms a band (derived, SS2.2)\n",
		       MS10 / 10, MS10 % 10);
		printf("     %-26s %-9s %s\n", "", "bands", "ms");
		printf("     %-26s %-9d %d\n", "one value in place", 2, 2 * MS10 / 10);
		printf("     %-26s %-9d %d\n", "one scroll step", total * 2 / 3,
		       total * 2 / 3 * MS10 / 10);
		printf("     %-26s %-9d %d\n", "the whole panel", total,
		       total * MS10 / 10);
		{
			int ms10 = total * 2 / 3 * MS10;        /* tenths of a ms */
			printf("\n     against the 19 ms a finger waits for a press (SS2.2):\n");
			printf("     a scroll step is %d.%dx that budget.\n",
			       ms10 / 190, (ms10 * 10 / 190) % 10);
		}
		{
			/* How tall a scrolling region may be and still make a frame
			   budget. This used to read "never as an animation", which
			   was true of an extrapolated 8.5 ms a band and is not true
			   of the measured 2.3. */
			int b30 = 333 / MS10, b60 = 166 / MS10;

			printf("\n     A full-screen scroll step is %d ms, so scrolling the\n",
			       total * 2 / 3 * MS10 / 10);
			printf("     WHOLE screen stays a discrete jump: one item or one page\n");
			printf("     per input event, not an animation.\n");
			printf("\n     But a PANE is a different question, and the answer\n");
			printf("     changed when the P4 was measured rather than estimated:\n");
			printf("       up to %2d bands (%3d rows) fits a 30 fps frame\n", b30, b30 * WSK_BAND);
			printf("       up to %2d bands (%3d rows) fits a 60 fps frame\n", b60, b60 * WSK_BAND);
			printf("     and that is nearly the same on both boards, because the\n");
			printf("     per-band cost barely differs -- 2.5 ms against 2.3 -- so\n");
			printf("     what decides it is how TALL the scrolling region is, not\n");
			printf("     which panel it is on.\n");
			printf("\n     IMPLIED, NOT DEMONSTRATED: nobody has dragged anything\n");
			printf("     on either panel and timed it. RV-9 reached the same place\n");
			printf("     from the other side on 2026-10-11 and put it the right\n");
			printf("     way -- measure before assuming a drag is too expensive,\n");
			printf("     rather than prohibiting it.\n");
		}
	}

	/*
	 * 4. And the cost is proportional to the viewport, which is the one lever
	 *    a layout has: a scrolling PANE is cheaper than a scrolling screen.
	 */
	{
		const int MS10 = (PW <= 320) ? 25 : 23;
		printf("\n  4. a scrolling pane rather than a scrolling screen\n");
		printf("     %-16s %-8s %-7s %s\n", "viewport", "bytes", "bands", "ms");
		for (int vh = PH; vh >= PH / 4; vh /= 2) {
			int b;
			build_list(&t, ITEMS, ROW, VY, vh, 0);
			frame(&t, &dev, VY, vh, 1);
			memcpy(before, screen, sizeof before);
			build_list(&t, ITEMS, ROW, VY, vh, 26);
			int bytes = frame(&t, &dev, VY, vh, 1);
			b = bands_changed();
			printf("     %-16d %-8d %-7d %d\n", vh, bytes, b, b * MS10 / 10);
		}
		printf("     (so keep a scrolling region small -- it is the only lever)\n");
	}
	return 0;
}
