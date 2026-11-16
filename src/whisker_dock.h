/*
 * whisker_dock.h -- docking (design.md §5ag).
 *
 * gBASIC's `docks right` and `dock name "Caption"`: panes in the edge areas
 * of a window, around its centre, each with a caption it can be dragged by
 * to another edge, a close button, and a divider to resize its area. This
 * header builds that frame in the CALLER's memory, from a layout the caller
 * owns and may save and load as one line of text.
 *
 * The frame is made of the root's own parts, in slots the program sets
 * aside, as the chooser's is: trees in `nest` from nest_base (WSK_DOCK_TREES
 * of them), the panes' captions in `str` from str_base
 * (WSK_DOCK_MAX), and the root itself, whose items the dock writes. A
 * pane's content and the centre are the program's own trees in `nest`.
 *
 *     wsk_dock_t d;                                  caller memory
 *     wsk_dock_init(&d, &root, strs, nest_base, str_base, centre);
 *     wsk_dock_pane(&d, "files", "Files", files_tree, WSK_DOCK_LEFT);
 *     ...
 *     wsk_dock_build(&d);
 *     ... each event: n = wsk_feed(&ui, &ev, notes, ...);
 *         if (wsk_dock_feed(&d, &ui, &ev, notes, n)) draw it all again;
 *
 * A pane double-clicked by its caption floats (step 2): the program opens a
 * window for each pane whose wsk_dock_shown is WSK_DOCK_FLOAT, with a root
 * from wsk_dock_window, feeds that window's toolkit to wsk_dock_feed too,
 * and closes the window when the pane stops floating. The dock opens none.
 *
 * Same rules as whisker.h: no libc, no .data or .bss of its own, no tables
 * of pointers.
 */
#ifndef WHISKER_DOCK_H
#define WHISKER_DOCK_H

#include "whisker.h"

#if defined(WSK_NO_NEST) || defined(WSK_NO_TEXT) || defined(WSK_NO_SPLIT) || \
    defined(WSK_NO_GRAB) || defined(WSK_NO_INPUT)
#error "whisker_dock.h needs nesting, text items, dividers, grabbing and the toolkit"
#endif

#ifndef WSK_DOCK_MAX
#define WSK_DOCK_MAX 8             /* panes a window may have */
#endif
#define WSK_DOCK_TREES (6 + 2 * WSK_DOCK_MAX)   /* the tree slots a dock sets aside */

enum { WSK_DOCK_LEFT = 0, WSK_DOCK_RIGHT, WSK_DOCK_TOP, WSK_DOCK_BOTTOM };
#define WSK_DOCK_FLOAT 2           /* `shown`: 0 closed, 1 docked, 2 in a window of its own */

typedef struct {
	char          name[24];     /* what a program and a saved layout call it */
	char          caption[32];  /* what a person reads on its bar */
	short         content;      /* its content: a tree in the root's `nest` */
	short         order;        /* where in its area: lower first */
	short         size;         /* its extent along its area, or 0 to share */
	unsigned char area, shown;  /* its edge -- kept while it floats -- and 0, 1 or WSK_DOCK_FLOAT */
} wsk_pane_t;

typedef struct {
	wsk_tree_t   *root;
	const char  **str;
	short         nb, sb, centre;
	int           n;
	wsk_pane_t    pane[WSK_DOCK_MAX];
	short         asize[4];     /* each area's extent: a side's width, an edge's height */
	short         hint;         /* the edge a dragged pane would go to, or -1 */
	short         drag;         /* the pane being dragged, or -1 */

	/* what the frame is built of -- the dock's, rebuilt from the layout */
	wsk_widget_t  rootw[7], midw[7], areaw[4][2 * WSK_DOCK_MAX], panew[WSK_DOCK_MAX][2],
	              capw[WSK_DOCK_MAX][2];
	short         rsizes[7], msizes[7], asizes[4][2 * WSK_DOCK_MAX];
	signed char   rarea[7], marea[7];          /* which item of root or middle is which area */
	signed char   apane[4][2 * WSK_DOCK_MAX];  /* which item of an area is which pane */
	wsk_widget_t  floatw[WSK_DOCK_MAX];        /* a floating window's one item: its pane */
} wsk_dock_t;

/* ------------------------------------------------------------------ */

static inline void wsk__dk_zero(void *p, int n)
{
	unsigned char *b = (unsigned char *)p;
	for (int k = 0; k < n; k++) {
		b[k] = 0;
#if defined(__GNUC__)
		__asm__ volatile ("" ::: "memory");
#endif
	}
}

