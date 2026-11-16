/*
 * whisker -- a widget layer for a window you write SVG to.
 *
 * Labels, buttons and a text field in single-axis stacks, column or row,
 * any of which may scroll or wrap, and any of which may hold another stack.
 * All pure, none of them touching a device:
 *
 *     wsk_render(tree, sink, budget)                  one stack, whole screen
 *     wsk_rows(tree, ids, n, &y0, &y1)                which device rows moved
 *     wsk_render_clipped(tree, ids, n, sink, budget)  one stack, those rows
 *     wsk_hit(tree, x, y)                             which widget is there
 *     wsk_compose(body, ctx, sink, budget)            several stacks at once
 *     wsk_scroll_max(tree)                            how far a stack may scroll
 *     wsk_area_rows(tree, &y0, &y1)                   what a scroll step repaints
 *     wsk_slice(pane, total, size, scroll, &first)    a long list, the rows in view
 *     wsk_hit_in(tree, x, y, &in)                     the deepest widget there
 *     wsk_find_in(tree, "id" or "/a/b", &in)          a widget at any depth
 *     wsk_hit_word(tree, x, y, &in, &word)            which word of a paragraph
 *     wsk_render_rows(tree, y0, y1, sink, budget)     a band of rows, whatever moved
 *
 * And the toolkit (design.md §5i, §8), which is not pure -- it updates the
 * caller's trees -- but keeps nothing of its own:
 *
 *     wsk_ui_init(&ui, &root)
 *     wsk_feed(&ui, &event, notes, max, &y0, &y1)     an event in, what changed out
 *
 * The host writes what the sink collected to /w0, having first told the
 * window which rows to repaint:
 *
 *     wsk_rows(&t, ids, 1, &y0, &y1);
 *     uint32_t v = ((uint32_t)y0 << 16) | (uint32_t)y1;
 *     env->setstat(w0, RV9_SVG_SS_ROWS, &v);      // one-shot, every time
 *     wsk_render_clipped(&t, ids, 1, sink, &w0);
 *
 * Header-only, like RV-9's modlib.h (D1). No libc, no allocation, no static
 * data, no 64-bit division, no table of pointers -- a program on RV-9 is a
 * module and inherits every rule a module obeys (design.md §4.1).
 *
 * The caller owns the tree (D5) and an id is the address (D4).
 *
 * WHAT A CALLER'S PANEL MAY LOOK LIKE. Widget text sits *inside* the struct
 * rather than behind a `const char *`, so that a panel may be `static const`
 * and live in rodata. A table of pointers is addresses in rodata and does not
 * link into a module; inline arrays do (review-2026-09-30.md §3.1). The tree
 * itself is built at runtime, because it holds a pointer to that array:
 *
 *     static const wsk_widget_t PANEL[] = {
 *         { WSK_LABEL,  "title",  "Settings" },
 *         { WSK_BUTTON, "ok",     "OK" },
 *         { WSK_BUTTON, "cancel", "Cancel" },
 *     };
 *
 *     wsk_tree_t t;
 *     wsk_init(&t, WSK_COL, dev_w, dev_h);     // the size getstat returned
 *     t.item = PANEL;
 *     t.n = 3;
 */
#ifndef WHISKER_H
#define WHISKER_H

/* ------------------------------------------------------------------ */
/* Profiles -- what to leave out, for a host that cannot afford it     */
/* ------------------------------------------------------------------ */
/*
 * whisker serves hosts that differ by four orders of magnitude of memory
 * (design.md §2.8), so the small ones must be able to leave things out. Two
 * facts shape how:
 *
 * 1. **Almost everything scales down by itself.** Every function here is
 *    `static inline`, so a caller that never names `wsk_hit`, `wsk_compose`
 *    or `wsk_rows` does not link them -- measured, not assumed
 *    (`tools/evidence/profiles/`).
 *
 * 2. **Except a branch in the shared per-widget loop**, which every caller
 *    links whatever it draws. A dispatch table would let the linker drop
 *    those too, and a dispatch table is precisely what an RV-9 module may
 *    not have (§4.1) -- it is addresses in rodata and it broke two modules
 *    before whisker existed. So the one mechanism left is the preprocessor,
 *    and the cost of not having it is measured at 668 bytes, 20 % of a small
 *    panel's code.
 *
 * Hence: **the default is everything, and a host opts down.** A big host
 * configures nothing, which is §2.8's rule applied to the build -- do not
 * make every caller pay for the C5's limits. Three TIERS, by what the
 * platform has (D21) -- where there is more, whisker does more; where there
 * is less, it does less and refuses the rest by name:
 *
 *     (default)         DESKTOP, BROWSER: everything -- wrap, text items,
 *                       the toolkit, editing, overlays, checks
 *     -DWSK_PANEL       TOUCH PANEL, the P4: controls -- fields, marks,
 *                       scrolling, nesting, the toolkit, overlays, checks --
 *                       without wrap and text items (D20)
 *     -DWSK_MINIMAL     READOUT, the C5: labels, buttons, stacks, bands, and
 *                       whichever of hit/rows/compose the caller names, which
 *                       drop by linkage. No input, ever (D6, D9).
 *
 * And each part by itself, for a program between the tiers:
 *
 *     -DWSK_NO_FIELD    the text field            -DWSK_NO_MARKS   the corner marks
 *     -DWSK_NO_SCROLL   scrolling                 -DWSK_NO_NEST    stacks in stacks
 *     -DWSK_NO_WRAP     wrapping onto new lines   -DWSK_NO_TEXT    text items
 *     -DWSK_NO_OVER     overlays (menus)          -DWSK_NO_CHECK   the checkbox
 *     -DWSK_NO_MEMO     the memo: many lines, edited
 *     -DWSK_NO_UTF8     text beyond ASCII, for a host that can draw it (§5r)
 *     -DWSK_NO_IMAGE    the image, for a host that can draw one (§5r)
 *     -DWSK_NO_INPUT    the toolkit: events, focus, editing
 *     -DWSK_NO_TEXTSEL  selection in a field, and the clipboard (§5s)
 *     -DWSK_NO_UNDO     undo and redo of what was typed (§5v)
 *     -DWSK_NO_SOURCE   the code editor: a gutter, colour runs (§5v)
 *     -DWSK_NO_ENTITY   < > & in text, for a host that decodes entities (§5v)
 *     -DWSK_NO_WORDS    Ctrl's word moves, double- and triple-click (§5w)
 *     -DWSK_NO_SPLIT    dividers a person drags, and the sizes they set (§5x)
 *     -DWSK_NO_MENUKEYS Up, Down, Enter in an open menu (§5y)
 *     -DWSK_NO_WORLD    a world: placed items, a view and a zoom (§5q)
 *     -DWSK_NO_LISTKEYS a list or a world as one Tab stop, and its keys (§5z)
 *     -DWSK_NO_HOVER    what the pointer rests on, for a host that has one (§5aa)
 *     -DWSK_NO_TIP      tooltips: a rest, a hold, a key's focus (§5ac)
 *     -DWSK_NO_GRAB     a tree dragged by itself: GRAB, DRAG, DROP (§5ag)
 *
 * What each costs is MEASURED, not written here -- `src/run.sh --module`
 * builds the tiers and the switches and prints the bytes, so the numbers
 * cannot go stale in a comment, which the ones that used to be here had.
 *
 * A reduced build is not a different renderer. For the widgets it still has
 * it emits **byte-identical** documents, which `src/run.sh` checks by
 * building the same panel both ways and diffing. What it does instead of
 * drawing a widget it lacks is **refuse the tree** with `WSK_E_KIND`, named
 * at the widget -- never draw something else, because a field silently drawn
 * as a label is the silent-wrong-answer shape this project keeps finding in
 * other people's code (§2.3).
 */
#ifdef WSK_PANEL
#  ifndef WSK_NO_WRAP
#    define WSK_NO_WRAP 1
#  endif
#  ifndef WSK_NO_TEXT
#    define WSK_NO_TEXT 1
#  endif
#  ifndef WSK_NO_TEXTSEL
#    define WSK_NO_TEXTSEL 1       /* a finger in a field scrolls (§5s) */
#  endif
#  ifndef WSK_NO_UNDO
#    define WSK_NO_UNDO 1          /* a desktop's, as the clipboard is */
#  endif
#  ifndef WSK_NO_SOURCE
#    define WSK_NO_SOURCE 1        /* and so is a code editor */
#  endif
#  ifndef WSK_NO_WORDS
#    define WSK_NO_WORDS 1         /* no Ctrl on its keyboard (§5w) */
#  endif
#  ifndef WSK_NO_SPLIT
#    define WSK_NO_SPLIT 1         /* panes the program sizes (§5x) */
#  endif
#  ifndef WSK_NO_MENUKEYS
#    define WSK_NO_MENUKEYS 1      /* no arrow keys on its keyboard (§5y) */
#  endif
#  ifndef WSK_NO_WORLD
#    define WSK_NO_WORLD 1         /* until a P4 program asks for one (§5q) */
#  endif
#  ifndef WSK_NO_LISTKEYS
#    define WSK_NO_LISTKEYS 1      /* no arrow keys on its keyboard (§5z) */
#  endif
#  ifndef WSK_NO_HOVER
#    define WSK_NO_HOVER 1         /* a finger is only where it touches (§5aa) */
#  endif
#  ifndef WSK_NO_GRAB
#    define WSK_NO_GRAB 1          /* docking is a desktop's, as dividers are (§5ag) */
#  endif
#endif
#ifdef WSK_MINIMAL
#  ifndef WSK_NO_INPUT
#    define WSK_NO_INPUT 1
#  endif
#  ifndef WSK_NO_SPLIT
#    define WSK_NO_SPLIT 1
#  endif
#  ifndef WSK_NO_WORLD
#    define WSK_NO_WORLD 1
#  endif
#  ifndef WSK_NO_OVER
#    define WSK_NO_OVER 1
#  endif
#  ifndef WSK_NO_CHECK
#    define WSK_NO_CHECK 1
#  endif
#  ifndef WSK_NO_MEMO
#    define WSK_NO_MEMO 1
#  endif
#  ifndef WSK_NO_UTF8
#    define WSK_NO_UTF8 1
#  endif
#endif
/* /w0 draws no image and no text beyond ASCII on either board, so neither
   board's tier carries the code for them (§5r) */
#if (defined(WSK_MINIMAL) || defined(WSK_PANEL)) && !defined(WSK_NO_IMAGE)
#  define WSK_NO_IMAGE 1
#endif
/* nor does /w0 decode &lt; -- measured, it draws four characters (§2.4) */
#if (defined(WSK_MINIMAL) || defined(WSK_PANEL)) && !defined(WSK_NO_ENTITY)
#  define WSK_NO_ENTITY 1
#endif
#if defined(WSK_PANEL) && !defined(WSK_NO_UTF8)
#  define WSK_NO_UTF8 1
#endif
#if defined(WSK_NO_FIELD) && !defined(WSK_NO_MEMO)
#  define WSK_NO_MEMO 1            /* a memo is a field with lines (§5o) */
#endif
#if defined(WSK_NO_MEMO) && !defined(WSK_NO_SOURCE)
#  define WSK_NO_SOURCE 1          /* and source is a memo for code (§5v) */
#endif
#ifdef WSK_MINIMAL
#  ifndef WSK_NO_FIELD
#    define WSK_NO_FIELD 1
#  endif
#  ifndef WSK_NO_MARKS
#    define WSK_NO_MARKS 1
#  endif
#  ifndef WSK_NO_SCROLL
#    define WSK_NO_SCROLL 1
#  endif
#  ifndef WSK_NO_NEST
#    define WSK_NO_NEST 1
#  endif
#  ifndef WSK_NO_WRAP
#    define WSK_NO_WRAP 1
#  endif
#  ifndef WSK_NO_TEXT
#    define WSK_NO_TEXT 1
#  endif
#endif

#if defined(WSK_NO_NEST) && !defined(WSK_NO_OVER)
#  define WSK_NO_OVER 1            /* an overlay is a tree in `nest` (§5l) */
#endif
#if (defined(WSK_NO_NEST) || defined(WSK_NO_SCROLL)) && !defined(WSK_NO_WORLD)
#  define WSK_NO_WORLD 1           /* drawn through copies, as nesting is; its
                                      posters are stacks; and it pans through
                                      a scroll's drag (§5q) */
#endif
#if (defined(WSK_NO_NEST) || defined(WSK_NO_INPUT) || defined(WSK_NO_FIELD)) && \
    !defined(WSK_NO_LISTKEYS)
#  define WSK_NO_LISTKEYS 1        /* a list is a stack in a stack, and Tab's
                                      walk is the fields' (§5z) */
#endif
#if (defined(WSK_NO_OVER) || defined(WSK_NO_INPUT)) && !defined(WSK_NO_MENUKEYS)
#  define WSK_NO_MENUKEYS 1        /* the keys walk an overlay (§5y) */
#endif
#if (defined(WSK_NO_INPUT) || defined(WSK_NO_FIELD)) && !defined(WSK_NO_TEXTSEL)
#  define WSK_NO_TEXTSEL 1         /* a selection is edited by the toolkit */
#endif
#if (defined(WSK_NO_INPUT) || defined(WSK_NO_FIELD)) && !defined(WSK_NO_UNDO)
#  define WSK_NO_UNDO 1            /* and so is what undo undoes */
#endif
#if (defined(WSK_NO_INPUT) || defined(WSK_NO_FIELD)) && !defined(WSK_NO_WORDS)
#  define WSK_NO_WORDS 1           /* and words are the toolkit's moves (§5w) */
#endif
#if defined(WSK_NO_INPUT) && !defined(WSK_NO_GRAB)
#  define WSK_NO_GRAB 1            /* a grab is a press the toolkit follows (§5ag) */
#endif
#if defined(WSK_NO_INPUT) && !defined(WSK_NO_TIP)
#  define WSK_NO_TIP 1             /* a tip answers a rest the toolkit counts (§5ac) */
#endif
#if defined(WSK_NO_INPUT) && !defined(WSK_NO_HOVER)
#  define WSK_NO_HOVER 1           /* hover is an event the toolkit takes (§5aa) */
#endif

/* ------------------------------------------------------------------ */
/* The tree                                                            */
/* ------------------------------------------------------------------ */

#define WSK_ID_MAX    12
#define WSK_TEXT_MAX  32

#define WSK_BAND      8          /* RV-9 rasterises in 8-row bands */
#define WSK_FIELD_PAD 6          /* a field's text, in from its trough */
#define WSK_CARET_W   2          /* and the caret after it */
#define WSK_BTN_ROWS  64         /* 8 bands, and about a finger at 170 dpi */
#define WSK_DEPTH_MAX 8          /* stacks inside stacks; also what stops a cycle */

/* Every profile declares every kind, and the values never move: a reduced
   build refuses WSK_FIELD, it does not renumber around it. A caller's panel,
   a golden and an id mean the same thing whatever this was compiled with. */
enum { WSK_LABEL = 1, WSK_BUTTON, WSK_FIELD, WSK_STACK, WSK_TEXT, WSK_CHECK, WSK_MEMO,
       WSK_IMAGE, WSK_SOURCE,
       WSK_DIVIDER };             /* a bar between two panes, dragged (§5x) */

#ifndef WSK_NO_MEMO
/* A widget of many lines: a memo, or source -- a memo for code (§5v). */
static inline int wsk__lined(int kind)
{
	return kind == WSK_MEMO
#ifndef WSK_NO_SOURCE
	    || kind == WSK_SOURCE
#endif
	    ;
}
#endif
enum { WSK_COL = 1, WSK_ROW };

/*
 * Where the leftover space along the axis goes, when no child claimed it.
 *
 * `weight` hands the surplus to the children; this hands it to the GAPS, and
 * the two do not compete -- a stack with any weighted child has no surplus
 * left for a justify to place. START is zero because it is what whisker did
 * before this existed, so an unset tree lays out byte for byte as it always
 * did.
 */
enum {
	WSK_START = 0,               /* all of it after the last child */
	WSK_END,                     /* all of it before the first */
	WSK_CENTER,                  /* half each end */
	WSK_BETWEEN,                 /* shared between the children, none at the ends */
	WSK_AROUND                   /* shared around them, half-shares at the ends */
};

/*
 * What a stack does when its widgets want more than its area (D10, §5e).
 *
 * REFUSE is zero, so every tree written before scrolling existed keeps the
 * rule it was written under: a stack that does not fit is WSK_E_FIT. SCROLL
 * makes the area a viewport onto the stack laid out at its natural extents,
 * slid back by `scroll` -- and a scrolling stack whose widgets fit lays out
 * exactly as a refusing one, byte for byte.
 */
enum {
	WSK_REFUSE = 0,
	WSK_SCROLL,
	WSK_WRAP,                    /* onto a new line across the axis (§5g) */
	WSK_WORLD                    /* placed, not stacked: a view onto a space (§5q) */
};

/* A world's space (§5q rule 4): no edges, a size its view stays inside, or
   a size that wraps both ways. */
enum { WSK_OPEN = 0, WSK_BOUNDED, WSK_TORUS };

/*
 * Where a world's item is (§5q rule 2): the caller's, beside the tree, one
 * per item -- a game moves a sprite by writing x and y. World units, 32-bit
 * because a world outgrows a short; w and h its size at 1:1; `scale` its own
 * size on top of the view's zoom, 256 = 1:1; `fade` for step 2, 0 for now.
 */
typedef struct {
	int           x, y;
	short         w, h;
	short         scale;
	unsigned char fade;
	unsigned char move;          /* 1: a drag moves it rather than panning (step 6) */
} wsk_place_t;

/*
 * A mark on a widget, for the states a flat substrate has no other way to
 * show. No shadow, no gradient, no opacity -- §2.4 -- so a pressed button
 * changes colour, and that is one signal doing all the work. Ticks driven
 * inward from the four corners are a second one, in the same ink as the
 * label, costing four line segments in a single path.
 */
enum {
	WSK_MARK_CORNERS = 1,        /* a button, pressed */
	WSK_MARK_CARET   = 2,        /* a field, taking input */
	/* a tree's row (§5t): a triangle drawn before a label's or button's
	   text -- a shape, so every substrate draws it, where a ▸ is UTF-8 */
	WSK_MARK_SHUT    = 3,        /* ▸ a closed folder */
	WSK_MARK_OPEN    = 4,        /* ▾ an open one */
	WSK_MARK_LEAF    = 5         /* the same room, nothing drawn */
};

/*
 * "whatever the tree says", and it is zero on purpose.
 *
 * A panel is written with designated initialisers -- `{ .kind = WSK_BUTTON,
 * .id = "ok", .text = "OK" }` -- so that adding a field to this struct does
 * not break every caller, and so that -Wextra -Werror, which RV-9's module
 * build uses, does not object to the ones left out. Everything omitted is
 * zero, so zero has to mean inherit or the common case would come out black.
 *
 * The cost is that a widget cannot ask for pure black. On this hardware that
 * costs nothing at all: the panel is RGB565, so #010101 and #000000 are the
 * same pixel.
 */
#define WSK_INHERIT   0u

/* A length that is whatever its sibling setting says: lgap is gap. */
#define WSK_SAME      (-1)

/* No tree: `over` with nothing open. */
#define WSK_NONE      (-1)

/*
 * What a host can draw beyond /w0 (design.md §5r, D21's runtime half). The
 * program sets `root.caps` from its host; 0 -- RV-9, and every document
 * before this -- is exactly the subset whisker always drew.
 */
enum {
	WSK_CAP_UTF8    = 1,         /* text beyond ASCII */
	WSK_CAP_IMAGE   = 2,         /* <image> */
	WSK_CAP_CLIP    = 4,         /* <clipPath>: text slides at a scroll's edge */
	WSK_CAP_OPACITY = 8,         /* transparency: the world's fade (§5q) */
	WSK_CAP_ENTITY  = 16         /* decodes &lt; &gt; &amp;: text may hold
	                                < > &, escaped as it is written (§5v).
	                                /w0 does not -- it draws &lt; as four
	                                characters -- and code is full of them */
};

/* Where a stack's labels and buttons put their text (§5n): the middle of
   the widget, or its start, as a table's cells do. */
enum { WSK_MIDDLE = 0, WSK_ALIGN_START };

typedef struct {
	unsigned char kind;
	char          id[WSK_ID_MAX];
	char          text[WSK_TEXT_MAX];
	short         size;          /* rows in a column, columns in a row; 0 = default */
	short         weight;        /* share of what is left over; 0 = fixed */
	unsigned int  fill;          /* WSK_INHERIT to take the tree's accent */
	unsigned int  ink;           /* WSK_INHERIT to take the tree's ink */
	unsigned char mark;          /* WSK_MARK_CORNERS, or 0 */
	unsigned char depth;         /* its level in a tree, under a stack's indent
	                                (§5p); in the padding, so still 64 bytes */

	/*
	 * WSK_STACK: which tree is inside it -- an INDEX into the root's `nest`,
	 * not a pointer (design.md §5f). A pointer in a `static const` panel is
	 * an address in rodata, which an RV-9 module cannot link; an index keeps
	 * a nested panel static. It sits in the padding: the struct is still 64.
	 */
	short         sub;
} wsk_widget_t;

/*
 * Editable text (design.md §5j): a field's text, when it may change, lives
 * in a buffer the CALLER owns -- not in the widget, which may be in flash
 * and which the toolkit never writes. `b` holds `cap` bytes including the
 * NUL, so a field takes cap - 1 characters; `caret` is where the next goes.
 */
/*
 * Undo (§5v): a byte array the CALLER gives a buffer, holding the edits the
 * toolkit made -- each what was removed and what was put in, where -- up to
 * `used` to undo and from there to `end` to redo. Full, the oldest go.
 */
typedef struct {
	char *b;
	int   cap, used, end;
	unsigned char join;          /* the last step may grow: typing */
} wsk_undo_t;

/*
 * A colour run (§5v): `len` bytes from `at` in `ink` -- the PROGRAM's
 * colouring of its own text, since whisker knows no language's syntax. An
 * array of them, sorted and not overlapping, never starting or ending inside
 * a character; characters none covers are in the widget's ink.
 */
typedef struct {
	int          at, len;
	unsigned int ink;
} wsk_ink_t;

/*
 * A mark in a source's gutter (§5v step 4): one shape on one line, from 0.
 * Which mark a line shows -- a breakpoint over an error, say -- is the
 * program's to decide, as colours are; whisker is given one per line.
 */
enum { WSK_GM_DOT = 1,            /* a filled square: a breakpoint */
       WSK_GM_ARROW,              /* §5t's triangle, pointing right: a section */
       WSK_GM_BAR };              /* the line's gutter cell filled: where it stopped */
typedef struct {
	int           line;
	unsigned int  ink;
	unsigned char shape;
} wsk_gmark_t;

typedef struct {
	char  *b;
	int    cap, len, caret;      /* int, not short: Studio's own files are
	                                200-326 KB (§5v) */
	int    top;                  /* a memo's first line shown (§5o) */
	int    span;                 /* the selection: caret to caret + span,
	                                either way; 0, none (§5s). A program that
	                                writes the text sets it back to 0. */
	unsigned char wrap;          /* a memo: 1 wraps long lines at its width,
	                                0 slides sideways to the caret (§5u) */
#ifndef WSK_NO_UNDO
	wsk_undo_t   *undo;          /* what Ctrl-Z undoes, or 0 for none (§5v) */
#endif
#ifndef WSK_NO_SOURCE
	const wsk_ink_t *inks;       /* source: its colour runs, or 0 (§5v) */
	int           ninks;
	const wsk_gmark_t *marks;    /* source: its gutter's marks, by line; set,
	                                even to none, it reserves their column */
	int           nmarks;
#endif
} wsk_buf_t;

typedef struct wsk_tree {
	const wsk_widget_t *item;
	int           n;

	unsigned char axis;          /* WSK_COL or WSK_ROW */

	/*
	 * The device, in pixels, from getstat -- and the viewBox, which is why
	 * it must stay the device. User units are then device rows and wsk_rows
	 * needs no conversion; a document whose viewBox says anything else is
	 * scaled onto the panel and every row number in it becomes a lie. Do
	 * not set these to mean "the part of the screen I am using": that is
	 * what the area below is for.
	 */
	short         w, h;

	/*
	 * Where the stack sits inside it. The whole device after wsk_init, and
	 * wsk_area narrows it -- a toolbar across the top, a column down one
	 * side.
	 *
	 * This bounds where the stack LAYS OUT. It does not on its own make two
	 * trees share a screen: wsk_render paints a background across the whole
	 * device, so a second one rendered after the first erases it. Giving
	 * each its own area is necessary and was not sufficient, which four
	 * keyboard rows coming out as one row showed (design.md §5c). Two
	 * stacks over one device is wsk_compose, below, where the background
	 * belongs to the document and a stack has none.
	 */
	short         ax, ay, aw, ah;

	/*
	 * How wide a widget may get across the stack, 0 for no limit.
	 *
	 * The sizing rule says a widget's extent ALONG the axis is pixels rather
	 * than a proportion, so a bigger screen holds more widgets instead of
	 * bigger ones. Across the axis there was no rule, and a column takes the
	 * whole area -- which at 1024 wide is a 1016x64 button, a sixteen-to-one
	 * letterbox that no finger is shaped like. Capping it and centring what
	 * is left is what lets one tree be sensible on both panels without every
	 * caller doing the arithmetic from the device size it just read.
	 */
	short         cross_max;

	short         pad, gap;
	short         font;          /* device pixels; the cell height */

	unsigned int  bg;            /* the scene behind everything */
	unsigned int  ink;           /* text */
	unsigned int  accent;        /* a button */
	unsigned int  trough;        /* a field: darker than bg, so it reads as a
	                                well rather than a button. On a flat
	                                substrate colour is the only signal there
	                                is (§2.4), so a field must not inherit the
	                                accent or it is a button with words in. */

	unsigned char ids;           /* emit id= attributes: 1 yes, 0 no */
	unsigned char justify;       /* WSK_START..WSK_AROUND, along the axis */

	/*
	 * Scrolling (design.md §5e). The offset is the caller's, like every
	 * other piece of state (D5): whisker keeps no position, no momentum and
	 * no drag, and a wheel or a finger becomes a new `scroll` in the host
	 * before a picture is asked for. 0 .. wsk_scroll_max(), or the tree is
	 * refused with WSK_E_SCROLL rather than clamped -- whether an overshoot
	 * is a bug or a bounce is the caller's to know.
	 */
	unsigned char overflow;      /* WSK_REFUSE, WSK_SCROLL or WSK_WRAP */
	short         scroll;        /* pixels along the axis, from the start */
#ifndef WSK_NO_SPLIT
	/*
	 * The caller's sizes for its items (§5x), one per item, or 0: an entry
	 * above 0 is that item's extent along the axis, its size and weight set
	 * aside. Writable, as `scroll` is -- a divider's drag writes it -- so
	 * the widgets stay where they were declared, flash included.
	 */
	short        *sizes;
#endif

	/*
	 * Between the lines of a wrapping stack (§5g), or WSK_SAME for `gap`.
	 * Words want a space's width between them and lines want far less; one
	 * gap both ways made a paragraph look double-spaced.
	 */
	short         lgap;

	/*
	 * Nesting (design.md §5f). The trees a WSK_STACK widget's `sub` indexes,
	 * set on the ROOT only: a child is laid out in a copy that carries the
	 * root's `nest`, its device, and an area that is the stack widget's own
	 * rectangle -- so a child tree says only what it is, never where.
	 */
	const struct wsk_tree *nest;
	short         nests;

	/*
	 * Text items (design.md §5h): the strings a WSK_TEXT widget's `sub`
	 * indexes, set on the ROOT at runtime and carried into nested copies as
	 * `nest` is. An array of pointers built at runtime is RAM, which a
	 * module may have; a table of them in rodata it may not (§4.1).
	 */
	const char *const *str;
	short         strs;

	/*
	 * The widget a finger is holding, drawn with the corner marks -- set by
	 * the toolkit (§5i), on the ROOT, and carried into nested copies. In a
	 * tree and not a widget because a widget may be `static const`, in flash,
	 * and the toolkit writes nothing it cannot be sure is RAM.
	 */
	const void   *held;

	/*
	 * Fields that edit (§5j): the buffers a WSK_FIELD's `sub` indexes, on
	 * the root and carried into copies. A field uses one when the root has
	 * buffers and its `sub` is in range; otherwise it is §5d's read-only
	 * field of inline text. `focus` is the widget typing goes to, set by the
	 * toolkit, drawn with its caret.
	 */
	wsk_buf_t    *buf;
	short         bufs;
	const void   *focus;

	/*
	 * An overlay (§5l): the tree in `nest` open OVER the root -- a menu --
	 * or WSK_NONE; and where it sits, which wsk_over_open works out from
	 * the widget it hangs from. Drawn after the root, on a backing of its
	 * own, and asked first by hit. On the root only.
	 */
	short         over;
	short         ox, oy, ow, oh;
	unsigned char omodal;        /* a dialog, not a menu: nothing off it answers (§5ae) */
#ifndef WSK_NO_MENUKEYS
	short         ohi;           /* the overlay's button the keys have reached,
	                                or WSK_NONE (§5y) */
#endif

	/*
	 * State (§5m): bytes a WSK_CHECK's `sub` indexes -- ticked or not --
	 * on the root and carried into copies, the caller's memory, as buffers
	 * are. And selection, which is a stack's: with `select` set, `sel` is
	 * the index of its chosen item (or WSK_NONE), drawn in `selfill`.
	 */
	unsigned char *state;
	short         states;
	unsigned char select;
	short         sel;
#ifndef WSK_NO_GRAB
	unsigned char grab;          /* a press here that moves drags this tree (§5ag) */
#endif
	unsigned int  selfill;

	/*
	 * Tables (§5n). `head` names a tree in `nest` -- a header row -- and
	 * every row at this stack's first level takes its cells' places and
	 * widths from the header's cells. `talign` puts labels' and buttons'
	 * text at the start of the widget. `ghead` is whisker's, set in the
	 * copies it lays a row out in: the header that row follows.
	 */
	short         head;
	unsigned char talign;
	const struct wsk_tree *ghead;

	/*
	 * Trees (§5p): a widget at depth d is laid out d x `indent` in from the
	 * start of a column, and narrower by as much. A tree is a flat list of the
	 * rows a program shows, with their depths -- the program owns expansion.
	 * `rdepth`/`rindent` are whisker's, set in a table row's copy: the row's
	 * own depth and its body's indent, for its first cell.
	 */
	short         indent;
	unsigned char rdepth;
	short         rindent;

	/* What the host can draw beyond /w0: WSK_CAP_*, on the root (§5r). */
	unsigned char caps;

#ifndef WSK_NO_TIP
	/* A tooltip (§5ac), on the root, set by wsk_ui_tip: the program's text,
	   its memory, or 0; and the box whisker placed it in. */
	const char   *tip;
	short         tx, ty, tw, th;
#endif

#ifndef WSK_NO_WORLD
	/*
	 * A world (§5q), when `overflow` is WSK_WORLD: its items' places, the
	 * caller's and writable; the view -- the world point at the area's top
	 * left, and the zoom, 256 = 1:1, the caller's as `scroll` is; and its
	 * space, WSK_OPEN or WSK_BOUNDED within ww x wh. `wone` is whisker's,
	 * set in the one-item copies an item is drawn through.
	 */
	wsk_place_t  *place;
	short        *order;         /* the drawing order, bottom first, or 0: array
	                                order -- the caller's, writable (step 6) */
	int           vx, vy, ww, wh;
	short         zoom;
	unsigned char wspace;
	unsigned char wone;
	unsigned char wfade;         /* whisker's, in those copies: the item's fade */
	unsigned int  wbg;           /* ... and the world's background it fades toward */
	int           wdx, wdy;      /* and which copy of a torus's item: 0 or -ww, -wh */
	/* whisker's, in the copies a poster is laid out in (step 2b): the
	   factor, 256 = 1:1, its lengths were scaled by -- 0 outside a world */
	int           wz;
#endif

	/* Set by whisker in those copies, never by a caller: the intersection of
	   every scrolling viewport above this stack, which cuts it (§5f rule 3). */
	unsigned char clipped;
	short         cx0, cy0, cx1, cy1;
} wsk_tree_t;

typedef struct { short x, y, w, h; } wsk_rect_t;

/* ------------------------------------------------------------------ */
/* Errors                                                              */
/* ------------------------------------------------------------------ */

enum {
	WSK_OK          =  0,
	WSK_E_TEXT      = -1,        /* a label the window cannot draw */
	WSK_E_BUDGET    = -2,        /* it will not fit in the bytes allowed */
	WSK_E_NOTFOUND  = -3,        /* no widget by that id */
	WSK_E_EMPTY     = -4,        /* an empty row range: nothing to repaint */
	WSK_E_DEVICE    = -5,        /* a stack sized for a different screen */
	WSK_E_KIND      = -6,        /* a widget this build was compiled without */
	WSK_E_FIT       = -7,        /* the widgets want more than the stack's area */
	WSK_E_SCROLL    = -8,        /* a scroll offset past either end */
	WSK_E_DEPTH     = -9,        /* stacks nested past WSK_DEPTH_MAX, or in a cycle */
	WSK_E_HOST      = -10        /* what this host said it cannot draw (§5r) */
};

/* ------------------------------------------------------------------ */
/* The sink                                                            */
/* ------------------------------------------------------------------ */

/*
 * Where the document goes. A sink that appends to an array is the buffer
 * form; a sink that calls env->write is the streaming one. Passing a
 * function pointer is position independent -- only a stored table of them
 * is not.
 *
 * `put` may be null, in which case nothing is written and only the length
 * is counted, which is how the byte budget is checked before anything is
 * emitted rather than after.
 */
typedef struct wsk_sink {
	void (*put)(struct wsk_sink *s, const char *b, int n);
	void  *ctx;
	int    len;                  /* what it would take, written or not */
} wsk_sink_t;

static inline void wsk__w(wsk_sink_t *s, const char *b, int n)
{
	if (s->put != 0) s->put(s, b, n);
	s->len += n;
}

static inline void wsk__s(wsk_sink_t *s, const char *t)
{
	int n = 0;
	while (t[n] != '\0') n++;
	wsk__w(s, t, n);
}

static inline void wsk__n(wsk_sink_t *s, int v)
{
	char b[8];
	int i = 0;
	if (v < 0) { wsk__w(s, "-", 1); v = -v; }
	do { b[i++] = (char)('0' + (v % 10)); v /= 10; } while (v != 0 && i < 8);
	while (i > 0) { char c = b[--i]; wsk__w(s, &c, 1); }
}

/*
 * Arithmetic rather than a lookup table: a table is a variable at a fixed
 * address, which linked into a module before 2026-09-30 and may still be
 * linked into an older one. RV-9's `dump` learned this and says so.
 */
static inline char wsk__hexdig(unsigned int v)
{
	v &= 0xF;
	return (char)((v < 10) ? ('0' + v) : ('a' + (v - 10)));
}

/*
 * A colour, in the shortest spelling the window accepts. #1a2b3c is six
 * digits; #112233 is three. Three bytes a colour, on every element, on every
 * band of every repaint.
 */
static inline void wsk__col(wsk_sink_t *s, unsigned int rgb)
{
	unsigned int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
	char out[7];
	int n;

	if ((r >> 4) == (r & 0xF) && (g >> 4) == (g & 0xF) && (b >> 4) == (b & 0xF)) {
		out[0] = wsk__hexdig(r);
		out[1] = wsk__hexdig(g);
		out[2] = wsk__hexdig(b);
		n = 3;
	} else {
		out[0] = wsk__hexdig(r >> 4); out[1] = wsk__hexdig(r);
		out[2] = wsk__hexdig(g >> 4); out[3] = wsk__hexdig(g);
		out[4] = wsk__hexdig(b >> 4); out[5] = wsk__hexdig(b);
		n = 6;
	}
	wsk__w(s, "#", 1);
	wsk__w(s, out, n);
}

/* ------------------------------------------------------------------ */
/* Text, which the window has already decided for us                   */
/* ------------------------------------------------------------------ */

/*
 * The font is fixed-width and there is exactly one of it, so every number a
 * layout needs is arithmetic (design.md §2.1). Widths are in device pixels.
 */
#define WSK_GLYPH_W   10
#define WSK_GLYPH_H   20
#define WSK_BASELINE  15

static inline int wsk_advance(int font) { return font * WSK_GLYPH_W / WSK_GLYPH_H; }
static inline int wsk_text_w(int font, int chars) { return chars * wsk_advance(font); }
static inline int wsk_baseline_of(int top, int font)
{
	return top + font * WSK_BASELINE / WSK_GLYPH_H;
}

/*
 * What a label may carry.
 *
 * The window draws entities literally -- `&lt;` is four characters on the
 * glass -- so text cannot be escaped, only kept clean; and a raw '<' hung
 * the driver outright until 2026-09-30. Bytes outside 32..126 advance a cell
 * and draw nothing. So the boundary is here (design.md §2.4).
 */
/*
 * Characters in the first n bytes of UTF-8: every byte that does not
 * continue a character (§5r). For ASCII it is n, so a host that declares
 * nothing measures exactly as before.
 */
static inline int wsk__chars(const char *s, int n)
{
#ifndef WSK_NO_UTF8
	int c = 0;
	for (int i = 0; i < n; i++) if (((unsigned char)s[i] & 0xC0) != 0x80) c++;
	return c;
#else
	(void)s;
	return n;
#endif
}

/* The byte where character k starts, or n: truncation never splits one. */
static inline int wsk__byte_of(const char *s, int n, int k)
{
#ifndef WSK_NO_UTF8
	int c = 0;
	for (int i = 0; i < n; i++)
		if (((unsigned char)s[i] & 0xC0) != 0x80) {
			if (c == k) return i;
			c++;
		}
	return n;
#else
	(void)s;
	return (k < n) ? k : n;
#endif
}

static inline int wsk_text_ok(const char *t);

#ifndef WSK_NO_IMAGE
/* An image's URL: the string its `sub` names in the root's `str` (§5r). */
static inline const char *wsk__href(const wsk_tree_t *t, const wsk_widget_t *w)
{
	if (t->str == 0 || w->sub < 0 || w->sub >= t->strs) return 0;
	return t->str[w->sub];
}
#endif

/*
 * Text a host can draw: /w0's subset, or, with WSK_CAP_UTF8, well-formed
 * UTF-8 beyond it (§5r). '<', '&' and '>' are refused either way -- the
 * window draws them literally or not at all (§2.4) -- and so is any byte that
 * starts no character, ends one too soon, or spells one the long way round.
 */
/* One of the three characters text cannot carry raw: drawable only where
   the host decodes entities (§5v). */
static inline int wsk__markup(int c, int caps)
{
#ifdef WSK_NO_ENTITY
	(void)caps;
	return c == '<' || c == '&' || c == '>';
#else
	return (c == '<' || c == '&' || c == '>') && !(caps & WSK_CAP_ENTITY);
#endif
}

