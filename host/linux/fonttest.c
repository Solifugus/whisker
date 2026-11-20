/*
 * The desktop's font beyond ASCII (design.md §5at), checked at the pixels.
 *
 * RV-9's own driver, built as the desktop host builds it: with
 * -DRV9_FONT_WIDE and font_wide.c it reads <text> as UTF-8 and draws past
 * ASCII; without, it is the board's. Built both ways by run.sh, so the
 * checks below that need the wide font fail on the board's build -- their
 * control -- and the ASCII picture is compared across the two, byte for byte.
 *
 *     fonttest out.ppm         draws, checks, writes the picture
 *     fonttest out.ppm ascii   draws ASCII only, for comparing the builds
 */
#include "drv_svgwin.c"

#include <stdio.h>
#include <string.h>

#define PW 360
#define PH 120

static uint16_t screen[PW * PH];
static int fails;

rv9_io_err_t rv9_panel_open(bool l, int *w, int *h)
{ (void)l; if (w) *w = PW; if (h) *h = PH; return RV9_IO_OK; }
void rv9_panel_size(int *w, int *h) { if (w) *w = PW; if (h) *h = PH; }
void rv9_panel_backlight(uint32_t p) { (void)p; }
uint32_t rv9_panel_backlight_get(void) { return 100; }
bool rv9_panel_take(const void *o) { (void)o; return false; }
bool rv9_panel_swaps_bytes(void) { return false; }
void rv9_panel_blit(int x0, int y0, int x1, int y1, const uint16_t *px)
{
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++)
			screen[y * PW + x] = px[(y - y0) * (x1 - x0) + (x - x0)];
}

static void ok(int cond, const char *what)
{
	printf("%s  %s\n", cond ? "ok  " : "FAIL", what);
	if (!cond) fails++;
}

static rv9_dev_t dev;

static void draw(const char *doc)
{
	size_t done;
	svgwin_write(&dev, doc, strlen(doc), &done);
}

/* Ink in a cell: pixels not the background (black). */
static int ink(int x, int y, int w, int h)
{
	int n = 0;
	for (int j = y; j < y + h; j++)
		for (int i = x; i < x + w; i++)
			if (screen[j * PW + i]) n++;
	return n;
}

/* The pixels of two cells the same? */
static int same(int x0, int x1, int y, int w, int h)
{
	for (int j = y; j < y + h; j++)
		for (int i = 0; i < w; i++)
			if (screen[j * PW + x0 + i] != screen[j * PW + x1 + i]) return 0;
	return 1;
}

/* Two cells anywhere the same? */
static int cells(int x0, int y0, int x1, int y1, int w, int h)
{
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++)
			if (screen[(y0 + j) * PW + x0 + i] != screen[(y1 + j) * PW + x1 + i]) return 0;
	return 1;
}

/* Text at 20 user units is the font's own cell: 10 wide, 20 tall, its top
   at y - 15. Row k's baseline is 20 + 24k, so cell (c, k) is at x = 10c,
   y = 5 + 24k. */
#define CX(c) (10 * (c))
#define CY(k) (5 + 24 * (k))

