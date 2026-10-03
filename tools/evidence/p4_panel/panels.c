/*
 * Does a realistic seven-inch panel fit in 4 KB?
 *
 * design.md §2.3 makes a prediction nobody has tested: SRC_MAX does not scale
 * with the screen, so 4,096 bytes is "comfortable on the C5 and the binding
 * constraint on the P4". That was reasoning, and it was reasoned before
 * whisker's emitter existed -- tools/evidence/svgwin_facts/bytes.c answers the
 * same question with hand-written SVG, which is a different document from the
 * one whisker actually produces.
 *
 * This asks it with whisker, at 1024x600, on panels somebody would really
 * build. It needs no P4: the number of bytes a tree emits is a property of the
 * tree and the emitter, and both are here. What the board is for is timing and
 * the look of it on glass, neither of which is this question.
 *
 * Each panel is built the way a caller would build it -- composition for
 * anything with more than one stack, because §5c is how a big screen gets a
 * grid -- and measured with a counting sink, which is the same pass
 * wsk_compose uses to refuse a document before writing a byte of it.
 */
#include "drv_svgwin.c"
#include "whisker.h"

#include <stdio.h>
#include <string.h>

#define PW 1024
#define PH 600

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

/* ------------------------------------------------------------------ */

#define MAXW 300
#define MAXP 8

typedef struct {
	wsk_doc_t    d;
	wsk_tree_t   part[MAXP];
	int          n;
} panel_t;

static int body(wsk_sink_t *s, void *ctx)
{
	panel_t *p = (panel_t *)ctx;
	int i, e;

	wsk_open(&p->d, s);
	for (i = 0; i < p->n; i++) {
		e = wsk_part(&p->d, &p->part[i], s);
		if (e != WSK_OK) return e;
	}
	wsk_shut(s);
	return WSK_OK;
}

/* Bytes only: the counting sink, which is what refuses a document. */
static int measure(panel_t *p)
{
	wsk_sink_t s;
	s.put = 0; s.ctx = 0; s.len = 0;
	return wsk_compose(body, p, &s, 0);
}

static void set_ids(panel_t *p, int on)
{
	for (int i = 0; i < p->n; i++) p->part[i].ids = (short)on;
}

static void fill(wsk_widget_t *w, int kind, const char *id, const char *text, int weight)
{
	memset(w, 0, sizeof *w);
	w->kind = (short)kind;
	w->weight = (short)weight;
	wsk__scpy(w->id, id, WSK_ID_MAX);
	wsk__scpy(w->text, text, WSK_TEXT_MAX);
	w->fill = WSK_INHERIT;
	w->ink = WSK_INHERIT;
}

static void name_n(char *out, const char *stem, int i)
{
	int n = 0;
	while (stem[n] != '\0' && n < WSK_ID_MAX - 3) { out[n] = stem[n]; n++; }
	if (i >= 10) out[n++] = (char)('0' + i / 10);
	out[n++] = (char)('0' + i % 10);
	out[n] = '\0';
}

static void stack(panel_t *p, int axis, int x, int y, int w, int h,
                  wsk_widget_t *items, int n, int font)
{
	wsk_tree_t *t = &p->part[p->n++];
	wsk_init(t, axis, PW, PH);
	t->pad = 8; t->gap = 8; t->font = (short)font;
	/* wsk_init's palette, left alone. An earlier version of this set bg to
	   its own taste and kept the default trough, and the rendered form came
	   out with INVISIBLE FIELDS -- the trough is deliberately darker than
	   the background, so it reads as a well, and a caller that moves one
	   without the other loses the contrast that makes a field look like a
	   field. The numbers did not notice; the picture did. */
	wsk_area(t, x, y, w, h);
	t->item = items; t->n = (short)n;
}

/* ------------------------------------------------------------------ */

static void report(const char *name, panel_t *p, const char *note)
{
	int with, without;

	set_ids(p, 1); with = measure(p);
	set_ids(p, 0); without = measure(p);
	set_ids(p, 1);

	printf("  %-22s %5d %5s   %5d %5s   %s\n", name,
	       with,    with    > 0 && with    <= SRC_MAX ? "fits" : "OVER",
	       without, without > 0 && without <= SRC_MAX ? "fits" : "OVER",
	       note);
}