static inline int wsk__text_ok(const char *t, int caps)
{
#ifndef WSK_NO_UTF8
	if (caps & WSK_CAP_UTF8) {
		for (int i = 0; t[i] != '\0'; ) {
			unsigned char c = (unsigned char)t[i];
			int more = 0;
			if (c < 0x80) {
				if (c < 32 || c == 127 || wsk__markup(c, caps)) return 0;
				i++;
				continue;
			}
			if (c >= 0xC2 && c <= 0xDF) more = 1;
			else if (c >= 0xE0 && c <= 0xEF) more = 2;
			else if (c >= 0xF0 && c <= 0xF4) more = 3;
			else return 0;
			for (int k = 1; k <= more; k++)
				if (((unsigned char)t[i + k] & 0xC0) != 0x80) return 0;
			i += more + 1;
		}
		return 1;
	}
#endif
	for (int i = 0; t[i] != '\0'; i++) {
		char c = t[i];
		if (c < 32 || c > 126 || wsk__markup(c, caps)) return 0;
	}
	return 1;
}

#ifndef WSK_NO_FIELD
/*
 * n bytes an editor may hold (§5v step 3): what wsk__text_ok accepts,
 * counted rather than terminated -- a buffer, a paste, a key's text -- and
 * \n too where `nl` says lines are allowed.
 */
static inline int wsk__ok_n(const char *t, int n, int caps, int nl)
{
	for (int i = 0; i < n; ) {
		unsigned char c = (unsigned char)t[i];
		int more = 0;
		if (c == '\n' && nl) { i++; continue; }
		if (c < 0x80) {
			if (c < 32 || c == 127 || wsk__markup(c, caps)) return 0;
			i++;
			continue;
		}
#ifndef WSK_NO_UTF8
		if (!(caps & WSK_CAP_UTF8)) return 0;
		if (c >= 0xC2 && c <= 0xDF) more = 1;
		else if (c >= 0xE0 && c <= 0xEF) more = 2;
		else if (c >= 0xF0 && c <= 0xF4) more = 3;
		else return 0;
		if (i + more >= n) return 0;     /* cut off before its end */
		for (int k = 1; k <= more; k++)
			if (((unsigned char)t[i + k] & 0xC0) != 0x80) return 0;
		{
			/* nor the long way round, a surrogate, or past U+10FFFF */
			unsigned char d = (unsigned char)t[i + 1];
			if ((c == 0xE0 && d < 0xA0) || (c == 0xED && d > 0x9F) ||
			    (c == 0xF0 && d < 0x90) || (c == 0xF4 && d > 0x8F)) return 0;
		}
		i += more + 1;
#else
		(void)more;
		return 0;
#endif
	}
	return 1;
}

/* Whether byte `at` of a buffer begins a character -- or is its end. */
static inline int wsk__starts(const char *t, int len, int at)
{
	return at <= 0 || at >= len || ((unsigned char)t[at] & 0xC0) != 0x80;
}

/* The character before `at`, and after it: where the caret steps to. */
static inline int wsk__prev(const char *t, int at)
{
	if (at <= 0) return 0;
	at--;
#ifndef WSK_NO_UTF8
	while (at > 0 && ((unsigned char)t[at] & 0xC0) == 0x80) at--;
#else
	(void)t;
#endif
	return at;
}

static inline int wsk__next(const char *t, int len, int at)
{
	if (at >= len) return len;
	at++;
#ifndef WSK_NO_UTF8
	while (at < len && ((unsigned char)t[at] & 0xC0) == 0x80) at++;
#else
	(void)t;
#endif
	return at;
}
#endif

static inline int wsk_text_ok(const char *t)
{
	for (int i = 0; t[i] != '\0'; i++) {
		char c = t[i];
		if (c < 32 || c > 126) return 0;
		if (c == '<' || c == '&' || c == '>') return 0;
	}
	return 1;
}

/*
 * WSK_OK, or WSK_E_TEXT -- or WSK_E_KIND from a reduced build (see
 * Profiles, above), or WSK_E_FIT -- with `which` naming the widget.
 *
 * The index comes back through a parameter rather than as the return value,
 * because the first widget in a tree has index 0 and so does WSK_OK -- a
 * refusal that cannot be told from an acceptance is worse than no check.
 * `which` may be null when only the verdict is wanted.
 */
static inline int wsk__natural(const wsk_tree_t *t, int i);
static inline int wsk__round_band(int v);

#ifndef WSK_NO_TEXT
/* The string a WSK_TEXT item names, from `r`'s `str` (§5h), or 0. `r` is
   the root or a laid-out copy; `t` may be a caller's own child tree, which
   carries no `str` of its own. */
static inline const char *wsk__str_of(const wsk_tree_t *r, const wsk_tree_t *t,
                                      int k)
{
	const wsk_widget_t *w = &t->item[k];
	if (w->kind != WSK_TEXT || r->str == 0 || w->sub < 0 || w->sub >= r->strs)
		return 0;
	return r->str[w->sub];
}
#endif

/* What a widget says: a text item's string, or its own inline text. */
static inline const char *wsk__say(const wsk_tree_t *r, const wsk_tree_t *t, int k)
{
#ifndef WSK_NO_TEXT
	const char *x = wsk__str_of(r, t, k);
	if (x != 0) return x;
#else
	(void)r;
#endif
	return t->item[k].text;
}

#ifndef WSK_NO_WRAP
/*
 * A unit of a wrapping stack's flow: an item, or one word of a text item
 * (§5h rule 2) -- `wi` the word's number, -1 for a whole item, and where the
 * word lies in its string. A paragraph's words are placed by exactly the
 * rule an item is, which is D18's "text is not special" in code.
 */
typedef struct { short k, wi, off, len; } wsk__u_t;

static inline int wsk__u_at(const wsk_tree_t *r, const wsk_tree_t *t, int k,
                            wsk__u_t *u)
{
	for (; k < t->n; k++) {
#ifndef WSK_NO_TEXT
		if (t->item[k].kind == WSK_TEXT) {
			const char *x = wsk__str_of(r, t, k);
			int o = 0, l = 0;
			if (x == 0) continue;
			while (x[o] == ' ') o++;
			if (x[o] == '\0') continue;        /* no words: no units */
			while (x[o + l] != '\0' && x[o + l] != ' ') l++;
			u->k = (short)k; u->wi = 0; u->off = (short)o; u->len = (short)l;
			return 1;
		}
#else
		(void)r;
#endif
		u->k = (short)k; u->wi = -1; u->off = 0; u->len = 0;
		return 1;
	}
	return 0;
}

static inline int wsk__u_next(const wsk_tree_t *r, const wsk_tree_t *t, wsk__u_t *u)
{
#ifndef WSK_NO_TEXT
	if (u->wi >= 0) {
		const char *x = wsk__str_of(r, t, u->k);
		int o = u->off + u->len, l = 0;
		while (x[o] == ' ') o++;
		if (x[o] != '\0') {
			while (x[o + l] != '\0' && x[o + l] != ' ') l++;
			u->wi++; u->off = (short)o; u->len = (short)l;
			return 1;
		}
	}
#endif
	return wsk__u_at(r, t, u->k + 1, u);
}

static inline int wsk__meas(const wsk_tree_t *r, const wsk_tree_t *t, int i,
                            int axis, int depth, int avail);
static inline int wsk__flow(const wsk_tree_t *r, const wsk_tree_t *t, int span,
                            int idx, int wi, int *ia, int *ic, int *ea, int *ec,
                            int depth);

/* A unit's extent along its stack: a word is its text, no padding. */
static inline int wsk__u_along(const wsk_tree_t *r, const wsk_tree_t *t,
                               const wsk__u_t *u, int depth)
{
	if (u->wi >= 0) {
#ifndef WSK_NO_TEXT
		const char *x = wsk__str_of(r, t, u->k);
		if (x != 0) return wsk_text_w(t->font, wsk__chars(x + u->off, u->len));
#endif
		return wsk_text_w(t->font, u->len);
	}
	return wsk__meas(r, t, u->k, t->axis, depth, -1);
}

/* And across: a word is as deep as a label's line. */
static inline int wsk__u_deep(const wsk_tree_t *r, const wsk_tree_t *t,
                              const wsk__u_t *u, int along, int depth)
{
#ifndef WSK_NO_WORLD
	if (u->wi >= 0 && r->wz != 0) return t->font + t->gap;
#endif
	if (u->wi >= 0) return wsk__round_band(t->font + t->gap);
	return wsk__meas(r, t, u->k, (t->axis == WSK_COL) ? WSK_ROW : WSK_COL,
	                 depth, along);
}
#endif

/* What the widgets and the gaps between them want along the axis, padding
   not included. */
static inline int wsk__used(const wsk_tree_t *t)
{
	int used = 0;
	for (int i = 0; i < t->n; i++) {
		if (i > 0) used += t->gap;
		used += wsk__natural(t, i);
	}
	return used;
}

/*
 * How far a stack may scroll: the content -- padding, widgets, gaps,
 * padding -- less the area it is seen through, or 0 when it fits. The one
 * line a caller needs to clamp a drag is `if (s > max) s = max;`.
 */
static inline int wsk_scroll_max(const wsk_tree_t *t)
{
	int over = wsk__used(t) + 2 * t->pad - (t->axis == WSK_COL ? t->ah : t->aw);
	return (over > 0) ? over : 0;
}

static inline int wsk__check1(const wsk_tree_t *t, int *which)
{
	for (int i = 0; i < t->n; i++) {
		if (!wsk__text_ok(t->item[i].text, t->caps)) {
			if (which != 0) *which = i;
			return WSK_E_TEXT;
		}
#ifdef WSK_NO_CHECK
		if (t->item[i].kind == WSK_CHECK) {
			if (which != 0) *which = i;
			return WSK_E_KIND;
		}
#endif
#ifdef WSK_NO_TEXT
		if (t->item[i].kind == WSK_TEXT) {
			if (which != 0) *which = i;
			return WSK_E_KIND;
		}
#else
		/* A text item names a string that exists, and the window can draw
		   (§5h rule 4); and its words stand along a row, not one per line
		   down a wrapping column. */
		if (t->item[i].kind == WSK_TEXT) {
			const char *x = wsk__str_of(t, t, i);
			if (x == 0 || (t->overflow == WSK_WRAP && t->axis == WSK_COL)) {
				if (which != 0) *which = i;
				return WSK_E_KIND;
			}
			if (!wsk__text_ok(x, t->caps)) {
				if (which != 0) *which = i;
				return WSK_E_TEXT;
			}
		}
#endif
#ifdef WSK_NO_NEST
		if (t->item[i].kind == WSK_STACK) {
			if (which != 0) *which = i;
			return WSK_E_KIND;
		}
#else
		/* A path is ids joined by '/', so an id may not hold one (D19). */
		for (int k = 0; t->item[i].id[k] != '\0'; k++)
			if (t->item[i].id[k] == '/') {
				if (which != 0) *which = i;
				return WSK_E_TEXT;
			}
		if (t->item[i].kind == WSK_STACK &&
		    (t->nest == 0 || t->item[i].sub < 0 || t->item[i].sub >= t->nests)) {
			if (which != 0) *which = i;
			return WSK_E_KIND;
		}
#endif
		/* And, in a reduced build, what this one cannot draw. Refusing is
		   the point: drawing a field as a label would be a picture that is
		   wrong without saying so, which is the failure shape §2.3 keeps
		   finding in substrates and is not one to add here. */
#ifdef WSK_NO_FIELD
		if (t->item[i].kind == WSK_FIELD) {
			if (which != 0) *which = i;
			return WSK_E_KIND;
		}
#else
		/* A field's buffer says what it holds, and holds what the window
		   can draw (§5j): the toolkit keeps both true, and a caller who
		   filled one by hand is checked the same way. */
		if ((t->item[i].kind == WSK_FIELD
#ifndef WSK_NO_MEMO
		     || wsk__lined(t->item[i].kind)
#endif
		    ) && t->buf != 0 &&
		    t->item[i].sub >= 0 && t->item[i].sub < t->bufs) {
			const wsk_buf_t *b = &t->buf[t->item[i].sub];
			int good = 1;
			if (b->b == 0 || b->cap < 1 || b->len < 0 || b->len >= b->cap ||
			    b->caret < 0 || b->caret > b->len || b->b[b->len] != '\0') {
				if (which != 0) *which = i;
				return WSK_E_KIND;
			}
			/* a memo's lines are separated by \n, and that is the one
			   character it may hold that a label may not (§5o rule 4);
			   beyond ASCII for a host that draws it, the caret between
			   characters, never inside one (§5v step 3) */
			good = wsk__ok_n(b->b, b->len, t->caps, t->item[i].kind != WSK_FIELD) &&
			       wsk__starts(b->b, b->len, b->caret) &&
			       wsk__starts(b->b, b->len, b->caret + b->span);
#ifndef WSK_NO_SOURCE
			/* colour runs in order, none over another, none before 0 --
			   past the end is allowed: the text may just have shrunk */
			if (t->item[i].kind == WSK_SOURCE && b->ninks > 0) {
				if (b->inks == 0) good = 0;
				for (int k = 0; good && k < b->ninks; k++)
					if (b->inks[k].at < 0 || b->inks[k].len < 0 ||
					    (k > 0 && b->inks[k].at < b->inks[k - 1].at + b->inks[k - 1].len) ||
					    !wsk__starts(b->b, b->len, b->inks[k].at) ||
					    !wsk__starts(b->b, b->len, b->inks[k].at + b->inks[k].len))
						good = 0;      /* nor through a character */
			}
			/* marks: by line, one a line, a shape whisker draws (step 4) */
			if (t->item[i].kind == WSK_SOURCE && b->nmarks > 0) {
				if (b->marks == 0) good = 0;
				for (int k = 0; good && k < b->nmarks; k++)
					if (b->marks[k].line < 0 || (k > 0 && b->marks[k].line <= b->marks[k - 1].line) ||
					    b->marks[k].shape < WSK_GM_DOT || b->marks[k].shape > WSK_GM_BAR
#ifdef WSK_NO_MARKS
					    || b->marks[k].shape == WSK_GM_ARROW    /* no triangle to draw it */
#endif
					   )
						good = 0;
			}
#endif
			if (!good) {
				if (which != 0) *which = i;
				return WSK_E_TEXT;
			}
		}
#endif
#ifdef WSK_NO_MEMO
		if (t->item[i].kind == WSK_MEMO) {
			if (which != 0) *which = i;
			return WSK_E_KIND;
		}
#endif
#ifdef WSK_NO_SOURCE
		if (t->item[i].kind == WSK_SOURCE) {
			if (which != 0) *which = i;
			return WSK_E_KIND;
		}
#endif
#ifdef WSK_NO_IMAGE
		if (t->item[i].kind == WSK_IMAGE) {
			if (which != 0) *which = i;
			return WSK_E_KIND;
		}
#else
		/* An image: only for a host that says it can draw one (§5r), and an
		   href that cannot end its attribute or start an element. */
		if (t->item[i].kind == WSK_IMAGE) {
			const char *h = wsk__href(t, &t->item[i]);
			int good = (h != 0 && h[0] != '\0');
			if (!(t->caps & WSK_CAP_IMAGE)) {
				if (which != 0) *which = i;
				return WSK_E_HOST;
			}
			for (int k = 0; good && h[k] != '\0'; k++) {
				char c = h[k];
				if (c <= 32 || c > 126 || c == '"' || c == '<' || c == '>' || c == '&') good = 0;
			}
			if (!good) {
				if (which != 0) *which = i;
				return WSK_E_TEXT;
			}
		}
#endif
#ifdef WSK_NO_SPLIT
		if (t->item[i].kind == WSK_DIVIDER) {
			if (which != 0) *which = i;
			return WSK_E_KIND;
		}
#else
		/* a divider has a pane on each side, in a stack laid out as a list
		   of extents -- not a wrapping one, nor a table's row (§5x rule 6) */
		if (t->item[i].kind == WSK_DIVIDER &&
		    (i == 0 || i == t->n - 1 || t->sizes == 0 || t->overflow == WSK_WRAP
#ifndef WSK_NO_NEST
		     || t->ghead != 0
#endif
		    )) {
			if (which != 0) *which = i;
			return WSK_E_KIND;
		}
#endif
#ifdef WSK_NO_MARKS
		if (t->item[i].mark == WSK_MARK_CORNERS || t->item[i].mark >= WSK_MARK_SHUT) {
			if (which != 0) *which = i;
			return WSK_E_KIND;
		}
#else
		/* a tree's mark goes before text: a label's or a button's (§5t) */
		if (t->item[i].mark >= WSK_MARK_SHUT && t->item[i].kind != WSK_LABEL &&
		    t->item[i].kind != WSK_BUTTON) {
			if (which != 0) *which = i;
			return WSK_E_KIND;
		}
#endif
	}

#ifndef WSK_NO_NEST
	/* A row in a table (§5n): its widths are its header's, which was
	   checked as itself; what it may not have is a cell with no column. */
	if (t->ghead != 0) {
		if (t->n > t->ghead->n) {
			if (which != 0) *which = t->ghead->n;
			return WSK_E_FIT;
		}
		return WSK_OK;
	}
#endif
#ifdef WSK_NO_WRAP
	if (t->overflow == WSK_WRAP) {
		if (which != 0) *which = -1;
		return WSK_E_KIND;
	}
#else
	/* Wrapping (§5g rule 5): an item wider than a whole line is refused,
	   since breaking it would be drawing something else; and lines deeper
	   than the area are refused at the first item that does not fit. A
	   text window is a wrapping row inside a scrolling column, never one
	   stack doing both. */
	if (t->overflow == WSK_WRAP) {
		int col = (t->axis == WSK_COL);
		int span = (col ? t->ah : t->aw) - 2 * t->pad;
		int deep = (col ? t->aw : t->ah) - 2 * t->pad;
#ifndef WSK_NO_SPLIT
		/* a wrapping stack's lines are not a list of extents to size */
		if (t->sizes != 0) {
			if (which != 0) *which = -1;
			return WSK_E_KIND;
		}
#endif
		/* Widths first: a unit no line can hold is the fault to name, even
		   where the lines it forces would run out of depth sooner. A unit
		   is an item or a word, and either way `which` is the item. */
		wsk__u_t u;
		int more;
		for (more = wsk__u_at(t, t, 0, &u); more; more = wsk__u_next(t, t, &u))
			if (wsk__u_along(t, t, &u, 0) > span) {
				if (which != 0) *which = u.k;
				return WSK_E_FIT;
			}
		for (more = wsk__u_at(t, t, 0, &u); more; more = wsk__u_next(t, t, &u)) {
			int ia = 0, ic = 0, ea = 0, ec = 0;
			(void)wsk__flow(t, t, span, u.k, u.wi, &ia, &ic, &ea, &ec, 0);
			if (ic + ec > deep) {
				if (which != 0) *which = u.k;
				return WSK_E_FIT;
			}
		}
		return WSK_OK;
	}
#endif

#ifdef WSK_NO_WORLD
	if (t->overflow == WSK_WORLD) {
		if (which != 0) *which = -1;
		return WSK_E_KIND;
	}
#else
	/* A world (§5q): places for its items, a zoom and scales above 0, a
	   space whisker knows -- and, bounded, a view inside it, refused
	   rather than clamped as a scroll past its end is. Nothing has to fit:
	   overlapping and running off the view is what a world is for. */
	if (t->overflow == WSK_WORLD) {
		/* a world in a poster would zoom by its own view alone (step 2b) */
		if (t->place == 0 || t->zoom <= 0 || t->wspace > WSK_TORUS || t->wz != 0 ||
		    (t->wspace != WSK_OPEN && (t->ww <= 0 || t->wh <= 0))) {
			if (which != 0) *which = -1;
			return WSK_E_KIND;
		}
		for (int i = 0; i < t->n; i++)
			/* an editable field waits for the toolkit to place its caret
			   at the item's scale, which it does not yet; a stack is a
			   poster (step 2b), checked below at 1:1 */
			if (t->place[i].scale <= 0 ||
			    t->item[i].kind == WSK_FIELD || t->item[i].kind == WSK_MEMO ||
			    t->item[i].kind == WSK_SOURCE ||
			    t->place[i].w < 0 || t->place[i].h < 0) {
				if (which != 0) *which = i;
				return WSK_E_KIND;
			}
		/* an order holds each item exactly once (step 6) */
		for (int j = 0; t->order != 0 && j < t->n; j++) {
			int seen = 0;
			for (int q = 0; q < t->n; q++) if (t->order[q] == j) seen++;
			if (seen != 1) {
				if (which != 0) *which = j;
				return WSK_E_KIND;
			}
		}
		if (t->wspace == WSK_BOUNDED &&
		    (t->vx < 0 || t->vy < 0 || t->vx + t->aw * 256 / t->zoom > t->ww ||
		     t->vy + t->ah * 256 / t->zoom > t->wh)) {
			if (which != 0) *which = -1;
			return WSK_E_SCROLL;
		}
		/* a torus's view may be anywhere, but no bigger than the world:
		   seeing an item twice side by side is two worlds (rule 4) */
		if (t->wspace == WSK_TORUS &&
		    (t->aw * 256 / t->zoom > t->ww || t->ah * 256 / t->zoom > t->wh)) {
			if (which != 0) *which = -1;
			return WSK_E_FIT;
		}
		return WSK_OK;
	}
#endif
#ifdef WSK_NO_SCROLL
	/* Refused by name, and there is no widget to name, so `which` is -1. */
	if (t->overflow == WSK_SCROLL) {
		if (which != 0) *which = -1;
		return WSK_E_KIND;
	}
#else
	/* A scrolling stack is allowed to want more than its area -- that is
	   what it is for -- but not more than a coordinate can say, and not to
	   be scrolled past either end.

	   The first is not hypothetical. A rectangle is four shorts, so a
	   widget laid out past row 32767 wraps to a negative row, and a list
	   of 20,000 rows drew 17 KB of rows from the wrong places without a
	   word (§5e). A list that long is not whisker's to hold anyway: every
	   rectangle is laid out from the top, so a document costs the square
	   of the list, and the caller -- who holds the list (D5) -- hands
	   whisker the slice in view. `which` names the widget that would
	   wrap. */
#ifndef WSK_NO_WORLD
	/* A stack that scrolls, inside a poster (§5q step 2b): its offset is the
	   caller's pixels, which do not scale with the poster, so it would show
	   other rows at every zoom -- refused by name, as a field is (found by
	   review, 2026-11-16). */
	if (t->overflow == WSK_SCROLL && t->wz != 0) {
		if (which != 0) *which = -1;
		return WSK_E_KIND;
	}
#endif
	if (t->overflow == WSK_SCROLL) {
		int end = ((t->axis == WSK_COL) ? t->ay : t->ax) + 2 * t->pad;
		for (int i = 0; i < t->n; i++) {
			if (i > 0) end += t->gap;
			end += wsk__natural(t, i);
			if (end > 32767) {
				if (which != 0) *which = i;
				return WSK_E_FIT;
			}
		}
		if (t->scroll < 0 || t->scroll > wsk_scroll_max(t)) {
			if (which != 0) *which = -1;
			return WSK_E_SCROLL;
		}
		return WSK_OK;
	}
#endif

	/* And whether the stack fits its area. Layout takes each widget's
	   natural extent first and shares only the surplus, so when there is
	   none it used to carry on and draw past the end of the area -- with
	   no error, and with hit agreeing with the drawing. A button left at
	   size 0 is 64 rows, so one in a shorter area overflowed silently: on
	   2026-10-24 that was wslop's marks and edge bars, and a day spent
	   asking RV-9 why its panel drew low (roadmap 4.3). `which` names the
	   first widget that crosses the end. */
#ifndef WSK_NO_WORLD
	/* Inside a poster (§5q step 2b): editing waits, as in a world; and
	   fit is a poster's design, checked at 1:1 -- at any other factor
	   rounding may cross the end by a pixel, and its rectangle cuts it. */
	if (t->wz != 0) {
		for (int i = 0; i < t->n; i++)
			if (t->item[i].kind == WSK_FIELD
#ifndef WSK_NO_MEMO
			    || wsk__lined(t->item[i].kind)
#endif
			    ) {
				if (which != 0) *which = i;
				return WSK_E_KIND;
			}
		if (t->wz != 256) return WSK_OK;
	}
#endif
	{
		int span = (t->axis == WSK_COL ? t->ah : t->aw) - 2 * t->pad;
		int used = 0;
		for (int i = 0; i < t->n; i++) {
			if (i > 0) used += t->gap;
			used += wsk__natural(t, i);
			if (used > span) {
				if (which != 0) *which = i;
				return WSK_E_FIT;
			}
		}
	}
	return WSK_OK;
}

static inline int wsk__len(const char *s)
{
	int n = 0;
	while (s[n] != '\0') n++;
	return n;
}

/*
 * Copy a panel out of rodata so it can be changed.
 *
 * A caller keeps its panel `static const` -- that is the point of the text
 * being inline -- and then wants a mutable copy to press a button in. The
 * obvious `live[i] = PANEL[i]` compiles to a call to memcpy, which a module
 * has no way to link: the build fails, which is the good case. So the copy
 * is spelled out, and the strings are copied by a loop that stops at a NUL,
 * which cannot be turned back into a memcpy because its length is not known
 * until it runs.
 */
static inline void wsk__scpy(char *d, const char *s, int cap)
{
	int i = 0;
	while (s[i] != '\0' && i < cap - 1) { d[i] = s[i]; i++; }
	d[i] = '\0';
}

static inline void wsk_clone(wsk_widget_t *dst, const wsk_widget_t *src, int n)
{
	for (int i = 0; i < n; i++) {
		dst[i].kind   = src[i].kind;
		dst[i].size   = src[i].size;
		dst[i].weight = src[i].weight;
		dst[i].fill   = src[i].fill;
		dst[i].ink    = src[i].ink;
		dst[i].mark   = src[i].mark;
		dst[i].depth  = src[i].depth;
		dst[i].sub    = src[i].sub;
		wsk__scpy(dst[i].id, src[i].id, WSK_ID_MAX);
		wsk__scpy(dst[i].text, src[i].text, WSK_TEXT_MAX);
	}
}

static inline int wsk__same(const char *a, const char *b)
{
	while (*a != '\0' && *a == *b) { a++; b++; }
	return *a == *b;
}

/* ------------------------------------------------------------------ */
/* Layout: one axis, and nothing else                                  */
/* ------------------------------------------------------------------ */

static inline void wsk_init(wsk_tree_t *t, int axis, int dev_w, int dev_h)
{
	t->item = 0;
	t->n = 0;
	t->axis = (unsigned char)axis;
	t->w = (short)dev_w;
	t->h = (short)dev_h;
	t->ax = 0;
	t->ay = 0;
	t->aw = (short)dev_w;
	t->ah = (short)dev_h;
	t->cross_max = 0;
	t->pad = 8;
	t->gap = 8;
	t->font = 16;
	t->bg = 0x102030u;
	t->ink = 0xffffffu;
	t->accent = 0x2a6b8fu;
	t->trough = 0x081620u;
	t->ids = 0;
	t->justify = WSK_START;
	t->overflow = WSK_REFUSE;
	t->scroll = 0;
	t->lgap = WSK_SAME;
	t->nest = 0;
	t->nests = 0;
	t->str = 0;
	t->strs = 0;
	t->held = 0;
	t->buf = 0;
	t->bufs = 0;
	t->focus = 0;
	t->over = WSK_NONE;
#ifndef WSK_NO_MENUKEYS
	t->ohi = WSK_NONE;
#endif
	t->ox = t->oy = t->ow = t->oh = 0;
	t->omodal = 0;
	t->state = 0;
	t->states = 0;
	t->select = 0;
	t->sel = WSK_NONE;
#ifndef WSK_NO_GRAB
	t->grab = 0;
#endif
	t->selfill = 0xb06a2au;
	t->head = WSK_NONE;
	t->talign = WSK_MIDDLE;
	t->ghead = 0;
	t->indent = 0;
	t->rdepth = 0;
	t->rindent = 0;
	t->caps = 0;
#ifndef WSK_NO_TIP
	t->tip = 0;
	t->tx = t->ty = t->tw = t->th = 0;
#endif
#ifndef WSK_NO_SPLIT
	t->sizes = 0;
#endif
#ifndef WSK_NO_WORLD
	t->place = 0;
	t->order = 0;
	t->vx = t->vy = t->ww = t->wh = 0;
	t->zoom = 256;
	t->wspace = WSK_OPEN;
	t->wone = 0;
	t->wfade = 0;
	t->wbg = 0;
	t->wdx = t->wdy = 0;
	t->wz = 0;
#endif
	t->clipped = 0;
	t->cx0 = t->cy0 = t->cx1 = t->cy1 = 0;
}

/* The part of the device this stack lays out in. */
static inline void wsk_area(wsk_tree_t *t, int x, int y, int w, int h)
{
	t->ax = (short)x;
	t->ay = (short)y;
	t->aw = (short)w;
	t->ah = (short)h;
}

static inline int wsk__round_band(int v)
{
	return ((v + WSK_BAND - 1) / WSK_BAND) * WSK_BAND;
}

#ifndef WSK_NO_WORLD
/*
 * A length at a poster's factor (§5q step 2b): as it is outside a world,
 * where `wz` is 0. 64-bit, shifted and never divided.
 */
static inline int wsk__zv(const wsk_tree_t *r, int v)
{
	return (r->wz == 0) ? v : (int)(((long long)v * r->wz) >> 8);
}

/* A copy's own lengths at factor f, so drawing scales without knowing. */
static inline void wsk__zscale(wsk_tree_t *c, int f)
{
	c->wz = f;
	c->font = (short)(((long long)c->font * f) >> 8);
	if (c->font < 1) c->font = 1;
	c->pad = (short)(((long long)c->pad * f) >> 8);
	c->gap = (short)(((long long)c->gap * f) >> 8);
	if (c->lgap != WSK_SAME) c->lgap = (short)(((long long)c->lgap * f) >> 8);
	c->cross_max = (short)(((long long)c->cross_max * f) >> 8);
	c->indent = (short)(((long long)c->indent * f) >> 8);
}
#else
#define wsk__zv(r, v) (v)
#endif

/*
 * A widget's natural extent along the stack's axis, before weights.
 *
 * Rounded up to a whole band where the caller did not say otherwise: the
 * clip snaps outward anyway, so a widget that straddles a boundary costs an
 * extra band for nothing (design.md §5, criterion 4).
 */
#ifdef WSK_NO_NEST
static inline int wsk__natural(const wsk_tree_t *t, int i)
{
	const wsk_widget_t *w = &t->item[i];
#ifndef WSK_NO_SPLIT
	if (t->sizes != 0 && t->sizes[i] > 0) return t->sizes[i];    /* the caller's (§5x) */
#endif
	if (w->size > 0) return w->size;
	if (t->axis == WSK_ROW) {
		const char *x = wsk__say(t, t, i);
		int text = wsk_text_w(t->font, wsk__chars(x, wsk__len(x)));
#ifndef WSK_NO_CHECK
		if (w->kind == WSK_CHECK) text += t->font + t->font / 2;
#endif
#ifndef WSK_NO_MARKS
		if (w->mark >= WSK_MARK_SHUT) text += t->font;     /* a tree's mark (§5t) */
#endif
		return text + 2 * t->pad;
	}
	if (w->kind == WSK_BUTTON) return WSK_BTN_ROWS;
	return wsk__round_band(t->font + t->gap);
}
#else
/*
 * The same, measured along `axis`, which need not be the stack's own: a row
 * nested in a column is as tall as its tallest child, so a child's extent
 * ACROSS its stack has to be askable too (design.md §5f rule 1). A leaf's
 * extent across is what it would be in a stack of that axis; `size` is
 * along its own stack only.
 *
 * `avail` is the space the widget will get the OTHER way, or -1 when
 * nothing above knows it. Only a wrapping stack needs it: how deep its
 * lines go depends on how wide they may be (§5g rule 4).
 *
 * A stack's is its content: the sum along its axis, the largest across, its
 * padding both ends. `nest` is passed beside `t` because a child tree
 * measured here is the caller's own, which carries no `nest` of its own --
 * only the copies layout makes do. `depth` stops a tree that holds itself.
 */
static inline int wsk__nat_on(const wsk_tree_t *r, const wsk_tree_t *t, int i,
                              int axis, int depth, int avail);

/* A stack's natural extent: its content along `axis`, its padding both ends. */
static inline int wsk__nat_stack(const wsk_tree_t *r, const wsk_tree_t *c,
                                 int axis, int depth, int avail)
{
	int ext = 0, inner = (avail < 0) ? -1 : avail - 2 * c->pad;
#ifndef WSK_NO_SCROLL
	/* A stack that scrolls asks for nothing along the axis it scrolls
	   on: being shorter than its content is what it is for. Its size
	   comes from `size` or `weight`. */
	if (c->overflow == WSK_SCROLL && c->axis == axis) return 2 * c->pad;
#endif
#ifndef WSK_NO_WORLD
	/* nor does a world, either way: it is a view, sized from outside */
	if (c->overflow == WSK_WORLD) return 2 * c->pad;
#endif
#ifndef WSK_NO_WRAP
	/* A wrapping stack takes what it is given and wraps to fit it, so
	   along its axis it asks only for its widest unit -- item or word
	   (§5g rule 4); across, the depth of its lines at the width it will
	   get -- or, where nothing above knows that, one line. */
	if (c->overflow == WSK_WRAP && (c->axis == axis || inner >= 0)) {
		wsk__u_t u;
		int more;
		if (c->axis != axis)
			return wsk__flow(r, c, inner, -1, -1, 0, 0, 0, 0, depth + 1)
			     + 2 * c->pad;
		for (more = wsk__u_at(r, c, 0, &u); more; more = wsk__u_next(r, c, &u)) {
			int e = wsk__u_along(r, c, &u, depth + 1);
			if (e > ext) ext = e;
		}
		return ext + 2 * c->pad;
	}
#endif
	for (int j = 0; j < c->n; j++) {
		int e = (c->axis == axis)
		      ? wsk__nat_on(r, c, j, axis, depth + 1, inner)
		      : wsk__nat_on(r, c, j, axis, depth + 1, -1);
		if (c->axis == axis) ext += e + ((j > 0) ? c->gap : 0);
		else if (e > ext) ext = e;
	}
	return ext + 2 * c->pad;
}

#ifndef WSK_NO_WORLD
static inline void wsk__tcopy(wsk_tree_t *d, const wsk_tree_t *s);

/*
 * The same, inside a poster (§5q step 2b): the caller's tree is measured
 * through a copy at the poster's factor. Out of line, so only a poster
 * pays the copy's stack.
 */
#if defined(__GNUC__)
__attribute__((noinline, unused))
#endif
static int wsk__nat_z(const wsk_tree_t *r, const wsk_tree_t *c,
                      int axis, int depth, int avail)
{
	wsk_tree_t z;
	wsk__tcopy(&z, c);
	wsk__zscale(&z, r->wz);
	return wsk__nat_stack(r, &z, axis, depth, avail);
}
#endif

static inline int wsk__nat_on(const wsk_tree_t *r, const wsk_tree_t *t, int i,
                              int axis, int depth, int avail)
{
	const wsk_widget_t *w = &t->item[i];

#ifndef WSK_NO_SPLIT
	/* the caller's size for it, which a divider sets (§5x rule 1) */
	if (axis == t->axis && t->sizes != 0 && t->sizes[i] > 0) return wsk__zv(r, t->sizes[i]);
#endif
	if (w->size > 0 && axis == t->axis) return wsk__zv(r, w->size);
	if (w->kind == WSK_STACK) {
		if (r->nest == 0 || w->sub < 0 || w->sub >= r->nests || depth >= WSK_DEPTH_MAX)
			return 0;                    /* wsk_check refuses it by name */
#ifndef WSK_NO_WORLD
		if (r->wz != 0) return wsk__nat_z(r, &r->nest[w->sub], axis, depth, avail);
#endif
		return wsk__nat_stack(r, &r->nest[w->sub], axis, depth, avail);
	}
	if (axis == WSK_ROW) {
		const char *x = wsk__say(r, t, i);
		int text = wsk_text_w(t->font, wsk__chars(x, wsk__len(x)));
#ifndef WSK_NO_CHECK
		if (w->kind == WSK_CHECK) text += t->font + t->font / 2;
#endif
#ifndef WSK_NO_MARKS
		if (w->mark >= WSK_MARK_SHUT) text += t->font;     /* a tree's mark (§5t) */
#endif
		return text + 2 * t->pad;
	}
	if (w->kind == WSK_BUTTON) return wsk__zv(r, WSK_BTN_ROWS);
#ifndef WSK_NO_WORLD
	if (r->wz != 0) return t->font + t->gap;     /* rounding would not scale */
#endif
	return wsk__round_band(t->font + t->gap);
}

/* What a stack gives each of its items across its axis: its area, less its
   padding, capped -- the `avail` its items are measured with. */
static inline int wsk__across(const wsk_tree_t *t)
{
	int c = ((t->axis == WSK_COL) ? t->aw : t->ah) - 2 * t->pad;
	if (t->cross_max > 0 && c > t->cross_max) c = t->cross_max;
	return c;
}

static inline int wsk__natural(const wsk_tree_t *t, int i)
{
	return wsk__nat_on(t, t, i, t->axis, 0, wsk__across(t));
}
#endif

#ifndef WSK_NO_WRAP
/*
 * A widget's extent along `axis`, whatever this build has: through stacks
 * when it nests, from the widget alone when it does not. `r` is the tree
 * that carries `nest` and `str` -- the root, or a laid-out copy.
 */
static inline int wsk__meas(const wsk_tree_t *r, const wsk_tree_t *t, int i,
                            int axis, int depth, int avail)
{
#ifndef WSK_NO_NEST
	return wsk__nat_on(r, t, i, axis, depth, avail);
#else
	const wsk_widget_t *w = &t->item[i];
	(void)depth; (void)avail;
	if (w->size > 0 && axis == t->axis) return w->size;
	if (axis == WSK_ROW)
		return wsk_text_w(t->font, wsk__chars(wsk__say(r, t, i), wsk__len(wsk__say(r, t, i)))) + 2 * t->pad
#ifndef WSK_NO_CHECK
		     + ((w->kind == WSK_CHECK) ? t->font + t->font / 2 : 0)
#endif
#ifndef WSK_NO_MARKS
		     + ((w->mark >= WSK_MARK_SHUT) ? t->font : 0)
#endif
		     ;
	if (w->kind == WSK_BUTTON) return WSK_BTN_ROWS;
	return wsk__round_band(t->font + t->gap);
#endif
}