int main(int argc, char **argv)
{
	memset(&dev, 0, sizeof dev);
	dev.opt[OPT_BG] = 0x0000;
	svgwin_init(&dev);
	svgwin_open(&dev, 3);

	/* fonttest out.ppm ascii: every ASCII character and the three entities,
	   nothing else -- the picture both builds must draw byte for byte */
	if (argc > 2) {
		char doc[1024], *d = doc;
		d += sprintf(d, "<svg viewBox=\"0 0 360 120\"><rect width=\"360\" height=\"120\" fill=\"#000\"/>"
		             "<g fill=\"#fff\" font-size=\"20\">");
		for (int r = 0; r < 4; r++) {
			d += sprintf(d, "<text x=\"0\" y=\"%d\"%s>", 20 + 24 * r, r == 3 ? " font-size=\"13\"" : "");
			for (int c = 33 + 24 * r; c < 33 + 24 * r + 24 && c < 127; c++)
				d += c == '<' ? sprintf(d, "&lt;") : c == '>' ? sprintf(d, "&gt;")
				   : c == '&' ? sprintf(d, "&amp;") : sprintf(d, "%c", c);
			d += sprintf(d, "</text>");
		}
		sprintf(d, "</g></svg>");
		draw(doc);
	} else
	draw("<svg viewBox=\"0 0 360 120\"><rect width=\"360\" height=\"120\" fill=\"#000\"/>"
	     "<g fill=\"#fff\" font-size=\"20\">"
	     "<text x=\"0\" y=\"20\">cafe ASCII &lt;ok&gt;</text>"
	     "<text x=\"0\" y=\"44\">caf\xc3\xa9 \xe2\x96\xb8\xce\xa9\xd0\x96 \xe2\x94\x80\xe2\x94\x80</text>"
	     "<text x=\"0\" y=\"68\">\xe6\x97\xa5|\xff|\xc3|e</text>"
	     "<text x=\"180\" y=\"92\" text-anchor=\"middle\">\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9</text>"
	     "</g></svg>");

#ifdef RV9_FONT_UTF8
	const int wide = 1;
#else
	const int wide = 0;
#endif
	printf("     built %s the wide font\n", wide ? "with" : "without");

	if (argc > 2) goto write;

	/* row 0: ASCII, the control -- it must draw whatever the build */
	ok(ink(CX(0), CY(0), 10, 20) > 10 && ink(CX(3), CY(0), 10, 20) > 10,
	   "ASCII draws (the control: in either build)");

	/* row 1: c a f é ␠ ▸ Ω Ж ␠ ─ ─ -- one cell each when the font is wide */
	ok(ink(CX(3), CY(1), 10, 8) > ink(CX(3), CY(0), 10, 8) + 3
	   && cells(CX(3), CY(1) + 8, CX(3), CY(0) + 8, 10, 12),
	   "\xc3\xa9 is an e with an accent above it: e's pixels below, ink above where e has none");
	ok(ink(CX(4), CY(1), 10, 20) == 0, "the space after it is the fifth cell: \xc3\xa9 took one");
	ok(ink(CX(5), CY(1), 10, 20) > 10 && ink(CX(6), CY(1), 10, 20) > 10
	   && ink(CX(7), CY(1), 10, 20) > 10, "\xe2\x96\xb8, \xce\xa9, \xd0\x96 each draw, one cell each");
	ok(same(CX(9), CX(10), CY(1), 10, 20) && screen[(CY(1) + 10) * PW + CX(10) - 1]
	   && screen[(CY(1) + 10) * PW + CX(10)],
	   "\xe2\x94\x80\xe2\x94\x80: two identical cells, the line joined across them");

	/* row 2: 日 (not in the font) | a stray byte | a cut sequence | e */
	int box = 1;
	for (int i = CX(0) + 2; i < CX(0) + 8; i++)
		box &= screen[(CY(2) + 4) * PW + i] != 0 && screen[(CY(2) + 10) * PW + i] == 0;
	ok(box, "\xe6\x97\xa5, which the font lacks: a box, hollow, in one cell");
	ok(same(CX(2), CX(4), CY(2), 10, 20) && !same(CX(0), CX(2), CY(2), 10, 20)
	   && ink(CX(2), CY(2), 10, 20) > 30,
	   "a stray byte and a cut sequence: each one cell of \xef\xbf\xbd, the replacement character -- not the box");
	ok(cells(CX(6), CY(2), CX(3), CY(0), 10, 20),
	   "and the e after them in the seventh cell, as e draws: the text did not slip");

	/* row 3: four é centred on 180 -- 40 wide by characters, not 80 by bytes */
	int lo = PW, hi = -1;
	for (int j = CY(3); j < CY(3) + 20; j++)
		for (int i = 0; i < PW; i++)
			if (screen[j * PW + i]) { if (i < lo) lo = i; if (i > hi) hi = i; }
	ok(lo >= 160 && hi < 200 && lo < 175 && hi > 185,
	   "text-anchor middle centres \xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9 by characters, not bytes");

write:
	if (argc > 1) {
		FILE *o = fopen(argv[1], "wb");
		fprintf(o, "P6\n%d %d\n255\n", PW, PH);
		for (int i = 0; i < PW * PH; i++) {
			uint16_t v = screen[i];
			fputc(((v >> 11) & 0x1F) * 255 / 31, o);
			fputc(((v >> 5) & 0x3F) * 255 / 63, o);
			fputc((v & 0x1F) * 255 / 31, o);
		}
		fclose(o);
		printf("     wrote %s\n", argv[1]);
	}
	return fails ? 1 : 0;
}
