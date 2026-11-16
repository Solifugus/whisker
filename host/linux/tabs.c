/*
 * Notes in tabs (design.md §5ah).
 *
 *     ./run.sh --tabs                  the SDL window, RV-9's rasteriser
 *     host/web/run.sh --tabs           the same program in a browser
 *
 * Studio's shape: a tab per document, the caption marked "* " while it is
 * unsaved. Ctrl-T a new note, after the current one; Ctrl-S "saves" it;
 * Ctrl-W or its x closes it -- an unsaved one asks first: its x again
 * discards it. A right-click on any tab offers Close and Close others, and
 * acts on THAT tab without switching to it (STU-21's rule).
 *
 * A browser keeps Ctrl-T and Ctrl-W for its own tabs -- a page is never
 * given them -- so Alt-T and Alt-W do the same, in either host.
 *
 * The tab set is whisker_tabs.h's, built in tree slots this program sets
 * aside; each note's memo is this program's own tree.
 */
#include "host.h"
#include "whisker.h"
#include "whisker_tabs.h"
#include "fit.h"

#include <stdio.h>
#include <string.h>

#define W 1024
#define H 600
#define NOTES 8

enum { T_MENU = NOTES, T_SET };       /* the notes' trees are 0..NOTES-1 */

static const wsk_widget_t NOTEW[NOTES][1] = {
	{ { .kind = WSK_MEMO, .id = "m0", .sub = 0, .weight = 1 } },
	{ { .kind = WSK_MEMO, .id = "m1", .sub = 1, .weight = 1 } },
	{ { .kind = WSK_MEMO, .id = "m2", .sub = 2, .weight = 1 } },
	{ { .kind = WSK_MEMO, .id = "m3", .sub = 3, .weight = 1 } },
	{ { .kind = WSK_MEMO, .id = "m4", .sub = 4, .weight = 1 } },
	{ { .kind = WSK_MEMO, .id = "m5", .sub = 5, .weight = 1 } },
	{ { .kind = WSK_MEMO, .id = "m6", .sub = 6, .weight = 1 } },
	{ { .kind = WSK_MEMO, .id = "m7", .sub = 7, .weight = 1 } },
};
static const wsk_widget_t MENU[] = {
	{ .kind = WSK_BUTTON, .id = "close",  .text = "Close",        .size = 40 },
	{ .kind = WSK_BUTTON, .id = "others", .text = "Close others", .size = 40 },
};
static const wsk_widget_t TOP[] = {
	{ .kind = WSK_STACK, .id = "set", .sub = T_SET, .weight = 1 },
	{ .kind = WSK_TEXT, .id = "status", .sub = WSK_TABS_MAX, .size = 30 },
};

static char text[NOTES][4096];
static wsk_buf_t buf[NOTES];
static unsigned char used[NOTES], dirty[NOTES];
static int made;                        /* notes made so far: the next one's number */
static char number[NOTES][20];          /* "Note 3": what each note is called */
static char status[200];
static wsk_tree_t root, trees[T_SET + WSK_TABS_TREES];
static const char *strs[WSK_TABS_MAX + 1];
static wsk_tabs_t tabs;
static wsk_ui_t ui;
static int confirm = -1;                /* the unsaved note whose x was tapped once */
static char menu_on[24];                /* the tab a right-click opened the menu on */

static void to_window(wsk_sink_t *s, const char *b, int n)
{
	(void)s;
	wh_write(b, n);
}

static void say(const char *m)
{
	snprintf(status, sizeof status, "%s", m);
	printf("%s\n", m);
}

/* a note's slot from its tab's name, "n3" */
static int slot_of(const char *name) { return (name && name[0] == 'n') ? name[1] - '0' : -1; }

static void caption(int s)
{
	char c[24], nm[4] = { 'n', (char)('0' + s), 0, 0 };
	snprintf(c, sizeof c, "%s%s", dirty[s] ? "* " : "", number[s]);
	wsk_tabs_caption(&tabs, nm, c);
}

/* the chosen note's memo has the keys */
static void focus_current(void)
{
	int y0, y1, s = slot_of(wsk_tabs_current(&tabs));
	char id[4] = { 'm', (char)('0' + s), 0, 0 };
	if (s >= 0) wsk_ui_focus(&ui, id, &y0, &y1);
}