static inline void wsk__dk_cpy(char *d, const char *s, int cap)
{
	int i = 0;
	if (s != 0) while (s[i] != '\0' && i < cap - 1) { d[i] = s[i]; i++; }
	d[i] = '\0';
}

static inline int wsk__dk_same(const char *a, const char *b)
{
	while (*a != '\0' && *a == *b) { a++; b++; }
	return *a == *b;
}

/* The dock's own trees: 0 the middle row, 1..4 the areas, then a column
   for each pane, a caption row for each, and last the drop bar's, empty --
   a stack's fill is what draws it. */
static inline wsk_tree_t *wsk__dk_tree(wsk_dock_t *d, int k)
{
	return (wsk_tree_t *)&d->root->nest[d->nb + k];
}

static inline int wsk__dk_mid(void)        { return 0; }
static inline int wsk__dk_area(int a)      { return 1 + a; }
static inline int wsk__dk_pane(int p)      { return 5 + p; }
static inline int wsk__dk_cap(int p)       { return 5 + WSK_DOCK_MAX + p; }
static inline int wsk__dk_bar(void)        { return 5 + 2 * WSK_DOCK_MAX; }

/*
 * Set aside the root's slots: trees nest_base..+WSK_DOCK_TREES, the
 * captions' strings str_base..+WSK_DOCK_MAX in `str` -- the root's own
 * array, writable -- and the centre, a tree of the program's in `nest`.
 * WSK_E_KIND if the root's arrays do not reach that far.
 */
static inline int wsk_dock_init(wsk_dock_t *d, wsk_tree_t *root, const char **str,
                                int nest_base, int str_base, int centre)
{
	if (root->nest == 0 || nest_base < 0 || nest_base + WSK_DOCK_TREES > root->nests ||
	    centre < 0 || centre >= root->nests ||
	    (centre >= nest_base && centre < nest_base + WSK_DOCK_TREES) ||
	    str == 0 || (const char *const *)str != root->str || str_base < 0 ||
	    str_base + WSK_DOCK_MAX > root->strs)
		return WSK_E_KIND;
	wsk__dk_zero(d, (int)sizeof *d);
	d->root = root; d->str = str;
	d->nb = (short)nest_base; d->sb = (short)str_base; d->centre = (short)centre;
	d->hint = -1; d->drag = -1;
	d->asize[WSK_DOCK_LEFT] = d->asize[WSK_DOCK_RIGHT] = (short)(root->w / 5);
	d->asize[WSK_DOCK_TOP] = d->asize[WSK_DOCK_BOTTOM] = (short)(root->h / 4);
	return WSK_OK;
}

/* A pane: its index, or -1 (no room, a name already used, no such tree, or
   a name the layout line could not carry: longer than 23 bytes, or with a
   ':', ';' or space in it). */
static inline int wsk_dock_pane(wsk_dock_t *d, const char *name, const char *caption,
                                int content, int area)
{
	wsk_pane_t *p;
	int len = 0;
	if (d->n >= WSK_DOCK_MAX || name == 0 || name[0] == '\0' || area < 0 || area > 3 ||
	    content < 0 || content >= d->root->nests) return -1;
	for (; name[len] != '\0'; len++)
		if (name[len] == ':' || name[len] == ';' || name[len] == ' ' || len >= (int)sizeof p->name - 1) return -1;
	for (int i = 0; i < d->n; i++) if (wsk__dk_same(d->pane[i].name, name)) return -1;
	p = &d->pane[d->n];
	wsk__dk_cpy(p->name, name, (int)sizeof p->name);
	wsk__dk_cpy(p->caption, caption ? caption : name, (int)sizeof p->caption);
	p->content = (short)content;
	p->area = (unsigned char)area;
	p->shown = 1;
	p->size = 0;
	p->order = (short)d->n;
	return d->n++;
}

static inline int wsk__dk_find(const wsk_dock_t *d, const char *name)
{
	for (int i = 0; i < d->n; i++) if (wsk__dk_same(d->pane[i].name, name)) return i;
	return -1;
}

/* A divider widget, the dock's look. */
static inline void wsk__dk_div(wsk_widget_t *w, unsigned int fill)
{
	wsk__dk_zero(w, (int)sizeof *w);
	w->kind = WSK_DIVIDER;
	w->size = 6;
	w->fill = fill;
}

/* The caption row's pieces, and the width a side must keep for its widest:
   the title's own extent, the close button, the padding and gap between. */
