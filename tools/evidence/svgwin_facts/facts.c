/*
 * Questions about /w0 that only the driver can answer.
 *
 * Built against RV-9's own sources through tools/hosttest's stubs, so this
 * is the real parser and the real rasteriser with a fake panel behind them.
 * Each section prints the claim it supports; see docs/review-2026-09-30.md.
 *
 *     ./run.sh            all of it
 *     ./run.sh 3          one section, by number
 *
 * Two of these were defects when this was written, and RV-9 fixed both on
 * 2026-09-30 (rv9-answers.md §7). The programme is kept, and reports what it
 * finds rather than what it found: against an older tree it still says
 * "wedged", and section 3's document still never returns -- which is why that
 * one is drawn in a child process the parent gives three seconds.
 */
#include "drv_svgwin.c"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define PW 320
#define PH 172

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

/* The panel stores its pixels byte-swapped; undo that to read one. */
static void at(int x, int y, int *r, int *g, int *b)
{
	uint16_t v = (uint16_t)((screen[y * PW + x] >> 8) | (screen[y * PW + x] << 8));
	*r = ((v >> 11) & 0x1F) * 255 / 31;
	*g = ((v >> 5) & 0x3F) * 255 / 63;
	*b = (v & 0x1F) * 255 / 31;
}

static void px(int x, int y, const char *what)
{
	int r, g, b; at(x, y, &r, &g, &b);
	printf("     (%3d,%3d) #%02x%02x%02x   %s\n", x, y, r, g, b, what);
}

static int inked_columns(void)
{
	int n = 0;
	for (int x = 0; x < PW; x++)
		for (int y = 0; y < PH; y++)
			if (screen[y * PW + x]) { n++; break; }
	return n;
}

static void draw(rv9_dev_t *dev, const char *doc)
{
	size_t done;
	memset(screen, 0, sizeof screen);
	blits = 0;
	svgwin_write(dev, doc, strlen(doc), &done);
}

/* ------------------------------------------------------------------ */

static void s1_overflow(rv9_dev_t *dev)
{
	static char big[6000];
	size_t done;
	int n = snprintf(big, sizeof big, "<svg viewBox=\"0 0 320 172\">");

	puts("1. What happens when a document exceeds SRC_MAX?");

	while (n < 5000)
		n += snprintf(big + n, sizeof big - n,
		              "<rect x=\"1\" y=\"1\" width=\"9\" height=\"9\" fill=\"#abc\"/>");
	n += snprintf(big + n, sizeof big - n, "</svg>");

	const char *ok = "<svg viewBox=\"0 0 320 172\">"
	                 "<rect width=\"320\" height=\"172\" fill=\"#123456\"/></svg>";
	int after, again;

	blits = 0;
	svgwin_write(dev, big, (size_t)n, &done);
	printf("     the oversized document (%d bytes):   %d bands\n", n, blits);

	blits = 0; svgwin_write(dev, ok, strlen(ok), &done); after = blits;
	printf("     a valid document written after it:   %d bands\n", after);
	blits = 0; svgwin_write(dev, ok, strlen(ok), &done); again = blits;
	printf("     and again:                           %d bands\n", again);

	if (after > 0 && again > 0) {
		puts("     -- the window still answers. An overflowed document is dropped");
		puts("        and logged, which is what RV-9 changed on 2026-09-30.");
		puts("        whisker still needs a byte budget: what it may not do is");
		puts("        discover the ceiling by writing past it.");
	} else {
		puts("     -- WEDGED. Past SRC_MAX the bytes were dropped, so the </svg>");
		puts("        that ends a document could never land in the buffer again,");
		puts("        for the life of the open path, and nothing was logged.");
		puts("        Fixed in RV-9 on 2026-09-30; this is an older tree.");
	}
}