/*
 * Lines (design.md §5g), of units (§5h): items, and the words of text
 * items. Units go along the axis until one would cross `span`; it starts the
 * next line. Each line is as deep as its deepest unit and all of them are
 * that deep; within it, weight shares the line's leftover and, with none
 * weighted, justify places it -- an unwrapped stack's rules, a line at a
 * time. A line always takes at least one unit, so one wider than the span
 * sits alone and wsk_check refuses it by name.
 *
 * With idx >= 0, where unit (idx, wi) sits -- wi -1 for an item, or a text
 * item's first word -- relative to the content's start: along (ia, ea) and
 * across (ic, ec). With idx < 0, how deep all the lines are, gaps between
 * them included and padding not. One walk for both, so what is measured and
 * what is laid out cannot disagree.
 */
static inline int wsk__flow(const wsk_tree_t *r, const wsk_tree_t *t, int span,
                            int idx, int wi, int *ia, int *ic, int *ea, int *ec,
                            int depth)
{
	int lg = (t->lgap == WSK_SAME) ? t->gap : t->lgap;
	int cross = 0, more;
	wsk__u_t u;

	more = wsk__u_at(r, t, 0, &u);
	while (more) {
		wsk__u_t s0 = u, v;
		int used = 0, weights = 0, deep = 0, n = 0, lastw = -1, extra, j;

		do {
			int a = wsk__u_along(r, t, &u, depth);
			int need = used + ((n > 0) ? t->gap : 0) + a;
			int wt = (u.wi >= 0) ? 0 : t->item[u.k].weight;
			int d;
			if (n > 0 && need > span) break;
			used = need;
			weights += wt;
			if (wt > 0) lastw = n;
			d = wsk__u_deep(r, t, &u, a, depth);
			if (d > deep) deep = d;
			n++;
			more = wsk__u_next(r, t, &u);
		} while (more);
		extra = span - used;
		if (extra < 0) extra = 0;

		for (v = s0, j = 0; j < n; j++) {
			if (v.k == idx && (wi < 0 || v.wi == wi)) break;
			(void)wsk__u_next(r, t, &v);
		}
		if (j < n) {
			int at = 0, given = 0, e, wt, free = (weights > 0) ? 0 : extra;
			if (free > 0) {
				if (t->justify == WSK_END) at = free;
				else if (t->justify == WSK_CENTER) at = free / 2;
				else if (t->justify == WSK_BETWEEN) at = (n > 1) ? free * j / (n - 1) : 0;
				else if (t->justify == WSK_AROUND) at = free * (2 * j + 1) / (2 * n);
			}
			v = s0;
			for (int q = 0; q < j; q++) {
				wt = (v.wi >= 0) ? 0 : t->item[v.k].weight;
				e = wsk__u_along(r, t, &v, depth);
				if (weights > 0 && wt > 0) {
					int share = extra * wt / weights;
					e += share;
					given += share;
				}
				at += e + t->gap;
				(void)wsk__u_next(r, t, &v);
			}
			wt = (v.wi >= 0) ? 0 : t->item[v.k].weight;
			e = wsk__u_along(r, t, &v, depth);
			if (weights > 0 && wt > 0)
				e += (j == lastw) ? (extra - given) : (extra * wt / weights);
			*ia = at; *ea = e; *ic = cross; *ec = deep;
			return cross;
		}
		cross += deep;
		if (more) cross += lg;
	}
	return cross;
}

/* Unit (idx, wi) of a wrapping stack, in device pixels. */
static inline wsk_rect_t wsk__wrap_unit(const wsk_tree_t *t, int idx, int wi)
{
	wsk_rect_t r;
	int col = (t->axis == WSK_COL);
	int span = (col ? t->ah : t->aw) - 2 * t->pad;
	int ia = 0, ic = 0, ea = 0, ec = 0;

	(void)wsk__flow(t, t, span, idx, wi, &ia, &ic, &ea, &ec, 0);
	if (col) {
		r.y = (short)(t->ay + t->pad + ia); r.h = (short)ea;
		r.x = (short)(t->ax + t->pad + ic); r.w = (short)ec;
	} else {
		r.x = (short)(t->ax + t->pad + ia); r.w = (short)ea;
		r.y = (short)(t->ay + t->pad + ic); r.h = (short)ec;
	}
	return r;
}

/* Item idx of a wrapping stack: for a text item, the box round all its
   words, which is the rows a repaint of it needs (§5h rule 6). */
static inline wsk_rect_t wsk__wrap_rect(const wsk_tree_t *t, int idx)
{
	wsk_rect_t r = wsk__wrap_unit(t, idx, -1);
#ifndef WSK_NO_TEXT
	if (t->item[idx].kind == WSK_TEXT) {
		int x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h, more;
		wsk__u_t u;
		for (more = wsk__u_at(t, t, idx, &u); more && u.k == idx;
		     more = wsk__u_next(t, t, &u)) {
			wsk_rect_t q = wsk__wrap_unit(t, idx, u.wi);
			if (q.x < x0) x0 = q.x;
			if (q.y < y0) y0 = q.y;
			if (q.x + q.w > x1) x1 = q.x + q.w;
			if (q.y + q.h > y1) y1 = q.y + q.h;
		}
		r.x = (short)x0; r.y = (short)y0;
		r.w = (short)(x1 - x0); r.h = (short)(y1 - y0);
	}
#endif
	return r;
}

#ifndef WSK_NO_TEXT
/* Which word of text item i is at the point, or -1: a point between two
   words, or past the end of a line, is on none of them. */
static inline int wsk__word_at(const wsk_tree_t *t, int i, int x, int y)
{
	wsk__u_t u;
	int more;
	for (more = wsk__u_at(t, t, i, &u); more && u.k == i; more = wsk__u_next(t, t, &u)) {
		wsk_rect_t q = wsk__wrap_unit(t, i, u.wi);
		if (x >= q.x && x < q.x + q.w && y >= q.y && y < q.y + q.h) return u.wi;
	}
	return -1;
}
#endif
#endif

/*
 * Where widget i sits, in device pixels.
 *
 * Fixed extents first, then whatever is left over is shared by weight. Sizes
 * are pixels and not proportions, which is what makes the layer scale: a
 * repaint costs (its bands / total bands) of a frame, so a fixed-height
 * widget costs the same in absolute terms on a large screen as on a small
 * one. A bigger screen holds more widgets rather than bigger ones.
 */
/*
 * How far child `idx` is pushed along the axis by the justify.
 *
 * Computed from idx rather than accumulated, so the rounding cannot drift:
 * `free * i / (n-1)` is exactly `free` at the last child whatever the
 * division threw away on the way, which is the same care the weighted path
 * takes by giving the last weighted child the remainder.
 */
static inline int wsk__lead(const wsk_tree_t *t, int idx, int free)
{
	if (free <= 0) return 0;
	switch (t->justify) {
	case WSK_END:     return free;
	case WSK_CENTER:  return free / 2;
	case WSK_BETWEEN: return (t->n > 1) ? free * idx / (t->n - 1) : 0;
	case WSK_AROUND:  return (t->n > 0) ? free * (2 * idx + 1) / (2 * t->n) : 0;
	default:          return 0;            /* WSK_START */
	}
}

#ifndef WSK_NO_NEST
static inline void wsk__tcopy(wsk_tree_t *d, const wsk_tree_t *s);
static inline wsk_rect_t wsk_rect(const wsk_tree_t *t, int idx);

/*
 * Cell idx of a row that follows a header (§5n): along the row, where the
 * header's cell idx is, the header laid out across this row's own area; and
 * across, the row's own depth. The header is a tree like any other, sized by
 * `size` and `weight`, so the columns are whatever it says they are.
 */
static inline wsk_rect_t wsk__cell(const wsk_tree_t *t, int idx)
{
	wsk_rect_t r, hc;
	wsk_tree_t h;
	int ch = t->ah - 2 * t->pad;

	wsk__tcopy(&h, t->ghead);
	h.w = t->w; h.h = t->h;
	h.nest = t->nest; h.nests = t->nests;
	h.str = t->str; h.strs = t->strs;
	h.caps = t->caps;
	h.ax = t->ax; h.aw = t->aw; h.ay = t->ay; h.ah = t->ah;
	h.ghead = 0; h.clipped = 0;
#ifndef WSK_NO_WORLD
	h.wz = 0;
	if (t->wz != 0) wsk__zscale(&h, t->wz);       /* a table in a poster */
#endif
	if (idx < h.n) hc = wsk_rect(&h, idx);
	else { hc.x = (short)(t->ax + t->aw); hc.w = 0; }   /* refused by wsk_check */
	if (idx == 0 && t->rdepth != 0 && t->rindent > 0) {  /* a tree's row (§5p) */
		int d = t->rdepth * t->rindent;
		if (d > hc.w) d = hc.w;
		hc.x = (short)(hc.x + d);
		hc.w = (short)(hc.w - d);
	}
	if (t->cross_max > 0 && ch > t->cross_max) ch = t->cross_max;
	r.x = hc.x;
	r.w = hc.w;
	r.y = (short)(t->ay + (t->ah - ch) / 2);
	r.h = (short)ch;
	return r;
}
#endif

#ifndef WSK_NO_WORLD
/*
 * Item i of a world (§5q rule 3): its place less the view, times the zoom,
 * from the area's corner; its size times the zoom and its own scale. 64-bit
 * products, shifted and never divided (a module has no 64-bit division). One
 * too far off to be a coordinate is put wholly outside the area, where a cut
 * drops it -- nothing far away may wrap a short onto the screen (§5e).
 */
/* A coordinate into [0, n): 32-bit, since a module has no 64-bit modulo. */
static inline int wsk__wrap(int v, int n)
{
	v %= n;
	return (v < 0) ? v + n : v;
}

/* The item drawn j-th, bottom first: by the caller's order, or the array's. */
static inline int wsk__wi(const wsk_tree_t *t, int j)
{
	return (t->order != 0) ? t->order[j] : j;
}

static inline wsk_rect_t wsk__wrect_lim(const wsk_tree_t *t, int i, int ox, int oy, int poster);

static inline wsk_rect_t wsk__wrect_at(const wsk_tree_t *t, int i, int ox, int oy)
{
	return wsk__wrect_lim(t, i, ox, oy, 0);
}

/*
 * The same; with `poster`, for laying out what is inside: the true corner
 * kept as far as a short holds it (-32,000) and the far edge cut 32,000 on
 * -- a poster's children are placed from its corner, so cutting that to
 * the -16,000 an item's box is cut to moved them all (found by review,
 * 2026-11-16). Past -32,000 the corner is cut there, and what is inside
 * shifts: a poster that far off a screen at that zoom is named, not met.
 */
static inline wsk_rect_t wsk__wrect_lim(const wsk_tree_t *t, int i, int ox, int oy, int poster)
{
	const wsk_place_t *p = &t->place[i];
	long long z = t->zoom, zs = (long long)t->zoom * p->scale;
	long long dx = (long long)p->x - t->vx, dy = (long long)p->y - t->vy;
	long long x, y;
	if (t->wspace == WSK_TORUS) {
		/* the offset from the view, wrapped into the world (step 2 rule 1),
		   then the copy asked for: 0, or one world back */
		dx = wsk__wrap(p->x, t->ww) - wsk__wrap(t->vx, t->ww);
		dy = wsk__wrap(p->y, t->wh) - wsk__wrap(t->vy, t->wh);
		if (dx < 0) dx += t->ww;
		if (dy < 0) dy += t->wh;
		dx += ox;
		dy += oy;
	}
	x = t->ax + ((dx * z) >> 8);
	y = t->ay + ((dy * z) >> 8);
	long long w = ((long long)p->w * zs) >> 16, h = ((long long)p->h * zs) >> 16;
	wsk_rect_t r;
	/* cut to what a short can say, in 64 bits first: a floor bigger than
	   the screen zoomed in still covers it; one wholly beyond is nowhere */
	{
		long long x1 = x + w, y1 = y + h, lo = poster ? -32000 : -16000;
		if (x < lo) x = lo;
		if (y < lo) y = lo;
		if (x1 > (poster ? x + 32000 : 16000)) x1 = poster ? x + 32000 : 16000;
		if (y1 > (poster ? y + 32000 : 16000)) y1 = poster ? y + 32000 : 16000;
		if (x1 > 32000) x1 = 32000;         /* and every edge a short */
		if (y1 > 32000) y1 = 32000;
		if (x1 <= x || y1 <= y) { r.x = -16000; r.y = -16000; r.w = 0; r.h = 0; return r; }
		w = x1 - x; h = y1 - y;
	}
	r.x = (short)x; r.y = (short)y; r.w = (short)w; r.h = (short)h;
	return r;
}

static inline wsk_rect_t wsk__wrect(const wsk_tree_t *t, int i)
{
	return wsk__wrect_at(t, i, t->wdx, t->wdy);
}

static inline int wsk__cut(const wsk_tree_t *t, wsk_rect_t *r);

/*
 * Whether a point is on item i of a world -- on any of its copies, for a
 * torus, each cut by the view (step 2 rule 2).
 */
/* ...1 + the copy that was hit (bit 0: a world back across, bit 1: down),
   so a caller can lay out the copy the person sees; 0 if none */
static inline int wsk__whit(const wsk_tree_t *t, int i, int x, int y)
{
	for (int k = 0; k < ((t->wspace == WSK_TORUS) ? 4 : 1); k++) {
		wsk_rect_t r = wsk__wrect_at(t, i, (k & 1) ? -t->ww : 0, (k & 2) ? -t->wh : 0);
		if (!wsk__cut(t, &r)) continue;
		if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return k + 1;
	}
	return 0;
}

/*
 * A colour faded toward the world's background (step 2, §5q rule 6): /w0
 * has no opacity, so fading is a colour. 0 leaves it exactly; 255 is the
 * background. No division: the fraction is taken out of 256.
 */
static inline unsigned int wsk__fade(const wsk_tree_t *t, unsigned int c)
{
	unsigned int out = 0;
	int a = t->wfade + (t->wfade >> 7);
	if (t->wfade == 0 || c == WSK_INHERIT) return c;
	for (int sh = 0; sh < 24; sh += 8) {
		int x = (int)((c >> sh) & 0xFF), b = (int)((t->wbg >> sh) & 0xFF);
		out |= (unsigned int)(((x * (256 - a) + b * a) >> 8) & 0xFF) << sh;
	}
	return out;
}
#else
#define wsk__fade(t, c) (c)
#endif

/* A widget's weight -- none once the caller has sized it (§5x rule 1). */
static inline int wsk__wt(const wsk_tree_t *t, int i)
{
#ifndef WSK_NO_SPLIT
	if (t->sizes != 0 && t->sizes[i] > 0) return 0;
#endif
	return t->item[i].weight;
}

static inline wsk_rect_t wsk_rect(const wsk_tree_t *t, int idx)
{
	wsk_rect_t r;
	int span = (t->axis == WSK_COL ? t->ah : t->aw) - 2 * t->pad;
	int used = 0, weights = 0, at, extra, given = 0;

#ifndef WSK_NO_WRAP
	if (t->overflow == WSK_WRAP) return wsk__wrap_rect(t, idx);
#endif
#ifndef WSK_NO_WORLD
	if (t->overflow == WSK_WORLD) return wsk__wrect(t, idx);
#endif
#ifndef WSK_NO_NEST
	if (t->ghead != 0) return wsk__cell(t, idx);
#endif

	for (int i = 0; i < t->n; i++) {
		used += wsk__natural(t, i);
		weights += wsk__wt(t, i);
	}
	if (t->n > 1) used += (t->n - 1) * t->gap;
	extra = span - used;
	if (extra < 0) extra = 0;

	/* A weighted stack has no surplus left: the children took it. */
	at = (t->axis == WSK_COL ? t->ay : t->ax) + t->pad
	   + wsk__lead(t, idx, (weights > 0) ? 0 : extra);
#ifndef WSK_NO_SCROLL
	/* Slid back by the offset. A stack with something to scroll has no
	   surplus, so nothing above moves it as well; one with nothing to
	   scroll has scroll == 0, or wsk_check refused it. */
	if (t->overflow == WSK_SCROLL) at -= t->scroll;
#endif
	for (int i = 0; i < idx; i++) {
		int e = wsk__natural(t, i);
		if (weights > 0 && wsk__wt(t, i) > 0) {
			int share = extra * wsk__wt(t, i) / weights;
			e += share;
			given += share;
		}
		at += e + t->gap;
	}

	{
		int e = wsk__natural(t, idx);
		if (weights > 0 && wsk__wt(t, idx) > 0) {
			/* the last weighted child takes the rounding, so the stack
			   ends where the padding says it should */
			int last = -1;
			for (int i = 0; i < t->n; i++) if (wsk__wt(t, i) > 0) last = i;
			e += (idx == last) ? (extra - given)
			                   : (extra * wsk__wt(t, idx) / weights);
		}
		if (t->axis == WSK_COL) {
			int cw = t->aw - 2 * t->pad;
			if (t->cross_max > 0 && cw > t->cross_max) cw = t->cross_max;
			r.w = (short)cw;
			r.x = (short)(t->ax + (t->aw - cw) / 2);
			r.y = (short)at;
			r.h = (short)e;
			/* a tree (§5p) -- but a table's rows stay under their header,
			   and only their first cell is indented (wsk__cell) */
			if (t->indent > 0 && t->item[idx].depth != 0 && t->head == WSK_NONE) {
				int d = t->item[idx].depth * t->indent;
				if (d > cw) d = cw;
				r.x = (short)(r.x + d);
				r.w = (short)(r.w - d);
			}
		} else {
			int ch = t->ah - 2 * t->pad;
			if (t->cross_max > 0 && ch > t->cross_max) ch = t->cross_max;
			r.h = (short)ch;
			r.y = (short)(t->ay + (t->ah - ch) / 2);
			r.x = (short)at;
			r.w = (short)e;
		}
	}
	return r;
}

static inline int wsk_find(const wsk_tree_t *t, const char *id)
{
	for (int i = 0; i < t->n; i++)
		if (wsk__same(t->item[i].id, id)) return i;
	return WSK_E_NOTFOUND;
}

/* ------------------------------------------------------------------ */
/* Nesting: a stack inside a stack (design.md §5f)                      */
/* ------------------------------------------------------------------ */

#ifndef WSK_NO_NEST
/*
 * A tree, a byte at a time. Not `*d = *s`: a struct this size is copied with
 * a call to memcpy, which a module has no way to link (see wsk_clone) -- and
 * a plain byte loop is recognised as memcpy and turned back into the same
 * call, so the empty asm says the loop must stay a loop. Field by field was
 * 460 bytes of code; this is a few dozen, and ninety-odd byte moves is
 * nothing beside the layout it precedes. A char may alias anything, so the
 * loop is defined C; the barrier is GCC's and clang's spelling.
 */
static inline void wsk__tcopy(wsk_tree_t *d, const wsk_tree_t *s)
{
	unsigned char *o = (unsigned char *)d;
	const unsigned char *i = (const unsigned char *)s;
	for (unsigned k = 0; k < sizeof *d; k++) {
		o[k] = i[k];
#if defined(__GNUC__)
		__asm__ volatile ("" ::: "memory");
#endif
	}
}

/*
 * The tree inside widget i, ready to lay out: the caller's child tree, with
 * the parent's device and `nest`, laid out in the widget's own rectangle,
 * and cut by everything that cuts the parent -- plus the parent's own
 * viewport when the parent scrolls (rule 3). 0 when the widget names no
 * tree, which wsk_check has already refused by name.
 */
static inline int wsk__child(const wsk_tree_t *t, int i, wsk_tree_t *c)
{
	const wsk_widget_t *w = &t->item[i];
	wsk_rect_t r;

	if (t->nest == 0 || w->sub < 0 || w->sub >= t->nests) return 0;
	wsk__tcopy(c, &t->nest[w->sub]);
	c->w = t->w;
	c->h = t->h;
	c->nest = t->nest;
	c->nests = t->nests;
	c->str = t->str;
	c->strs = t->strs;
	c->held = t->held;
	c->buf = t->buf;
	c->bufs = t->bufs;
	c->focus = t->focus;
	c->over = WSK_NONE;                 /* an overlay is the root's alone */
	c->state = t->state;
	c->states = t->states;
	/* a row in a table follows its header (§5n) */
	c->ghead = (t->head != WSK_NONE && t->head >= 0 && t->head < t->nests &&
	            c->axis == WSK_ROW) ? &t->nest[t->head] : 0;
	c->rdepth = w->depth;               /* a row's depth, for its first cell */
	c->rindent = t->indent;
	c->caps = t->caps;
	r = wsk_rect(t, i);
#ifndef WSK_NO_WORLD
	if (t->overflow == WSK_WORLD) r = wsk__wrect_lim(t, i, t->wdx, t->wdy, 1);
#endif
	c->ax = r.x; c->ay = r.y; c->aw = r.w; c->ah = r.h;

	c->clipped = t->clipped;
	c->cx0 = t->cx0; c->cy0 = t->cy0; c->cx1 = t->cx1; c->cy1 = t->cy1;
#ifndef WSK_NO_WORLD
	/* A poster (§5q step 2b): laid out at the world's zoom times its own
	   scale -- times any poster it is in -- its lengths scaled, and cut by
	   the view and by its own rectangle, which rounding may cross. */
	c->wz = t->wz;
	c->wone = 0;
	if (t->overflow == WSK_WORLD) {
		long long f = ((long long)t->zoom * t->place[i].scale) >> 8;
		wsk_rect_t k = r;
		if (t->wz != 0) f = (f * t->wz) >> 8;
		/* at most 64 times: further, a short length overflows */
		c->wz = (f < 1) ? 1 : (f > 16384) ? 16384 : (int)f;
		if (!wsk__cut(t, &k)) k.w = k.h = 0;
		c->cx0 = k.x; c->cy0 = k.y;
		c->cx1 = (short)(k.x + k.w); c->cy1 = (short)(k.y + k.h);
		c->clipped = 1;
	}
	if (c->wz != 0) wsk__zscale(c, c->wz);
	c->wfade = t->wfade;
	c->wbg = t->wbg;                    /* toward the world's, not the poster's own */
	if (c->wfade != 0) {                /* faded with the poster (step 2) */
		c->ink = wsk__fade(c, c->ink);
		c->accent = wsk__fade(c, c->accent);
		c->trough = wsk__fade(c, c->trough);
		c->selfill = wsk__fade(c, c->selfill);
	}
#endif
#ifndef WSK_NO_SCROLL
	if (t->overflow == WSK_SCROLL) {
		int x0 = t->ax, y0 = t->ay, x1 = t->ax + t->aw, y1 = t->ay + t->ah;
		if (!c->clipped) {
			c->cx0 = (short)x0; c->cy0 = (short)y0;
			c->cx1 = (short)x1; c->cy1 = (short)y1;
			c->clipped = 1;
		} else {
			if (x0 > c->cx0) c->cx0 = (short)x0;
			if (y0 > c->cy0) c->cy0 = (short)y0;
			if (x1 < c->cx1) c->cx1 = (short)x1;
			if (y1 < c->cy1) c->cy1 = (short)y1;
		}
	}
#endif
	return 1;
}

/*
 * One level, then each stack inside it, each in the rectangle its parent
 * gave it -- so D10 holds at every depth (rule 2). `in` gets the caller's own
 * tree the refusal was found in, not the copy it was laid out in, so that
 * `in->item[which]` is the widget to blame.
 */
static inline int wsk__check_at(const wsk_tree_t *t, const wsk_tree_t *orig,
                                int *which, const wsk_tree_t **in, int depth)
{
	int e = wsk__check1(t, which);

	if (e != WSK_OK) { if (in != 0) *in = orig; return e; }
	for (int i = 0; i < t->n; i++) {
		wsk_tree_t c;
		if (t->item[i].kind != WSK_STACK) continue;
		(void)wsk__child(t, i, &c);         /* wsk__check1 vouched for sub */
#ifndef WSK_NO_WORLD
		if (t->overflow == WSK_WORLD) {
			/* a poster is checked as declared: its place's size, at 1:1
			   (step 2b rule 4), whatever the view is doing */
			const wsk_tree_t *o = &t->nest[t->item[i].sub];
			c.font = o->font; c.pad = o->pad; c.gap = o->gap; c.lgap = o->lgap;
			c.cross_max = o->cross_max; c.indent = o->indent;
			wsk__zscale(&c, 256);
			c.aw = (short)t->place[i].w; c.ah = (short)t->place[i].h;
			c.clipped = 0;
		}
#endif
		e = wsk__check_at(&c, &t->nest[t->item[i].sub], which, in, depth + 1);
		if (e != WSK_OK) return e;
	}
	return WSK_OK;
}

/*
 * How deep the stacks go, by structure alone and before any layout: a
 * stack's natural size measures every tree under it, and measured past the
 * limit from one level and short of it from the next, two levels would
 * disagree about the same widget and the refusal would come out as "does
 * not fit". So depth is refused first, as itself.
 */
static inline int wsk__deep(const wsk_tree_t *nest, int nests, const wsk_tree_t *t,
                            int *which, const wsk_tree_t **in, int depth)
{
	for (int i = 0; i < t->n; i++) {
		const wsk_widget_t *w = &t->item[i];
		int e;
		if (w->kind != WSK_STACK || w->sub < 0 || w->sub >= nests) continue;
		if (depth + 1 >= WSK_DEPTH_MAX) {
			if (which != 0) *which = i;
			if (in != 0) *in = t;
			return WSK_E_DEPTH;
		}
		e = wsk__deep(nest, nests, &nest[w->sub], which, in, depth + 1);
		if (e != WSK_OK) return e;
	}
	return WSK_OK;
}

/*
 * A tree used by two stacks would draw its ids twice, and an id is prime
 * (D19) -- so each tree in `nest` may be inside at most one widget, anywhere.
 * Counted over the root and every tree in `nest`, reachable or not.
 */
static inline int wsk__shared(const wsk_tree_t *t, int *which,
                              const wsk_tree_t **in)
{
	for (int k = 0; k < t->nests; k++) {
		int seen = 0;
		for (int a = -1; a < t->nests; a++) {
			const wsk_tree_t *u = (a < 0) ? t : &t->nest[a];
			if (a >= 0 && u == t) continue;   /* the root, kept in its own nest */
			for (int i = 0; i < u->n; i++) {
				if (u->item[i].kind != WSK_STACK || u->item[i].sub != k) continue;
				if (++seen > 1) {
					if (which != 0) *which = i;
					if (in != 0) *in = u;
					return WSK_E_KIND;
				}
			}
		}
	}
	return WSK_OK;
}

#ifndef WSK_NO_OVER
static inline int wsk__over_tree(const wsk_tree_t *t, wsk_tree_t *c);
#endif

/* wsk_check, saying which tree as well as which widget. */
static inline int wsk_check_in(const wsk_tree_t *t, int *which,
                               const wsk_tree_t **in)
{
	/* Sharing first: it needs no layout, and a shared tree laid out in the
	   wrong one of its two places would be blamed for not fitting. */
	if (t->nest != 0) {
		int e = wsk__shared(t, which, in);
		if (e == WSK_OK) e = wsk__deep(t->nest, t->nests, t, which, in, 0);
		if (e != WSK_OK) return e;
	}
#ifndef WSK_NO_OVER
	if (t->over != WSK_NONE) {
		/* an overlay's place is whisker's arithmetic, but a caller may set
		   it by hand, so it is checked: on the screen, and fitting */
		int e = wsk__check_at(t, t, which, in, 0);
		wsk_tree_t c;
		if (e != WSK_OK) return e;
		if (!wsk__over_tree(t, &c) || t->ox < 0 || t->oy < 0 ||
		    t->ox + t->ow > t->w || t->oy + t->oh > t->h) {
			if (which != 0) *which = -1;
			if (in != 0) *in = t;
			return WSK_E_KIND;
		}
		return wsk__check_at(&c, &t->nest[t->over], which, in, 1);
	}
#endif
	return wsk__check_at(t, t, which, in, 0);
}

static inline int wsk_check(const wsk_tree_t *t, int *which)
{
	return wsk_check_in(t, which, 0);
}

#ifndef WSK_NO_OVER
/* ------------------------------------------------------------------ */
/* Overlays: a tree opened over the root (design.md §5l)                */
/* ------------------------------------------------------------------ */

/*
 * The open overlay, laid out: the caller's tree, with the root's device,
 * `nest`, strings, buffers and press, in the place wsk_over_open gave it.
 * Nothing above it cuts it -- it is above everything.
 */
static inline int wsk__over_tree(const wsk_tree_t *t, wsk_tree_t *c)
{
	if (t->over == WSK_NONE || t->nest == 0 || t->over < 0 || t->over >= t->nests)
		return 0;
	wsk__tcopy(c, &t->nest[t->over]);
	c->w = t->w; c->h = t->h;
	c->nest = t->nest; c->nests = t->nests;
	c->str = t->str; c->strs = t->strs;
	c->buf = t->buf; c->bufs = t->bufs;
	c->held = t->held; c->focus = t->focus;
	c->state = t->state; c->states = t->states;
	c->caps = t->caps;
	c->ghead = 0;
	c->ax = t->ox; c->ay = t->oy; c->aw = t->ow; c->ah = t->oh;
	c->clipped = 0;
	c->over = WSK_NONE;
	return 1;
}

/* Whether a point is on the open overlay -- which then answers for it. */
static inline int wsk__on_over(const wsk_tree_t *t, int x, int y)
{
	return t->over != WSK_NONE && x >= t->ox && x < t->ox + t->ow &&
	       y >= t->oy && y < t->oy + t->oh;
}

/* A tree's content along `axis`: as a stack's natural extent is (§5f rule
   1), but asked of a tree that is not inside a widget. */
static inline int wsk__content(const wsk_tree_t *r, const wsk_tree_t *c, int axis)
{
	int ext = 0;
	for (int j = 0; j < c->n; j++) {
		int e = wsk__nat_on(r, c, j, axis, 1, -1);
		if (c->axis == axis) ext += e + ((j > 0) ? c->gap : 0);
		else if (e > ext) ext = e;
	}
	return ext + 2 * c->pad;
}

/*
 * Open tree `sub` over the root, hanging from `anchor` (§5l rule 1): as big
 * as its content and no narrower than the anchor; below it, or above it if
 * there is no room below; moved in from the right edge if it would cross it.
 * Taller than the screen is refused, WSK_E_FIT (D10) -- unless the tree
 * scrolls, when it is given the screen's height to scroll in.
 */
static inline int wsk_over_open(wsk_tree_t *t, int sub, wsk_rect_t anchor)
{
	const wsk_tree_t *c;
	int w, h, x, y;

	if (t->nest == 0 || sub < 0 || sub >= t->nests) return WSK_E_KIND;
	c = &t->nest[sub];
	w = wsk__content(t, c, WSK_ROW);
	h = wsk__content(t, c, WSK_COL);
	if (w < anchor.w) w = anchor.w;
	if (w > t->w) return WSK_E_FIT;
	if (h > t->h) {
#ifndef WSK_NO_SCROLL
		if (c->overflow != WSK_SCROLL || c->axis != WSK_COL) return WSK_E_FIT;
		h = t->h;
#else
		return WSK_E_FIT;
#endif
	}
#ifndef WSK_NO_MENUKEYS
	t->ohi = WSK_NONE;                             /* opened with nothing reached */
#endif
	x = anchor.x;
	y = anchor.y + anchor.h;
	if (y + h > t->h) y = anchor.y - h;            /* no room below: above */
	if (y < 0) y = t->h - h;                       /* nor above: as low as it fits */
	if (x + w > t->w) x = t->w - w;
	if (x < 0) x = 0;
	t->over = (short)sub;
	t->ox = (short)x; t->oy = (short)y; t->ow = (short)w; t->oh = (short)h;
	return WSK_OK;
}

static inline void wsk_over_close(wsk_tree_t *t)
{
	t->over = WSK_NONE;
	t->omodal = 0;
#ifndef WSK_NO_MENUKEYS
	t->ohi = WSK_NONE;
#endif
}

/* The rows the open overlay covers, band-snapped: what opening or closing
   it repaints. */
static inline int wsk_over_rows(const wsk_tree_t *t, int *y0, int *y1)
{
	int lo, hi;
	if (t->over == WSK_NONE) return WSK_E_EMPTY;
	lo = (t->oy / WSK_BAND) * WSK_BAND;
	hi = wsk__round_band(t->oy + t->oh);
	if (lo < 0) lo = 0;
	if (hi > t->h) hi = t->h;
	*y0 = lo;
	*y1 = hi;
	return WSK_OK;
}
#endif /* WSK_NO_OVER */


/*
 * A child tree, started with its parent's look -- device, font, colours, ids
 * -- so that nesting does not mean restating them. Padding, gap and the rest
 * are wsk_init's. Nothing is inherited later or silently: what the child
 * says after this is what it is.
 */
static inline void wsk_nest_init(wsk_tree_t *c, const wsk_tree_t *parent, int axis)
{
	wsk_init(c, axis, parent->w, parent->h);
	c->font = parent->font;
	c->bg = parent->bg;
	c->ink = parent->ink;
	c->accent = parent->accent;
	c->trough = parent->trough;
	c->ids = parent->ids;
}
#else
static inline int wsk_check(const wsk_tree_t *t, int *which)
{
	return wsk__check1(t, which);
}
#endif /* WSK_NO_NEST */

/* ------------------------------------------------------------------ */
/* The viewport: what a scrolling stack lets you see                    */
/* ------------------------------------------------------------------ */

/*
 * A widget's rectangle cut to its stack's area, along the axis; 0 when none
 * of it is in view, and the rectangle untouched for a stack that does not
 * scroll (design.md §5e).
 *
 * Cut by arithmetic because there is nothing else to cut with: no clipPath
 * on /w0 (§2.4), and /w0's row clip belongs to one display system and is
 * vertical only today, so whisker may not lean on it (§2.8). A rectangle can
 * be intersected; text cannot, which is wsk__whole's business.
 */
static inline int wsk__cut(const wsk_tree_t *t, wsk_rect_t *r)
{
#ifndef WSK_NO_SCROLL
	if (t->overflow == WSK_SCROLL) {
		/* one axis, spelled once: the same four fields either way */
		int col = (t->axis == WSK_COL);
		short *at = col ? &r->y : &r->x, *len = col ? &r->h : &r->w;
		int a = col ? t->ay : t->ax, b = a + (col ? t->ah : t->aw);
		int lo = *at, hi = *at + *len;
		if (lo < a) lo = a;
		if (hi > b) hi = b;
		if (hi <= lo) return 0;
		*at = (short)lo;
		*len = (short)(hi - lo);
	}
#else
	(void)t; (void)r;
#endif
#ifndef WSK_NO_WORLD
	/* a world's view cuts both ways (§5q rule 7) */
	if (t->overflow == WSK_WORLD) {
		int x0 = r->x, y0 = r->y, x1 = r->x + r->w, y1 = r->y + r->h;
		if (x0 < t->ax) x0 = t->ax;
		if (y0 < t->ay) y0 = t->ay;
		if (x1 > t->ax + t->aw) x1 = t->ax + t->aw;
		if (y1 > t->ay + t->ah) y1 = t->ay + t->ah;
		if (x1 <= x0 || y1 <= y0) return 0;
		r->x = (short)x0; r->y = (short)y0;
		r->w = (short)(x1 - x0); r->h = (short)(y1 - y0);
	}
#endif
#ifndef WSK_NO_NEST
	/* and by every viewport above it, along both axes (§5f rule 3) */
	if (t->clipped) {
		int x0 = r->x, y0 = r->y, x1 = r->x + r->w, y1 = r->y + r->h;
		if (x0 < t->cx0) x0 = t->cx0;
		if (y0 < t->cy0) y0 = t->cy0;
		if (x1 > t->cx1) x1 = t->cx1;
		if (y1 > t->cy1) y1 = t->cy1;
		if (x1 <= x0 || y1 <= y0) return 0;
		r->x = (short)x0; r->y = (short)y0;
		r->w = (short)(x1 - x0); r->h = (short)(y1 - y0);
	}
#endif
	return 1;
}

/*
 * Whether [lo, hi) along the axis lies wholly in view. Text, the corner
 * marks and the caret are drawn whole or not at all, because none of them
 * can be cut: a label pops in at the edge of a scrolling pane rather than
 * sliding in, which is the flat substrate's price and is paid nowhere else.
 */
static inline int wsk__whole(const wsk_tree_t *t, int lo, int hi)
{
#ifndef WSK_NO_SCROLL
	if (t->overflow == WSK_SCROLL) {
		int a = (t->axis == WSK_COL) ? t->ay : t->ax;
		int b = a + ((t->axis == WSK_COL) ? t->ah : t->aw);
		return lo >= a && hi <= b;
	}
#else
	(void)t; (void)lo; (void)hi;
#endif
	return 1;
}

#ifndef WSK_NO_NEST
/* Whether a box lies wholly inside every viewport above its stack: the
   other half of drawing text whole or not at all, once stacks nest. */
static inline int wsk__seen(const wsk_tree_t *t, int x0, int y0, int x1, int y1)
{
	if (!t->clipped) return 1;
	return x0 >= t->cx0 && y0 >= t->cy0 && x1 <= t->cx1 && y1 <= t->cy1;
}
#endif

/* ------------------------------------------------------------------ */
/* Hit: a point, and the name of what is under it                      */
/* ------------------------------------------------------------------ */

/*
 * Which widget is at this point, or WSK_E_NOTFOUND.
 *
 * The point is in device pixels, which costs nothing because the viewBox is
 * the device: a tap arrives in the same units wsk_rows hands back.
 *
 * Edges are half-open -- a widget owns [x, x+w) by [y, y+h) -- so two
 * widgets with no gap between them never both claim the pixel they share,
 * and no pixel between them belongs to neither.
 *
 * NOTHING is under a point in the padding, in a gap, or outside the area,
 * and saying so is most of what this function is for. A hit test that
 * always answered would pass every test that only ever asks about the
 * middle of a button, and would make the gaps into whichever widget came
 * first -- which a person discovers by pressing one thing and watching
 * another light up.
 *
 * There is no slop: a tap two pixels into a gap misses. Whether it should
 * depends on how accurate the touch controller's centroid is, and nobody
 * has measured that yet (§5b).
 *
 * A label is returned like anything else. whisker does not know which
 * widgets are interactive -- it has no state and no event model -- so the
 * caller reads `kind` and decides. In a single-axis stack nothing overlaps,
 * so there is never a second answer to choose between.
 */
#ifndef WSK_NO_OVER
static inline int wsk__on_over(const wsk_tree_t *t, int x, int y);
#endif

static inline int wsk_hit(const wsk_tree_t *t, int x, int y)
{
#ifndef WSK_NO_OVER
	if (wsk__on_over(t, x, y)) return WSK_E_NOTFOUND;   /* not a root widget */
#endif
	for (int j = 0; j < t->n; j++) {
		int i = j;
#ifndef WSK_NO_WORLD
		if (t->overflow == WSK_WORLD) {               /* the topmost first (§5q) */
			i = wsk__wi(t, t->n - 1 - j);
			if (wsk__whit(t, i, x, y)) return i;
			continue;
		}
#endif
		wsk_rect_t r = wsk_rect(t, i);
		/* cut first: a widget scrolled off still has a rectangle, and it
		   may lie over a neighbouring stack the person can see (§5e) */
		if (!wsk__cut(t, &r)) continue;
		if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
#if !defined(WSK_NO_TEXT) && !defined(WSK_NO_WRAP)
			/* a paragraph's box holds its line ends and the gaps between
			   words; only a word is the paragraph (§5h rule 6) */
			if (t->item[i].kind == WSK_TEXT && t->overflow == WSK_WRAP &&
			    wsk__word_at(t, i, x, y) < 0) continue;
#endif
			return i;
		}
	}
	return WSK_E_NOTFOUND;
}

