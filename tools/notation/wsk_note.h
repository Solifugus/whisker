/*
 * wsk_note -- a panel, written down.
 *
 * One node per line. A line opens with dots that say how deep it is, so a
 * section ends where the next one begins and nothing is closed explicitly:
 *
 *     .col pad=4 gap=4
 *     ..lbl title "Settings"
 *     ..btn ok "OK"
 *     ..btn cancel "Cancel"
 *
 * Three things follow from the dots, and the third is the reason to prefer
 * them over indentation.
 *
 *   - No closing tags. `</text>` and its kind are about a quarter of the
 *     bytes of the SVG this expands into, and they carry no information the
 *     next line does not already give.
 *   - Depth survives whitespace. A tab that became spaces, a line re-wrapped
 *     by a mailer, a file edited with `ed` on the board -- none of them move
 *     a node, because the level is data rather than layout.
 *   - **A line is addressable on its own.** Its dots and its name locate it
 *     without reading the rest of the file, so an update can be a few lines
 *     rather than a new document, and applying one tells you exactly which
 *     widgets changed. That list is what a clipped repaint needs: the ids go
 *     straight to rows(), and the rows go to RV9_SVG_SS_ROWS.
 *
 * This is NOT whisker's wire format. /w0 receives SVG and nothing here
 * changes that (docs/notation-sketch.md prices the alternative and argues
 * against it). This is the form a panel is *written* and *stored* in -- by a
 * person with an editor, or by a gBASIC program -- and it is isomorphic to
 * the widget tree rather than to the XML, which is where the compression is.
 *
 * Module rules (docs/review-2026-09-30.md §3.1): no libc, no allocation, no
 * 64-bit division, no table of pointers. The caller owns every byte. Nodes
 * and text live in two arrays it provides, which is the shape D5 asks for
 * anyway -- whisker is pure and the caller retains the tree.
 */
#ifndef WSK_NOTE_H
#define WSK_NOTE_H

#include <stddef.h>

/* ---- the tree ---------------------------------------------------- */

#define WSK_MAX_LEVEL 8          /* the driver's MAX_DEPTH; deeper is refused */
#define WSK_UNSET     (-1)
#define WSK_NO_FILL   0xFFFFFFFFu

enum {
	WSK_COL = 1,
	WSK_ROW,
	WSK_LBL,
	WSK_BTN
};

typedef struct {
	unsigned char  level;        /* 1 = top */
	unsigned char  kind;
	unsigned short name;         /* offset into pool; 0 = unnamed */
	unsigned short text;         /* offset into pool; 0 = none */
	short          pad, gap, h;  /* WSK_UNSET when the line did not say */
	unsigned int   fill;         /* WSK_NO_FILL when the line did not say */
} wsk_node_t;

typedef struct {
	wsk_node_t *node;
	int         nodes;
	int         node_cap;
	char       *pool;            /* names and texts, NUL separated */
	int         pool_len;
	int         pool_cap;
} wsk_doc_t;

/* ---- errors ------------------------------------------------------ */

enum {
	WSK_OK = 0,
	WSK_E_SYNTAX = -1,           /* a line the grammar does not admit */
	WSK_E_KIND = -2,             /* .thing where thing is not a widget */
	WSK_E_LEVEL = -3,            /* skipped a level, or went past WSK_MAX_LEVEL */
	WSK_E_TEXT = -4,             /* a character a label may not carry */
	WSK_E_DUP = -5,              /* two nodes with one name */
	WSK_E_ROOM = -6,             /* the caller's arrays are full */
	WSK_E_NOTFOUND = -7          /* a patch named a node that is not there */
};

typedef struct {
	int line;                    /* 1-based, 0 when the error has no line */
	int code;
} wsk_err_t;

/* ---- the small things libc would otherwise provide ---------------- */

static inline int wsk__len(const char *s)
{
	int n = 0;
	while (s[n] != '\0') n++;
	return n;
}

static inline int wsk__same(const char *a, const char *b, int n)
{
	for (int i = 0; i < n; i++)
		if (a[i] != b[i]) return 0;
	return 1;
}

static inline int wsk__is_name(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
	    || (c >= '0' && c <= '9') || c == '_';
}

