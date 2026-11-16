/*
 * A conference floor on whisker's world (design.md §5q), for several people
 * at once (§5ab).
 *
 *     ./run.sh --floor             the SDL window, RV-9's rasteriser: one seat
 *     host/web/run.sh --floor      in a browser: every page is a person
 *
 * Matthew's virtual conference, small: people walk the floor, posters hang
 * on the wall -- each a stack of its own, a title, an abstract that wraps
 * and a button, zooming as one (§5q step 2b) -- and what people say floats
 * above them, smaller and fainter the further it is from you. That is THIS
 * PROGRAM's policy; whisker draws each thing at the scale and fade it is
 * told (§5q rule 2). Drag to look around, the wheel to zoom, a click on the
 * floor to walk there, a click on a person or a poster to meet it. The floor
 * has the keyboard from the start: the arrows look around, + and - zoom
 * (§5z).
 *
 * Seats (§5ab): a screen is the size it says it is (step 2) -- a phone gets
 * a phone's floor, not a desk's shrunk. Each screen that joins takes over a person -- the first one
 * free -- who stops wandering and walks where that screen says; leaving
 * gives them back. Each seat has its own view, its own status line, and its
 * own copy of every place, so what people say is scaled and faded by
 * distance from ITS person. whisker keeps no state of its own (D5), so a
 * seat is just another set of trees over the same widgets.
 *
 * A seat's view starts at the hall's door and follows its person once they
 * walk (wsk_world_follow, §5q step 5): within 30% of an edge it glides.
 * Looking around -- a drag or the arrows -- stops it following; walking, or
 * a click on yourself, starts it again. Zooming does not stop it.
 *
 * Posters may be arranged by hand (§5q step 6): a drag on one moves it, and
 * whisker brings it to the front of the floor's order -- shared, since it is
 * one floor -- and this program raises the people and what they say back
 * above it, its own layering. Nobody may walk into a poster, so no poster
 * may be put down on anybody: a move that would touch a person is written
 * back to the last place that touched no one.
 *
 * Point at a poster and the status line gives its abstract; at a person,
 * their name (hover, §5aa). Rest there half a second -- or hold a finger
 * there -- and a tip says more (§5ac): a poster's abstract, a person's name
 * and how far away they are.
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
#define TREES   (1 + 2 * POSTERS)           /* the floor; each poster and its abstract */
#define FLOOR_W 2400
#define FLOOR_H 1400
#define PW_     70                          /* a person, at 1:1 */
#define PH_     30
#define SW_     260                         /* what they say */
#define SH_     24
#define DEV_W   1024
#define DEV_H   600

static const char *const NAMES[PEOPLE] = {
	"Max", "Ana", "Bo", "Chen", "Dee", "Eli", "Fay", "Gus", "Hana", "Ivo",
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
	{ .kind = WSK_TEXT, .id = "status", .sub = POSTERS, .size = 32 },   /* strs[POSTERS] */
	{ .kind = WSK_STACK, .id = "floor", .sub = 0, .weight = 1 },
};

/* A poster: its title, its abstract wrapping in a row, a button. */
static const wsk_widget_t ABSTRACT[1] = { { .kind = WSK_TEXT, .id = "abstract" } };

/* What every seat shares: the widgets, the drawing order, and the model --
   where everyone is and is going, where each poster was last put down. */
static wsk_widget_t items[ITEMS], poster[POSTERS][3], abstract[POSTERS][1];
static short order[ITEMS];                      /* bottom first: posters, people, speech */
static int px[PEOPLE], py[PEOPLE], tx[PEOPLE], ty[PEOPLE];
static int owner[PEOPLE];                       /* the seat walking each person, or -1 */
static int okx[POSTERS], oky[POSTERS];          /* each poster's place, clear of people */
static wsk_place_t model[ITEMS];                /* the places, for asking what touches */
static wsk_tree_t world;                        /* ... as a world whisker can ask */
static unsigned int seed = 12345;
static int split_told, checking;
static int max_waits = 1;                       /* Max stands at the door until a screen takes him */

