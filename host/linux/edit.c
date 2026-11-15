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
 * it, or tap anywhere else to close it; Escape closes it too.
 *
 * And a desktop's plumbing (§5s): Shift with the arrows, or a drag, selects
 * in a field; Ctrl-A, C, X and V are the clipboard -- the system's, through
 * the host; Ctrl-Z undoes and Ctrl-Y redoes (§5v). A right-click on a field
 * opens a menu of the same four, each of
 * which feeds the toolkit the same key, so a menu command and its shortcut
 * cannot disagree. Ctrl-S is Save, the program's own shortcut. Close the
 * window to quit.
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
	{ .kind = WSK_IMAGE,  .id = "logo", .sub = 1, .size = 56 },   /* only where a host can (§5r) */
};

/* the menu a right-click on a field opens (§5s): each item is a key */
static const wsk_widget_t CTXMENU[] = {
	{ .kind = WSK_BUTTON, .id = "cx-cut",   .text = "Cut",        .size = 40 },
	{ .kind = WSK_BUTTON, .id = "cx-copy",  .text = "Copy",       .size = 40 },
	{ .kind = WSK_BUTTON, .id = "cx-paste", .text = "Paste",      .size = 40 },
	{ .kind = WSK_BUTTON, .id = "cx-all",   .text = "Select all", .size = 40 },
};
static const unsigned short CTXKEY[] = {
	'x' | WSK_MOD_CTRL, 'c' | WSK_MOD_CTRL, 'v' | WSK_MOD_CTRL, 'a' | WSK_MOD_CTRL,
};
#define CTX 36                          /* its index in nest */

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
static int caps;                        /* what the host said it can draw (§5r) */

/* The table is a FILE TREE (§5p), and its expansion is this program's, as
   Studio's is: the model below is ours, and what whisker is handed is the
   flat list of rows we show, each with its depth. A tap selects a row; on a
   folder we open or close it and hand over the rows again. */
typedef struct { const char *name, *size; unsigned char depth, kids, open; } node_t;
static node_t NODES[] = {
	{ "src", "-", 0, 1, 1 },
	{ "whisker.h", "140K", 1, 0, 0 }, { "test.c", "90K", 1, 0, 0 },
	{ "picture.c", "60K", 1, 0, 0 },  { "run.sh", "8K", 1, 0, 0 },
	{ "modules", "-", 1, 1, 0 },
	{ "module_keys.c", "12K", 2, 0, 0 }, { "module_piano.c", "14K", 2, 0, 0 },
	{ "module_scroll.c", "10K", 2, 0, 0 },
	{ "docs", "-", 0, 1, 0 },
	{ "design.md", "180K", 1, 0, 0 }, { "decisions.md", "30K", 1, 0, 0 },
	{ "roadmap.md", "40K", 1, 0, 0 },  { "window-gap.md", "20K", 1, 0, 0 },
	{ "host", "-", 0, 1, 0 },
	{ "linux", "-", 1, 1, 0 },
	{ "edit.c", "7K", 2, 0, 0 }, { "host_sdl.c", "8K", 2, 0, 0 },
	{ "web", "-", 1, 1, 0 },
	{ "host_web.c", "12K", 2, 0, 0 }, { "chrome.mjs", "6K", 2, 0, 0 },
	{ "tools", "-", 0, 1, 0 },
	{ "evidence", "-", 1, 1, 0 },
	{ "stack", "4K", 2, 0, 0 }, { "keys", "6K", 2, 0, 0 }, { "piano", "7K", 2, 0, 0 },
	{ "README.md", "12K", 0, 0, 0 },
	{ "CLAUDE.md", "30K", 0, 0, 0 },
};
#define NN ((int)(sizeof NODES / sizeof NODES[0]))
static int rownode[30];

/* The rows to show: every node not under a closed folder, a folder's
   arrow drawn as a mark (§5t) -- a shape, so every host draws it. */