/*
 * A kind's spelling, returned from code rather than read from a table.
 * `static const char *KIND[] = {...}` is a table of addresses in rodata and
 * does not link into a module; this does. (§3.1, and RV-9's `have` learned
 * it the expensive way.)
 */
static inline const char *wsk_kind_name(int kind)
{
	if (kind == WSK_COL) return "col";
	if (kind == WSK_ROW) return "row";
	if (kind == WSK_LBL) return "lbl";
	if (kind == WSK_BTN) return "btn";
	return "";
}

static inline int wsk_kind_of(const char *s, int n)
{
	if (n != 3) return 0;
	if (wsk__same(s, "col", 3)) return WSK_COL;
	if (wsk__same(s, "row", 3)) return WSK_ROW;
	if (wsk__same(s, "lbl", 3)) return WSK_LBL;
	if (wsk__same(s, "btn", 3)) return WSK_BTN;
	return 0;
}

/*
 * What a label may contain.
 *
 * Not a style rule. RV-9's window draws entities literally -- `&lt;` is four
 * characters on the panel -- so text cannot be escaped, only kept clean; and
 * a raw '<' hung the driver outright until 2026-09-30. Anything outside
 * 32..126 advances a cell and draws nothing. So the boundary is here, where
 * the data arrives, rather than in the emitter where it would be too late to
 * name the line that caused it. See review-2026-09-30.md §3.3.
 */
static inline int wsk_text_ok(char c)
{
	if (c < 32 || c > 126) return 0;
	return c != '<' && c != '&' && c != '>' && c != '"';
}

/* ---- the pool ---------------------------------------------------- */

static inline int wsk__intern(wsk_doc_t *d, const char *s, int n)
{
	int at = d->pool_len;
	if (at + n + 1 > d->pool_cap) return WSK_E_ROOM;
	for (int i = 0; i < n; i++) d->pool[at + i] = s[i];
	d->pool[at + n] = '\0';
	d->pool_len = at + n + 1;
	return at;
}

static inline const char *wsk_str(const wsk_doc_t *d, unsigned short off)
{
	return (off == 0) ? "" : &d->pool[off];
}

/* ---- parse ------------------------------------------------------- */

static inline unsigned int wsk__hex(const char *s, int n, int *ok)
{
	unsigned int v = 0;
	*ok = (n == 6);
	for (int i = 0; i < n && *ok; i++) {
		char c = s[i];
		unsigned int d;
		if (c >= '0' && c <= '9')      d = (unsigned int)(c - '0');
		else if (c >= 'a' && c <= 'f') d = (unsigned int)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F') d = (unsigned int)(c - 'A' + 10);
		else { *ok = 0; break; }
		v = (v << 4) | d;
	}
	return v;
}

static inline int wsk__num(const char *s, int n, int *ok)
{
	int v = 0;
	*ok = (n > 0 && n <= 5);
	for (int i = 0; i < n && *ok; i++) {
		if (s[i] < '0' || s[i] > '9') { *ok = 0; break; }
		v = v * 10 + (s[i] - '0');
	}
	return v;
}

/*
 * One document, into the caller's two arrays.
 *
 * Blank lines and lines opening with '#' are ignored, because a panel a
 * person maintains wants somewhere to say why.
 *
 * `rooted` is what separates a panel from a patch. A panel starts at level 1
 * and may only ever go one deeper at a time; a patch is a fragment cut out of
 * one, so its first line arrives at whatever depth it had where it came from.
 * Keeping the depth is not decoration -- it is how a person reading the patch
 * sees where the line belongs, and it is what would let a patch grow a node
 * rather than only change one.
 */
