/*
 * Typing, on a desktop: whisker's toolkit with a real keyboard and mouse.
 *
 *     ./run.sh --edit                1024x600, the P4's panel
 *     ./run.sh --edit 800 480 1      something else
 *     host/web/run.sh                the same program, in a browser (§5k)
 *
 * design.md §5j. Three fields in a form and a list beside them that scrolls.
 * Click a field and type; Tab and Shift-Tab move between fields, buttons and
 * the check (Enter presses a focused button or ticks the check); the
 * arrows, Home, End, Backspace and Delete edit; Enter prints the field. Drag
 * the list, or use the wheel over it. Save opens a menu (§5l): choose from
 * it, or tap anywhere else to close it. Escape quits -- or, in a browser,
 * closes the menu.
 *
 * Compare input.c, which is §5d's loop: there the APPLICATION found the
 * key, appended the character and worked out the rows. Here every event goes
 * to wsk_feed and this program only translates the host's events, prints the
 * notices and repaints the rows it is told -- which is the whole of what a
 * host loop should be (§8.2's layer 3).
 */
#include "host.h"
#include "whisker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const wsk_widget_t WIN[] = {
	{ .kind = WSK_LABEL, .id = "title", .text = "whisker -- type into a field", .size = 48 },
	{ .kind = WSK_STACK, .id = "body",  .sub = 0, .weight = 1 },
	{ .kind = WSK_TEXT,  .id = "help",  .sub = 0, .size = 40 },   /* longer than a label holds */
};
static const wsk_widget_t BODY[] = {
	{ .kind = WSK_STACK, .id = "form", .sub = 1, .weight = 2 },
	{ .kind = WSK_STACK, .id = "list", .sub = 2, .weight = 1 },
};
static const wsk_widget_t FORM[] = {
	{ .kind = WSK_LABEL,  .id = "l1",   .text = "Name", .size = 32 },
	{ .kind = WSK_FIELD,  .id = "name", .sub = 0, .size = 48 },
	{ .kind = WSK_LABEL,  .id = "l2",   .text = "City", .size = 32 },
	{ .kind = WSK_FIELD,  .id = "city", .sub = 1, .size = 48 },
	{ .kind = WSK_LABEL,  .id = "l3",   .text = "Note", .size = 32 },
	{ .kind = WSK_MEMO,   .id = "note", .sub = 2, .size = 96 },   /* many lines (§5o) */
	{ .kind = WSK_BUTTON, .id = "save", .text = "Save" },
	{ .kind = WSK_CHECK,  .id = "remember", .text = "Remember me", .sub = 0, .size = 36 },
};

static const wsk_widget_t SAVEMENU[] = {
	{ .kind = WSK_BUTTON, .id = "save-now", .text = "Save now", .size = 44 },
	{ .kind = WSK_BUTTON, .id = "save-as",  .text = "Save as...", .size = 44 },
	{ .kind = WSK_BUTTON, .id = "discard",  .text = "Discard", .size = 44 },
};

/* The list is a table (§5n): a header that stays, over a body that
   scrolls, whose rows put their cells in the header's columns. */
static const wsk_widget_t LIST[] = {
	{ .kind = WSK_STACK, .id = "head",  .sub = 4, .size = 40 },
	{ .kind = WSK_STACK, .id = "table", .sub = 5, .weight = 1 },
};
static const wsk_widget_t HEAD[] = {
	{ .kind = WSK_BUTTON, .id = "h-name", .text = "Name", .weight = 1, .fill = 0x1c3550u },
	{ .kind = WSK_BUTTON, .id = "h-size", .text = "Size", .size = 90,  .fill = 0x1c3550u },
};
static wsk_widget_t items[30], cells[30][2];
static char name[40], city[40], note[80];
static unsigned char state[1];          /* Remember me, ticked or not (§5m) */
static const char *strs[1] = { "Tab between fields, Enter prints a field, Esc quits" };
static wsk_buf_t buf[3];
static wsk_tree_t win, nest[6 + 30];

static void to_window(wsk_sink_t *s, const char *b, int n) { (void)s; wh_write(b, n); }