#define WSK__DK_CPAD 2
#define WSK__DK_CGAP 4
static inline int wsk__dk_need(const wsk_dock_t *d, int a)
{
	const wsk_tree_t *r = d->root;
	int need = 40;
	for (int i = 0; i < d->n; i++) {
		const char *c = d->pane[i].caption;
		int w;
		if (d->pane[i].shown != 1 || d->pane[i].area != a) continue;
		w = wsk_text_w(r->font, wsk__chars(c, wsk__len(c))) + 4 * WSK__DK_CPAD + WSK__DK_CGAP + r->font + 6;
		if (w > need) need = w;
	}
	return need;
}

/* The drop bar: a strip along the edge a dragged pane would go to. */
static inline void wsk__dk_hintbar(wsk_dock_t *d, wsk_widget_t *w, unsigned int fill)
{
	wsk__dk_zero(w, (int)sizeof *w);
	w->kind = WSK_STACK;
	w->sub = (short)(d->nb + wsk__dk_bar());
	w->size = 6;
	w->fill = fill;
}

/*
 * The frame, built again from the layout: the root a column of the top
 * area, the middle row and the bottom area; the middle the left area, the
 * centre and the right area; each area its shown panes in order, divided;
 * each pane its caption over its content. An area with no pane shown takes
 * no room. Call after the layout changes -- wsk_dock_feed does.
 */
