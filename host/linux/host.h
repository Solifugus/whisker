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
 * what changed, on whisker's side of the seam.
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
	WH_EXPOSE                /* draw everything again: a screen has come
	                            that has not seen the picture (§5k) -- X's
	                            Expose, for the same reason */
};

/* The keys a field cares about. The same numbers as whisker's WSK_KEY_*, so
   a host loop passes them straight through; the host does not include
   whisker to know them, and nothing here is a widget. */
enum {
	WH_KEY_LEFT = 1, WH_KEY_RIGHT, WH_KEY_HOME, WH_KEY_END,
	WH_KEY_BACKSPACE, WH_KEY_DELETE, WH_KEY_ENTER, WH_KEY_TAB, WH_KEY_BACKTAB,
	WH_KEY_ESCAPE            /* the web host sends it; SDL's Escape quits */
};

typedef struct {
	int  kind;               /* WH_* */
	int  x, y;               /* device pixels, as the panel reports them */
	int  d;                  /* WH_WHEEL */
	int  key;                /* WH_KEY */
	char text[8];            /* WH_TEXT */
} wh_event_t;

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

/* Wait up to this long for something to happen, so a loop need not spin. */
void wh_wait(int ms);

void wh_close(void);

#endif
