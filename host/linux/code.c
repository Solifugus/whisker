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
 *
 * Several viewers at once (§5al): served by the browser host, each screen --
 * a page, or whisker-client over SSH -- is a seat with its own window, its
 * own caret, selection, view, divider and file dialog, over ONE document.
 * An edit by one moves the others' carets and views with the text. A viewer
 * who detaches leaves the program running and the document as it was,
 * unsaved edits and all; one who comes back under the same name (wh_who)
 * finds their caret and their view where they left them, moved by whatever
 * the others did meanwhile -- but their focus starts fresh, on the code, and
 * any dialog they left open is gone. Undo is the document's: Ctrl-Z undoes
 * the last edit, whoever made it. In its own SDL window there is one seat.
 */
#include "host.h"
#include "whisker.h"
#include "whisker_chooser.h"
#include "textfile.h"
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
static textfile_t tf;                   /* the open file's line endings and encoding, kept for its save (§5aj) */
static wsk_ink_t inks[INKS];
static unsigned char breaks[65536];        /* breakpoints, by line */
static wsk_gmark_t marks[4096];         /* ...as whisker draws them, in order */
static int textlen, outlen, ninks, nmarks;   /* the document's, every seat's view of it */

/* A viewer's seat (§5al): its own window over the one document. */
typedef struct {
	int           on;
	wsk_tree_t    win;
	wsk_widget_t  items[4];
	wsk_buf_t     buf[3];           /* the code, the output -- the document's text, its own
	                                   caret and view -- and the chooser's name */
	wsk_tree_t    chtrees[4];       /* the chooser's slots (§5ae) */
	const char   *chstrs[2 + WSK_CH_MAX];
	wsk_chooser_t chooser;
	int           choosing;         /* 0, or what the chooser was opened for: 'o' or 's' */
	short         sizes[4];         /* the panes, as its divider leaves them */
	wsk_ui_t      ui;
	int           d0, d1;           /* its rows to draw, when events stop */
	char          who[64];          /* wh_who: the name its viewer gave, or "" */
} seat_t;
static seat_t seat[WH_SEATS];

/* What a viewer who has gone keeps for when they come back: their caret and
   their view, moved by the others' edits meanwhile. Not their focus. */
typedef struct { char who[64]; int caret, top; } viewer_t;
#define VIEWERS 32
static viewer_t viewers[VIEWERS];
static int nviewers;

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
	wh_to(-1);                          /* one pad, on every seat's screen */
	wh_on(pad);
	wh_draw_fit(&padwin, s, 0, SW_H);
	wh_on(0);
	say("scratch opened");
}
static char pathbuf[PATH_MAX];
static wsk_undo_t undo;
static const char *path = "sample.bas";
static const int W = 1024, H = 600;

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
	int n = 0, i = 0, len = textlen;
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
	ninks = n;
	return n;
}

/* Every seat's view of the document brought up to date: its length, its
   colours and marks, the output's newest line in sight. A caret past the
   end comes back to it. */
static void views(void)
{
	for (int i = 0; i < WH_SEATS; i++) {
		wsk_buf_t *b = seat[i].buf;
		if (!seat[i].on) continue;
		b[0].len = textlen;
		b[0].inks = inks; b[0].ninks = ninks;
		b[0].marks = marks; b[0].nmarks = nmarks;
		if (b[0].caret > textlen) b[0].caret = textlen;
		if (b[0].caret + b[0].span < 0 || b[0].caret + b[0].span > textlen) b[0].span = 0;
		b[1].len = outlen;
		b[1].caret = outlen;            /* the view follows the newest */
		b[1].span = 0;
	}
}

/* Every seat drawn again whole, when events stop. */
static void all_dirty(void)
{
	for (int i = 0; i < WH_SEATS; i++) if (seat[i].on) { seat[i].d0 = 0; seat[i].d1 = H; }
}

