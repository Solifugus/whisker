/*
 * whisker_chooser.h -- choosing a file (design.md §5ae).
 *
 * gBASIC asks three questions -- which existing file, what shall I call it,
 * which folder -- and so does this: WSK_CHOOSE_FILE, _SAVE and _FOLDER. It
 * builds a chooser in the CALLER's memory, as everything in whisker is, and
 * opens it as a modal dialog (wsk_ui_dialog). It never touches a filesystem
 * -- whisker has no libc, by rule -- so the program gives it a function that
 * lists a folder: POSIX on a desktop, the server's in a browser host, RV-9's
 * on a board.
 *
 * The dialog is made of the root's own parts, in slots the program sets
 * aside: four trees in `nest` from nest_base, strings in `str` from
 * str_base (two, and one per entry), and one buffer in `buf` at buf_base
 * for the name. The program owns those arrays and sizes them; the chooser
 * writes only the slots it was given.
 *
 *     wsk_chooser_t ch;                       caller memory, ~22 KB
 *     wsk_chooser_init(&ch, &root, strs, nest_base, str_base, buf_base, lister, ctx);
 *     wsk_chooser_open(&ch, &ui, WSK_CHOOSE_FILE, "Open", "/home/me", 0, "*.bas", &y0, &y1);
 *     ... each event: n = wsk_feed(&ui, &ev, notes, ...);
 *         r = wsk_chooser_feed(&ch, &ui, notes, n, &y0, &y1);
 *         WSK_CHOSEN: the path is ch.path.  WSK_CANCELLED: nothing was chosen.
 *
 * A click chooses a row; a click on the chosen row again goes into a folder,
 * or picks a file -- a double-click without counting. Up goes out. OK (Open,
 * Save or Choose) answers; Cancel or Escape answers cancelled, never an
 * empty path. Folders first, each sorted; names that begin with '.' and
 * names the window cannot draw are left out; the filter, `*.bas *.txt`,
 * admits files, never folders.
 *
 * Same rules as whisker.h: no libc, no .data or .bss of its own, no tables
 * of pointers.
 */
#ifndef WHISKER_CHOOSER_H
#define WHISKER_CHOOSER_H

#include "whisker.h"

#if defined(WSK_NO_OVER) || defined(WSK_NO_TEXT) || defined(WSK_NO_FIELD) || \
    defined(WSK_NO_SCROLL) || defined(WSK_NO_NEST) || defined(WSK_NO_INPUT)
#error "whisker_chooser.h needs overlays, text items, fields, scrolling, nesting and the toolkit"
#endif

#ifndef WSK_CH_MAX
#define WSK_CH_MAX    256          /* entries a folder may show */
#endif
#ifndef WSK_CH_NAMES
#define WSK_CH_NAMES  8192         /* bytes of names, a folder's */
#endif
#define WSK_CH_PATH   512          /* a path, and the name typed */

enum { WSK_CHOOSE_FILE = 0, WSK_CHOOSE_SAVE, WSK_CHOOSE_FOLDER };
enum { WSK_CHOSEN = 1, WSK_CANCELLED = 2 };

/* An entry of a folder: its name, an offset into the names, and whether it
   is a folder itself. */
typedef struct {
	int           name;
	unsigned char dir;
} wsk_entry_t;

/*
 * The program's: list `folder` into e[0..max) with their names, NUL-ended,
 * in names[0..cap); the count, or -1 if the folder cannot be read. Order
 * and hidden names do not matter -- the chooser sorts and leaves out.
 */
typedef int (*wsk_lister_t)(void *ctx, const char *folder, wsk_entry_t *e, int max,
                            char *names, int cap);

