/*
 * A code editor on whisker's `source` widget (design.md §5v).
 *
 *     ./run.sh --code [file.bas]         the SDL window, RV-9's rasteriser
 *     host/web/run.sh --code [file.bas]  the same program in a browser
 *
 * The point of it is the split Matthew chose: "colour runs from the
 * program". whisker knows no language. THIS PROGRAM reads gBASIC -- the
 * keywords below are gBASIC's lexer's own list -- and hands whisker runs of
 * "these characters in this colour"; whisker draws them. It colours again
 * whenever it is told the text changed, before the repaint, so a stale
 * colour is never on the screen.
 *
 * Typing, the arrows, Shift to select, Ctrl-A/C/X/V, Ctrl-Z/Y, the wheel.
 * Below the code, an output pane, and between them a divider to drag
 * (§5x) -- Studio's window is split the same way. What the program says
 * goes there, and to stdout.
 *
 * A click in the gutter sets or clears a breakpoint on that line: a red mark
 * in the gutter (§5v step 4), and printed. Breakpoints stay on their line
 * numbers as the text changes -- moving them with it is a program's job, and
 * this one does not. The file is never written: Ctrl-S says so. Close the
 * window to quit.
 */
#include "host.h"
#include "whisker.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAP   (400 * 1024)              /* Studio's largest file is 326 KB */
#define INKS  65536

static const wsk_widget_t WIN[] = {
	{ .kind = WSK_LABEL,   .id = "status", .size = 32 },
	{ .kind = WSK_SOURCE,  .id = "code",   .sub = 0, .weight = 1 },
	{ .kind = WSK_DIVIDER, .id = "split",  .size = 6, .fill = 0x3a5068u },
	{ .kind = WSK_MEMO,    .id = "output", .sub = 1, .size = 120 },
};

static char text[CAP], clip[CAP], undo_b[256 * 1024], out[8192];
static wsk_ink_t inks[INKS];
static unsigned char brk[65536];        /* breakpoints, by line */
static wsk_gmark_t marks[4096];         /* ...as whisker draws them, in order */
static wsk_widget_t win_items[4];
static wsk_buf_t buf[2];
static short sizes[4];                  /* the panes, as the divider leaves them */
static wsk_undo_t undo;
static wsk_tree_t win;
static wsk_ui_t ui;
static const char *path = "sample.bas";

/* gBASIC's keywords, from its lexer (gbasic src/lexer.c) */
static const char *const KW[] = {
	"program", "library", "load", "use", "export", "if", "then", "else", "end", "for",
	"to", "step", "do", "until", "while", "consider", "break", "continue", "function",
	"return", "print", "dim", "as", "watch", "unwatch", "without", "watchers",
	"modifier", "goto", "gosub", "with", "new", "spawn", "on", "next", "stop", "error",
	"true", "false", "nothing", "unknown", "and", "or", "not", "try", "excluding",
	"intersecting", "in", "each", "default", 0
};

enum { C_KEYWORD = 0x569cd6u, C_STRING = 0xce9178u, C_COMMENT = 0x6a9955u,
       C_NUMBER = 0xb5cea8u, C_CALL = 0xdcdcaau };

static int is_keyword(const char *w, int n)
{
	for (int k = 0; KW[k] != 0; k++)
		if ((int)strlen(KW[k]) == n && strncasecmp(KW[k], w, (size_t)n) == 0) return 1;
	return 0;
}

/* The program's colouring of its own language: runs, in order. */
static int colour(void)
{
	int n = 0, i = 0, len = buf[0].len;
	while (i < len && n < INKS) {
		char c = text[i];
		int at = i;
		unsigned int ink = 0;
		if (c == '\'') {                                   /* a comment, to the line's end */
			while (i < len && text[i] != '\n') i++;
			ink = C_COMMENT;
		} else if (c == '"') {                             /* a string */
			i++;
			while (i < len && text[i] != '"' && text[i] != '\n') i++;
			if (i < len && text[i] == '"') i++;
			ink = C_STRING;
		} else if (isdigit((unsigned char)c)) {
			while (i < len && (isalnum((unsigned char)text[i]) || text[i] == '.')) i++;
			ink = C_NUMBER;
		} else if (isalpha((unsigned char)c) || c == '_') {
			while (i < len && (isalnum((unsigned char)text[i]) || text[i] == '_')) i++;
			if (is_keyword(text + at, i - at)) ink = C_KEYWORD;
			else if (i < len && text[i] == '(') ink = C_CALL;
		} else
			i++;
		if (ink != 0) { inks[n].at = at; inks[n].len = i - at; inks[n].ink = ink; n++; }
	}
	buf[0].inks = inks;
	buf[0].ninks = n;
	return n;
}

