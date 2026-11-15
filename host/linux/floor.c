/*
 * A conference floor on whisker's world (design.md §5q).
 *
 *     ./run.sh --floor             the SDL window, RV-9's rasteriser
 *     host/web/run.sh --floor      the same program in a browser
 *
 * Matthew's virtual conference, small: people walk the floor, posters hang
 * on the wall -- each a stack of its own, a title, an abstract that wraps
 * and a button, zooming as one (§5q step 2b) -- and what people say floats above them -- smaller and fainter
 * the further it is from you. That is THIS PROGRAM's policy; whisker draws
 * each thing at the scale and fade it is told (§5q rule 2). Drag to look
 * around, the wheel to zoom, a click on the floor to walk there, a click on
 * a person or a poster to meet it. The floor has the keyboard from the
 * start: the arrows look around, + and - zoom (§5z).
 *
 * The view follows you (wsk_world_follow, §5q step 5): you may wander the
 * middle of the screen, and it glides when you come within 30% of an edge.
 * Looking around -- a drag or the arrows -- stops it following; walking, or
 * a click on yourself, starts it again. Zooming does not stop it.
 *
 * Posters may be arranged by hand (§5q step 6): a drag on one moves it, and
 * whisker brings it to the front of the floor's order -- this program then
 * raises the people and what they say back above it, its own layering.
 *
 * Nobody walks through anybody: whisker says what a step would touch
 * (wsk_world_in, §5q step 4) and this program decides -- you stop and meet
 * whoever it is; the others choose somewhere else to go.
 */
#include "host.h"
#include "whisker.h"

#include <stdio.h>
#include <string.h>

#define PEOPLE  10
#define POSTERS 6
#define ITEMS   (POSTERS + 2 * PEOPLE)      /* posters, people, what they say */
#define FLOOR_W 2400
#define FLOOR_H 1400
#define PW_     70                          /* a person, at 1:1 */
#define PH_     30
#define SW_     260                         /* what they say */
#define SH_     24

static const char *const NAMES[PEOPLE] = {
	"You", "Ana", "Bo", "Chen", "Dee", "Eli", "Fay", "Gus", "Hana", "Ivo",
};
static const char *const SAYS[PEOPLE] = {
	"hello!", "has anyone tried gBASIC?", "the P4 draws in 37 ms", "whisker is one header",
	"where is the coffee?", "poster 3 is great", "here for the RISC-V talk",
	"SVG all the way down", "is it lunch yet?", "flat, on purpose",
};
static const char *const TITLES[POSTERS] = {
	"Tiny GUIs on RISC-V", "Damage, banded", "A BASIC that scales",
	"Touch slop, measured", "SVG as a display list", "Worlds and views",
};
static const char *const ABSTRACTS[POSTERS] = {
	"a GUI in 3.8 KB on a C5",
	"repaint only the rows that moved",
	"one program from C5 to desktop",
	"9-21 px of scatter, no drift",
	"write the picture, not the pixels",
	"zoom, pan and fade a floor",
};
static const unsigned int FILLS[PEOPLE] = {
	0xd0a020u, 0x3a7ab0u, 0x8a4a9au, 0x3a9a6au, 0xb05a3au,
	0x5a6ab0u, 0x9a8a3au, 0x4a8a8au, 0xa04a6au, 0x6a9a3au,
};

static const wsk_widget_t WIN[] = {
	{ .kind = WSK_LABEL, .id = "status", .size = 32 },
	{ .kind = WSK_STACK, .id = "floor", .sub = 0, .weight = 1 },
};

/* A poster: its title, its abstract wrapping in a row, a button. */
static const wsk_widget_t ABSTRACT[1] = { { .kind = WSK_TEXT, .id = "abstract" } };

static wsk_widget_t win_items[2], items[ITEMS], poster[POSTERS][3], abstract[POSTERS][1];
static wsk_place_t place[ITEMS];
static short order[ITEMS];                      /* bottom first: posters, people, speech */
static wsk_tree_t win, nest[1 + 2 * POSTERS];    /* the floor; each poster and its abstract */
static wsk_ui_t ui;
static int px[PEOPLE], py[PEOPLE], tx[PEOPLE], ty[PEOPLE];   /* where, and where to */
static unsigned int seed = 12345;
static int following = 1;
static int split_told;

static int rnd(int n) { seed = seed * 1103515245u + 12345u; return (int)((seed >> 16) % (unsigned)n); }
static int iabs(int v) { return v < 0 ? -v : v; }

static void say(const char *s) { snprintf(win_items[0].text, WSK_TEXT_MAX, "%.31s", s); }