typedef struct {
	wsk_tree_t   *root;
	const char  **str;          /* the root's strings, which the program owns */
	short         nb, sb, bb;   /* the slots: trees, strings, the buffer */
	wsk_lister_t  list;
	void         *ctx;

	int           kind, open, n, prev;
	char          folder[WSK_CH_PATH];
	char          path[WSK_CH_PATH];       /* the answer, when WSK_CHOSEN */
	char          title[64];
	char          filter[128];
	char          name[WSK_CH_PATH];       /* the name field's text */
	wsk_entry_t   ent[WSK_CH_MAX];
	char          names[WSK_CH_NAMES];
	char          shown[WSK_CH_NAMES];     /* as shown: a folder's with a '/' */

	wsk_widget_t  dlg[5], prow[2], rows[WSK_CH_MAX], brow[3];
} wsk_chooser_t;

/* ------------------------------------------------------------------ */

static inline int wsk__ch_len(const char *s) { int n = 0; while (s[n] != '\0') n++; return n; }

/* Bytes to zero, a loop kept a loop: one the compiler sees as memset would
   become a call a module cannot link (as wsk__tcopy says). */
static inline void wsk__ch_zero(void *p, int n)
{
	unsigned char *b = (unsigned char *)p;
	for (int k = 0; k < n; k++) {
		b[k] = 0;
#if defined(__GNUC__)
		__asm__ volatile ("" ::: "memory");
#endif
	}
}

static inline void wsk__ch_cpy(char *d, const char *s, int cap)
{
	int i = 0;
	if (s != 0) while (s[i] != '\0' && i < cap - 1) { d[i] = s[i]; i++; }
	d[i] = '\0';
}

static inline int wsk__ch_lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* Names in order, ASCII letters without their case. */
static inline int wsk__ch_cmp(const char *a, const char *b)
{
	for (;; a++, b++) {
		int x = wsk__ch_lower((unsigned char)*a), y = wsk__ch_lower((unsigned char)*b);
		if (x != y) return x - y;
		if (x == 0) return 0;
	}
}

/* A name against one pattern: '*' any run, '?' any one character. */
static inline int wsk__ch_glob(const char *p, int pn, const char *s)
{
	int i = 0, j = 0, star = -1, back = 0;
	while (s[j] != '\0') {
		if (i < pn && (p[i] == '?' || p[i] == s[j])) { i++; j++; }
		else if (i < pn && p[i] == '*') { star = i++; back = j; }
		else if (star >= 0) { i = star + 1; j = ++back; }
		else return 0;
	}
	while (i < pn && p[i] == '*') i++;
	return i == pn;
}

/* Whether the filter -- patterns apart by spaces, or none -- admits a file. */
static inline int wsk__ch_admits(const char *f, const char *name)
{
	int any = 0;
	for (int i = 0; f[i] != '\0';) {
		int k = i;
		while (f[k] != '\0' && f[k] != ' ' && f[k] != ';') k++;
		if (k > i) {
			any = 1;
			if (wsk__ch_glob(f + i, k - i, name)) return 1;
		}
		i = (f[k] == '\0') ? k : k + 1;
	}
	return !any;
}

/* Folder and name joined: "/" and "a" make "/a", "/x" and "a" "/x/a"; a
   name that is a path from the top already is itself. 0, or -1 if it would
   not fit -- never a path cut short, which would name another file (found
   by review, 2026-11-16). */
static inline int wsk__ch_join(char *out, const char *folder, const char *name)
{
	int k = 0;
	if (name[0] == '/') {
		if (wsk__ch_len(name) >= WSK_CH_PATH) return -1;
		wsk__ch_cpy(out, name, WSK_CH_PATH);
		return 0;
	}
	for (int i = 0; folder[i] != '\0'; i++) { if (k >= WSK_CH_PATH - 1) return -1; out[k++] = folder[i]; }
	if (k > 0 && out[k - 1] != '/') { if (k >= WSK_CH_PATH - 1) return -1; out[k++] = '/'; }
	for (int i = 0; name[i] != '\0'; i++) { if (k >= WSK_CH_PATH - 1) return -1; out[k++] = name[i]; }
	out[k] = '\0';
	return 0;
}