/* A line for the output pane, and for stdout; the oldest go when it fills. */
static void say(const char *line)
{
	int n = (int)strlen(line);
	printf("%s\n", line);
	if (n + 1 >= (int)sizeof out) return;
	while (buf[1].len + n + 1 >= (int)sizeof out) {     /* drop the first line */
		char *nl = memchr(out, '\n', (size_t)buf[1].len);
		int cut = nl ? (int)(nl - out) + 1 : buf[1].len;
		memmove(out, out + cut, (size_t)(buf[1].len - cut + 1));
		buf[1].len -= cut;
	}
	if (buf[1].len > 0) out[buf[1].len++] = '\n';
	memcpy(out + buf[1].len, line, (size_t)n + 1);
	buf[1].len += n;
	buf[1].caret = buf[1].len;              /* the view follows the newest */
	buf[1].span = 0;
}

/* The breakpoints as gutter marks: one a line, in order (§5v step 4). */
static void remark(void)
{
	int n = 0;
	for (int l = 0; l < (int)sizeof brk && n < (int)(sizeof marks / sizeof marks[0]); l++)
		if (brk[l]) { marks[n].line = l; marks[n].ink = 0xe51400u; marks[n].shape = WSK_GM_DOT; n++; }
	buf[0].marks = marks;               /* set even with none: the column is kept */
	buf[0].nmarks = n;
}

/* Where the caret is, for the status line. */
static void status(void)
{
	int line = 1, col = 1;
	for (int i = 0; i < buf[0].caret; i++) {
		if (text[i] == '\n') { line++; col = 1; }
		else if (((unsigned char)text[i] & 0xC0) != 0x80) col++;   /* characters, not bytes */
	}
	const char *base = strrchr(path, '/');
	char t[64];
	snprintf(t, sizeof t, "%.16s  %d:%d", base ? base + 1 : path, line, col);
	snprintf(win_items[0].text, WSK_TEXT_MAX, "%.31s", t);
}

static void to_window(wsk_sink_t *s, const char *b, int n) { (void)s; wh_write(b, n); }