/* What each says sits over them, at a scale and a fade for its distance from
   you: the program's policy. Near: 1:1 and clear. Far: down to 3/8, and up
   to most of the way into the floor. */
static void place_all(void)
{
	for (int i = 0; i < PEOPLE; i++) {
		wsk_place_t *p = &place[POSTERS + i], *s = &place[POSTERS + PEOPLE + i];
		int dx = iabs(px[i] - px[0]), dy = iabs(py[i] - py[0]);
		int d = (dx > dy) ? dx + dy / 2 : dy + dx / 2;        /* near enough a distance */
		int sc = (d < 300) ? 256 : 256 * 300 / d, fade = (d < 300) ? 0 : (d - 300) / 4;
		if (sc < 96) sc = 96;
		if (fade > 210) fade = 210;
		p->x = px[i]; p->y = py[i];
		s->scale = (short)sc;
		s->fade = (unsigned char)fade;
		s->x = px[i] + PW_ / 2 - (SW_ * sc / 256) / 2;          /* centred over them */
		s->y = py[i] - SH_ * sc / 256 - 4;
	}
}

/* Who or what a person would touch standing at x, y: a poster or another
   person -- not what anyone says, which floats over heads. -1 for nobody. */
static int bumps(int i, int x, int y)
{
	int out[16], n = wsk_world_in(&nest[0], x, y, PW_, PH_, out, 16);
	for (int k = 0; k < n && k < 16; k++)
		if (out[k] != POSTERS + i && out[k] < POSTERS + PEOPLE) return out[k];
	return -1;
}

/* Everyone a step toward where they are going; the others choose somewhere
   new when they get there, or when someone is in the way. 1 if anyone moved. */
static int step(void)
{
	int moved = 0;
	for (int i = 0; i < PEOPLE; i++) {
		int v = (i == 0) ? 10 : 3, dx = tx[i] - px[i], dy = ty[i] - py[i], nx, ny, b;
		if (dx == 0 && dy == 0) {
			if (i == 0 || rnd(40) != 0) continue;
			tx[i] = 100 + rnd(FLOOR_W - 200 - PW_);
			ty[i] = 260 + rnd(FLOOR_H - 360);
			continue;
		}
		nx = px[i] + ((dx > v) ? v : (dx < -v) ? -v : dx);
		ny = py[i] + ((dy > v) ? v : (dy < -v) ? -v : dy);
		/* only a step that would newly touch someone is stopped, so two
		   who start too close can still walk apart */
		if ((b = bumps(i, nx, ny)) >= 0 && bumps(i, px[i], py[i]) < 0) {
			tx[i] = px[i]; ty[i] = py[i];
			if (i == 0) {
				char m[64];
				if (b < POSTERS) snprintf(m, sizeof m, "You reach \"%s\".", TITLES[b]);
				else snprintf(m, sizeof m, "You meet %s.", NAMES[b - POSTERS]);
				say(m);
				printf("bump   %s\n", m);
				fflush(stdout);
				moved = 1;
			}
			continue;
		}
		px[i] = nx; py[i] = ny;
		moved = 1;
		if (i == 0 && px[0] == tx[0] && py[0] == ty[0]) say("You arrive.");
	}
	if (moved) place_all();
	return moved;
}

static void to_window(wsk_sink_t *s, const char *b, int n) { (void)s; wh_write(b, n); }

/*
 * Rows y0..y1 of the window, as one document if it fits the 4 KB a window
 * on RV-9's /w0 takes (design §2.3) -- and, zoomed out with the whole floor
 * in view, it does not: then the rows in halves, on 8-row bands, each a
 * whole document of what crosses them. whisker refuses an oversize document
 * before writing a byte (WSK_E_BUDGET), so a refusal costs only the count.
 */
static void draw(wsk_sink_t *s, int y0, int y1)
{
	int mid = ((y0 + y1) / 2) & ~7;
	wh_clip(y0, y1);
	s->len = 0;
	if (wsk_render_rows(&win, y0, y1, s, 4096) != WSK_E_BUDGET) return;
	if (mid <= y0 || mid >= y1) return;      /* one band too dense: nothing to split */
	if (!split_told) { split_told = 1; printf("bands  a frame over 4 KB goes in parts\n"); }
	draw(s, y0, mid);
	draw(s, mid, y1);
}