static inline int wsk__parse(const char *src, int len, wsk_doc_t *d, wsk_err_t *err,
                      int rooted)
{
	int i = 0, line = 0, prev_level = 0, first = 1;

	d->nodes = 0;
	d->pool_len = 1;
	if (d->pool_cap < 1) { if (err) { err->line = 0; err->code = WSK_E_ROOM; } return WSK_E_ROOM; }
	d->pool[0] = '\0';

	while (i < len) {
		int start, end, p, ok;
		wsk_node_t *n;

		line++;
		start = i;
		while (i < len && src[i] != '\n') i++;
		end = i;
		if (i < len) i++;                       /* step over the newline */
		while (end > start && src[end - 1] == '\r') end--;

		p = start;
		while (p < end && src[p] == ' ') p++;    /* a leading space is nothing */
		if (p >= end) continue;                  /* blank */
		if (src[p] == '#') continue;             /* comment */

		if (src[p] != '.') { if (err) { err->line = line; err->code = WSK_E_SYNTAX; } return WSK_E_SYNTAX; }

		int level = 0;
		while (p < end && src[p] == '.') { level++; p++; }
		if (level > WSK_MAX_LEVEL
		    || (level > prev_level + 1 && !(first && !rooted))) {
			if (err) { err->line = line; err->code = WSK_E_LEVEL; }
			return WSK_E_LEVEL;
		}
		first = 0;

		int ks = p;
		while (p < end && wsk__is_name(src[p])) p++;
		int kind = wsk_kind_of(&src[ks], p - ks);
		if (kind == 0) { if (err) { err->line = line; err->code = WSK_E_KIND; } return WSK_E_KIND; }

		if (d->nodes >= d->node_cap) { if (err) { err->line = line; err->code = WSK_E_ROOM; } return WSK_E_ROOM; }
		n = &d->node[d->nodes];
		n->level = (unsigned char)level;
		n->kind = (unsigned char)kind;
		n->name = 0;
		n->text = 0;
		n->pad = WSK_UNSET;
		n->gap = WSK_UNSET;
		n->h = WSK_UNSET;
		n->fill = WSK_NO_FILL;

		/* fields, in any order: a bare word is the name, k=v is an
		   attribute, and a quoted run is the label. */
		while (p < end) {
			while (p < end && src[p] == ' ') p++;
			if (p >= end) break;

			if (src[p] == '"') {
				int ts;
				p++;
				ts = p;
				while (p < end && src[p] != '"') {
					if (!wsk_text_ok(src[p])) {
						if (err) { err->line = line; err->code = WSK_E_TEXT; }
						return WSK_E_TEXT;
					}
					p++;
				}
				if (p >= end) { if (err) { err->line = line; err->code = WSK_E_SYNTAX; } return WSK_E_SYNTAX; }
				int off = wsk__intern(d, &src[ts], p - ts);
				if (off < 0) { if (err) { err->line = line; err->code = WSK_E_ROOM; } return WSK_E_ROOM; }
				n->text = (unsigned short)off;
				p++;
				continue;
			}

			int fs = p;
			while (p < end && src[p] != ' ') p++;
			int fl = p - fs;

			int eq = -1;
			for (int k = 0; k < fl; k++) if (src[fs + k] == '=') { eq = k; break; }

			if (eq < 0) {                        /* the node's name */
				for (int k = 0; k < fl; k++)
					if (!wsk__is_name(src[fs + k])) {
						if (err) { err->line = line; err->code = WSK_E_SYNTAX; }
						return WSK_E_SYNTAX;
					}
				int off = wsk__intern(d, &src[fs], fl);
				if (off < 0) { if (err) { err->line = line; err->code = WSK_E_ROOM; } return WSK_E_ROOM; }
				n->name = (unsigned short)off;
				continue;
			}

			const char *key = &src[fs];
			const char *val = &src[fs + eq + 1];
			int kl = eq, vl = fl - eq - 1;

			if (kl == 3 && wsk__same(key, "pad", 3))      n->pad = (short)wsk__num(val, vl, &ok);
			else if (kl == 3 && wsk__same(key, "gap", 3)) n->gap = (short)wsk__num(val, vl, &ok);
			else if (kl == 1 && key[0] == 'h')            n->h = (short)wsk__num(val, vl, &ok);
			else if (kl == 4 && wsk__same(key, "fill", 4)) {
				if (vl < 1 || val[0] != '#') ok = 0;
				else n->fill = wsk__hex(val + 1, vl - 1, &ok);
			}
			else ok = 0;

			if (!ok) { if (err) { err->line = line; err->code = WSK_E_SYNTAX; } return WSK_E_SYNTAX; }
		}

		/* Names are the address space (D4), so two nodes may not share one. */
		if (n->name != 0)
			for (int k = 0; k < d->nodes; k++)
				if (d->node[k].name != 0
				    && wsk__same(wsk_str(d, d->node[k].name), wsk_str(d, n->name),
				                 wsk__len(wsk_str(d, n->name)) + 1)) {
					if (err) { err->line = line; err->code = WSK_E_DUP; }
					return WSK_E_DUP;
				}

		prev_level = level;
		d->nodes++;
	}

	if (err) { err->line = 0; err->code = WSK_OK; }
	return d->nodes;
}