int main(int argc, char **argv)
{
	int w = 1024, h = 600, y0, y1, which = -1, e;
	const wsk_tree_t *in = 0;
	wsk_sink_t s;
	FILE *f;

	if (argc > 1) path = argv[1];
	f = fopen(path, "rb");
	if (f == NULL) { fprintf(stderr, "code: cannot open %s\n", path); return 1; }
	buf[0].len = (int)fread(text, 1, CAP - 1, f);
	fclose(f);
	text[buf[0].len] = '\0';
	buf[0].b = text;
	buf[0].cap = CAP;
	undo.b = undo_b; undo.cap = sizeof undo_b;
	buf[0].undo = &undo;
	colour();
	remark();

	out[0] = '\0';
	buf[1].b = out; buf[1].cap = sizeof out;
	memcpy(win_items, WIN, sizeof WIN);
	wsk_init(&win, WSK_COL, w, h);
	win.pad = 4; win.gap = 0; win.font = 18; win.ids = 1;
	win.item = win_items; win.n = 4;
	win.buf = buf; win.bufs = 2;
	win.sizes = sizes;
	win.caps = (unsigned char)wh_caps();
	win.talign = WSK_ALIGN_START;
	status();
	/* whisker refuses what its host cannot draw, and says so: a tab, a
	   character beyond ASCII on a host that draws none (the SDL window --
	   /w0's font is ASCII), or < > & on a host that does not decode
	   entities (the boards, until they are flashed with RV-9 8e0ef96) */
	if ((e = wsk_check_in(&win, &which, &in)) != WSK_OK) {
		fprintf(stderr, "code: %s was refused (%d): %s\n", path, e,
		        e != WSK_E_TEXT ? "see design.md"
		        : (win.caps & WH_CAP_ENTITY) ? "it holds a tab, a control character or malformed UTF-8"
		        : "it holds a tab, a character beyond ASCII, or < > & -- which this host "
		          "cannot draw");
		return 1;
	}
	{
		char m[96];
		snprintf(m, sizeof m, "opened %.80s", path);
		say(m);
	}
	if (wh_open(w, h, 1, "whisker -- source") != 0) return 1;
	s.put = to_window; s.ctx = 0; s.len = 0;
	wsk_ui_init(&ui, &win);
	ui.clip = clip; ui.clipcap = sizeof clip;
	buf[0].caret = 0;
	wsk_ui_focus(&ui, "code", &y0, &y1);
	wh_clip(0, 0);
	wsk_render(&win, &s, 0);

	for (;;) {
		wh_event_t he;
		wsk_event_t ev;
		wsk_note_t note[4];
		int k = wh_poll(&he), n, text_changed = 0;

		if (k == WH_QUIT) break;
		if (k == WH_NOTHING) { wh_wait(20); continue; }
		if (k == WH_EXPOSE) { wh_clip(0, 0); wsk_render(&win, &s, 0); continue; }
		memset(&ev, 0, sizeof ev);
		ev.x = (short)he.x; ev.y = (short)he.y;
		if (k == WH_PRESS) { ev.kind = WSK_EV_DOWN; ev.key = (unsigned short)he.key; ev.d = (short)he.d; }
		else if (k == WH_RELEASE) ev.kind = WSK_EV_UP;
		else if (k == WH_MOVE) ev.kind = WSK_EV_MOVE;
		else if (k == WH_MENU) ev.kind = WSK_EV_MENU;
		else if (k == WH_WHEEL) { ev.kind = WSK_EV_WHEEL; ev.d = (short)he.d; }
		else if (k == WH_KEY) { ev.kind = WSK_EV_KEY; ev.key = (unsigned short)he.key; }
		else if (k == WH_TEXT) { ev.kind = WSK_EV_TEXT; memcpy(ev.text, he.text, sizeof ev.text); }
		else continue;

		n = wsk_feed(&ui, &ev, note, 4, &y0, &y1);
		for (int i = 0; i < n && i < 4; i++) {
			const wsk_note_t *t = &note[i];
			if (t->kind == WSK_NOTE_TEXT) text_changed = 1;
			else if (t->kind == WSK_NOTE_GUTTER && t->value < (int)sizeof brk) {
				char m[48];
				brk[t->value] = (unsigned char)!brk[t->value];
				snprintf(m, sizeof m, "breakpoint %s line %d", brk[t->value] ? "on " : "off", t->value + 1);
				say(m);
				remark();
				y0 = 0; y1 = h;                 /* the gutter: the widget's height */
			} else if (t->kind == WSK_NOTE_COPY)
				wh_copy(clip, t->value);
			else if (t->kind == WSK_NOTE_PASTE) {
				static char pb[CAP];
				wsk_note_t pn[2];
				int m0, m1, pl = wh_paste(pb, sizeof pb);
				if (pl > 0 && wsk_ui_paste(&ui, pb, pl, pn, 2, &m0, &m1) > 0) text_changed = 1;
			} else if (t->kind == WSK_NOTE_KEY && t->value == ('s' | WSK_MOD_CTRL)) {
				say("save: this demo never writes the file");
				y0 = 0; y1 = h;
			} else if (t->kind == WSK_NOTE_SPLIT)
				printf("output pane %d high\n", t->value);   /* each step of a drag: not the pane's */
		}
		/* the text changed: colour it again, BEFORE drawing (§5v rule 4) */
		if (text_changed) {
			colour();
			y0 = 0; y1 = h;                 /* a line's colours can change the lines after */
		}
		status();
		y0 = 0;                             /* the status line moves with every key */
		if (y1 <= 32) y1 = 32;
		fflush(stdout);
		if (y1 > y0) { wh_clip(y0, y1); wsk_render_rows(&win, y0, y1, &s, 0); }
	}
	wh_close();
	return 0;
}
