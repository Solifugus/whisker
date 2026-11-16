/*
 * whisker_tabs.h -- tabs (design.md §5ah).
 *
 * gBASIC's `tabs` and `tab "Caption"`: a bar of tabs over a body that is
 * the chosen tab's content. Studio keeps a tab per open document, its
 * caption marked while unsaved, so a tab is addressed by a NAME and its
 * caption may change; a tab may be closable, the close asked for and the
 * program deciding; and STU-21's rule holds -- a right-click on a background
 * tab, or its close button, acts on that tab without switching to it.
 *
 * The set is made of the root's own parts, in slots the program sets aside,
 * as the chooser's and the dock's are: trees in `nest` from nest_base
 * (WSK_TABS_TREES of them), the captions in `str` from str_base
 * (WSK_TABS_MAX). The program places the set where it likes, with a stack
 * widget whose `sub` is nest_base -- so a window may hold several sets. A
 * tab's content is the program's own tree in `nest`.
 *
 *     wsk_tabs_t t;                                  caller memory
 *     wsk_tabs_init(&t, &root, strs, nest_base, str_base, WSK_TABS_TOP);
 *     wsk_tabs_add(&t, "main", "main.bas", main_tree, 1);
 *     ...
 *     wsk_tabs_build(&t);
 *     ... each event: n = wsk_feed(&ui, &ev, notes, ...);
 *         f = wsk_tabs_feed(&t, &ui, notes, n);
 *         if (f & WSK_TABS_ASKED) the close of tab t.asked is the program's;
 *         if (f) draw the set again;
 *
 * Tab names are ids in the window, as any widget's: a name used elsewhere
 * in it finds the first.
 *
 * Same rules as whisker.h: no libc, no .data or .bss of its own, no tables
 * of pointers.
 */
#ifndef WHISKER_TABS_H
#define WHISKER_TABS_H

#include "whisker.h"

#if defined(WSK_NO_NEST) || defined(WSK_NO_TEXT) || defined(WSK_NO_SCROLL) || defined(WSK_NO_INPUT)
#error "whisker_tabs.h needs nesting, text items, scrolling and the toolkit"
#endif

#ifndef WSK_TABS_MAX
#define WSK_TABS_MAX 32            /* tabs a set may have */
#endif
#define WSK_TABS_TREES (3 + WSK_TABS_MAX)   /* the tree slots a set sets aside */

enum { WSK_TABS_TOP = 0, WSK_TABS_BOTTOM = 1 };
enum { WSK_TABS_CHOSEN = 1, WSK_TABS_ASKED = 2 };   /* what wsk_tabs_feed says */

typedef struct {
	char          name[24];     /* what the program calls it, and its id */
	char          caption[48];  /* what a person reads on it */
	short         content;      /* its content: a tree in the root's `nest` */
	unsigned char closable;
} wsk_tab_t;

typedef struct {
	wsk_tree_t   *root;
	const char  **str;
	short         nb, sb;
	unsigned char bottom;       /* the bar under the body */
	int           n;
	short         cur;          /* the chosen tab, or -1 with none */
	short         asked;        /* the tab whose close button was tapped, or -1 */
	wsk_tab_t     tab[WSK_TABS_MAX];

	/* what the set is built of -- the header's, rebuilt from the tabs */
	wsk_widget_t  setw[2], barw[WSK_TABS_MAX], tabw[WSK_TABS_MAX][2];
} wsk_tabs_t;

/* ------------------------------------------------------------------ */

static inline void wsk__tb_zero(void *p, int n)
{
	unsigned char *b = (unsigned char *)p;
	for (int k = 0; k < n; k++) {
		b[k] = 0;
#if defined(__GNUC__)
		__asm__ volatile ("" ::: "memory");
#endif
	}
}

/* A tab moved along: a struct copy is a memcpy call to gcc at -Os, and a
   module has no libc. */
static inline void wsk__tb_move(wsk_tab_t *d, const wsk_tab_t *s)
{
	unsigned char *a = (unsigned char *)d;
	const unsigned char *b = (const unsigned char *)s;
	for (int k = 0; k < (int)sizeof *d; k++) {
		a[k] = b[k];
#if defined(__GNUC__)
		__asm__ volatile ("" ::: "memory");
#endif
	}
}

static inline void wsk__tb_cpy(char *d, const char *s, int cap)
{
	int i = 0;
	if (s != 0) while (s[i] != '\0' && i < cap - 1) { d[i] = s[i]; i++; }
	d[i] = '\0';
}