static int tree_rows(void)
{
	int n = 0, hide = 255;
	for (int i = 0; i < NN && n < 30; i++) {
		node_t *t = &NODES[i];
		if (t->depth > hide) continue;
		hide = 255;
		items[n].depth = t->depth;
		items[n].fill = (n % 2) ? 0x1a3048u : 0x15283du;
		cells[n][0].mark = !t->kids ? WSK_MARK_LEAF : t->open ? WSK_MARK_OPEN : WSK_MARK_SHUT;
		snprintf(cells[n][0].text, WSK_TEXT_MAX, "%s", t->name);
		snprintf(cells[n][1].text, WSK_TEXT_MAX, "%s", t->size);
		rownode[n++] = i;
		if (t->kids && !t->open) hide = t->depth;
	}
	return n;
}
static char name[40], city[40], note[400];
static unsigned char state[1];          /* Remember me, ticked or not (§5m) */
static const char *strs[2] = { "Tab between fields, Enter prints a field, right-click a field for Copy and Paste",
                               "/static/logo.svg" };
/* the same help where a host draws UTF-8 (§5r): a real dash and arrow */
static const char HELP_UTF8[] = "Tab between fields \xe2\x80\x94 Enter prints a field \xe2\x80\x94 "
                                "right-click a field \xe2\x96\xb8 Copy, Paste";
static wsk_buf_t buf[3];
static char undo_b[3][2048];            /* what Ctrl-Z undoes, per field (§5v) */
static wsk_undo_t undo[3];
static wsk_tree_t win, nest[6 + 30 + 1];
static wsk_ui_t ui;
static char clip[128];                  /* the toolkit's copy, for the host (§5s) */
static int H;

static void to_window(wsk_sink_t *s, const char *b, int n) { (void)s; wh_write(b, n); }

/* Rows a..b joined to those already to repaint. */
static void also(int *y0, int *y1, int a, int b)
{
	if (b <= a) return;
	if (*y1 <= *y0) { *y0 = a; *y1 = b; return; }
	if (a < *y0) *y0 = a;
	if (b > *y1) *y1 = b;
}

static int is_field(const wsk_note_t *t)
{
	return t->item >= 0 && (t->in->item[t->item].kind == WSK_FIELD ||
	                        t->in->item[t->item].kind == WSK_MEMO);
}

/*
 * One event to the toolkit, and what it says changed, acted on: printed, a
 * menu opened, the clipboard used. A menu's command feeds its key back in
 * here, so it is handled exactly as the key would be.
 */