static inline wsk_tree_t *wsk__ch_tree(wsk_chooser_t *ch, int k)
{
	return (wsk_tree_t *)&ch->root->nest[ch->nb + k];
}

/*
 * The folder read again: listed, the hidden and the undrawable left out,
 * filtered, sorted -- folders first -- and its rows built. 0, or -1 if it
 * could not be read, the old rows kept.
 */
static inline int wsk__ch_read(wsk_chooser_t *ch)
{
	wsk_entry_t *e = ch->ent;
	int n = ch->list(ch->ctx, ch->folder, e, WSK_CH_MAX, ch->names, WSK_CH_NAMES), m = 0, at = 0;
	wsk_tree_t *list = wsk__ch_tree(ch, 2);

	if (n < 0) return -1;
	if (n > WSK_CH_MAX) n = WSK_CH_MAX;
	for (int i = 0; i < n; i++) {
		const char *nm;
		int end = e[i].name;
		if (e[i].name < 0 || e[i].name >= WSK_CH_NAMES) continue;
		while (end < WSK_CH_NAMES && ch->names[end] != '\0') end++;
		if (end >= WSK_CH_NAMES) continue;                        /* a name the lister did not end */
		nm = ch->names + e[i].name;
		if (nm[0] == '\0' || nm[0] == '.') continue;
		if (!wsk__text_ok(nm, ch->root->caps)) continue;          /* the window cannot draw it */
		if (!e[i].dir && ch->kind == WSK_CHOOSE_FOLDER) continue;
		if (!e[i].dir && !wsk__ch_admits(ch->filter, nm)) continue;
		e[m++] = e[i];
	}
	for (int i = 1; i < m; i++)                                    /* folders first, then by name */
		for (int j = i; j > 0; j--) {
			wsk_entry_t a = e[j - 1], b = e[j];
			int c = (a.dir != b.dir) ? (b.dir - a.dir)
			      : wsk__ch_cmp(ch->names + a.name, ch->names + b.name);
			if (c <= 0) break;
			e[j - 1] = b; e[j] = a;
		}
	for (int i = 0; i < m; i++) {                                 /* as shown: "name/" for a folder */
		const char *nm = ch->names + e[i].name;
		int len = wsk__ch_len(nm);
		if (at + len + 2 > WSK_CH_NAMES) { m = i; break; }
		ch->str[ch->sb + 2 + i] = ch->shown + at;
		for (int k = 0; k < len; k++) ch->shown[at++] = nm[k];
		if (e[i].dir) ch->shown[at++] = '/';
		ch->shown[at++] = '\0';
		wsk__ch_zero(&ch->rows[i], (int)sizeof ch->rows[i]);
		ch->rows[i].kind = WSK_TEXT;
		{                                               /* "r0", "r1", ...: an id each */
			char dg[8];
			int d = 1, k = 0, v = i;
			do { dg[k++] = (char)('0' + v % 10); v /= 10; } while (v > 0 && k < 7);
			ch->rows[i].id[0] = 'r';
			while (k > 0 && d < WSK_ID_MAX - 1) ch->rows[i].id[d++] = dg[--k];
			ch->rows[i].id[d] = '\0';
		}
		ch->rows[i].sub = (short)(ch->sb + 2 + i);
		ch->rows[i].size = (short)(ch->root->font + 8);
	}
	ch->n = m;
	list->n = m;
	list->sel = WSK_NONE;
	list->scroll = 0;
	ch->prev = -1;
	return 0;
}

/*
 * Set aside the root's slots for a chooser: trees nest_base..+3, strings
 * str_base..+WSK_CH_MAX+1 in `str` -- the root's own array, writable -- and
 * buffer buf_base. WSK_E_KIND if the root's arrays do not reach that far.
 */
