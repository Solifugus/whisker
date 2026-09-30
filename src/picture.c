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

	/* Press: the same tree, one fill changed, one clipped document. */
	ids[0] = "confirm";
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

	printf(fails ? "\n%d pixel check(s) failed\n" : "\nall pixel checks passed\n", fails);
	return fails != 0;
}