/*
 * The same thing as the address rather than the index: `hit(tree, x, y)`
 * returns the id, which is the form §1 names and the one a host hands back
 * to its application. Null when nothing is there.
 */
#ifndef WSK_NO_NEST
/*
 * The DEEPEST widget at the point, and the tree it is in (§5f rule 5): a
 * leaf, or a stack when the point is in that stack's padding or gaps -- a
 * stack is a widget with an id, so it has an answer there and the root,
 * which is not a widget, does not. `in` is the caller's own tree, so
 * `in->item[i]` is the widget.
 *
 * wsk_hit keeps its old answer, an index in the root -- the outermost
 * widget there -- so that a flat caller is unchanged and a nested one can
 * still ask which top-level region was touched.
 */
static inline int wsk__hit_at(const wsk_tree_t *t, const wsk_tree_t *orig,
                              int x, int y, const wsk_tree_t **in,
                              wsk_tree_t *at, int depth);

#ifndef WSK_NO_WORLD
/*
 * Into a poster (§5q step 2b), laid out from the copy that was hit: its
 * buttons are buttons. Out of line, so the two copies cost only a world.
 */
#if defined(__GNUC__)
__attribute__((noinline, unused))
#endif
static int wsk__whit_in(const wsk_tree_t *t, int i, int hitk, int x, int y,
                        const wsk_tree_t **in, wsk_tree_t *at, int depth)
{
	wsk_tree_t h, c;
	wsk__tcopy(&h, t);
	h.wdx = ((hitk - 1) & 1) ? -t->ww : 0;
	h.wdy = ((hitk - 1) & 2) ? -t->wh : 0;
	if (!wsk__child(&h, i, &c)) return WSK_E_NOTFOUND;
	return wsk__hit_at(&c, &t->nest[t->item[i].sub], x, y, in, at, depth + 1);
}
#endif

static inline int wsk__hit_at(const wsk_tree_t *t, const wsk_tree_t *orig,
                              int x, int y, const wsk_tree_t **in,
                              wsk_tree_t *at, int depth)
{
#ifndef WSK_NO_OVER
	/* An open overlay is asked first, and a point on it is its to answer --
	   a widget, or nothing in its padding, but never what lies beneath
	   (§5l rule 3). */
	if (depth == 0 && wsk__on_over(t, x, y)) {
		wsk_tree_t c;
		if (!wsk__over_tree(t, &c)) return WSK_E_NOTFOUND;
		return wsk__hit_at(&c, &t->nest[t->over], x, y, in, at, 1);
	}
#endif
	for (int j = 0; j < t->n; j++) {
		int i = j;
#ifndef WSK_NO_WORLD
		if (t->overflow == WSK_WORLD) {               /* the topmost first (§5q) */
			int hitk;
			i = wsk__wi(t, t->n - 1 - j);
			if ((hitk = wsk__whit(t, i, x, y)) == 0) continue;
			if (t->item[i].kind == WSK_STACK && depth + 1 < WSK_DEPTH_MAX) {
				int k = wsk__whit_in(t, i, hitk, x, y, in, at, depth);
				if (k >= 0) return k;
			}
			if (in != 0) *in = orig;
			if (at != 0) {
				/* laid out as the copy that was hit, so its rectangle --
				   a held button's rows -- is the one on the screen */
				wsk__tcopy(at, t);
				at->wdx = ((hitk - 1) & 1) ? -t->ww : 0;
				at->wdy = ((hitk - 1) & 2) ? -t->wh : 0;
			}
			return i;
		}
#endif
		wsk_rect_t r = wsk_rect(t, i);
		if (!wsk__cut(t, &r)) continue;
		if (!(x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)) continue;
#if !defined(WSK_NO_TEXT) && !defined(WSK_NO_WRAP)
		if (t->item[i].kind == WSK_TEXT && t->overflow == WSK_WRAP &&
		    wsk__word_at(t, i, x, y) < 0) continue;
#endif
		if (t->item[i].kind == WSK_STACK && depth + 1 < WSK_DEPTH_MAX) {
			wsk_tree_t c;
			if (wsk__child(t, i, &c)) {
				int k = wsk__hit_at(&c, &t->nest[t->item[i].sub], x, y, in,
				                    at, depth + 1);
				if (k >= 0) return k;
			}
		}
		if (in != 0) *in = orig;
		if (at != 0) wsk__tcopy(at, t);
		return i;
	}
	return WSK_E_NOTFOUND;
}

static inline int wsk_hit_in(const wsk_tree_t *t, int x, int y,
                             const wsk_tree_t **in)
{
	return wsk__hit_at(t, t, x, y, in, 0, 0);
}

#if !defined(WSK_NO_TEXT) && !defined(WSK_NO_WRAP)
/*
 * Which word of a paragraph is under the point (§5h rule 6): returns the
 * item, as wsk_hit_in does, and sets *word to the word's number in its
 * string -- or -1 when the widget there is not a wrapped text item.
 */
static inline int wsk_hit_word(const wsk_tree_t *t, int x, int y,
                               const wsk_tree_t **in, int *word)
{
	wsk_tree_t at;
	int k = wsk__hit_at(t, t, x, y, in, &at, 0);
	*word = -1;
	if (k >= 0 && at.item[k].kind == WSK_TEXT && at.overflow == WSK_WRAP)
		*word = wsk__word_at(&at, k, x, y);
	return k;
}
#endif

static inline const char *wsk_hit_id(const wsk_tree_t *t, int x, int y)
{
	const wsk_tree_t *in = t;
	int i = wsk_hit_in(t, x, y, &in);
	return (i < 0) ? 0 : in->item[i].id;
}
#else
static inline int wsk_hit_in(const wsk_tree_t *t, int x, int y,
                             const wsk_tree_t **in)
{
	if (in != 0) *in = t;
	return wsk_hit(t, x, y);
}

static inline const char *wsk_hit_id(const wsk_tree_t *t, int x, int y)
{
	int i = wsk_hit(t, x, y);
	return (i < 0) ? 0 : t->item[i].id;
}
#endif

#ifndef WSK_NO_WORLD
/*
 * The world point under a point on the screen (§5q rule 8) -- the inverse of
 * an item's placing -- so a click on bare floor is somewhere to walk to.
 * WSK_E_NOTFOUND off the world's area, or for a tree that is not a world.
 */
static inline int wsk_world_at(const wsk_tree_t *t, int x, int y, int *wx, int *wy)
{
	if (t->overflow != WSK_WORLD || t->zoom <= 0 ||
	    x < t->ax || x >= t->ax + t->aw || y < t->ay || y >= t->ay + t->ah)
		return WSK_E_NOTFOUND;
	*wx = t->vx + (x - t->ax) * 256 / t->zoom;
	*wy = t->vy + (y - t->ay) * 256 / t->zoom;
	if (t->wspace == WSK_TORUS) {               /* step 2 rule 3 */
		*wx = wsk__wrap(*wx, t->ww);
		*wy = wsk__wrap(*wy, t->wh);
	}
	return WSK_OK;
}
#endif

#ifndef WSK_NO_WORLD
/* Whether [a, a + an) and [b, b + bn) meet, along one axis of a world --
   modulo `size` on a torus, where b may wrap round to meet a's start. */
static inline int wsk__meets(long long a, long long an, long long b, long long bn, int torus, int size)
{
	if (!torus) return a < b + bn && b < a + an;
	{
		/* both are ints: wrapped in 32 bits, as a module has no 64-bit % */
		long long d = wsk__wrap(wsk__wrap((int)b, size) - wsk__wrap((int)a, size), size);
		return d < an || d + bn > size;
	}
}

/*
 * The items overlapping a rectangle of the world (§5q step 4): place and
 * size times each item's own scale, wrapped on a torus; topmost first, and
 * `skip` left out (-1 for none). How many; the first `max` into `out`.
 * What a collision does is the program's.
 */
static inline int wsk__world_in(const wsk_tree_t *t, int x, int y, int w, int h, int skip,
                                int *out, int max)
{
	int n = 0, torus = (t->wspace == WSK_TORUS);
	if (t->overflow != WSK_WORLD || t->place == 0) return 0;
	for (int q = t->n - 1; q >= 0; q--) {
		int j = wsk__wi(t, q);
		const wsk_place_t *p = &t->place[j];
		long long pw = ((long long)p->w * p->scale) >> 8, ph = ((long long)p->h * p->scale) >> 8;
		if (j == skip) continue;
		if (wsk__meets(x, w, p->x, pw, torus, t->ww) && wsk__meets(y, h, p->y, ph, torus, t->wh)) {
			if (n < max) out[n] = j;
			n++;
		}
	}
	return n;
}

static inline int wsk_world_in(const wsk_tree_t *t, int x, int y, int w, int h, int *out, int max)
{
	return wsk__world_in(t, x, y, w, h, -1, out, max);
}

/* What item i is touching: the same, for its own rectangle, itself left out. */
static inline int wsk_world_overlaps(const wsk_tree_t *t, int i, int *out, int max)
{
	const wsk_place_t *p;
	if (t->overflow != WSK_WORLD || t->place == 0 || i < 0 || i >= t->n) return 0;
	p = &t->place[i];
	return wsk__world_in(t, p->x, p->y, (int)(((long long)p->w * p->scale) >> 8),
	                     (int)(((long long)p->h * p->scale) >> 8), i, out, max);
}

/*
 * Item i to the top of a world's order (step 6): drawn last, hit first.
 * Returns 1 if it moved, 0 if it was on top already or the world has no
 * order to change -- array order belongs to arrays that may be in flash.
 */
static inline int wsk_world_raise(wsk_tree_t *t, int i)
{
	int j = 0;
	if (t->overflow != WSK_WORLD || t->order == 0 || i < 0 || i >= t->n) return 0;
	while (j < t->n && t->order[j] != i) j++;
	if (j >= t->n - 1) return 0;
	for (; j < t->n - 1; j++) t->order[j] = t->order[j + 1];
	t->order[t->n - 1] = (short)i;
	return 1;
}
#endif

#ifndef WSK_NO_NEST
/*
 * A widget by id at any depth, or by path (D19).
 *
 * An id is prime and document-unique, so a bare id is searched for through
 * every stack. A key starting with '/' is a path -- ids from the root,
 * `/toolbar/save` -- and is checked LINK BY LINK: each segment must be a
 * widget directly inside the one before, and a path whose last id exists
 * somewhere else is not found. Saying the path is the point of writing one.
 *
 * `at` gets the tree the widget was found in as LAID OUT -- device, area and
 * viewports filled in -- which is what wsk_rows needs to cut it; `in` gets
 * the caller's own tree.
 */
static inline int wsk__seg(const char *id, const char *p)
{
	int k = 0;
	while (id[k] != '\0' && id[k] == p[k]) k++;
	return (id[k] == '\0' && (p[k] == '/' || p[k] == '\0')) ? k : 0;
}

static inline int wsk__locate(const wsk_tree_t *t, const wsk_tree_t *orig,
                              const char *key, int path, wsk_tree_t *at,
                              const wsk_tree_t **in, int depth)
{
	for (int i = 0; i < t->n; i++) {
		const wsk_widget_t *w = &t->item[i];
		int k = path ? wsk__seg(w->id, key) : wsk__same(w->id, key);
		if (k && (!path || key[k] == '\0')) {
			if (at != 0) wsk__tcopy(at, t);
			if (in != 0) *in = orig;
			return i;
		}
		if (w->kind == WSK_STACK && depth + 1 < WSK_DEPTH_MAX && (!path || k)) {
			wsk_tree_t c;
			if (wsk__child(t, i, &c)) {
				int f = wsk__locate(&c, &t->nest[w->sub], path ? key + k + 1 : key,
				                    path, at, in, depth + 1);
				if (f >= 0) return f;
			}
		}
	}
	return WSK_E_NOTFOUND;
}

static inline int wsk__lookup(const wsk_tree_t *t, const char *key,
                              wsk_tree_t *at, const wsk_tree_t **in)
{
	int k;
	if (key[0] == '/') return wsk__locate(t, t, key + 1, 1, at, in, 0);
	k = wsk__locate(t, t, key, 0, at, in, 0);
#ifndef WSK_NO_OVER
	/* an id is prime (D19), and an open overlay's widgets have them too */
	if (k < 0 && t->over != WSK_NONE) {
		wsk_tree_t c;
		if (wsk__over_tree(t, &c))
			k = wsk__locate(&c, &t->nest[t->over], key, 0, at, in, 1);
	}
#endif
	return k;
}

static inline int wsk_find_in(const wsk_tree_t *t, const char *key,
                              const wsk_tree_t **in)
{
	return wsk__lookup(t, key, 0, in);
}
#endif

/* ------------------------------------------------------------------ */
/* Rows: what a repaint costs, in the units the setstat wants          */
/* ------------------------------------------------------------------ */

/*
 * The device rows these widgets occupy, snapped outward to whole bands.
 *
 * Snapped, and that is not a detail. The driver snaps the clip outward and
 * clears every band it visits in full, so a document whose backing covers
 * only the widgets' own rows leaves a strip of the device colour at each end
 * -- which is the bug `flick` shipped for months. These are the rows to hand
 * RV9_SVG_SS_ROWS *and* the rows the backing must cover; one number, so the
 * two cannot disagree.
 *
 * A set rather than one id, because a real interface changes more than one
 * widget at a time and the single form cannot be retrofitted once callers
 * have written loops (rv9-answers.md §3.3).
 */
static inline int wsk_rows(const wsk_tree_t *t, const char *const *ids, int n,
                           int *y0, int *y1)
{
	int lo = t->h, hi = 0;

	for (int k = 0; k < n; k++) {
#ifdef WSK_NO_NEST
		int i = wsk_find(t, ids[k]);
		wsk_rect_t r;
		if (i < 0) return WSK_E_NOTFOUND;
		r = wsk_rect(t, i);
		if (!wsk__cut(t, &r)) continue;    /* out of view: nothing to repaint */
#else
		/* At any depth, by id or by path; a stack's rows are its whole
		   rectangle, which is how a branch is repainted (§5f rule 4). */
		wsk_tree_t at;
		int i = wsk__lookup(t, ids[k], &at, 0);
		wsk_rect_t r;
		if (i < 0) return WSK_E_NOTFOUND;
		r = wsk_rect(&at, i);
		if (!wsk__cut(&at, &r)) continue;  /* out of view: nothing to repaint */
#endif
		if (r.y < lo) lo = r.y;
		if (r.y + r.h > hi) hi = r.y + r.h;
	}
	if (hi <= lo) return WSK_E_EMPTY;

	lo = (lo / WSK_BAND) * WSK_BAND;
	hi = wsk__round_band(hi);
	if (lo < 0) lo = 0;
	if (hi > t->h) hi = t->h;

	*y0 = lo;
	*y1 = hi;
	return WSK_OK;
}

#ifndef WSK_NO_SCROLL
/*
 * A long list, a slice at a time (design.md §5e).
 *
 * whisker lays every rectangle out from the top of its stack, so a document
 * costs the square of the list, and a rectangle is four shorts, so a stack
 * may not run past row 32767. Neither matters to a pane of a few hundred
 * rows and both end a table of ten thousand. The caller holds the list
 * (D5), so the caller hands whisker only the rows in view -- and for rows
 * of one size that choice is arithmetic, which is this.
 *
 * `t` is the pane: area, pad, gap, overflow = WSK_SCROLL. The list has
 * `total` rows, each exactly `size` along the axis, and `scroll` is the
 * offset into the WHOLE list, 0 .. wsk_slice_max(). On return `*first` is
 * the first row to hand over, t->n how many, and t->scroll the offset into
 * the slice; the caller points t->item at those rows. The document is then
 * byte for byte the one the whole list would have drawn, which is how it is
 * tested -- this is a shortcut to the same picture, never a second layout.
 */
static inline int wsk_slice_max(const wsk_tree_t *t, int total, int size)
{
	int over = 2 * t->pad + total * (size + t->gap) - t->gap
	         - ((t->axis == WSK_COL) ? t->ah : t->aw);
	return (total > 0 && over > 0) ? over : 0;
}

static inline int wsk_slice(wsk_tree_t *t, int total, int size, int scroll,
                            int *first)
{
	int pitch = size + t->gap;
	int pane = (t->axis == WSK_COL) ? t->ah : t->aw;
	int f, last;

	if (scroll < 0 || scroll > wsk_slice_max(t, total, size) || pitch <= 0)
		return WSK_E_SCROLL;
	if (total <= 0) {                       /* an empty list is an empty pane */
		*first = 0;
		t->n = 0;
		t->scroll = 0;
		return WSK_OK;
	}
	f = scroll / pitch;                     /* 32-bit: a module may */
	last = (scroll + pane) / pitch + 1;     /* not divide 64 */
	if (last > total - 1) last = total - 1;
	if (f > last) f = last;
	*first = f;
	t->n = last - f + 1;
	t->scroll = (short)(scroll - f * pitch);
	return WSK_OK;
}
#endif

/*
 * The device rows of a stack's whole area, band-snapped: what a scroll step
 * repaints, since every visible widget moves. A composite repaint whenever
 * another stack shares those bands -- §5c's machinery, unchanged.
 */
static inline int wsk_area_rows(const wsk_tree_t *t, int *y0, int *y1)
{
	int lo = (t->ay / WSK_BAND) * WSK_BAND;
	int hi = wsk__round_band(t->ay + t->ah);

	if (lo < 0) lo = 0;
	if (hi > t->h) hi = t->h;
	if (hi <= lo) return WSK_E_EMPTY;
	*y0 = lo;
	*y1 = hi;
	return WSK_OK;
}

/* ------------------------------------------------------------------ */
/* Render                                                              */
/* ------------------------------------------------------------------ */

/* A filled rectangle, optionally named. x and y are omitted when zero, which
   is most backings and every widget in a column. */
static inline void wsk__box(wsk_sink_t *s, const char *id, wsk_rect_t r,
                            unsigned int col)
{
	wsk__s(s, "<rect");
	if (id != 0) { wsk__s(s, " id=\""); wsk__s(s, id); wsk__s(s, "\""); }
	if (r.x != 0) { wsk__s(s, " x=\""); wsk__n(s, r.x); wsk__s(s, "\""); }
	if (r.y != 0) { wsk__s(s, " y=\""); wsk__n(s, r.y); wsk__s(s, "\""); }
	wsk__s(s, " width=\""); wsk__n(s, r.w);
	wsk__s(s, "\" height=\""); wsk__n(s, r.h);
	wsk__s(s, "\" fill=\"");
	wsk__col(s, col);
	wsk__s(s, "\"/>");
}

static inline void wsk__rect(wsk_sink_t *s, const wsk_tree_t *t,
                             const wsk_widget_t *w, wsk_rect_t r)
{
	wsk__box(s, (w != 0 && t->ids) ? w->id : 0, r,
	         (w->fill != WSK_INHERIT) ? wsk__fade(t, w->fill)
#ifndef WSK_NO_SPLIT
	         : (w->kind == WSK_DIVIDER) ? t->trough
#endif
#ifndef WSK_NO_FIELD
	         : (w->kind == WSK_FIELD
#ifndef WSK_NO_MEMO
	            || wsk__lined(w->kind)
#endif
	           ) ? t->trough
#endif
	         : t->accent);
}

#ifndef WSK_NO_MARKS
/*
 * Ticks driven inward from the four corners.
 *
 * One path, four subpaths, and no `fill="none"`: a two-point contour has no
 * area, so the <g>'s fill draws nothing and the attribute would be twelve
 * bytes to say so. The arm is a quarter of the widget's height, capped, so
 * the mark keeps its proportions on a seven-inch panel and its presence on a
 * small one; below three pixels it is not drawn, because a mark nobody can
 * see is bytes re-parsed once per band for nothing.
 */
static inline void wsk__corner(wsk_sink_t *s, int x, int y, int dx, int dy)
{
	wsk__s(s, "M");
	wsk__n(s, x);
	wsk__s(s, " ");
	wsk__n(s, y);
	wsk__s(s, "l");
	wsk__n(s, dx);
	wsk__s(s, " ");
	wsk__n(s, dy);
}

static inline void wsk__marks(wsk_sink_t *s, const wsk_tree_t *t,
                              const wsk_widget_t *w, wsk_rect_t r)
{
	int a = r.h / 4;

	if (a > 16) a = 16;
	if (a < 3) return;

	wsk__s(s, "<path d=\"");
	wsk__corner(s, r.x,        r.y,        a,  a);
	wsk__corner(s, r.x + r.w,  r.y,       -a,  a);
	wsk__corner(s, r.x,        r.y + r.h,  a, -a);
	wsk__corner(s, r.x + r.w,  r.y + r.h, -a, -a);
	wsk__s(s, "\" stroke=\"");
	wsk__col(s, (w->ink != WSK_INHERIT) ? w->ink : t->ink);
	wsk__s(s, "\" stroke-width=\"2\"/>");
}

/*
 * A tree's arrow (§5t), in the cell of one font size that starts at x: ▸
 * for a closed folder, ▾ for an open one -- a filled triangle half the
 * font high, in the widget's ink, one <path> of three points.
 */
static inline void wsk__arrow(wsk_sink_t *s, const wsk_tree_t *t,
                              int mark, unsigned int ink, int x, int top)
{
	int a = t->font / 2, bx = x + (t->font - a) / 2, by = top + (t->font - a) / 2;
	int px[3], py[3];

	if (mark == WSK_MARK_SHUT) {
		px[0] = bx + a / 8; py[0] = by;
		px[1] = bx + a / 8; py[1] = by + a;
		px[2] = bx + a;     py[2] = by + a / 2;
	} else if (mark == WSK_MARK_OPEN) {
		px[0] = bx;         py[0] = by + a / 8;
		px[1] = bx + a;     py[1] = by + a / 8;
		px[2] = bx + a / 2; py[2] = by + a;
	} else
		return;                      /* a leaf: the room, and nothing in it */
	wsk__s(s, "<path d=\"M");
	for (int k = 0; k < 3; k++) {
		if (k > 0) wsk__s(s, "L");
		wsk__n(s, px[k]);
		wsk__s(s, " ");
		wsk__n(s, py[k]);
	}
	wsk__s(s, "Z\"");
	if (ink != WSK_INHERIT) {
		wsk__s(s, " fill=\"");
		wsk__col(s, ink);
		wsk__s(s, "\"");
	}
	wsk__s(s, "/>");
}
#endif /* WSK_NO_MARKS */

/*
 * Every <text> whisker writes -- a label's, a field's, a check's, a run of
 * words -- is this element: at x on a baseline, anchored at its start or at
 * the <g>'s middle, in its own ink only when it differs from the <g>'s (a
 * pressed button needs that: white on a pale fill is legible in a test and
 * not on the glass, which is what the picture showed). One place, so the
 * four cannot drift apart, and one copy in a binary rather than four.
 */
static inline void wsk__textel(wsk_sink_t *s, int start, int x, int base,
                               unsigned int ink, const char *b, int n)
{
	wsk__s(s, start ? "<text text-anchor=\"start\" x=\"" : "<text x=\"");
	wsk__n(s, x);
	wsk__s(s, "\" y=\"");
	wsk__n(s, base);
	if (ink != WSK_INHERIT) {
		wsk__s(s, "\" fill=\"");
		wsk__col(s, ink);
	}
	wsk__s(s, "\">");
#ifdef WSK_NO_ENTITY
	wsk__w(s, b, n);
#else
	/* < & > reach here only from a host that decodes entities -- the checks
	   refuse them elsewhere -- so they are escaped whenever they come, and
	   text without them is written exactly as it always was */
	for (int i = 0, from = 0; i <= n; i++) {
		const char *e = (i == n) ? 0 : (b[i] == '<') ? "&lt;" : (b[i] == '>') ? "&gt;"
		              : (b[i] == '&') ? "&amp;" : 0;
		if (i < n && e == 0) continue;
		if (i > from) wsk__w(s, b + from, i - from);
		if (e != 0) wsk__s(s, e);
		from = i + 1;
	}
#endif
	wsk__s(s, "</text>");
}

/*
 * Whether a line of text from x0 to x1 with its cell at `top` may be drawn:
 * whole along the stack's axis (a scroll cuts a box, not a glyph -- §5e) and
 * inside every viewport above it (§5f). Labels, fields, checks and runs all
 * ask exactly this.
 */
static inline int wsk__shows(const wsk_tree_t *t, int x0, int x1, int top)
{
#ifndef WSK_NO_SCROLL
	/* A host that clips draws it all and lets the clip decide (§5r): a label
	   half out of a scrolled pane slides, rather than vanishing whole. */
	if (t->caps & WSK_CAP_CLIP) return 1;
#endif
#ifndef WSK_NO_WORLD
	/* in a world, whole within the view both ways (§5q rule 7) */
	if (t->overflow == WSK_WORLD &&
	    (x0 < t->ax || x1 > t->ax + t->aw || top < t->ay || top + t->font > t->ay + t->ah))
		return 0;
#endif
	if (t->axis == WSK_COL ? !wsk__whole(t, top, top + t->font)
	                       : !wsk__whole(t, x0, x1)) return 0;
#ifndef WSK_NO_NEST
	if (!wsk__seen(t, x0, top, x1, top + t->font)) return 0;
#endif
	return 1;
}

/*
 * A label, centred in its widget, truncated to what fits.
 *
 * Truncated because there is no clipPath in the subset, so an overlong label
 * runs out of its button and over its neighbour rather than being cut off.
 * text-anchor does the centring, against the same advance box §2.1 gives --
 * one attribute instead of a computed x, and fewer bytes.
 */
static inline void wsk__text(wsk_sink_t *s, const wsk_tree_t *t,
                             const wsk_widget_t *w, const char *txt, wsk_rect_t r)
{
	int bytes = wsk__len(txt), chars = wsk__chars(txt, bytes);
	int start = (t->talign == WSK_ALIGN_START);
	/* All of the width: a label exactly its text's width -- every widget in
	   a row with no padding -- draws all of it. It wanted two pixels of
	   slack until 2026-11-11, which cut "OK" to "O" (design.md §5f). Text
	   at the start of a cell keeps the padding at both ends (§5n). */
	int room = (start ? r.w - 2 * t->pad : r.w) / wsk_advance(t->font);
	int top = r.y + (r.h - t->font) / 2, g = 0;

#ifndef WSK_NO_MARKS
	/* a tree's mark takes a gutter of one font size, two characters (§5t) */
	if (w->mark >= WSK_MARK_SHUT) { g = t->font; room -= 2; }
#endif
	if (room < 1) return;
	if (chars > room) chars = room;
	if (chars == 0) return;
	/* the text's own cell -- and its mark's -- which is all a scroll can cut */
	{
		int tw = wsk_text_w(t->font, chars);
		int x0 = start ? r.x + t->pad : r.x + r.w / 2 - (tw + g) / 2;
		if (!wsk__shows(t, x0, x0 + g + tw, top)) return;
#ifndef WSK_NO_MARKS
		if (g > 0) {
			wsk__arrow(s, t, w->mark, wsk__fade(t, w->ink), x0, top);
			wsk__textel(s, 1, x0 + g, wsk_baseline_of(top, t->font), wsk__fade(t, w->ink), txt,
			            wsk__byte_of(txt, bytes, chars));
			return;
		}
#endif
		/* truncated by characters, and so never through one (§5r) */
		wsk__textel(s, start, start ? x0 : r.x + r.w / 2,
		            wsk_baseline_of(top, t->font), wsk__fade(t, w->ink), txt,
		            wsk__byte_of(txt, bytes, chars));
	}
}



#ifndef WSK_NO_FIELD
/*
 * How many characters of a field are visible, and which ones.
 *
 * The TAIL, not the head -- the opposite of a label. A label that overflows
 * is truncated because the part you can read is the part that identifies it;
 * a field that did the same would hide the characters just typed, which are
 * the only ones anybody is looking at. So the window slides, and `from` is
 * where it starts.
 */
/*
 * The buffer a field edits, or 0 for §5d's read-only field (§5j).
 */
static inline wsk_buf_t *wsk__fbuf(const wsk_tree_t *t, const wsk_widget_t *w)
{
	if ((w->kind != WSK_FIELD
#ifndef WSK_NO_MEMO
	     && !wsk__lined(w->kind)
#endif
	    ) || t->buf == 0 || w->sub < 0 || w->sub >= t->bufs)
		return 0;
	return &t->buf[w->sub];
}

/*
 * Which characters show, given where the caret is: all of them when they
 * fit; otherwise a window ending at the caret, so the caret is always in
 * sight -- at the right edge when the text is longer than the field. With
 * the caret at the end that is the tail, which is all §5d's field ever was,
 * so the read-only field draws byte for byte as it did.
 */
static inline int wsk__view(const wsk_tree_t *t, wsk_rect_t r, int len, int caret,
                            int *from)
{
	int room = (r.w - 2 * WSK_FIELD_PAD - WSK_CARET_W) / wsk_advance(t->font);
	int f;

	if (room < 0) room = 0;
	f = caret - room;
	if (f > len - room) f = len - room;
	if (f < 0) f = 0;
	*from = f;
	return (len - f < room) ? len - f : room;
}

#ifndef WSK_NO_TEXTSEL
/*
 * A buffer's selection as start and end, clamped to its text -- a program
 * may have written the text since -- and its length: 0 when there is none.
 */
static inline int wsk__selr(const wsk_buf_t *b, int *s0, int *s1)
{
	int a = b->caret, z = b->caret + b->span;
	if (z < a) { int t = a; a = z; z = t; }
	if (a < 0) a = 0;
	if (z > b->len) z = b->len;
	*s0 = a;
	*s1 = z;
	return (z > a) ? z - a : 0;
}

/*
 * The part of a selection on one run of characters -- txt[from .. from+n),
 * drawn from x -- as a box of the tree's selfill behind them (§5s rule 4).
 * Only in the focused field, so a buffer written without clearing its span
 * is never drawn wrong anywhere else.
 */
static inline void wsk__selbox(wsk_sink_t *s, const wsk_tree_t *t, const wsk_widget_t *w,
                               const wsk_buf_t *b, int from, int n, int x, int top)
{
	int s0, s1, adv = wsk_advance(t->font);
	wsk_rect_t r;
	if (b == 0 || (const void *)w != t->focus || !wsk__selr(b, &s0, &s1)) return;
	if (s0 < from) s0 = from;
	if (s1 > from + n) s1 = from + n;
	if (s1 <= s0) return;
	/* bytes to columns: characters, for a host that draws UTF-8 */
	r.x = (short)(x + wsk__chars(b->b + from, s0 - from) * adv);
	r.y = (short)top;
	r.w = (short)(wsk__chars(b->b + s0, s1 - s0) * adv);
	r.h = t->font;
	wsk__box(s, 0, r, t->selfill);
}
#endif

static inline int wsk_field_view(const wsk_tree_t *t, const wsk_widget_t *w,
                                 wsk_rect_t r, int *from)
{
	int chars = wsk__len(w->text);
	return wsk__view(t, r, chars, chars, from);
}

/*
 * A field's text, left-aligned, and the caret where the next character goes.
 *
 * text-anchor="start" is spelled here rather than on the <g>, because the <g>
 * centres everything else and a field is the only thing that must not move
 * sideways as it grows. The caret is a filled box two pixels wide -- there is
 * no stroke-dasharray, no animation and no blink in the subset (§2.4), and a
 * blink would cost a document per frame anyway.
 */
static inline void wsk__field(wsk_sink_t *s, const wsk_tree_t *t,
                              const wsk_widget_t *w, wsk_rect_t r)
{
	const wsk_buf_t *b = wsk__fbuf(t, w);
	const char *txt = (b != 0) ? b->b : w->text;
	int len = (b != 0) ? b->len : wsk__len(w->text);
	int caret = (b != 0) ? b->caret : len;
	/* the view is in characters, the text in bytes (§5v step 3) */
	int col = wsk__chars(txt, caret);
	int from, n = wsk__view(t, r, wsk__chars(txt, len), col, &from);
	int x = r.x + WSK_FIELD_PAD;
	int show = (w->mark == WSK_MARK_CARET);
	int top = r.y + (r.h - t->font) / 2;
	unsigned int ink = (w->ink != WSK_INHERIT) ? w->ink : t->ink;

	/* the text and its caret, whole or not at all (§5e) */
	if (!wsk__shows(t, r.x, r.x + r.w, top)) return;
	caret = col - from;              /* the caret's column in the view */
	from = wsk__byte_of(txt, len, from);
	n = wsk__byte_of(txt + from, len - from, n);
#ifndef WSK_NO_TEXTSEL
	wsk__selbox(s, t, w, b, from, n, x, top);
#endif
	if (n > 0)
		wsk__textel(s, 1, x, wsk_baseline_of(top, t->font), w->ink, txt + from, n);

#ifndef WSK_NO_INPUT
	if ((const void *)w == t->focus) show = 1;
#endif
	if (show) {
		wsk_rect_t c;
		c.x = (short)(x + caret * wsk_advance(t->font));
		c.y = (short)(top + t->font / 8);
		c.w = WSK_CARET_W;
		c.h = (short)(t->font - t->font / 4);
		if (c.h < 4) c.h = 4;
		if (c.x + c.w > r.x + r.w) c.x = (short)(r.x + r.w - c.w);
		wsk__box(s, 0, c, ink);
	}
}
#endif /* WSK_NO_FIELD */

#ifndef WSK_NO_MEMO
/* ------------------------------------------------------------------ */
/* The memo: many lines (design.md §5o)                                 */
/* ------------------------------------------------------------------ */

/* Lines are a font and a quarter apart. */
static inline int wsk__pitch(const wsk_tree_t *t) { return t->font + t->font / 4; }

/* How many lines a memo's rectangle shows: at least one. */
static inline int wsk__vis(const wsk_tree_t *t, wsk_rect_t r)
{
	int v = (r.h - 2 * WSK_FIELD_PAD) / wsk__pitch(t);
	return (v < 1) ? 1 : v;
}

/*
 * The characters a memo's row holds: `room` for a memo that wraps (§5u),
 * and for one that does not, so many that a row is a whole line as typed.
 */
#define WSK_NOWRAP 32767
static inline int wsk__mroom(const wsk_tree_t *t, const wsk_widget_t *w, wsk_rect_t r,
                              const wsk_buf_t *b)
{
	int room = (r.w - 2 * WSK_FIELD_PAD - WSK_CARET_W) / wsk_advance(t->font);
	if (b == 0 || !b->wrap || w->kind != WSK_MEMO) return WSK_NOWRAP;   /* source never wraps */
	return (room < 1) ? 1 : room;
}

/*
 * The row that starts at `at`: where it ends (*end -- a line break, or the
 * space it broke at, is not in it) and where the next starts, len + 1 after
 * the last. A line longer than `room` breaks after its last space that
 * fits, or, with none, at the room itself (§5u rule 2).
 */
static inline int wsk__vrow(const char *b, int len, int at, int room, int *end)
{
	int e = at;
	while (e < len && b[e] != '\n') e++;
	/* room is characters; a line is never more of them than its bytes */
	if (e - at > room && wsk__chars(b + at, e - at) > room) {
		int cut = at + wsk__byte_of(b + at, e - at, room);
		int sp = cut;                    /* a space here may still break */
		while (sp > at && b[sp] != ' ') sp--;
		if (sp > at) { *end = sp; return sp + 1; }
		*end = cut;
		return cut;
	}
	*end = e;
	return (e < len) ? e + 1 : len + 1;
}

/* The row and column at offset `at`, and how many rows there are. */
static inline void wsk__where(const char *b, int len, int at, int room,
                              int *line, int *col, int *lines)
{
	int k = 0, st = 0, end, next, found = 0;
	for (;;) {
		next = wsk__vrow(b, len, st, room, &end);
		if (!found && (at < next || next > len)) {
			*line = k; *col = wsk__chars(b + st, at - st); found = 1;   /* in characters */
		}
		if (next > len) break;
		st = next;
		k++;
	}
	*lines = k + 1;
}

/* Where row `k` starts, and (in *end) where it stops. */
static inline int wsk__line(const char *b, int len, int k, int room, int *end)
{
	int st = 0, next;
	for (int i = 0; ; i++) {
		next = wsk__vrow(b, len, st, room, end);
		if (i == k || next > len) return st;
		st = next;
	}
}

/*
 * The first line shown: the buffer's `top`, moved only as far as keeps the
 * caret in sight and the view inside the text (§5o rule 2). Asked of by
 * drawing, which never writes it, and by the toolkit, which does.
 */
static inline int wsk__top(const wsk_buf_t *b, int vis, int line, int lines)
{
	int t = b->top;
	if (line < t) t = line;
	if (line >= t + vis) t = line - vis + 1;
	if (t > lines - vis) t = lines - vis;
	if (t < 0) t = 0;
	return t;
}

/* The first column shown: as a field's, so the caret's column is in sight. */
static inline int wsk__left(const wsk_tree_t *t, wsk_rect_t r, int col)
{
	int room = (r.w - 2 * WSK_FIELD_PAD - WSK_CARET_W) / wsk_advance(t->font);
	if (room < 1) room = 1;
	return (col > room) ? col - room : 0;
}

#ifndef WSK_NO_SOURCE
/*
 * A source widget's gutter: room for its last line's number, two digits at
 * least, and one character between it and the text -- so it widens only
 * when a file passes 99 lines, 999, and so on.
 */
static inline int wsk__gutter(const wsk_tree_t *t, const wsk_buf_t *b, const char *txt, int len)
{
	int lines = 1, d = 1;
	for (int i = 0; i < len; i++) if (txt[i] == '\n') lines++;
	for (int v = lines; v >= 10; v /= 10) d++;
	if (d < 2) d = 2;
	/* and a column for marks, one font wide, whenever the program has
	   given it marks -- even none, so the first does not move the text */
	return (d + 1) * wsk_advance(t->font) + ((b != 0 && b->marks != 0) ? t->font : 0);
}

/* A line's mark, in the gutter's mark column from x (step 4). */
static inline void wsk__gmark(wsk_sink_t *s, const wsk_tree_t *t, const wsk_gmark_t *m,
                              int x, int y, int gut)
{
	wsk_rect_t r;
	int a = t->font / 2, p = wsk__pitch(t);
	if (m->shape == WSK_GM_BAR) {        /* the whole cell, the rows touching */
		r.x = (short)x; r.y = (short)(y - (p - t->font) / 2); r.w = (short)gut; r.h = (short)p;
	} else if (m->shape == WSK_GM_DOT) {
		r.x = (short)(x + (t->font - a) / 2); r.y = (short)(y + (t->font - a) / 2);
		r.w = (short)a; r.h = (short)a;
	} else {
#ifndef WSK_NO_MARKS
		wsk__arrow(s, t, WSK_MARK_SHUT, m->ink, x, y);
#endif
		return;
	}
	wsk__box(s, 0, r, m->ink);
}

