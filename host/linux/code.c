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
 * that only Save as writes. Ctrl-N (Alt-N in a browser, which keeps Ctrl-N)
 * opens a second window, a scratch pad (§5af). Closing the last window
 * quits.
 */
#include "host.h"
#include "whisker.h"
#include "whisker_chooser.h"
#include "lister.h"
#include "fit.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
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
static unsigned char breaks[65536];        /* breakpoints, by line */
static wsk_gmark_t marks[4096];         /* ...as whisker draws them, in order */
static wsk_widget_t win_items[4];
static wsk_buf_t buf[3];                /* the code, the output, the chooser's name */
static wsk_tree_t chtrees[4];           /* the chooser's slots (§5ae) */
static const char *chstrs[2 + WSK_CH_MAX];
static wsk_chooser_t chooser;
static int choosing;                    /* 0, or what the chooser was opened for: 'o' or 's' */

/*
 * A second window (§5af): Ctrl-N opens a scratch pad -- its own root, its
 * own toolkit, its own text -- typed into apart from the editor. Its close
 * button closes it; the editor's, with the pad open, closes the editor and
 * leaves the pad; the last window closed ends the program.
 */
#define SW_W 600
#define SW_H 360
static const wsk_widget_t PAD[] = {
	{ .kind = WSK_LABEL, .id = "padline", .text = "scratch: type here", .size = 32 },
	{ .kind = WSK_MEMO,  .id = "pad",     .sub = 0, .weight = 1 },
};
static char padtext[8192];
static wsk_buf_t padbuf[1];
static wsk_tree_t padwin;
static wsk_ui_t padui;
static int pad = -1, main_open = 1;   /* the pad's window, or -1; the editor's still open */

static void say(const char *line);