int main(void)
{
	static wsk_widget_t wa[MAXW], wb[MAXW], wc[MAXW], wd[MAXW];
	panel_t p;
	char id[WSK_ID_MAX];
	int i;

	printf("a whisker panel at %dx%d, SRC_MAX %d\n\n", PW, PH, SRC_MAX);
	printf("  %-22s %-11s   %-11s   %s\n", "panel", "with ids", "no ids", "");

	/* 1. A readout. wstat's shape, at seven inches: two columns of
	      readings, which is what a wide screen wants (§2.3's picture). */
	memset(&p, 0, sizeof p);
	for (i = 0; i < 10; i++) {
		name_n(id, "a", i); fill(&wa[i], WSK_LABEL, id, "TEMP     45.3 C", 0);
		name_n(id, "b", i); fill(&wb[i], WSK_LABEL, id, "PRESSURE 1013 mb", 0);
	}
	stack(&p, WSK_COL, 0, 0, PW / 2, PH, wa, 10, 28);
	stack(&p, WSK_COL, PW / 2, 0, PW / 2, PH, wb, 10, 28);
	wsk_doc_init(&p.d, &p.part[0]);
	report("readout, 20 values", &p, "two columns");

	/* 2. A settings form: a label and a field per row, plus two buttons. */
	memset(&p, 0, sizeof p);
	for (i = 0; i < 6; i++) {
		name_n(id, "l", i); fill(&wa[i], WSK_LABEL, id, "Network name", 0);
		name_n(id, "f", i); fill(&wb[i], WSK_FIELD, id, "wifi-home", 0);
	}
	fill(&wc[0], WSK_BUTTON, "save", "Save", 1);
	fill(&wc[1], WSK_BUTTON, "cancel", "Cancel", 1);
	stack(&p, WSK_COL, 0, 0, 340, 520, wa, 6, 28);
	stack(&p, WSK_COL, 340, 0, PW - 340, 520, wb, 6, 28);
	stack(&p, WSK_ROW, 0, 520, PW, 80, wc, 2, 28);
	wsk_doc_init(&p.d, &p.part[0]);
	report("settings, 6 fields", &p, "labels, fields, 2 buttons");

	/* 3. A launcher: a grid of buttons, which on this screen is three
	      stacks side by side rather than one long column. */
	memset(&p, 0, sizeof p);
	for (i = 0; i < 5; i++) {
		name_n(id, "x", i); fill(&wa[i], WSK_BUTTON, id, "Settings", 1);
		name_n(id, "y", i); fill(&wb[i], WSK_BUTTON, id, "Network", 1);
		name_n(id, "z", i); fill(&wc[i], WSK_BUTTON, id, "Sensors", 1);
	}
	stack(&p, WSK_COL, 0,          0, PW / 3, PH, wa, 5, 28);
	stack(&p, WSK_COL, PW / 3,     0, PW / 3, PH, wb, 5, 28);
	stack(&p, WSK_COL, 2 * PW / 3, 0, PW / 3, PH, wc, 5, 28);
	wsk_doc_init(&p.d, &p.part[0]);
	report("launcher, 15 buttons", &p, "a 3x5 grid");

	/* 4. Text entry: one field and a keyboard, which §5d measured at
	      115 bytes under the ceiling with nothing else on the screen. */
	memset(&p, 0, sizeof p);
	fill(&wa[0], WSK_FIELD, "entry", "", 0);
	{
		static const char *ROW[4] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM.", "" };
		int r, k, n;
		wsk_widget_t *dst[3] = { wb, wc, wd };
		stack(&p, WSK_COL, 0, 0, PW, 96, wa, 1, 32);
		for (r = 0; r < 3; r++) {
			n = 0;
			for (k = 0; ROW[r][k] != '\0'; k++) {
				char t[2];
				t[0] = ROW[r][k]; t[1] = '\0';
				name_n(id, "k", r * 10 + k);
				fill(&dst[r][n], WSK_BUTTON, id, t, 1);
				n++;
			}
			stack(&p, WSK_ROW, 0, 96 + r * 120, PW, 120, dst[r], n, 32);
		}
	}
	wsk_doc_init(&p.d, &p.part[0]);
	report("text entry, 28 keys", &p, "field + 3 keyboard rows");

	/* The same keyboard §5d measured, four rows of it, to compare directly
	   with the 3,981 that was 115 bytes under the ceiling. */
	memset(&p, 0, sizeof p);
	fill(&wa[0], WSK_FIELD, "entry", "", 0);
	{
		static const char *ROW[4] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM.", "123456" };
		int r, k, n;
		wsk_widget_t *dst[4] = { wb, wc, wd, &wa[8] };
		stack(&p, WSK_COL, 0, 0, PW, 96, wa, 1, 32);
		for (r = 0; r < 4; r++) {
			n = 0;
			for (k = 0; ROW[r][k] != '\0'; k++) {
				char t[2];
				t[0] = ROW[r][k]; t[1] = '\0';
				name_n(id, "k", r * 10 + k);
				fill(&dst[r][n], WSK_BUTTON, id, t, 1);
				n++;
			}
			stack(&p, WSK_ROW, 0, 96 + r * 120, PW, 120, dst[r], n, 32);
		}
	}
	wsk_doc_init(&p.d, &p.part[0]);
	report("text entry, 36 keys", &p, "field + 4 rows -- the SS5d panel");

	/*
	 * And the question §2.3 actually asks: on this screen, which runs out
	 * first -- the bytes or the room?
	 *
	 * Asked by filling the screen at a given row height rather than by
	 * counting widgets up to 4,096, because a stack that overruns the panel
	 * emits only what still intersects it: the widgets below the edge are
	 * skipped, so "how many fit in SRC_MAX" silently becomes "how many fit
	 * on the glass" and the number stops meaning anything. The first
	 * version of this printed MAXW twice and looked like an answer.
	 */
	{
		static const int H[] = { 64, 48, 32, 24, 16 };
		int k;

		printf("\n  filling the screen with a column of buttons:\n");
		printf("     %-10s %-6s %-7s %s\n", "row height", "fits", "bytes", "");
		for (k = 0; k < (int)(sizeof H / sizeof H[0]); k++) {
			int h = H[k];
			int n = (PH - 16 + 8) / (h + 8);
			int len;

			if (n > MAXW) n = MAXW;
			memset(&p, 0, sizeof p);
			for (i = 0; i < n; i++) {
				name_n(id, "c", i);
				fill(&wa[i], WSK_BUTTON, id, "Control", 1);
				wa[i].size = (short)h;
			}
			stack(&p, WSK_COL, 0, 0, PW, PH, wa, n, 20);
			wsk_doc_init(&p.d, &p.part[0]);
			set_ids(&p, 1);
			len = measure(&p);
			printf("     %-10d %-6d %-7d %s\n", h, n, len,
			       len <= SRC_MAX ? "fits" : "OVER SRC_MAX");
		}
		printf("     (64 rows is about a finger at 170 dpi -- SS5's default)\n");
	}

	/*
	 * So what does bind? Density. A column cannot put more than a couple of
	 * dozen controls on a screen, but a GRID can -- and a keyboard is a
	 * grid, which is why the keyboard is the only realistic panel measured
	 * above that goes anywhere near the ceiling.
	 */
	{
		static const int COLS[] = { 3, 4, 5, 6, 6, 6 };
		static const int ROWS[] = { 3, 4, 5, 6, 8, 10 };
		int k;

		printf("\n  a grid of buttons, which is the shape that binds:\n");
		printf("     %-10s %-7s %-7s %s\n", "grid", "buttons", "bytes", "");
		for (k = 0; k < (int)(sizeof COLS / sizeof COLS[0]); k++) {
			int c = COLS[k], r = ROWS[k], col, len;
			wsk_widget_t *dst[6] = { wa, wb, wc, wd, &wa[80], &wb[80] };

			memset(&p, 0, sizeof p);
			for (col = 0; col < c; col++) {
				for (i = 0; i < r; i++) {
					name_n(id, "g", col * 10 + i);
					fill(&dst[col][i], WSK_BUTTON, id, "Ctl", 1);
				}
				stack(&p, WSK_COL, col * (PW / c), 0, PW / c, PH,
				      dst[col], r, 20);
			}
			wsk_doc_init(&p.d, &p.part[0]);
			set_ids(&p, 1);
			len = measure(&p);
			printf("     %dx%-8d %-7d %-7d %s\n", c, r, c * r, len,
			       len <= SRC_MAX ? "fits" : "OVER SRC_MAX");
		}
	}

	/* Look at one of them, because a number does not say whether it is an
	   interface. §2.3's "a big screen wants a grid" came from a picture. */
	{
		rv9_dev_t dev;
		wsk_sink_t s;
		memset(&dev, 0, sizeof dev);
		dev.opt[OPT_BG] = 0x0000;
		svgwin_init(&dev);
		svgwin_open(&dev, 3);
		memset(&p, 0, sizeof p);
		for (i = 0; i < 6; i++) {
			name_n(id, "l", i); fill(&wa[i], WSK_LABEL, id, "Network name", 0);
			name_n(id, "f", i); fill(&wb[i], WSK_FIELD, id, "wifi-home", 0);
		}
		fill(&wc[0], WSK_BUTTON, "save", "Save", 1);
		fill(&wc[1], WSK_BUTTON, "cancel", "Cancel", 1);
		stack(&p, WSK_COL, 0, 0, 340, 520, wa, 6, 28);
		stack(&p, WSK_COL, 340, 0, PW - 340, 520, wb, 6, 28);
		stack(&p, WSK_ROW, 0, 520, PW, 80, wc, 2, 28);
		wsk_doc_init(&p.d, &p.part[0]);
		s.put = to_window; s.ctx = &dev; s.len = 0;
		blits = 0;
		printf("\n  rendering the settings form: %d bytes, ", wsk_compose(body, &p, &s, SRC_MAX));
		printf("%d bands\n", blits);
		shot("p4_settings.ppm");
	}
	return 0;
}