static void new_note(void)
{
	char nm[4] = { 'n', 0, 0, 0 };
	int s = 0;
	while (s < NOTES && used[s]) s++;
	if (s == NOTES) { say("eight notes is all this demo keeps: close one first"); return; }
	used[s] = 1; dirty[s] = 0;
	memset(&buf[s], 0, sizeof buf[s]);
	buf[s].b = text[s]; buf[s].cap = sizeof text[s];
	text[s][0] = '\0';
	snprintf(number[s], sizeof number[s], "Note %d", ++made);
	nm[1] = (char)('0' + s);
	wsk_tabs_add(&tabs, nm, number[s], s, 1, 1);
	wsk_tabs_choose(&tabs, nm);
	wsk_tabs_build(&tabs);
	focus_current();
	{ char m[64]; snprintf(m, sizeof m, "%s made", number[s]); say(m); }
}

/* close a note; an unsaved one only when asked twice in a row (`sure`) */
static int close_note(int s, int sure)
{
	char nm[4] = { 'n', (char)('0' + s), 0, 0 }, m[96];
	if (s < 0 || s >= NOTES || !used[s]) return 0;
	if (dirty[s] && !sure) {
		snprintf(m, sizeof m, "%s is not saved: its x again discards it, Ctrl-S saves it", number[s]);
		say(m);
		confirm = s;
		return 0;
	}
	wsk_tabs_remove(&tabs, nm);
	used[s] = dirty[s] = 0;
	snprintf(m, sizeof m, "%s closed", number[s]);
	say(m);
	confirm = -1;
	wsk_tabs_build(&tabs);
	focus_current();
	return 1;
}