/* A line's number, its last digit ending at xr. */
static inline void wsk__lineno(wsk_sink_t *s, const wsk_tree_t *t, int num, int xr, int y)
{
	char d[12];
	int n = 0;
	for (int v = num; v > 0 || n == 0; v /= 10) n++;
	for (int k = n - 1, v = num; k >= 0; k--, v /= 10) d[k] = (char)('0' + v % 10);
	wsk__textel(s, 1, xr - n * wsk_advance(t->font), wsk_baseline_of(y, t->font),
	            WSK_INHERIT, d, n);
}

/*
 * Characters from..from+n of a row, in the program's colour runs (§5v rule
 * 4): each run a <text> of its own at its column -- monospace makes the
 * column the position, and every substrate draws a <text>. The gaps between
 * runs are the widget's ink; spaces that open a piece are not sent, since
 * its x already says where it starts.
 */
static inline void wsk__inked(wsk_sink_t *s, const wsk_tree_t *t, const wsk_widget_t *w,
                              const wsk_buf_t *b, const char *txt, int from, int n,
                              int x, int y)
{
	const wsk_ink_t *ik = (b != 0) ? b->inks : 0;
	int ni = (b != 0 && ik != 0) ? b->ninks : 0;
	int adv = wsk_advance(t->font), base = wsk_baseline_of(y, t->font);
	int pos = from, stop = from + n, lo = 0, hi = ni;

	/* the first run that ends after `from` */
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if (ik[mid].at + ik[mid].len <= from) lo = mid + 1; else hi = mid;
	}
	while (pos < stop) {
		unsigned int ink = w->ink;
		int e, p;
		if (lo < ni && ik[lo].at <= pos) { e = ik[lo].at + ik[lo].len; ink = ik[lo].ink; lo++; }
		else e = (lo < ni) ? ik[lo].at : stop;
		if (e > stop) e = stop;
		for (p = pos; p < e && txt[p] == ' '; p++) ;
		if (p < e) wsk__textel(s, 1, x + wsk__chars(txt + from, p - from) * adv, base, ink,
		                       txt + p, e - p);
		if (e > pos) pos = e;
	}
}
#endif

/*
 * A memo's lines, each from the same column, and its caret. Every line is
 * whole or not drawn (§5e), as a field's one line is.
 */
/*
 * ry0..ry1 are the document's rows: a line outside them is walked past and
 * not drawn. Before 2026-11-16 every visible line was drawn into every
 * band, so a code editor's 8-row repaint carried all its text -- 6.5 KB,
 * over what RV-9's window takes (found by the second review's fit.h fix).
 */
static inline void wsk__memo(wsk_sink_t *s, const wsk_tree_t *t,
                             const wsk_widget_t *w, wsk_rect_t r, int ry0, int ry1)
{
	const wsk_buf_t *b = wsk__fbuf(t, w);
	const char *txt = (b != 0) ? b->b : w->text;
	int len = (b != 0) ? b->len : wsk__len(w->text);
	int caret = (b != 0) ? b->caret : len;
	int adv = wsk_advance(t->font), pitch = wsk__pitch(t), vis = wsk__vis(t, r);
	int room, line = 0, col = 0, lines = 1, top, left, at, end;
	int show = (w->mark == WSK_MARK_CARET);
	int wr = wsk__mroom(t, w, r, b), gut = 0, mk = 0;
	wsk_rect_t tr = r;               /* where the text goes: past a gutter */
	wsk_buf_t none;

#ifndef WSK_NO_SOURCE
	if (w->kind == WSK_SOURCE) {
		wsk_rect_t g = r;
		gut = wsk__gutter(t, b, txt, len);
		tr.x = (short)(r.x + gut);
		tr.w = (short)(r.w - gut);
		g.w = (short)gut;
		if (wsk__cut(t, &g)) wsk__box(s, 0, g, t->bg);
	}
#endif
	(void)gut; (void)mk;
	room = (tr.w - 2 * WSK_FIELD_PAD - WSK_CARET_W) / adv;
	if (room < 1) room = 1;
	wsk__where(txt, len, caret, wr, &line, &col, &lines);
	none.top = 0;
	top = wsk__top((b != 0) ? b : &none, vis, line, lines);
	left = wsk__left(t, tr, col);
	/* the first row shown, found once; each after it from the one before --
	   a 300 KB buffer is walked once a picture, not once a row (§5v) */
	at = wsk__line(txt, len, top, wr, &end);
#ifndef WSK_NO_SOURCE
	/* the first mark at or after the first line shown */
	if (gut > 0 && b != 0 && b->marks != 0) {
		int hi = b->nmarks;
		while (mk < hi) {
			int mid = (mk + hi) / 2;
			if (b->marks[mid].line < top) mk = mid + 1; else hi = mid;
		}
	}
#endif
	for (int k = top; k < top + vis && k < lines; k++) {
		int y = r.y + WSK_FIELD_PAD + (k - top) * pitch, next = wsk__vrow(txt, len, at, wr, &end);
		/* columns are characters: `left` and `room` count them (§5v step 3) */
		int from = at + wsk__byte_of(txt + at, end - at, left), n = end - from;
		if (y + pitch > ry0 && y < ry1 && wsk__shows(t, r.x, r.x + r.w, y)) {
			n = wsk__byte_of(txt + from, n, room);
#ifndef WSK_NO_SOURCE
			const wsk_gmark_t *m = 0;
			if (gut > 0 && b != 0 && b->marks != 0) {
				while (mk < b->nmarks && b->marks[mk].line < k) mk++;
				if (mk < b->nmarks && b->marks[mk].line == k) m = &b->marks[mk];
			}
			if (m != 0 && m->shape == WSK_GM_BAR) wsk__gmark(s, t, m, r.x, y, gut);
			if (gut > 0) wsk__lineno(s, t, k + 1, r.x + gut - adv / 2, y);
			if (m != 0 && m->shape != WSK_GM_BAR) wsk__gmark(s, t, m, r.x, y, gut);
#endif
#ifndef WSK_NO_TEXTSEL
			if (n > 0) wsk__selbox(s, t, w, b, from, n, tr.x + WSK_FIELD_PAD, y);
#endif
#ifndef WSK_NO_SOURCE
			if (n > 0 && gut > 0)
				wsk__inked(s, t, w, b, txt, from, n, tr.x + WSK_FIELD_PAD, y);
			else
#endif
			if (n > 0)
				wsk__textel(s, 1, tr.x + WSK_FIELD_PAD, wsk_baseline_of(y, t->font),
				            w->ink, txt + from, n);
		}
		if (next > len) break;
		at = next;
	}
#ifndef WSK_NO_INPUT
	if ((const void *)w == t->focus) show = 1;
#endif
	if (show && line >= top && line < top + vis) {
		wsk_rect_t c;
		int y = r.y + WSK_FIELD_PAD + (line - top) * pitch;
		if (wsk__shows(t, r.x, r.x + r.w, y)) {
			c.x = (short)(tr.x + WSK_FIELD_PAD + (col - left) * adv);
			c.y = (short)(y + t->font / 8);
			c.w = WSK_CARET_W;
			c.h = (short)(t->font - t->font / 4);
			if (c.h < 4) c.h = 4;
			wsk__box(s, 0, c, (w->ink != WSK_INHERIT) ? w->ink : t->ink);
		}
	}
}
#endif /* WSK_NO_MEMO */

/* ------------------------------------------------------------------ */
/* The document: one device, one background, any number of stacks      */
/* ------------------------------------------------------------------ */

/*
 * What a stack is drawn INTO.
 *
 * `y0`/`y1` bound what is painted: the whole screen when they are 0 and h, a
 * clipped repaint otherwise. Either way it is a COMPLETE document with the
 * same viewBox -- there is no such thing as a fragment here -- and it carries
 * the backing for its own rows, because a band is cleared to the device
 * colour before anything is drawn into it.
 *
 * The viewBox is the device, deliberately: user units are then device rows
 * and wsk_rows needs no conversion. A whisker that assumed that instead of
 * arranging it works at 320x172 and clips the wrong strip at 1024x600.
 *
 * THE BACKGROUND BELONGS HERE AND NOWHERE ELSE. That is the whole of what
 * makes two stacks able to share a screen (design.md §5c): a stack
 * contributes widgets and no backing, so a second one cannot erase the
 * first, and the question of whose background a clipped repaint carries has
 * one answer instead of one per stack.
 */
typedef struct {
	short        w, h;           /* the device, and the viewBox */
	short        y0, y1;         /* the device rows this document paints */
	unsigned int bg;
	unsigned int ink;            /* the shared <g>: text, and marks */
	short        font;           /* the shared <g>: cell height in pixels */
} wsk_doc_t;

/*
 * Take the device and the defaults from a stack -- which is a copy and not a
 * relationship. The document does not belong to that tree and the tree is
 * not privileged by having been asked; any of a composite's stacks will do,
 * and the caller is free to overwrite any field afterwards.
 */
static inline void wsk_doc_init(wsk_doc_t *d, const wsk_tree_t *t)
{
	d->w = t->w;
	d->h = t->h;
	d->y0 = 0;
	d->y1 = t->h;
	d->bg = t->bg;
	d->ink = t->ink;
	d->font = t->font;
}

/*
 * Narrow the document to the rows those widgets occupy, band-snapped, so the
 * same range can go to RV9_SVG_SS_ROWS. Replaces whatever range the document
 * had; wsk_doc_union widens it, which is how a repaint spanning two stacks is
 * built -- rows from the first, union with each of the rest.
 */
static inline int wsk_doc_rows(wsk_doc_t *d, const wsk_tree_t *t,
                               const char *const *ids, int n)
{
	int y0, y1, e = wsk_rows(t, ids, n, &y0, &y1);
	if (e != WSK_OK) return e;
	d->y0 = (short)y0;
	d->y1 = (short)y1;
	return WSK_OK;
}

static inline int wsk_doc_union(wsk_doc_t *d, const wsk_tree_t *t,
                                const char *const *ids, int n)
{
	int y0, y1, e = wsk_rows(t, ids, n, &y0, &y1);
	if (e != WSK_OK) return e;
	if (y0 < d->y0) d->y0 = (short)y0;
	if (y1 > d->y1) d->y1 = (short)y1;
	return WSK_OK;
}

/* <svg>, the backing for this document's rows, and the shared <g>.
 *
 * font-size, anchor and ink go on one <g>: measured at 44 % more interface
 * under SRC_MAX than spelling them on every element, and re-parsed once per
 * band either way. */
static inline void wsk_open(const wsk_doc_t *d, wsk_sink_t *s)
{
	wsk_rect_t back;

	wsk__s(s, "<svg viewBox=\"0 0 ");
	wsk__n(s, d->w);
	wsk__s(s, " ");
	wsk__n(s, d->h);
	wsk__s(s, "\">");

	back.x = 0;
	back.y = d->y0;
	back.w = d->w;
	back.h = (short)(d->y1 - d->y0);
	wsk__box(s, 0, back, d->bg);

	wsk__s(s, "<g font-size=\"");
	wsk__n(s, d->font);
	wsk__s(s, "\" text-anchor=\"middle\" fill=\"");
	wsk__col(s, d->ink);
	wsk__s(s, "\">");
}

#if !defined(WSK_NO_TEXT) && !defined(WSK_NO_WRAP)
/*
 * A text item's words in a wrapping row (§5h rule 5), as RUNS: words of
 * this item on one line, one space apart in the string, while `gap` is one
 * character's advance, are exactly where the window would put the same
 * characters written together -- so they are one <text>, at the first word,
 * anchored at its start. Anything else is a word at a time. Same characters,
 * same places, far fewer bytes: about forty a word saved, which is the
 * difference between a page of prose fitting SRC_MAX and not.
 *
 * Whole or not at all (D12): a run is drawn only where it lies wholly in
 * view. A run is one line, so a scroll cuts it as it would a label.
 */
static inline void wsk__run(const wsk_doc_t *d, const wsk_tree_t *t,
                            const wsk_widget_t *w, const char *x, int from,
                            int to, wsk_rect_t a, int x1, wsk_sink_t *s)
{
	int top = a.y + (a.h - t->font) / 2;

	if (a.y >= d->y1 || a.y + a.h <= d->y0) return;   /* not in these rows */
	if (!wsk__shows(t, a.x, x1, top)) return;
	wsk__textel(s, 1, a.x, wsk_baseline_of(top, t->font), w->ink, x + from, to - from);
}

static inline void wsk__runs(const wsk_doc_t *d, const wsk_tree_t *t, int i,
                             wsk_sink_t *s)
{
	const wsk_widget_t *w = &t->item[i];
	const char *x = wsk__str_of(t, t, i);
	int adv = wsk_advance(t->font), more;
	wsk__u_t u;

	more = wsk__u_at(t, t, i, &u);
	while (more && u.k == i) {
		wsk_rect_t a = wsk__wrap_unit(t, i, u.wi);
		int from = u.off, to = u.off + u.len, x1 = a.x + a.w;
		for (;;) {
			wsk__u_t nx = u;
			wsk_rect_t b;
			more = wsk__u_next(t, t, &nx);
			u = nx;
			if (!more || nx.k != i) break;
			b = wsk__wrap_unit(t, i, nx.wi);
			if (t->gap != adv || b.y != a.y || nx.off != to + 1 || b.x != x1 + t->gap)
				break;                       /* a new run starts here */
			to = nx.off + nx.len;
			x1 = b.x + b.w;
		}
		wsk__run(d, t, w, x, from, to, a, x1, s);
	}
}
#endif

#ifndef WSK_NO_SCROLL
/*
 * A scrolling stack, in a host that clips (§5r): its widgets inside a <g>
 * clipped to its area, so they are drawn whole and the clip cuts them --
 * what /w0 cannot do, and why whisker cut by arithmetic until a host said it
 * could. The id is the area's corner, which no two scrolling stacks share.
 * Returns whether it opened one.
 */
static inline int wsk__clip_open(wsk_sink_t *s, const wsk_tree_t *t)
{
	wsk_rect_t a;
	if (!(t->caps & WSK_CAP_CLIP) || (t->overflow != WSK_SCROLL
#ifndef WSK_NO_WORLD
	                                  && t->overflow != WSK_WORLD
#endif
	   )) return 0;
	a.x = t->ax; a.y = t->ay; a.w = t->aw; a.h = t->ah;
	wsk__s(s, "<clipPath id=\"wsk-c");
	wsk__n(s, t->ax); wsk__s(s, "-"); wsk__n(s, t->ay);
	wsk__s(s, "\">");
	wsk__box(s, 0, a, 0);
	wsk__s(s, "</clipPath><g clip-path=\"url(#wsk-c");
	wsk__n(s, t->ax); wsk__s(s, "-"); wsk__n(s, t->ay);
	wsk__s(s, ")\">");
	return 1;
}
#endif

#ifndef WSK_NO_NEST
static inline void wsk__items(const wsk_doc_t *d, const wsk_tree_t *t,
                              wsk_sink_t *s);

/*
 * A nested stack, out of line on purpose: its child tree is a copy on the
 * stack, and inside wsk__items that copy would sit in the frame of every
 * stack drawn -- flat ones included, which measured 144 bytes of stack for
 * nothing (tools/evidence/stack). Here only a stack that holds a stack pays,
 * once per level, and the rasteriser runs beneath all of it on /w0.
 */
#if !defined(WSK_NO_LISTKEYS)
static inline void wsk__ring(wsk_sink_t *s, const wsk_tree_t *t,
                             const wsk_widget_t *w, wsk_rect_t r);
#endif
#if defined(__GNUC__)
__attribute__((noinline, unused))
#endif
static void wsk__stack(const wsk_doc_t *d, const wsk_tree_t *t, int i,
                       wsk_rect_t v, wsk_sink_t *s)
{
	const wsk_widget_t *w = &t->item[i];
	wsk_tree_t c;
	int id = (t->ids && w->id[0] != '\0'), font, g;

	if (!wsk__child(t, i, &c)) return;      /* wsk_check refused it */
	font = (c.font != t->font);
	g = id || font || c.ink != t->ink;
	if (g) {
		wsk__s(s, "<g");
		if (id) { wsk__s(s, " id=\""); wsk__s(s, w->id); wsk__s(s, "\""); }
		if (font) { wsk__s(s, " font-size=\""); wsk__n(s, c.font); wsk__s(s, "\""); }
		if (c.ink != t->ink) { wsk__s(s, " fill=\""); wsk__col(s, c.ink); wsk__s(s, "\""); }
		wsk__s(s, ">");
	}
#ifndef WSK_NO_INPUT
	if (t->select && i == t->sel) wsk__box(s, 0, v, t->selfill);   /* a chosen row */
	else
#endif
	if (w->fill != WSK_INHERIT) wsk__box(s, 0, v, wsk__fade(t, w->fill));
#ifndef WSK_NO_SCROLL
	{
		int clip = wsk__clip_open(s, &c);
		wsk__items(d, &c, s);
		if (clip) wsk__s(s, "</g>");
	}
#else
	wsk__items(d, &c, s);
#endif
#if !defined(WSK_NO_LISTKEYS)
	/* a list or a world with focus wears focus's ring, over what it holds */
	if ((const void *)w == t->focus) wsk__ring(s, t, w, v);
#endif
	if (g) wsk__s(s, "</g>");
}
#endif

#ifndef WSK_NO_INPUT
/*
 * The focus ring (§5m rule 4): four thin rectangles inset two pixels, in the
 * widget's ink -- rectangles, not a stroke, so it costs the window nothing
 * it does not already draw.
 */
static inline void wsk__ring(wsk_sink_t *s, const wsk_tree_t *t,
                             const wsk_widget_t *w, wsk_rect_t r)
{
	unsigned int ink = (w->ink != WSK_INHERIT) ? w->ink : t->ink;
	wsk_rect_t e;
	if (r.w < 12 || r.h < 12) return;
	e.x = (short)(r.x + 2); e.y = (short)(r.y + 2); e.w = (short)(r.w - 4); e.h = 2;
	wsk__box(s, 0, e, ink);
	e.y = (short)(r.y + r.h - 4);
	wsk__box(s, 0, e, ink);
	e.y = (short)(r.y + 2); e.w = 2; e.h = (short)(r.h - 4);
	wsk__box(s, 0, e, ink);
	e.x = (short)(r.x + r.w - 4);
	wsk__box(s, 0, e, ink);
}
#endif

#ifndef WSK_NO_CHECK
/* Whether a check is ticked: its byte of the root's state, or not at all. */
static inline int wsk__ticked(const wsk_tree_t *t, const wsk_widget_t *w)
{
	return t->state != 0 && w->sub >= 0 && w->sub < t->states && t->state[w->sub] != 0;
}

/*
 * A check (§5m rule 1): a box -- a border in the ink, the trough inside it --
 * a smaller box in the accent inside that when ticked, and the label
 * left-aligned after it. In
 * rectangles, so no <path>; whole or not at all, as a field's text is.
 */
static inline void wsk__check(wsk_sink_t *s, const wsk_tree_t *t,
                              const wsk_widget_t *w, wsk_rect_t r)
{
	int b = t->font * 3 / 4, top = r.y + (r.h - t->font) / 2, len;
	wsk_rect_t box;

	if (!wsk__shows(t, r.x, r.x + r.w, top)) return;
	box.x = (short)(r.x + t->pad);
	box.y = (short)(r.y + (r.h - b) / 2);
	box.w = (short)b; box.h = (short)b;
	/* a border in the ink, then the trough inside it: a trough-coloured box
	   alone was all but invisible on the scene, which the first picture
	   showed and no test could have */
	wsk__box(s, t->ids ? w->id : 0, box, (w->ink != WSK_INHERIT) ? w->ink : t->ink);
	{
		wsk_rect_t in = box;
		in.x = (short)(in.x + 2); in.y = (short)(in.y + 2);
		in.w = (short)(in.w - 4); in.h = (short)(in.h - 4);
		wsk__box(s, 0, in, t->trough);
	}
	if (wsk__ticked(t, w)) {
		box.x = (short)(box.x + b / 4); box.y = (short)(box.y + b / 4);
		box.w = (short)(b - b / 2);     box.h = (short)(b - b / 2);
		wsk__box(s, 0, box, (w->fill != WSK_INHERIT) ? w->fill : t->accent);
	}
	len = wsk__len(w->text);
	if (len > 0)
		wsk__textel(s, 1, r.x + t->pad + b + b / 2, wsk_baseline_of(top, t->font),
		            w->ink, w->text, len);
#ifndef WSK_NO_INPUT
	if ((const void *)w == t->focus) wsk__ring(s, t, w, r);
#endif
}
#endif

/*
 * A stack drawn in a font or ink of its own gets a <g> saying so, and one
 * that matches the document pays nothing -- for a composite's stack and for
 * an overlay alike. Returns whether it opened one, which the caller closes.
 */
static inline int wsk__look(wsk_sink_t *s, const wsk_doc_t *d, const wsk_tree_t *t)
{
	if (t->font == d->font && t->ink == d->ink) return 0;
	wsk__s(s, "<g font-size=\"");
	wsk__n(s, t->font);
	wsk__s(s, "\" fill=\"");
	wsk__col(s, t->ink);
	wsk__s(s, "\">");
	return 1;
}


/*
 * One stack's widgets into a document, clipped to its rows -- and, for a
 * WSK_STACK, the tree inside it, in the rectangle it was given (§5f).
 *
 * A nested stack is a <g> only when it has something to say: its id when
 * ids are on, which the browser client replaces (§9.7); a font or ink that
 * differs from the stack around it. A filled rectangle behind its children
 * only when its widget asks for a fill. A stack that says nothing costs
 * nothing, so nesting for layout alone adds no bytes.
 */
#ifndef WSK_NO_WORLD
static inline void wsk__items(const wsk_doc_t *d, const wsk_tree_t *t,
                              wsk_sink_t *s);

/*
 * A world's items (§5q), each through a copy of the world holding it alone:
 * the copy's font and pad are the world's times the zoom and the item's own
 * scale, so every drawing function scales the item without knowing it is in
 * a world -- and a <g font-size> says so where it differs. The widget is the
 * same widget, so focus, a held button and a field's buffer still find it.
 * Out of line, as a nested stack is, so only a world pays for the copy.
 */
#if defined(__GNUC__)
__attribute__((noinline, unused))
#endif
static void wsk__world(const wsk_doc_t *d, const wsk_tree_t *t, wsk_sink_t *s)
{
	wsk_tree_t c;
	for (int j = 0; j < t->n; j++)
	/* a torus's item across a seam is drawn again a world back, each way
	   (step 2 rule 2); anything else once */
	for (int k = 0; k < ((t->wspace == WSK_TORUS) ? 4 : 1); k++) {
		int i = wsk__wi(t, j);
		int ox = (k & 1) ? -t->ww : 0, oy = (k & 2) ? -t->wh : 0;
		wsk_rect_t v = wsk__wrect_at(t, i, ox, oy);
		long long zs = (long long)t->zoom * t->place[i].scale;
		int g;
		if (!wsk__cut(t, &v) || v.y >= d->y1 || v.y + v.h <= d->y0) continue;
		wsk__tcopy(&c, t);
		c.item = &t->item[i];
		c.place = &t->place[i];
		c.order = 0;
		c.n = 1;
		c.wone = 1;
		c.wdx = ox; c.wdy = oy;
#ifndef WSK_NO_INPUT
		c.sel = (t->sel == i) ? 0 : WSK_NONE;
#endif
		c.font = (short)((t->font * zs) >> 16);
		c.pad = (short)((t->pad * zs) >> 16);
		if (c.font < 1) continue;                       /* too small to draw */
		/* faded toward the background: the copy's own colours here, a
		   widget's own where they are drawn (step 2) */
		c.wfade = t->place[i].fade;
		c.wbg = t->bg;
		c.ink = wsk__fade(&c, t->ink);
		c.accent = wsk__fade(&c, t->accent);
		c.trough = wsk__fade(&c, t->trough);
		c.selfill = wsk__fade(&c, t->selfill);
		g = wsk__look(s, d, &c);
		wsk__items(d, &c, s);
		if (g) wsk__s(s, "</g>");
	}
}
#endif

static inline void wsk__items(const wsk_doc_t *d, const wsk_tree_t *t,
                              wsk_sink_t *s)
{
#ifndef WSK_NO_WORLD
	if (t->overflow == WSK_WORLD && !t->wone) { wsk__world(d, t, s); return; }
#endif
	for (int i = 0; i < t->n; i++) {
		const wsk_widget_t *w = &t->item[i];
		wsk_rect_t r = wsk_rect(t, i), v = r;
		if (!wsk__cut(t, &v)) continue;                    /* scrolled out of view */
		if (v.y >= d->y1 || v.y + v.h <= d->y0) continue;  /* not in these rows */
#ifndef WSK_NO_SCROLL
		if (t->caps & WSK_CAP_CLIP) v = r;                  /* the clip cuts it (§5r) */
#endif
		/* The box is drawn cut (v); everything else from the widget's own
		   rectangle (r), whole or not at all. */
#ifndef WSK_NO_NEST
		if (w->kind == WSK_STACK) { wsk__stack(d, t, i, v, s); continue; }
#endif
#if !defined(WSK_NO_TEXT) && !defined(WSK_NO_WRAP)
		if (w->kind == WSK_TEXT && t->overflow == WSK_WRAP) { wsk__runs(d, t, i, s); continue; }
#endif
#ifndef WSK_NO_SPLIT
		/* a divider is its bar, and nothing on it (§5x rule 2) -- here,
		   before the selection's `} else`, which the box below hangs on */
		if (w->kind == WSK_DIVIDER) { wsk__rect(s, t, w, v); continue; }
#endif
#ifndef WSK_NO_INPUT
		/* the chosen item of a selecting stack, in its own fill (§5m) -- a
		   text item too, which a chooser's rows are (§5ae): leaving it out
		   left a choice moved by the arrows nowhere to be seen */
		if (t->select && i == t->sel &&
		    (w->kind == WSK_BUTTON || w->kind == WSK_LABEL || w->kind == WSK_TEXT)) {
			wsk__box(s, t->ids ? w->id : 0, v, t->selfill);
			if (w->kind != WSK_BUTTON) { wsk__text(s, t, w, wsk__say(t, t, i), r); continue; }
		} else
#endif
#ifdef WSK_NO_FIELD
		if (w->kind == WSK_BUTTON) wsk__rect(s, t, w, v);
#else
		if (w->kind == WSK_BUTTON || w->kind == WSK_FIELD
#ifndef WSK_NO_MEMO
		    || wsk__lined(w->kind)
#endif
		    ) wsk__rect(s, t, w, v);
#endif
#ifndef WSK_NO_CHECK
		if (w->kind == WSK_CHECK) { wsk__check(s, t, w, r); continue; }
#endif
#ifndef WSK_NO_IMAGE
		/* an image is drawn whole or not at all, as text is: cutting its
		   rectangle would squash the picture rather than crop it -- unless
		   the host clips, when the clip crops it (§5r) */
		if (w->kind == WSK_IMAGE) {
			if ((t->caps & WSK_CAP_CLIP) || (v.w == r.w && v.h == r.h)) {
				wsk__s(s, "<image href=\"");
				wsk__s(s, wsk__href(t, w));
				wsk__s(s, "\" x=\""); wsk__n(s, r.x);
				wsk__s(s, "\" y=\""); wsk__n(s, r.y);
				wsk__s(s, "\" width=\""); wsk__n(s, r.w);
				wsk__s(s, "\" height=\""); wsk__n(s, r.h);
				wsk__s(s, "\"/>");
			}
			continue;
		}
#endif
#ifndef WSK_NO_INPUT
		if ((const void *)w == t->focus && w->kind == WSK_BUTTON &&
		    (t->axis == WSK_COL ? v.h == r.h : v.w == r.w))
			wsk__ring(s, t, w, r);
#endif
#ifndef WSK_NO_MARKS
		if ((w->mark == WSK_MARK_CORNERS
#ifndef WSK_NO_INPUT
		     || (const void *)w == t->held
#endif
		    ) && (t->axis == WSK_COL ? v.h == r.h : v.w == r.w)
#ifndef WSK_NO_NEST
		    && wsk__seen(t, r.x, r.y, r.x + r.w, r.y + r.h)
#endif
		    )
			wsk__marks(s, t, w, r);
#endif
#ifndef WSK_NO_FIELD
		if (w->kind == WSK_FIELD) wsk__field(s, t, w, r);
		else
#endif
#ifndef WSK_NO_MEMO
		if (wsk__lined(w->kind)) wsk__memo(s, t, w, r, d->y0, d->y1);
		else
#endif
		wsk__text(s, t, w, wsk__say(t, t, i), r);
	}
}

#ifndef WSK_NO_OVER
/*
 * The open overlay, over everything the root drew (§5l rule 2): a backing of
 * its own -- it lies over widgets, not over the scene -- cut to this
 * document's rows, then its widgets. A <g> only when its font or ink differ
 * from the document's.
 */
static inline void wsk__overlay(const wsk_doc_t *d, const wsk_tree_t *t,
                                wsk_sink_t *s)
{
	wsk_tree_t c;
	wsk_rect_t v;
	int g;

	if (!wsk__over_tree(t, &c)) return;
#ifndef WSK_NO_MENUKEYS
	/* the button the keys reached wears focus's ring -- in this copy
	   only, so the window's own focus is untouched (§5y rule 3) */
	if (t->ohi >= 0 && t->ohi < c.n) c.focus = &c.item[t->ohi];
#endif
	v.x = t->ox; v.w = t->ow;
	v.y = (short)((t->oy > d->y0) ? t->oy : d->y0);
	v.h = (short)(((t->oy + t->oh < d->y1) ? t->oy + t->oh : d->y1) - v.y);
	if (v.h <= 0) return;
	g = wsk__look(s, d, &c);
	wsk__box(s, 0, v, c.bg);
	wsk__items(d, &c, s);
	if (g) wsk__s(s, "</g>");
}
#endif

/*
 * One stack's widgets, clipped to the document's rows. No <svg>, no backing.
 *
 * A stack whose device differs from the document's is refused rather than
 * laid out against the wrong size: every rect in it would be placed by one
 * screen and clipped by another, which is the wrong-strip bug's family
 * (design.md §5) and the one new mistake this API makes possible.
 *
 * A stack with its own font or ink gets a nested <g>; one that matches the
 * document pays nothing, which is the common case.
 */
static inline int wsk_part(const wsk_doc_t *d, const wsk_tree_t *t,
                           wsk_sink_t *s)
{
	int nested;

	if (t->w != d->w || t->h != d->h) return WSK_E_DEVICE;
	{ int e = wsk_check(t, 0); if (e != WSK_OK) return e; }

	nested = wsk__look(s, d, t);

#ifndef WSK_NO_SCROLL
	{
		int clip = wsk__clip_open(s, t);
		wsk__items(d, t, s);
		if (clip) wsk__s(s, "</g>");
	}
#else
	wsk__items(d, t, s);
#endif

	if (nested) wsk__s(s, "</g>");
	return WSK_OK;
}

static inline void wsk_shut(wsk_sink_t *s) { wsk__s(s, "</g></svg>"); }

/* One stack, which is open + part + shut and is deliberately nothing else:
   the single-stack path and the composite path are the same code, so they
   cannot come to disagree about what a document looks like (D2). */
#ifndef WSK_NO_TIP
/*
 * A tip (§5ac rule 4): a box in the window's ink, its letters in the
 * window's background -- inverted, so it reads over whatever it covers --
 * one line, cut to the box. Nothing when there is no tip, so a window
 * without one draws exactly what it always did.
 */
static inline void wsk__tipbox(const wsk_doc_t *d, const wsk_tree_t *t, wsk_sink_t *s)
{
	wsk_rect_t v;
	int pad = 4, len, fit, n;
	if (t->tip == 0 || t->ty >= d->y1 || t->ty + t->th <= d->y0) return;
	v.x = t->tx; v.y = t->ty; v.w = t->tw; v.h = t->th;
	wsk__box(s, 0, v, t->ink);
	len = wsk__len(t->tip);
	fit = (t->tw - 2 * pad) / wsk_advance(t->font);
	n = (wsk__chars(t->tip, len) > fit) ? wsk__byte_of(t->tip, len, fit) : len;
	if (n > 0)
		wsk__textel(s, 1, t->tx + pad, wsk_baseline_of(t->ty + pad, t->font), t->bg, t->tip, n);
}
#endif

static inline void wsk__doc(const wsk_tree_t *t, int y0, int y1, wsk_sink_t *s)
{
	wsk_doc_t d;

	wsk_doc_init(&d, t);
	d.y0 = (short)y0;
	d.y1 = (short)y1;

	wsk_open(&d, s);
	(void)wsk_part(&d, t, s);    /* d came from t: it cannot be refused here */
#ifndef WSK_NO_OVER
	wsk__overlay(&d, t, s);      /* after the root, so on top of it (§5l) */
#endif
#ifndef WSK_NO_TIP
	wsk__tipbox(&d, t, s);       /* and a tip over everything (§5ac) */
#endif
	wsk_shut(s);
}

/*
 * Measure, then emit -- so a document that will not fit is refused before a
 * byte of it is written rather than after.
 *
 * That is not tidiness. Past SRC_MAX the window drops the document, and on
 * an RV-9 older than 2026-09-30 it stopped answering altogether. A caller
 * that adds a widget at runtime has to be told, and a half-written picture
 * is the one answer that helps nobody (design.md §2.3).
 */
static inline int wsk__emit(const wsk_tree_t *t, int y0, int y1,
                            wsk_sink_t *sink, int budget)
{
	wsk_sink_t dry;

	{ int e = wsk_check(t, 0); if (e != WSK_OK) return e; }

	dry.put = 0;
	dry.ctx = 0;
	dry.len = 0;
	wsk__doc(t, y0, y1, &dry);
	if (budget > 0 && dry.len > budget) return WSK_E_BUDGET;

	wsk__doc(t, y0, y1, sink);
	return dry.len;
}

/* The whole screen. Returns the bytes written, or a negative WSK_E_*. */
static inline int wsk_render(const wsk_tree_t *t, wsk_sink_t *sink, int budget)
{
	return wsk__emit(t, 0, t->h, sink, budget);
}

/*
 * Just the rows those widgets occupy, backing and all.
 *
 * Pair it with wsk_rows and give that same range to RV9_SVG_SS_ROWS. The
 * two agree by construction: both snap outward to the band, so what the
 * driver clears is exactly what this document paints.
 */
static inline int wsk_render_clipped(const wsk_tree_t *t, const char *const *ids,
                                     int n, wsk_sink_t *sink, int budget)
{
	int y0, y1, e = wsk_rows(t, ids, n, &y0, &y1);
	if (e != WSK_OK) return e;
	return wsk__emit(t, y0, y1, sink, budget);
}

/*
 * Rows y0..y1 of the device, whatever changed in them -- the repaint a
 * toolkit event asks for (§5i), which is the document wsk_render_clipped
 * always built underneath, asked for by rows rather than by ids. The range
 * should be band-snapped, as wsk_feed's always is.
 */
static inline int wsk_render_rows(const wsk_tree_t *t, int y0, int y1,
                                  wsk_sink_t *sink, int budget)
{
	if (y1 <= y0) return WSK_E_EMPTY;
	return wsk__emit(t, y0, y1, sink, budget);
}

/*
 * Several stacks over one device, in one document.
 *
 * The caller's body does the emitting -- wsk_open, a wsk_part per stack,
 * wsk_shut -- and this runs it TWICE: once against a counting sink, once for
 * real. So a composite that will not fit is refused before a byte of it is
 * written, which is the same guarantee wsk_render gives a single stack and
 * for the same reason (§2.3): past SRC_MAX the window drops the document, and
 * a half-written picture is the one answer that helps nobody.
 *
 * THE BODY MUST EMIT THE SAME BYTES BOTH TIMES. It is called twice; if it
 * reads state that changes in between, the count it was refused or accepted
 * on is a lie about the document that got written. That is why it takes a
 * ctx instead of closing over anything -- there is nothing to close over in
 * C, and a body that reaches for a mutable global is the mistake this
 * sentence exists to name.
 *
 * Passing a function pointer as an argument is position independent; it is a
 * stored table of them that a module cannot link (§4.1), and the sink has
 * worked this way from the start.
 *
 *     static int keyboard(wsk_sink_t *s, void *ctx)
 *     {
 *         kb_t *k = (kb_t *)ctx;
 *         int e;
 *         wsk_open(&k->doc, s);
 *         for (int i = 0; i < 4; i++)
 *             if ((e = wsk_part(&k->doc, &k->row[i], s)) != WSK_OK) return e;
 *         wsk_shut(s);
 *         return WSK_OK;
 *     }
 *
 *     int n = wsk_compose(keyboard, &kb, sink, 4096);
 *
 * Returns the bytes written, or a negative WSK_E_* -- including whatever the
 * body returned, so a WSK_E_DEVICE from one stack reaches the caller rather
 * than producing a document missing a row.
 */
typedef int (*wsk_body_t)(wsk_sink_t *s, void *ctx);

static inline int wsk_compose(wsk_body_t body, void *ctx, wsk_sink_t *sink,
                              int budget)
{
	wsk_sink_t dry;
	int e;

	dry.put = 0;
	dry.ctx = 0;
	dry.len = 0;
	e = body(&dry, ctx);
	if (e < 0) return e;
	if (budget > 0 && dry.len > budget) return WSK_E_BUDGET;

	e = body(sink, ctx);
	if (e < 0) return e;
	return dry.len;
}

/*
 * Which widget is at that point, across a composite.
 *
 * There is no wsk_compose_hit, because there is nothing for it to do that the
 * caller cannot: loop the stacks and take the first that answers. Stacks do
 * not overlap by construction -- the caller gave them areas -- so the order
 * is the caller's and whisker has no opinion about it (D5).
 */

/* ------------------------------------------------------------------ */
/* The toolkit: an event in, what changed out (design.md §5i, §8)       */
/* ------------------------------------------------------------------ */

#ifndef WSK_NO_INPUT
/*
 * One event, the same on every host -- a host's whole job is translating to
 * it. Touch and the mouse are the same three (D7): a click and a tap are one
 * point each. KEY and TEXT are declared and, until the field's iteration,
 * change nothing.
 */
enum { WSK_EV_DOWN = 1, WSK_EV_MOVE, WSK_EV_UP, WSK_EV_WHEEL, WSK_EV_KEY, WSK_EV_TEXT,
       WSK_EV_MENU,               /* a right-click, or a long press: at x, y (§5s) */
       WSK_EV_HOVER,              /* the pointer moved with nothing held: at x, y, or
                                     -1, -1 when it left the window (§5aa) */
       WSK_EV_TICK };             /* time passed: d milliseconds since the last tick --
                                     whisker has no clock of its own (§5ac) */