static inline void wsk_dock_build(wsk_dock_t *d)
{
	wsk_tree_t *r = d->root, *mid = wsk__dk_tree(d, wsk__dk_mid());
	unsigned int dfill = r->trough, hfill = r->accent;
	int has[4] = { 0, 0, 0, 0 }, k, maxside = (r->w - 80) / 2, maxedge = (r->h - 80) / 2;

	for (int i = 0; i < d->n; i++) if (d->pane[i].shown == 1) has[d->pane[i].area] = 1;
	if (maxside < 40) maxside = 40;
	if (maxedge < 40) maxedge = 40;

	/* each area: its panes, by order, with a divider between */
	for (int a = 0; a < 4; a++) {
		wsk_tree_t *at = wsk__dk_tree(d, wsk__dk_area(a));
		int used[WSK_DOCK_MAX], m = 0;
		for (int i = 0; i < d->n; i++) used[i] = 0;
		k = 0;
		for (;;) {                                 /* the next shown pane by order */
			int best = -1;
			for (int i = 0; i < d->n; i++)
				if (!used[i] && d->pane[i].shown == 1 && d->pane[i].area == a &&
				    (best < 0 || d->pane[i].order < d->pane[best].order)) best = i;
			if (best < 0) break;
			used[best] = 1;
			if (m > 0) {
				wsk__dk_div(&d->areaw[a][k], dfill);
				d->asizes[a][k] = 0; d->apane[a][k] = -1; k++;
			}
			wsk__dk_zero(&d->areaw[a][k], (int)sizeof d->areaw[a][k]);
			d->areaw[a][k].kind = WSK_STACK;
			d->areaw[a][k].sub = (short)(d->nb + wsk__dk_pane(best));
			d->areaw[a][k].weight = 1;
			d->asizes[a][k] = d->pane[best].size;
			d->apane[a][k] = (signed char)best;
			k++; m++;
		}
		wsk_nest_init(at, r, (a == WSK_DOCK_LEFT || a == WSK_DOCK_RIGHT) ? WSK_COL : WSK_ROW);
		at->item = d->areaw[a]; at->n = k; at->pad = 0; at->gap = 0; at->sizes = d->asizes[a];
	}

	/* each pane: a caption row, grabbed to drag it, over its content */
	for (int i = 0; i < d->n; i++) {
		wsk_tree_t *pt = wsk__dk_tree(d, wsk__dk_pane(i)), *ct = wsk__dk_tree(d, wsk__dk_cap(i));
		wsk_widget_t *pw = d->panew[i], *cw = d->capw[i];
		d->str[d->sb + i] = d->pane[i].caption;
		wsk__dk_zero(pw, (int)sizeof d->panew[i]);
		pw[0].kind = WSK_STACK; pw[0].sub = (short)(d->nb + wsk__dk_cap(i)); pw[0].size = (short)(r->font + 10);
		pw[0].fill = dfill;
		wsk__dk_cpy(pw[0].id, d->pane[i].name, WSK_ID_MAX);
		pw[1].kind = WSK_STACK; pw[1].sub = d->pane[i].content; pw[1].weight = 1;
		wsk__dk_zero(cw, (int)sizeof d->capw[i]);
		cw[0].kind = WSK_TEXT; cw[0].sub = (short)(d->sb + i); cw[0].weight = 1;
		cw[1].kind = WSK_BUTTON; cw[1].size = (short)(r->font + 6);
		cw[1].id[0] = 'x'; cw[1].text[0] = 'x';
		wsk_nest_init(pt, r, WSK_COL); pt->item = pw; pt->n = 2; pt->pad = 0; pt->gap = 0;
		wsk_nest_init(ct, r, WSK_ROW); ct->item = cw; ct->n = 2; ct->pad = WSK__DK_CPAD; ct->gap = WSK__DK_CGAP;
		ct->talign = WSK_ALIGN_START;
		ct->grab = 1;
	}
	for (int i = d->n; i < WSK_DOCK_MAX; i++) {    /* slots no pane uses: empty, */
		wsk_nest_init(wsk__dk_tree(d, wsk__dk_pane(i)), r, WSK_COL);   /* as a check reads every tree */
		wsk_nest_init(wsk__dk_tree(d, wsk__dk_cap(i)), r, WSK_ROW);
		d->str[d->sb + i] = "";
	}
	wsk_nest_init(wsk__dk_tree(d, wsk__dk_bar()), r, WSK_COL);

	/* the middle row: left, centre, right -- and the drop bar at a side */
	k = 0;
	if (d->hint == WSK_DOCK_LEFT) { wsk__dk_hintbar(d, &d->midw[k], hfill); d->msizes[k] = 0; d->marea[k++] = -1; }
	if (has[WSK_DOCK_LEFT]) {
		int s = d->asize[WSK_DOCK_LEFT], lo = wsk__dk_need(d, WSK_DOCK_LEFT);
		wsk__dk_zero(&d->midw[k], (int)sizeof d->midw[k]);
		d->midw[k].kind = WSK_STACK; d->midw[k].sub = (short)(d->nb + wsk__dk_area(WSK_DOCK_LEFT));
		d->msizes[k] = (short)((s < lo) ? lo : (s > maxside) ? maxside : s); d->marea[k++] = WSK_DOCK_LEFT;
		wsk__dk_div(&d->midw[k], dfill); d->msizes[k] = 0; d->marea[k++] = -1;
	}
	wsk__dk_zero(&d->midw[k], (int)sizeof d->midw[k]);
	d->midw[k].kind = WSK_STACK; d->midw[k].sub = d->centre; d->midw[k].weight = 1;
	d->msizes[k] = 0; d->marea[k++] = -1;
	if (has[WSK_DOCK_RIGHT]) {
		int s = d->asize[WSK_DOCK_RIGHT], lo = wsk__dk_need(d, WSK_DOCK_RIGHT);
		wsk__dk_div(&d->midw[k], dfill); d->msizes[k] = 0; d->marea[k++] = -1;
		wsk__dk_zero(&d->midw[k], (int)sizeof d->midw[k]);
		d->midw[k].kind = WSK_STACK; d->midw[k].sub = (short)(d->nb + wsk__dk_area(WSK_DOCK_RIGHT));
		d->msizes[k] = (short)((s < lo) ? lo : (s > maxside) ? maxside : s); d->marea[k++] = WSK_DOCK_RIGHT;
	}
	if (d->hint == WSK_DOCK_RIGHT) { wsk__dk_hintbar(d, &d->midw[k], hfill); d->msizes[k] = 0; d->marea[k++] = -1; }
	wsk_nest_init(mid, r, WSK_ROW);
	mid->item = d->midw; mid->n = k; mid->pad = 0; mid->gap = 0; mid->sizes = d->msizes;

	/* the root: top, middle, bottom -- and the drop bar at an edge */
	k = 0;
	if (d->hint == WSK_DOCK_TOP) { wsk__dk_hintbar(d, &d->rootw[k], hfill); d->rsizes[k] = 0; d->rarea[k++] = -1; }
	if (has[WSK_DOCK_TOP]) {
		int s = d->asize[WSK_DOCK_TOP];
		wsk__dk_zero(&d->rootw[k], (int)sizeof d->rootw[k]);
		d->rootw[k].kind = WSK_STACK; d->rootw[k].sub = (short)(d->nb + wsk__dk_area(WSK_DOCK_TOP));
		d->rsizes[k] = (short)((s > maxedge) ? maxedge : (s < 40) ? 40 : s); d->rarea[k++] = WSK_DOCK_TOP;
		wsk__dk_div(&d->rootw[k], dfill); d->rsizes[k] = 0; d->rarea[k++] = -1;
	}
	wsk__dk_zero(&d->rootw[k], (int)sizeof d->rootw[k]);
	d->rootw[k].kind = WSK_STACK; d->rootw[k].sub = (short)(d->nb + wsk__dk_mid()); d->rootw[k].weight = 1;
	d->rsizes[k] = 0; d->rarea[k++] = -1;
	if (has[WSK_DOCK_BOTTOM]) {
		int s = d->asize[WSK_DOCK_BOTTOM];
		wsk__dk_div(&d->rootw[k], dfill); d->rsizes[k] = 0; d->rarea[k++] = -1;
		wsk__dk_zero(&d->rootw[k], (int)sizeof d->rootw[k]);
		d->rootw[k].kind = WSK_STACK; d->rootw[k].sub = (short)(d->nb + wsk__dk_area(WSK_DOCK_BOTTOM));
		d->rsizes[k] = (short)((s > maxedge) ? maxedge : (s < 40) ? 40 : s); d->rarea[k++] = WSK_DOCK_BOTTOM;
	}
	if (d->hint == WSK_DOCK_BOTTOM) { wsk__dk_hintbar(d, &d->rootw[k], hfill); d->rsizes[k] = 0; d->rarea[k++] = -1; }
	r->item = d->rootw; r->n = k; r->axis = WSK_COL; r->sizes = d->rsizes;
	r->pad = 0; r->gap = 0;                        /* the frame reaches the window's edges */
}