/* One person's screen (§5ab): trees over the shared widgets, its own view,
   its own toolkit state, its own status line and places. */
typedef struct {
	int           on, me, following;      /* here; its person, or -1; its view follows them */
	wsk_widget_t  win_items[2];
	wsk_tree_t    win, nest[TREES];
	wsk_place_t   place[ITEMS];
	wsk_ui_t      ui;
	char          status[160];
	const char   *strs[POSTERS + 1];
	char          tip[96];                /* the tip this seat is shown (§5ac) */
} seat_t;
static seat_t seat[WH_SEATS];

static int rnd(int n) { seed = seed * 1103515245u + 12345u; return (int)((seed >> 16) % (unsigned)n); }
static int iabs(int v) { return v < 0 ? -v : v; }

static void say(int s, const char *t) { snprintf(seat[s].status, sizeof seat[s].status, "%s", t); }

/* Seat s's places from the model -- and what each person says, scaled and
   faded by their distance from s's own person: near, 1:1 and clear; far,
   down to 3/8 and most of the way into the floor. A seat with no person
   hears everyone clearly. */
static void place_for(int s)
{
	seat_t *S = &seat[s];
	for (int i = 0; i < POSTERS; i++) { S->place[i].x = okx[i]; S->place[i].y = oky[i]; }
	for (int i = 0; i < PEOPLE; i++) {
		wsk_place_t *p = &S->place[POSTERS + i], *t = &S->place[POSTERS + PEOPLE + i];
		int d = 0, sc, fade;
		if (S->me >= 0) {
			int dx = iabs(px[i] - px[S->me]), dy = iabs(py[i] - py[S->me]);
			d = (dx > dy) ? dx + dy / 2 : dy + dx / 2;          /* near enough a distance */
		}
		sc = (d < 300) ? 256 : 256 * 300 / d;
		fade = (d < 300) ? 0 : (d - 300) / 4;
		if (sc < 96) sc = 96;
		if (fade > 210) fade = 210;
		p->x = px[i]; p->y = py[i];
		t->scale = (short)sc;
		t->fade = (unsigned char)fade;
		t->x = px[i] + PW_ / 2 - (SW_ * sc / 256) / 2;          /* centred over them */
		t->y = py[i] - SH_ * sc / 256 - 4;
	}
}

/* Who or what a person would touch standing at x, y: a poster or another
   person -- not what anyone says, which floats over heads. -1 for nobody. */
static int bumps(int i, int x, int y)
{
	int out[16], n = wsk_world_in(&world, x, y, PW_, PH_, out, 16);
	for (int k = 0; k < n && k < 16; k++)
		if (out[k] != POSTERS + i && out[k] < POSTERS + PEOPLE) return out[k];
	return -1;
}

static void model_people(void)
{
	for (int i = 0; i < PEOPLE; i++) { model[POSTERS + i].x = px[i]; model[POSTERS + i].y = py[i]; }
}

/* Everyone a step toward where they are going; those no seat walks choose
   somewhere new when they get there, or when someone is in the way. 1 if
   anyone moved. */
static int step(void)
{
	int moved = 0;
	for (int i = 0; i < PEOPLE; i++) {
		int o = owner[i], v = (o >= 0) ? 10 : 3, dx = tx[i] - px[i], dy = ty[i] - py[i], nx, ny, b;
		if (dx == 0 && dy == 0) {
			if (o >= 0 || (i == 0 && max_waits) || rnd(40) != 0) continue;
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
			if (o >= 0) {
				char m[64];
				if (b < POSTERS) snprintf(m, sizeof m, "You reach \"%s\".", TITLES[b]);
				else snprintf(m, sizeof m, "You meet %s.", NAMES[b - POSTERS]);
				say(o, m);
				printf("bump   %s\n", m);
				fflush(stdout);
				moved = 1;
			}
			continue;
		}
		px[i] = nx; py[i] = ny;
		/* where they are now, before the next person looks: two walking
		   toward each other must not both step into the same frame's gap */
		model[POSTERS + i].x = nx; model[POSTERS + i].y = ny;
		moved = 1;
		if (o >= 0 && px[i] == tx[i] && py[i] == ty[i]) say(o, "You arrive.");
	}
	if (moved) model_people();
	return moved;
}