int main(void)
{
	int w = 1024, h = 600, y0, y1, which = -1, e;
	const wsk_tree_t *in = 0;
	wsk_sink_t s;

	/* posters along the top wall: stacks, laid out at the floor's zoom */
	for (int i = 0; i < POSTERS; i++) {
		wsk_widget_t *it = &items[i], *p = poster[i];
		it->kind = WSK_STACK;
		snprintf(it->id, WSK_ID_MAX, "p%d", i);
		it->sub = (short)(1 + 2 * i);
		it->fill = 0x2a3a50u;
		place[i].x = 120 + i * 380; place[i].y = 20;
		place[i].w = 300; place[i].h = 116; place[i].scale = 256;
		place[i].move = 1;                                       /* arranged by hand */
		p[0].kind = WSK_LABEL; snprintf(p[0].id, WSK_ID_MAX, "t%d", i);
		snprintf(p[0].text, WSK_TEXT_MAX, "%.31s", TITLES[i]); p[0].size = 22;
		p[1].kind = WSK_STACK; snprintf(p[1].id, WSK_ID_MAX, "x%d", i);
		p[1].sub = (short)(2 + 2 * i); p[1].weight = 1;
		p[2].kind = WSK_BUTTON; snprintf(p[2].id, WSK_ID_MAX, "v%d", i);
		snprintf(p[2].text, WSK_TEXT_MAX, "Visit"); p[2].size = 26; p[2].fill = 0x4a6a8au;
		abstract[i][0] = ABSTRACT[0];
		abstract[i][0].sub = (short)i;
	}
	for (int i = 0; i < PEOPLE; i++) {
		wsk_widget_t *p = &items[POSTERS + i], *t = &items[POSTERS + PEOPLE + i];
		p->kind = WSK_BUTTON;
		snprintf(p->id, WSK_ID_MAX, "a%d", i);
		snprintf(p->text, WSK_TEXT_MAX, "%s", NAMES[i]);
		p->fill = FILLS[i];
		place[POSTERS + i].w = PW_; place[POSTERS + i].h = PH_; place[POSTERS + i].scale = 256;
		t->kind = WSK_LABEL;
		snprintf(t->id, WSK_ID_MAX, "s%d", i);
		snprintf(t->text, WSK_TEXT_MAX, "%.31s", SAYS[i]);
		place[POSTERS + PEOPLE + i].w = SW_; place[POSTERS + PEOPLE + i].h = SH_;
		place[POSTERS + PEOPLE + i].scale = 256;
		px[i] = tx[i] = (i == 0) ? 500 : 150 + rnd(FLOOR_W - 300);
		py[i] = ty[i] = (i == 0) ? 300 : 280 + rnd(FLOOR_H - 380);
	}
	place_all();

	memcpy(win_items, WIN, sizeof WIN);
	wsk_init(&win, WSK_COL, w, h);
	win.pad = 4; win.gap = 0; win.ids = 1;
	win.item = win_items; win.n = 2;
	win.nest = nest; win.nests = 1 + 2 * POSTERS;
	win.str = ABSTRACTS; win.strs = POSTERS;
	win.caps = (unsigned char)wh_caps();
	win.talign = WSK_ALIGN_START;
	wsk_nest_init(&nest[0], &win, WSK_COL);
	nest[0].pad = 0;
	nest[0].bg = 0x1a2a20u;
	nest[0].item = items; nest[0].n = ITEMS;
	nest[0].overflow = WSK_WORLD; nest[0].place = place;
	for (int i = 0; i < ITEMS; i++) order[i] = (short)i;
	nest[0].order = order;
	nest[0].wspace = WSK_BOUNDED; nest[0].ww = FLOOR_W; nest[0].wh = FLOOR_H;
	nest[0].vx = 100; nest[0].vy = 0;
	win_items[1].fill = nest[0].bg;
	for (int i = 0; i < POSTERS; i++) {
		wsk_tree_t *p = &nest[1 + 2 * i], *a = &nest[2 + 2 * i];
		wsk_nest_init(p, &win, WSK_COL);
		p->item = poster[i]; p->n = 3; p->pad = 6; p->gap = 4;
		wsk_nest_init(a, &win, WSK_ROW);
		a->item = abstract[i]; a->n = 1; a->overflow = WSK_WRAP;
		a->pad = 0; a->gap = 8; a->lgap = 0; a->talign = WSK_ALIGN_START;
	}
	say("drag:look wheel:zoom click:walk");      /* a label holds 31 */
	if ((e = wsk_check_in(&win, &which, &in)) != WSK_OK) {
		fprintf(stderr, "floor: refused (%d) at %d\n", e, which);
		return 1;
	}
	if (wh_open(w, h, 1, "whisker -- a conference floor") != 0) return 1;
	s.put = to_window; s.ctx = 0; s.len = 0;
	wsk_ui_init(&ui, &win);
	wsk_ui_focus(&ui, "floor", &y0, &y1);     /* the keys look around from the start */
	draw(&s, 0, h);

	for (;;) {
		wh_event_t he;
		wsk_event_t ev;
		wsk_note_t note[4];
		int k = wh_poll(&he), n, all = 0;

		if (k == WH_QUIT) break;
		if (k == WH_NOTHING) {
			/* a frame: everyone takes a step, and the floor is drawn again */
			wh_wait(40);
			{
				int moved = step(), fy0, fy1;
				/* the view glides after you: 30% margin, a quarter of the way a frame */
				if (following && wsk_world_follow(&win, "floor", POSTERS + 0, 30, 64, &fy0, &fy1) == 1)
					moved = 1;
				if (moved) draw(&s, 0, h);
			}
			continue;
		}
		if (k == WH_EXPOSE) { draw(&s, 0, h); continue; }
		memset(&ev, 0, sizeof ev);
		ev.x = (short)he.x; ev.y = (short)he.y;
		if (k == WH_PRESS) { ev.kind = WSK_EV_DOWN; ev.key = (unsigned short)he.key; ev.d = (short)he.d; }
		else if (k == WH_RELEASE) ev.kind = WSK_EV_UP;
		else if (k == WH_MOVE) ev.kind = WSK_EV_MOVE;
		else if (k == WH_WHEEL) { ev.kind = WSK_EV_WHEEL; ev.d = (short)he.d; }
		else if (k == WH_KEY) { ev.kind = WSK_EV_KEY; ev.key = (unsigned short)he.key; }
		else if (k == WH_TEXT) { ev.kind = WSK_EV_TEXT; memcpy(ev.text, he.text, sizeof ev.text); }
		else continue;

		n = wsk_feed(&ui, &ev, note, 4, &y0, &y1);
		for (int i = 0; i < n && i < 4; i++) {
			const wsk_note_t *t = &note[i];
			char m[96];
			if (t->kind == WSK_NOTE_CLICK && t->in == &nest[0] && t->item >= 0) {
				int j = t->item;
				if (j == POSTERS) following = 1;            /* a click on yourself */
				if (j < POSTERS) snprintf(m, sizeof m, "%s: %s", TITLES[j], ABSTRACTS[j]);
				else if (j < POSTERS + PEOPLE) {
					j -= POSTERS;
					snprintf(m, sizeof m, j ? "%s: \"%s\"" : "%s waves.", NAMES[j], SAYS[j]);
				} else
					snprintf(m, sizeof m, "%s", SAYS[j - POSTERS - PEOPLE]);
				say(m);
				printf("meet   %s\n", m);
				all = 1;
			} else if (t->kind == WSK_NOTE_CLICK && t->in > &nest[0] && t->in < &nest[1 + 2 * POSTERS]) {
				/* inside a poster: its Visit button, or anything else on it */
				int j = (int)((t->in - &nest[1]) / 2);
				if (t->in == &nest[1 + 2 * j] && t->item == 2)
					snprintf(m, sizeof m, "You visit \"%s\".", TITLES[j]);
				else
					snprintf(m, sizeof m, "%s: %s", TITLES[j], ABSTRACTS[j]);
				say(m);
				printf("poster %s\n", m);
				all = 1;
			} else if (t->kind == WSK_NOTE_CLICK && t->in == &win && t->item == 1) {
				/* bare floor: walk there (step 3) */
				int wx, wy;
				if (wsk_world_point(&win, ev.x, ev.y, 0, &wx, &wy) == WSK_OK) {
					tx[0] = wx - PW_ / 2; ty[0] = wy - PH_ / 2;
					if (tx[0] < 0) tx[0] = 0;
					if (ty[0] < 0) ty[0] = 0;
					printf("walk   to %d,%d\n", wx, wy);
					following = 1;
					say("Walking...");
					all = 1;
				}
			} else if (t->kind == WSK_NOTE_RAISE) {
				/* a poster to the front of the posters -- but people and their
				   words stay above every poster: this program's layering */
				for (int j = POSTERS; j < ITEMS; j++) wsk_world_raise(&nest[0], j);
				printf("raise  p%d\n", t->item);
				all = 1;
			} else if (t->kind == WSK_NOTE_MOVE) {
				printf("move   p%d to %d,%d\n", t->item, place[t->item].x, place[t->item].y);
			} else if (t->kind == WSK_NOTE_VIEW) {
				/* the person looked around: stop following -- unless it was a zoom */
				if (ev.kind == WSK_EV_MOVE || ev.kind == WSK_EV_KEY) {
					if (following) printf("follow off\n");
					following = 0;
				}
				printf("view   %d,%d at %d/256\n", nest[0].vx, nest[0].vy, t->value);
			}
		}
		fflush(stdout);
		if (all) draw(&s, 0, h);
		else if (y1 > y0) draw(&s, y0, y1);
	}
	wh_close();
	return 0;
}
