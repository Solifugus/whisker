/*
 * The host, tested without a person.
 *
 * SDL has a dummy video driver, so the whole contract runs headless: a
 * window that is never shown, RV-9's rasteriser behind it, and events pushed
 * onto the queue instead of pressed with a finger. What that buys is the
 * check that matters here -- that a clipped write touches ONLY the rows it
 * was given, which is the property the whole design rests on and the one a
 * host is most likely to get wrong by helpfully redrawing everything.
 */
#include "host_sdl.c"
#include "whisker.h"

static int fails;

static void ok(int cond, const char *what)
{
	printf("%s  %s\n", cond ? "ok  " : "FAIL", what);
	if (!cond) fails++;
}

/* WH_NOTHING means "not yet", not "never": see host.h. */
static int poll_until(wh_event_t *ev, int tries)
{
	for (int i = 0; i < tries; i++) {
		int k = wh_poll(ev);
		if (k != WH_NOTHING) return k;
		SDL_Delay(5);
	}
	return WH_NOTHING;
}

static const wsk_widget_t PANEL[] = {
	{ .kind = WSK_LABEL,  .id = "title", .text = "whisker" },
	{ .kind = WSK_BUTTON, .id = "a",     .text = "One" },
	{ .kind = WSK_BUTTON, .id = "b",     .text = "Two" },
};

static void to_window(wsk_sink_t *s, const char *b, int n) { (void)s; wh_write(b, n); }

#define W 320
#define H 172
#define SCALE 2

/* §5d's loop needs two stacks, so it needs §5c's composition. */
static wsk_tree_t   PART2[2];
static wsk_doc_t    DOC2;

static int body2(wsk_sink_t *s, void *ctx)
{
	(void)ctx;
	wsk_open(&DOC2, s);
	for (int i = 0; i < 2; i++) {
		int e = wsk_part(&DOC2, &PART2[i], s);
		if (e != WSK_OK) return e;
	}
	wsk_shut(s);
	return WSK_OK;
}

static int compose_now(int y0, int y1)
{
	wsk_sink_t s;
	DOC2.y0 = (short)y0; DOC2.y1 = (short)y1;
	wh_clip((y0 == 0 && y1 == H) ? 0 : y0, (y0 == 0 && y1 == H) ? 0 : y1);
	s.put = to_window; s.ctx = 0; s.len = 0;
	return wsk_compose(body2, 0, &s, 4096);
}