static inline int wsk__tb_same(const char *a, const char *b)
{
	while (*a != '\0' && *a == *b) { a++; b++; }
	return *a == *b;
}

/* The set's own trees: 0 the set, 1 the bar, 2 an empty body for a set
   with no tab, then a row for each tab -- its caption and close button. */
static inline wsk_tree_t *wsk__tb_tree(wsk_tabs_t *t, int k)
{
	return (wsk_tree_t *)&t->root->nest[t->nb + k];
}

#define WSK__TB_PAD 4
#define WSK__TB_GAP 4

/*
 * Set aside the root's slots: trees nest_base..+WSK_TABS_TREES -- the set
 * itself is nest_base, for the program to place -- and the captions'
 * strings str_base..+WSK_TABS_MAX in `str`, the root's own array. The bar
 * at the top or the bottom. WSK_E_KIND if the root's arrays do not reach.
 */
static inline int wsk_tabs_init(wsk_tabs_t *t, wsk_tree_t *root, const char **str,
                                int nest_base, int str_base, int where)
{
	if (root->nest == 0 || nest_base < 0 || nest_base + WSK_TABS_TREES > root->nests ||
	    str == 0 || (const char *const *)str != root->str || str_base < 0 ||
	    str_base + WSK_TABS_MAX > root->strs)
		return WSK_E_KIND;
	wsk__tb_zero(t, (int)sizeof *t);
	t->root = root; t->str = str;
	t->nb = (short)nest_base; t->sb = (short)str_base;
	t->bottom = (unsigned char)(where == WSK_TABS_BOTTOM);
	t->cur = -1;
	t->asked = -1;
	return WSK_OK;
}

static inline int wsk_tabs_find(const wsk_tabs_t *t, const char *name)
{
	if (name == 0) return -1;
	for (int i = 0; i < t->n; i++) if (wsk__tb_same(t->tab[i].name, name)) return i;
	return -1;
}

/* The chosen tab's name, or 0 with none. */
static inline const char *wsk_tabs_current(const wsk_tabs_t *t)
{
	return (t->cur >= 0) ? t->tab[t->cur].name : 0;
}

/*
 * A tab: at the end, or with `next` just after the chosen one -- its index,
 * or -1 (no room, a name already used or empty or longer than 23 bytes, no
 * such tree). The first tab is chosen; after that, choosing is the
 * program's. Build after.
 */
static inline int wsk_tabs_add(wsk_tabs_t *t, const char *name, const char *caption,
                               int content, int closable, int next)
{
	int at, len = 0;
	if (t->n >= WSK_TABS_MAX || name == 0 || name[0] == '\0' || content < 0 ||
	    content >= t->root->nests || wsk_tabs_find(t, name) >= 0) return -1;
	while (name[len] != '\0') if (++len >= (int)sizeof t->tab[0].name) return -1;
	at = (next && t->cur >= 0) ? t->cur + 1 : t->n;
	for (int i = t->n; i > at; i--) wsk__tb_move(&t->tab[i], &t->tab[i - 1]);
	wsk__tb_cpy(t->tab[at].name, name, (int)sizeof t->tab[at].name);
	wsk__tb_cpy(t->tab[at].caption, caption ? caption : name, (int)sizeof t->tab[at].caption);
	t->tab[at].content = (short)content;
	t->tab[at].closable = (unsigned char)(closable != 0);
	t->n++;
	if (t->cur < 0) t->cur = (short)at;
	else if (at <= t->cur) t->cur++;
	return at;
}

/*
 * A tab gone: 0, or WSK_E_NOTFOUND. The chosen one removed, the tab to its
 * right is chosen, else the one to its left (Qt's default), else none.
 * Build after.
 */
static inline int wsk_tabs_remove(wsk_tabs_t *t, const char *name)
{
	int i = wsk_tabs_find(t, name);
	if (i < 0) return WSK_E_NOTFOUND;
	for (int k = i; k < t->n - 1; k++) wsk__tb_move(&t->tab[k], &t->tab[k + 1]);
	t->n--;
	if (t->cur > i) t->cur--;
	else if (t->cur == i && t->cur >= t->n) t->cur = (short)(t->n - 1);
	t->asked = -1;
	return WSK_OK;
}

/* Choose a tab: 0, or WSK_E_NOTFOUND. Build after. */
static inline int wsk_tabs_choose(wsk_tabs_t *t, const char *name)
{
	int i = wsk_tabs_find(t, name);
	if (i < 0) return WSK_E_NOTFOUND;
	t->cur = (short)i;
	return WSK_OK;
}

