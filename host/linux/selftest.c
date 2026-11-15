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
/* The next event, hover aside: the window says where the pointer rests as
   it opens and as it is left (§5aa), which no check below is about. */
static int poll_until(wh_event_t *ev, int tries)
{
	for (int i = 0; i < tries; i++) {
		int k = wh_poll(ev);
		if (k != WH_NOTHING && k != WH_HOVER) return k;
		if (k == WH_NOTHING) SDL_Delay(5);
	}
	return WH_NOTHING;
}

/* The next hover, everything else aside. */
static int poll_hover(wh_event_t *ev, int tries)
{
	for (int i = 0; i < tries; i++) {
		int k = wh_poll(ev);
		if (k == WH_HOVER) return k;
		if (k == WH_NOTHING) SDL_Delay(5);
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
		e.button.button = SDL_BUTTON_LEFT;      /* as a real click says */
		e.button.x = (r.x + 10) * SCALE;
		e.button.y = (r.y + 10) * SCALE;
		SDL_PushEvent(&e);
		ok(poll_until(&ev, 20) == WH_PRESS, "a press arrives");
		ok(ev.x == r.x + 10 && ev.y == r.y + 10,
		   "divided back down from the window to device pixels");
		ok(wsk_hit(&t, ev.x, ev.y) == 1, "and whisker names the widget under it");
		ok(ev.d == 0, "CONTROL: a press SDL counted as nothing carries a count of 0, a single");
		e.button.clicks = 2;                    /* the desktop's double-click */
		SDL_PushEvent(&e);
		ok(poll_until(&ev, 20) == WH_PRESS && ev.d == 2,
		   "a press SDL counted as a second click carries d = 2 (§5w)");
		{
			/* a finger from a device that is not a touchscreen -- what a
			   touchpad looked like, pressing at a fraction of the PAD */
			SDL_Event f;
			memset(&f, 0, sizeof f);
			f.type = SDL_FINGERDOWN;
			f.tfinger.touchId = 4242;
			f.tfinger.x = 0.9f; f.tfinger.y = 0.9f;
			SDL_PushEvent(&f);
			ok(poll_until(&ev, 20) == WH_NOTHING,
			   "a finger from a device that is not a touchscreen is not a press (a touchpad's taps are mouse clicks)");
			SDL_PushEvent(&e);
			ok(poll_until(&ev, 20) == WH_PRESS, "CONTROL: a mouse press still is");
		}
	}

	/* And the gap, which must resolve to nothing at all. */
	{
		SDL_Event e;
		wsk_rect_t r = wsk_rect(&t, 1);
		memset(&e, 0, sizeof e);
		e.type = SDL_MOUSEBUTTONDOWN;
		e.button.button = SDL_BUTTON_LEFT;      /* as a real click says */
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
		e.button.button = SDL_BUTTON_LEFT;      /* as a real click says */
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

	/* §5j: a real keyboard through the host and the toolkit. Text and keys
	   are pushed as SDL events -- what a person's keyboard produces -- and
	   come out of wh_poll as WH_TEXT and WH_KEY, which go to wsk_feed. */
	{
		static const wsk_widget_t F[] = {
			{ .kind = WSK_FIELD, .id = "a", .sub = 0, .size = 40 },
			{ .kind = WSK_FIELD, .id = "b", .sub = 1, .size = 40 },
		};
		char ab[16] = "", bb[16] = "";
		wsk_buf_t buf[2];
		wsk_tree_t t2;
		wsk_ui_t ui;
		wsk_note_t nt[2];
		wsk_event_t we;
		wh_event_t ev;
		SDL_Event e;
		int y0, y1, got = 0;

		memset(buf, 0, sizeof buf);             /* a buffer grows fields: zero it first */
		buf[0].b = ab; buf[0].cap = 16; buf[0].len = 0; buf[0].caret = 0;
		buf[1].b = bb; buf[1].cap = 16; buf[1].len = 0; buf[1].caret = 0;
		wsk_init(&t2, WSK_COL, W, H);
		t2.item = F; t2.n = 2; t2.buf = buf; t2.bufs = 2;
		wsk_ui_init(&ui, &t2);
		ok(wsk_ui_focus(&ui, "a", &y0, &y1) == WSK_OK, "a field takes focus");

		memset(&e, 0, sizeof e);
		e.type = SDL_TEXTINPUT;
		snprintf(e.text.text, sizeof e.text.text, "hi");
		SDL_PushEvent(&e);
		ok(poll_until(&ev, 20) == WH_TEXT && strcmp(ev.text, "hi") == 0,
		   "typed text arrives from the host as WH_TEXT");
		memset(&we, 0, sizeof we);
		we.kind = WSK_EV_TEXT;
		memcpy(we.text, ev.text, sizeof we.text);
		got = wsk_feed(&ui, &we, nt, 2, &y0, &y1);
		ok(got == 1 && strcmp(ab, "hi") == 0, "and the toolkit puts it in the field");

		memset(&e, 0, sizeof e);
		e.type = SDL_KEYDOWN;
		e.key.keysym.sym = SDLK_TAB;
		SDL_PushEvent(&e);
		ok(poll_until(&ev, 20) == WH_KEY && ev.key == WH_KEY_TAB, "Tab arrives as WH_KEY_TAB");
		we.kind = WSK_EV_KEY; we.key = (unsigned short)ev.key;
		wsk_feed(&ui, &we, nt, 2, &y0, &y1);
		ok(t2.focus == &F[1], "and moves focus to the second field");

		e.key.keysym.sym = SDLK_BACKSPACE;
		SDL_PushEvent(&e);
		ok(poll_until(&ev, 20) == WH_KEY && ev.key == WH_KEY_BACKSPACE &&
		   (int)WH_KEY_BACKSPACE == (int)WSK_KEY_BACKSPACE,
		   "Backspace arrives as the same number whisker uses");
		e.key.keysym.sym = SDLK_q;
		SDL_PushEvent(&e);
		ok(poll_until(&ev, 20) != WH_QUIT, "q no longer quits: it is a letter now");
	}

	/* §5s: a desktop's plumbing through the host -- right-click, chords,
	   Shift where it means something, and the clipboard. */
	{
		static const wsk_widget_t F[] = {
			{ .kind = WSK_FIELD, .id = "a", .sub = 0, .size = 40 },
		};
		char ab[16] = "", clip[16], pb[16];
		wsk_buf_t buf[1];
		wsk_tree_t t3;
		wsk_ui_t ui;
		wsk_note_t nt[2];
		wsk_event_t we;
		wh_event_t ev;
		SDL_Event e;
		int y0, y1, got, pn;

		memset(buf, 0, sizeof buf);
		buf[0].b = ab; buf[0].cap = 16;
		wsk_init(&t3, WSK_COL, W, H);
		t3.item = F; t3.n = 1; t3.buf = buf; t3.bufs = 1;
		wsk_ui_init(&ui, &t3);
		ui.clip = clip; ui.clipcap = sizeof clip;
		wsk_ui_focus(&ui, "a", &y0, &y1);
		snprintf(ab, sizeof ab, "copy me"); buf[0].len = 7; buf[0].caret = 7;

		memset(&e, 0, sizeof e);
		e.type = SDL_MOUSEBUTTONDOWN;
		e.button.button = SDL_BUTTON_RIGHT;
		e.button.x = 30 * SCALE; e.button.y = 20 * SCALE;
		SDL_PushEvent(&e);
		ok(poll_until(&ev, 20) == WH_MENU && ev.x == 30 && ev.y == 20,
		   "a right-click arrives as WH_MENU, at the point -- not as a press");
		e.type = SDL_MOUSEBUTTONUP;
		SDL_PushEvent(&e);
		ok(poll_until(&ev, 5) == WH_NOTHING, "CONTROL: and its release is nothing at all");

		/* §5aa: motion with nothing held is hover; with the button, a drag */
		memset(&e, 0, sizeof e);
		e.type = SDL_MOUSEMOTION;
		e.motion.x = 40 * SCALE; e.motion.y = 25 * SCALE; e.motion.state = 0;
		SDL_PushEvent(&e);
		ok(poll_hover(&ev, 20) == WH_HOVER && ev.x == 40 && ev.y == 25,
		   "the mouse moved with no button held: WH_HOVER, at the point");
		e.motion.state = SDL_BUTTON_LMASK;
		SDL_PushEvent(&e);
		ok(poll_until(&ev, 20) == WH_MOVE, "CONTROL: with the button held, WH_MOVE -- a drag, not hover");
		memset(&e, 0, sizeof e);
		e.type = SDL_WINDOWEVENT;
		e.window.event = SDL_WINDOWEVENT_LEAVE;
		SDL_PushEvent(&e);
		ok(poll_hover(&ev, 20) == WH_HOVER && ev.x == -1 && ev.y == -1,
		   "the pointer leaves the window: WH_HOVER at -1, -1");

		memset(&e, 0, sizeof e);
		e.type = SDL_KEYDOWN;
		e.key.keysym.sym = SDLK_LEFT;
		e.key.keysym.mod = KMOD_LSHIFT;
		SDL_PushEvent(&e);
		ok(poll_until(&ev, 20) == WH_KEY && ev.key == (WH_KEY_LEFT | WH_MOD_SHIFT),
		   "Shift-Left arrives with its Shift");
		we.kind = WSK_EV_KEY; we.key = (unsigned short)ev.key;
		wsk_feed(&ui, &we, nt, 2, &y0, &y1);
		wsk_feed(&ui, &we, nt, 2, &y0, &y1);
		ok(buf[0].span == 2, "and through the toolkit, twice, selects \"me\"");
		e.key.keysym.sym = SDLK_RETURN;
		SDL_PushEvent(&e);
		ok(poll_until(&ev, 20) == WH_KEY && ev.key == WH_KEY_ENTER,
		   "Shift-Enter is Enter: Shift means nothing to it");
		e.key.keysym.sym = SDLK_c;
		e.key.keysym.mod = KMOD_LCTRL;
		SDL_PushEvent(&e);
		ok(poll_until(&ev, 20) == WH_KEY && ev.key == ('c' | WH_MOD_CTRL) && (int)WH_MOD_CTRL == (int)WSK_MOD_CTRL,
		   "Ctrl-C arrives as a chord, 'c' | WH_MOD_CTRL, whisker's numbers");
		we.key = (unsigned short)ev.key;
		got = wsk_feed(&ui, &we, nt, 2, &y0, &y1);
		ok(got == 1 && nt[0].kind == WSK_NOTE_COPY && wh_copy(clip, nt[0].value) == 0,
		   "the toolkit copies the selection, and the host puts it on the clipboard");
		pn = wh_paste(pb, sizeof pb);
		ok(pn == 2 && strcmp(pb, "me") == 0, "and the host's clipboard gives \"me\" back");
		ok(wh_paste(pb, 2) == -1, "CONTROL: into two bytes it does not fit with its NUL: -1, not a part");
		e.key.keysym.sym = SDLK_ESCAPE;
		e.key.keysym.mod = 0;
		SDL_PushEvent(&e);
		ok(poll_until(&ev, 20) == WH_KEY && ev.key == WH_KEY_ESCAPE,
		   "Escape is a key now, for a menu to close -- not a quit");
		e.key.keysym.sym = SDLK_APPLICATION;
		SDL_PushEvent(&e);
		ok(poll_until(&ev, 20) == WH_KEY && ev.key == WH_KEY_MENU && (int)WH_KEY_MENU == (int)WSK_KEY_MENU,
		   "the Menu key arrives as WH_KEY_MENU");
		SDL_SetModState(KMOD_LALT);
		memset(&e, 0, sizeof e);
		e.type = SDL_TEXTINPUT;
		snprintf(e.text.text, sizeof e.text.text, "x");
		SDL_PushEvent(&e);
		ok(poll_until(&ev, 5) == WH_NOTHING, "with Alt held, typed text is not text: Alt-x is a chord");
		SDL_SetModState(KMOD_NONE);
		SDL_PushEvent(&e);
		ok(poll_until(&ev, 20) == WH_TEXT, "CONTROL: without it, the same x is");
	}

	wh_close();
	printf(fails ? "\n%d check(s) failed\n" : "\nall host checks passed\n", fails);
	return fails != 0;
}