/* A line for the output pane, and for stdout; the oldest go when it fills. */
static void say(const char *line)
{
	int n = (int)strlen(line);
	printf("%s\n", line);
	if (n + 1 >= (int)sizeof out) return;
	while (outlen + n + 1 >= (int)sizeof out) {        /* drop the first line */
		char *nl = memchr(out, '\n', (size_t)outlen);
		int cut = nl ? (int)(nl - out) + 1 : outlen;
		memmove(out, out + cut, (size_t)(outlen - cut + 1));
		outlen -= cut;
	}
	if (outlen > 0) out[outlen++] = '\n';
	memcpy(out + outlen, line, (size_t)n + 1);
	outlen += n;
	views();                                 /* every seat's output pane */
	all_dirty();
}

/* The breakpoints as gutter marks: one a line, in order (§5v step 4). */
static void remark(void)
{
	int n = 0;
	for (int l = 0; l < (int)sizeof breaks && n < (int)(sizeof marks / sizeof marks[0]); l++)
		if (breaks[l]) { marks[n].line = l; marks[n].ink = 0xe51400u; marks[n].shape = WSK_GM_DOT; n++; }
	nmarks = n;                         /* set even with none: the column is kept */
}

/* Where a seat's caret is, for its status line. */
static void status(seat_t *st)
{
	int line = 1, col = 1;
	for (int i = 0; i < st->buf[0].caret; i++) {
		if (text[i] == '\n') { line++; col = 1; }
		else if (((unsigned char)text[i] & 0xC0) != 0x80) col++;   /* characters, not bytes */
	}
	const char *base = strrchr(path, '/');
	char t[96];
	char kind[24];
	tf_name(&tf, kind, sizeof kind);
	snprintf(t, sizeof t, "%.16s  %d:%d%s%s", base ? base + 1 : path, line, col, kind[0] ? "  " : "", kind);
	snprintf(st->items[0].text, WSK_TEXT_MAX, "%.31s", t);
}

static void to_window(wsk_sink_t *s, const char *b, int n) { (void)s; wh_write(b, n); }

/* Read a file into the editor: 0, or -1 with the reason said -- and a file
   this window cannot draw refused, the one open kept. */
static int load(seat_t *by, const char *p)
{
	static char keep[CAP];
	FILE *f = fopen(p, "rb");
	struct stat st;
	int len, was = textlen, bad;
	textfile_t was_tf = tf;
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
	len = tf_in(text, len, CAP, &tf);       /* CRLF or CR, Latin-1: edited as \n and UTF-8 */
	if (len < 0) {
		memcpy(text, keep, (size_t)was + 1);
		tf = was_tf;
		snprintf(m, sizeof m, "%s is larger than this editor holds once decoded: not opened", p);
		say(m);
		return -1;
	}
	/* checked in the seat that asked; the document is every seat's */
	textlen = len;
	ninks = 0;
	for (int i = 0; i < WH_SEATS; i++)
		if (seat[i].on) { seat[i].buf[0].caret = 0; seat[i].buf[0].span = 0; seat[i].buf[0].top = 0; }
	views();
	if (wsk_check(&by->win, 0) != WSK_OK) {
		memcpy(text, keep, (size_t)was + 1);
		tf = was_tf;
		textlen = was;
		colour();
		views();
		snprintf(m, sizeof m, "%s holds what this window cannot draw: not opened", p);
		say(m);
		return -1;
	}
	for (int i = 0; i < nviewers; i++) viewers[i].caret = viewers[i].top = 0;   /* a new document */
	undo.used = 0; undo.end = 0;
	memset(breaks, 0, sizeof breaks);
	snprintf(pathbuf, sizeof pathbuf, "%s", p);
	path = pathbuf;
	colour();
	remark();
	views();
	snprintf(m, sizeof m, "opened %s", p);
	say(m);
	return 0;
}

/* A seat set up over the document: its tree, its buffers -- the shared
   text, its own caret -- its toolkit and its chooser. */