static void feed(const wsk_event_t *ev, int *y0, int *y1)
{
	wsk_note_t note_[4];
	int n = wsk_feed(&ui, ev, note_, 4, y0, y1), m0, m1;

	for (int i = 0; i < n && i < 4; i++) {
		const wsk_note_t *t = &note_[i];
		const char *id = (t->item >= 0) ? t->in->item[t->item].id : "nothing";
		if (t->kind == WSK_NOTE_CLICK) {
			/* the tree row clicked: a cell of it, or the row itself */
			int row = (t->in >= &nest[6] && t->in < &nest[6 + 30]) ? (int)(t->in - &nest[6])
			        : (t->in == &nest[5]) ? t->item : -1;
			printf("click  %s\n", id);
			/* A folder opens or closes on every click. On a CLICK, not a
			   SELECT: a click on the row already chosen selects nothing new,
			   and a toggle that waited for a selection did nothing there --
			   Matthew found it, 2026-11-14. */
			if (row >= 0 && row < nest[5].n && NODES[rownode[row]].kids) {
				node_t *f = &NODES[rownode[row]];
				f->open = !f->open;
				printf("%s  %s\n", f->open ? "open " : "close", f->name);
				nest[5].n = tree_rows();
				/* fewer rows may now end above where the list was scrolled
				   to, and an offset past the end is refused (D11): to the top */
				nest[5].scroll = 0;
				*y0 = 0; *y1 = H;                 /* the rows all moved */
			}
			/* Save opens its menu; the menu's own buttons close it */
			if (strcmp(id, "save") == 0 && wsk_ui_open(&ui, 3, "save", &m0, &m1) == WSK_OK)
				also(y0, y1, m0, m1);
			for (int c = 0; c < 4; c++)
				if (t->in == &nest[CTX] && t->item == c) {
					wsk_event_t k;
					memset(&k, 0, sizeof k);
					k.kind = WSK_EV_KEY;
					k.key = CTXKEY[c];
					feed(&k, &m0, &m1);
					also(y0, y1, m0, m1);
				}
		} else if (t->kind == WSK_NOTE_CLOSE)
			printf("close  the menu\n");
		else if (t->kind == WSK_NOTE_CHANGE)
			printf("change %s = %d\n", id, t->value);
		else if (t->kind == WSK_NOTE_MENU) {
			/* a context menu: on a field, Cut/Copy/Paste/Select all, hung
			   from the pointer -- or, from the Menu key, from the field */
			printf("menu   %s\n", id);
			if (is_field(t) && ((ev->kind == WSK_EV_MENU)
			                    ? wsk_ui_open_at(&ui, CTX, ev->x, ev->y, &m0, &m1)
			                    : wsk_ui_open(&ui, CTX, id, &m0, &m1)) == WSK_OK)
				also(y0, y1, m0, m1);
		} else if (t->kind == WSK_NOTE_KEY) {
			if (t->value == ('s' | WSK_MOD_CTRL)) {     /* our own shortcut */
				printf("shortcut save\n");
				if (wsk_ui_open(&ui, 3, "save", &m0, &m1) == WSK_OK) also(y0, y1, m0, m1);
			} else
				printf("key    0x%x, not ours\n", t->value);
		} else if (t->kind == WSK_NOTE_COPY) {
			printf("copy   %s \"%s\"%s\n", id, clip, wh_copy(clip, t->value) == 0 ? "" : ", the host refused it");
		} else if (t->kind == WSK_NOTE_PASTE) {
			char pb[128];
			wsk_note_t pn[2];
			int k = wh_paste(pb, sizeof pb), e = (k > 0) ? wsk_ui_paste(&ui, pb, k, pn, 2, &m0, &m1) : k;
			if (e >= 0) { printf("paste  %s \"%s\"\n", id, buf[t->in->item[t->item].sub].b); also(y0, y1, m0, m1); }
			else printf("paste  refused (%d)\n", e);
		}
		if (t->kind == WSK_NOTE_SELECT)
			printf("select %s\n", id);
		else if (t->kind == WSK_NOTE_ENTER)
			printf("enter  %s = \"%s\"\n", id, buf[t->in->item[t->item].sub].b);
		else if (t->kind == WSK_NOTE_FOCUS)
			printf("focus  %s\n", id);
	}
}

