/*
 * The substrate contract, on Linux.
 *
 * design.md §3 says whisker talks to a host through two byte channels and
 * nothing else:
 *
 *     out:  a complete SVG document, plus the row range it needs repainted
 *     in:   a point -- x, y, and whether it is a press or a release
 *
 * and, since §5j, what a desktop has that a panel does not: the pointer
 * moving while pressed, the wheel, a key that means something to a field,
 * and text typed. Still input, still no widget: the toolkit turns these into
 * what changed, on whisker's side of the seam. Since §5s, a right-click, the
 * modifiers on a key, and the system clipboard -- which is the host's, since
 * whisker has none and no way to reach one.
 *
 * On RV-9 that is four operations on a path: open /w0, setstat
 * RV9_SVG_SS_ROWS, write, and read a point from whatever device supplies one.
 * This is the same four, and deliberately not one more -- §6 sets the bar
 * that a Linux host must be SMALL, because gBASIC already has a toolkit and a
 * whisker that needs another one has bought it nothing.
 *
 * So: a window, a blit, a point. No widget lives on this side of the seam.
 */
#ifndef WHISKER_HOST_H
#define WHISKER_HOST_H

enum {
	WH_NOTHING = 0,
	WH_PRESS,
	WH_RELEASE,
	WH_QUIT,
	WH_MOVE,                 /* the pointer moved while pressed */
	WH_WHEEL,                /* d: pixels, positive scrolls toward the end */
	WH_KEY,                  /* key: one of WH_KEY_* */
	WH_TEXT,                 /* text: what was typed, UTF-8, NUL-ended */
	WH_EXPOSE,               /* draw everything again: a screen has come
	                            that has not seen the picture (§5k) -- X's
	                            Expose, for the same reason */
	WH_MENU,                 /* a right-click, or what the platform calls
	                            asking for a context menu: at x, y (§5s) */
	WH_HOVER,                /* the pointer moved with no button held, at
	                            x, y -- or left the window, at -1, -1. A
	                            host with no pointer never sends it (§5aa) */
	WH_JOIN,                 /* a screen arrived: `seat` (§5ab) -- only to a
	                            program that called wh_seats(1) */
	WH_GONE,                 /* ... and went -- or went quiet (§5ab step 2) */
	WH_SIZE,                 /* a seat's screen is x by y pixels: at its
	                            join, and when resized (§5ab step 2) */
	WH_CLOSE                 /* window `win`'s close button: the program
	                            decides -- once it has asked for a second
	                            window; until then it is WH_QUIT (§5af) */
};

/* The keys a field cares about. The same numbers as whisker's WSK_KEY_*, so
   a host loop passes them straight through; the host does not include
   whisker to know them, and nothing here is a widget. */
enum {
	WH_KEY_LEFT = 1, WH_KEY_RIGHT, WH_KEY_HOME, WH_KEY_END,
	WH_KEY_BACKSPACE, WH_KEY_DELETE, WH_KEY_ENTER, WH_KEY_TAB, WH_KEY_BACKTAB,
	WH_KEY_ESCAPE,           /* closes a menu; both hosts send it (§5s) */
	WH_KEY_UP, WH_KEY_DOWN,  /* a memo's lines (§5o) */
	WH_KEY_MENU,             /* the Menu key, Shift-F10 (§5s) */
	WH_KEY_PAGEUP, WH_KEY_PAGEDOWN   /* a page of a memo or a list (§5ak) */
};

/* Modifiers on `key` (§5s), whisker's WSK_MOD_* numbers: on a named key, or
   on a printable character, lower-case, for a chord -- Ctrl-S is
   's' | WH_MOD_CTRL. On a press, whether Shift was held. */
enum { WH_MOD_SHIFT = 0x100, WH_MOD_CTRL = 0x200, WH_MOD_ALT = 0x400,
       WH_MOD_TOUCH = 0x800 };   /* on a press: a finger, not a mouse (§5ac) */

typedef struct {
	int  kind;               /* WH_* */
	int  x, y;               /* device pixels, as the panel reports them */
	int  d;                  /* WH_WHEEL; on WH_PRESS, the click count --
	                            the platform's double-click, counted (§5w) */
	int  key;                /* WH_KEY; on WH_PRESS, WH_MOD_SHIFT or 0 */
	char text[8];            /* WH_TEXT */
	int  seat;               /* the screen it came from: 0 .. WH_SEATS-1 (§5ab) */
	int  finger;             /* PRESS, MOVE, RELEASE: which finger, 0..255; a
	                            mouse is 0 (§5ad) */
	int  win;                /* the window it came from: 0, wh_open's (§5af) */
} wh_event_t;

