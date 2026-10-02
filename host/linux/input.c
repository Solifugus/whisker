/*
 * Typing, with a mouse.
 *
 *     ./run.sh --input                 1024x600, the P4's panel
 *     ./run.sh --input 800 480 1       something else
 *
 * design.md §5d, and D7: the touch monitor on this desk enumerates and
 * reports nothing, but the mouse works, and a click is a single point --
 * which is exactly what wsk_hit takes and exactly what the panel will deliver
 * in October. So the whole loop can be finished and watched now rather than
 * waited for.
 *
 * The loop, and it is the same four steps `demo.c` has, with one more:
 *
 *     a point comes in         wh_poll
 *     it becomes a name        wsk_hit, over each stack in turn
 *     the APPLICATION decides what the name means -- here, a character
 *     the name becomes rows    wsk_doc_rows
 *     one document repaints    wsk_compose, clipped to those rows
 *
 * The third step is the one worth looking at. whisker does not know that
 * "k0_0" means Q; there is no keymap in it, because a layout is an
 * application's business and a shifted layout is a different set of labels.
 * It knows the key's text because the CALLER put it there, and the caller
 * reads it back out. That is D5 -- whisker is pure -- doing something useful
 * rather than being a slogan.
 */
#include "host.h"
#include "whisker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NROW 4

static const char *LAB[NROW][10] = {
	{ "Q","W","E","R","T","Y","U","I","O","P" },
	{ "A","S","D","F","G","H","J","K","L","ENT" },
	{ "Z","X","C","V","B","N","M",".","DEL", 0 },
	{ "SHIFT","123","SPACE","-","+","ESC","TAB", 0,0,0 },
};
static const int NK[NROW] = { 10, 10, 9, 7 };
static const int WT[NROW][10] = {
	{ 1,1,1,1,1,1,1,1,1,1 },
	{ 1,1,1,1,1,1,1,1,1,1 },
	{ 1,1,1,1,1,1,1,1,1 },
	{ 2,1,4,1,1,1,1 },
};

/* [0] is the field; [1..4] are the key rows. One device, five stacks. */
static wsk_tree_t   PART[1 + NROW];
static wsk_widget_t FIELD[1];
static wsk_widget_t KEY[NROW][10];
static wsk_doc_t    DOC;

typedef struct { int n; } ctx_t;

static void to_window(wsk_sink_t *s, const char *b, int n)
{
	(void)s;
	wh_write(b, n);
}

static int body(wsk_sink_t *s, void *ctx)
{
	ctx_t *c = (ctx_t *)ctx;
	wsk_open(&DOC, s);
	for (int i = 0; i < c->n; i++) {
		int e = wsk_part(&DOC, &PART[i], s);
		if (e != WSK_OK) return e;
	}
	wsk_shut(s);
	return WSK_OK;
}

static int draw(int all)
{
	ctx_t c;
	wsk_sink_t s;
	c.n = 1 + NROW;
	s.put = to_window; s.ctx = 0; s.len = 0;
	if (all) { DOC.y0 = 0; DOC.y1 = DOC.h; }
	wh_clip(all ? 0 : DOC.y0, all ? 0 : DOC.y1);
	return wsk_compose(body, &c, &s, 4096);
}

/* Repaint just the rows one widget occupies, in whichever stack it is in. */
static int touch_up(int part, const char *id)
{
	const char *ids[1];
	ids[0] = id;
	if (wsk_doc_rows(&DOC, &PART[part], ids, 1) != WSK_OK) return -1;
	return draw(0);
}