static void s2_text_metrics(rv9_dev_t *dev)
{
	int x0 = PW, x1 = -1, y0 = PH, y1 = -1;

	puts("2. What does a string occupy? (whisker cannot lay out a label without this)");

	draw(dev, "<svg viewBox=\"0 0 320 172\">"
	          "<text x=\"20\" y=\"40\" font-size=\"20\" fill=\"#ffffff\">MMMMM</text></svg>");

	for (int y = 0; y < PH; y++)
		for (int x = 0; x < PW; x++)
			if (screen[y * PW + x]) {
				if (x < x0) x0 = x;
				if (x > x1) x1 = x;
				if (y < y0) y0 = y;
				if (y > y1) y1 = y;
			}

	printf("     \"MMMMM\" at x=20 y=40 font-size=20: ink x %d..%d, y %d..%d\n",
	       x0, x1, y0, y1);
	printf("     predicted advance  font_size*%d/%d = %d px, five of them = %d px\n",
	       RV9_GLYPH_W, RV9_GLYPH_H, 20 * RV9_GLYPH_W / RV9_GLYPH_H,
	       5 * 20 * RV9_GLYPH_W / RV9_GLYPH_H);
	printf("     predicted cell     rows %d..%d  (y - font_size*%d/%d, height font_size)\n",
	       40 - 20 * RV9_FONT_BASELINE / RV9_GLYPH_H,
	       40 - 20 * RV9_FONT_BASELINE / RV9_GLYPH_H + 20,
	       RV9_FONT_BASELINE, RV9_GLYPH_H);

	draw(dev, "<svg viewBox=\"0 0 320 172\">"
	          "<text x=\"160\" y=\"40\" font-size=\"20\" text-anchor=\"middle\""
	          " fill=\"#ffffff\">CONFIRM</text></svg>");
	x0 = PW; x1 = -1;
	for (int y = 0; y < PH; y++)
		for (int x = 0; x < PW; x++)
			if (screen[y * PW + x]) { if (x < x0) x0 = x; if (x > x1) x1 = x; }
	printf("     \"CONFIRM\" anchored middle at x=160: ink %d..%d, centred on %d\n",
	       x0, x1, (x0 + x1) / 2);

	/* Leading and trailing space is stripped before anything is measured or
	   anchored, so a label padded to a column width is not the width it was
	   padded to. Caught by the RV-9 session reading this back; see
	   rv9-answers.md §7 and RV-9's reference.md §9. */
	draw(dev, "<svg viewBox=\"0 0 320 172\">"
	          "<text x=\"20\" y=\"40\" font-size=\"20\" fill=\"#ffffff\">  OK  </text></svg>");
	x0 = PW; x1 = -1;
	for (int y = 0; y < PH; y++)
		for (int x = 0; x < PW; x++)
			if (screen[y * PW + x]) { if (x < x0) x0 = x; if (x > x1) x1 = x; }
	draw(dev, "<svg viewBox=\"0 0 320 172\">"
	          "<text x=\"20\" y=\"40\" font-size=\"20\" fill=\"#ffffff\">OK</text></svg>");
	{
		int a = PW, b = -1;
		for (int y = 0; y < PH; y++)
			for (int x = 0; x < PW; x++)
				if (screen[y * PW + x]) { if (x < a) a = x; if (x > b) b = x; }
		printf("     \"  OK  \" ink %d..%d against \"OK\" ink %d..%d  --  %s\n",
		       x0, x1, a, b,
		       (a == x0 && b == x1) ? "identical: padding is stripped, not measured"
		                            : "different");
	}

	puts("     -- one fixed-width face; a label can be sized, centred and");
	puts("        truncated with 32-bit arithmetic and no font API.");
}

