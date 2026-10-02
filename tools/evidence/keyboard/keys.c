/*
 * What a keyboard costs, three ways, measured rather than estimated.
 *
 * gbasic-gap.md §3 says the distance between whisker and gBASIC's declarative
 * `gui` is one widget, `input`, and that on RV-9 an input field means an
 * on-screen keyboard. It offers three ways forward and marks the third
 * UNMEASURED:
 *
 *   1  text entry stays over the terminal; a panel displays and commands
 *   2  input is a P4-and-Linux feature, with SRC_MAX raised where affordable
 *   3  "a keyboard that is not a widget set -- one <path> of key outlines and
 *      one <text> per row rather than 36 of each ... the only route that keeps
 *      option 2 affordable on a small screen"
 *
 * This is that measurement. The same 36-key keyboard, same geometry to the
 * pixel, emitted three ways and drawn through RV-9's own parser and
 * rasteriser:
 *
 *   A  WIDGETS   whisker's composite: a <rect> and a <text> per key
 *   B  ROW PATHS one <path> of key outlines per row, plus a <text> per key
 *   C  ONE PATH  all 36 outlines in a single <path>, plus a <text> per key
 *
 * C is in here because the driver caps a path at MAX_CONTOURS, and the cap is
 * 16. 36 outlines is more than 16 and the question is what happens then --
 * which is a fact about the substrate, so it is run rather than read.
 *
 * Everything reports what it finds, not what it found. The pixel counts are
 * the check that bytes saved are not keys lost: B and C have exactly A's
 * geometry, so a form that draws the same keyboard paints the same number of
 * key-coloured pixels, and one that drops a key does not.
 */
#include "drv_svgwin.c"
#include "whisker.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PW
#define PW 1024
#endif
#ifndef PH
#define PH 600
#endif

static uint16_t screen[PW * PH];
static int blits;

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

/* ---- the keyboard, defined once so all three forms draw the same one ---- */

#define ROWS    4
#define MAXK    10

/* A table of pointers, which a module may not hold. This is a host programme
   and never links as one; whisker's own panels keep their text inline. */
static const char *LAB[ROWS][MAXK] = {
	{ "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P" },
	{ "A", "S", "D", "F", "G", "H", "J", "K", "L", "ENT" },
	{ "Z", "X", "C", "V", "B", "N", "M", ".", "DEL", 0 },
	{ "SHIFT", "123", "SPACE", "-", "+", "ESC", "TAB", 0, 0, 0 },
};
static const int NK[ROWS] = { 10, 10, 9, 7 };
static const int WT[ROWS][MAXK] = {
	{ 1,1,1,1,1,1,1,1,1,1 },
	{ 1,1,1,1,1,1,1,1,1,1 },
	{ 1,1,1,1,1,1,1,1,1 },
	{ 2,1,4,1,1,1,1 },
};

#define PAD   4
#define GAP   4
#define KEYH  ((PH >= 400) ? 62 : 32)
#define ROWP  (KEYH + GAP)
#define TOP   (PH - ROWS * ROWP)
#define FONT  ((PH >= 400) ? 20 : 12)

#define BG    0x10203au
#define KEY   0x2a6b8fu
#define INK   0xffffffu

/* The layout is WHISKER'S, asked for rather than re-derived -- so "the same
   geometry to the pixel" is a property of the programme and not a claim about
   it. Re-deriving it was the first thing this measured, and it was 14 % wrong,
   which would have been read as a rasterisation difference. */

/* The trees, which hold the layout every form here draws from. */
static wsk_widget_t KEYW[ROWS][MAXK];
static wsk_tree_t   KEYT[ROWS];

/* ---- a buffer that counts even when it overflows ---- */

typedef struct { char *b; int cap; int n; } ob_t;

static void ap(ob_t *o, const char *fmt, ...)
{
	va_list a;
	int room = (o->cap > o->n) ? o->cap - o->n : 0;
	va_start(a, fmt);
	o->n += vsnprintf(o->b + (room ? o->n : 0), room, fmt, a);
	va_end(a);
}