/*
 * Windows (§5af): wh_open's is 0; wh_window opens another and answers its
 * number, or -1; wh_window_close closes one; wh_on says which the
 * documents after it go to (as wh_to says which seat). A program that
 * opens a second window hears close buttons as WH_CLOSE with their `win`
 * -- the last one closed, it decides to end. One that never does is the
 * host as it always was.
 */
int  wh_window(int w, int h, const char *title);
void wh_window_close(int win);
void wh_on(int win);

/* A window's title, changed -- 0's is wh_open's to begin with -- and a
   window brought to the front of the others (§5as). A screen shows the
   title its way: SDL on the window, the page on its tab and a panel's bar,
   the client on its own window. */
void wh_title(int win, const char *title);
void wh_raise(int win);

/*
 * Seats (§5ab): one person's screen and input. A program that serves several
 * people calls wh_seats(1), then hears WH_JOIN and WH_GONE with each event's
 * `seat`, and sends a document to one screen with wh_to(seat) before it
 * (-1, every screen). One that never calls it sees one screen, mirrored to
 * every page -- the host as it always was. SDL has one seat, 0.
 */
#define WH_SEATS 16
void wh_seats(int on);
void wh_to(int seat);

/*
 * Who a seat says it is (§5al): the name its viewer gave when it connected
 * -- letters, digits, '-', '_' and '.', at most 63 -- or "" if it gave none.
 * A CLAIM, kept so that a viewer who comes back finds what was theirs (their
 * caret, their view); never proof of who someone is, so never a reason to
 * let them do anything. whisker reads none of it; it is the program's.
 */
const char *wh_who(int seat);

/* What this host can draw beyond /w0, as whisker's WSK_CAP_* bits (§5r): a
   program puts it in its root's `caps`. The host says; whisker never guesses.
   The same numbers as whisker's, so the host need not include whisker. */
enum { WH_CAP_UTF8 = 1, WH_CAP_IMAGE = 2, WH_CAP_CLIP = 4, WH_CAP_OPACITY = 8,
       WH_CAP_ENTITY = 16 };     /* decodes &lt; &gt; &amp; (§5v) */
int  wh_caps(void);

/* What ONE seat's screen draws (§5am): a browser, a native client, a board
   differ, and a program with seats draws each its own -- this, not wh_caps,
   which is what every screen at once can draw (a program that sends them
   all the same picture). A screen that says nothing is taken at the host's
   word. SDL's one screen is wh_caps(). */
int  wh_seat_caps(int seat);

/* Open a window of this many device pixels, shown at `scale` times size.
   Returns 0, or -1 with a reason on stderr. Like opening /w0. */
int  wh_open(int w, int h, int scale, const char *title);

/* The rows the next document should repaint; 0,0 means all of them.
   One-shot, exactly as RV9_SVG_SS_ROWS is. */
void wh_clip(int y0, int y1);

/* Write SVG. A document ends at "</svg>", at which point it is drawn --
   the same rule the device uses, so a caller may stream. */
void wh_write(const void *bytes, int n);

/*
 * The next input, or WH_NOTHING. Non-blocking.
 *
 * WH_NOTHING means nothing is ready *now*, not that nothing is queued: SDL
 * bounds how many events one poll returns, so an event pushed a moment ago
 * can arrive on the next call instead of this one. A loop that keeps polling
 * sees everything in order; one that polls once and then sleeps for a long
 * time can lag a cycle behind. Measured, not assumed -- it is why the self
 * test polls in a loop.
 */
int  wh_poll(wh_event_t *out);

/*
 * The system clipboard (§5s). wh_copy puts n bytes on it: 0, or -1 if the
 * host could not. wh_paste copies what it holds into b, NUL-ended: its
 * length, or -1 if it is empty, is not text, or would not fit in cap --
 * never a part of it.
 */
int  wh_copy(const char *s, int n);
int  wh_paste(char *b, int cap);

/* Milliseconds from some fixed moment, never going back: what a program
   measures to tell whisker how much time has passed (§5ac's ticks). */
long wh_ms(void);

/* Wait up to this long for something to happen, so a loop need not spin. */
void wh_wait(int ms);

void wh_close(void);

#endif
