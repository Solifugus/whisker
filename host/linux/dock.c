/*
 * A window with docked panes (design.md §5ag).
 *
 *     ./run.sh --dock                  the SDL window, RV-9's rasteriser
 *     host/web/run.sh --dock           the same program in a browser
 *
 * Studio's shape: an editor in the centre, Files and Outline on the left,
 * Output at the bottom. Drag a pane by its caption to another edge -- a bar
 * shows where it would go, and dropped in the middle it stays where it was.
 * Its x closes it; Ctrl-1, Ctrl-2, Ctrl-3 show or hide each. The dividers
 * resize the areas. Ctrl-S keeps the layout (one line of text, printed and
 * written to Output); Ctrl-L puts the kept one back.
 *
 * The frame is whisker_dock.h's, built in tree slots this program sets
 * aside; the panes' contents and the centre are this program's own trees.
 * Nothing about the layout lives anywhere but in `dock` and the line.
 */
#include "host.h"
#include "whisker.h"
#include "whisker_dock.h"
#include "fit.h"

#include <stdio.h>
#include <string.h>

#define W 1024
#define H 600

enum { T_CENTRE, T_FILES, T_OUTLINE, T_OUTPUT, T_DOCK };

static const wsk_widget_t CENTRE[] = {
	{ .kind = WSK_MEMO, .id = "editor", .sub = 0, .weight = 1 },
};
static const wsk_widget_t FILES[] = {
	{ .kind = WSK_LABEL, .id = "f0", .text = "main.bas" },
	{ .kind = WSK_LABEL, .id = "f1", .text = "draw.bas" },
	{ .kind = WSK_LABEL, .id = "f2", .text = "menus.bas" },
	{ .kind = WSK_LABEL, .id = "f3", .text = "README.md" },
};
static const wsk_widget_t OUTLINE[] = {
	{ .kind = WSK_LABEL, .id = "o0", .text = "program demo" },
	{ .kind = WSK_LABEL, .id = "o1", .text = "function draw" },
	{ .kind = WSK_LABEL, .id = "o2", .text = "function menu" },
};
static const wsk_widget_t OUTPUT[] = {
	{ .kind = WSK_MEMO, .id = "output", .sub = 1, .weight = 1 },
};

static char text[8192] =
	"program demo\n"
	"\n"
	"' drag a pane by its caption to another edge of the window;\n"
	"' its x closes it, Ctrl-1 2 3 bring them back.\n"
	"' Ctrl-S keeps the layout, Ctrl-L puts it back.\n"
	"\n"
	"function draw()\n"
	"    print \"hello\"\n"
	"end function\n";
static char out[4096];
static char kept[256];
static wsk_buf_t buf[2];
static wsk_tree_t root, trees[T_DOCK + WSK_DOCK_TREES];
static const char *strs[WSK_DOCK_MAX];
static wsk_dock_t dock;
static wsk_ui_t ui;
static int said;                        /* Output changed: draw it all again */

static void to_window(wsk_sink_t *s, const char *b, int n)
{
	(void)s;
	wh_write(b, n);
}

/* A line in Output and on stdout; the oldest lines go when it fills. */
static void say(const char *line)
{
	int n = (int)strlen(line), len = buf[1].len;
	printf("%s\n", line);
	while (len > 0 && len + n + 2 > (int)sizeof out) {
		char *nl = memchr(out, '\n', (size_t)len);
		int cut = nl ? (int)(nl - out) + 1 : len;
		memmove(out, out + cut, (size_t)(len - cut));
		len -= cut;
	}
	if (len > 0) out[len++] = '\n';
	memcpy(out + len, line, (size_t)n);
	len += n;
	out[len] = '\0';
	buf[1].len = len;
	buf[1].caret = len;
	buf[1].top = 0;
	said = 1;
}

/* the edge names, by WSK_DOCK_*: a function, not a table of pointers */
static const char *area_name(int a)
{
	return a == WSK_DOCK_LEFT ? "the left" : a == WSK_DOCK_RIGHT ? "the right"
	     : a == WSK_DOCK_TOP ? "the top" : "the bottom";
}