/* The edge a point is nearest, within a quarter of the window -- or -1. */
static inline int wsk__dk_edge(const wsk_dock_t *d, int x, int y)
{
	int w = d->root->w, h = d->root->h, best = -1, bd = 0;
	int dist[4];
	dist[WSK_DOCK_LEFT] = x * 4 < w ? x * 1000 / w : -1;
	dist[WSK_DOCK_RIGHT] = (w - x) * 4 < w ? (w - x) * 1000 / w : -1;
	dist[WSK_DOCK_TOP] = y * 4 < h ? y * 1000 / h : -1;
	dist[WSK_DOCK_BOTTOM] = (h - y) * 4 < h ? (h - y) * 1000 / h : -1;
	for (int a = 0; a < 4; a++)
		if (dist[a] >= 0 && (best < 0 || dist[a] < bd)) { best = a; bd = dist[a]; }
	return best;
}

/* The sizes a divider's drag wrote, kept in the layout. */
static inline void wsk__dk_keep(wsk_dock_t *d)
{
	for (int k = 0; k < d->root->n && k < 7; k++)
		if (d->rarea[k] >= 0 && d->rsizes[k] > 0) d->asize[(int)d->rarea[k]] = d->rsizes[k];
	for (int k = 0; k < wsk__dk_tree(d, wsk__dk_mid())->n && k < 7; k++)
		if (d->marea[k] >= 0 && d->msizes[k] > 0) d->asize[(int)d->marea[k]] = d->msizes[k];
	for (int a = 0; a < 4; a++)
		for (int k = 0; k < wsk__dk_tree(d, wsk__dk_area(a))->n; k++)
			if (d->apane[a][k] >= 0) d->pane[(int)d->apane[a][k]].size = d->asizes[a][k];
}

/* Whether widget w is in tree t or a tree beneath it: focus left in a pane
   that has gone out of a window is dropped. */
static inline int wsk__dk_reach(const wsk_tree_t *r, const wsk_tree_t *t, const void *w, int depth)
{
	for (int i = 0; i < t->n; i++) {
		const wsk_widget_t *it = &t->item[i];
		if ((const void *)it == w) return 1;
		if (it->kind == WSK_STACK && it->sub >= 0 && it->sub < r->nests && depth < WSK_DEPTH_MAX &&
		    wsk__dk_reach(r, &r->nest[it->sub], w, depth + 1)) return 1;
	}
	return 0;
}

/*
 * What wsk_feed said, as far as it is the dock's: a caption grabbed, dragged
 * (the drop bar following the edge it is near) and dropped (the pane moved
 * to that edge's end -- or, nearer no edge, left where it was); a close
 * button; a divider dragged; a caption double-clicked, which floats a pane
 * or docks it again (step 2). `ui` is the toolkit fed -- the window's, or a
 * floating pane's window's, whose grabs are not the dock's -- and `ev` the
 * event, for its point; `n` the notes in `note` -- what wsk_feed returned,
 * no more than the array holds (it counts what did not fit). 1 if the
 * layout changed: the frame must be drawn again whole, and a pane may have
 * gone into a window or come out of one -- and the toolkit's focus, if its
 * field went with it, is dropped. A drag the toolkit let go of with no
 * drop (a dialog, a context menu, a lifting lost) is let go of here too.
 */
