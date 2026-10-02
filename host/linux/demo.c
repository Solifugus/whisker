/*
 * whisker, running.
 *
 *     ./run.sh                 320x172, the C5's panel, at 3x
 *     ./run.sh 1024 600 1      the P4's, at actual size
 *
 * The whole loop, and there is not much of it: a point comes in, wsk_hit
 * turns it into a name, wsk_rows turns the name into a band range, and
 * wsk_render_clipped writes one document covering those rows. Press and
 * release are the same three calls with a different fill. Nothing here holds
 * a widget, an event queue or a state machine -- the host has the pixels, the
 * application has its state, and whisker has neither.
 */
#include "host.h"
#include "whisker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const wsk_widget_t PANEL[] = {
	{ .kind = WSK_LABEL,  .id = "title",  .text = "whisker" },
	{ .kind = WSK_BUTTON, .id = "pump",   .text = "Pump" },
	{ .kind = WSK_BUTTON, .id = "valve",  .text = "Valve" },
	{ .kind = WSK_BUTTON, .id = "heater", .text = "Heater" },
	{ .kind = WSK_BUTTON, .id = "stop",   .text = "STOP", .fill = 0x8f2a2au },
};

static void to_window(wsk_sink_t *s, const char *b, int n)
{
	(void)s;
	wh_write(b, n);
}

/* One widget, repainted: the clip and the document come from the same rows. */
static void repaint(const wsk_tree_t *t, const char *id)
{
	const char *ids[1];
	wsk_sink_t s;
	int y0, y1;

	ids[0] = id;
	if (wsk_rows(t, ids, 1, &y0, &y1) != WSK_OK) return;
	wh_clip(y0, y1);                       /* one-shot, every time */
	s.put = to_window; s.ctx = 0; s.len = 0;
	wsk_render_clipped(t, ids, 1, &s, 4096);
}

int main(int argc, char **argv)
{
	int w = (argc > 1) ? atoi(argv[1]) : 320;
	int h = (argc > 2) ? atoi(argv[2]) : 172;
	int scale = (argc > 3) ? atoi(argv[3]) : 3;
	wsk_widget_t live[5];
	wsk_tree_t t;
	wsk_sink_t s;
	wh_event_t ev;
	int held = -1;

	if (wh_open(w, h, scale, "whisker") != 0) return 1;

	wsk_clone(live, PANEL, 5);
	wsk_init(&t, WSK_COL, w, h);
	t.pad = 4;
	t.gap = 4;
	t.bg = 0x10203au;
	t.cross_max = 480;                     /* a button is a finger, not a bar */
	t.item = live;
	t.n = 5;

	s.put = to_window; s.ctx = 0; s.len = 0;
	if (wsk_render(&t, &s, 4096) < 0) {
		fprintf(stderr, "demo: the panel does not fit in 4096 bytes\n");
		wh_close();
		return 1;
	}
	printf("whisker: %dx%d, %d bytes for the screen. Click a button; q to quit.\n",
	       w, h, s.len);

	for (;;) {
		int kind = wh_poll(&ev);

		if (kind == WH_QUIT) break;

		if (kind == WH_PRESS) {
			int i = wsk_hit(&t, ev.x, ev.y);
			if (i < 0) {
				/* The gap. No name, so no rows, so nothing is written --
				   and nothing is the right amount to write. */
				printf("  (%3d,%3d) nothing\n", ev.x, ev.y);
			} else if (live[i].kind == WSK_BUTTON) {
				held = i;
				live[i].fill = 0x8fd0ffu;
				live[i].ink = 0x112233u;
				live[i].mark = WSK_MARK_CORNERS;
				repaint(&t, live[i].id);
				printf("  (%3d,%3d) %s\n", ev.x, ev.y, live[i].id);
			} else {
				printf("  (%3d,%3d) %s (a label, so the application ignores it)\n",
				       ev.x, ev.y, live[i].id);
			}
		}

		if (kind == WH_RELEASE && held >= 0) {
			live[held].fill = (held == 4) ? 0x8f2a2au : WSK_INHERIT;
			live[held].ink = WSK_INHERIT;
			live[held].mark = 0;
			repaint(&t, live[held].id);
			held = -1;
		}

		if (kind == WH_NOTHING) wh_wait(20);
	}

	wh_close();
	return 0;
}