static inline int wsk_chooser_init(wsk_chooser_t *ch, wsk_tree_t *root, const char **str,
                                   int nest_base, int str_base, int buf_base,
                                   wsk_lister_t list, void *ctx)
{
	if (root->nest == 0 || nest_base < 0 || nest_base + 4 > root->nests ||
	    str == 0 || (const char *const *)str != root->str || str_base < 0 ||
	    str_base + 2 + WSK_CH_MAX > root->strs ||
	    root->buf == 0 || buf_base < 0 || buf_base >= root->bufs || list == 0)
		return WSK_E_KIND;
	ch->root = root; ch->str = str;
	ch->nb = (short)nest_base; ch->sb = (short)str_base; ch->bb = (short)buf_base;
	ch->list = list; ch->ctx = ctx;
	ch->open = 0; ch->n = 0; ch->prev = -1;
	ch->folder[0] = ch->path[0] = ch->title[0] = ch->filter[0] = ch->name[0] = '\0';
	return WSK_OK;
}

/*
 * Open it: a title, the folder to start in, a name to offer (SAVE), and a
 * filter, any of them 0. w x h is the dialog's size, cut to the window. The
 * rows to repaint; WSK_E_NOTFOUND if the folder cannot be read.
 */
static inline int wsk_chooser_open(wsk_chooser_t *ch, wsk_ui_t *ui, int kind, const char *title,
                                   const char *folder, const char *name, const char *filter,
                                   int w, int h, int *y0, int *y1)
{
	wsk_tree_t *r = ch->root, *d = wsk__ch_tree(ch, 0), *p = wsk__ch_tree(ch, 1),
	           *l = wsk__ch_tree(ch, 2), *b = wsk__ch_tree(ch, 3);
	wsk_buf_t *nb = &r->buf[ch->bb];
	int k = 0, f = r->font;

	ch->kind = kind;
	wsk__ch_cpy(ch->title, title ? title : (kind == WSK_CHOOSE_SAVE) ? "Save as"
	                                     : (kind == WSK_CHOOSE_FOLDER) ? "Choose a folder" : "Open",
	            (int)sizeof ch->title);
	wsk__ch_cpy(ch->folder, (folder && folder[0]) ? folder : "/", WSK_CH_PATH);
	wsk__ch_cpy(ch->filter, filter ? filter : "", (int)sizeof ch->filter);
	wsk__ch_cpy(ch->name, (name && kind != WSK_CHOOSE_FOLDER) ? name : "", WSK_CH_PATH);
	ch->path[0] = '\0';
	ch->str[ch->sb] = ch->title;
	ch->str[ch->sb + 1] = ch->folder;

	/* the dialog: a column of title, folder, list, name, buttons */
	wsk__ch_zero(ch->dlg, (int)sizeof ch->dlg);
	ch->dlg[k].kind = WSK_TEXT; ch->dlg[k].id[0] = 't'; ch->dlg[k].sub = ch->sb; ch->dlg[k].size = (short)(f + 12); k++;
	ch->dlg[k].kind = WSK_STACK; ch->dlg[k].id[0] = 'p'; ch->dlg[k].sub = (short)(ch->nb + 1); ch->dlg[k].size = (short)(f + 16); k++;
	ch->dlg[k].kind = WSK_STACK; ch->dlg[k].id[0] = 'l'; ch->dlg[k].sub = (short)(ch->nb + 2); ch->dlg[k].weight = 1;
	ch->dlg[k].fill = r->trough; k++;
	if (kind != WSK_CHOOSE_FOLDER) {
		ch->dlg[k].kind = WSK_FIELD; ch->dlg[k].id[0] = 'n'; ch->dlg[k].sub = ch->bb; ch->dlg[k].size = (short)(f + 12); k++;
	}
	ch->dlg[k].kind = WSK_STACK; ch->dlg[k].id[0] = 'b'; ch->dlg[k].sub = (short)(ch->nb + 3); ch->dlg[k].size = (short)(f + 20); k++;

	wsk__ch_zero(ch->prow, (int)sizeof ch->prow);
	ch->prow[0].kind = WSK_BUTTON; ch->prow[0].id[0] = 'u'; ch->prow[0].size = (short)(4 * f);
	wsk__ch_cpy(ch->prow[0].text, "Up", WSK_TEXT_MAX);
	ch->prow[1].kind = WSK_TEXT; ch->prow[1].id[0] = 'f'; ch->prow[1].sub = (short)(ch->sb + 1); ch->prow[1].weight = 1;

	wsk__ch_zero(ch->brow, (int)sizeof ch->brow);
	ch->brow[0].kind = WSK_LABEL; ch->brow[0].id[0] = 's'; ch->brow[0].weight = 1;
	ch->brow[1].kind = WSK_BUTTON; ch->brow[1].id[0] = 'c'; ch->brow[1].size = (short)(7 * f);
	wsk__ch_cpy(ch->brow[1].text, "Cancel", WSK_TEXT_MAX);
	ch->brow[2].kind = WSK_BUTTON; ch->brow[2].id[0] = 'o'; ch->brow[2].size = (short)(7 * f);
	wsk__ch_cpy(ch->brow[2].text, (kind == WSK_CHOOSE_SAVE) ? "Save"
	                              : (kind == WSK_CHOOSE_FOLDER) ? "Choose" : "Open", WSK_TEXT_MAX);

	wsk_nest_init(d, r, WSK_COL); d->item = ch->dlg; d->n = k; d->pad = 8; d->gap = 6;
	d->talign = WSK_ALIGN_START;
	wsk_nest_init(p, r, WSK_ROW); p->item = ch->prow; p->n = 2; p->pad = 0; p->gap = 8;
	p->talign = WSK_ALIGN_START;
	wsk_nest_init(l, r, WSK_COL); l->item = ch->rows; l->n = 0; l->pad = 0; l->gap = 0;
	l->overflow = WSK_SCROLL; l->select = 1; l->sel = WSK_NONE; l->talign = WSK_ALIGN_START;
	wsk_nest_init(b, r, WSK_ROW); b->item = ch->brow; b->n = 3; b->pad = 0; b->gap = 8;

	wsk__ch_zero(nb, (int)sizeof *nb);
	nb->b = ch->name; nb->cap = WSK_CH_PATH; nb->len = wsk__ch_len(ch->name); nb->caret = nb->len;

	if (wsk__ch_read(ch) != 0) return WSK_E_NOTFOUND;
	{
		int e = wsk_ui_dialog(ui, ch->nb, w, h, y0, y1);
		ch->open = (e == WSK_OK);           /* open only if its dialog is */
		return e;
	}
}