static inline int wsk_dock_feed(wsk_dock_t *d, wsk_ui_t *ui, const wsk_event_t *ev,
                                const wsk_note_t *note, int n)
{
	int changed = 0, home = (ui->root == d->root);
	if (ev->kind == WSK_EV_DOWN && ev->d == 2 && ui->gin != 0) {     /* a third click is not another */
		for (int q = 0; q < d->n; q++) {          /* a caption's title or padding, not its x */
			const wsk_tree_t *ct = wsk__dk_tree(d, wsk__dk_cap(q));
			if (ui->gin != ct || (ui->hit_in == ct && ui->hit == 1) || d->pane[q].shown == 0) continue;
			d->pane[q].shown = (unsigned char)((d->pane[q].shown == 1) ? WSK_DOCK_FLOAT : 1);
			ui->gin = 0;                           /* the press is not a drag now: its tree has gone */
			ui->grabbing = 0;
			d->drag = -1;
			d->hint = -1;
			changed = 1;
		}
	}
	for (int i = 0; i < n; i++) {
		const wsk_note_t *t = &note[i];
		int p = -1;
		for (int q = 0; q < d->n; q++)
			if (t->in == wsk__dk_tree(d, wsk__dk_cap(q))) p = q;
		if (!home && (t->kind == WSK_NOTE_GRAB || t->kind == WSK_NOTE_DRAG || t->kind == WSK_NOTE_DROP)) continue;
		if (t->kind == WSK_NOTE_GRAB) {                /* any grab begins afresh: a caption's is the dock's */
			if (d->hint != -1) changed = 1;
			d->hint = -1;
			d->drag = (short)((p >= 0 && d->pane[p].shown == 1) ? p : -1);
		}
		if ((t->kind == WSK_NOTE_DRAG || t->kind == WSK_NOTE_DROP) &&
		    (d->drag < 0 || t->in != wsk__dk_tree(d, wsk__dk_cap(d->drag))))
			continue;                              /* another tree's drag: the program's */
		if ((t->kind == WSK_NOTE_GRAB || t->kind == WSK_NOTE_DRAG) && d->drag >= 0) {
			int e = wsk__dk_edge(d, ev->x, ev->y);
			if (e != d->hint) { d->hint = (short)e; changed = 1; }
		} else if (t->kind == WSK_NOTE_DROP && d->drag >= 0) {
			int e = wsk__dk_edge(d, ev->x, ev->y), top = -1;
			if (e >= 0) {
				wsk_pane_t *pp = &d->pane[d->drag];
				for (int q = 0; q < d->n; q++)
					if (q != d->drag && d->pane[q].area == e && d->pane[q].order > top) top = d->pane[q].order;
				pp->area = (unsigned char)e;
				pp->order = (short)(top + 1);
				pp->size = 0;
			}
			d->hint = -1;
			d->drag = -1;
			changed = 1;
		} else if (t->kind == WSK_NOTE_CLICK && p >= 0 && t->item == 1) {
			d->pane[p].shown = 0;                  /* its close button */
			changed = 1;
		} else if (t->kind == WSK_NOTE_SPLIT) {
			wsk__dk_keep(d);
		}
	}
	/* a drag the toolkit let go of with no drop -- a dialog opened, a
	   context menu, a lifting lost: the bar goes, the pane stays */
	if (home && d->drag >= 0 && !ui->grabbing) {
		d->drag = -1;
		d->hint = -1;
		changed = 1;
	}
	if (changed) {
		wsk_dock_build(d);
		if (ui->root->focus != 0 && !wsk__dk_reach(ui->root, ui->root, ui->root->focus, 0)) {
			wsk_rect_t z = { 0, 0, 0, 0 };
			int lo = 32767, hi = -1;
			wsk__refocus(ui, 0, -1, z, 0, &lo, &hi);   /* its field went with a pane */
		}
	}
	return changed;
}

/* Show a pane closed, or close one: 0, or WSK_E_NOTFOUND. A floating pane
   shown stays floating. Build after. */