static void hex(char *out, unsigned int c)
{
	sprintf(out, "#%02x%02x%02x", (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

/* The label, centred, exactly where whisker puts it: advance is font*10/20 and
   the baseline is the cell top plus font*15/20 (design.md §2.1). */
static void label(ob_t *o, int row, int i)
{
	wsk_rect_t r = wsk_rect(&KEYT[row], i);
	ap(o, "<text x=\"%d\" y=\"%d\">%s</text>",
	   r.x + r.w / 2, wsk_baseline_of(r.y + (r.h - FONT) / 2, FONT), LAB[row][i]);
}

static void head(ob_t *o)
{
	char c[8];
	hex(c, BG);
	ap(o, "<svg viewBox=\"0 0 %d %d\">", PW, PH);
	ap(o, "<rect width=\"%d\" height=\"%d\" fill=\"%s\"/>", PW, PH, c);
	hex(c, INK);
	ap(o, "<g font-size=\"%d\" text-anchor=\"middle\" fill=\"%s\">", FONT, c);
}

static void outline(ob_t *o, int row, int i)
{
	wsk_rect_t r = wsk_rect(&KEYT[row], i);
	ap(o, "M%d %dh%dv%dh-%dz", r.x, r.y, r.w, r.h, r.w);
}

/* B: one path per row. C: one path for the lot. */
static int form_paths(char *buf, int cap, int per_row)
{
	ob_t o = { buf, cap, 0 };
	char c[8];
	int row, i;

	head(&o);
	hex(c, KEY);
	if (!per_row) ap(&o, "<path d=\"");
	for (row = 0; row < ROWS; row++) {
		if (per_row) ap(&o, "<path d=\"");
		for (i = 0; i < NK[row]; i++) outline(&o, row, i);
		if (per_row) ap(&o, "\" fill=\"%s\"/>", c);
	}
	if (!per_row) ap(&o, "\" fill=\"%s\"/>", c);
	for (row = 0; row < ROWS; row++)
		for (i = 0; i < NK[row]; i++) label(&o, row, i);
	ap(&o, "</g></svg>");
	return o.n;
}

/* ---- A: whisker's own composite, because the baseline has to be real ---- */

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

static void build_trees(int ids)
{
	for (int row = 0; row < ROWS; row++) {
		for (int i = 0; i < NK[row]; i++) {
			memset(&KEYW[row][i], 0, sizeof KEYW[row][i]);
			KEYW[row][i].kind = WSK_BUTTON;
			KEYW[row][i].weight = (short)WT[row][i];
			snprintf(KEYW[row][i].id, WSK_ID_MAX, "k%d_%d", row, i);
			snprintf(KEYW[row][i].text, WSK_TEXT_MAX, "%s", LAB[row][i]);
		}
		wsk_init(&KEYT[row], WSK_ROW, PW, PH);
		KEYT[row].pad = PAD;
		KEYT[row].gap = GAP;
		KEYT[row].font = FONT;
		KEYT[row].bg = BG;
		KEYT[row].ink = INK;
		KEYT[row].accent = KEY;
		KEYT[row].ids = (unsigned char)ids;
		wsk_area(&KEYT[row], 0, TOP + row * ROWP, PW, KEYH);
		KEYT[row].item = KEYW[row];
		KEYT[row].n = NK[row];
	}
}

static void to_ob(wsk_sink_t *s, const char *b, int n)
{
	ob_t *o = (ob_t *)s->ctx;
	for (int i = 0; i < n; i++)
		if (s->len + i < o->cap - 1) o->b[s->len + i] = b[i];
}

static int form_widgets(char *buf, int cap, int ids)
{
	ob_t o = { buf, cap, 0 };
	wsk_sink_t s;
	comp_t c;
	int n;

	build_trees(ids);
	wsk_doc_init(&c.d, &KEYT[0]);
	c.part = KEYT; c.n = ROWS;
	s.put = to_ob; s.ctx = &o; s.len = 0;
	n = wsk_compose(comp_body, &c, &s, 0);     /* 0 = no budget: measure it */
	if (n > 0 && n < cap) buf[n] = '\0';
	return n;
}

/* ---- draw it, and count what landed ---- */

static int keypixels(void)
{
	uint16_t want = (uint16_t)(((KEY >> 8) & 0xF800) | ((KEY >> 5) & 0x07E0)
	                           | ((KEY >> 3) & 0x001F));
	uint16_t sw = (uint16_t)((want >> 8) | (want << 8));
	int k = 0;
	for (int i = 0; i < PW * PH; i++) if (screen[i] == sw) k++;
	return k;
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
}

static int draw(const char *doc, int n, const char *ppm)
{
	rv9_dev_t dev;
	size_t done;
	memset(&dev, 0, sizeof dev);
	dev.opt[OPT_BG] = 0x0000;
	svgwin_init(&dev);
	svgwin_open(&dev, 3);
	memset(screen, 0, sizeof screen);
	blits = 0;
	svgwin_write(&dev, doc, (size_t)n, &done);
	if (ppm) shot(ppm);
	return keypixels();
}

int main(void)
{
	static char a[65536], b[65536], c[65536];
	int na, nb, nc, nid, pa, pb, pc, keys = 0;

	for (int r = 0; r < ROWS; r++) keys += NK[r];

	nid = form_widgets(a, sizeof a, 1);        /* with ids, as hit needs */
	na  = form_widgets(a, sizeof a, 0);        /* without, the fair floor */
	nb  = form_paths(b, sizeof b, 1);          /* build_trees has run: the */
	nc  = form_paths(c, sizeof c, 0);          /* outlines are wsk_rect's */

	printf("a %d-key keyboard at %dx%d, SRC_MAX is %d\n\n", keys, PW, PH, SRC_MAX);

	pa = draw(a, na, "keys_widgets.ppm");
	pb = draw(b, nb, "keys_rowpaths.ppm");
	pc = draw(c, nc, "keys_onepath.ppm");

	printf("  %-28s %5s  %7s  %s\n", "", "bytes", "key px", "");
	printf("  %-28s %5d  %7d  %s\n", "A  widgets, with ids", nid, pa,
	       nid <= SRC_MAX ? "fits" : "OVER SRC_MAX");
	printf("  %-28s %5d  %7d  %s\n", "A  widgets, no ids", na, pa,
	       na <= SRC_MAX ? "fits" : "OVER SRC_MAX");
	printf("  %-28s %5d  %7d  %s\n", "B  one path per row", nb, pb,
	       nb <= SRC_MAX ? "fits" : "OVER SRC_MAX");
	printf("  %-28s %5d  %7d  %s\n", "C  one path for all", nc, pc,
	       nc <= SRC_MAX ? "fits" : "OVER SRC_MAX");

	/* A <rect> and a filled <path> contour need not land on exactly the same
	   edge pixels, so the test is whether the same KEYBOARD is there, not
	   whether two rasterisations agree to the pixel. A dropped key is 6,076
	   pixels at this size; a differing edge is tens. */
	printf("\n  B draws %+d key pixels against A (%+d%%): %s\n",
	       pb - pa, pa ? (pb - pa) * 100 / pa : 0,
	       (pa && abs(pb - pa) * 100 / pa < 2) ? "the same keyboard"
	                                           : "NOT the same keyboard");
	printf("  C draws %+d key pixels against A (%+d%%): %s\n",
	       pc - pa, pa ? (pc - pa) * 100 / pa : 0,
	       (pa && abs(pc - pa) * 100 / pa < 2) ? "the same keyboard"
	                                           : "NOT the same keyboard");
	printf("  MAX_CONTOURS is %d, and this keyboard wants %d outlines\n",
	       MAX_CONTOURS, keys);
	if (na)
		printf("\n  B costs %d%% of A with ids, %d%% of A without\n",
		       nb * 100 / nid, nb * 100 / na);
	printf("\n  wrote keys_widgets.ppm, keys_rowpaths.ppm, keys_onepath.ppm\n");
	return 0;
}