/* A whole panel: the first line is the root and sits at level 1. */
static inline int wsk_parse(const char *src, int len, wsk_doc_t *d, wsk_err_t *err)
{
	return wsk__parse(src, len, d, err, 1);
}

/* A fragment: an update, keeping the depth it had in the panel. */
static inline int wsk_parse_patch(const char *src, int len, wsk_doc_t *d, wsk_err_t *err)
{
	return wsk__parse(src, len, d, err, 0);
}

/* ---- emit -------------------------------------------------------- */

typedef struct {
	char *out;
	int   cap;
	int   len;                   /* what it would take, whether or not it fits */
} wsk_sink_t;

static inline void wsk__put(wsk_sink_t *s, char c)
{
	if (s->len < s->cap) s->out[s->len] = c;
	s->len++;
}

static inline void wsk__puts(wsk_sink_t *s, const char *t)
{
	while (*t != '\0') wsk__put(s, *t++);
}

static inline void wsk__putn(wsk_sink_t *s, int v)
{
	char b[8];
	int i = 0;
	if (v < 0) { wsk__put(s, '-'); v = -v; }
	do { b[i++] = (char)('0' + (v % 10)); v /= 10; } while (v != 0 && i < 8);
	while (i > 0) wsk__put(s, b[--i]);
}

static inline void wsk__puthex6(wsk_sink_t *s, unsigned int v)
{
	for (int shift = 20; shift >= 0; shift -= 4) {
		unsigned int d = (v >> shift) & 0xF;
		wsk__put(s, (char)(d < 10 ? '0' + d : 'a' + (d - 10)));
	}
}

/*
 * The canonical spelling: one per tree, so emit(parse(x)) == x for anything
 * this wrote, and a golden can be compared byte for byte.
 */
static inline int wsk_emit(const wsk_doc_t *d, char *out, int cap)
{
	wsk_sink_t s;
	s.out = out;
	s.cap = cap;
	s.len = 0;

	for (int i = 0; i < d->nodes; i++) {
		const wsk_node_t *n = &d->node[i];

		for (int k = 0; k < n->level; k++) wsk__put(&s, '.');
		wsk__puts(&s, wsk_kind_name(n->kind));

		if (n->name != 0) { wsk__put(&s, ' '); wsk__puts(&s, wsk_str(d, n->name)); }
		if (n->pad != WSK_UNSET) { wsk__puts(&s, " pad="); wsk__putn(&s, n->pad); }
		if (n->gap != WSK_UNSET) { wsk__puts(&s, " gap="); wsk__putn(&s, n->gap); }
		if (n->h != WSK_UNSET)   { wsk__puts(&s, " h="); wsk__putn(&s, n->h); }
		if (n->fill != WSK_NO_FILL) { wsk__puts(&s, " fill=#"); wsk__puthex6(&s, n->fill); }
		if (n->text != 0) {
			wsk__puts(&s, " \"");
			wsk__puts(&s, wsk_str(d, n->text));
			wsk__put(&s, '"');
		}
		wsk__put(&s, '\n');
	}

	if (s.len < s.cap) out[s.len] = '\0';
	return s.len;
}

/* ---- paths ------------------------------------------------------- */

/*
 * Where a node sits, by name: `col.ok`.
 *
 * Names are document-unique (D4), so the bare name is already an address and
 * a path is never needed to resolve one. It is here because a person reading
 * a diff wants to know what changed *inside*, and because a patch may give
 * either form -- the long one being the way to be sure, if names ever stop
 * being unique.
 */
