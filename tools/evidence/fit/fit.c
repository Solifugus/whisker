/*
 * What do the lower rungs of the overflow ladder buy?
 *
 * `scale-sketch.md` §4 proposes that when content does not fit, whisker should
 * COMPRESS first (padding and gaps, which cost no legibility), then SCALE down
 * to a measured floor, and only then scroll, rotate or refuse. `scroll/` priced
 * the last rung. This prices the first two, and asks the question that decides
 * whether the ladder is worth having at all:
 *
 *     how much more content fits after compressing and scaling,
 *     before you are forced to scroll anyway?
 *
 * If the answer is "half as much again" the ladder earns its complexity. If it
 * is "fifteen percent" then scrolling is the real answer and the first two
 * rungs are decoration.
 *
 * THE FLOOR IS THE HARD PART, and it is why section 1 draws a picture. RV-9's
 * font is a 10x20 bitmap with 4-bit alpha, supersampled 3x3 into whatever
 * font-size asks for -- so shrinking text BLURS it rather than breaking the
 * strokes, and there is no size at which it obviously fails. A number cannot
 * find that floor. A person looking at a panel can.
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
{ size_t done; svgwin_write((rv9_dev_t *)s->ctx, b, (size_t)n, &done); }

/* The green channel of the brightest pixel in a band of rows, 0..63.
   Text drawn in near-white on a dark scene: if the peak never reaches the
   ink's own brightness, every stroke is being averaged with its background
   and the glyph is a smudge rather than a letter. */
static int peak_g(int y0, int y1)
{
	int best = 0;
	for (int y = y0; y < y1 && y < PH; y++)
		for (int x = 0; x < PW; x++) {
			uint16_t v = (uint16_t)((screen[y * PW + x] >> 8) | (screen[y * PW + x] << 8));
			int g = (v >> 5) & 0x3F;
			if (g > best) best = g;
		}
	return best;
}

static int inked(int y0, int y1, int floor_g)
{
	int n = 0;
	for (int y = y0; y < y1 && y < PH; y++)
		for (int x = 0; x < PW; x++) {
			uint16_t v = (uint16_t)((screen[y * PW + x] >> 8) | (screen[y * PW + x] << 8));
			if (((v >> 5) & 0x3F) > floor_g) n++;
		}
	return n;
}

static wsk_widget_t ROW[40];

static void mkrow(int i, int size)
{
	static const char *T = "Temp 45.3 C";
	int k = 0;
	memset(&ROW[i], 0, sizeof ROW[i]);
	ROW[i].kind = WSK_LABEL;
	ROW[i].size = (short)size;
	ROW[i].fill = WSK_INHERIT;
	ROW[i].ink = WSK_INHERIT;
	ROW[i].id[0] = 'r'; ROW[i].id[1] = (char)('0' + i / 10);
	ROW[i].id[2] = (char)('0' + i % 10); ROW[i].id[3] = '\0';
	while (T[k] != '\0') { ROW[i].text[k] = T[k]; k++; }
	ROW[i].text[k] = '\0';
}

/* How many rows of height `h` fit, with this padding and gap? */
static int fits(int h, int pad, int gap)
{
	int span = PH - 2 * pad;
	int n = (span + gap) / (h + gap);
	return n < 0 ? 0 : n;
}