static void build(int w, int h)
{
	int keyh = (h >= 400) ? 62 : 32;
	int rowp = keyh + 4;
	int top = h - NROW * rowp;

	memset(FIELD, 0, sizeof FIELD);
	FIELD[0].kind = WSK_FIELD;
	wsk__scpy(FIELD[0].id, "entry", WSK_ID_MAX);
	FIELD[0].mark = WSK_MARK_CARET;

	wsk_init(&PART[0], WSK_COL, w, h);
	PART[0].pad = 8; PART[0].gap = 8; PART[0].ids = 1;
	PART[0].font = (h >= 400) ? 24 : 14;
	PART[0].bg = 0x10203au;
	wsk_area(&PART[0], 0, top - 90, w, 70);
	PART[0].item = FIELD; PART[0].n = 1;

	for (int r = 0; r < NROW; r++) {
		for (int i = 0; i < NK[r]; i++) {
			memset(&KEY[r][i], 0, sizeof KEY[r][i]);
			KEY[r][i].kind = WSK_BUTTON;
			KEY[r][i].weight = (short)WT[r][i];
			snprintf(KEY[r][i].id, WSK_ID_MAX, "k%d_%d", r, i);
			snprintf(KEY[r][i].text, WSK_TEXT_MAX, "%s", LAB[r][i]);
		}
		wsk_init(&PART[r + 1], WSK_ROW, w, h);
		PART[r + 1].pad = 4; PART[r + 1].gap = 4; PART[r + 1].ids = 1;
		PART[r + 1].font = (h >= 400) ? 20 : 12;
		PART[r + 1].bg = 0x10203au;
		wsk_area(&PART[r + 1], 0, top + r * rowp, w, keyh);
		PART[r + 1].item = KEY[r]; PART[r + 1].n = NK[r];
	}

	wsk_doc_init(&DOC, &PART[0]);
}

/*
 * What a key means. The application's job, not whisker's -- and it is written
 * out longhand rather than tabulated because a table of string pointers is
 * exactly what an RV-9 module may not hold (§4.1), and this file is the shape
 * a module would copy.
 *
 * Returns the character to append, 8 for a rub-out, or 0 for "not text".
 */
static char meaning(const char *label)
{
	if (label[0] != '\0' && label[1] == '\0') return label[0];
	if (label[0] == 'S' && label[1] == 'P') return ' ';       /* SPACE */
	if (label[0] == 'D' && label[1] == 'E') return 8;         /* DEL */
	return 0;      /* ENT, ESC, TAB, SHIFT, 123 -- §5d has no shift state */
}

static void append(char c)
{
	int n = wsk__len(FIELD[0].text);
	if (c == 8) { if (n > 0) FIELD[0].text[n - 1] = '\0'; return; }
	if (n >= WSK_TEXT_MAX - 1) return;      /* a field holds 31 and says so */
	FIELD[0].text[n] = c;
	FIELD[0].text[n + 1] = '\0';
}

int main(int argc, char **argv)
{
	int w = (argc > 1) ? atoi(argv[1]) : 1024;
	int h = (argc > 2) ? atoi(argv[2]) : 600;
	int scale = (argc > 3) ? atoi(argv[3]) : 1;
	wh_event_t ev;
	int held_row = -1, held_i = -1, n;

	if (wh_open(w, h, scale, "whisker: input") != 0) return 1;
	build(w, h);

	n = draw(1);
	if (n < 0) {
		fprintf(stderr, "input: the panel does not fit in 4096 bytes (%d)\n", n);
		wh_close();
		return 1;
	}
	printf("whisker: %dx%d, %d bytes for the screen, %d under SRC_MAX.\n",
	       w, h, n, 4096 - n);
	printf("Click the keys. DEL rubs out, SPACE is a space, the rest are\n"
	       "ignored -- there is no shift state in iteration 4. q to quit.\n");

	for (;;) {
		int kind = wh_poll(&ev);

		if (kind == WH_QUIT) break;

		if (kind == WH_PRESS) {
			int found = 0;
			for (int r = 0; r < NROW && !found; r++) {
				int i = wsk_hit(&PART[r + 1], ev.x, ev.y);
				if (i < 0) continue;
				found = 1;
				held_row = r; held_i = i;
				KEY[r][i].fill = 0x8fd0ffu;
				KEY[r][i].ink = 0x112233u;
				KEY[r][i].mark = WSK_MARK_CORNERS;
				touch_up(r + 1, KEY[r][i].id);          /* the key lights */

				{
					char c = meaning(KEY[r][i].text);
					if (c) {
						append(c);
						touch_up(0, "entry");           /* and the field grows */
					}
				}
			}
			if (!found) printf("  (%4d,%3d) nothing\n", ev.x, ev.y);
		}

		if (kind == WH_RELEASE && held_row >= 0) {
			KEY[held_row][held_i].fill = WSK_INHERIT;
			KEY[held_row][held_i].ink = WSK_INHERIT;
			KEY[held_row][held_i].mark = 0;
			touch_up(held_row + 1, KEY[held_row][held_i].id);
			held_row = -1;
		}

		if (kind == WH_NOTHING) wh_wait(20);
	}

	printf("\n  typed: \"%s\"\n", FIELD[0].text);
	wh_close();
	return 0;
}