static void to_window(wsk_sink_t *s, const char *b, int n) { (void)s; wh_write(b, n); }

/*
 * Rows y0..y1 of seat st's window, as one document if it fits the 4 KB a
 * window on RV-9's /w0 takes (design §2.3) -- and, zoomed out with the whole
 * floor in view, it does not: then the rows in halves, on 8-row bands, each
 * a whole document of what crosses them. whisker refuses an oversize
 * document before writing a byte (WSK_E_BUDGET), so a refusal costs only
 * the count.
 */
static void draw(int st, wsk_sink_t *s, int y0, int y1)
{
	int mid = ((y0 + y1) / 2) & ~7;
	wh_to(st);
	wh_clip(y0, y1);
	s->len = 0;
	if (wsk_render_rows(&seat[st].win, y0, y1, s, 4096) != WSK_E_BUDGET) return;
	if (mid <= y0 || mid >= y1) return;      /* one band too dense: nothing to split */
	if (!split_told) { split_told = 1; printf("bands  a frame over 4 KB goes in parts\n"); }
	draw(st, s, y0, mid);
	draw(st, s, mid, y1);
}

/* A screen arrives: its trees over the shared widgets, and a person. */
static void join(int s, int caps)
{
	seat_t *S = &seat[s];
	int y0, y1;

	/* a seat joining again whose leaving was never heard gives its person
	   back first, or they would be owned by nobody for ever */
	if (S->on && S->me >= 0 && owner[S->me] == s) owner[S->me] = -1;
	memset(S, 0, sizeof *S);
	S->on = 1;
	S->me = -1;
	for (int i = 0; i < PEOPLE && S->me < 0; i++)
		if (owner[i] < 0) S->me = i;
	if (S->me >= 0) {
		owner[S->me] = s;
		tx[S->me] = px[S->me]; ty[S->me] = py[S->me];     /* stops wandering */
		if (S->me == 0 && !checking) max_waits = 0;
	}
	for (int i = 0; i < ITEMS; i++) S->place[i] = model[i];
	for (int i = 0; i < POSTERS; i++) S->strs[i] = ABSTRACTS[i];
	S->strs[POSTERS] = S->status;
	memcpy(S->win_items, WIN, sizeof WIN);
	wsk_init(&S->win, WSK_COL, DEV_W, DEV_H);
	S->win.pad = 4; S->win.gap = 0; S->win.ids = 1;
	S->win.item = S->win_items; S->win.n = 2;
	S->win.nest = S->nest; S->win.nests = TREES;
	S->win.str = S->strs; S->win.strs = POSTERS + 1;
	S->win.caps = (unsigned char)caps;
	S->win.talign = WSK_ALIGN_START;
	wsk_nest_init(&S->nest[0], &S->win, WSK_COL);
	S->nest[0].pad = 0;
	S->nest[0].bg = 0x1a2a20u;
	S->nest[0].item = items; S->nest[0].n = ITEMS;
	S->nest[0].overflow = WSK_WORLD; S->nest[0].place = S->place;
	S->nest[0].order = order;
	S->nest[0].wspace = WSK_BOUNDED; S->nest[0].ww = FLOOR_W; S->nest[0].wh = FLOOR_H;
	S->nest[0].vx = 100; S->nest[0].vy = 0;              /* the hall's door */
	S->win_items[1].fill = S->nest[0].bg;
	for (int i = 0; i < POSTERS; i++) {
		wsk_tree_t *p = &S->nest[1 + 2 * i], *a = &S->nest[2 + 2 * i];
		wsk_nest_init(p, &S->win, WSK_COL);
		p->item = poster[i]; p->n = 3; p->pad = 6; p->gap = 4;
		wsk_nest_init(a, &S->win, WSK_ROW);
		a->item = abstract[i]; a->n = 1; a->overflow = WSK_WRAP;
		a->pad = 0; a->gap = 8; a->lgap = 0; a->talign = WSK_ALIGN_START;
	}
	/* Max starts in view of the door, so that seat follows at once; a
	   later one starts at the door, looking, until its person walks */
	S->following = (S->me == 0);
	place_for(s);
	if (S->me >= 0) {
		char m[96];
		snprintf(m, sizeof m, "You are %s.   drag: look   wheel: zoom   click: walk   point: read",
		         NAMES[S->me]);
		say(s, m);
	} else say(s, "The floor is full: you are watching.");
	wsk_ui_init(&S->ui, &S->win);
	wsk_ui_focus(&S->ui, "floor", &y0, &y1);       /* the keys look around from the start */
	if (!checking) {
		printf("join   seat %d is %s\n", s, S->me >= 0 ? NAMES[S->me] : "watching");
		fflush(stdout);
	}
}