static int seat_init(int i)
{
	seat_t *st = &seat[i];
	int y0, y1;
	memset(st, 0, sizeof *st);
	st->on = 1;
	memcpy(st->items, WIN, sizeof WIN);
	st->buf[0].b = text; st->buf[0].cap = CAP;
	st->buf[0].undo = &undo;
	st->buf[0].tabkey = 1;              /* Tab types a tab here; Shift-Tab still walks (§5ai) */
	st->buf[1].b = out; st->buf[1].cap = sizeof out;
	wsk_init(&st->win, WSK_COL, W, H);
	st->win.pad = 4; st->win.gap = 0; st->win.font = 18; st->win.ids = 1;
	st->win.item = st->items; st->win.n = 4;
	st->win.buf = st->buf; st->win.bufs = 3;
	st->win.nest = st->chtrees; st->win.nests = 4;
	st->win.str = st->chstrs; st->win.strs = 2 + WSK_CH_MAX;
	for (int k = 0; k < 4; k++) wsk_nest_init(&st->chtrees[k], &st->win, WSK_COL);
	st->win.sizes = st->sizes;
	st->win.caps = (unsigned char)wh_seat_caps(i);   /* what THIS viewer's screen draws (§5am) */
	st->win.talign = WSK_ALIGN_START;
	views();
	wsk_ui_init(&st->ui, &st->win);
	st->ui.clip = clip; st->ui.clipcap = sizeof clip;
	if (wsk_chooser_init(&st->chooser, &st->win, st->chstrs, 0, 0, 2, posix_lister, 0) != WSK_OK) return -1;
	wsk_ui_focus(&st->ui, "code", &y0, &y1);   /* focus starts fresh, on the code */
	status(st);
	st->d0 = 0; st->d1 = H;
	return 0;
}

/* The line `line` starts at, in t[0..len). */
static int line_at(const char *t, int len, int line)
{
	int at = 0;
	for (int l = 0; l < line && at < len; at++) if (t[at] == '\n') l++;
	return at;
}

/* A caret through an edit: old [a, oe) became new [a, ne). Before it,
   unmoved; after it, moved by the difference; inside what changed, to its
   start -- always between characters. */
static int through(int c, int a, int oe, int ne)
{
	if (c <= a) return c;
	if (c >= oe) return c + (ne - oe);
	return a;
}

/* The document was edited by seat `by` (§5al): `prev` is the text before.
   Every other seat's caret and view, and every away viewer's, move with
   it; every other seat is drawn again. */
static char prev[CAP];
static int prevlen;
static void edited(int by)
{
	int a = 0, z = 0, oe, ne, lines = 0;
	while (a < prevlen && a < textlen && prev[a] == text[a]) a++;
	while (z < prevlen - a && z < textlen - a && prev[prevlen - 1 - z] == text[textlen - 1 - z]) z++;
	oe = prevlen - z;
	ne = textlen - z;
	if (oe == a && ne == a) return;                 /* nothing changed */
	for (int i = a; i < ne; i++) if (text[i] == '\n') lines++;
	for (int i = a; i < oe; i++) if (prev[i] == '\n') lines--;
	for (int i = 0; i < WH_SEATS; i++) {
		wsk_buf_t *b = seat[i].buf;
		int c, e;
		if (!seat[i].on || i == by) continue;
		c = b[0].caret; e = c + b[0].span;
		b[0].caret = through(c, a, oe, ne);
		b[0].span = through(e, a, oe, ne) - b[0].caret;
		if (line_at(prev, prevlen, b[0].top) >= oe) b[0].top += lines;   /* lines came or went above */
		if (b[0].top < 0) b[0].top = 0;
		status(&seat[i]);
		seat[i].d0 = 0; seat[i].d1 = H;
	}
	for (int i = 0; i < nviewers; i++) {
		if (line_at(prev, prevlen, viewers[i].top) >= oe) viewers[i].top += lines;
		if (viewers[i].top < 0) viewers[i].top = 0;
		viewers[i].caret = through(viewers[i].caret, a, oe, ne);
	}
}

/* A viewer arriving (§5al): who they say they are, and if they were here
   before, their caret and view back -- not their focus, which is fresh. */