int main(int argc, char **argv)
{
	int w = (argc > 2) ? atoi(argv[1]) : 1024;
	int h = (argc > 2) ? atoi(argv[2]) : 600;
	int scale = (argc > 3) ? atoi(argv[3]) : 1;
	wsk_sink_t s;
	wsk_ui_t ui;
	int y0, y1, which = -1, e;
	const wsk_tree_t *in = 0;

	for (int i = 0; i < 30; i++) {
		items[i].kind = WSK_STACK;              /* a row of the table */
		items[i].sub = (short)(6 + i);
		items[i].size = 40;
		items[i].fill = (i % 2) ? 0x1a3048u : 0x15283du;
		snprintf(items[i].id, WSK_ID_MAX, "it%d", i);
		cells[i][0].kind = WSK_LABEL;
		snprintf(cells[i][0].id, WSK_ID_MAX, "n%d", i);
		snprintf(cells[i][0].text, WSK_TEXT_MAX, "Item %d", i);
		cells[i][1].kind = WSK_LABEL;
		snprintf(cells[i][1].id, WSK_ID_MAX, "s%d", i);
		snprintf(cells[i][1].text, WSK_TEXT_MAX, "%d KB", (i * 37) % 900 + 1);
	}
	buf[0].b = name; buf[0].cap = sizeof name;
	buf[1].b = city; buf[1].cap = sizeof city;
	buf[2].b = note; buf[2].cap = sizeof note;

	wsk_init(&win, WSK_COL, w, h);
	win.pad = 0; win.gap = 0; win.font = 20; win.ids = 1;
	win.item = WIN; win.n = 3;
	win.nest = nest; win.nests = 6 + 30;
	win.buf = buf; win.bufs = 3;
	win.str = strs; win.strs = 1;
	win.state = state; win.states = 1;
	wsk_nest_init(&nest[0], &win, WSK_ROW);
	nest[0].pad = 8; nest[0].gap = 16; nest[0].item = BODY; nest[0].n = 2;
	wsk_nest_init(&nest[1], &win, WSK_COL);
	nest[1].pad = 0; nest[1].gap = 6; nest[1].item = FORM; nest[1].n = 8;
	wsk_nest_init(&nest[2], &win, WSK_COL);
	nest[2].pad = 0; nest[2].gap = 4; nest[2].item = LIST; nest[2].n = 2;
	wsk_nest_init(&nest[4], &win, WSK_ROW);     /* the header */
	nest[4].pad = 4; nest[4].gap = 2; nest[4].item = HEAD; nest[4].n = 2;
	nest[4].talign = WSK_ALIGN_START;
	wsk_nest_init(&nest[5], &win, WSK_COL);     /* the body: scrolls, chooses */
	nest[5].pad = 0; nest[5].gap = 2; nest[5].item = items; nest[5].n = 30;
	nest[5].overflow = WSK_SCROLL;
	nest[5].select = 1;
	nest[5].head = 4;                           /* its rows follow the header */
	for (int i = 0; i < 30; i++) {
		wsk_nest_init(&nest[6 + i], &win, WSK_ROW);
		nest[6 + i].pad = 4; nest[6 + i].gap = 2;
		nest[6 + i].talign = WSK_ALIGN_START;
		nest[6 + i].item = cells[i]; nest[6 + i].n = 2;
	}
	wsk_nest_init(&nest[3], &win, WSK_COL);    /* the menu Save opens */
	nest[3].pad = 6; nest[3].gap = 4; nest[3].bg = 0x2a3a4au;
	nest[3].item = SAVEMENU; nest[3].n = 3;

	if ((e = wsk_check_in(&win, &which, &in)) != WSK_OK) {
		fprintf(stderr, "edit: the window was refused, %d at %s\n", e,
		        (in != 0 && which >= 0) ? in->item[which].id : "a stack");
		return 1;
	}
	if (wh_open(w, h, scale, "whisker -- edit") != 0) return 1;
	s.put = to_window; s.ctx = 0; s.len = 0;
	wsk_ui_init(&ui, &win);
	wsk_ui_focus(&ui, "name", &y0, &y1);
	wh_clip(0, 0);
	wsk_render(&win, &s, 0);

	for (;;) {
		wh_event_t he;
		wsk_event_t ev;
		wsk_note_t note_[4];
		int k = wh_poll(&he), n;

		if (k == WH_QUIT) break;
		if (k == WH_NOTHING) { wh_wait(20); continue; }
		if (k == WH_EXPOSE) {                 /* a new screen: all of it */
			wh_clip(0, 0);
			wsk_render(&win, &s, 0);
			continue;
		}
		memset(&ev, 0, sizeof ev);
		ev.x = (short)he.x; ev.y = (short)he.y;
		if (k == WH_PRESS) ev.kind = WSK_EV_DOWN;
		else if (k == WH_RELEASE) ev.kind = WSK_EV_UP;
		else if (k == WH_MOVE) ev.kind = WSK_EV_MOVE;
		else if (k == WH_WHEEL) { ev.kind = WSK_EV_WHEEL; ev.d = (short)he.d; }
		else if (k == WH_KEY) { ev.kind = WSK_EV_KEY; ev.key = (unsigned short)he.key; }
		else if (k == WH_TEXT) { ev.kind = WSK_EV_TEXT; memcpy(ev.text, he.text, sizeof ev.text); }
		else continue;

		n = wsk_feed(&ui, &ev, note_, 4, &y0, &y1);
		for (int i = 0; i < n && i < 4; i++) {
			const wsk_note_t *t = &note_[i];
			if (t->kind == WSK_NOTE_CLICK) {
				int m0, m1;
				printf("click  %s\n", t->in->item[t->item].id);
				/* Save opens its menu; the menu's own buttons close it */
				if (strcmp(t->in->item[t->item].id, "save") == 0 &&
				    wsk_ui_open(&ui, 3, "save", &m0, &m1) == WSK_OK) {
					if (y1 <= y0) { y0 = m0; y1 = m1; }
					if (m0 < y0) y0 = m0;
					if (m1 > y1) y1 = m1;
				}
			} else if (t->kind == WSK_NOTE_CLOSE)
				printf("close  the menu\n");
			else if (t->kind == WSK_NOTE_CHANGE)
				printf("change %s = %d\n", t->in->item[t->item].id, t->value);
			else if (t->kind == WSK_NOTE_SELECT)
				printf("select %s\n", t->in->item[t->item].id);
			else if (t->kind == WSK_NOTE_ENTER)
				printf("enter  %s = \"%s\"\n", t->in->item[t->item].id,
				       buf[t->in->item[t->item].sub].b);
			else if (t->kind == WSK_NOTE_FOCUS)
				printf("focus  %s\n", t->in->item[t->item].id);
		}
		fflush(stdout);
		if (y1 > y0) {
			wh_clip(y0, y1);
			wsk_render_rows(&win, y0, y1, &s, 0);
		}
	}
	printf("name=\"%s\" city=\"%s\" note=\"%s\"\n", name, city, note);
	wh_close();
	return 0;
}
