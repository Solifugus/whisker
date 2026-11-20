/*
 * A form in cells (design.md §5aq), and a form's small words in a window.
 *
 *     ./run.sh --form                  the SDL window, RV-9's rasteriser
 *     host/web/run.sh --form           the same program in a browser
 *
 * One grid: labels in a column of their own, fields beside them, a role
 * field in a second pair of columns, a password across three, notes down
 * two rows taking what height is left, a licence read only, a striped list
 * of recent people, and I agree beside Cancel and OK. Every word §5ao and
 * §5ap added is in it: the hints, the secret, the default button (Enter in
 * a field presses OK), the read-only memo, the striped rows. And a menu bar
 * (§5ar): Form opens its menu by itself, and Fill in opens a submenu of
 * people beside it -- no code here opens either.
 */
#include "host.h"
#include "whisker.h"
#include "fit.h"

#include <stdio.h>
#include <string.h>

#define W 720
#define H 480

enum { T_FORM, T_RECENT, T_BAR, T_MENU, T_PEOPLE, TREES };
enum { B_NAME, B_ROLE, B_PASS, B_NOTES, B_LIC, BUFS };

static const wsk_widget_t TOP[] = {
	{ .kind = WSK_STACK, .id = "bar", .sub = T_BAR, .size = 30 },
	{ .kind = WSK_STACK, .id = "form", .sub = T_FORM, .weight = 1 },
	{ .kind = WSK_TEXT, .id = "status", .sub = 0, .size = 30 },
};
static const wsk_widget_t FORM[] = {
	{ .kind = WSK_LABEL,  .id = "lname",  .text = "Name", .size = 96 },
	{ .kind = WSK_FIELD,  .id = "name",   .text = "your name", .sub = B_NAME, .weight = 1, .flags = WSK_HINT },
	{ .kind = WSK_LABEL,  .id = "lrole",  .text = "Role", .size = 64 },
	{ .kind = WSK_FIELD,  .id = "role",   .text = "e.g. editor", .sub = B_ROLE, .weight = 1, .flags = WSK_HINT },
	{ .kind = WSK_LABEL,  .id = "lpass",  .text = "Password" },
	{ .kind = WSK_FIELD,  .id = "pass",   .sub = B_PASS, .flags = WSK_SECRET },
	{ .kind = WSK_LABEL,  .id = "lnotes", .text = "Notes" },
	{ .kind = WSK_MEMO,   .id = "notes",  .text = "anything we should know", .sub = B_NOTES, .flags = WSK_HINT },
	{ .kind = WSK_LABEL,  .id = "llic",   .text = "Licence" },
	{ .kind = WSK_MEMO,   .id = "lic",    .sub = B_LIC, .flags = WSK_READONLY },
	{ .kind = WSK_LABEL,  .id = "lrecent", .text = "Recent" },
	{ .kind = WSK_STACK,  .id = "recent", .sub = T_RECENT },
	{ .kind = WSK_CHECK,  .id = "agree",  .text = "I agree", .sub = 0 },
	{ .kind = WSK_BUTTON, .id = "cancel", .text = "Cancel" },
	{ .kind = WSK_BUTTON, .id = "ok",     .text = "OK", .flags = WSK_DEFAULT },
};
/* row, col, across, down, tall, grow -- one per part of FORM */
static const wsk_cell_t CELLS[] = {
	{ 0, 0, 0, 0, 0, 0 }, { 0, 1, 0, 0, 0, 0 }, { 0, 2, 0, 0, 0, 0 }, { 0, 3, 0, 0, 0, 0 },
	{ 1, 0, 0, 0, 0, 0 }, { 1, 1, 3, 0, 0, 0 },
	{ 2, 0, 0, 0, 0, 0 }, { 2, 1, 3, 2, 0, 1 },
	{ 4, 0, 0, 0, 0, 0 }, { 4, 1, 3, 0, 72, 0 },
	{ 5, 0, 0, 0, 0, 0 }, { 5, 1, 3, 0, 100, 0 },
	{ 6, 0, 2, 0, 0, 0 }, { 6, 2, 0, 0, 36, 0 }, { 6, 3, 0, 0, 36, 0 },
};
static const wsk_widget_t BAR[] = {
	{ .kind = WSK_BUTTON, .id = "mform", .text = "Form", .size = 90, .flags = WSK_SUBMENU, .sub = T_MENU },
	{ .kind = WSK_LABEL,  .id = "fill",  .weight = 1 },
};
static const wsk_widget_t MENU[] = {
	{ .kind = WSK_BUTTON, .id = "mclear",  .text = "Clear",   .size = 30 },
	{ .kind = WSK_BUTTON, .id = "mfill",   .text = "Fill in", .size = 30, .flags = WSK_SUBMENU, .sub = T_PEOPLE },
	{ .kind = WSK_BUTTON, .id = "msubmit", .text = "Submit",  .size = 30 },
};
/* who Fill in offers, and what it fills */
static const wsk_widget_t PEOPLE[] = {
	{ .kind = WSK_BUTTON, .id = "f0", .text = "Ada Lovelace", .size = 30 },
	{ .kind = WSK_BUTTON, .id = "f1", .text = "Grace Hopper", .size = 30 },
	{ .kind = WSK_BUTTON, .id = "f2", .text = "Ken Thompson", .size = 30 },
};
static const char *const ROLE_OF[3] = { "analyst", "compiler", "shell" };
static const wsk_widget_t RECENT[] = {
	{ .kind = WSK_LABEL, .id = "p0", .text = "Ada Lovelace -- analyst" },
	{ .kind = WSK_LABEL, .id = "p1", .text = "Grace Hopper -- compiler" },
	{ .kind = WSK_LABEL, .id = "p2", .text = "Edsger Dijkstra -- editor" },
	{ .kind = WSK_LABEL, .id = "p3", .text = "Barbara Liskov -- types" },
	{ .kind = WSK_LABEL, .id = "p4", .text = "Ken Thompson -- shell" },
	{ .kind = WSK_LABEL, .id = "p5", .text = "Frances Allen -- optimiser" },
};

