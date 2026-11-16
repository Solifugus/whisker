/*
 * Rows y0..y1 of a window, drawn in documents the window will take.
 *
 * RV-9's window -- /w0 on a board, the same driver in the SDL window --
 * drops a document over 4 KB, rightly and with a warning (design §2.3). A
 * program whose picture can grow past that (a code editor scrolled to its
 * busiest lines; a floor zoomed out) tries the whole range as one document
 * first, and, refused (WSK_E_BUDGET, before a byte is written), draws it as
 * halves on 8-row bands -- each a complete document of what crosses its
 * rows, which is what a clipped repaint always is.
 */
#ifndef WHISKER_FIT_H
#define WHISKER_FIT_H

#include "host.h"
#include "whisker.h"

static void wh_draw_fit(const wsk_tree_t *t, wsk_sink_t *s, int y0, int y1)
{
	int mid;
	if (y1 <= y0) { y0 = 0; y1 = t->h; }
	mid = ((y0 + y1) / 2) & ~7;
	wh_clip(y0, y1);
	s->len = 0;
	if (wsk_render_rows(t, y0, y1, s, 4096) != WSK_E_BUDGET) return;
	if (mid <= y0 || mid >= y1) {
		/* one band too dense to split: sent anyway -- the window may drop
		   it, and says so, which is better than a silence that leaves
		   old pixels (found by review, 2026-11-16) */
		wh_clip(y0, y1);
		s->len = 0;
		(void)wsk_render_rows(t, y0, y1, s, 0);
		return;
	}
	wh_draw_fit(t, s, y0, mid);
	wh_draw_fit(t, s, mid, y1);
}

#endif
