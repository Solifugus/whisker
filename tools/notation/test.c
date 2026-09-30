/*
 * The notation, tested. Host-only: the header itself uses no libc.
 *
 *   ./run.sh          every check, then the byte comparison
 */
#include "wsk_note.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void ok(int cond, const char *what)
{
	printf("%s  %s\n", cond ? "ok  " : "FAIL", what);
	if (!cond) fails++;
}

static int slurp(const char *path, char *buf, int cap)
{
	FILE *f = fopen(path, "rb");
	int n;
	if (f == NULL) return -1;
	n = (int)fread(buf, 1, (size_t)cap, f);
	fclose(f);
	return n;
}

/* Storage is the caller's, always. */
static wsk_node_t nodes[64], pnodes[16];
static char pool[2048], ppool[512];

static void doc_init(wsk_doc_t *d, wsk_node_t *n, int nc, char *p, int pc)
{
	d->node = n; d->node_cap = nc; d->nodes = 0;
	d->pool = p; d->pool_cap = pc; d->pool_len = 0;
}

int main(void)
{
	static char src[8192], out[8192], patch[2048];
	wsk_doc_t d, p;
	wsk_err_t err;
	int n, len;

	/* ---- 1. it parses, and it round-trips byte for byte ---- */
	n = slurp("panel.wsk", src, sizeof src);
	ok(n > 0, "panel.wsk was read");

	doc_init(&d, nodes, 64, pool, sizeof pool);
	n = wsk_parse(src, n, &d, &err);
	if (n < 0) printf("     parse failed at line %d, code %d\n", err.line, err.code);
	ok(n == 13, "13 nodes parsed (a column and twelve controls)");
	ok(d.node[0].kind == WSK_COL && d.node[0].level == 1, "the column is at level 1");
	ok(d.node[1].level == 2 && d.node[1].kind == WSK_LBL, "the label is inside it");
	ok(d.node[0].pad == 4 && d.node[0].gap == 4, "pad and gap were read");
	ok(d.node[0].fill == 0x10203au, "fill was read as 24-bit colour");
	ok(strcmp(wsk_str(&d, d.node[4].text), "Valve B") == 0, "a label with a space survives");

	len = wsk_emit(&d, out, sizeof out);
	{
		/* The golden is the file with its comments and blank lines gone,
		   which is what canonical means here. */
		char want[8192];
		int wl = 0;
		for (int i = 0; i < (int)sizeof src && src[i]; ) {
			int s = i, e;
			while (src[i] && src[i] != '\n') i++;
			e = i;
			if (src[i]) i++;
			if (e > s && src[s] != '#')
				{ for (int k = s; k < e; k++) want[wl++] = src[k]; want[wl++] = '\n'; }
		}
		want[wl] = '\0';
		ok(len == wl && memcmp(out, want, (size_t)wl) == 0,
		   "emit(parse(x)) is x, byte for byte");
		if (len != wl) printf("     emitted %d bytes, wanted %d\n", len, wl);
	}

	/* ---- 2. what it refuses, each with the line that caused it ---- */
	{
		struct { const char *src; int code; const char *what; } bad[] = {
			{ ".col\n....btn a \"A\"\n", WSK_E_LEVEL,  "a skipped level" },
			{ ".col\n..box a \"A\"\n",  WSK_E_KIND,   "a kind that is not a widget" },
			{ ".col\n..btn a \"a < b\"\n", WSK_E_TEXT, "a '<' in a label (it hung /w0 until 2026-09-30)" },
			{ ".col\n..btn a \"caf\xc3\xa9\"\n", WSK_E_TEXT, "a non-ASCII byte (it draws blank)" },
			{ ".col\n..btn a \"A\"\n..btn a \"B\"\n", WSK_E_DUP, "two nodes with one name" },
			{ "col a\n", WSK_E_SYNTAX, "a line with no dots" },
			{ ".col\n..btn a h=x\n", WSK_E_SYNTAX, "an attribute that is not a number" },
			{ ".col\n..btn a \"unclosed\n", WSK_E_SYNTAX, "an unclosed label" },
		};
		for (int i = 0; i < (int)(sizeof bad / sizeof bad[0]); i++) {
			wsk_doc_t t;
			doc_init(&t, nodes, 64, pool, sizeof pool);
			int r = wsk_parse(bad[i].src, (int)strlen(bad[i].src), &t, &err);
			char msg[128];
			snprintf(msg, sizeof msg, "refused: %s (line %d)", bad[i].what, err.line);
			ok(r == bad[i].code && err.line > 0, msg);
		}
	}

	/* The control for all of that: the same shapes, legal, are accepted --
	   otherwise "it refuses bad input" is satisfied by refusing everything. */
	{
		wsk_doc_t t;
		doc_init(&t, nodes, 64, pool, sizeof pool);
		const char *legal = ".col\n..btn a \"a is b\"\n...lbl b \"x\"\n";
		ok(wsk_parse(legal, (int)strlen(legal), &t, &err) == 3,
		   "control: the same shapes, legal, are accepted");
	}

	/* ---- 3. a patch, and the ids it moved ---- */
	doc_init(&d, nodes, 64, pool, sizeof pool);
	n = slurp("panel.wsk", src, sizeof src);
	wsk_parse(src, n, &d, &err);

	n = slurp("patch.wsk", patch, sizeof patch);
	doc_init(&p, pnodes, 16, ppool, sizeof ppool);
	n = wsk_parse_patch(patch, n, &p, &err);
	ok(n == 3, "the patch is three lines");

	{
		int changed[16];
		int nc = wsk_merge(&d, &p, changed, 16, &err);
		ok(nc == 2, "two nodes actually changed, though three were named");
		printf("     changed:");
		for (int i = 0; i < nc; i++) {
			char path[64];
			wsk_path(&d, changed[i], path, sizeof path);
			printf("  %s (%s)", wsk_str(&d, d.node[changed[i]].name), path);
		}
		printf("\n     unchanged: fan, because the patch restated what was already true\n");

		ok(strcmp(wsk_str(&d, d.node[changed[0]].name), "title") == 0
		   && strcmp(wsk_str(&d, d.node[changed[1]].name), "heater") == 0,
		   "they come back in document order, which is the order rows() wants");
		ok(d.node[5].fill == 0x8fd0ffu, "heater took the new fill");
		ok(strcmp(wsk_str(&d, d.node[1].text), "Settings *") == 0, "title took the new text");
	}

	/* A patch naming something that is not there is refused rather than
	   quietly adding it: a typo in an id would otherwise become a widget. */
	{
		wsk_doc_t q;
		int changed[4];
		doc_init(&q, pnodes, 16, ppool, sizeof ppool);
		const char *absent = "..btn nosuch \"X\"\n";
		wsk_parse_patch(absent, (int)strlen(absent), &q, &err);
		ok(wsk_merge(&d, &q, changed, 4, &err) == WSK_E_NOTFOUND,
		   "a patch naming an unknown node is refused");
	}

	/* ---- 4. what it costs ---- */
	{
		int note = wsk_emit(&d, out, sizeof out);
		int svg_per_control = 109;    /* measured, review-2026-09-30.md §4 */
		int controls = 12;
		int svg = 1439;               /* the same panel, <g>-hoisted */
		char pout[512];
		int pn = wsk_emit(&p, pout, sizeof pout);
		printf("\n  the same twelve controls, written down:\n");
		printf("    SVG, <g>-hoisted          %5d bytes   %3d per control\n",
		       svg, svg_per_control);
		printf("    this notation             %5d bytes   %3d per control   %.1fx smaller\n",
		       note, note / controls, (double)svg / note);
		printf("\n  and an update to two of them:\n");
		printf("    the patch, canonical      %5d bytes   %d lines\n", pn, p.nodes);
		printf("    the panel it updates      %5d bytes   so an update is %d%% of it\n",
		       note, 100 * pn / note);
		printf("    what whisker must emit    two clipped documents, or one\n");
		printf("                              spanning both -- which is the choice\n");
		printf("                              rows(tree, ids, n) exists to make\n");
	}

	printf(fails ? "\n%d check(s) failed\n" : "\nall checks passed\n", fails);
	return fails != 0;
}