/* A tab's caption changed -- Studio's "* " while unsaved: 0, or
   WSK_E_NOTFOUND. Build after. */
static inline int wsk_tabs_caption(wsk_tabs_t *t, const char *name, const char *caption)
{
	int i = wsk_tabs_find(t, name);
	if (i < 0) return WSK_E_NOTFOUND;
	wsk__tb_cpy(t->tab[i].caption, caption ? caption : t->tab[i].name, (int)sizeof t->tab[i].caption);
	return WSK_OK;
}

/* The width a tab must have: its caption whole, its button, the padding. */
static inline int wsk__tb_need(const wsk_tabs_t *t, int i)
{
	const wsk_tree_t *r = t->root;
	const char *c = t->tab[i].caption;
	int w = wsk_text_w(r->font, wsk__chars(c, wsk__len(c))) + 4 * WSK__TB_PAD;
	if (t->tab[i].closable) w += WSK__TB_GAP + r->font + 6;
	return w;
}

/*
 * The bar's scroll kept in its range, and -- with `show` -- the chosen tab
 * in view. Needs the set placed in the root, laid out where the program
 * put it; before that it does nothing.
 */
static inline void wsk__tb_view(wsk_tabs_t *t, int show)
{
	wsk_tree_t at;
	const wsk_tree_t *in = 0;
	wsk_tree_t *bar = wsk__tb_tree(t, 1);
	int k, max, lo, hi;
	wsk_rect_t r;
	if (t->n == 0) { bar->scroll = 0; return; }
	k = wsk__lookup(t->root, t->tab[(t->cur >= 0) ? t->cur : 0].name, &at, &in);
	if (k < 0 || in != bar) return;
	max = wsk_scroll_max(&at);
	if (bar->scroll > max) { bar->scroll = (short)max; at.scroll = bar->scroll; }
	if (!show || t->cur < 0) return;
	r = wsk_rect(&at, k);
	lo = at.ax + at.pad;
	hi = at.ax + at.aw - at.pad;
	if (r.x < lo) bar->scroll = (short)(bar->scroll - (lo - r.x));
	else if (r.x + r.w > hi) bar->scroll = (short)(bar->scroll + (r.x + r.w - hi));
	if (bar->scroll < 0) bar->scroll = 0;
	if (bar->scroll > max) bar->scroll = (short)max;
}

/*
 * The set, built again from the tabs: a column of the bar and the body (the
 * body first with the bar at the bottom), the bar a selecting row of the
 * tabs -- the chosen one drawn as a chosen row is (`selfill`, §5m), the
 * others in the window's background, on a bar in the trough. Call after
 * anything changes; wsk_tabs_feed does. The chosen tab is brought into view.
 */
static inline void wsk_tabs_build(wsk_tabs_t *t)
{
	wsk_tree_t *r = t->root, *set = wsk__tb_tree(t, 0), *bar = wsk__tb_tree(t, 1);
	unsigned int on = r->selfill, off = r->bg, under = r->trough;
	short scroll = bar->scroll;
	int b = t->bottom ? 1 : 0;

	for (int i = 0; i < t->n; i++) {
		wsk_tree_t *row = wsk__tb_tree(t, 3 + i);
		wsk_widget_t *tw = t->tabw[i], *bw = &t->barw[i];
		t->str[t->sb + i] = t->tab[i].caption;
		wsk__tb_zero(tw, (int)sizeof t->tabw[i]);
		tw[0].kind = WSK_TEXT; tw[0].sub = (short)(t->sb + i); tw[0].weight = 1;
		tw[1].kind = WSK_BUTTON; tw[1].size = (short)(r->font + 6);
		tw[1].id[0] = 'x'; tw[1].text[0] = 'x';
		wsk_nest_init(row, r, WSK_ROW);
		row->item = tw; row->n = t->tab[i].closable ? 2 : 1;
		row->pad = WSK__TB_PAD; row->gap = WSK__TB_GAP; row->talign = WSK_ALIGN_START;
		wsk__tb_zero(bw, (int)sizeof *bw);
		bw->kind = WSK_STACK; bw->sub = (short)(t->nb + 3 + i);
		bw->size = (short)wsk__tb_need(t, i);
		bw->fill = off;
		wsk__tb_cpy(bw->id, t->tab[i].name, WSK_ID_MAX);
	}
	for (int i = t->n; i < WSK_TABS_MAX; i++) {     /* slots no tab uses: empty, */
		wsk_nest_init(wsk__tb_tree(t, 3 + i), r, WSK_ROW);   /* as a check reads every tree */
		t->str[t->sb + i] = "";
	}
	wsk_nest_init(wsk__tb_tree(t, 2), r, WSK_COL);

	wsk_nest_init(bar, r, WSK_ROW);
	bar->item = t->barw; bar->n = t->n; bar->pad = 0; bar->gap = 2;
	bar->overflow = WSK_SCROLL; bar->scroll = scroll;
	bar->select = 1; bar->sel = (short)((t->cur >= 0) ? t->cur : WSK_NONE); bar->selfill = on;

	wsk__tb_zero(t->setw, (int)sizeof t->setw);
	t->setw[b].kind = WSK_STACK; t->setw[b].sub = (short)(t->nb + 1);
	t->setw[b].size = (short)(r->font + 10); t->setw[b].fill = under;
	t->setw[1 - b].kind = WSK_STACK; t->setw[1 - b].weight = 1;
	t->setw[1 - b].sub = (t->cur >= 0) ? t->tab[t->cur].content : (short)(t->nb + 2);
	wsk_nest_init(set, r, WSK_COL);
	set->item = t->setw; set->n = 2; set->pad = 0; set->gap = 0;

	wsk__tb_view(t, 1);
}