static inline int wsk_dock_show(wsk_dock_t *d, const char *name, int shown)
{
	int i = wsk__dk_find(d, name);
	if (i < 0) return WSK_E_NOTFOUND;
	if (!shown) d->pane[i].shown = 0;
	else if (d->pane[i].shown == 0) d->pane[i].shown = 1;
	return WSK_OK;
}

/* Float a pane -- shown, in a window of its own -- or dock a floating one
   again where it was: 0, or WSK_E_NOTFOUND. Build after. */
static inline int wsk_dock_float(wsk_dock_t *d, const char *name, int on)
{
	int i = wsk__dk_find(d, name);
	if (i < 0) return WSK_E_NOTFOUND;
	if (on) d->pane[i].shown = WSK_DOCK_FLOAT;
	else if (d->pane[i].shown == WSK_DOCK_FLOAT) d->pane[i].shown = 1;
	return WSK_OK;
}

/*
 * The root of a floating pane's window, in the program's memory: w x h, its
 * one item the pane -- its caption over its content, the trees it had
 * docked -- and the window's root's trees, strings, buffers and state
 * shared, so what is in the pane is the same data in either place. Its own
 * press, focus and overlay, as a window's are. Build first; 0, or
 * WSK_E_NOTFOUND, or WSK_E_KIND for a pane that is not floating.
 */
static inline int wsk_dock_window(wsk_dock_t *d, const char *name, wsk_tree_t *w,
                                  int width, int height)
{
	const wsk_tree_t *r = d->root;
	int i = wsk__dk_find(d, name);
	if (i < 0) return WSK_E_NOTFOUND;
	if (d->pane[i].shown != WSK_DOCK_FLOAT) return WSK_E_KIND;
	wsk_init(w, WSK_COL, width, height);
	w->font = r->font; w->bg = r->bg; w->ink = r->ink; w->accent = r->accent;
	w->trough = r->trough; w->selfill = r->selfill; w->ids = r->ids;
	w->talign = r->talign; w->caps = r->caps;
	w->nest = r->nest; w->nests = r->nests; w->str = r->str; w->strs = r->strs;
	w->buf = r->buf; w->bufs = r->bufs; w->state = r->state; w->states = r->states;
	w->pad = 0; w->gap = 0;
	wsk__dk_zero(&d->floatw[i], (int)sizeof d->floatw[i]);
	d->floatw[i].kind = WSK_STACK;
	d->floatw[i].sub = (short)(d->nb + wsk__dk_pane(i));
	d->floatw[i].weight = 1;
	w->item = &d->floatw[i];
	w->n = 1;
	return WSK_OK;
}

/* Which edge a pane is on -- gBASIC's qt.dock: WSK_DOCK_*, or
   WSK_E_NOTFOUND. Whether it is shown is wsk_dock_shown's. */
static inline int wsk_dock_area(const wsk_dock_t *d, const char *name)
{
	int i = wsk__dk_find(d, name);
	return (i < 0) ? WSK_E_NOTFOUND : d->pane[i].area;
}

static inline int wsk_dock_shown(const wsk_dock_t *d, const char *name)
{
	int i = wsk__dk_find(d, name);
	return (i < 0) ? WSK_E_NOTFOUND : d->pane[i].shown;
}

/* ---- the layout as one line of text ---- */

/* The line's writers count past cap, storing only what fits, so a line
   that fits exactly is told from one that does not. */
static inline int wsk__dk_ch(char *b, int cap, int k, char c)
{
	if (k < cap - 1) b[k] = c;
	return k + 1;
}

static inline int wsk__dk_put(char *b, int cap, int k, const char *s)
{
	while (*s != '\0') k = wsk__dk_ch(b, cap, k, *s++);
	return k;
}

static inline int wsk__dk_num(char *b, int cap, int k, int v)
{
	char dg[12];
	int m = 0;
	if (v < 0) { k = wsk__dk_ch(b, cap, k, '-'); v = -v; }
	do { dg[m++] = (char)('0' + v % 10); v /= 10; } while (v > 0 && m < 11);
	while (m > 0) k = wsk__dk_ch(b, cap, k, dg[--m]);
	return k;
}

/*
 * The layout as one line: "L240 R200 T150 B160;files:L0:1:0;out:B0:1:120;"
 * -- each area's size, then each pane's name, area and order, shown (0, 1,
 * or 2 floating), and size. What gBASIC keeps Qt's saveState blob for, but readable. The
 * length, or -1 if it did not fit in cap.
 */