static void joined(int i)
{
	const char *who = wh_who(i);
	seat_t *st;
	if (seat_init(i) != 0) return;
	st = &seat[i];
	snprintf(st->who, sizeof st->who, "%s", who);
	for (int v = 0; st->who[0] && v < nviewers; v++)
		if (strcmp(viewers[v].who, st->who) == 0) {
			int c = viewers[v].caret, lines = 0;
			if (c > textlen) c = textlen;
			while (c > 0 && ((unsigned char)text[c] & 0xC0) == 0x80) c--;   /* between characters */
			for (int k = 0; k < textlen; k++) if (text[k] == '\n') lines++;
			st->buf[0].caret = c;
			st->buf[0].span = 0;
			st->buf[0].top = (viewers[v].top > lines) ? lines : viewers[v].top;
			status(st);
			printf("seat %d: %s is back -- caret %d, line %d at the top\n", i, st->who, c, st->buf[0].top + 1);
			return;
		}
	printf("seat %d: %s joined\n", i, st->who[0] ? st->who : "a viewer");
}

/* A viewer leaving: the program goes on, and keeps their caret and view. */
static void left(int i)
{
	seat_t *st = &seat[i];
	int v;
	if (!st->on) return;
	if (st->who[0]) {
		for (v = 0; v < nviewers && strcmp(viewers[v].who, st->who) != 0; v++) ;
		if (v == nviewers && nviewers < VIEWERS) nviewers++;
		if (v < nviewers) {
			snprintf(viewers[v].who, sizeof viewers[v].who, "%s", st->who);
			viewers[v].caret = st->buf[0].caret;
			viewers[v].top = st->buf[0].top;
		}
	}
	printf("seat %d: %s left -- caret %d, line %d at the top; the program goes on\n", i,
	       st->who[0] ? st->who : "a viewer", st->buf[0].caret, st->buf[0].top + 1);
	st->on = 0;
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
	int y0, y1, which = -1, e;
	const wsk_tree_t *in = 0;
	wsk_sink_t s;
	FILE *f;

	if (argc > 1) path = argv[1];
	f = fopen(path, "rb");
	if (f == NULL) { fprintf(stderr, "code: cannot open %s\n", path); return 1; }
	textlen = (int)fread(text, 1, CAP - 1, f);
	if (fgetc(f) != EOF) { fprintf(stderr, "code: %s is larger than this editor holds\n", path); return 1; }
	fclose(f);
	text[textlen] = '\0';
	textlen = tf_in(text, textlen, CAP, &tf);   /* written back as it came (§5aj) */
	if (textlen < 0) { fprintf(stderr, "code: %s is larger than this editor holds once decoded\n", path); return 1; }
	undo.b = undo_b; undo.cap = sizeof undo_b;
	colour();
	remark();
	out[0] = '\0';

	/* checked once before any window: a seat set up to ask, then put away
	   until a viewer arrives (wh_seats) */
	if (seat_init(0) != 0) return 1;
	/* what this host cannot draw, the source shows as markers (§5aj); a
	   file is refused only for what a memo cannot hold at all -- a NUL */
	if ((e = wsk_check_in(&seat[0].win, &which, &in)) != WSK_OK) {
		fprintf(stderr, "code: %s was refused (%d): %s\n", path, e,
		        e != WSK_E_TEXT ? "see design.md" : "it holds a NUL byte: not text");
		return 1;
	}
	seat[0].on = 0;
	{
		char m[96];
		snprintf(m, sizeof m, "opened %.80s", path);
		say(m);
	}
	if (wh_open(W, H, 1, "whisker -- source") != 0) return 1;
	s.put = to_window; s.ctx = 0; s.len = 0;
	wh_seats(1);                        /* a seat a viewer (§5al); SDL's one window is seat 0 */

	for (;;) {
		wh_event_t he;
		wsk_event_t ev;
		wsk_note_t note[4];
		int k = wh_poll(&he), n, text_changed = 0, snap = 0;
		seat_t *st;

		/* the rows to draw are gathered while events wait, and drawn when
		   none does -- each seat its own: a drag sends a move for every
		   pixel, and drawing each left the window behind the pointer */
		if (k == WH_QUIT) break;
		if (k == WH_NOTHING) {
			for (int i = 0; i < WH_SEATS; i++)
				if (seat[i].on && seat[i].d1 > seat[i].d0 && main_open) {
					wh_to(i); wh_on(0);
					wh_draw_fit(&seat[i].win, &s, seat[i].d0, seat[i].d1);
					seat[i].d0 = 32767; seat[i].d1 = 0;
				}
			wh_to(-1);
			fflush(stdout);
			wh_wait(20);
			continue;
		}
		if (k == WH_JOIN) {
			joined(he.seat);
			if (pad >= 0) { wh_to(he.seat); wh_on(pad); wh_draw_fit(&padwin, &s, 0, SW_H); wh_on(0); wh_to(-1); }
			continue;
		}
		if (k == WH_GONE) { left(he.seat); fflush(stdout); continue; }
		if (k == WH_SIZE) continue;             /* one size for every seat: the editor's */
		/* a window's close button (§5af): the pad's closes the pad; the
		   editor's closes the editor, and the program only when no window
		   is left */
		if (k == WH_CLOSE) {
			if (he.win == pad && pad >= 0) {
				wh_window_close(pad); pad = -1; say("scratch closed");
			}
			else if (he.win == 0) { wh_window_close(0); main_open = 0; printf("editor closed\n"); }
			fflush(stdout);
			if (!main_open && pad < 0) break;
			continue;
		}
		if (he.win != 0 && he.win == pad) {
			/* the pad's own events, to its own toolkit, drawn in its window --
			   one pad, every seat's */
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
			if (p1 > p0) { wh_to(-1); wh_on(pad); wh_draw_fit(&padwin, &s, p0, p1); wh_on(0); }
			continue;
		}
		if (!main_open || he.seat < 0 || he.seat >= WH_SEATS || !seat[he.seat].on) continue;
		st = &seat[he.seat];                    /* this viewer's seat, and no other's */
		if (k == WH_EXPOSE) { st->d0 = 0; st->d1 = H; continue; }
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

		/* the text before a key or a paste may change it: what the others'
		   carets are moved through, when there are others to move */
		if (k == WH_KEY || k == WH_TEXT) {
			int others = nviewers;
			for (int i = 0; i < WH_SEATS; i++) if (seat[i].on && st != &seat[i]) others++;
			if (others > 0) { memcpy(prev, text, (size_t)textlen + 1); prevlen = textlen; snap = 1; }
		}
		n = wsk_feed(&st->ui, &ev, note, 4, &y0, &y1);
		textlen = st->buf[0].len;               /* the document's, whoever typed */
		/* the chooser's, while it is open (§5ae) */
		if (st->choosing) {
			int r = wsk_chooser_feed(&st->chooser, &st->ui, note, n, &y0, &y1);
			if (r == WSK_CHOSEN && st->choosing == 'o') {
				(void)load(st, st->chooser.path);
			} else if (r == WSK_CHOSEN && st->choosing == 's') {
				/* written beside it, then put in its place: a write that
				   fails -- a full disk, which may only say so at the close --
				   leaves the old file as it was (found by review) */
				char tmp[PATH_MAX + 16], m[PATH_MAX + 32];
				FILE *f;
				int okw = 0;
				snprintf(tmp, sizeof tmp, "%s.saving", st->chooser.path);
				f = fopen(tmp, "wb");
				if (f != NULL) {
					okw = tf_out(f, text, textlen, &tf) == 0;   /* its own line endings and encoding */
					okw = (fflush(f) == 0) && okw;
					okw = (fsync(fileno(f)) == 0 || errno == EINVAL) && okw;
					okw = (fclose(f) == 0) && okw;
					if (okw) okw = (rename(tmp, st->chooser.path) == 0);
					if (!okw) unlink(tmp);
				}
				if (okw) {
					snprintf(pathbuf, sizeof pathbuf, "%s", st->chooser.path);
					path = pathbuf;
					snprintf(m, sizeof m, "saved %s", st->chooser.path);
				} else if (tf.enc == ENC_LATIN1 && !tf_latin1_ok(text, textlen))
					snprintf(m, sizeof m, "%s is Latin-1, and the text has a character it cannot hold: not saved", st->chooser.path);
				else snprintf(m, sizeof m, "could not write %s: nothing changed", st->chooser.path);
				say(m);
			} else if (r == WSK_CANCELLED) say("nothing chosen");
			if (r != 0) st->choosing = 0;
			y0 = 0; y1 = H;
		}
		for (int i = 0; i < n && i < 4; i++) {
			const wsk_note_t *t = &note[i];
			if (t->kind == WSK_NOTE_TEXT) text_changed = 1;
			else if (t->kind == WSK_NOTE_GUTTER && t->value < (int)sizeof breaks) {
				char m[48];
				breaks[t->value] = (unsigned char)!breaks[t->value];
				snprintf(m, sizeof m, "breakpoint %s line %d", breaks[t->value] ? "on " : "off", t->value + 1);
				remark();
				views();
				say(m);                         /* every seat: the gutter and Output */
			} else if (t->kind == WSK_NOTE_COPY) {
				wh_to(he.seat);                 /* the clipboard of the screen that copied */
				wh_copy(clip, t->value);
				wh_to(-1);
			} else if (t->kind == WSK_NOTE_PASTE) {
				static char pb[CAP];
				wsk_note_t pn[2];
				int m0, m1, pl = wh_paste(pb, sizeof pb);
				if (pl > 0 && wsk_ui_paste(&st->ui, pb, pl, pn, 2, &m0, &m1) > 0) text_changed = 1;
				textlen = st->buf[0].len;
			} else if (t->kind == WSK_NOTE_KEY && t->value == ('s' | WSK_MOD_CTRL)) {
				say("save: only Save as (Ctrl-Shift-S) writes, to a name you choose");
			} else if (t->kind == WSK_NOTE_KEY && !st->choosing &&
			           (t->value == ('n' | WSK_MOD_CTRL) || t->value == ('n' | WSK_MOD_ALT))) {
				/* Alt-N too: a browser keeps Ctrl-N for a window of its own */
				pad_open(&s);                       /* a second window (§5af) */
			} else if (t->kind == WSK_NOTE_KEY && !st->choosing &&
			           (t->value == ('o' | WSK_MOD_CTRL) || t->value == ('s' | WSK_MOD_CTRL | WSK_MOD_SHIFT))) {
				/* the chooser (§5ae): open a file, or save as -- this seat's */
				char at[PATH_MAX];
				const char *base = strrchr(path, '/');
				int open = (t->value == ('o' | WSK_MOD_CTRL)), cy0, cy1;
				folder_of(path, at, sizeof at);
				if (wsk_chooser_open(&st->chooser, &st->ui, open ? WSK_CHOOSE_FILE : WSK_CHOOSE_SAVE,
				                     open ? "Open a file" : "Save as", at,
				                     open ? 0 : (base ? base + 1 : path), 0, 640, 440, &cy0, &cy1) == WSK_OK) {
					st->choosing = open ? 'o' : 's';
					/* one viewer's dialog: said on stdout, not in everyone's Output */
					printf("seat %d: %s\n", he.seat, open ? "choose a file to open" : "choose a name to save as");
				} else say("that folder cannot be read");
				y0 = 0; y1 = H;
			} else if (t->kind == WSK_NOTE_SPLIT)
				printf("output pane %d high\n", t->value);   /* each step of a drag: not the pane's */
		}
		/* the text changed: colour it again, BEFORE drawing (§5v rule 4) --
		   and the others' carets and views moved with it (§5al) */
		if (text_changed && !st->choosing) {
			colour();
			views();
			if (snap) edited(he.seat);
			y0 = 0; y1 = H;                 /* a line's colours can change the lines after */
		}
		status(st);
		y0 = 0;                             /* the status line moves with every key */
		if (y1 <= 32) y1 = 32;
		fflush(stdout);
		if (y1 > y0) {
			if (y0 < st->d0) st->d0 = y0;
			if (y1 > st->d1) st->d1 = y1;
		}
	}
	wh_close();
	return 0;
}