/* A seat's screen is w x h (§5ab step 2): its window that size, and its
   view asked for again -- a wider window may see past the floor's edge at
   the zoom it had, and wsk_world_view gives the nearest view allowed. */
static void resize(int s, int w, int h)
{
	seat_t *S = &seat[s];
	int y0, y1;
	S->win.w = (short)w; S->win.h = (short)h;
	wsk_area(&S->win, 0, 0, w, h);
	wsk_world_view(&S->win, "floor", S->nest[0].vx, S->nest[0].vy, S->nest[0].zoom, &y0, &y1);
	printf("size   seat %d is %dx%d, its view %d,%d at %d/256\n", s, w, h,
	       S->nest[0].vx, S->nest[0].vy, S->nest[0].zoom);
	fflush(stdout);
}

/* The tip for what seat st rests on (§5ac): the program's words, worked
   out when asked -- a person's distance changes as they walk. 0, none. */
static const char *tip_for(int st, const wsk_tree_t *in, int item)
{
	seat_t *S = &seat[st];
	const wsk_tree_t *fl = &S->nest[0];
	int j = -1;
	if (in == fl) j = item;
	else if (in > fl && in < &S->nest[TREES]) {
		int p = (int)((in - &S->nest[1]) / 2);
		if (in == &S->nest[1 + 2 * p] && item == 2)
			snprintf(S->tip, sizeof S->tip, "Visit \"%s\"", TITLES[p]);
		else
			snprintf(S->tip, sizeof S->tip, "%s: %s", TITLES[p], ABSTRACTS[p]);
		return S->tip;
	}
	if (j >= 0 && j < POSTERS) snprintf(S->tip, sizeof S->tip, "%s: %s", TITLES[j], ABSTRACTS[j]);
	else if (j >= POSTERS && j < POSTERS + PEOPLE) {
		int i = j - POSTERS;
		if (i == S->me) snprintf(S->tip, sizeof S->tip, "%s (you)", NAMES[i]);
		else if (S->me < 0) snprintf(S->tip, sizeof S->tip, "%s", NAMES[i]);
		else {
			int dx = iabs(px[i] - px[S->me]), dy = iabs(py[i] - py[S->me]);
			int d = (dx > dy) ? dx + dy / 2 : dy + dx / 2;       /* 50 to a metre */
			snprintf(S->tip, sizeof S->tip, "%s, %d m away", NAMES[i], d / 50);
		}
	} else return 0;
	return S->tip;
}

/* A screen goes: its person walks on by themselves. */
static void gone(int s)
{
	seat_t *S = &seat[s];
	if (!S->on) return;
	if (S->me >= 0) owner[S->me] = -1;
	printf("gone   seat %d, %s walks on\n", s, S->me >= 0 ? NAMES[S->me] : "nobody");
	fflush(stdout);
	S->on = 0;
}