/* The dialog closed with an answer: WSK_CHOSEN with ch->path, or
   WSK_CANCELLED -- and focus back where it was. */
static inline int wsk__ch_end(wsk_chooser_t *ch, wsk_ui_t *ui, int how, int *y0, int *y1)
{
	int a0 = 0, a1 = 0;
	if (ch->root->over == ch->nb) (void)wsk_ui_dialog_close(ui, &a0, &a1);
	if (a1 > a0) {
		if (*y1 <= *y0) { *y0 = a0; *y1 = a1; }
		else { if (a0 < *y0) *y0 = a0; if (a1 > *y1) *y1 = a1; }
	}
	ch->open = 0;
	if (how == WSK_CANCELLED) ch->path[0] = '\0';
	return how;
}

/* Into another folder, or out of this one: read, and all of it repainted.
   Opening a file, a name typed or chosen in the folder left is left behind
   with it; saving, the name is what is being saved, and stays. */
static inline void wsk__ch_go(wsk_chooser_t *ch, const char *to, int *y0, int *y1)
{
	char was[WSK_CH_PATH];
	wsk__ch_cpy(was, ch->folder, WSK_CH_PATH);
	wsk__ch_cpy(ch->folder, to, WSK_CH_PATH);
	if (wsk__ch_read(ch) != 0) {                   /* unreadable: stay where it was */
		wsk__ch_cpy(ch->folder, was, WSK_CH_PATH);
		(void)wsk__ch_read(ch);
	} else if (ch->kind == WSK_CHOOSE_FILE) {
		wsk_buf_t *nb = &ch->root->buf[ch->bb];
		ch->name[0] = '\0';
		nb->len = nb->caret = nb->span = 0;
	}
	(void)wsk_over_rows(ch->root, y0, y1);
}