static inline int wsk_dock_save(const wsk_dock_t *d, char *b, int cap)
{
	static const char AREAS[] = "LRTB";
	int k = 0;
	if (cap < 2) return -1;
	for (int a = 0; a < 4; a++) {
		if (a > 0) k = wsk__dk_ch(b, cap, k, ' ');
		k = wsk__dk_ch(b, cap, k, AREAS[a]);
		k = wsk__dk_num(b, cap, k, d->asize[a]);
	}
	k = wsk__dk_ch(b, cap, k, ';');
	for (int i = 0; i < d->n; i++) {
		const wsk_pane_t *p = &d->pane[i];
		k = wsk__dk_put(b, cap, k, p->name);
		k = wsk__dk_ch(b, cap, k, ':');
		k = wsk__dk_ch(b, cap, k, AREAS[p->area]);
		k = wsk__dk_num(b, cap, k, p->order);
		k = wsk__dk_ch(b, cap, k, ':');
		k = wsk__dk_num(b, cap, k, p->shown);
		k = wsk__dk_ch(b, cap, k, ':');
		k = wsk__dk_num(b, cap, k, p->size);
		k = wsk__dk_ch(b, cap, k, ';');
	}
	if (k > cap - 1) { b[0] = '\0'; return -1; }
	b[k] = '\0';
	return k;
}

static inline int wsk__dk_area_of(char c)
{
	return (c == 'L') ? WSK_DOCK_LEFT : (c == 'R') ? WSK_DOCK_RIGHT
	     : (c == 'T') ? WSK_DOCK_TOP : (c == 'B') ? WSK_DOCK_BOTTOM : -1;
}

static inline int wsk__dk_int(const char **s, int *v)
{
	int neg = 0, any = 0, x = 0;
	if (**s == '-') { neg = 1; (*s)++; }
	while (**s >= '0' && **s <= '9') { x = x * 10 + (**s - '0'); (*s)++; any = 1; if (x > 30000) x = 30000; }
	*v = neg ? -x : x;
	return any;
}

/* A layout line read once to check it (apply 0) and once to put it back:
   the panes put back, or -1, having changed nothing, for a line that is not
   a layout -- any record malformed, not only the first. */
static inline int wsk__dk_parse(wsk_dock_t *d, const char *s, int apply)
{
	short asz[4];
	int got = 0;
	const char *q = s;
	for (int a = 0; a < 4; a++) {
		int v;
		while (*q == ' ') q++;
		if (wsk__dk_area_of(*q) != a) return -1;
		q++;
		if (!wsk__dk_int(&q, &v) || v < 0) return -1;
		asz[a] = (short)v;
	}
	if (*q != ';') return -1;
	q++;
	if (apply) for (int a = 0; a < 4; a++) d->asize[a] = asz[a];
	while (*q != '\0') {
		char nm[24];
		int k = 0, ar, order, shown, size, i;
		while (*q != '\0' && *q != ':') {
			if (k >= (int)sizeof nm - 1) return -1;   /* longer than any name */
			nm[k++] = *q++;
		}
		nm[k] = '\0';
		if (k == 0 || *q != ':') return -1;
		q++;
		if ((ar = wsk__dk_area_of(*q)) < 0) return -1;
		q++;
		if (!wsk__dk_int(&q, &order) || *q != ':') return -1;
		q++;
		if (!wsk__dk_int(&q, &shown) || *q != ':') return -1;
		q++;
		if (!wsk__dk_int(&q, &size) || *q != ';') return -1;
		q++;
		if ((i = wsk__dk_find(d, nm)) < 0) continue;
		got++;
		if (!apply) continue;
		d->pane[i].area = (unsigned char)ar;
		d->pane[i].order = (short)order;
		d->pane[i].shown = (unsigned char)((shown == WSK_DOCK_FLOAT) ? WSK_DOCK_FLOAT : (shown != 0));
		d->pane[i].size = (short)((size < 0) ? 0 : size);
	}
	return got;
}

/*
 * A layout saved, put back: the panes it names that this dock has get its
 * area, order, shown and size; names it does not know are passed over, as
 * a layout from an older program will hold. The number of panes put back,
 * or -1 for a line that is not a layout -- all of it read first, so a bad
 * record anywhere changes nothing. Build after.
 */
static inline int wsk_dock_load(wsk_dock_t *d, const char *s)
{
	if (wsk__dk_parse(d, s, 0) < 0) return -1;
	return wsk__dk_parse(d, s, 1);
}

#endif /* WHISKER_DOCK_H */
