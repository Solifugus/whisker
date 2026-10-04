/*
 * Of a tap, how much is whisker?
 *
 * design.md §2.2 is blunt that "no tap has ever been measured end to end", and
 * the roadmap files that under the P4. Most of it does not need the P4. The
 * chain is
 *
 *     sensor -> driver -> hit -> rows -> setstat -> render -> rasterise -> blit
 *
 * and only the first two links belong to a touch panel. Everything from "a
 * point arrives" to "pixels changed" runs here, today, with a synthetic point.
 *
 * WHAT THIS CAN AND CANNOT SAY. Absolute microseconds on a development machine
 * are not the C5's: the board is 240 MHz and this is not. What transfers is the
 * SHARE -- whisker's arithmetic and RV-9's rasteriser are both integer code
 * doing the same work on both machines, so the ratio between them is far more
 * portable than either number. So the question asked here is not "how fast is
 * a tap" but "**of the work a tap causes, how much is whisker's?**" -- which
 * decides whether whisker is worth optimising at all, and which no board figure
 * so far has broken out.
 *
 * The blit is excluded and that is deliberate: here it is a memcpy into an
 * array, on the board it is SPI DMA to the glass. Including it would import a
 * number that means nothing off the panel. What is measured is parse and
 * rasterise, which is the part the document's size and shape actually drive.
 */
#include "drv_svgwin.c"
#include "whisker.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef PW
#define PW 320
#endif
#ifndef PH
#define PH 172
#endif

static uint16_t screen[PW * PH];

rv9_io_err_t rv9_panel_open(bool l, int *w, int *h)
{ (void)l; if (w) *w = PW; if (h) *h = PH; return RV9_IO_OK; }
void rv9_panel_size(int *w, int *h) { if (w) *w = PW; if (h) *h = PH; }
void rv9_panel_backlight(uint32_t p) { (void)p; }
uint32_t rv9_panel_backlight_get(void) { return 100; }
bool rv9_panel_take(const void *o) { (void)o; return false; }
void rv9_panel_blit(int x0, int y0, int x1, int y1, const uint16_t *px)
{
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++)
			screen[y * PW + x] = px[(y - y0) * (x1 - x0) + (x - x0)];
}

static void to_window(wsk_sink_t *s, const char *b, int n)
{ size_t done; svgwin_write((rv9_dev_t *)s->ctx, b, (size_t)n, &done); }

static double now_us(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

static const wsk_widget_t PANEL[] = {
	{ .kind = WSK_LABEL,  .id = "title",  .text = "Settings" },
	{ .kind = WSK_BUTTON, .id = "ok",     .text = "OK" },
	{ .kind = WSK_BUTTON, .id = "cancel", .text = "Cancel" },
};

#define REPS 2000

int main(void)
{
	rv9_dev_t dev;
	wsk_widget_t live[3];
	wsk_tree_t t;
	wsk_sink_t s;
	const char *ids[1];
	wsk_rect_t r;
	double t0, hit_us, rows_us, emit_us, raster_us;
	int i, y0 = 0, y1 = 0, bytes = 0;
	/* Volatile sinks: with -O2 and a result nobody reads, the compiler is
	   entitled to delete the whole loop, and the first run of this reported
	   hit at 0.00 us for exactly that reason -- a timing of nothing. */
	static volatile int sink_which, sink_rows, sink_bytes;

	memset(&dev, 0, sizeof dev);
	dev.opt[OPT_BG] = 0x0000;
	svgwin_init(&dev);
	svgwin_open(&dev, 3);

	wsk_clone(live, PANEL, 3);
	wsk_init(&t, WSK_COL, PW, PH);
	t.pad = 4; t.gap = 4; t.bg = 0x10203au; t.ids = 1;
	t.item = live; t.n = 3;

	s.put = to_window; s.ctx = &dev; s.len = 0;
	wsk_render(&t, &s, 4096);                     /* the scene is already up */

	r = wsk_rect(&t, 1);
	ids[0] = "ok";

	/* 1. a point becomes a name */
	t0 = now_us();
	for (i = 0; i < REPS; i++) sink_which = wsk_hit(&t, r.x + r.w / 2, r.y + r.h / 2);
	hit_us = (now_us() - t0) / REPS;

	/* 2. a name becomes a row range */
	t0 = now_us();
	for (i = 0; i < REPS; i++) { wsk_rows(&t, ids, 1, &y0, &y1); sink_rows = y0 + y1; }
	rows_us = (now_us() - t0) / REPS;

	/* 3. the document is produced -- counted, not written, so this is
	      whisker's emitter alone with no window underneath it */
	t0 = now_us();
	for (i = 0; i < REPS; i++) {
		wsk_sink_t dry;
		dry.put = 0; dry.ctx = 0; dry.len = 0;
		wsk_render_clipped(&t, ids, 1, &dry, 4096);
		bytes = dry.len;
		sink_bytes = dry.len;
	}
	emit_us = (now_us() - t0) / REPS;

	/* 4. and the window turns it into pixels */
	{
		uint32_t v = ((uint32_t)y0 << 16) | (uint32_t)y1;
		t0 = now_us();
		for (i = 0; i < REPS; i++) {
			svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v);
			s.put = to_window; s.ctx = &dev; s.len = 0;
			wsk_render_clipped(&t, ids, 1, &s, 4096);
		}
		raster_us = (now_us() - t0) / REPS - emit_us;
	}

	{
		double total = hit_us + rows_us + emit_us + raster_us;
		(void)sink_which; (void)sink_rows; (void)sink_bytes;
		double wsk = hit_us + rows_us + emit_us;

		printf("a press at %dx%d: %d bytes over rows %d..%d (%d bands)\n\n",
		       PW, PH, bytes, y0, y1, (y1 - y0) / WSK_BAND);
		printf("  %-34s %9s %7s\n", "stage", "us", "share");
		printf("  %-34s %9.2f %6.1f%%\n", "hit   -- a point becomes a name",
		       hit_us, hit_us * 100 / total);
		printf("  %-34s %9.2f %6.1f%%\n", "rows  -- a name becomes a band range",
		       rows_us, rows_us * 100 / total);
		printf("  %-34s %9.2f %6.1f%%\n", "emit  -- whisker writes the document",
		       emit_us, emit_us * 100 / total);
		printf("  %-34s %9.2f %6.1f%%\n", "parse + rasterise -- the window",
		       raster_us, raster_us * 100 / total);
		printf("  %-34s %9.2f\n", "", total);
		printf("\n  whisker is %.1f%% of it; the window is %.1f%%.\n",
		       wsk * 100 / total, raster_us * 100 / total);
		printf("\n  Absolute microseconds here are not the board's -- this is not\n");
		printf("  a 240 MHz core. The SHARE is what transfers, and it says where\n");
		printf("  effort would and would not pay.\n");
		printf("\n  (hit is charged once per press. Note it is also the only stage\n");
		printf("   that does NOT scale with the document: it walks %d widgets.)\n", t.n);
	}
	return 0;
}