/* The folder above this one: "/a/b" is "/a", "/a" is "/"; a relative one,
   "proj/src" is "proj", "proj" is ".", "." is ".." -- never the top (found
   by review: a program with no absolute path started at "."). */
static inline void wsk__ch_up(wsk_chooser_t *ch, int *y0, int *y1)
{
	char to[WSK_CH_PATH];
	int k, rel = (ch->folder[0] != '/');
	wsk__ch_cpy(to, ch->folder, WSK_CH_PATH);
	k = wsk__ch_len(to);
	while (k > 1 && to[k - 1] == '/') to[--k] = '\0';
	if (rel) {
		int dots = (k == 1 && to[0] == '.') || (k >= 2 && to[k - 1] == '.' && to[k - 2] == '.' &&
		                                        (k == 2 || to[k - 3] == '/'));
		if (dots) {                                /* "." or "..": one more ".." */
			if (k + 3 >= WSK_CH_PATH) return;
			if (k == 1) { to[0] = '.'; to[1] = '.'; to[2] = '\0'; }
			else { to[k] = '/'; to[k + 1] = '.'; to[k + 2] = '.'; to[k + 3] = '\0'; }
		} else {
			while (k > 0 && to[k - 1] != '/') k--;
			if (k == 0) { to[0] = '.'; to[1] = '\0'; } else to[k - 1] = '\0';
		}
	} else {
		while (k > 0 && to[k - 1] != '/') k--;
		if (k <= 1) { to[0] = '/'; to[1] = '\0'; } else to[k - 1] = '\0';
	}
	wsk__ch_go(ch, to, y0, y1);
}

/* OK -- Open, Save or Choose: what that means for each question. */
static inline int wsk__ch_ok(wsk_chooser_t *ch, wsk_ui_t *ui, int *y0, int *y1)
{
	wsk_tree_t *l = wsk__ch_tree(ch, 2);
	int s = l->sel;
	if (s >= 0 && s < ch->n && ch->ent[s].dir && ch->kind != WSK_CHOOSE_FOLDER) {
		char to[WSK_CH_PATH];                      /* a folder chosen: go into it */
		if (wsk__ch_join(to, ch->folder, ch->names + ch->ent[s].name) == 0) wsk__ch_go(ch, to, y0, y1);
		return 0;
	}
	if (ch->kind == WSK_CHOOSE_FOLDER) {
		if (s >= 0 && s < ch->n) {
			if (wsk__ch_join(ch->path, ch->folder, ch->names + ch->ent[s].name) != 0) return 0;
		} else wsk__ch_cpy(ch->path, ch->folder, WSK_CH_PATH);
		return wsk__ch_end(ch, ui, WSK_CHOSEN, y0, y1);
	}
	if (ch->name[0] == '\0') return 0;             /* nothing named yet */
	if (wsk__ch_join(ch->path, ch->folder, ch->name) != 0) return 0;   /* too long: stays open */
	return wsk__ch_end(ch, ui, WSK_CHOSEN, y0, y1);
}

/*
 * What wsk_feed said, as far as it is the chooser's: a row chosen, Up, OK,
 * Cancel, Enter in the name, Escape. 0 while it stays open; WSK_CHOSEN with
 * ch->path; WSK_CANCELLED. y0..y1 are widened by what it repainted.
 */