int main(void)
{
	int y0, y1, caps = wh_caps();
	long last;
	wsk_sink_t s;

	/* posters along the top wall: stacks, laid out at the floor's zoom */
	for (int i = 0; i < POSTERS; i++) {
		wsk_widget_t *it = &items[i], *p = poster[i];
		it->kind = WSK_STACK;
		snprintf(it->id, WSK_ID_MAX, "p%d", i);
		it->sub = (short)(1 + 2 * i);
		it->fill = 0x2a3a50u;
		okx[i] = 120 + i * 380; oky[i] = 20;
		model[i].x = okx[i]; model[i].y = oky[i];
		model[i].w = 300; model[i].h = 116; model[i].scale = 256;
		model[i].move = 1;                                       /* arranged by hand */
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
		model[POSTERS + i].w = PW_; model[POSTERS + i].h = PH_; model[POSTERS + i].scale = 256;
		t->kind = WSK_LABEL;
		snprintf(t->id, WSK_ID_MAX, "s%d", i);
		snprintf(t->text, WSK_TEXT_MAX, "%.31s", SAYS[i]);
		model[POSTERS + PEOPLE + i].w = SW_; model[POSTERS + PEOPLE + i].h = SH_;
		model[POSTERS + PEOPLE + i].scale = 256;
		px[i] = tx[i] = (i == 0) ? 500 : 150 + rnd(FLOOR_W - 300);
		py[i] = ty[i] = (i == 0) ? 300 : 280 + rnd(FLOOR_H - 380);
		owner[i] = -1;
	}
	model_people();
	for (int i = 0; i < ITEMS; i++) order[i] = (short)i;
	wsk_init(&world, WSK_COL, DEV_W, DEV_H);
	world.item = items; world.n = ITEMS;
	world.overflow = WSK_WORLD; world.place = model; world.order = order;
	world.wspace = WSK_BOUNDED; world.ww = FLOOR_W; world.wh = FLOOR_H;

	/* a seat's trees, checked before any screen arrives: one refused
	   would be refused for every seat */
	{
		const wsk_tree_t *in = 0;
		int which = -1, e;
		checking = 1;
		join(0, caps);
		e = wsk_check_in(&seat[0].win, &which, &in);
		owner[seat[0].me] = -1;
		seat[0].on = 0;
		checking = 0;
		if (e != WSK_OK) {
			fprintf(stderr, "floor: refused (%d) at %d\n", e, which);
			return 1;
		}
	}
	if (wh_open(DEV_W, DEV_H, 1, "whisker -- a conference floor") != 0) return 1;
	wh_seats(1);
	s.put = to_window; s.ctx = 0; s.len = 0;
	last = wh_ms();

	for (;;) {
		wh_event_t he;
		wsk_event_t ev;
		wsk_note_t note[4];
		int k = wh_poll(&he), n, all = 0, everyone = 0, status_only = 0, st;
		seat_t *S;

		if (k == WH_QUIT) break;
		if (k == WH_NOTHING) {
			/* a frame -- every 40 ms by the clock, not every time the host
			   wakes: a mouse moving wakes it a hundred times a second, and
			   people would walk that much faster */
			int moved, ms = (int)(wh_ms() - last);
			if (ms < 40) { wh_wait(40 - ms); continue; }
			last += ms;
			moved = step();
			for (int q = 0; q < WH_SEATS; q++) {
				int fy0, fy1, again = moved, tn, ty0 = 0, ty1 = 0;
				wsk_event_t tick;
				wsk_note_t tnote[2];
				if (!seat[q].on) continue;
				/* time passing, for tips (§5ac): one rested long enough asks */
				memset(&tick, 0, sizeof tick);
				tick.kind = WSK_EV_TICK;
				tick.d = (short)((ms > 30000) ? 30000 : ms);
				tn = wsk_feed(&seat[q].ui, &tick, tnote, 2, &ty0, &ty1);
				for (int i = 0; i < tn && i < 2; i++)
					if (tnote[i].kind == WSK_NOTE_TIP) {
						const char *t = tip_for(q, tnote[i].in, tnote[i].item);
						if (t != 0 && wsk_ui_tip(&seat[q].ui, t, &ty0, &ty1) == WSK_OK) {
							printf("tip    %s\n", t);
							fflush(stdout);
						}
					}
				if (moved) place_for(q);
				/* the view glides after its person: 30% margin, a quarter
				   of the way a frame */
				if (seat[q].following && seat[q].me >= 0 &&
				    wsk_world_follow(&seat[q].win, "floor", POSTERS + seat[q].me, 30, 64, &fy0, &fy1) == 1)
					again = 1;
				if (again) draw(q, &s, 0, seat[q].win.h);
				else if (ty1 > ty0) draw(q, &s, ty0, ty1);    /* a tip opened */
			}
			continue;
		}
		st = he.seat;
		if (st < 0 || st >= WH_SEATS) continue;
		if (k == WH_JOIN) { join(st, caps); draw(st, &s, 0, seat[st].win.h); continue; }
		if (k == WH_GONE) { gone(st); continue; }
		S = &seat[st];
		if (!S->on) continue;
		if (k == WH_EXPOSE) { draw(st, &s, 0, seat[st].win.h); continue; }
		if (k == WH_SIZE) { resize(st, he.x, he.y); draw(st, &s, 0, seat[st].win.h); continue; }
		memset(&ev, 0, sizeof ev);
		ev.x = (short)he.x; ev.y = (short)he.y;
		ev.finger = (unsigned char)he.finger;           /* two on the floor pinch it (§5ad) */
		if (k == WH_PRESS) { ev.kind = WSK_EV_DOWN; ev.key = (unsigned short)he.key; ev.d = (short)he.d; }
		else if (k == WH_RELEASE) ev.kind = WSK_EV_UP;
		else if (k == WH_MOVE) ev.kind = WSK_EV_MOVE;
		else if (k == WH_WHEEL) { ev.kind = WSK_EV_WHEEL; ev.d = (short)he.d; }
		else if (k == WH_KEY) { ev.kind = WSK_EV_KEY; ev.key = (unsigned short)he.key; }
		else if (k == WH_TEXT) { ev.kind = WSK_EV_TEXT; memcpy(ev.text, he.text, sizeof ev.text); }
		else if (k == WH_HOVER) ev.kind = WSK_EV_HOVER;
		else continue;

		n = wsk_feed(&S->ui, &ev, note, 4, &y0, &y1);
		for (int i = 0; i < n && i < 4; i++) {
			const wsk_note_t *t = &note[i];
			const wsk_tree_t *fl = &S->nest[0];
			char m[96];
			if (t->kind == WSK_NOTE_CLICK && t->in == fl && t->item >= 0) {
				int j = t->item;
				if (j == POSTERS + S->me) S->following = 1;    /* a click on yourself */
				if (j < POSTERS) snprintf(m, sizeof m, "%s: %s", TITLES[j], ABSTRACTS[j]);
				else if (j < POSTERS + PEOPLE) {
					j -= POSTERS;
					snprintf(m, sizeof m, (j != S->me) ? "%s: \"%s\"" : "%s waves.", NAMES[j], SAYS[j]);
				} else
					snprintf(m, sizeof m, "%s", SAYS[j - POSTERS - PEOPLE]);
				say(st, m);
				printf("meet   %s\n", m);
				all = 1;
			} else if (t->kind == WSK_NOTE_CLICK && t->in > fl && t->in < &S->nest[TREES]) {
				/* inside a poster: its Visit button, or anything else on it */
				int j = (int)((t->in - &S->nest[1]) / 2);
				if (t->in == &S->nest[1 + 2 * j] && t->item == 2)
					snprintf(m, sizeof m, "You visit \"%s\".", TITLES[j]);
				else
					snprintf(m, sizeof m, "%s: %s", TITLES[j], ABSTRACTS[j]);
				say(st, m);
				printf("poster %s\n", m);
				all = 1;
			} else if (t->kind == WSK_NOTE_CLICK && t->in == &S->win && t->item == 1) {
				/* bare floor: walk there (step 3) */
				int wx, wy;
				if (S->me >= 0 && wsk_world_point(&S->win, ev.x, ev.y, 0, &wx, &wy) == WSK_OK) {
					tx[S->me] = wx - PW_ / 2; ty[S->me] = wy - PH_ / 2;
					if (tx[S->me] < 0) tx[S->me] = 0;
					if (ty[S->me] < 0) ty[S->me] = 0;
					printf("walk   %s to %d,%d\n", NAMES[S->me], wx, wy);
					S->following = 1;
					say(st, "Walking...");
					all = 1;
				}
			} else if (t->kind == WSK_NOTE_HOVER) {
				/* what the pointer rests on: a poster -- anywhere on it --
				   gives its abstract, a person their name; anything else
				   leaves the line as it was */
				int j = -1;
				m[0] = '\0';
				if (t->in == fl && t->item >= 0) j = t->item;
				else if (t->in > fl && t->in < &S->nest[TREES])
					j = (int)((t->in - &S->nest[1]) / 2);
				if (j >= 0 && j < POSTERS) snprintf(m, sizeof m, "%s: %s", TITLES[j], ABSTRACTS[j]);
				else if (j >= POSTERS && j < POSTERS + PEOPLE) snprintf(m, sizeof m, "%s", NAMES[j - POSTERS]);
				if (m[0] != '\0') {
					say(st, m);
					printf("hover  %s\n", m);
					status_only = 1;
				}
			} else if (t->kind == WSK_NOTE_RAISE) {
				/* a poster to the front of the posters -- but people and their
				   words stay above every poster: this program's layering. The
				   order is everyone's, so every seat is drawn again. */
				for (int j = POSTERS; j < ITEMS; j++) wsk_world_raise(&S->nest[0], j);
				printf("raise  p%d\n", t->item);
				everyone = 1;
			} else if (t->kind == WSK_NOTE_MOVE && t->item < POSTERS) {
				/* the same rule as walking, the other way round: a poster
				   that would touch a person goes back to where it touched
				   no one (whisker says what touches, §5q step 4) */
				int j = t->item, out[32], c, who = -1;
				model[j].x = S->place[j].x; model[j].y = S->place[j].y;
				c = wsk_world_overlaps(&world, j, out, 32);
				for (int q = 0; q < c && q < 32; q++)
					if (out[q] >= POSTERS && out[q] < POSTERS + PEOPLE) { who = out[q] - POSTERS; break; }
				if (who >= 0) {
					model[j].x = S->place[j].x = okx[j];
					model[j].y = S->place[j].y = oky[j];
					printf("block  p%d by %s\n", j, NAMES[who]);
				} else {
					okx[j] = model[j].x; oky[j] = model[j].y;
					printf("move   p%d to %d,%d\n", j, okx[j], oky[j]);
				}
				everyone = 1;
			} else if (t->kind == WSK_NOTE_VIEW) {
				/* the person looked around: stop following -- unless it was a zoom */
				if (ev.kind == WSK_EV_MOVE || ev.kind == WSK_EV_KEY) {
					if (S->following) printf("follow off\n");
					S->following = 0;
				}
				printf("view   %d,%d at %d/256\n", S->nest[0].vx, S->nest[0].vy, t->value);
			}
		}
		fflush(stdout);
		if (everyone) {
			/* what every seat sees changed: a poster moved or rose */
			for (int q = 0; q < WH_SEATS; q++)
				if (seat[q].on) { place_for(q); draw(q, &s, 0, seat[q].win.h); }
		} else if (all) draw(st, &s, 0, seat[st].win.h);
		else {
			if (status_only) draw(st, &s, 0, 40);          /* the status line's rows */
			if (y1 > y0) draw(st, &s, y0, y1);             /* and whisker's: a tip closed */
		}
	}
	wh_close();
	return 0;
}