static inline int wsk_path(const wsk_doc_t *d, int idx, char *out, int cap)
{
	wsk_sink_t s;
	int want;

	s.out = out;
	s.cap = cap;
	s.len = 0;
	if (idx < 0 || idx >= d->nodes) return 0;

	want = d->node[idx].level;
	for (int lv = 1; lv <= want; lv++) {
		int at = -1;
		for (int i = idx; i >= 0; i--)
			if (d->node[i].level == lv) { at = i; break; }
		if (at < 0) continue;
		if (s.len > 0) wsk__put(&s, '.');
		if (d->node[at].name != 0) wsk__puts(&s, wsk_str(d, d->node[at].name));
		else wsk__puts(&s, wsk_kind_name(d->node[at].kind));
	}
	if (s.len < s.cap) out[s.len] = '\0';
	return s.len;
}

/* ---- merge ------------------------------------------------------- */

static inline int wsk_find(const wsk_doc_t *d, const char *name)
{
	int n = wsk__len(name);
	for (int i = 0; i < d->nodes; i++)
		if (d->node[i].name != 0
		    && wsk__len(wsk_str(d, d->node[i].name)) == n
		    && wsk__same(wsk_str(d, d->node[i].name), name, n))
			return i;
	return -1;
}

/*
 * Apply an update, and say what moved.
 *
 * A patch is an ordinary document whose lines name nodes that already exist.
 * Whatever a line states replaces what was there; whatever it leaves out is
 * left alone. The indices of the nodes that actually changed come back in
 * `changed`, in document order -- which is the list a clipped repaint wants,
 * because those ids are what rows() turns into a band range.
 *
 * A patch line that changes nothing is not reported, so "nothing happened"
 * costs no repaint at all rather than a repaint of the same pixels.
 */
static inline int wsk_merge(wsk_doc_t *base, const wsk_doc_t *patch,
                     int *changed, int max_changed, wsk_err_t *err)
{
	int n_changed = 0;

	for (int i = 0; i < patch->nodes; i++) {
		const wsk_node_t *p = &patch->node[i];
		int at, moved = 0;
		wsk_node_t *b;

		if (p->name == 0) { if (err) { err->line = i + 1; err->code = WSK_E_SYNTAX; } return WSK_E_SYNTAX; }

		at = wsk_find(base, wsk_str(patch, p->name));
		if (at < 0) { if (err) { err->line = i + 1; err->code = WSK_E_NOTFOUND; } return WSK_E_NOTFOUND; }
		b = &base->node[at];

		if (p->pad != WSK_UNSET && p->pad != b->pad) { b->pad = p->pad; moved = 1; }
		if (p->gap != WSK_UNSET && p->gap != b->gap) { b->gap = p->gap; moved = 1; }
		if (p->h != WSK_UNSET && p->h != b->h)       { b->h = p->h; moved = 1; }
		if (p->fill != WSK_NO_FILL && p->fill != b->fill) { b->fill = p->fill; moved = 1; }

		if (p->text != 0) {
			const char *was = wsk_str(base, b->text);
			const char *now = wsk_str(patch, p->text);
			int nl = wsk__len(now);
			if (wsk__len(was) != nl || !wsk__same(was, now, nl)) {
				int off = wsk__intern(base, now, nl);
				if (off < 0) { if (err) { err->line = i + 1; err->code = WSK_E_ROOM; } return WSK_E_ROOM; }
				b->text = (unsigned short)off;
				moved = 1;
			}
		}

		if (moved) {
			int k;
			if (n_changed >= max_changed) { if (err) { err->line = i + 1; err->code = WSK_E_ROOM; } return WSK_E_ROOM; }
			/* Kept in document order, which is the order rows() wants: a
			   caller computing one band span over several widgets needs the
			   first and the last, not the order the patch happened to be
			   written in. */
			k = n_changed;
			while (k > 0 && changed[k - 1] > at) { changed[k] = changed[k - 1]; k--; }
			changed[k] = at;
			n_changed++;
		}
	}

	if (err) { err->line = 0; err->code = WSK_OK; }
	return n_changed;
}

#endif /* WSK_NOTE_H */