int main(void)
{
	wsk_sink_t s;
	int y0, y1, e, which = -1;

	memset(buf, 0, sizeof buf);
	buf[0].b = text; buf[0].cap = sizeof text; buf[0].len = (int)strlen(text);
	buf[1].b = out; buf[1].cap = sizeof out;

	wsk_init(&root, WSK_COL, W, H);
	root.font = 18;
	root.talign = WSK_ALIGN_START;
	root.nest = trees; root.nests = T_DOCK + WSK_DOCK_TREES;
	root.str = strs; root.strs = WSK_DOCK_MAX;
	root.buf = buf; root.bufs = 2;
	root.caps = (unsigned char)wh_caps();
	for (int i = 0; i < T_DOCK; i++) {
		wsk_nest_init(&trees[i], &root, WSK_COL);
		trees[i].pad = 4;
		trees[i].talign = WSK_ALIGN_START;
		trees[i].overflow = WSK_SCROLL;       /* a pane may be shorter than what is in it */
	}
	trees[T_CENTRE].item = CENTRE; trees[T_CENTRE].n = 1;
	trees[T_CENTRE].overflow = 0;
	trees[T_FILES].item = FILES; trees[T_FILES].n = 4; trees[T_FILES].select = 1;
	trees[T_OUTLINE].item = OUTLINE; trees[T_OUTLINE].n = 3; trees[T_OUTLINE].select = 1;
	trees[T_OUTPUT].item = OUTPUT; trees[T_OUTPUT].n = 1;
	trees[T_OUTPUT].overflow = 0;

	if (wsk_dock_init(&dock, &root, strs, T_DOCK, 0, T_CENTRE) != WSK_OK) return 1;
	wsk_dock_pane(&dock, "files", "Files", T_FILES, WSK_DOCK_LEFT);
	wsk_dock_pane(&dock, "outline", "Outline", T_OUTLINE, WSK_DOCK_LEFT);
	wsk_dock_pane(&dock, "output", "Output", T_OUTPUT, WSK_DOCK_BOTTOM);
	wsk_dock_build(&dock);
	say("drag a caption to another edge");
	if ((e = wsk_check(&root, &which)) != WSK_OK) {
		fprintf(stderr, "dock: the frame was refused (%d at %d)\n", e, which);
		return 1;
	}

	if (wh_open(W, H, 1, "whisker -- docking") != 0) return 1;
	s.put = to_window; s.ctx = 0; s.len = 0;
	wsk_ui_init(&ui, &root);
	wsk_ui_focus(&ui, "editor", &y0, &y1);
	wh_draw_fit(&root, &s, 0, H);

	for (;;) {
		wh_event_t he;
		wsk_event_t ev;
		wsk_note_t note[8];
		int k = wh_poll(&he), n, whole = 0, dragged, was_area = -1, was_order = -1, dropped = 0;

		if (k == WH_QUIT) break;
		if (k == WH_NOTHING) { fflush(stdout); wh_wait(20); continue; }
		if (k == WH_EXPOSE) { wh_draw_fit(&root, &s, 0, H); continue; }
		memset(&ev, 0, sizeof ev);
		ev.x = (short)he.x; ev.y = (short)he.y; ev.finger = (unsigned char)he.finger;
		if (k == WH_PRESS) { ev.kind = WSK_EV_DOWN; ev.key = (unsigned short)he.key; ev.d = (short)he.d; }
		else if (k == WH_RELEASE) ev.kind = WSK_EV_UP;
		else if (k == WH_MOVE) ev.kind = WSK_EV_MOVE;
		else if (k == WH_WHEEL) { ev.kind = WSK_EV_WHEEL; ev.d = (short)he.d; }
		else if (k == WH_KEY) { ev.kind = WSK_EV_KEY; ev.key = (unsigned short)he.key; }
		else if (k == WH_TEXT) { ev.kind = WSK_EV_TEXT; memcpy(ev.text, he.text, sizeof ev.text); }
		else continue;

		n = wsk_feed(&ui, &ev, note, 8, &y0, &y1);
		said = 0;
		dragged = dock.drag;
		if (dragged >= 0) { was_area = dock.pane[dragged].area; was_order = dock.pane[dragged].order; }
		for (int i = 0; i < n; i++) {
			const wsk_note_t *t = &note[i];
			char m[300];
			if (t->kind == WSK_NOTE_DROP) dropped = 1;
			if ((t->kind == WSK_NOTE_CLICK || t->kind == WSK_NOTE_SELECT) && t->in == &trees[T_FILES] && t->item >= 0) {
				snprintf(m, sizeof m, "chose %s", FILES[t->item].text);
				say(m);
			} else if (t->kind == WSK_NOTE_KEY && (t->value == ('s' | WSK_MOD_CTRL))) {
				if (wsk_dock_save(&dock, kept, sizeof kept) > 0) {
					snprintf(m, sizeof m, "layout kept: %s", kept);
					say(m);
					whole = 1;
				}
			} else if (t->kind == WSK_NOTE_KEY && (t->value == ('l' | WSK_MOD_CTRL))) {
				if (kept[0] != '\0' && wsk_dock_load(&dock, kept) >= 0) {
					wsk_dock_build(&dock);
					say("layout put back");
				} else say("no layout kept yet: Ctrl-S keeps one");
				whole = 1;
			} else if (t->kind == WSK_NOTE_KEY && t->value >= ('1' | WSK_MOD_CTRL) && t->value <= ('3' | WSK_MOD_CTRL)) {
				const char *nm = dock.pane[(t->value & 0xff) - '1'].name;
				wsk_dock_show(&dock, nm, !wsk_dock_shown(&dock, nm));
				wsk_dock_build(&dock);
				snprintf(m, sizeof m, "%s %s", dock.pane[(t->value & 0xff) - '1'].caption,
				         wsk_dock_shown(&dock, nm) ? "shown" : "hidden");
				say(m);
				whole = 1;
			} else if (t->kind == WSK_NOTE_CLICK && t->item == 1) {
				for (int p = 0; p < dock.n; p++)
					if (t->in == &trees[T_DOCK + 5 + WSK_DOCK_MAX + p]) {
						snprintf(m, sizeof m, "%s closed: Ctrl-%d brings it back", dock.pane[p].caption, p + 1);
						say(m);
					}
			}
		}
		if (wsk_dock_feed(&dock, &ui, &ev, note, n)) whole = 1;
		if (dropped && dragged >= 0) {
			char m[120];
			const wsk_pane_t *p = &dock.pane[dragged];
			if (p->area != was_area || p->order != was_order)
				snprintf(m, sizeof m, "%s docked at %s", p->caption, area_name(p->area));
			else snprintf(m, sizeof m, "%s let go in the middle: left where it was", p->caption);
			say(m);
		}
		if (said) whole = 1;
		if (whole) { wh_draw_fit(&root, &s, 0, H); continue; }
		if (y1 > y0) wh_draw_fit(&root, &s, y0, y1);
	}
	if (wsk_dock_save(&dock, kept, sizeof kept) > 0) printf("layout at the end: %s\n", kept);
	wh_close();
	return 0;
}
