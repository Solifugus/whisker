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
 * this one does not. Ctrl-O opens another file and Ctrl-Shift-S saves this
 * one as a name of your choosing -- the chooser (§5ae), which reads folders
 * through POSIX here (lister.h) because whisker reads none. Ctrl-S says
 * that only Save as writes. Close the window to quit.
 */
#include "host.h"
#include "whisker.h"
#include "whisker_chooser.h"
#include "lister.h"

#include <ctype.h>
#include <limits.h>
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
static wsk_buf_t buf[3];                /* the code, the output, the chooser's name */
static wsk_tree_t chtrees[4];           /* the chooser's slots (§5ae) */
static const char *chstrs[2 + WSK_CH_MAX];
static wsk_chooser_t chooser;
static int choosing;                    /* 0, or what the chooser was opened for: 'o' or 's' */
static char pathbuf[PATH_MAX];
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

/* Read a file into the editor: 0, or -1 with the reason said -- and a file
   this window cannot draw refused, the one open kept. */
static int load(const char *p)
{
	static char keep[CAP];
	FILE *f = fopen(p, "rb");
	int len, was = buf[0].len;
	char m[PATH_MAX + 64];
	if (f == NULL) { snprintf(m, sizeof m, "cannot open %s", p); say(m); return -1; }
	memcpy(keep, text, (size_t)was + 1);
	len = (int)fread(text, 1, CAP - 1, f);
	fclose(f);
	text[len] = '\0';
	buf[0].len = len;
	buf[0].caret = 0; buf[0].span = 0;
	buf[0].ninks = 0;
	if (wsk_check(&win, 0) != WSK_OK) {
		memcpy(text, keep, (size_t)was + 1);
		buf[0].len = was;
		buf[0].caret = 0;
		colour();
		snprintf(m, sizeof m, "%s holds what this window cannot draw: not opened", p);
		say(m);
		return -1;
	}
	buf[0].len = len;
	buf[0].caret = 0; buf[0].span = 0; buf[0].top = 0;
	undo.used = 0; undo.end = 0;
	memset(brk, 0, sizeof brk);
	snprintf(pathbuf, sizeof pathbuf, "%s", p);
	path = pathbuf;
	colour();
	remark();
	snprintf(m, sizeof m, "opened %s", p);
	say(m);
	return 0;
}

/* The folder a path is in, made absolute: where the chooser starts. */
static void folder_of(const char *p, char *out, int cap)
{
	char full[PATH_MAX];
	const char *slash;
	if (realpath(p, full) == NULL) snprintf(full, sizeof full, "%s", p);
	slash = strrchr(full, '/');
	if (slash == NULL) { snprintf(out, (size_t)cap, "."); return; }
	if (slash == full) { snprintf(out, (size_t)cap, "/"); return; }
	snprintf(out, (size_t)cap, "%.*s", (int)(slash - full), full);
}

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
	win.buf = buf; win.bufs = 3;
	win.nest = chtrees; win.nests = 4;
	win.str = chstrs; win.strs = 2 + WSK_CH_MAX;
	for (int i = 0; i < 4; i++) wsk_nest_init(&chtrees[i], &win, WSK_COL);
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
	if (wsk_chooser_init(&chooser, &win, chstrs, 0, 0, 2, posix_lister, 0) != WSK_OK) return 1;
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
		/* the chooser's, while it is open (§5ae) */
		if (choosing) {
			int r = wsk_chooser_feed(&chooser, &ui, note, n, &y0, &y1);
			if (r == WSK_CHOSEN && choosing == 'o') {
				(void)load(chooser.path);
			} else if (r == WSK_CHOSEN && choosing == 's') {
				FILE *f = fopen(chooser.path, "wb");
				char m[PATH_MAX + 32];
				if (f != NULL && (int)fwrite(text, 1, (size_t)buf[0].len, f) == buf[0].len) {
					snprintf(pathbuf, sizeof pathbuf, "%s", chooser.path);
					path = pathbuf;
					snprintf(m, sizeof m, "saved %s", chooser.path);
				} else snprintf(m, sizeof m, "could not write %s", chooser.path);
				if (f != NULL) fclose(f);
				say(m);
			} else if (r == WSK_CANCELLED) say("nothing chosen");
			if (r != 0) choosing = 0;
			y0 = 0; y1 = h;
		}
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
				say("save: only Save as (Ctrl-Shift-S) writes, to a name you choose");
				y0 = 0; y1 = h;
			} else if (t->kind == WSK_NOTE_KEY && !choosing &&
			           (t->value == ('o' | WSK_MOD_CTRL) || t->value == ('s' | WSK_MOD_CTRL | WSK_MOD_SHIFT))) {
				/* the chooser (§5ae): open a file, or save as */
				char at[PATH_MAX];
				const char *base = strrchr(path, '/');
				int open = (t->value == ('o' | WSK_MOD_CTRL)), cy0, cy1;
				folder_of(path, at, sizeof at);
				if (wsk_chooser_open(&chooser, &ui, open ? WSK_CHOOSE_FILE : WSK_CHOOSE_SAVE,
				                     open ? "Open a file" : "Save as", at,
				                     open ? 0 : (base ? base + 1 : path), 0, 640, 440, &cy0, &cy1) == WSK_OK) {
					choosing = open ? 'o' : 's';
					say(open ? "choose a file to open" : "choose a name to save as");
				} else say("that folder cannot be read");
				y0 = 0; y1 = h;
			} else if (t->kind == WSK_NOTE_SPLIT)
				printf("output pane %d high\n", t->value);   /* each step of a drag: not the pane's */
		}
		/* the text changed: colour it again, BEFORE drawing (§5v rule 4) */
		if (text_changed && !choosing) {
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