static void pad_open(wsk_sink_t *s)
{
	int y0, y1;
	if (pad >= 0) return;
	pad = wh_window(SW_W, SW_H, "whisker -- scratch");
	if (pad < 0) { say("no room for another window"); return; }
	memset(padbuf, 0, sizeof padbuf);
	padbuf[0].b = padtext; padbuf[0].cap = sizeof padtext;
	padtext[0] = '\0';
	wsk_init(&padwin, WSK_COL, SW_W, SW_H);
	padwin.pad = 4; padwin.gap = 0; padwin.font = 18; padwin.item = PAD; padwin.n = 2;
	padwin.buf = padbuf; padwin.bufs = 1;
	padwin.caps = (unsigned char)wh_caps();
	padwin.talign = WSK_ALIGN_START;
	wsk_ui_init(&padui, &padwin);
	wsk_ui_focus(&padui, "pad", &y0, &y1);
	wh_on(pad);
	wh_draw_fit(&padwin, s, 0, SW_H);
	wh_on(0);
	say("scratch opened");
}
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
	for (int l = 0; l < (int)sizeof breaks && n < (int)(sizeof marks / sizeof marks[0]); l++)
		if (breaks[l]) { marks[n].line = l; marks[n].ink = 0xe51400u; marks[n].shape = WSK_GM_DOT; n++; }
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
	struct stat st;
	int len, was = buf[0].len, bad;
	char m[PATH_MAX + 64];
	if (f == NULL) { snprintf(m, sizeof m, "cannot open %s", p); say(m); return -1; }
	/* a folder, a device, a file bigger than the editor holds: refused,
	   not opened as empty or cut short -- a cut file saved back would lose
	   its tail (found by review, 2026-11-16) */
	if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > CAP - 1) {
		fclose(f);
		snprintf(m, sizeof m, "%s is %s: not opened", p,
		         (st.st_size > CAP - 1) ? "larger than this editor holds (400 KB)" : "not a file");
		say(m);
		return -1;
	}
	memcpy(keep, text, (size_t)was + 1);
	len = (int)fread(text, 1, CAP - 1, f);
	bad = ferror(f) || fgetc(f) != EOF;
	fclose(f);
	if (bad) {
		memcpy(text, keep, (size_t)was + 1);
		snprintf(m, sizeof m, "%s could not be read whole: not opened", p);
		say(m);
		return -1;
	}
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
	memset(breaks, 0, sizeof breaks);
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
	int w = 1024, h = 600, y0, y1, which = -1, e, dirty0 = 32767, dirty1 = 0;
	const wsk_tree_t *in = 0;
	wsk_sink_t s;
	FILE *f;

	if (argc > 1) path = argv[1];
	f = fopen(path, "rb");
	if (f == NULL) { fprintf(stderr, "code: cannot open %s\n", path); return 1; }
	buf[0].len = (int)fread(text, 1, CAP - 1, f);
	if (fgetc(f) != EOF) { fprintf(stderr, "code: %s is larger than this editor holds\n", path); return 1; }
	fclose(f);
	text[buf[0].len] = '\0';
	buf[0].b = text;
	buf[0].cap = CAP;
	undo.b = undo_b; undo.cap = sizeof undo_b;
	buf[0].undo = &undo;
	buf[0].tabkey = 1;                  /* Tab types a tab here; Shift-Tab still walks (§5ai) */
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
	/* whisker refuses what its host cannot draw, and says so: a control
	   character, a character beyond ASCII on a host that draws none (the SDL window --
	   /w0's font is ASCII), or < > & on a host that does not decode
	   entities (the boards, until they are flashed with RV-9 8e0ef96) */
	if ((e = wsk_check_in(&win, &which, &in)) != WSK_OK) {
		fprintf(stderr, "code: %s was refused (%d): %s\n", path, e,
		        e != WSK_E_TEXT ? "see design.md"
		        : (win.caps & WH_CAP_ENTITY) ? "it holds a control character or malformed UTF-8"
		        : "it holds a control character, a character beyond ASCII, or < > & -- which this host "
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
	wh_draw_fit(&win, &s, 0, h);

	for (;;) {
		wh_event_t he;
		wsk_event_t ev;
		wsk_note_t note[4];
		int k = wh_poll(&he), n, text_changed = 0;

		/* the rows to draw are gathered while events wait, and drawn when
		   none does: a drag sends a move for every pixel, and drawing each
		   left the window behind the pointer */
		if (k == WH_QUIT) break;
		if (k == WH_NOTHING) {
			if (dirty1 > dirty0 && main_open) { wh_on(0); wh_draw_fit(&win, &s, dirty0, dirty1); dirty0 = 32767; dirty1 = 0; }
			fflush(stdout);
			wh_wait(20);
			continue;
		}
		/* a window's close button (§5af): the pad's closes the pad; the
		   editor's closes the editor, and the program only when no window
		   is left */
		if (k == WH_CLOSE) {
			if (he.win == pad && pad >= 0) {
				wh_window_close(pad); pad = -1; say("scratch closed");
				dirty0 = 0; dirty1 = h;             /* Output, drawn when events stop */
			}
			else if (he.win == 0) { wh_window_close(0); main_open = 0; printf("editor closed\n"); }
			fflush(stdout);
			if (!main_open && pad < 0) break;
			continue;
		}
		if (he.win != 0 && he.win == pad) {
			/* the pad's own events, to its own toolkit, drawn in its window */
			wsk_event_t pe;
			wsk_note_t pn[4];
			int p0, p1;
			if (k == WH_EXPOSE) { p0 = 0; p1 = SW_H; }
			else {
				memset(&pe, 0, sizeof pe);
				pe.x = (short)he.x; pe.y = (short)he.y; pe.finger = (unsigned char)he.finger;
				if (k == WH_PRESS) { pe.kind = WSK_EV_DOWN; pe.key = (unsigned short)he.key; pe.d = (short)he.d; }
				else if (k == WH_RELEASE) pe.kind = WSK_EV_UP;
				else if (k == WH_MOVE) pe.kind = WSK_EV_MOVE;
				else if (k == WH_WHEEL) { pe.kind = WSK_EV_WHEEL; pe.d = (short)he.d; }
				else if (k == WH_KEY) { pe.kind = WSK_EV_KEY; pe.key = (unsigned short)he.key; }
				else if (k == WH_TEXT) { pe.kind = WSK_EV_TEXT; memcpy(pe.text, he.text, sizeof pe.text); }
				else continue;
				(void)wsk_feed(&padui, &pe, pn, 4, &p0, &p1);
			}
			if (p1 > p0) { wh_on(pad); wh_draw_fit(&padwin, &s, p0, p1); wh_on(0); }
			continue;
		}
		if (!main_open) continue;
		if (k == WH_EXPOSE) { dirty0 = 0; dirty1 = h; continue; }
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
				/* written beside it, then put in its place: a write that
				   fails -- a full disk, which may only say so at the close --
				   leaves the old file as it was (found by review) */
				char tmp[PATH_MAX + 16], m[PATH_MAX + 32];
				FILE *f;
				int okw = 0;
				snprintf(tmp, sizeof tmp, "%s.saving", chooser.path);
				f = fopen(tmp, "wb");
				if (f != NULL) {
					okw = (int)fwrite(text, 1, (size_t)buf[0].len, f) == buf[0].len;
					okw = (fflush(f) == 0) && okw;
					okw = (fsync(fileno(f)) == 0 || errno == EINVAL) && okw;
					okw = (fclose(f) == 0) && okw;
					if (okw) okw = (rename(tmp, chooser.path) == 0);
					if (!okw) unlink(tmp);
				}
				if (okw) {
					snprintf(pathbuf, sizeof pathbuf, "%s", chooser.path);
					path = pathbuf;
					snprintf(m, sizeof m, "saved %s", chooser.path);
				} else snprintf(m, sizeof m, "could not write %s: nothing changed", chooser.path);
				say(m);
			} else if (r == WSK_CANCELLED) say("nothing chosen");
			if (r != 0) choosing = 0;
			y0 = 0; y1 = h;
		}
		for (int i = 0; i < n && i < 4; i++) {
			const wsk_note_t *t = &note[i];
			if (t->kind == WSK_NOTE_TEXT) text_changed = 1;
			else if (t->kind == WSK_NOTE_GUTTER && t->value < (int)sizeof breaks) {
				char m[48];
				breaks[t->value] = (unsigned char)!breaks[t->value];
				snprintf(m, sizeof m, "breakpoint %s line %d", breaks[t->value] ? "on " : "off", t->value + 1);
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
			           (t->value == ('n' | WSK_MOD_CTRL) || t->value == ('n' | WSK_MOD_ALT))) {
				/* Alt-N too: a browser keeps Ctrl-N for a window of its own */
				pad_open(&s);                       /* a second window (§5af) */
				y0 = 0; y1 = h;                     /* and what it said, in Output */
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
		if (y1 > y0) {
			if (y0 < dirty0) dirty0 = y0;
			if (y1 > dirty1) dirty1 = y1;
		}
	}
	wh_close();
	return 0;
}