int main(void)
{
	wsk_widget_t live[3];
	wsk_tree_t t;
	wsk_sink_t s;
	wh_event_t ev;
	static uint16_t before[W * H];
	const char *ids[1];
	int y0, y1, painted = 0, outside = 0, inside = 0;

	SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
	setenv("SDL_VIDEODRIVER", "dummy", 1);

	ok(wh_open(W, H, SCALE, "selftest") == 0, "the window opens");

	wsk_clone(live, PANEL, 3);
	wsk_init(&t, WSK_COL, W, H);
	t.pad = 4; t.gap = 4; t.bg = 0x10203au;
	t.item = live; t.n = 3;

	s.put = to_window; s.ctx = 0; s.len = 0;
	ok(wsk_render(&t, &s, 4096) > 0, "the screen renders through the host");

	for (int i = 0; i < W * H; i++) if (S.fb[i] != 0) painted++;
	ok(painted > W * H / 2, "and it reached the framebuffer");

	/* The property the design rests on: a clipped write touches those rows
	   and no others. A host that redrew the window on every write would pass
	   every other check here and quietly cost the whole band budget. */
	memcpy(before, S.fb, sizeof before);
	ids[0] = "a";
	wsk_rows(&t, ids, 1, &y0, &y1);
	live[1].fill = 0x8fd0ffu;
	wh_clip(y0, y1);
	s.put = to_window; s.ctx = 0; s.len = 0;
	wsk_render_clipped(&t, ids, 1, &s, 4096);

	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			if (S.fb[y * W + x] != before[y * W + x]) {
				if (y >= y0 && y < y1) inside++;
				else outside++;
			}
	ok(inside > 0, "the clipped write changed pixels inside its rows");
	ok(outside == 0, "CONTROL: and not one pixel outside them");
	printf("     %d pixels changed, all of them within rows %d..%d\n", inside, y0, y1);

	/* Input: a point in, scaled back from the window to the device. */
	{
		SDL_Event e;
		wsk_rect_t r = wsk_rect(&t, 1);
		memset(&e, 0, sizeof e);
		e.type = SDL_MOUSEBUTTONDOWN;
		e.button.x = (r.x + 10) * SCALE;
		e.button.y = (r.y + 10) * SCALE;
		SDL_PushEvent(&e);
		ok(poll_until(&ev, 20) == WH_PRESS, "a press arrives");
		ok(ev.x == r.x + 10 && ev.y == r.y + 10,
		   "divided back down from the window to device pixels");
		ok(wsk_hit(&t, ev.x, ev.y) == 1, "and whisker names the widget under it");
	}

	/* And the gap, which must resolve to nothing at all. */
	{
		SDL_Event e;
		wsk_rect_t r = wsk_rect(&t, 1);
		memset(&e, 0, sizeof e);
		e.type = SDL_MOUSEBUTTONDOWN;
		e.button.x = (W / 2) * SCALE;
		e.button.y = (r.y + r.h + 1) * SCALE;
		memset(&ev, 0, sizeof ev);
		SDL_PushEvent(&e);
		ok(poll_until(&ev, 20) == WH_PRESS, "a press in the gap arrives too");
		ok(wsk_hit(&t, ev.x, ev.y) == WSK_E_NOTFOUND,
		   "CONTROL: and whisker names nothing under it");
	}

	/*
	 * §5d criterion 4: the loop closes with a real pointing device.
	 *
	 * A click on a key becomes a character in a field, and the repaint that
	 * follows touches the FIELD'S rows and nothing else -- which is the whole
	 * point of a widget layer over a banded window, and the thing a host that
	 * helpfully redrew everything would pass every other check without doing.
	 */
	{
		static wsk_widget_t fld[1], keys[3];
		static uint16_t was[W * H];
		SDL_Event e;
		wsk_rect_t kr;
		const char *who[1];
		int ky0, ky1, i, in2 = 0, out2 = 0, n;

		memset(fld, 0, sizeof fld);
		fld[0].kind = WSK_FIELD;
		wsk__scpy(fld[0].id, "entry", WSK_ID_MAX);
		fld[0].mark = WSK_MARK_CARET;

		memset(keys, 0, sizeof keys);
		for (i = 0; i < 3; i++) {
			keys[i].kind = WSK_BUTTON;
			keys[i].weight = 1;
			keys[i].id[0] = (char)('p' + i); keys[i].id[1] = '\0';
			keys[i].text[0] = (char)('X' + i); keys[i].text[1] = '\0';
		}

		wsk_init(&PART2[0], WSK_COL, W, H);
		PART2[0].pad = 6; PART2[0].gap = 6; PART2[0].ids = 1;
		PART2[0].font = 16; PART2[0].bg = 0x10203au;
		wsk_area(&PART2[0], 0, 24, W, 44);
		PART2[0].item = fld; PART2[0].n = 1;

		wsk_init(&PART2[1], WSK_ROW, W, H);
		PART2[1].pad = 6; PART2[1].gap = 6; PART2[1].ids = 1;
		PART2[1].font = 16; PART2[1].bg = 0x10203au;
		wsk_area(&PART2[1], 0, 104, W, 56);
		PART2[1].item = keys; PART2[1].n = 3;

		wsk_doc_init(&DOC2, &PART2[0]);
		n = compose_now(0, H);
		ok(n > 0, "a field and a keypad draw together");

		kr = wsk_rect(&PART2[1], 1);
		memset(&e, 0, sizeof e);
		e.type = SDL_MOUSEBUTTONDOWN;
		e.button.x = (kr.x + kr.w / 2) * SCALE;
		e.button.y = (kr.y + kr.h / 2) * SCALE;
		memset(&ev, 0, sizeof ev);
		SDL_PushEvent(&e);
		ok(poll_until(&ev, 20) == WH_PRESS, "a click on a key arrives");

		i = wsk_hit(&PART2[1], ev.x, ev.y);
		ok(i == 1, "and whisker names that key, not its neighbour");
		ok(wsk_hit(&PART2[0], ev.x, ev.y) == WSK_E_NOTFOUND,
		   "CONTROL: and the field, which is elsewhere, claims nothing");

		/* The application decides what the name means. whisker has no keymap. */
		{
			int len = wsk__len(fld[0].text);
			fld[0].text[len] = keys[i].text[0];
			fld[0].text[len + 1] = '\0';
		}
		ok(fld[0].text[0] == 'Y', "the application put the key's own label in the field");

		memcpy(was, S.fb, sizeof was);
		who[0] = "entry";
		ok(wsk_doc_rows(&DOC2, &PART2[0], who, 1) == WSK_OK,
		   "the field gives the document its rows");
		ky0 = DOC2.y0; ky1 = DOC2.y1;
		ok(compose_now(ky0, ky1) > 0, "and one document repaints them");

		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++)
				if (S.fb[y * W + x] != was[y * W + x]) {
					if (y >= ky0 && y < ky1) in2++; else out2++;
				}
		ok(in2 > 0, "the typed character changed pixels in the field's rows");
		ok(out2 == 0,
		   "CONTROL: and not one pixel outside them -- the keypad never moved");
		printf("     typing one character: %d pixels, all within rows %d..%d of %d\n",
		       in2, ky0, ky1, H);
	}

	wh_close();
	printf(fails ? "\n%d check(s) failed\n" : "\nall host checks passed\n", fails);
	return fails != 0;
}