static void s3_text_content(rv9_dev_t *dev)
{
	puts("3. What may a label contain?");

	draw(dev, "<svg viewBox=\"0 0 320 172\">"
	          "<text x=\"10\" y=\"40\" font-size=\"20\" fill=\"#fff\">a &lt; b</text></svg>");
	printf("     \"a &lt; b\"  (the entity):      %3d inked columns\n", inked_columns());

	draw(dev, "<svg viewBox=\"0 0 320 172\">"
	          "<text x=\"10\" y=\"40\" font-size=\"20\" fill=\"#fff\">a > b</text></svg>");
	printf("     \"a > b\"     (a raw '>'):      %3d inked columns\n", inked_columns());

	draw(dev, "<svg viewBox=\"0 0 320 172\">"
	          "<text x=\"10\" y=\"40\" font-size=\"20\" fill=\"#fff\">A\xc2\xb0" "B</text></svg>");
	printf("     \"A<degree>B\" (UTF-8, 4 bytes): %3d inked columns -- two cells"
	       " advance and draw nothing\n", inked_columns());
	puts("     -- entities are not decoded: the first line drew the characters");
	puts("        & l t ; on the panel. So text cannot be escaped.");

	/*
	 * And a raw '<'. That one does not return, so it is run in a child:
	 * the parent gives it three seconds -- a full frame is 0.3 ms -- and
	 * reports what happened either way.
	 */
	puts("     now a raw '<' in text content, in a child process:");
	fflush(stdout);

	pid_t kid = fork();
	if (kid == 0) {
		draw(dev, "<svg viewBox=\"0 0 320 172\">"
		          "<text x=\"10\" y=\"40\" font-size=\"20\" fill=\"#fff\">a < b</text></svg>");
		_exit(0);
	}

	for (int ms = 0; ms < 3000; ms += 20) {
		int st;
		if (waitpid(kid, &st, WNOHANG) == kid) {
			puts("     it returned -- the hang is fixed in this RV-9");
			return;
		}
		usleep(20000);
	}
	kill(kid, SIGKILL);
	waitpid(kid, NULL, 0);
	puts("     three seconds and still parsing, where a whole frame is 0.3 ms.");
	puts("     -- it does not return. attr() restarts its loop without advancing");
	puts("        p when the attribute-name scan stops on '/' or '>', and a raw");
	puts("        '<' makes the parser read the rest of the text as a tag.");
	puts("        A label is data: this is a hang reachable from a label.");
}

static void s4_band_backing(rv9_dev_t *dev)
{
	uint32_t v = (100u << 16) | 140u;          /* deliberately not band-aligned */

	const char *scene =
		"<svg viewBox=\"0 0 320 172\">"
		"<rect width=\"320\" height=\"172\" fill=\"#10203a\"/>"
		"<rect x=\"40\" y=\"100\" width=\"240\" height=\"40\" fill=\"#2a6b8f\"/></svg>";
	const char *tight =
		"<svg viewBox=\"0 0 320 172\">"
		"<rect x=\"0\" y=\"100\" width=\"320\" height=\"40\" fill=\"#10203a\"/>"
		"<rect x=\"40\" y=\"100\" width=\"240\" height=\"40\" fill=\"#8fd0ff\"/></svg>";
	const char *snapped =
		"<svg viewBox=\"0 0 320 172\">"
		"<rect x=\"0\" y=\"96\" width=\"320\" height=\"48\" fill=\"#10203a\"/>"
		"<rect x=\"40\" y=\"100\" width=\"240\" height=\"40\" fill=\"#8fd0ff\"/></svg>";

	puts("4. Which rows must a clipped repaint carry backing for?");

	draw(dev, scene);
	px(10, 97, "the scene, three rows above the button");

	svgwin_setstat(dev, RV9_SVG_SS_ROWS, &v);
	draw(dev, tight);
	puts("     clip 100..140, backing covering exactly 100..140:");
	px(10,  97, "row 97  -- in band 96..104, above the backing");
	px(10, 102, "row 102 -- inside the backing");
	px(10, 142, "row 142 -- in band 136..144, below the backing");

	draw(dev, scene);
	svgwin_setstat(dev, RV9_SVG_SS_ROWS, &v);
	draw(dev, snapped);
	puts("     the same clip, backing snapped out to 96..144:");
	px(10,  97, "row 97");
	px(10, 142, "row 142");
	puts("     -- the clip snaps outward and each band is cleared in full, so");
	puts("        the rows to carry are the band-aligned ones, not the widget's.");
}

int main(int argc, char **argv)
{
	rv9_dev_t dev;
	int only = (argc > 1) ? atoi(argv[1]) : 0;

	setvbuf(stdout, NULL, _IONBF, 0);
	memset(&dev, 0, sizeof dev);
	dev.opt[OPT_ROTATE] = 1;
	dev.opt[OPT_BG] = 0x0000;                  /* the device background: black */
	svgwin_init(&dev);
	svgwin_open(&dev, 3);

	if (only == 0 || only == 1) { s1_overflow(&dev); puts(""); }
	/* Section 1 leaves the window wedged on purpose, so start again. */
	svgwin_close(&dev); svgwin_open(&dev, 3);
	if (only == 0 || only == 2) { s2_text_metrics(&dev); puts(""); }
	if (only == 0 || only == 3) { s3_text_content(&dev); puts(""); }
	if (only == 0 || only == 4) { s4_band_backing(&dev); puts(""); }
	return 0;
}