typedef struct {
	unsigned char  kind;
	short          x, y;         /* device pixels */
	short          d;            /* WHEEL: pixels along the stack's axis;
	                                DOWN: the clicks the host counted, 2 a
	                                double-click -- 0 or 1, a single (§5w) */
	unsigned short key;
	char           text[8];
	unsigned int   ms;
	unsigned char  finger;       /* DOWN, MOVE, UP: which finger, the platform's
	                                number for it; a mouse, and every program
	                                that never says, is 0 (§5ad) */
} wsk_event_t;

/* What changed. A notice names its widget as wsk_hit_in does -- the
   caller's own tree and an index in it -- because stacks nest (§5f). */
enum { WSK_NOTE_CLICK = 1, WSK_NOTE_SCROLL, WSK_NOTE_TEXT, WSK_NOTE_ENTER,
       WSK_NOTE_FOCUS, WSK_NOTE_CLOSE, WSK_NOTE_CHANGE, WSK_NOTE_SELECT,
       WSK_NOTE_MENU,             /* asked for a context menu: the widget, or -1 */
       WSK_NOTE_KEY,              /* a chord no widget used: value is the key */
       WSK_NOTE_COPY,             /* the selection is in ui.clip: value, its length */
       WSK_NOTE_PASTE,            /* Ctrl-V in a field: hand wsk_ui_paste the text */
       WSK_NOTE_GUTTER,           /* a source's gutter clicked: value, the line (§5v) */
       WSK_NOTE_SPLIT,            /* a divider dragged: item, the divider; value, the
                                     new extent of the pane it moved (§5x) */
       WSK_NOTE_VIEW,             /* a world panned or zoomed: in, the world; value,
                                     its zoom (§5q step 3) */
       WSK_NOTE_MOVE,             /* a world's item dragged: in, the world; item (step 6) */
       WSK_NOTE_RAISE,            /* ... and brought to the top of its order */
       WSK_NOTE_HOVER,            /* the pointer rests on something else: in, item and
                                     value (the word) as a click's; in 0, nothing (§5aa) */
       WSK_NOTE_TIP,              /* it has rested long enough for a tip: answer with
                                     wsk_ui_tip, or do not (§5ac) */
       WSK_NOTE_GRAB,             /* a grab tree pressed and moved: in, the tree (§5ag) */
       WSK_NOTE_DRAG,             /* ... moved again: the point is the event's */
       WSK_NOTE_DROP };           /* ... let go: the point is the event's */

/* The keys that mean something to a field (§5j). A character typed is TEXT,
   not a key, so that a keyboard layout stays the host's business. */
enum { WSK_KEY_LEFT = 1, WSK_KEY_RIGHT, WSK_KEY_HOME, WSK_KEY_END,
       WSK_KEY_BACKSPACE, WSK_KEY_DELETE, WSK_KEY_ENTER, WSK_KEY_TAB,
       WSK_KEY_BACKTAB, WSK_KEY_ESCAPE, WSK_KEY_UP, WSK_KEY_DOWN,
       WSK_KEY_MENU };

/* Modifiers ride on the key (§5s rule 2): a named key above, or for a chord
   a printable character, lower-case -- Ctrl-S is 's' | WSK_MOD_CTRL. A key
   with none is exactly what it always was. On a press, whether Shift is held. */
enum { WSK_MOD_SHIFT = 0x100, WSK_MOD_CTRL = 0x200, WSK_MOD_ALT = 0x400,
       WSK_MOD_TOUCH = 0x800 };   /* on a press: a finger, which may hold for a tip (§5ac) */

typedef struct {
	unsigned char     kind;
	const wsk_tree_t *in;        /* CLICK: the widget's tree; SCROLL: the stack */
	short             item;      /* CLICK: its index; SCROLL: -1 */
	int               value;     /* CLICK: the word, on a text item, else -1;
	                                SCROLL: the new offset */
} wsk_note_t;

/*
 * The toolkit's state: the CALLER's, which is D5 in the form that mattered
 * (D16b) -- whisker allocates and keeps nothing. Trees it changes are the
 * caller's too, and must be writable; widgets it never writes (§5i).
 */
typedef struct {
	wsk_tree_t       *root;
	short             slop;      /* how far a finger moves before it drags */

	unsigned char     down, dragging;
	short             x0, y0;    /* where it went down */
	const wsk_tree_t *hit_in;    /* and what was there */
	short             hit, word;
	wsk_rect_t        held;      /* the held widget's rows, to repaint */

	wsk_tree_t       *drag;      /* the scrolling stack under the down */
	wsk_tree_t        drag_at;   /* ... as laid out: its area and range */
	short             scroll0;
#ifndef WSK_NO_WORLD
	int               wv0x, wv0y; /* or the world: its view at the down */
	/* Two fingers on a world (§5ad): the first's number and where it is;
	   the second's; 0 no pinch, 1 pinching, 2 one lifted, the other inert
	   until it lifts too; the distance, zoom and world point at the start */
	unsigned char     f0, f1, pinch, fleft;
	short             fx, fy, gx, gy;
	int               pd0, pz0, pwx, pwy;
	short             witem;     /* the item a drag moves, or -1 (step 6) */
	int               wp0x, wp0y; /* ... its place at the down */
#endif

#ifndef WSK_NO_OVER
	const void       *dfocus;    /* what had focus before a dialog opened (§5ae) */
#endif
#ifndef WSK_NO_GRAB
	const wsk_tree_t *gin;       /* the grab tree a press is in, or 0 (§5ag) */
	unsigned char     grabbing;  /* ...and it has moved: GRAB said */
#endif
	const wsk_tree_t *focus_in;  /* the focused field: its tree and index, */
	short             focus_item;
	wsk_rect_t        focus_r;   /* its rows to repaint, */
	wsk_buf_t        *focus_b;   /* and the buffer it edits */

#ifndef WSK_NO_TEXTSEL
	/* The clipboard's copy (§5s rule 6): the CALLER's memory, set after
	   wsk_ui_init, which Ctrl-C and Ctrl-X copy a selection into. 0, none. */
	char             *clip;
	int               clipcap;
	int               anchor;    /* a drag's selection: the end it started at */
	unsigned char     selecting; /* a press in the focused field, selecting */
#ifndef WSK_NO_WORDS
	int               wa, wz;    /* a double-click's word, which a drag keeps;
	                                wa -1, none (§5w) */
#endif
#endif
#ifndef WSK_NO_SOURCE
	int               gline;     /* a press in a source's gutter: its line, or -1 */
#endif
#ifndef WSK_NO_TIP
	/* A rest, counted toward a tip (§5ac): what rests, where it is, how
	   long; 0 none, 1 counting, 2 asked; whether a finger holds it */
	const wsk_tree_t *tip_in;
	short             tip_item;
	wsk_rect_t        tip_r;
	int               tip_ms, tip_wait;  /* tip_wait: the caller's, 500 by default */
	unsigned char     tip_state, tip_hold;
#endif
#ifndef WSK_NO_HOVER
	const wsk_tree_t *hover_in;  /* what the pointer rests on: its tree, or 0, */
	short             hover_item, hover_word; /* its index and word (§5aa) */
#endif
#ifndef WSK_NO_SPLIT
	const wsk_tree_t *split;     /* a divider held: its stack, or 0 (§5x) */
	wsk_tree_t        split_at;  /* ... as laid out: its area */
	short             split_k;   /* the divider */
	short             split0[2]; /* the panes either side, as the press found them */
#endif
} wsk_ui_t;

static inline void wsk_ui_init(wsk_ui_t *ui, wsk_tree_t *root)
{
	ui->root = root;
	ui->slop = 16;               /* §5b: scatter 9-21 px worst on the P4 */
	ui->down = 0;
	ui->dragging = 0;
	ui->drag = 0;
#ifndef WSK_NO_WORLD
	ui->witem = -1;
	ui->pinch = 0;
	ui->f0 = ui->f1 = ui->fleft = 0;
#endif
	ui->hit = -1;
	ui->word = -1;
	ui->focus_in = 0;
	ui->focus_item = -1;
	ui->focus_b = 0;
#ifndef WSK_NO_OVER
	ui->dfocus = 0;
#endif
#ifndef WSK_NO_GRAB
	ui->gin = 0;
	ui->grabbing = 0;
#endif
	root->held = 0;
	root->focus = 0;
#ifndef WSK_NO_SOURCE
	ui->gline = -1;
#endif
#ifndef WSK_NO_HOVER
	ui->hover_in = 0;
	ui->hover_item = ui->hover_word = -1;
#endif
#ifndef WSK_NO_TIP
	ui->tip_in = 0;
	ui->tip_item = -1;
	ui->tip_ms = 0;
	ui->tip_wait = 500;
	ui->tip_state = ui->tip_hold = 0;
	root->tip = 0;
#endif
#ifndef WSK_NO_SPLIT
	ui->split = 0;
#endif
#ifndef WSK_NO_TEXTSEL
	ui->clip = 0;
	ui->clipcap = 0;
	ui->selecting = 0;
	/* no selection anywhere, whatever a buffer held (§5s rule 4) */
	for (int i = 0; root->buf != 0 && i < root->bufs; i++) root->buf[i].span = 0;
#endif
}

#ifndef WSK_NO_TIP
/* Rows y0..y1, band-snapped as wsk_feed's are, from a span. */
static inline void wsk__rows_out(const wsk_tree_t *r, int lo, int hi, int *y0, int *y1)
{
	if (hi > lo) {
		lo = (lo / WSK_BAND) * WSK_BAND;
		hi = wsk__round_band(hi);
		if (lo < 0) lo = 0;
		if (hi > r->h) hi = r->h;
	} else lo = hi = 0;
	*y0 = lo;
	*y1 = hi;
}

/* An open tip closed, its rows to repaint. */
static inline void wsk__tip_close(wsk_ui_t *ui, int *lo, int *hi)
{
	wsk_tree_t *r = ui->root;
	if (r->tip == 0) return;
	if (*lo > r->ty) *lo = r->ty;
	if (*hi < r->ty + r->th) *hi = r->ty + r->th;
	r->tip = 0;
}

/* A new rest (§5ac rule 1) -- on item of in, at rect, held by a finger or
   not -- or, item < 0, none: either way any tip shown is closed. */
static inline void wsk__tip_rest(wsk_ui_t *ui, const wsk_tree_t *in, int item,
                                 wsk_rect_t rect, int hold, int *lo, int *hi)
{
	wsk__tip_close(ui, lo, hi);
	ui->tip_in = (item >= 0) ? in : 0;
	ui->tip_item = (short)((item >= 0) ? item : -1);
	ui->tip_r = rect;
	ui->tip_ms = 0;
	ui->tip_state = (item >= 0) ? 1 : 0;
	ui->tip_hold = (unsigned char)(hold && item >= 0);
}

/*
 * Time passing (§5ac rule 2): ms milliseconds since the last tick. A rest
 * long enough asks for its tip, once -- WSK_NOTE_TIP in note[0], and 1;
 * else 0. The same as a WSK_EV_TICK through wsk_feed, and for a program
 * that would rather not route time through it: on a module, wsk_feed
 * inlined where only presses reach it compiles to a fraction of the
 * toolkit, and a tick through it would bring the rest back in.
 */
static inline int wsk__note(wsk_note_t *note, int max, int n, int kind,
                            const wsk_tree_t *in, int item, int value);

static inline int wsk_ui_tick(wsk_ui_t *ui, int ms, wsk_note_t *note, int max)
{
	if (ui->tip_state != 1 || ms <= 0) return 0;
	ui->tip_ms += ms;
	if (ui->tip_ms < ui->tip_wait) return 0;
	ui->tip_state = 2;
	return wsk__note(note, max, 0, WSK_NOTE_TIP, ui->tip_in, ui->tip_item, 0);
}

/*
 * Show a tip (§5ac rule 3): the program's answer to WSK_NOTE_TIP, its text
 * in its own memory, which must outlive the tip. One line, placed below the
 * widget that rested -- else above -- and kept on the screen; the rows to
 * repaint. WSK_E_NOTFOUND if no rest has asked for one; WSK_E_TEXT for text
 * the window cannot draw, as a label's would be refused.
 */
static inline int wsk_ui_tip(wsk_ui_t *ui, const char *text, int *y0, int *y1)
{
	wsk_tree_t *r = ui->root;
	wsk_rect_t a = ui->tip_r;
	int lo = 32767, hi = -1, pad = 4, w, h, x, y;
	*y0 = *y1 = 0;
	if (ui->tip_state != 2 || ui->tip_item < 0) return WSK_E_NOTFOUND;
	if (text == 0 || text[0] == '\0' || !wsk__text_ok(text, r->caps)) return WSK_E_TEXT;
	wsk__tip_close(ui, &lo, &hi);
	w = wsk_text_w(r->font, wsk__chars(text, wsk__len(text))) + 2 * pad;
	h = r->font + 2 * pad;
	if (w > r->w) w = r->w;
	x = a.x;
	if (x + w > r->w) x = r->w - w;
	if (x < 0) x = 0;
	y = a.y + a.h + 2;
	if (y + h > r->h) y = a.y - h - 2;
	if (y < 0) y = 0;
	r->tip = text;
	r->tx = (short)x; r->ty = (short)y; r->tw = (short)w; r->th = (short)h;
	if (lo > y) lo = y;
	if (hi < y + h) hi = y + h;
	wsk__rows_out(r, lo, hi, y0, y1);
	return WSK_OK;
}
#endif

/* The deepest widget at a point, its tree, its rectangle as cut, and its
   word if it is a paragraph's. */
static inline int wsk__ui_hit(wsk_tree_t *r, int x, int y, const wsk_tree_t **in,
                              wsk_rect_t *rect, int *word, wsk_tree_t *at,
                              const wsk_tree_t **laid)
{
	int k;
	const wsk_tree_t *lay;
#ifndef WSK_NO_NEST
	k = wsk__hit_at(r, r, x, y, in, at, 0);
	lay = at;
#else
	(void)at;
	k = wsk_hit(r, x, y);
	*in = r;
	lay = r;
#endif
	*word = -1;
	*laid = lay;
	if (k < 0) return k;
	*rect = wsk_rect(lay, k);
	(void)wsk__cut(lay, rect);
#if !defined(WSK_NO_TEXT) && !defined(WSK_NO_WRAP)
	if (lay->item[k].kind == WSK_TEXT && lay->overflow == WSK_WRAP)
		*word = wsk__word_at(lay, k, x, y);
#endif
	return k;
}

#ifndef WSK_NO_SCROLL
/*
 * The deepest scrolling stack whose viewport holds the point: the caller's
 * own tree, to write `scroll` into, and the copy it was laid out as, for its
 * area and range. 0 when the point is in none.
 */
#ifndef WSK_NO_WORLD
/*
 * The item of a world a point is on -- the topmost, by the world's order,
 * on whichever copy of a torus's -- and, when it is a stack, the tree inside
 * it laid out as that copy (*got 1). -1 if none. What hit decides for a
 * world, for every walker that must agree with hit: a press scrolls or
 * chooses in the poster on top, never one beneath it. Out of line, so the
 * two copies cost only a world.
 */
#if defined(__GNUC__)
__attribute__((noinline, unused))
#endif
static int wsk__wtop(const wsk_tree_t *t, int x, int y, wsk_tree_t *c, int *got)
{
	*got = 0;
	for (int j = 0; j < t->n; j++) {
		int i = wsk__wi(t, t->n - 1 - j), k = wsk__whit(t, i, x, y);
		if (k == 0) continue;
		if (t->item[i].kind == WSK_STACK) {
			wsk_tree_t h;
			wsk__tcopy(&h, t);
			h.wdx = ((k - 1) & 1) ? -t->ww : 0;
			h.wdy = ((k - 1) & 2) ? -t->wh : 0;
			*got = wsk__child(&h, i, c);
		}
		return i;
	}
	return -1;
}
#endif

static inline wsk_tree_t *wsk__scroller(const wsk_tree_t *t, wsk_tree_t *orig,
                                        int x, int y, wsk_tree_t *at, int depth)
{
#ifndef WSK_NO_OVER
	if (depth == 0 && wsk__on_over(t, x, y)) {
		wsk_tree_t c;
		if (!wsk__over_tree(t, &c)) return 0;
		return wsk__scroller(&c, (wsk_tree_t *)&t->nest[t->over], x, y, at, 1);
	}
#endif
#ifndef WSK_NO_WORLD
	if (t->overflow == WSK_WORLD) {
		/* the poster on top, as hit has it -- not the first in the array */
		wsk_tree_t c;
		int got, i = wsk__wtop(t, x, y, &c, &got);
		if (i >= 0 && got && depth + 1 < WSK_DEPTH_MAX) {
			wsk_tree_t *f = wsk__scroller(&c, (wsk_tree_t *)&t->nest[t->item[i].sub],
			                              x, y, at, depth + 1);
			if (f != 0) return f;
		}
	} else
#endif
#ifndef WSK_NO_NEST
	for (int i = 0; i < t->n && depth + 1 < WSK_DEPTH_MAX; i++) {
		wsk_rect_t r;
		wsk_tree_t c;
		if (t->item[i].kind != WSK_STACK) continue;
		r = wsk_rect(t, i);
		if (!wsk__cut(t, &r)) continue;
		if (!(x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)) continue;
		if (wsk__child(t, i, &c)) {
			wsk_tree_t *f = wsk__scroller(&c, (wsk_tree_t *)&t->nest[t->item[i].sub],
			                              x, y, at, depth + 1);
			if (f != 0) return f;
		}
		break;                       /* stacks do not overlap */
	}
#endif
	if ((t->overflow == WSK_SCROLL
#ifndef WSK_NO_WORLD
	     || t->overflow == WSK_WORLD          /* a world pans and zooms (§5q) */
#endif
	    ) && x >= t->ax && x < t->ax + t->aw && y >= t->ay && y < t->ay + t->ah) {
#ifndef WSK_NO_NEST
		wsk__tcopy(at, t);
#else
		(void)at;
#endif
		return orig;
	}
	(void)depth;
	return 0;
}

/* Scroll a stack to `to`, clamped to its range: 1 if it moved. */
static inline int wsk__scroll_to(wsk_tree_t *orig, wsk_tree_t *at, int to)
{
	int max = wsk_scroll_max(at);
	if (to < 0) to = 0;
	if (to > max) to = max;
	if (to == orig->scroll) return 0;
	orig->scroll = (short)to;
	at->scroll = (short)to;
	return 1;
}
#endif

/* A rectangle into one the toolkit keeps. Field by field, because the
   assignment became a call to memcpy in two places, which a module cannot
   link -- the module build caught both (§5j). */
static inline void wsk__rput(wsk_rect_t *d, wsk_rect_t s)
{
	d->x = s.x; d->y = s.y; d->w = s.w; d->h = s.h;
}

/*
 * The deepest selecting stack with an item under the point (§5m, §5n): the
 * caller's tree, to write `sel` into; the item's index; and the stack as laid
 * out, for the rows to repaint. 0 when no selecting stack has one there.
 */
static inline wsk_tree_t *wsk__selector(const wsk_tree_t *t, wsk_tree_t *orig,
                                        int x, int y, wsk_tree_t *at,
                                        const wsk_tree_t **lay, int *item, int depth)
{
	(void)depth;
#ifndef WSK_NO_OVER
	if (depth == 0 && wsk__on_over(t, x, y)) {
		wsk_tree_t c;
		if (!wsk__over_tree(t, &c)) return 0;
		return wsk__selector(&c, (wsk_tree_t *)&t->nest[t->over], x, y, at, lay, item, 1);
	}
#endif
#ifndef WSK_NO_WORLD
	if (t->overflow == WSK_WORLD) {
		/* the item on top only, as hit has it (§5q step 6) */
		wsk_tree_t c;
		int got, i = wsk__wtop(t, x, y, &c, &got);
		if (i < 0) return 0;
		if (got && depth + 1 < WSK_DEPTH_MAX) {
			wsk_tree_t *f = wsk__selector(&c, (wsk_tree_t *)&t->nest[t->item[i].sub],
			                              x, y, at, lay, item, depth + 1);
			if (f != 0) return f;
		}
		if (!t->select) return 0;
		wsk__tcopy(at, t);
		*lay = at;
		*item = i;
		return orig;
	}
#endif
	for (int i = 0; i < t->n; i++) {
		wsk_rect_t r = wsk_rect(t, i);
		if (!wsk__cut(t, &r)) continue;
		if (!(x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)) continue;
#ifndef WSK_NO_NEST
		if (t->item[i].kind == WSK_STACK && depth + 1 < WSK_DEPTH_MAX) {
			wsk_tree_t c;
			if (wsk__child(t, i, &c)) {
				wsk_tree_t *f = wsk__selector(&c, (wsk_tree_t *)&t->nest[t->item[i].sub],
				                              x, y, at, lay, item, depth + 1);
				if (f != 0) return f;
			}
		}
#endif
		if (!t->select) return 0;
#ifndef WSK_NO_NEST
		wsk__tcopy(at, t);
		*lay = at;
#else
		(void)at;
		*lay = t;
#endif
		*item = i;
		return orig;
	}
	return 0;
}

#ifndef WSK_NO_CHECK
/* Tick or untick a check: its byte of the root's state. -1 if it has none. */
static inline int wsk__toggle(wsk_tree_t *r, const wsk_widget_t *w)
{
	if (r->state == 0 || w->sub < 0 || w->sub >= r->states) return -1;
	r->state[w->sub] = (unsigned char)!r->state[w->sub];
	return r->state[w->sub];
}
#endif

static inline void wsk__span(int *lo, int *hi, int a, int b)
{
	if (b <= a) return;
	if (a < *lo) *lo = a;
	if (b > *hi) *hi = b;
}

/*
 * Whatever press is in progress, let go (found by review, 2026-11-16): the
 * held button's rows repainted, and no click, drag, selection, divider or
 * pinch left to finish -- for a dialog opening or closing over it, or a new
 * finger's press when the old one's lifting was lost.
 */
static inline void wsk__press_end(wsk_ui_t *ui, int *lo, int *hi)
{
	wsk_tree_t *r = ui->root;
	ui->down = 0;
	ui->dragging = 0;
	ui->hit = -1;
	if (r->held != 0) {
		r->held = 0;
		wsk__span(lo, hi, ui->held.y, ui->held.y + ui->held.h);
	}
#ifndef WSK_NO_TEXTSEL
	ui->selecting = 0;
#endif
#ifndef WSK_NO_SPLIT
	ui->split = 0;
#endif
#ifndef WSK_NO_SCROLL
	ui->drag = 0;
#endif
#ifndef WSK_NO_WORLD
	ui->pinch = 0;
	ui->witem = -1;
#endif
#ifndef WSK_NO_SOURCE
	ui->gline = -1;
#endif
#ifndef WSK_NO_GRAB
	ui->gin = 0;
	ui->grabbing = 0;
#endif
}

static inline int wsk__note(wsk_note_t *note, int max, int n, int kind,
                            const wsk_tree_t *in, int item, int value)
{
	if (n < max) {
		note[n].kind = (unsigned char)kind;
		note[n].in = in;
		note[n].item = (short)item;
		note[n].value = value;
	}
	return n + 1;
}


#ifndef WSK_NO_FIELD
/*
 * The editable fields, in declaration order, depth first -- the order D0
 * kept stable for exactly this (§5j rule 4). Returns how many there are. The
 * one that is `want` (a widget's address), or failing that the `nth`, has
 * its tree, index, rectangle as cut and buffer written out; *pos gets the
 * ordinal of `want`, or stays -1.
 */
static inline int wsk__fields(const wsk_tree_t *t, const wsk_tree_t *orig,
                              int count, const void *want, int nth, int *pos,
                              const wsk_tree_t **in, short *item, wsk_rect_t *rect,
                              wsk_buf_t **buf, int depth)
{
	for (int i = 0; i < t->n; i++) {
		const wsk_widget_t *w = &t->item[i];
		wsk_buf_t *b = wsk__fbuf(t, w);
		int whole = 0;
#if !defined(WSK_NO_LISTKEYS) && !defined(WSK_NO_NEST)
		/* a list, or a world, is one stop, and what is inside it none
		   (§5z rule 1): its keys move within it */
		if (w->kind == WSK_STACK && t->nest != 0 && w->sub >= 0 && w->sub < t->nests &&
		    (t->nest[w->sub].select
#ifndef WSK_NO_WORLD
		     || t->nest[w->sub].overflow == WSK_WORLD
#endif
		    ))
			whole = 1;
#endif
		/* focusable (§5m rule 4): an editable field, a button, a check */
		if (b != 0 || w->kind == WSK_BUTTON || whole
#ifndef WSK_NO_CHECK
		    || w->kind == WSK_CHECK
#endif
		    ) {
			if ((const void *)w == want || (want == 0 && count == nth)) {
				wsk_rect_t r = wsk_rect(t, i);
				if (!wsk__cut(t, &r)) r.h = 0;        /* out of view */
				if (pos != 0) *pos = count;
				*in = orig; *item = (short)i; *buf = b;
				wsk__rput(rect, r);
			}
			count++;
		}
#ifndef WSK_NO_NEST
		if (w->kind == WSK_STACK && !whole && depth + 1 < WSK_DEPTH_MAX) {
			wsk_tree_t c;
			if (wsk__child(t, i, &c))
				count = wsk__fields(&c, &t->nest[w->sub], count, want, nth, pos,
				                    in, item, rect, buf, depth + 1);
		}
#endif
	}
	(void)depth;
	return count;
}

/*
 * Where the keys work (§5ae): an open dialog -- laid out in c, *orig its
 * caller's tree -- or else the window itself. Tab, focus and a list's keys
 * all walk this, so a dialog's fields are the keyboard's while it is open.
 */
static inline const wsk_tree_t *wsk__keys_in(const wsk_tree_t *r, wsk_tree_t *c,
                                             const wsk_tree_t **orig)
{
#ifndef WSK_NO_OVER
	if (r->over != WSK_NONE && r->omodal && wsk__over_tree(r, c)) {
		*orig = &r->nest[r->over];
		return c;
	}
#else
	(void)c;
#endif
	*orig = r;
	return r;
}

#if !defined(WSK_NO_LISTKEYS) && !defined(WSK_NO_NEST)
/* The laid-out tree that holds widget `w`, and its index there, or -1: how
   the keys reach the tree inside a focused list or world (§5z). */
static inline int wsk__lay_of(const wsk_tree_t *t, const void *w, wsk_tree_t *at, int depth)
{
	for (int i = 0; i < t->n; i++) {
		if ((const void *)&t->item[i] == w) { wsk__tcopy(at, t); return i; }
		if (t->item[i].kind == WSK_STACK && depth + 1 < WSK_DEPTH_MAX) {
			wsk_tree_t c;
			int k;
			if (wsk__child(t, i, &c) && (k = wsk__lay_of(&c, w, at, depth + 1)) >= 0) return k;
		}
	}
	return -1;
}
#endif

/* Focus moves: the old field's rows and the new one's both repaint. */
static inline void wsk__refocus(wsk_ui_t *ui, const wsk_tree_t *in, int item,
                                wsk_rect_t r, wsk_buf_t *b, int *lo, int *hi)
{
	if (ui->root->focus != 0) wsk__span(lo, hi, ui->focus_r.y, ui->focus_r.y + ui->focus_r.h);
	ui->focus_in = in;
	ui->focus_item = (short)item;
	wsk__rput(&ui->focus_r, r);
	ui->focus_b = b;
	ui->root->focus = (in != 0 && item >= 0) ? (const void *)&in->item[item] : 0;
	if (in != 0 && item >= 0) wsk__span(lo, hi, r.y, r.y + r.h);
}

/* Where the focused field is NOW -- a drag may have moved it since. */
#if defined(__GNUC__)
__attribute__((noinline, unused))
#endif
static void wsk__focus_now(wsk_ui_t *ui)
{
	wsk_tree_t c;
	const wsk_tree_t *orig, *k;
	if (ui->root->focus == 0) return;
	k = wsk__keys_in(ui->root, &c, &orig);
	(void)wsk__fields(k, orig, 0, ui->root->focus, -1, 0, &ui->focus_in,
	                  &ui->focus_item, &ui->focus_r, &ui->focus_b, 0);
}

/*
 * Tab or Shift-Tab (§5m rule 4): the next or previous focusable widget in
 * declaration order, depth first -- in the open dialog, if there is one
 * (§5ae). Out of line, so the dialog's copy costs only a Tab.
 */
#if defined(__GNUC__)
__attribute__((noinline, unused))
#endif
static int wsk__tab(wsk_ui_t *ui, int back, const wsk_tree_t **in, short *item,
                    wsk_rect_t *fr, wsk_buf_t **nb)
{
	wsk_tree_t c;
	const wsk_tree_t *orig, *k = wsk__keys_in(ui->root, &c, &orig);
	int pos = -1, all = wsk__fields(k, orig, 0, ui->root->focus, -1, &pos, in, item, fr, nb, 0);
	if (all <= 0 || (all == 1 && pos == 0)) return 0;
	*nb = 0;
	(void)wsk__fields(k, orig, 0, 0, back ? ((pos < 0) ? all - 1 : (pos + all - 1) % all)
	                                      : (pos + 1) % all, 0, in, item, fr, nb, 0);
	return 1;
}

/* Characters moved within a buffer. A loop the compiler is told to keep:
   it would otherwise become memmove, which a module cannot link. */
static inline void wsk__shift(char *b, int from, int to, int n)
{
	if (to > from)
		for (int j = n - 1; j >= 0; j--) {
			b[to + j] = b[from + j];
#if defined(__GNUC__)
			__asm__ volatile ("" ::: "memory");
#endif
		}
	else
		for (int j = 0; j < n; j++) {
			b[to + j] = b[from + j];
#if defined(__GNUC__)
			__asm__ volatile ("" ::: "memory");
#endif
		}
}

#ifndef WSK_NO_UNDO
/* A step's numbers, four bytes each, written a byte at a time: the array is
   the caller's chars, with no alignment to lean on. */
static inline void wsk__u4(char *p, int v)
{
	for (int k = 0; k < 4; k++) p[k] = (char)((unsigned)v >> (8 * k));
}
static inline int wsk__g4(const char *p)
{
	unsigned v = 0;
	for (int k = 0; k < 4; k++) v |= (unsigned)(unsigned char)p[k] << (8 * k);
	return (int)v;
}

/*
 * A step is  at, removed, inserted | the removed bytes | the inserted |
 * its own size  -- 16 bytes and the text, the size last so that undo can
 * walk back from `used`. Typing at the end of the last step's insertion
 * grows that step rather than adding one, so a word -- with the space
 * before it -- is one undo.
 */
static inline void wsk__urec(wsk_undo_t *u, const char *txt, int at, int del,
                             const char *ins, int n, int typing)
{
	int need = 16 + del + n, p;

	u->end = u->used;                        /* a new edit ends redo */
	if (u->join && typing && u->used >= 16) {
		int size = wsk__g4(u->b + u->used - 4), st = u->used - size;
		int pat = wsk__g4(u->b + st), pdel = wsk__g4(u->b + st + 4), pins = wsk__g4(u->b + st + 8);
		/* a space after a word starts the next step, which it is part of */
		if (pdel == 0 && pat + pins == at && u->used + n <= u->cap &&
		    !(ins[0] == ' ' && u->b[u->used - 5] != ' ')) {
			/* the character over the old size -- one byte, or a UTF-8
			   character's two to four -- and a new size after it */
			for (int j = 0; j < n; j++) u->b[u->used - 4 + j] = ins[j];
			wsk__u4(u->b + st + 8, pins + n);
			wsk__u4(u->b + u->used - 4 + n, size + n);
			u->used += n;
			u->end = u->used;
			return;
		}
	}
	u->join = (unsigned char)typing;
	if (need > u->cap) { u->used = u->end = 0; u->join = 0; return; }
	while (u->used + need > u->cap) {        /* the oldest go */
		int first = 16 + wsk__g4(u->b + 4) + wsk__g4(u->b + 8);
		wsk__shift(u->b, first, 0, u->used - first);
		u->used -= first;
	}
	p = u->used;
	wsk__u4(u->b + p, at); wsk__u4(u->b + p + 4, del); wsk__u4(u->b + p + 8, n);
	for (int j = 0; j < del; j++) u->b[p + 12 + j] = txt[at + j];
	for (int j = 0; j < n; j++) u->b[p + 12 + del + j] = ins[j];
	wsk__u4(u->b + p + 12 + del + n, need);
	u->used = u->end = p + need;
}
#endif

/*
 * Every edit the toolkit makes to a buffer: `del` bytes at `at` replaced by
 * the n bytes of `ins` -- recorded for undo first, when the buffer has an
 * undo array. The caller has checked there is room.
 */
static inline void wsk__put(wsk_buf_t *b, int at, int del, const char *ins, int n, int typing)
{
#ifndef WSK_NO_UNDO
	if (b->undo != 0) wsk__urec(b->undo, b->b, at, del, ins, n, typing);
#else
	(void)typing;
#endif
	if (n != del) wsk__shift(b->b, at + del, at + n, b->len - at - del + 1);
	for (int j = 0; j < n; j++) b->b[at + j] = ins[j];
	b->len += n - del;
}

#ifndef WSK_NO_UNDO
/*
 * Undo (back = 1) or redo the next step: the text put back as it was, the
 * caret after what came back. 0 if there was nothing to do, or no room for
 * it -- a buffer the program has since filled -- in which case nothing.
 */
static inline int wsk__undo(wsk_buf_t *b, int back)
{
	wsk_undo_t *u = b->undo;
	int st, at, del, ins, from, to;

	if (u == 0 || (back ? u->used == 0 : u->used == u->end)) return 0;
	st = back ? u->used - wsk__g4(u->b + u->used - 4) : u->used;
	at = wsk__g4(u->b + st); del = wsk__g4(u->b + st + 4); ins = wsk__g4(u->b + st + 8);
	/* undo takes out what was inserted and puts back what was removed */
	from = back ? ins : del;
	to = back ? del : ins;
	if (at + from > b->len || b->len - from + to >= b->cap) return 0;
	if (from != to) wsk__shift(b->b, at + from, at + to, b->len - at - from + 1);
	for (int j = 0; j < to; j++) b->b[at + j] = u->b[st + 12 + (back ? 0 : del) + j];
	b->len += to - from;
	b->caret = at + to;
	b->span = 0;
	u->used = back ? st : st + 16 + del + ins;
	u->join = 0;
	return 1;
}
#endif

/*
 * The character a point is nearest in field k of `lay`, laid out at `whole`:
 * in a memo, on the line under it, with the memo's view kept where it is --
 * b->top is set to the first line shown. Off the field, the nearest end.
 */
static inline int wsk__char_at(const wsk_tree_t *lay, int k, wsk_buf_t *b,
                               wsk_rect_t whole, int x, int y)
{
	int adv = wsk_advance(lay->font), from, c;
#ifndef WSK_NO_MEMO
	if (wsk__lined(lay->item[k].kind)) {
		/* the line under the finger, then the column on it */
#ifndef WSK_NO_SOURCE
		int gut = (lay->item[k].kind == WSK_SOURCE) ? wsk__gutter(lay, b, b->b, b->len) : 0;
		whole.x = (short)(whole.x + gut);
		whole.w = (short)(whole.w - gut);
#endif
		int line = 0, col = 0, lines = 1, top, left, at, end, vis = wsk__vis(lay, whole);
		int wr = wsk__mroom(lay, &lay->item[k], whole, b);
		wsk__where(b->b, b->len, b->caret, wr, &line, &col, &lines);
		top = wsk__top(b, vis, line, lines);
		left = wsk__left(lay, whole, col);
		line = top + (y - (whole.y + WSK_FIELD_PAD)) / wsk__pitch(lay);
		if (line >= lines) line = lines - 1;
		if (line < 0) line = 0;
		at = wsk__line(b->b, b->len, line, wr, &end);
		c = left + (x - (whole.x + WSK_FIELD_PAD) + adv / 2) / adv;
		if (c < 0) c = 0;
		c = at + wsk__byte_of(b->b + at, end - at, c);     /* a column to its byte */
		b->top = top;
		return c;
	}
#endif
	(void)k; (void)y;
	(void)wsk__view(lay, whole, wsk__chars(b->b, b->len), wsk__chars(b->b, b->caret), &from);
	c = from + (x - (whole.x + WSK_FIELD_PAD) + adv / 2) / adv;
	if (c < 0) c = 0;
	return wsk__byte_of(b->b, b->len, c);
}

/*
 * A press on field k: focus, the caret at the character pressed (§5j rule
 * 2), and -- with `extend`, Shift held, on the field that has focus -- the
 * selection from where it was anchored to there (§5s rule 4). Returns n.
 */
static inline int wsk__press_field(wsk_ui_t *ui, const wsk_tree_t *in, int k,
                                   const wsk_tree_t *lay, wsk_rect_t rect, int x, int y,
                                   int extend, int *lo, int *hi,
                                   wsk_note_t *note, int max, int n)
{
	wsk_buf_t *b = wsk__fbuf(lay, &lay->item[k]);
	int changed = ((const void *)&in->item[k] != ui->root->focus);
	int c = wsk__char_at(lay, k, b, wsk_rect(lay, k), x, y);
#ifndef WSK_NO_TEXTSEL
	{
		int anchor = (extend && !changed) ? b->caret + b->span : c;
		b->span = (anchor - c);
		ui->anchor = anchor;
		ui->selecting = 1;
	}
#else
	(void)extend;
#endif
	b->caret = c;
	wsk__focus_now(ui);
	wsk__refocus(ui, in, k, rect, b, lo, hi);
	if (changed) n = wsk__note(note, max, n, WSK_NOTE_FOCUS, in, k, 0);
	return n;
}

#ifndef WSK_NO_MEMO
/* The focused memo's view follows its caret: the buffer's top, written here
   -- caller memory, as the rest of the buffer is -- and only by the toolkit. */
static inline void wsk__follow(wsk_ui_t *ui)
{
	wsk_buf_t *b = ui->focus_b;
	int line = 0, col = 0, lines = 1;
	if (!wsk__lined(ui->focus_in->item[ui->focus_item].kind)) return;
	wsk__where(b->b, b->len, b->caret,
	           wsk__mroom(ui->focus_in, &ui->focus_in->item[ui->focus_item], ui->focus_r, b),
	           &line, &col, &lines);
	b->top = wsk__top(b, wsk__vis(ui->focus_in, ui->focus_r), line, lines);
}
#endif

#ifndef WSK_NO_TEXTSEL
/* A key that moves the caret, and so with Shift extends a selection. */
static inline int wsk__moves(int k)
{
#ifndef WSK_NO_WORDS
	if (k & WSK_MOD_CTRL) k &= ~WSK_MOD_CTRL;     /* Ctrl's word moves too (§5w) */
	else
#endif
	if (k == WSK_KEY_UP || k == WSK_KEY_DOWN) return 1;
	return k >= WSK_KEY_LEFT && k <= WSK_KEY_END;
}