int main(void)
{
	rv9_dev_t dev;
	wsk_tree_t t;
	wsk_sink_t s;
	int i;

	memset(&dev, 0, sizeof dev);
	dev.opt[OPT_BG] = 0x0000;
	svgwin_init(&dev);
	svgwin_open(&dev, 3);

	printf("the overflow ladder at %dx%d\n\n", PW, PH);

	/* ---- 1. where the legibility floor is, drawn to be looked at ---- */
	{
		static const int SIZE[] = { 24, 20, 16, 14, 12, 10, 8, 6 };
		int n = (int)(sizeof SIZE / sizeof SIZE[0]);
		int y = 2, k;

		printf("  1. the same text at descending font sizes -- LOOK AT fit_font.ppm\n");
		printf("     %-6s %-8s %-8s %s\n", "font", "peak", "ink px", "");
		for (k = 0; k < n && y + SIZE[k] + 2 < PH; k++) {
			wsk_widget_t one[1];
			memset(one, 0, sizeof one);
			one[0].kind = WSK_LABEL;
			one[0].size = (short)SIZE[k];
			one[0].fill = WSK_INHERIT; one[0].ink = WSK_INHERIT;
			{ const char *T = "Temp 45.3 C"; int j = 0;
			  while (T[j] != '\0') { one[0].text[j] = T[j]; j++; } one[0].text[j] = '\0'; }
			wsk_init(&t, WSK_COL, PW, PH);
			t.pad = 0; t.gap = 0; t.font = (short)SIZE[k];
			wsk_area(&t, 0, y, PW, SIZE[k]);
			t.item = one; t.n = 1;
			{
				uint32_t v = 0;
				svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v);
			}
			s.put = to_window; s.ctx = &dev; s.len = 0;
			if (k == 0) wsk_render(&t, &s, 4096);
			else {
				/* keep the earlier lines: draw into the same scene */
				wsk_doc_t d;
				wsk_doc_init(&d, &t);
				d.y0 = (short)y; d.y1 = (short)(y + SIZE[k]);
				{ uint32_t v = ((uint32_t)d.y0 << 16) | (uint32_t)d.y1;
				  svgwin_setstat(&dev, RV9_SVG_SS_ROWS, &v); }
				s.len = 0;
				wsk_open(&d, &s); wsk_part(&d, &t, &s); wsk_shut(&s);
			}
			printf("     %-6d %-8d %-8d %s\n", SIZE[k], peak_g(y, y + SIZE[k]),
			       inked(y, y + SIZE[k], 20),
			       SIZE[k] >= 16 ? "" : "shrinking");
			y += SIZE[k] + 2;
		}
		shot("fit_font.ppm");
		printf("\n     The peak is the brightest pixel the ink reached, of 63. It\n");
		printf("     holds at 59-63 down to font 10 and falls to 45 at 8 -- so\n");
		printf("     there is a cliff between 10 and 8 where strokes stop\n");
		printf("     reaching full intensity and start averaging into the\n");
		printf("     background.\n");
		printf("\n     THAT IS NOT THE FLOOR, and this programme does not set one.\n");
		printf("     Legibility is physical: it depends on the panel's pitch and\n");
		printf("     how far away the reader is, and a PPM looked at on a\n");
		printf("     development monitor is at whatever magnification that\n");
		printf("     monitor happens to give. finger_size/ had exactly this\n");
		printf("     problem and solved it by drawing at matched PHYSICAL size\n");
		printf("     on a screen you do have. The font floor wants the same\n");
		printf("     treatment and a person, and until then 10 is a signal\n");
		printf("     rather than an answer.\n\n");
	}

	/* ---- 2. what the ladder buys, against the MEASURED floors ---- */
	{
		/*
		 * The floors are Matthew's, read off the real C5 panel on
		 * 2026-10-04 with `wfont > /w0`: readable down to font 8,
		 * comfortable from about 16. Two numbers, not one -- the same
		 * shape finger_size/ found for targets, where a thing you CAN
		 * hit and a thing that is pleasant to hit are different sizes.
		 *
		 * They are quoted here as font numbers because that is how they
		 * were read, but they are PHYSICAL: font 16 on the C5 is 1.64 mm
		 * of glass. See section 3 for why that matters and why the P4
		 * must not inherit the number.
		 */
		const int COMFORT = 16, READABLE = 8;
		const int ROWH = 32, PAD = 8, GAP = 8;
		int a  = fits(ROWH, PAD, GAP);
		int b  = fits(ROWH, 0, 0);
		int cc = fits(COMFORT + 8, 0, 0);
		int cr = fits(READABLE + 4, 0, 0);
		int touch_px = (PW <= 320) ? 88 : 60;   /* 9 mm, from finger_size/ */
		int tfit = fits(touch_px, PAD, GAP);

		printf("  2. a readout of %d-row items: what each rung buys\n", ROWH);
		printf("     %-36s %-6s %s\n", "rung", "fit", "");
		printf("     %-36s %-6d %s\n", "as laid out (pad 8, gap 8)", a, "");
		printf("     %-36s %-6d %s\n", "1. compressed (pad 0, gap 0)", b,
		       b > a ? "+1" : "no gain");
		printf("     %-36s %-6d %+d%%\n", "2. scaled to COMFORT (font 16)", cc,
		       a > 0 ? (cc - a) * 100 / a : 0);
		printf("     %-36s %-6d %+d%%\n", "2. scaled to READABLE (font 8)", cr,
		       a > 0 ? (cr - a) * 100 / a : 0);
		printf("\n     So the ladder is worth +%d%% if comfort is respected, and\n",
		       a > 0 ? (cc - a) * 100 / a : 0);
		printf("     +%d%% if it is spent down to the hard floor. The second\n",
		       a > 0 ? (cr - a) * 100 / a : 0);
		printf("     number is available in an emergency and is not a design.\n");

		printf("\n     And for a TOUCH surface the floor is different again: 9 mm\n");
		printf("     is %d px here, so a touchable list holds %d rows and\n",
		       touch_px, tfit);
		printf("     compressing cannot move it -- the floor is the finger.\n");
	}

	/* ---- 3. the floor is physical, so it transfers as millimetres ---- */
	{
		/* px per mm, from the panel's own geometry. Integer arithmetic in
		   tenths, because this is the shape a module would have to use. */
		int pitch10 = (PW <= 320) ? 97 : 67;    /* px per mm, x10 */
		int comfort_um = 16 * 10000 / 97;       /* font 16 on the C5, in um */

		printf("\n  3. the floor is PHYSICAL and does not transfer as a font size\n");
		printf("     C5 font 16 is %d.%02d mm of glass, at %d.%d px/mm.\n",
		       comfort_um / 1000, (comfort_um % 1000) / 10, 97 / 10, 97 % 10);
		printf("     The same %d.%02d mm on THIS panel (%d.%d px/mm) is font %d.\n",
		       comfort_um / 1000, (comfort_um % 1000) / 10,
		       pitch10 / 10, pitch10 % 10, (comfort_um * pitch10 + 5000) / 10000);
		printf("\n     So a panel with coarser pixels needs a SMALLER font number\n");
		printf("     for the same readable size, which is the opposite of the\n");
		printf("     intuition and is exactly why RV9_GS_PHYSICAL matters\n");
		printf("     (scale-sketch.md SS7). The C5's comfort floor must not be\n");
		printf("     copied to the P4 as a number -- only as millimetres, and\n");
		printf("     even then the viewing distance is a different question.\n");
	}

	return 0;
}