static inline int wsk_chooser_feed(wsk_chooser_t *ch, wsk_ui_t *ui, const wsk_note_t *note,
                                   int n, int *y0, int *y1)
{
	const wsk_tree_t *d = wsk__ch_tree(ch, 0), *p = wsk__ch_tree(ch, 1),
	                 *l = wsk__ch_tree(ch, 2), *b = wsk__ch_tree(ch, 3);
	if (!ch->open) return 0;
	for (int i = 0; i < n; i++) {
		const wsk_note_t *t = &note[i];
		if (t->kind == WSK_NOTE_CLOSE && t->in == d) return wsk__ch_end(ch, ui, WSK_CANCELLED, y0, y1);
		if (t->kind == WSK_NOTE_ENTER && t->in == d) return wsk__ch_ok(ch, ui, y0, y1);
		/* a row chosen by the arrows names its file, as a click does --
		   or OK would answer the last file clicked (found by review) */
		if (t->kind == WSK_NOTE_SELECT && t->in == l && t->item >= 0 && t->item < ch->n &&
		    !ch->ent[t->item].dir && ch->kind != WSK_CHOOSE_FOLDER) {
			wsk_buf_t *nb = &ch->root->buf[ch->bb];
			int a0, a1;
			wsk__ch_cpy(ch->name, ch->names + ch->ent[t->item].name, WSK_CH_PATH);
			nb->len = wsk__ch_len(ch->name); nb->caret = nb->len; nb->span = 0;
			if (wsk_over_rows(ch->root, &a0, &a1) == WSK_OK) {
				if (*y1 <= *y0) { *y0 = a0; *y1 = a1; }
				else { if (a0 < *y0) *y0 = a0; if (a1 > *y1) *y1 = a1; }
			}
			continue;
		}
		if (t->kind != WSK_NOTE_CLICK) continue;
		if (t->in == b && t->item == 1) return wsk__ch_end(ch, ui, WSK_CANCELLED, y0, y1);
		if (t->in == b && t->item == 2) return wsk__ch_ok(ch, ui, y0, y1);
		if (t->in == p && t->item == 0) { wsk__ch_up(ch, y0, y1); return 0; }
		if (t->in == l && t->item >= 0 && t->item < ch->n) {
			int k = t->item, again = (k == ch->prev);
			const wsk_entry_t *e = &ch->ent[k];
			ch->prev = k;
			if (!e->dir && ch->kind != WSK_CHOOSE_FOLDER) {
				wsk_buf_t *nb = &ch->root->buf[ch->bb];
				int a0, a1;
				wsk__ch_cpy(ch->name, ch->names + e->name, WSK_CH_PATH);
				nb->len = wsk__ch_len(ch->name); nb->caret = nb->len; nb->span = 0;
				if (wsk_over_rows(ch->root, &a0, &a1) == WSK_OK) {
					if (*y1 <= *y0) { *y0 = a0; *y1 = a1; }
					else { if (a0 < *y0) *y0 = a0; if (a1 > *y1) *y1 = a1; }
				}
			}
			if (again) {
				if (e->dir) {                      /* the chosen folder again: into it */
					char to[WSK_CH_PATH];
					if (wsk__ch_join(to, ch->folder, ch->names + e->name) == 0) wsk__ch_go(ch, to, y0, y1);
					return 0;
				}
				return wsk__ch_ok(ch, ui, y0, y1); /* the chosen file again: it */
			}
		}
	}
	return 0;
}

/*
 * Answer it as a person would, for tests -- gBASIC's qt.answer: a path is
 * chosen, 0 cancels. WSK_CHOSEN or WSK_CANCELLED; 0 if it was not open.
 */
static inline int wsk_chooser_answer(wsk_chooser_t *ch, wsk_ui_t *ui, const char *path,
                                     int *y0, int *y1)
{
	*y0 = *y1 = 0;
	if (!ch->open) return 0;
	if (path == 0) return wsk__ch_end(ch, ui, WSK_CANCELLED, y0, y1);
	if (wsk__ch_join(ch->path, ch->folder, path) != 0) return 0;   /* too long: not answered */
	return wsk__ch_end(ch, ui, WSK_CHOSEN, y0, y1);
}

#endif /* WHISKER_CHOOSER_H */