int main(void)
{
	wsk_sink_t s;
	int y0, y1, e, which = -1;

	wsk_init(&root, WSK_COL, W, H);
	root.font = 18; root.pad = 0; root.gap = 0;
	root.talign = WSK_ALIGN_START;
	root.item = TOP; root.n = 2;
	root.nest = trees; root.nests = T_SET + WSK_TABS_TREES;
	root.str = strs; root.strs = WSK_TABS_MAX + 1;
	root.buf = buf; root.bufs = NOTES;
	root.caps = (unsigned char)wh_caps();
	strs[WSK_TABS_MAX] = status;
	for (int i = 0; i < NOTES; i++) {
		wsk_nest_init(&trees[i], &root, WSK_COL);
		trees[i].item = NOTEW[i]; trees[i].n = 1; trees[i].pad = 6;
		buf[i].b = text[i]; buf[i].cap = sizeof text[i];
	}
	wsk_nest_init(&trees[T_MENU], &root, WSK_COL);   /* the menu a right-click on a tab opens */
	trees[T_MENU].pad = 6; trees[T_MENU].gap = 4; trees[T_MENU].bg = 0x2a3a4au;
	trees[T_MENU].item = MENU; trees[T_MENU].n = 2;

	if (wsk_tabs_init(&tabs, &root, strs, T_SET, 0, WSK_TABS_TOP) != WSK_OK) return 1;
	wsk_ui_init(&ui, &root);
	new_note(); new_note(); new_note();
	say("Alt-T (Ctrl-T) a new note, Ctrl-S saves it, Alt-W (Ctrl-W) or its x closes it; right-click a tab");
	if ((e = wsk_check(&root, &which)) != WSK_OK) {
		fprintf(stderr, "tabs: the window was refused (%d at %d)\n", e, which);
		return 1;
	}

	if (wh_open(W, H, 1, "whisker -- tabs") != 0) return 1;
	s.put = to_window; s.ctx = 0; s.len = 0;
	wh_draw_fit(&root, &s, 0, H);

	for (;;) {
		wh_event_t he;
		wsk_event_t ev;
		wsk_note_t note[8];
		int k = wh_poll(&he), n, f, whole = 0;

		if (k == WH_QUIT) break;
		if (k == WH_NOTHING) { fflush(stdout); wh_wait(20); continue; }
		if (k == WH_EXPOSE) { wh_draw_fit(&root, &s, 0, H); continue; }
		memset(&ev, 0, sizeof ev);
		ev.x = (short)he.x; ev.y = (short)he.y; ev.finger = (unsigned char)he.finger;
		if (k == WH_PRESS) { ev.kind = WSK_EV_DOWN; ev.key = (unsigned short)he.key; ev.d = (short)he.d; }
		else if (k == WH_RELEASE) ev.kind = WSK_EV_UP;
		else if (k == WH_MOVE) ev.kind = WSK_EV_MOVE;
		else if (k == WH_WHEEL) { ev.kind = WSK_EV_WHEEL; ev.d = (short)he.d; }
		else if (k == WH_KEY) { ev.kind = WSK_EV_KEY; ev.key = (unsigned short)he.key; }
		else if (k == WH_TEXT) { ev.kind = WSK_EV_TEXT; memcpy(ev.text, he.text, sizeof ev.text); }
		else if (k == WH_MENU) ev.kind = WSK_EV_MENU;
		else continue;

		n = wsk_feed(&ui, &ev, note, 8, &y0, &y1);
		if (n > 8) n = 8;                       /* wsk_feed counts what did not fit */
		f = wsk_tabs_feed(&tabs, &ui, note, n);
		if (f & WSK_TABS_CHOSEN) {
			char m[64];
			int c = slot_of(wsk_tabs_current(&tabs));
			focus_current();
			snprintf(m, sizeof m, "%s chosen", number[c]);
			say(m);
			confirm = -1;
			whole = 1;
		}
		if (f & WSK_TABS_ASKED) {
			int a = slot_of(tabs.tab[tabs.asked].name);
			close_note(a, confirm == a);
			whole = 1;
		}
		for (int i = 0; i < n; i++) {
			const wsk_note_t *t = &note[i];
			if (t->kind == WSK_NOTE_TEXT) {
				for (int q = 0; q < NOTES; q++)
					if (t->in == &trees[q] && used[q] && !dirty[q]) { dirty[q] = 1; caption(q); wsk_tabs_build(&tabs); whole = 1; }
			} else if (t->kind == WSK_NOTE_MENU) {
				int a = wsk_tabs_at(&tabs, t), m0, m1;
				if (a >= 0 && ev.kind == WSK_EV_MENU && wsk_ui_open_at(&ui, T_MENU, ev.x, ev.y, &m0, &m1) == WSK_OK) {
					char m[64];
					memcpy(menu_on, tabs.tab[a].name, sizeof menu_on);
					snprintf(m, sizeof m, "a menu on %s", number[slot_of(menu_on)]);
					say(m);
					whole = 1;
				}
			} else if (t->kind == WSK_NOTE_CLICK && t->in == &trees[T_MENU]) {
				int on = slot_of(menu_on), kept = 0;
				if (t->item == 0) close_note(on, 0);
				else {
					for (int q = 0; q < NOTES; q++)
						if (q != on && used[q] && !close_note(q, 0)) kept++;
					if (kept > 0) { char m[64]; snprintf(m, sizeof m, "closed the others but %d unsaved", kept); say(m); }
				}
				whole = 1;
			} else if (t->kind == WSK_NOTE_KEY && (t->value == ('t' | WSK_MOD_CTRL) || t->value == ('t' | WSK_MOD_ALT))) {
				new_note();
				whole = 1;
			} else if (t->kind == WSK_NOTE_KEY && t->value == ('s' | WSK_MOD_CTRL)) {
				int c = slot_of(wsk_tabs_current(&tabs));
				if (c >= 0) {
					char m[64];
					dirty[c] = 0; caption(c); wsk_tabs_build(&tabs);
					snprintf(m, sizeof m, "%s saved", number[c]);
					say(m);
					confirm = -1;
					whole = 1;
				}
			} else if (t->kind == WSK_NOTE_KEY && (t->value == ('w' | WSK_MOD_CTRL) || t->value == ('w' | WSK_MOD_ALT))) {
				int c = slot_of(wsk_tabs_current(&tabs));
				close_note(c, confirm == c);
				whole = 1;
			}
		}
		if (whole) { wh_draw_fit(&root, &s, 0, H); continue; }
		if (y1 > y0) wh_draw_fit(&root, &s, y0, y1);
	}
	wh_close();
	return 0;
}