/* Delete the selection: the caret at its start. 1 if there was one. */
static inline int wsk__drop(wsk_buf_t *b)
{
	int s0, s1, m = wsk__selr(b, &s0, &s1);
	b->span = 0;
	if (m == 0) return 0;
	wsk__put(b, s0, m, "", 0, 0);
	b->caret = s0;
	return 1;
}
#endif

#ifndef WSK_NO_WORDS
/*
 * What a byte is to a word (§5w rule 1): 1 inside one -- a letter, a digit,
 * '_', or any byte of a character beyond ASCII, so é and 日本 never split
 * a word; 2 a mark, which makes words of its own; 0 a space; 3 a line
 * break, which Ctrl's moves pass as a space and a double-click does not.
 */
static inline int wsk__wc(char ch)
{
	unsigned char c = (unsigned char)ch;
	if (c == '\n') return 3;
	if (c == ' ') return 0;
	if (c >= 0x80 || c == '_' || (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z'))
		return 1;
	return 2;
}

/* Where Ctrl-Left (back = 1) or Ctrl-Right lands from `at`: over spaces and
   line breaks, then to the far edge of the word there (§5w rule 2). */
static inline int wsk__wmove(const char *b, int len, int at, int back)
{
	int c;
	if (back) {
		while (at > 0 && (wsk__wc(b[at - 1]) == 0 || wsk__wc(b[at - 1]) == 3)) at--;
		if (at == 0) return 0;
		c = wsk__wc(b[at - 1]);
		while (at > 0 && wsk__wc(b[at - 1]) == c) at--;
		return at;
	}
	while (at < len && (wsk__wc(b[at]) == 0 || wsk__wc(b[at]) == 3)) at++;
	if (at == len) return len;
	c = wsk__wc(b[at]);
	while (at < len && wsk__wc(b[at]) == c) at++;
	return at;
}
#endif

#ifndef WSK_NO_NEST
/*
 * Give a field focus by id or path, as a program does when its window
 * opens. Sets y0..y1 to the rows to repaint. WSK_E_NOTFOUND if it is not an
 * editable field.
 */
static inline int wsk_ui_focus(wsk_ui_t *ui, const char *key, int *y0, int *y1)
{
	wsk_tree_t at;
	const wsk_tree_t *in = 0;
	int lo = 32767, hi = -1, i = wsk__lookup(ui->root, key, &at, &in);
	wsk_buf_t *b;
	wsk_rect_t r;

	if (i < 0) return WSK_E_NOTFOUND;
	b = wsk__fbuf(&at, &at.item[i]);
#if !defined(WSK_NO_LISTKEYS)
	/* a list or a world takes focus too, as one stop (§5z) */
	if (b == 0 && !(at.item[i].kind == WSK_STACK && at.nest != 0 && at.item[i].sub >= 0 &&
	                at.item[i].sub < at.nests && (at.nest[at.item[i].sub].select
#ifndef WSK_NO_WORLD
	                || at.nest[at.item[i].sub].overflow == WSK_WORLD
#endif
	   )))
		return WSK_E_NOTFOUND;
#else
	if (b == 0) return WSK_E_NOTFOUND;
#endif
	r = wsk_rect(&at, i);
	if (!wsk__cut(&at, &r)) r.h = 0;
	wsk__focus_now(ui);
	wsk__refocus(ui, in, i, r, b, &lo, &hi);
	*y0 = (lo / WSK_BAND) * WSK_BAND;
	*y1 = wsk__round_band(hi);
	if (*y1 > ui->root->h) *y1 = ui->root->h;
	return WSK_OK;
}
#endif

#ifndef WSK_NO_TEXTSEL
/*
 * Paste: `n` bytes of `text` in place of the focused field's selection, or
 * at its caret (§5s rule 6) -- what a program does with WSK_NOTE_PASTE and
 * what its host's clipboard holds. All or nothing, never trimmed (D8):
 * WSK_E_TEXT for a character the window cannot draw, or a line break in a
 * one-line field; WSK_E_BUDGET if it would not fit; WSK_E_NOTFOUND with no
 * field focused -- and the buffer as it was. Otherwise the notices (a TEXT)
 * and the rows to repaint, as wsk_feed gives them.
 */
static inline int wsk_ui_paste(wsk_ui_t *ui, const char *text, int len,
                               wsk_note_t *note, int max, int *y0, int *y1)
{
	wsk_buf_t *b;
	int s0, s1, sel, memo = 0;

	*y0 = *y1 = 0;
	if (ui->root->focus == 0) return WSK_E_NOTFOUND;
	wsk__focus_now(ui);
	if ((b = ui->focus_b) == 0) return WSK_E_NOTFOUND;
#ifndef WSK_NO_MEMO
	memo = wsk__lined(ui->focus_in->item[ui->focus_item].kind);
#endif
	if (!wsk__ok_n(text, len, ui->root->caps, memo)) return WSK_E_TEXT;
	sel = wsk__selr(b, &s0, &s1);
	if (b->len - sel + len >= b->cap) return WSK_E_BUDGET;
	wsk__put(b, s0, sel, text, len, 0);          /* one step: the selection replaced */
	b->caret = s0 + len;
	b->span = 0;
#ifndef WSK_NO_MEMO
	if (memo) wsk__follow(ui);
#endif
	*y0 = (ui->focus_r.y / WSK_BAND) * WSK_BAND;
	*y1 = wsk__round_band(ui->focus_r.y + ui->focus_r.h);
	if (*y1 > ui->root->h) *y1 = ui->root->h;
	return wsk__note(note, max, 0, WSK_NOTE_TEXT, ui->focus_in, ui->focus_item, b->len);
}
#endif
#endif /* WSK_NO_FIELD */

#ifndef WSK_NO_OVER
/*
 * Open tree `sub` over the window, hanging from the widget `anchor` (an id
 * or a path) -- a menu from its button (§5l rule 4). Any overlay already
 * open is closed first. Sets y0..y1 to the rows to repaint.
 */
static inline int wsk_ui_open(wsk_ui_t *ui, int sub, const char *anchor,
                              int *y0, int *y1)
{
	wsk_tree_t *r = ui->root, at;
	const wsk_tree_t *in = 0;
	int lo = 32767, hi = -1, a0, a1, e, i;
	wsk_rect_t ar;

	/* one overlay at a time, and a dialog's is not to be closed by a menu
	   opening over it: refused (found by review, 2026-11-16) */
	if (r->omodal && r->over != WSK_NONE) { *y0 = *y1 = 0; return WSK_E_KIND; }
#ifndef WSK_NO_TIP
	wsk__tip_close(ui, &lo, &hi);        /* whisker's overlay covers a tip (§5ac) */
#endif
	if (r->over != WSK_NONE && wsk_over_rows(r, &a0, &a1) == WSK_OK) {
		wsk__span(&lo, &hi, a0, a1);
		wsk_over_close(r);
	}
	i = wsk__lookup(r, anchor, &at, &in);
	if (i < 0) return WSK_E_NOTFOUND;
	ar = wsk_rect(&at, i);
	(void)wsk__cut(&at, &ar);
	if ((e = wsk_over_open(r, sub, ar)) != WSK_OK) return e;
	if (wsk_over_rows(r, &a0, &a1) == WSK_OK) wsk__span(&lo, &hi, a0, a1);
	*y0 = lo; *y1 = hi;
	return WSK_OK;
}

/*
 * Open tree `sub` at a point -- a context menu where the pointer was, for
 * WSK_NOTE_MENU (§5s rule 1): §5l's placing, the point for an anchor.
 */
static inline int wsk_ui_open_at(wsk_ui_t *ui, int sub, int x, int y, int *y0, int *y1)
{
	wsk_tree_t *r = ui->root;
	int lo = 32767, hi = -1, a0, a1, e;
	wsk_rect_t ar;

	/* one overlay at a time, and a dialog's is not to be closed by a menu
	   opening over it: refused (found by review, 2026-11-16) */
	if (r->omodal && r->over != WSK_NONE) { *y0 = *y1 = 0; return WSK_E_KIND; }
#ifndef WSK_NO_TIP
	wsk__tip_close(ui, &lo, &hi);        /* whisker's overlay covers a tip (§5ac) */
#endif
	if (r->over != WSK_NONE && wsk_over_rows(r, &a0, &a1) == WSK_OK) {
		wsk__span(&lo, &hi, a0, a1);
		wsk_over_close(r);
	}
	ar.x = (short)x; ar.y = (short)y; ar.w = 0; ar.h = 0;
	if ((e = wsk_over_open(r, sub, ar)) != WSK_OK) return e;
	if (wsk_over_rows(r, &a0, &a1) == WSK_OK) wsk__span(&lo, &hi, a0, a1);
	*y0 = lo; *y1 = hi;
	return WSK_OK;
}

/* Close the open dialog, and give focus back to what had it (§5ae): the
   rows to repaint. 0 rows if no dialog is open. */
static inline int wsk_ui_dialog_close(wsk_ui_t *ui, int *y0, int *y1)
{
	wsk_tree_t *r = ui->root;
	int lo = 32767, hi = -1, a0, a1;
	*y0 = *y1 = 0;
	if (r->over == WSK_NONE || !r->omodal) return WSK_OK;
	if (wsk_over_rows(r, &a0, &a1) == WSK_OK) wsk__span(&lo, &hi, a0, a1);
	wsk__press_end(ui, &lo, &hi);       /* a drag begun in it ends with it */
	wsk_over_close(r);
	r->focus = ui->dfocus;
	ui->dfocus = 0;
	ui->focus_in = 0; ui->focus_item = -1; ui->focus_b = 0;
#ifndef WSK_NO_FIELD
	wsk__focus_now(ui);
#endif
	if (r->focus != 0) wsk__span(&lo, &hi, ui->focus_r.y, ui->focus_r.y + ui->focus_r.h);
	*y0 = lo; *y1 = hi;
	return WSK_OK;
}

/*
 * Open tree `sub` as a dialog (§5ae): w x h, centred, cut to the screen, and
 * modal -- nothing off it answers, and the keys are its own. Focus is kept
 * to give back when it closes, by Escape or wsk_ui_dialog_close. The rows to
 * repaint; WSK_E_KIND for no such tree; the tree refused as any would be.
 */
static inline int wsk_ui_dialog(wsk_ui_t *ui, int sub, int w, int h, int *y0, int *y1)
{
	wsk_tree_t *r = ui->root;
	int lo = 32767, hi = -1, a0, a1, e;

	*y0 = *y1 = 0;
	if (r->nest == 0 || sub < 0 || sub >= r->nests) return WSK_E_KIND;
#ifndef WSK_NO_TIP
	wsk__tip_close(ui, &lo, &hi);
#endif
	/* a press on the window, or on a dialog this one replaces, ends here:
	   its lifting must not click beneath a dialog (found by review) */
	wsk__press_end(ui, &lo, &hi);
	if (r->over != WSK_NONE) {
		if (wsk_over_rows(r, &a0, &a1) == WSK_OK) wsk__span(&lo, &hi, a0, a1);
		if (r->omodal) {
			(void)wsk_ui_dialog_close(ui, &a0, &a1);
			if (a1 > a0) wsk__span(&lo, &hi, a0, a1);
		} else wsk_over_close(r);
	}
	if (w > r->w) w = r->w;
	if (h > r->h) h = r->h;
	r->over = (short)sub;
	r->ox = (short)((r->w - w) / 2); r->oy = (short)((r->h - h) / 2);
	r->ow = (short)w; r->oh = (short)h;
	r->omodal = 1;
#ifndef WSK_NO_MENUKEYS
	r->ohi = WSK_NONE;
#endif
	if ((e = wsk_check(r, 0)) != WSK_OK) {
		wsk_over_close(r);
		*y0 = lo; *y1 = hi;                /* what was closed still repaints */
		return e;
	}
	/* focus is the dialog's now; what had it is kept */
	ui->dfocus = r->focus;
	if (r->focus != 0) wsk__span(&lo, &hi, ui->focus_r.y, ui->focus_r.y + ui->focus_r.h);
	r->focus = 0;
	ui->focus_in = 0; ui->focus_item = -1; ui->focus_b = 0;
	if (wsk_over_rows(r, &a0, &a1) == WSK_OK) wsk__span(&lo, &hi, a0, a1);
	*y0 = lo; *y1 = hi;
	return WSK_OK;
}
#endif

#ifndef WSK_NO_WORLD
/*
 * The deepest world under a screen point, at any depth, and the world point
 * there (step 3): what a program asks when a click lands on bare floor.
 * `world` is the caller's own tree. WSK_E_NOTFOUND where no world is.
 */
static inline int wsk_world_point(const wsk_tree_t *root, int x, int y,
                                  const wsk_tree_t **world, int *wx, int *wy)
{
	wsk_tree_t at;
	wsk_tree_t *f = wsk__scroller(root, (wsk_tree_t *)root, x, y, &at, 0);
	if (f == 0 || at.overflow != WSK_WORLD) return WSK_E_NOTFOUND;
	if (world != 0) *world = f;
	return wsk_world_at(&at, x, y, wx, wy);
}

/*
 * A world's view set to vx, vy at zoom z (§5q step 3 rule 3): zoomed no
 * further out than its whole world, a bounded view kept inside it, a
 * torus's wrapped. Written to the caller's tree and the laid-out copy; 1 if
 * anything moved.
 */
/* A zoom the toolkit may set: 1/16 to 16, and no further out than a
   bounded world or torus whole -- unless even 16 cannot fill the view. */
static inline int wsk__zclamp(const wsk_tree_t *at, int z)
{
	if (z < 16) z = 16;
	if (at->wspace != WSK_OPEN && at->ww > 0 && at->wh > 0) {
		int zmin = (at->aw * 256 + at->ww - 1) / at->ww, zy = (at->ah * 256 + at->wh - 1) / at->wh;
		if (zy > zmin) zmin = zy;
		if (z < zmin) z = zmin;
	}
	if (z > 4096) z = 4096;
	return z;
}

static inline int wsk__view_to(wsk_tree_t *orig, wsk_tree_t *at, int vx, int vy, int z)
{
	z = wsk__zclamp(at, z);
	if (at->wspace != WSK_OPEN && (at->ww <= 0 || at->wh <= 0)) return 0;
	if (at->wspace == WSK_BOUNDED) {
		int mx = at->ww - at->aw * 256 / z, my = at->wh - at->ah * 256 / z;
		if (vx > mx) vx = mx;
		if (vy > my) vy = my;
		if (vx < 0) vx = 0;
		if (vy < 0) vy = 0;
	} else if (at->wspace == WSK_TORUS) {
		vx = wsk__wrap(vx, at->ww);
		vy = wsk__wrap(vy, at->wh);
	}
	if (vx == orig->vx && vy == orig->vy && z == orig->zoom) return 0;
	orig->vx = at->vx = vx;
	orig->vy = at->vy = vy;
	orig->zoom = at->zoom = (short)z;
	return 1;
}

/* The rows item i of a laid-out world shows on: a torus's whole view, since
   its copies may be anywhere in it; else its rectangle, cut (step 6). */
static inline void wsk__wrows(const wsk_tree_t *at, int i, int *lo, int *hi)
{
	wsk_rect_t v = wsk__wrect(at, i);
	int a0, a1;
	if (at->wspace == WSK_TORUS) {
		if (wsk_area_rows(at, &a0, &a1) == WSK_OK) wsk__span(lo, hi, a0, a1);
	} else if (wsk__cut(at, &v))
		wsk__span(lo, hi, v.y, v.y + v.h);
}

/*
 * The item a drag holds to x, y in the world (step 6 rule 2): kept inside
 * a bounded world, wrapped on a torus. Its rows before and after to
 * repaint. 1 if it moved.
 */
static inline int wsk__item_to(wsk_ui_t *ui, int x, int y, int *lo, int *hi)
{
	const wsk_tree_t *at = &ui->drag_at;
	wsk_place_t *p = &ui->drag->place[ui->witem];
	if (at->wspace == WSK_BOUNDED) {
		int mx = at->ww - (int)(((long long)p->w * p->scale) >> 8);
		int my = at->wh - (int)(((long long)p->h * p->scale) >> 8);
		if (x > mx) x = mx;
		if (y > my) y = my;
		if (x < 0) x = 0;
		if (y < 0) y = 0;
	} else if (at->wspace == WSK_TORUS) {
		x = wsk__wrap(x, at->ww);
		y = wsk__wrap(y, at->wh);
	}
	if (x == p->x && y == p->y) return 0;
	wsk__wrows(at, ui->witem, lo, hi);
	p->x = x;
	p->y = y;
	wsk__wrows(at, ui->witem, lo, hi);
	return 1;
}
#endif

#ifndef WSK_NO_WORLD
/*
 * Keep item `item` of a world in view (§5q step 5): at least `margin`
 * percent of the view from every edge (50, centred), moving `ease`/256 of
 * the way this call (256, at once; at least a unit). `world` is its id or
 * path, or 0 for a root that is itself the world. 1 if the view moved, with
 * y0..y1 the rows to repaint; 0 if not; WSK_E_NOTFOUND if no such world.
 */
static inline int wsk_world_follow(wsk_tree_t *root, const char *world, int item,
                                   int margin, int ease, int *y0, int *y1)
{
	wsk_tree_t at, c, *o;
	int i, vw, vh, dx, dy, iw, ih, sx, sy;
	*y0 = *y1 = 0;
	if (world == 0) { wsk__tcopy(&c, root); o = root; }
	else {
		if ((i = wsk__lookup(root, world, &at, 0)) < 0 || at.item[i].kind != WSK_STACK ||
		    !wsk__child(&at, i, &c))
			return WSK_E_NOTFOUND;
		o = (wsk_tree_t *)&at.nest[at.item[i].sub];
	}
	if (c.overflow != WSK_WORLD || c.place == 0 || c.zoom <= 0 || item < 0 || item >= c.n)
		return WSK_E_NOTFOUND;
	if (margin < 0) margin = 0;
	if (margin > 50) margin = 50;
	if (ease < 1) ease = 1;
	if (ease > 256) ease = 256;
	vw = c.aw * 256 / c.zoom; vh = c.ah * 256 / c.zoom;
	iw = (int)(((long long)c.place[item].w * c.place[item].scale) >> 8);
	ih = (int)(((long long)c.place[item].h * c.place[item].scale) >> 8);
	dx = c.place[item].x - c.vx;
	dy = c.place[item].y - c.vy;
	if (c.wspace == WSK_TORUS) {               /* the short way round the seam */
		dx = wsk__wrap(dx + c.ww / 2, c.ww) - c.ww / 2;
		dy = wsk__wrap(dy + c.wh / 2, c.wh) - c.wh / 2;
	}
	/* how far the view must move along each axis to keep the margin */
	{
		int lo = vw * margin / 100, hi = vw - lo - iw;
		sx = (margin >= 50 || hi < lo) ? dx - (vw - iw) / 2 : (dx < lo) ? dx - lo : (dx > hi) ? dx - hi : 0;
		lo = vh * margin / 100; hi = vh - lo - ih;
		sy = (margin >= 50 || hi < lo) ? dy - (vh - ih) / 2 : (dy < lo) ? dy - lo : (dy > hi) ? dy - hi : 0;
	}
	if (sx != 0) { int e = sx * ease / 256; sx = (e != 0) ? e : (sx > 0 ? 1 : -1); }
	if (sy != 0) { int e = sy * ease / 256; sy = (e != 0) ? e : (sy > 0 ? 1 : -1); }
	if ((sx == 0 && sy == 0) || !wsk__view_to(o, &c, c.vx + sx, c.vy + sy, c.zoom)) return 0;
	if (wsk_area_rows(&c, y0, y1) != WSK_OK) *y0 = *y1 = 0;
	return 1;
}
#endif

#ifndef WSK_NO_WORLD
/*
 * The rows item i of a world covers in this window now (§5ab step 3): cut by
 * the view -- a torus's whole view, since its copies may be anywhere -- and
 * band-snapped, as wsk_feed's rows are. For a program that moved something
 * and would repaint only where it was and where it is. `world` as
 * wsk_world_follow takes it. 1 with y0..y1 if it shows; 0, and nothing to
 * repaint, if not; WSK_E_NOTFOUND if there is no such world or item.
 */
static inline int wsk_world_rows(wsk_tree_t *root, const char *world, int item,
                                 int *y0, int *y1)
{
	wsk_tree_t at, c;
	int i, lo = 32767, hi = -1;
	*y0 = *y1 = 0;
	if (world == 0) wsk__tcopy(&c, root);
	else if ((i = wsk__lookup(root, world, &at, 0)) < 0 || at.item[i].kind != WSK_STACK ||
	         !wsk__child(&at, i, &c))
		return WSK_E_NOTFOUND;
	if (c.overflow != WSK_WORLD || c.place == 0 || item < 0 || item >= c.n) return WSK_E_NOTFOUND;
	wsk__wrows(&c, item, &lo, &hi);
	if (hi <= lo) return 0;
	lo = (lo / WSK_BAND) * WSK_BAND;
	hi = wsk__round_band(hi);
	if (lo < 0) lo = 0;
	if (hi > root->h) hi = root->h;
	*y0 = lo;
	*y1 = hi;
	return 1;
}

/*
 * Set a world's view as a drag or the wheel would (§5ab step 2): the zoom
 * clamped -- no further out than a bounded world or torus whole -- the view
 * kept inside a bounded world, wrapped on a torus. For a program that moves
 * the view itself, or resizes the window a bounded world is seen through
 * and must not be left looking past its edge. `world` as wsk_world_follow
 * takes it. 1 if the view changed, with y0..y1 the rows to repaint; 0 if
 * not; WSK_E_NOTFOUND if there is no such world.
 */
static inline int wsk_world_view(wsk_tree_t *root, const char *world, int vx, int vy,
                                 int zoom, int *y0, int *y1)
{
	wsk_tree_t at, c, *o;
	int i;
	*y0 = *y1 = 0;
	if (world == 0) { wsk__tcopy(&c, root); o = root; }
	else {
		if ((i = wsk__lookup(root, world, &at, 0)) < 0 || at.item[i].kind != WSK_STACK ||
		    !wsk__child(&at, i, &c))
			return WSK_E_NOTFOUND;
		o = (wsk_tree_t *)&at.nest[at.item[i].sub];
	}
	if (c.overflow != WSK_WORLD || c.place == 0) return WSK_E_NOTFOUND;
	if (!wsk__view_to(o, &c, vx, vy, zoom)) return 0;
	if (wsk_area_rows(&c, y0, y1) != WSK_OK) *y0 = *y1 = 0;
	return 1;
}
#endif

#ifndef WSK_NO_SPLIT
/*
 * A held divider moved `d` pixels along its stack (§5x rules 3 and 4): the
 * pane it moves resized in the caller's `sizes`, clamped so it and any
 * weighted rest keep two lines of the font. Returns the pane's new extent,
 * or -1 if nothing changed; *pane is which pane.
 */
static inline int wsk__split_move(wsk_ui_t *ui, int d, int *pane)
{
	const wsk_tree_t *t = &ui->split_at;
	int k = ui->split_k, a = k - 1, b = k + 1, min = 2 * t->font;
	int fa = (wsk__wt(t, a) == 0), fb = (wsk__wt(t, b) == 0);
	int span = (t->axis == WSK_COL ? t->ah : t->aw) - 2 * t->pad, others = 0, rest = 0;
	int x, e, x0, lo, hi;

	/* which pane: the one before, unless it shares by weight and the one
	   after is fixed; between two fixed panes, both */
	x = (!fa && fb) ? b : a;
	x0 = ui->split0[x == a ? 0 : 1];
	e = (x == a) ? x0 + d : x0 - d;
	if (fa && fb) {
		/* the boundary moves, the pair's total kept */
		lo = min;
		hi = ui->split0[0] + ui->split0[1] - min;
	} else {
		for (int i = 0; i < t->n; i++) {
			if (i != x) others += wsk__natural(t, i);
			if (i != x && wsk__wt(t, i) > 0) rest = 1;
		}
		if (t->n > 1) others += (t->n - 1) * t->gap;
		lo = min;
		hi = span - others - (rest ? min : 0);
	}
	if (hi < lo) return -1;               /* no room to move at all: leave it */
	if (e > hi) e = hi;
	if (e < lo) e = lo;
	if (t->sizes[x] == e) return -1;
	if (fa && fb) {
		t->sizes[a] = (short)e;
		t->sizes[b] = (short)(ui->split0[0] + ui->split0[1] - e);
	} else
		t->sizes[x] = (short)e;
	*pane = x;
	return e;
}
#endif

#if !defined(WSK_NO_LISTKEYS)
/*
 * A key for the focused list or world (§5z): a list's Up, Down, Home and
 * End move its choice, kept in sight, and Enter clicks the chosen row; a
 * world's arrows pan an eighth of the view, + and - zoom about its middle.
 * 1 if the key was theirs.
 */
static inline int wsk__stackkeys(wsk_ui_t *ui, const wsk_event_t *ev, wsk_note_t *note,
                                 int max, int *n, int *lo, int *hi)
{
	wsk_tree_t par, c, *o;
	const wsk_widget_t *w = (const wsk_widget_t *)ui->root->focus;
	const wsk_tree_t *orig, *scope = wsk__keys_in(ui->root, &c, &orig);
	int k = wsk__lay_of(scope, w, &par, 0), key = (ev->kind == WSK_EV_KEY) ? ev->key : 0;
	int a0, a1;

	if (k < 0 || w->kind != WSK_STACK || !wsk__child(&par, k, &c)) return 0;
	o = (wsk_tree_t *)&par.nest[w->sub];         /* the caller's: writable (§5i) */
#ifndef WSK_NO_WORLD
	if (c.overflow == WSK_WORLD) {
		int z = c.zoom, nz = z, vx = c.vx, vy = c.vy;
		int sx = c.aw * 256 / z / 8, sy = c.ah * 256 / z / 8;
		char t0 = (ev->kind == WSK_EV_TEXT) ? ev->text[0] : 0;
		if (key == WSK_KEY_LEFT) vx -= sx;
		else if (key == WSK_KEY_RIGHT) vx += sx;
		else if (key == WSK_KEY_UP) vy -= sy;
		else if (key == WSK_KEY_DOWN) vy += sy;
		else if (t0 == '+' || t0 == '=' || t0 == '-') {
			/* about the middle of the view: that world point stays put */
			int cx = vx + (c.aw / 2) * 256 / z, cy = vy + (c.ah / 2) * 256 / z;
			nz = wsk__zclamp(&c, (t0 == '-') ? z * 4 / 5 : z * 5 / 4);
			vx = cx - (c.aw / 2) * 256 / nz;
			vy = cy - (c.ah / 2) * 256 / nz;
		} else
			return 0;
		if (wsk__view_to(o, &c, vx, vy, nz)) {
			if (wsk_area_rows(&c, &a0, &a1) == WSK_OK) wsk__span(lo, hi, a0, a1);
			*n = wsk__note(note, max, *n, WSK_NOTE_VIEW, o, -1, o->zoom);
		}
		return 1;
	}
#endif
	if (c.select && c.n > 0) {
		int sel = (c.sel >= 0 && c.sel < c.n) ? c.sel : -1, ns;
		if (key == WSK_KEY_ENTER) {
			if (sel >= 0) *n = wsk__note(note, max, *n, WSK_NOTE_CLICK, o, sel, -1);
			return 1;
		}
		if (key == WSK_KEY_UP) ns = (sel <= 0) ? 0 : sel - 1;
		else if (key == WSK_KEY_DOWN) ns = (sel < 0) ? 0 : (sel + 1 < c.n) ? sel + 1 : sel;
		else if (key == WSK_KEY_HOME) ns = 0;
		else if (key == WSK_KEY_END) ns = c.n - 1;
		else return 0;
		if (ns != sel) {
			o->sel = c.sel = (short)ns;
#ifndef WSK_NO_SCROLL
			/* a list that scrolls keeps its choice whole in sight */
			if (c.overflow == WSK_SCROLL) {
				wsk_rect_t b = wsk_rect(&c, ns);
				int col = (c.axis == WSK_COL), at = col ? b.y : b.x, len = col ? b.h : b.w;
				int s0 = (col ? c.ay : c.ax) + c.pad, s1 = (col ? c.ay + c.ah : c.ax + c.aw) - c.pad;
				int to = c.scroll;
				if (at < s0) to -= s0 - at;
				else if (at + len > s1) to += at + len - s1;
				if (to < 0) to = 0;
				if (to > wsk_scroll_max(&c)) to = wsk_scroll_max(&c);
				o->scroll = c.scroll = (short)to;
			}
#endif
			if (wsk_area_rows(&c, &a0, &a1) == WSK_OK) wsk__span(lo, hi, a0, a1);
			*n = wsk__note(note, max, *n, WSK_NOTE_SELECT, o, ns, ns);
		}
		return 1;
	}
	return 0;
}
#endif

/*
 * One event in; the notices of what changed out, and the rows to repaint
 * (design.md §5i). Returns how many things changed -- more than `max` were
 * written if it exceeds it -- and sets y0..y1 to a band-snapped range, or
 * y0 == y1 when nothing needs drawing.
 *
 *   press   DOWN on a button holds it; UP on the widget DOWN was on is a
 *           CLICK, on anything -- a label, a word -- and the program decides
 *           what it means. UP elsewhere is nothing.
 *   drag    DOWN in a scrolling stack arms it; a MOVE past `slop` along its
 *           axis starts it, lets go of any hold, and from then the stack
 *           follows the finger, clamped here so that layer 1, which refuses
 *           out of range (D11), is never asked. Each step is a SCROLL. An UP
 *           after a drag is not a click.
 *   wheel   scrolls the deepest scrolling stack under the point by d.
 */
#ifndef WSK_NO_WORLD
/* The square root of v, by bits: no sqrt, no division (§5ad). */
static inline int wsk__isqrt(unsigned int v)
{
	unsigned int r = 0, b = 1u << 30;
	while (b > v) b >>= 2;
	while (b != 0) {
		if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
		else r >>= 1;
		b >>= 2;
	}
	return (int)r;
}

/* The two fingers' distance, and the world point under their midpoint at
   zoom z, from the world as the first finger's press laid it out. */
static inline int wsk__pdist(const wsk_ui_t *ui)
{
	int dx = ui->gx - ui->fx, dy = ui->gy - ui->fy, d;
	d = wsk__isqrt((unsigned int)(dx * dx + dy * dy));
	return (d < 1) ? 1 : d;
}

/*
 * A pinch (§5ad): a finger's event, if it is a pinch's to take -- 1, and
 * wsk_feed goes no further -- or 0, for the first finger alone, which
 * presses and drags as it always has. A second finger down on the world the
 * first one's press found starts it: the first's press let go, and the
 * distance, zoom and world point under the midpoint kept. Either moving
 * zooms by the distance and keeps that world point under the midpoint, so a
 * pinch pans too; either lifting ends it, the other inert till it lifts.
 */
static inline int wsk__pinch(wsk_ui_t *ui, const wsk_event_t *ev, wsk_note_t *note,
                             int max, int *n, int *lo, int *hi)
{
	wsk_tree_t *r = ui->root;
	int x = ev->x, y = ev->y;
	if (ev->kind != WSK_EV_DOWN && ev->kind != WSK_EV_MOVE && ev->kind != WSK_EV_UP) return 0;
	if (!ui->down) {
		ui->pinch = 0;
		if (ev->kind == WSK_EV_DOWN) { ui->f0 = ev->finger; ui->fx = (short)x; ui->fy = (short)y; }
		return 0;
	}
	if (ui->pinch == 0) {
		wsk_rect_t z = { 0, 0, 0, 0 };
		const wsk_tree_t *at = &ui->drag_at;
		if (ev->finger == ui->f0) {
			if (ev->kind == WSK_EV_MOVE) { ui->fx = (short)x; ui->fy = (short)y; }
			return 0;
		}
		/* another finger: its moving and lifting are nothing; its press, a
		   second on the first one's world, a pinch -- anywhere else, a new
		   press, the first let go: a press is one finger's, and a lifting
		   lost (a browser's cancelled touch) must not freeze every press
		   after it (found by review, 2026-11-16) */
		if (ev->kind != WSK_EV_DOWN) return 1;
		if (ui->drag == 0 || at->overflow != WSK_WORLD || at->zoom <= 0 ||
		    x < at->ax || x >= at->ax + at->aw || y < at->ay || y >= at->ay + at->ah) {
			wsk__press_end(ui, lo, hi);
			ui->f0 = ev->finger; ui->fx = (short)x; ui->fy = (short)y;
			return 0;
		}
		ui->f1 = ev->finger;
		ui->gx = (short)x; ui->gy = (short)y;
		/* the first finger's press is let go: no click, no drag, no hold */
		ui->hit = -1;
		ui->dragging = 1;
		ui->witem = -1;
		if (r->held != 0) {
			r->held = 0;
			wsk__span(lo, hi, ui->held.y, ui->held.y + ui->held.h);
		}
#ifndef WSK_NO_TIP
		wsk__tip_rest(ui, 0, -1, z, 0, lo, hi);
#endif
#ifndef WSK_NO_TEXTSEL
		ui->selecting = 0;
#endif
#ifndef WSK_NO_SPLIT
		ui->split = 0;
#endif
		(void)z;
		ui->pd0 = wsk__pdist(ui);
		ui->pz0 = at->zoom;
		ui->pwx = at->vx + ((ui->fx + ui->gx) / 2 - at->ax) * 256 / at->zoom;
		ui->pwy = at->vy + ((ui->fy + ui->gy) / 2 - at->ay) * 256 / at->zoom;
		ui->pinch = 1;
		return 1;
	}
	if (ui->pinch == 1) {
		int z, mx, my;
		if (ev->finger != ui->f0 && ev->finger != ui->f1) {
			if (ev->kind != WSK_EV_DOWN) return 1;
			/* a third finger's press: the pinch ends, and it presses --
			   what the two were doing cannot be told from a lost lifting */
			wsk__press_end(ui, lo, hi);
			ui->f0 = ev->finger; ui->fx = (short)x; ui->fy = (short)y;
			return 0;
		}
		if (ev->kind == WSK_EV_DOWN) return 1;
		if (ev->kind == WSK_EV_UP) {
			ui->fleft = (ev->finger == ui->f0) ? ui->f1 : ui->f0;
			ui->pinch = 2;
			return 1;
		}
		if (ev->finger == ui->f0) { ui->fx = (short)x; ui->fy = (short)y; }
		else { ui->gx = (short)x; ui->gy = (short)y; }
		z = ui->pz0 * wsk__pdist(ui) / ui->pd0;
		z = wsk__zclamp(&ui->drag_at, z);
		mx = (ui->fx + ui->gx) / 2;
		my = (ui->fy + ui->gy) / 2;
		if (wsk__view_to(ui->drag, &ui->drag_at, ui->pwx - (mx - ui->drag_at.ax) * 256 / z,
		                 ui->pwy - (my - ui->drag_at.ay) * 256 / z, z)) {
			int a0, a1;
			if (wsk_area_rows(&ui->drag_at, &a0, &a1) == WSK_OK) wsk__span(lo, hi, a0, a1);
			*n = wsk__note(note, max, *n, WSK_NOTE_VIEW, ui->drag, -1, ui->drag->zoom);
		}
		return 1;
	}
	/* one lifted: the other is inert until it lifts too -- and a press, if
	   its lifting was lost, starts afresh rather than waiting for ever */
	if (ev->kind == WSK_EV_DOWN) {
		ui->down = 0;
		ui->dragging = 0;
		ui->pinch = 0;
		ui->f0 = ev->finger; ui->fx = (short)x; ui->fy = (short)y;
		return 0;
	}
	if (ev->kind == WSK_EV_UP && ev->finger == ui->fleft) {
		ui->down = 0;
		ui->dragging = 0;
		ui->pinch = 0;
		ui->hit = -1;
	}
	return 1;
}
#endif

static inline int wsk_feed(wsk_ui_t *ui, const wsk_event_t *ev,
                           wsk_note_t *note, int max, int *y0, int *y1)
{
	wsk_tree_t *r = ui->root;
	int n = 0, lo = 32767, hi = -1, x = ev->x, y = ev->y, used = 0, ate = 0;

#ifndef WSK_NO_TIP
	/* a press, a key, typing, the wheel, a menu: any tip closes and the
	   rest ends (§5ac rule 5) -- a finger's press and Tab start another */
	if (ev->kind == WSK_EV_DOWN || ev->kind == WSK_EV_KEY || ev->kind == WSK_EV_TEXT ||
	    ev->kind == WSK_EV_WHEEL || ev->kind == WSK_EV_MENU) {
		wsk_rect_t z = { 0, 0, 0, 0 };
		wsk__tip_rest(ui, 0, -1, z, 0, &lo, &hi);
	}
#endif
#ifndef WSK_NO_OVER
	/* A dialog is modal (§5ae): a press, a right-click or the wheel off it
	   does nothing at all -- a stray tap must not lose what was typed in it
	   -- and the pointer resting off it rests on nothing. */
	if (r->over != WSK_NONE && r->omodal && !wsk__on_over(r, x, y)) {
		if (ev->kind == WSK_EV_HOVER) x = y = -1;
		else if (ev->kind == WSK_EV_DOWN || ev->kind == WSK_EV_MENU || ev->kind == WSK_EV_WHEEL)
			goto rows;          /* and a press already on the dialog goes on */
	}
#endif
#ifndef WSK_NO_WORLD
	/* a second finger is a pinch's, and goes no further (§5ad) */
	if (wsk__pinch(ui, ev, note, max, &n, &lo, &hi)) goto rows;
#endif

#ifndef WSK_NO_OVER
	/* A press off an open overlay closes it, and goes no further: the tap
	   that dismisses a menu must not also press what is under it (§5l). */
	if (ev->kind == WSK_EV_DOWN && r->over != WSK_NONE && !wsk__on_over(r, x, y)) {
		const wsk_tree_t *was = &r->nest[r->over];
		int a0, a1;
		if (wsk_over_rows(r, &a0, &a1) == WSK_OK) wsk__span(&lo, &hi, a0, a1);
		wsk_over_close(r);
		n = wsk__note(note, max, n, WSK_NOTE_CLOSE, was, -1, 0);
		ui->down = 0;
	} else if (ev->kind == WSK_EV_KEY && ev->key == WSK_KEY_ESCAPE && r->over != WSK_NONE) {
		const wsk_tree_t *was = &r->nest[r->over];
		int a0, a1;
		if (wsk_over_rows(r, &a0, &a1) == WSK_OK) wsk__span(&lo, &hi, a0, a1);
		if (r->omodal) {                             /* focus back (§5ae) */
			(void)wsk_ui_dialog_close(ui, &a0, &a1);
			if (a1 > a0) wsk__span(&lo, &hi, a0, a1);
		} else wsk_over_close(r);
		n = wsk__note(note, max, n, WSK_NOTE_CLOSE, was, -1, 0);
	}
#ifndef WSK_NO_MENUKEYS
	/* An open menu's keys are its own (§5y): a highlight walks its
	   buttons, and Enter chooses as a click would. */
	else if (ev->kind == WSK_EV_KEY && r->over != WSK_NONE && !r->omodal &&
	         (ev->key == WSK_KEY_UP || ev->key == WSK_KEY_DOWN || ev->key == WSK_KEY_HOME ||
	          ev->key == WSK_KEY_END || ev->key == WSK_KEY_ENTER)) {
		/* the menu's own tree, which scrolls: writable, as every tree the
		   toolkit changes is the caller's (§5i) */
		wsk_tree_t c, *o = (wsk_tree_t *)&r->nest[r->over];
		int a0, a1, k = r->ohi;
		used = 1;
		if (wsk__over_tree(r, &c)) {
			if (ev->key == WSK_KEY_ENTER) {
				if (k >= 0 && k < c.n && c.item[k].kind == WSK_BUTTON) {
					n = wsk__note(note, max, n, WSK_NOTE_CLICK, o, k, -1);
					if (wsk_over_rows(r, &a0, &a1) == WSK_OK) wsk__span(&lo, &hi, a0, a1);
					wsk_over_close(r);
					n = wsk__note(note, max, n, WSK_NOTE_CLOSE, o, -1, 0);
				}
			} else {
				/* the next button that way, wrapping; Home and End from
				   outside the ends, so the first and last are found */
				int up = (ev->key == WSK_KEY_UP || ev->key == WSK_KEY_END);
				int at = (ev->key == WSK_KEY_HOME) ? -1 : (ev->key == WSK_KEY_END) ? c.n
				       : (k >= 0 && k < c.n) ? k : (up ? c.n : -1);
				for (int step = 0; step < c.n; step++) {
					at += up ? -1 : 1;
					if (at < 0) at = c.n - 1;
					if (at >= c.n) at = 0;
					if (c.item[at].kind == WSK_BUTTON) break;
				}
				if (at >= 0 && at < c.n && c.item[at].kind == WSK_BUTTON && at != k) {
					r->ohi = (short)at;
#ifndef WSK_NO_SCROLL
					/* a menu that scrolls keeps it whole in sight (rule 5) */
					if (c.overflow == WSK_SCROLL) {
						wsk_rect_t b = wsk_rect(&c, at);
						int top = c.ay + c.pad, bot = c.ay + c.ah - c.pad;
						if (b.y < top) o->scroll = (short)(o->scroll - (top - b.y));
						else if (b.y + b.h > bot) o->scroll = (short)(o->scroll + (b.y + b.h - bot));
					}
#endif
					if (wsk_over_rows(r, &a0, &a1) == WSK_OK) wsk__span(&lo, &hi, a0, a1);
				}
			}
		}
	}
#endif
	else
#endif
	if (ev->kind == WSK_EV_MENU) {
		/* A context menu is asked for (§5s rule 1): the program is told
		   what is under the point, and opens a menu there if it has one. */
		const wsk_tree_t *in = 0, *lay = r;
		wsk_tree_t at;
		wsk_rect_t rect = { 0, 0, 0, 0 };
		int word = -1, k;
		/* asked by a finger still down -- a phone's long press -- the press
		   was for the menu, and its lifting is not also a click (§5ac) */
		if (ui->down) {
			ui->hit = -1;
#ifndef WSK_NO_WORLD
			ui->witem = -1;
#endif
#ifndef WSK_NO_GRAB
			ui->gin = 0;                   /* nor a drop: a grab ends here, */
			ui->grabbing = 0;              /* as it ends under a dialog */
#endif
			if (r->held != 0) {
				r->held = 0;
				wsk__span(&lo, &hi, ui->held.y, ui->held.y + ui->held.h);
			}
		}
#ifndef WSK_NO_OVER
		if (r->over != WSK_NONE && !wsk__on_over(r, x, y)) {
			const wsk_tree_t *was = &r->nest[r->over];
			int a0, a1;
			if (wsk_over_rows(r, &a0, &a1) == WSK_OK) wsk__span(&lo, &hi, a0, a1);
			wsk_over_close(r);
			n = wsk__note(note, max, n, WSK_NOTE_CLOSE, was, -1, 0);
		}
#endif
		if (ui->down) {                  /* a long press: let it go */
			ui->down = 0;
			ui->dragging = 0;
			ui->drag = 0;
			if (r->held != 0) {
				r->held = 0;
				wsk__span(&lo, &hi, ui->held.y, ui->held.y + ui->held.h);
			}
		}
		k = wsk__ui_hit(r, x, y, &in, &rect, &word, &at, &lay);
#ifndef WSK_NO_FIELD
		/* a field without focus takes it, so a Paste goes there; the
		   focused one keeps its caret and selection, for a Copy */
		if (k >= 0 && wsk__fbuf(lay, &lay->item[k]) != 0 &&
		    (const void *)&in->item[k] != r->focus)
			n = wsk__press_field(ui, in, k, lay, rect, x, y, 0, &lo, &hi, note, max, n);
#endif
#ifndef WSK_NO_TEXTSEL
		ui->selecting = 0;
#endif
		n = wsk__note(note, max, n, WSK_NOTE_MENU, (k >= 0) ? in : r, (k >= 0) ? k : -1, word);
#ifndef WSK_NO_HOVER
	} else if (ev->kind == WSK_EV_HOVER) {
		/* what the pointer rests on (§5aa): said when it changes, drawn
		   never -- what hover means is the program's. A host sends none
		   while a button is held, and one that does is ignored. */
		const wsk_tree_t *in = 0, *lay = r;
		wsk_tree_t at;
		wsk_rect_t rect = { 0, 0, 0, 0 };
		int word = -1, k = -1;
		if (!ui->down && x >= 0 && y >= 0) k = wsk__ui_hit(r, x, y, &in, &rect, &word, &at, &lay);
		if (k < 0) { in = 0; k = -1; word = -1; }
		if (!ui->down && (in != ui->hover_in || k != ui->hover_item || word != ui->hover_word)) {
			ui->hover_in = in;
			ui->hover_item = (short)k;
			ui->hover_word = (short)word;
			n = wsk__note(note, max, n, WSK_NOTE_HOVER, in, k, word);
#ifndef WSK_NO_TIP
			wsk__tip_rest(ui, in, k, rect, 0, &lo, &hi);     /* a mouse resting (§5ac) */
#endif
		}
#endif
#ifndef WSK_NO_TIP
	} else if (ev->kind == WSK_EV_TICK) {
		n = wsk_ui_tick(ui, ev->d, note, max);
#endif
	} else if (ev->kind == WSK_EV_DOWN) {
		const wsk_tree_t *in = 0, *lay = r;
		wsk_tree_t at;
		wsk_rect_t rect = { 0, 0, 0, 0 };
		int word = -1, k = wsk__ui_hit(r, x, y, &in, &rect, &word, &at, &lay);
		ui->down = 1;
		ui->dragging = 0;
		ui->x0 = (short)x;
		ui->y0 = (short)y;
		ui->hit = (short)k;
		ui->hit_in = in;
		ui->word = (short)word;
#ifndef WSK_NO_GRAB
		/* in a tree marked grab -- the widget's own, or the stack widget
		   pressed in its padding: moved, it is dragged (§5ag) */
		ui->gin = 0;
		ui->grabbing = 0;
		if (k >= 0) {
			if (in->grab) ui->gin = in;
#ifndef WSK_NO_NEST
			else if (in->item[k].kind == WSK_STACK && r->nest != 0 && in->item[k].sub >= 0 &&
			         in->item[k].sub < r->nests && r->nest[in->item[k].sub].grab)
				ui->gin = &r->nest[in->item[k].sub];
#endif
		}
#endif
#ifndef WSK_NO_TIP
		/* a finger held is a rest; a mouse's press never is (§5ac rule 1) */
		if ((ev->key & WSK_MOD_TOUCH) && k >= 0) wsk__tip_rest(ui, in, k, rect, 1, &lo, &hi);
#endif
		if (k >= 0 && in->item[k].kind == WSK_BUTTON) {
			r->held = &in->item[k];
			wsk__rput(&ui->held, rect);
			wsk__span(&lo, &hi, rect.y, rect.y + rect.h);
		}
#ifndef WSK_NO_FIELD
		/* A tap on a field focuses it, the caret at the character tapped;
		   a tap on anything else leaves focus alone, so that a key of an
		   on-screen keyboard does not take it from the field (§5j rule 2). */
#if !defined(WSK_NO_WORDS) && !defined(WSK_NO_TEXTSEL)
		ui->wa = -1;
#endif
		if (k >= 0 && wsk__fbuf(lay, &lay->item[k]) != 0) {
			n = wsk__press_field(ui, in, k, lay, rect, x, y, ev->key & WSK_MOD_SHIFT,
			                     &lo, &hi, note, max, n);
#if !defined(WSK_NO_WORDS) && !defined(WSK_NO_TEXTSEL)
			/* the host counted the clicks (§5w rule 5): two select the
			   word there, three its line -- a drag after them extends */
			if (ev->d >= 2 && !(ev->key & WSK_MOD_SHIFT)) {
				wsk_buf_t *b = wsk__fbuf(lay, &lay->item[k]);
				int a = b->caret, z = b->caret, c;
				if (ev->d >= 3) {
					while (a > 0 && b->b[a - 1] != '\n') a--;
					while (z < b->len && b->b[z] != '\n') z++;
				} else {
					/* the word the click is in, or else the one it ends */
					c = (z < b->len && b->b[z] != '\n') ? wsk__wc(b->b[z])
					  : (a > 0) ? wsk__wc(b->b[a - 1]) : 3;
					if (c != 3) {
						while (a > 0 && wsk__wc(b->b[a - 1]) == c) a--;
						while (z < b->len && wsk__wc(b->b[z]) == c) z++;
					}
				}
				b->caret = z;
				b->span = a - z;
				ui->anchor = a;
				ui->wa = a;
				ui->wz = z;
			}
#endif
		}
#endif
#ifndef WSK_NO_SOURCE
		/* in a source's gutter: the press put the caret at its line's
		   start; the release will say which line (§5v rule 5) */
		ui->gline = -1;
		if (k >= 0 && lay->item[k].kind == WSK_SOURCE && wsk__fbuf(lay, &lay->item[k]) != 0) {
			wsk_buf_t *gb = wsk__fbuf(lay, &lay->item[k]);
			if (x < wsk_rect(lay, k).x + wsk__gutter(lay, gb, gb->b, gb->len)) {
				int line = 0, col = 0, lines = 1;
				wsk__where(gb->b, gb->len, gb->caret, WSK_NOWRAP, &line, &col, &lines);
				ui->gline = line;
			}
		}
#endif
#ifndef WSK_NO_SCROLL
		ui->drag = wsk__scroller(r, r, x, y, &ui->drag_at, 0);
#ifdef WSK_NO_NEST
		if (ui->drag != 0) ui->drag_at = *r;
#endif
		if (ui->drag != 0) ui->scroll0 = ui->drag->scroll;
#ifndef WSK_NO_WORLD
		if (ui->drag != 0) { ui->wv0x = ui->drag->vx; ui->wv0y = ui->drag->vy; }
		/* the topmost item under the press: a movable one is the drag's to
		   move, and is raised if the world keeps an order (step 6) */
		ui->witem = -1;
		if (ui->drag != 0 && ui->drag_at.overflow == WSK_WORLD) {
			const wsk_tree_t *wt = &ui->drag_at;
			for (int j = wt->n - 1; j >= 0; j--) {
				int i = wsk__wi(wt, j);
				if (!wsk__whit(wt, i, x, y)) continue;
				if (wt->place[i].move) {
					ui->witem = (short)i;
					ui->wp0x = wt->place[i].x;
					ui->wp0y = wt->place[i].y;
					if (wsk_world_raise(ui->drag, i)) {
						wsk__wrows(wt, i, &lo, &hi);
						n = wsk__note(note, max, n, WSK_NOTE_RAISE, ui->drag, i, 0);
					}
				}
				break;
			}
		}
#endif
#endif
#ifndef WSK_NO_SPLIT
		/* a divider held: not a click, not a scroll -- the panes' sizes,
		   as the press found them, for the drag to move (§5x) */
		ui->split = 0;
		if (k >= 0 && lay->item[k].kind == WSK_DIVIDER) {
			wsk_rect_t pa = wsk_rect(lay, k - 1), pb = wsk_rect(lay, k + 1);
			int col = (lay->axis == WSK_COL);
			ui->split = in;
#ifndef WSK_NO_NEST
			wsk__tcopy(&ui->split_at, lay);
#else
			ui->split_at = *lay;         /* as the scroll's drag_at is, here */
#endif
			ui->split_k = (short)k;
			ui->split0[0] = col ? pa.h : pa.w;
			ui->split0[1] = col ? pb.h : pb.w;
			ui->hit = -1;
#ifndef WSK_NO_SCROLL
			ui->drag = 0;
#endif
		}
#endif
	} else if (ev->kind == WSK_EV_MOVE && ui->down) {
		int took = 0;
#ifndef WSK_NO_GRAB
		/* a grab tree moved past the slop: the tree is dragged, and nothing
		   else this press could have done -- no click, scroll or pan */
		if (ui->gin != 0) {
			if (!ui->grabbing && (x - ui->x0 > ui->slop || ui->x0 - x > ui->slop ||
			                      y - ui->y0 > ui->slop || ui->y0 - y > ui->slop)) {
				ui->grabbing = 1;
				ui->dragging = 1;
				if (r->held != 0) {
					r->held = 0;
					wsk__span(&lo, &hi, ui->held.y, ui->held.y + ui->held.h);
				}
				n = wsk__note(note, max, n, WSK_NOTE_GRAB, ui->gin, -1, 0);
				/* and nothing else the press began goes on under it: no
				   tooltip held, selection, divider or scroll */
#ifndef WSK_NO_TIP
				if (ui->tip_hold) {
					wsk_rect_t z = { 0, 0, 0, 0 };
					wsk__tip_rest(ui, 0, -1, z, 0, &lo, &hi);
				}
#endif
#ifndef WSK_NO_TEXTSEL
				ui->selecting = 0;
#endif
#ifndef WSK_NO_SPLIT
				ui->split = 0;
#endif
#ifndef WSK_NO_SCROLL
				ui->drag = 0;
#endif
			} else if (ui->grabbing)
				n = wsk__note(note, max, n, WSK_NOTE_DRAG, ui->gin, -1, 0);
			if (ui->grabbing) goto rows;
		}
#endif
#ifndef WSK_NO_TIP
		/* a finger that moves past the slop is dragging, not holding */
		if (ui->tip_hold && (x - ui->x0 > ui->slop || ui->x0 - x > ui->slop ||
		                     y - ui->y0 > ui->slop || ui->y0 - y > ui->slop)) {
			wsk_rect_t z = { 0, 0, 0, 0 };
			wsk__tip_rest(ui, 0, -1, z, 0, &lo, &hi);
		}
#endif
#ifndef WSK_NO_SPLIT
		if (ui->split != 0) {
			const wsk_tree_t *t = &ui->split_at;
			int pane = -1, e = wsk__split_move(ui, (t->axis == WSK_COL) ? y - ui->y0 : x - ui->x0, &pane);
			took = 1;
			if (e >= 0) {
				int a0, a1;
				if (wsk_area_rows(t, &a0, &a1) == WSK_OK) wsk__span(&lo, &hi, a0, a1);
				n = wsk__note(note, max, n, WSK_NOTE_SPLIT, ui->split, ui->split_k, e);
			}
		}
#endif
#ifndef WSK_NO_TEXTSEL
		/* A press in the focused field selects while the pointer stays in
		   its rows along a scrolling stack's axis; past them by the slop,
		   the drag is the stack's again (§5s rule 5). */
		if (ui->selecting && ui->focus_b != 0) {
			wsk_rect_t *f = &ui->focus_r;
			int out = 0;
#ifndef WSK_NO_SCROLL
			if (ui->drag != 0)
				out = (ui->drag_at.axis == WSK_COL)
				    ? (y < f->y - ui->slop || y >= f->y + f->h + ui->slop)
				    : (x < f->x - ui->slop || x >= f->x + f->w + ui->slop);
#endif
			if (out) ui->selecting = 0;
			else {
				const wsk_tree_t *in = 0, *lay = r;
				wsk_tree_t at;
				wsk_rect_t rect = { 0, 0, 0, 0 };
				int word = -1, k = wsk__ui_hit(r, ui->x0, ui->y0, &in, &rect, &word, &at, &lay);
				took = 1;
				if (k >= 0 && wsk__fbuf(lay, &lay->item[k]) == ui->focus_b) {
					wsk_buf_t *b = ui->focus_b;
					int c = wsk__char_at(lay, k, b, wsk_rect(lay, k), x, y), anchor = ui->anchor;
#ifndef WSK_NO_WORDS
					/* after a double-click the word stays chosen, and a drag
					   grows the selection out from whichever end it leaves by */
					if (ui->wa >= 0) {
						if (c >= ui->wa && c <= ui->wz) { anchor = ui->wa; c = ui->wz; }
						else anchor = (c > ui->wz) ? ui->wa : ui->wz;
					}
#endif
					if (c != b->caret || anchor - c != b->span) {
						b->caret = c;
						b->span = (anchor - c);
#ifndef WSK_NO_MEMO
						wsk__follow(ui);
#endif
						wsk__span(&lo, &hi, f->y, f->y + f->h);
					}
				}
			}
		}
#endif
#ifndef WSK_NO_WORLD
		/* a world pans both ways, once past the slop either way (step 3) */
		if (ui->drag != 0 && !took && ui->drag_at.overflow == WSK_WORLD) {
			int dx = x - ui->x0, dy = y - ui->y0, z = ui->drag_at.zoom;
			took = 1;
			if (!ui->dragging && (dx > ui->slop || dx < -ui->slop || dy > ui->slop || dy < -ui->slop)) {
				ui->dragging = 1;
				if (r->held != 0) {
					r->held = 0;
					wsk__span(&lo, &hi, ui->held.y, ui->held.y + ui->held.h);
				}
			}
			if (ui->dragging && ui->witem >= 0) {
				if (wsk__item_to(ui, ui->wp0x + dx * 256 / z, ui->wp0y + dy * 256 / z, &lo, &hi))
					n = wsk__note(note, max, n, WSK_NOTE_MOVE, ui->drag, ui->witem, 0);
			} else if (ui->dragging && wsk__view_to(ui->drag, &ui->drag_at, ui->wv0x - dx * 256 / z,
			                                 ui->wv0y - dy * 256 / z, z)) {
				int a0, a1;
				if (wsk_area_rows(&ui->drag_at, &a0, &a1) == WSK_OK) wsk__span(&lo, &hi, a0, a1);
				n = wsk__note(note, max, n, WSK_NOTE_VIEW, ui->drag, -1, z);
			}
		}
#endif
#ifndef WSK_NO_SCROLL
		if (ui->drag != 0 && !took) {
			int col = (ui->drag_at.axis == WSK_COL);
			int dd = col ? (y - ui->y0) : (x - ui->x0);
			if (!ui->dragging && (dd > ui->slop || dd < -ui->slop)) {
				ui->dragging = 1;
				if (r->held != 0) {
					r->held = 0;
					wsk__span(&lo, &hi, ui->held.y, ui->held.y + ui->held.h);
				}
			}
			if (ui->dragging && wsk__scroll_to(ui->drag, &ui->drag_at, ui->scroll0 - dd)) {
				int a0, a1;
				if (wsk_area_rows(&ui->drag_at, &a0, &a1) == WSK_OK) wsk__span(&lo, &hi, a0, a1);
				n = wsk__note(note, max, n, WSK_NOTE_SCROLL, ui->drag, -1, ui->drag->scroll);
			}
		}
#endif
		(void)took;
	} else if (ev->kind == WSK_EV_UP && ui->down) {
		ui->down = 0;
#ifndef WSK_NO_GRAB
		if (ui->grabbing) {
			n = wsk__note(note, max, n, WSK_NOTE_DROP, ui->gin, -1, 0);
			ui->grabbing = 0;
			ui->gin = 0;
			ui->dragging = 0;
			goto rows;
		}
		ui->gin = 0;
#endif
#ifndef WSK_NO_TIP
		/* a finger lifted after its tip showed: the hold was used, and it
		   does not click (§5ac rule 5); lifted before, or with no tip, it does */
		if (ui->tip_hold) {
			wsk_rect_t z = { 0, 0, 0, 0 };
			ate = (r->tip != 0);
			wsk__tip_rest(ui, 0, -1, z, 0, &lo, &hi);
		}
#endif
#ifndef WSK_NO_SPLIT
		ui->split = 0;
#endif
#ifndef WSK_NO_TEXTSEL
		ui->selecting = 0;
#endif
		if (r->held != 0) {
			r->held = 0;
			wsk__span(&lo, &hi, ui->held.y, ui->held.y + ui->held.h);
		}
#ifndef WSK_NO_WORLD
		/* bare floor in a world that is the root: nothing was hit, and the
		   program still has to hear it -- CLICK, item -1, in the world; a
		   nested world's widget answers for its floor already (step 3) */
		if (!ui->dragging && ui->hit < 0 && ui->drag == r && r->overflow == WSK_WORLD)
			n = wsk__note(note, max, n, WSK_NOTE_CLICK, r, -1, -1);
#endif
		if (!ui->dragging && ui->hit >= 0 && !ate) {
			const wsk_tree_t *in = 0, *lay = r;
			wsk_tree_t at;
			wsk_rect_t rect = { 0, 0, 0, 0 };
			int word = -1, k = wsk__ui_hit(r, x, y, &in, &rect, &word, &at, &lay);
			if (k == ui->hit && in == ui->hit_in) {
				n = wsk__note(note, max, n, WSK_NOTE_CLICK, in, k, ui->word);
#ifndef WSK_NO_SOURCE
				if (ui->gline >= 0)
					n = wsk__note(note, max, n, WSK_NOTE_GUTTER, in, k, ui->gline);
#endif
#ifndef WSK_NO_CHECK
				/* a check toggles (§5m rule 2) */
				if (in->item[k].kind == WSK_CHECK && wsk__toggle(r, &in->item[k]) >= 0) {
					n = wsk__note(note, max, n, WSK_NOTE_CHANGE, in, k,
					              wsk__ticked(r, &in->item[k]));
					wsk__span(&lo, &hi, rect.y, rect.y + rect.h);
				}
#endif
				/* an item of a selecting stack is chosen (§5m rule 3) -- the
				   deepest such stack under the finger, so that a tap on any
				   cell of a table's row chooses the row (§5n). The stack is
				   the caller's tree, which is writable. */
				{
					wsk_tree_t sat;
					const wsk_tree_t *slay = 0;
					int si = -1;
					wsk_tree_t *own = wsk__selector(r, r, x, y, &sat, &slay, &si, 0);
					if (own != 0 && own->sel != si) {
						wsk_rect_t nr = wsk_rect(slay, si);
						if (own->sel >= 0 && own->sel < slay->n) {
							wsk_rect_t old = wsk_rect(slay, own->sel);
							if (wsk__cut(slay, &old)) wsk__span(&lo, &hi, old.y, old.y + old.h);
						}
						own->sel = (short)si;
						if (wsk__cut(slay, &nr)) wsk__span(&lo, &hi, nr.y, nr.y + nr.h);
						n = wsk__note(note, max, n, WSK_NOTE_SELECT, own, si, si);
					}
				}
#ifndef WSK_NO_OVER
				/* a menu's choice is made once: a button in it closes it --
				   a dialog's does not: OK may refuse what was typed, Up
				   stays, so closing is the program's (§5ae) */
				if (r->over != WSK_NONE && !r->omodal && in == &r->nest[r->over] &&
				    in->item[k].kind == WSK_BUTTON) {
					int a0, a1;
					if (wsk_over_rows(r, &a0, &a1) == WSK_OK) wsk__span(&lo, &hi, a0, a1);
					wsk_over_close(r);
					n = wsk__note(note, max, n, WSK_NOTE_CLOSE, in, -1, 0);
				}
#endif
			}
		}
		ui->dragging = 0;
		ui->drag = 0;
	}
	/* The keyboard's way to a context menu: the focused widget's (§5s). */
	else if (ev->kind == WSK_EV_KEY && ev->key == WSK_KEY_MENU) {
#ifndef WSK_NO_FIELD
		if (r->focus != 0) {
			wsk__focus_now(ui);
			n = wsk__note(note, max, n, WSK_NOTE_MENU, ui->focus_in, ui->focus_item, -1);
		} else
#endif
#ifndef WSK_NO_OVER
		if (r->omodal && r->over != WSK_NONE)       /* the dialog's, not the window's */
			n = wsk__note(note, max, n, WSK_NOTE_MENU, &r->nest[r->over], -1, -1);
		else
#endif
		n = wsk__note(note, max, n, WSK_NOTE_MENU, r, -1, -1);
	}
#ifndef WSK_NO_FIELD
	/* Tab and Shift-Tab: the next or previous focusable widget -- field,
	   button, check -- in declaration order, depth first (§5m rule 4). */
	else if (ev->kind == WSK_EV_KEY && (ev->key == WSK_KEY_TAB || ev->key == WSK_KEY_BACKTAB)) {
		const wsk_tree_t *in = 0;
		short item = -1;
		wsk_rect_t fr = { 0, 0, 0, 0 };
		wsk_buf_t *nb = 0;
		wsk__focus_now(ui);
		if (wsk__tab(ui, ev->key == WSK_KEY_BACKTAB, &in, &item, &fr, &nb)) {
			wsk__refocus(ui, in, item, fr, nb, &lo, &hi);
			n = wsk__note(note, max, n, WSK_NOTE_FOCUS, in, item, 0);
#ifndef WSK_NO_TIP
			wsk__tip_rest(ui, in, item, fr, 0, &lo, &hi);   /* focus arriving rests (§5ac) */
#endif
		}
	}
#if !defined(WSK_NO_LISTKEYS)
	/* the focused list's or world's keys (§5z) */
	else if ((ev->kind == WSK_EV_KEY || ev->kind == WSK_EV_TEXT) && r->focus != 0 &&
	         ui->focus_b == 0 && ((const wsk_widget_t *)r->focus)->kind == WSK_STACK &&
	         wsk__stackkeys(ui, ev, note, max, &n, &lo, &hi))
		used = 1;
#endif
	/* ENTER on a focused button is its click, and on a check its toggle. */
	else if (ev->kind == WSK_EV_KEY && ev->key == WSK_KEY_ENTER && r->focus != 0 &&
	         ui->focus_b == 0) {
		wsk__focus_now(ui);
		n = wsk__note(note, max, n, WSK_NOTE_CLICK, ui->focus_in, ui->focus_item, -1);
#ifndef WSK_NO_CHECK
		if (ui->focus_in->item[ui->focus_item].kind == WSK_CHECK &&
		    wsk__toggle(r, &ui->focus_in->item[ui->focus_item]) >= 0) {
			n = wsk__note(note, max, n, WSK_NOTE_CHANGE, ui->focus_in, ui->focus_item,
			              wsk__ticked(r, &ui->focus_in->item[ui->focus_item]));
			wsk__span(&lo, &hi, ui->focus_r.y, ui->focus_r.y + ui->focus_r.h);
		}
#endif
	}
	else if ((ev->kind == WSK_EV_TEXT || ev->kind == WSK_EV_KEY) && ui->focus_b != 0) {
		wsk_buf_t *b;
		int edit = 0, moved = 0, memo = 0, key = ev->key, sel = 0, s0 = 0;
#ifndef WSK_NO_TEXTSEL
		int ext = 0, anchor, s1 = 0;
#endif
		wsk__focus_now(ui);
		b = ui->focus_b;
#ifndef WSK_NO_MEMO
		memo = wsk__lined(ui->focus_in->item[ui->focus_item].kind);
#else
		(void)memo;
#endif
#ifndef WSK_NO_TEXTSEL
		/* Shift with a key that moves the caret moves it and keeps the
		   anchor (§5s rule 4): the same key, extending */
		if (ev->kind == WSK_EV_KEY && (key & WSK_MOD_SHIFT) &&
		    wsk__moves(key & ~WSK_MOD_SHIFT)) {
			ext = 1;
			key &= ~WSK_MOD_SHIFT;
		}
		anchor = b->caret + b->span;
		sel = wsk__selr(b, &s0, &s1);
#endif
		if (ev->kind == WSK_EV_TEXT) {
			/* All of it or none of it: characters the window can draw, and
			   room for every one (§5j rule 3) -- in place of a selection */
			int m = 0, ok;
			while (m < (int)sizeof ev->text && ev->text[m] != '\0') m++;
			ok = wsk__ok_n(ev->text, m, r->caps, 0);
			if (ok && m > 0 && b->len - sel + m < b->cap) {
				int at = sel ? s0 : b->caret;
				/* one character, replacing nothing: typing, which undo
				   groups; anything else is a step of its own */
				wsk__put(b, at, sel, ev->text, m, sel == 0 && wsk__chars(ev->text, m) == 1);
				b->caret = at + m;
				b->span = 0;
				edit = 1;
			}
		}
#ifndef WSK_NO_UNDO
		/* undo and redo (§5v), when the buffer keeps steps; else the
		   chords are the program's */
		else if (b->undo != 0 && (key == ('z' | WSK_MOD_CTRL) || key == ('y' | WSK_MOD_CTRL) ||
		                          key == ('z' | WSK_MOD_CTRL | WSK_MOD_SHIFT))) {
			used = 1;
			edit = wsk__undo(b, key == ('z' | WSK_MOD_CTRL));
		}
#endif
#ifndef WSK_NO_TEXTSEL
		/* the chords a field uses (§5s rule 6); any other is the program's */
		else if (key == ('a' | WSK_MOD_CTRL)) {
			used = 1;
			if (b->caret != b->len || b->span != -b->len) {
				b->caret = b->len;
				b->span = -b->len;
				moved = 1;
			}
		} else if ((key == ('c' | WSK_MOD_CTRL) || key == ('x' | WSK_MOD_CTRL)) && ui->clip != 0) {
			used = 1;
			if (sel > 0 && sel < ui->clipcap) {
				for (int j = 0; j < sel; j++) ui->clip[j] = b->b[s0 + j];
				ui->clip[sel] = '\0';
				n = wsk__note(note, max, n, WSK_NOTE_COPY, ui->focus_in, ui->focus_item, sel);
				if (key == ('x' | WSK_MOD_CTRL)) edit = wsk__drop(b);
			}
		} else if (key == ('v' | WSK_MOD_CTRL)) {
			used = 1;
			n = wsk__note(note, max, n, WSK_NOTE_PASTE, ui->focus_in, ui->focus_item, 0);
		} else if (!ext && sel > 0 && (key == WSK_KEY_BACKSPACE || key == WSK_KEY_DELETE)) {
			edit = wsk__drop(b);
		} else if (!ext && sel > 0 && (key == WSK_KEY_LEFT || key == WSK_KEY_RIGHT)) {
			/* a selection collapses to the end the arrow points at */
			b->caret = ((key == WSK_KEY_LEFT) ? s0 : s1);
			b->span = 0;
			moved = 1;
		}
#endif
#ifndef WSK_NO_WORDS
		/* Ctrl's moves and deletions, a word at a time; Ctrl-Home and
		   Ctrl-End the whole text's ends (§5w rules 2-4) */
		else if (key == (WSK_KEY_LEFT | WSK_MOD_CTRL) || key == (WSK_KEY_RIGHT | WSK_MOD_CTRL) ||
		         key == (WSK_KEY_HOME | WSK_MOD_CTRL) || key == (WSK_KEY_END | WSK_MOD_CTRL)) {
			int k0 = key & ~WSK_MOD_CTRL;
			int to = (k0 == WSK_KEY_HOME) ? 0 : (k0 == WSK_KEY_END) ? b->len
			       : wsk__wmove(b->b, b->len, b->caret, k0 == WSK_KEY_LEFT);
			used = 1;
			if (to != b->caret) { b->caret = to; moved = 1; }
#ifndef WSK_NO_TEXTSEL
			/* a chord it used, so the rule below keeps a selection: a
			   move without Shift lets it go here */
			if (!ext && b->span != 0) { b->span = 0; moved = 1; }
#endif
		} else if (key == (WSK_KEY_BACKSPACE | WSK_MOD_CTRL) || key == (WSK_KEY_DELETE | WSK_MOD_CTRL)) {
			used = 1;
#ifndef WSK_NO_TEXTSEL
			if (sel > 0) edit = wsk__drop(b);
			else
#endif
			{
				int back = ((key & ~WSK_MOD_CTRL) == WSK_KEY_BACKSPACE);
				int to = wsk__wmove(b->b, b->len, b->caret, back);
				int a = back ? to : b->caret, z = back ? b->caret : to;
				if (z > a) { wsk__put(b, a, z - a, "", 0, 0); b->caret = a; edit = 1; }
			}
		}
#endif
#ifndef WSK_NO_MEMO
		/* In a memo, a line is a line: Enter breaks one, Up and Down move
		   between them keeping the column, Home and End are its ends. */
		else if (memo && key == WSK_KEY_ENTER) {
			if (b->len - sel + 1 < b->cap) {
				int at = sel ? s0 : b->caret;
				wsk__put(b, at, sel, "\n", 1, 0);
				b->caret = at + 1;
				b->span = 0;
				edit = 1;
			}
		} else if (memo && (key == WSK_KEY_UP || key == WSK_KEY_DOWN ||
		                    key == WSK_KEY_HOME || key == WSK_KEY_END)) {
			int line = 0, col = 0, lines = 1, at, end, to = b->caret;
			int wr = wsk__mroom(ui->focus_in, &ui->focus_in->item[ui->focus_item], ui->focus_r, b);
			wsk__where(b->b, b->len, b->caret, wr, &line, &col, &lines);
			if (key == WSK_KEY_UP && line > 0) line--;
			else if (key == WSK_KEY_DOWN && line < lines - 1) line++;
			at = wsk__line(b->b, b->len, line, wr, &end);
			if (key == WSK_KEY_HOME) to = at;
			else if (key == WSK_KEY_END) to = end;
			else to = at + wsk__byte_of(b->b + at, end - at, col);   /* the column kept */
			if (to != b->caret) { b->caret = to; moved = 1; }
		}
#endif
		/* a character at a time, however many bytes it is (§5v step 3) */
		else if (key == WSK_KEY_LEFT && b->caret > 0) {
			b->caret = wsk__prev(b->b, b->caret); moved = 1;
		} else if (key == WSK_KEY_RIGHT && b->caret < b->len) {
			b->caret = wsk__next(b->b, b->len, b->caret); moved = 1;
		} else if (key == WSK_KEY_HOME && b->caret != 0) {
			b->caret = 0; moved = 1;
		} else if (key == WSK_KEY_END && b->caret != b->len) {
			b->caret = b->len; moved = 1;
		} else if (key == WSK_KEY_BACKSPACE && b->caret > 0) {
			int p = wsk__prev(b->b, b->caret);
			wsk__put(b, p, b->caret - p, "", 0, 0);
			b->caret = p; edit = 1;
		} else if (key == WSK_KEY_DELETE && b->caret < b->len) {
			wsk__put(b, b->caret, wsk__next(b->b, b->len, b->caret) - b->caret, "", 0, 0);
			edit = 1;
		} else if (key == WSK_KEY_ENTER) {
			n = wsk__note(note, max, n, WSK_NOTE_ENTER, ui->focus_in, ui->focus_item, b->len);
		}
#ifndef WSK_NO_TEXTSEL
		/* Shift kept the anchor; any other move lets the selection go --
		   and so does an edit, which wsk__drop has done if there was one */
		if (ext) {
			if (moved) b->span = (anchor - b->caret);
		} else if (!used && b->span != 0 && (edit || moved || wsk__moves(key))) {
			b->span = 0;
			moved = 1;
		}
#endif
		if (edit) n = wsk__note(note, max, n, WSK_NOTE_TEXT, ui->focus_in, ui->focus_item, b->len);
		if (edit || moved) wsk__span(&lo, &hi, ui->focus_r.y, ui->focus_r.y + ui->focus_r.h);
#ifndef WSK_NO_MEMO
		/* and the view follows the caret */
		if (memo && (edit || moved)) wsk__follow(ui);
#endif
	}
#endif
#ifndef WSK_NO_SCROLL
	else if (ev->kind == WSK_EV_WHEEL) {
		wsk_tree_t at;
		wsk_tree_t *s = wsk__scroller(r, r, x, y, &at, 0);
#ifdef WSK_NO_NEST
		if (s != 0) at = *r;
#endif
#ifndef WSK_NO_WORLD
		/* the wheel zooms a world about the pointer: the world point under
		   it stays under it (step 3 rule 2) */
		if (s != 0 && at.overflow == WSK_WORLD) {
			int z = at.zoom, nz = z - z * ev->d / 256, px = x - at.ax, py = y - at.ay;
			int wx = at.vx + px * 256 / z, wy = at.vy + py * 256 / z;
			if (nz == z && ev->d != 0) nz = z + ((ev->d < 0) ? 1 : -1);
			nz = wsk__zclamp(&at, nz);          /* the zoom it will be, so the point holds */
			if (ev->d != 0 && wsk__view_to(s, &at, wx - px * 256 / nz, wy - py * 256 / nz, nz)) {
				int a0, a1;
				if (wsk_area_rows(&at, &a0, &a1) == WSK_OK) wsk__span(&lo, &hi, a0, a1);
				n = wsk__note(note, max, n, WSK_NOTE_VIEW, s, -1, s->zoom);
			}
		} else
#endif
		if (s != 0 && wsk__scroll_to(s, &at, s->scroll + ev->d)) {
			int a0, a1;
			if (wsk_area_rows(&at, &a0, &a1) == WSK_OK) wsk__span(&lo, &hi, a0, a1);
			n = wsk__note(note, max, n, WSK_NOTE_SCROLL, s, -1, s->scroll);
		}
	}
#endif
	/* A chord no widget used is the program's: its shortcuts (§5s rule 3). */
	if (ev->kind == WSK_EV_KEY && (ev->key & (WSK_MOD_CTRL | WSK_MOD_ALT)) && !used)
		n = wsk__note(note, max, n, WSK_NOTE_KEY, r, -1, ev->key);
	(void)ate;
#if !defined(WSK_NO_WORLD) || !defined(WSK_NO_OVER) || !defined(WSK_NO_GRAB)
rows:
#endif
	if (hi > lo) {
		lo = (lo / WSK_BAND) * WSK_BAND;
		hi = wsk__round_band(hi);
		if (lo < 0) lo = 0;
		if (hi > r->h) hi = r->h;
	} else {
		lo = hi = 0;
	}
	*y0 = lo;
	*y1 = hi;
	return n;
}
#endif /* WSK_NO_INPUT */

#endif /* WHISKER_H */