static char text[BUFS][512];
static wsk_buf_t buf[BUFS];
static unsigned char state[1];
static char status[200], clip[512];
static const char *strs[1];
static wsk_tree_t root, trees[TREES];
static wsk_ui_t ui;

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

static void clear(void)
{
	for (int i = B_NAME; i <= B_NOTES; i++) {
		text[i][0] = '\0';
		buf[i].len = buf[i].caret = buf[i].span = buf[i].top = 0;
	}
	state[0] = 0;
}

static void put(int b, const char *v)
{
	snprintf(text[b], sizeof text[b], "%s", v);
	buf[b].len = buf[b].caret = (int)strlen(text[b]);
	buf[b].span = 0;
}

static void submit(void)
{
	char m[200];
	if (buf[B_NAME].len == 0) say("a name, please");
	else if (!state[0]) say("tick I agree first");
	else {
		snprintf(m, sizeof m, "thank you, %.40s%s%.40s -- a password of %d characters, %d of notes",
		         text[B_NAME], buf[B_ROLE].len ? ", " : "", text[B_ROLE], buf[B_PASS].len, buf[B_NOTES].len);
		say(m);
	}
}

int main(void)
{
	wsk_sink_t s;
	int y0, y1, e, which = -1;
	static const char lic[] = "This form is a demonstration. Nothing typed here leaves the program: "
	                          "it is whisker's cells (design.md section 5aq), with a hint, a password, "
	                          "a default button, this read-only memo and a striped list in them.";

	memset(buf, 0, sizeof buf);
	for (int i = 0; i < BUFS; i++) { buf[i].b = text[i]; buf[i].cap = sizeof text[i]; }
	buf[B_NOTES].wrap = 1;
	buf[B_LIC].wrap = 1;
	snprintf(text[B_LIC], sizeof text[B_LIC], "%s", lic);
	buf[B_LIC].len = (int)strlen(text[B_LIC]);

	wsk_init(&root, WSK_COL, W, H);
	root.pad = 0; root.gap = 0;
	root.item = TOP; root.n = 3;
	root.nest = trees; root.nests = TREES;
	root.str = strs; root.strs = 1;
	root.buf = buf; root.bufs = BUFS;
	root.state = state; root.states = 1;
	root.caps = (unsigned char)wh_caps();
	strs[0] = status;

	wsk_nest_init(&trees[T_FORM], &root, WSK_ROW);
	trees[T_FORM].item = FORM; trees[T_FORM].n = (int)(sizeof FORM / sizeof FORM[0]);
	trees[T_FORM].overflow = WSK_GRID; trees[T_FORM].cells = CELLS;
	trees[T_FORM].pad = 12; trees[T_FORM].gap = 8;
	wsk_nest_init(&trees[T_RECENT], &root, WSK_COL);
	trees[T_RECENT].item = RECENT; trees[T_RECENT].n = (int)(sizeof RECENT / sizeof RECENT[0]);
	trees[T_RECENT].overflow = WSK_SCROLL; trees[T_RECENT].select = 1; trees[T_RECENT].stripe = 1;
	trees[T_RECENT].talign = WSK_ALIGN_START; trees[T_RECENT].pad = 0; trees[T_RECENT].gap = 0;

	wsk_nest_init(&trees[T_BAR], &root, WSK_ROW);
	trees[T_BAR].item = BAR; trees[T_BAR].n = 2; trees[T_BAR].pad = 0; trees[T_BAR].gap = 0;
	wsk_nest_init(&trees[T_MENU], &root, WSK_COL);
	trees[T_MENU].item = MENU; trees[T_MENU].n = 3; trees[T_MENU].pad = 4; trees[T_MENU].gap = 0;
	trees[T_MENU].bg = 0x2a3a4au;
	wsk_nest_init(&trees[T_PEOPLE], &root, WSK_COL);
	trees[T_PEOPLE].item = PEOPLE; trees[T_PEOPLE].n = 3; trees[T_PEOPLE].pad = 4; trees[T_PEOPLE].gap = 0;
	trees[T_PEOPLE].bg = 0x2a3a4au;

	wsk_ui_init(&ui, &root);
	ui.clip = clip; ui.clipcap = sizeof clip;
	say("Tab between the parts; Enter in a field presses OK");
	if ((e = wsk_check(&root, &which)) != WSK_OK) {
		fprintf(stderr, "form: the window was refused (%d at %d)\n", e, which);
		return 1;
	}

	if (wh_open(W, H, 1, "whisker -- a form in cells") != 0) return 1;
	s.put = to_window; s.ctx = 0; s.len = 0;
	wh_draw_fit(&root, &s, 0, H);

	for (;;) {
		wh_event_t he;
		wsk_event_t ev;
		wsk_note_t note[8];
		int k = wh_poll(&he), n, whole = 0;

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
		for (int i = 0; i < n; i++) {
			const wsk_note_t *t = &note[i];
			const char *id = (t->in != 0 && t->item >= 0) ? t->in->item[t->item].id : "";
			if (t->kind == WSK_NOTE_CLICK && (strcmp(id, "ok") == 0 || strcmp(id, "msubmit") == 0)) {
				submit();
				whole = 1;
			} else if (t->kind == WSK_NOTE_CLICK && strcmp(id, "mclear") == 0) {
				clear();
				say("cleared, from the menu");
				whole = 1;
			} else if (t->kind == WSK_NOTE_CLICK && t->in == &trees[T_PEOPLE]) {
				char m[96];
				put(B_NAME, PEOPLE[t->item].text);
				put(B_ROLE, ROLE_OF[t->item]);
				snprintf(m, sizeof m, "filled in %s", PEOPLE[t->item].text);
				say(m);
				whole = 1;
			} else if (t->kind == WSK_NOTE_CLOSE) {
				whole = 1;                      /* what the menus covered */
			}
			if (t->kind == WSK_NOTE_CLICK && strcmp(id, "cancel") == 0) {
				clear();
				say("cleared");
				whole = 1;
			} else if (t->kind == WSK_NOTE_CLICK && strcmp(id, "agree") == 0) {
				say(state[0] ? "agreed" : "not agreed");
				whole = 1;
			} else if (t->kind == WSK_NOTE_SELECT && t->in == &trees[T_RECENT]) {
				char m[96];
				snprintf(m, sizeof m, "chose %s", RECENT[trees[T_RECENT].sel].text);
				say(m);
				whole = 1;
			} else if (t->kind == WSK_NOTE_ENTER) {
				char m[64];
				snprintf(m, sizeof m, "Enter in %s", id);
				say(m);
			} else if (t->kind == WSK_NOTE_COPY) {
				wh_copy(clip, t->value);
			} else if (t->kind == WSK_NOTE_PASTE) {
				char pb[512];
				wsk_note_t pn[2];
				int m0, m1, got = wh_paste(pb, sizeof pb);
				if (got > 0 && wsk_ui_paste(&ui, pb, got, pn, 2, &m0, &m1) >= 0) whole = 1;
			}
		}
		if (whole) { wh_draw_fit(&root, &s, 0, H); continue; }
		if (y1 > y0) wh_draw_fit(&root, &s, y0, y1);
	}
	wh_close();
	return 0;
}