int main(int argc, char **argv)
{
	int w = (argc > 2) ? atoi(argv[1]) : 1024;
	int h = (argc > 2) ? atoi(argv[2]) : 600;
	int scale = (argc > 3) ? atoi(argv[3]) : 1;
	wsk_sink_t s;
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
		cells[i][1].kind = WSK_LABEL;
		snprintf(cells[i][1].id, WSK_ID_MAX, "s%d", i);
	}
	buf[0].b = name; buf[0].cap = sizeof name;
	buf[1].b = city; buf[1].cap = sizeof city;
	buf[2].b = note; buf[2].cap = sizeof note;
	buf[2].wrap = 1;                        /* a note wraps (§5u) */
	for (int i = 0; i < 3; i++) {
		undo[i].b = undo_b[i]; undo[i].cap = sizeof undo_b[i];
		buf[i].undo = &undo[i];
	}

	caps = wh_caps();                       /* ask the host first (§5r) */
	if (caps & WH_CAP_UTF8) strs[0] = HELP_UTF8;
	wsk_init(&win, WSK_COL, w, h);
	win.pad = 0; win.gap = 0; win.font = 20; win.ids = 1;
	win.item = WIN; win.n = 3;
	win.nest = nest; win.nests = 6 + 30 + 1;
	win.buf = buf; win.bufs = 3;
	win.str = strs; win.strs = 2;
	win.caps = (unsigned char)caps;
	win.state = state; win.states = 1;
	wsk_nest_init(&nest[0], &win, WSK_ROW);
	nest[0].pad = 8; nest[0].gap = 16; nest[0].item = BODY; nest[0].n = 2;
	wsk_nest_init(&nest[1], &win, WSK_COL);
	nest[1].pad = 0; nest[1].gap = 6; nest[1].item = FORM;
	nest[1].n = (caps & WH_CAP_IMAGE) ? 9 : 8;  /* the logo where it can be drawn */
	wsk_nest_init(&nest[2], &win, WSK_COL);
	nest[2].pad = 0; nest[2].gap = 4; nest[2].item = LIST; nest[2].n = 2;
	wsk_nest_init(&nest[4], &win, WSK_ROW);     /* the header */
	nest[4].pad = 4; nest[4].gap = 2; nest[4].item = HEAD; nest[4].n = 2;
	nest[4].talign = WSK_ALIGN_START;
	wsk_nest_init(&nest[5], &win, WSK_COL);     /* the body: scrolls, chooses */
	nest[5].pad = 0; nest[5].gap = 2; nest[5].item = items; nest[5].n = tree_rows();
	nest[5].indent = 18;                        /* a tree: depths over the rows */
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
	wsk_nest_init(&nest[CTX], &win, WSK_COL);  /* the menu a right-click opens */
	nest[CTX].pad = 6; nest[CTX].gap = 4; nest[CTX].bg = 0x2a3a4au;
	nest[CTX].item = CTXMENU; nest[CTX].n = 4;
	H = h;

	if ((e = wsk_check_in(&win, &which, &in)) != WSK_OK) {
		fprintf(stderr, "edit: the window was refused, %d at %s\n", e,
		        (in != 0 && which >= 0) ? in->item[which].id : "a stack");
		return 1;
	}
	if (wh_open(w, h, scale, "whisker -- edit") != 0) return 1;
	s.put = to_window; s.ctx = 0; s.len = 0;
	wsk_ui_init(&ui, &win);
	ui.clip = clip; ui.clipcap = sizeof clip;
	wsk_ui_focus(&ui, "name", &y0, &y1);
	wh_clip(0, 0);
	wsk_render(&win, &s, 0);

	for (;;) {
		wh_event_t he;
		wsk_event_t ev;
		int k = wh_poll(&he);

		if (k == WH_QUIT) break;
		if (k == WH_NOTHING) { wh_wait(20); continue; }
		if (k == WH_EXPOSE) {                 /* a new screen: all of it */
			wh_clip(0, 0);
			wsk_render(&win, &s, 0);
			continue;
		}
		memset(&ev, 0, sizeof ev);
		ev.x = (short)he.x; ev.y = (short)he.y;
		if (k == WH_PRESS) { ev.kind = WSK_EV_DOWN; ev.key = (unsigned short)he.key; }
		else if (k == WH_RELEASE) ev.kind = WSK_EV_UP;
		else if (k == WH_MOVE) ev.kind = WSK_EV_MOVE;
		else if (k == WH_MENU) ev.kind = WSK_EV_MENU;
		else if (k == WH_WHEEL) { ev.kind = WSK_EV_WHEEL; ev.d = (short)he.d; }
		else if (k == WH_KEY) { ev.kind = WSK_EV_KEY; ev.key = (unsigned short)he.key; }
		else if (k == WH_TEXT) { ev.kind = WSK_EV_TEXT; memcpy(ev.text, he.text, sizeof ev.text); }
		else continue;

		feed(&ev, &y0, &y1);
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