/* Whether widget w is in tree t or a tree beneath it: focus left in a body
   that is no longer shown is dropped. */
static inline int wsk__tb_reach(const wsk_tree_t *r, const wsk_tree_t *t, const void *w, int depth)
{
	for (int i = 0; i < t->n; i++) {
		const wsk_widget_t *it = &t->item[i];
		if ((const void *)it == w) return 1;
		if (it->kind == WSK_STACK && it->sub >= 0 && it->sub < r->nests && depth < WSK_DEPTH_MAX &&
		    wsk__tb_reach(r, &r->nest[it->sub], w, depth + 1)) return 1;
	}
	return 0;
}

/* Which tab a note is about -- a right-click's MENU, a CLICK -- or -1. */
static inline int wsk_tabs_at(wsk_tabs_t *t, const wsk_note_t *note)
{
	if (note->in == wsk__tb_tree(t, 1)) return (note->item >= 0 && note->item < t->n) ? note->item : -1;
	for (int i = 0; i < t->n; i++) if (note->in == wsk__tb_tree(t, 3 + i)) return i;
	return -1;
}

/*
 * What wsk_feed said, as far as it is the set's: a tab chosen -- tapped,
 * or by the keys of a list (§5z) -- WSK_TABS_CHOSEN, the set rebuilt and
 * the toolkit's focus dropped if its field went with the old body; a close
 * button tapped, WSK_TABS_ASKED with `t->asked` its tab, and the chosen tab
 * as it was (the toolkit chose the tapped one too, as a tap in a list
 * does: that is undone). 0 if neither. `n` no more than `note` holds.
 */
static inline int wsk_tabs_feed(wsk_tabs_t *t, wsk_ui_t *ui, const wsk_note_t *note, int n)
{
	wsk_tree_t *bar = wsk__tb_tree(t, 1);
	int said = 0, closed = -1;
	for (int i = 0; i < n; i++) {
		int k = wsk_tabs_at(t, &note[i]);
		if (note[i].kind == WSK_NOTE_CLICK && k >= 0 && note[i].in != bar && note[i].item == 1 &&
		    t->tab[k].closable)
			closed = k;
	}
	if (closed >= 0) {                             /* its SELECT passed over: the */
		t->asked = (short)closed;                  /* rebuild puts the bar's choice */
		said |= WSK_TABS_ASKED;                    /* back where it was */
	} else {
		for (int i = 0; i < n; i++)
			if (note[i].kind == WSK_NOTE_SELECT && note[i].in == bar && note[i].value >= 0 &&
			    note[i].value < t->n && note[i].value != t->cur) {
				t->cur = (short)note[i].value;
				said |= WSK_TABS_CHOSEN;
			}
	}
	if (said) {
		wsk_tabs_build(t);
		if (ui->root->focus != 0 && !wsk__tb_reach(ui->root, ui->root, ui->root->focus, 0)) {
			wsk_rect_t z = { 0, 0, 0, 0 };
			int lo = 32767, hi = -1;
			wsk__refocus(ui, 0, -1, z, 0, &lo, &hi);   /* its field went with the old body */
		}
	}
	return said;
}

#endif /* WHISKER_TABS_H */
