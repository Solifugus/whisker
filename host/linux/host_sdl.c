/*
 * The contract, over SDL2 -- and over RV-9's own rasteriser.
 *
 * The pixels are not drawn by SDL. They are drawn by `raster.c`, `font.c` and
 * `drv_svgwin.c`, compiled from RV-9's tree through the forty lines of stubs
 * it already keeps for exactly this, so WHAT YOU SEE IS WHAT THE BOARD DRAWS
 * -- the same parser, the same bands, the same clip, the same font. SDL puts
 * the resulting buffer in a window and reports where you clicked. That is the
 * whole of its job, and keeping it that small is what §6 asks for.
 *
 * The one thing that is not the board: scale. A 320x172 panel is a postage
 * stamp on a modern screen, so the window may be shown at 2x or 3x. The
 * scaling happens when the buffer is presented, never when it is rasterised,
 * so the picture is pixel-for-pixel the device's and only the viewing is
 * magnified. Input is divided back down, so a click lands where a finger
 * would.
 */
#include "host.h"

/*
 * RV-9's sources are freestanding and use short internal names. `fmul` is one
 * of them, and C23's <math.h> -- which SDL pulls in -- declares a function of
 * that name with a different signature, so the two collide the moment the
 * rasteriser is compiled inside a hosted program. Renaming it here costs
 * nothing and leaves RV-9's tree alone; anyone else embedding raster.c in a
 * hosted build will meet the same thing.
 */
#define fmul rv9_svg_fmul

/* RV-9's driver says "drew N bytes in M ms" for every document -- 25 lines a
   second from an animated program, which buried the program's own output
   (a floor demo's "walk to" lines). Its information lines only when asked:
   WHISKER_VERBOSE=1. Warnings and errors always. */
#include <stdlib.h>
#include "esp_log.h"
#undef ESP_LOGI
#define ESP_LOGI(t, ...) do { if (getenv("WHISKER_VERBOSE") != 0) { \
	printf("I %s: ", t); printf(__VA_ARGS__); printf("\n"); } } while (0)
#include "drv_svgwin.c"      /* statics and all, as tools/hosttest does */

#undef fmul                  /* or math.h's gets renamed too, and collides */

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
	int         w, h, scale;
	uint16_t   *fb;          /* device pixels, native order */
	SDL_Window *win;
	SDL_Renderer *ren;
	SDL_Texture *tex;
	rv9_dev_t   dev;
	int         dirty;
	int         open;
} S;

/* ---- the panel RV-9's driver expects to be talking to ---- */

rv9_io_err_t rv9_panel_open(bool landscape, int *w, int *h)
{
	(void)landscape;
	if (w) *w = S.w;
	if (h) *h = S.h;
	return RV9_IO_OK;
}

void rv9_panel_size(int *w, int *h) { if (w) *w = S.w; if (h) *h = S.h; }
void rv9_panel_backlight(uint32_t pct) { (void)pct; }
uint32_t rv9_panel_backlight_get(void) { return 100; }
bool rv9_panel_take(const void *owner) { (void)owner; return false; }
/* RV-9 split the panel in two on 2026-10-11: the C5's SPI ST7789 wants
   its pixels byte-swapped and the P4's MIPI-DSI does not. These harnesses
   have always emulated the C5, and every swap in this file assumes it, so
   say so rather than inherit whichever the linker finds. */
bool rv9_panel_swaps_bytes(void) { return true; }

/*
 * One band, pushed. The driver hands these over in 8-row strips and only for
 * the rows it was asked to repaint, which is the behaviour the whole design
 * rests on -- so the host honours it rather than redrawing the screen.
 */
void rv9_panel_blit(int x0, int y0, int x1, int y1, const uint16_t *px)
{
	for (int y = y0; y < y1; y++) {
		const uint16_t *src = &px[(size_t)(y - y0) * (size_t)(x1 - x0)];
		uint16_t *dst = &S.fb[(size_t)y * (size_t)S.w + (size_t)x0];
		for (int x = 0; x < x1 - x0; x++) {
			uint16_t v = src[x];           /* the panel's byte order */
			dst[x] = (uint16_t)((v >> 8) | (v << 8));
		}
	}
	S.dirty = 1;
}

/* ---- the contract ---- */

int wh_open(int w, int h, int scale, const char *title)
{
	if (scale < 1) scale = 1;
	memset(&S, 0, sizeof S);
	S.w = w; S.h = h; S.scale = scale;

	S.fb = (uint16_t *)calloc((size_t)w * (size_t)h, sizeof(uint16_t));
	if (S.fb == NULL) { fprintf(stderr, "whisker-host: no memory\n"); return -1; }

	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "whisker-host: SDL: %s\n", SDL_GetError());
		return -1;
	}
	S.win = SDL_CreateWindow(title ? title : "whisker",
	                         SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
	                         w * scale, h * scale, 0);
	if (S.win == NULL) { fprintf(stderr, "whisker-host: %s\n", SDL_GetError()); return -1; }
	/* Typed characters arrive as text, already through the keyboard's
	   layout, which is the host's business (§5j) -- not as keys to map. */
	SDL_StartTextInput();

	S.ren = SDL_CreateRenderer(S.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (S.ren == NULL) S.ren = SDL_CreateRenderer(S.win, -1, 0);
	if (S.ren == NULL) { fprintf(stderr, "whisker-host: %s\n", SDL_GetError()); return -1; }

	/* Nearest, not smooth: a device pixel magnified should look like a device
	   pixel, or the host flatters the picture the board will actually draw. */
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
	S.tex = SDL_CreateTexture(S.ren, SDL_PIXELFORMAT_RGB565,
	                          SDL_TEXTUREACCESS_STREAMING, w, h);
	if (S.tex == NULL) { fprintf(stderr, "whisker-host: %s\n", SDL_GetError()); return -1; }

	/* The device descriptor: landscape, and a black background, which is what
	   /w0's descriptor sets and is why a clipped document must carry its own
	   backing. The host does not get to be kinder about that than the board. */
	memset(&S.dev, 0, sizeof S.dev);
	S.dev.opt[OPT_ROTATE] = 1;
	S.dev.opt[OPT_BG] = 0x0000;
	if (svgwin_init(&S.dev) != RV9_IO_OK || svgwin_open(&S.dev, 3) != RV9_IO_OK) {
		fprintf(stderr, "whisker-host: the window device would not open\n");
		return -1;
	}
	S.open = 1;
	return 0;
}

void wh_clip(int y0, int y1)
{
	uint32_t v = ((uint32_t)y0 << 16) | ((uint32_t)y1 & 0xFFFFu);
	svgwin_setstat(&S.dev, RV9_SVG_SS_ROWS, &v);
}

static void present(void)
{
	SDL_UpdateTexture(S.tex, NULL, S.fb, S.w * (int)sizeof(uint16_t));
	SDL_RenderClear(S.ren);
	SDL_RenderCopy(S.ren, S.tex, NULL, NULL);
	SDL_RenderPresent(S.ren);
	S.dirty = 0;
}

void wh_write(const void *bytes, int n)
{
	size_t done;
	svgwin_write(&S.dev, bytes, (size_t)n, &done);
	if (S.dirty) present();
}

static int point_of(int wx, int wy, wh_event_t *out)
{
	out->x = wx / S.scale;
	out->y = wy / S.scale;
	return (out->x >= 0 && out->x < S.w && out->y >= 0 && out->y < S.h);
}

int wh_poll(wh_event_t *out)
{
	SDL_Event e;

	out->kind = WH_NOTHING;
	while (SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_QUIT:
			out->kind = WH_QUIT;
			return WH_QUIT;
		case SDL_KEYDOWN: {
			/* Escape is a key, as in a browser: it closes a menu, and a
			   program with none open may take it to mean quit. It used to
			   quit here, until right-click opened menus (§5s); `q` used to,
			   until a field could be typed into (§5j). */
			SDL_Keycode k = e.key.keysym.sym;
			Uint16 m = e.key.keysym.mod;
			int key = 0, mods = ((m & KMOD_SHIFT) ? WH_MOD_SHIFT : 0) |
			                    ((m & KMOD_CTRL) ? WH_MOD_CTRL : 0) |
			                    ((m & KMOD_LALT) ? WH_MOD_ALT : 0);
			if (k == SDLK_ESCAPE) key = WH_KEY_ESCAPE;
			else if (k == SDLK_APPLICATION || (k == SDLK_F10 && (m & KMOD_SHIFT)))
				{ key = WH_KEY_MENU; mods = 0; }
			else if (k == SDLK_LEFT) key = WH_KEY_LEFT;
			else if (k == SDLK_RIGHT) key = WH_KEY_RIGHT;
			else if (k == SDLK_HOME) key = WH_KEY_HOME;
			else if (k == SDLK_UP) key = WH_KEY_UP;
			else if (k == SDLK_DOWN) key = WH_KEY_DOWN;
			else if (k == SDLK_END) key = WH_KEY_END;
			else if (k == SDLK_BACKSPACE) key = WH_KEY_BACKSPACE;
			else if (k == SDLK_DELETE) key = WH_KEY_DELETE;
			else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) key = WH_KEY_ENTER;
			else if (k == SDLK_TAB) {
				key = (m & KMOD_SHIFT) ? WH_KEY_BACKTAB : WH_KEY_TAB;
				mods &= ~WH_MOD_SHIFT;
			}
			/* a chord: a printable key with Ctrl or Alt -- SDL's keycode for
			   one is the character, lower-case, as the contract wants */
			else if ((mods & (WH_MOD_CTRL | WH_MOD_ALT)) && k > 32 && k < 127)
				key = (int)k;
			/* Shift means something only to a key that moves the caret,
			   and to a chord; Shift-Enter is Enter */
			if (!(mods & (WH_MOD_CTRL | WH_MOD_ALT)) && key != WH_KEY_LEFT && key != WH_KEY_RIGHT &&
			    key != WH_KEY_HOME && key != WH_KEY_END && key != WH_KEY_UP && key != WH_KEY_DOWN)
				mods &= ~WH_MOD_SHIFT;
			if (key != 0) { out->kind = WH_KEY; out->key = key | mods; return WH_KEY; }
			break;
		}
		case SDL_TEXTINPUT: {
			int i = 0;
			/* a chord is a key, not text: SDL drops Ctrl's control
			   characters itself, but Alt-x would type an x as well */
			if ((SDL_GetModState() & (KMOD_CTRL | KMOD_LALT)) &&
			    !(SDL_GetModState() & (KMOD_RALT | KMOD_MODE))) break;
			while (i < (int)sizeof out->text - 1 && e.text.text[i] != '\0') {
				out->text[i] = e.text.text[i];
				i++;
			}
			out->text[i] = '\0';
			out->kind = WH_TEXT;
			return WH_TEXT;
		}
		case SDL_MOUSEMOTION:
			if (e.motion.which == SDL_TOUCH_MOUSEID) break;
			if ((e.motion.state & SDL_BUTTON_LMASK) && point_of(e.motion.x, e.motion.y, out)) {
				out->kind = WH_MOVE;
				return WH_MOVE;
			}
			break;
		case SDL_MOUSEWHEEL: {
			int mx, my;
			SDL_GetMouseState(&mx, &my);
			if (point_of(mx, my, out)) {
				out->kind = WH_WHEEL;
				out->d = -e.wheel.y * 40;
				return WH_WHEEL;
			}
			break;
		}
		case SDL_WINDOWEVENT:
			if (e.window.event == SDL_WINDOWEVENT_EXPOSED) present();
			break;
		/* A mouse is a one-point pointer and so is a finger: the contract
		   wants a point, and does not care which produced it. The panel
		   reports five and whisker uses one (§2.6). */
		case SDL_MOUSEBUTTONDOWN:
			/* a touchscreen's touch is taken as a finger, below; the mouse
			   press SDL makes of it too would press twice */
			if (e.button.which == SDL_TOUCH_MOUSEID) break;
			/* the right button asks for a context menu (§5s); the middle
			   one means nothing here */
			if (e.button.button == SDL_BUTTON_RIGHT && point_of(e.button.x, e.button.y, out))
				{ out->kind = WH_MENU; return WH_MENU; }
			if (e.button.button == SDL_BUTTON_LEFT && point_of(e.button.x, e.button.y, out)) {
				out->kind = WH_PRESS;
				out->key = (SDL_GetModState() & KMOD_SHIFT) ? WH_MOD_SHIFT : 0;
				out->d = e.button.clicks;   /* SDL counts, by the desktop's timing */
				return WH_PRESS;
			}
			break;
		case SDL_MOUSEBUTTONUP:
			if (e.button.which == SDL_TOUCH_MOUSEID) break;
			if (e.button.button == SDL_BUTTON_LEFT && point_of(e.button.x, e.button.y, out))
				{ out->kind = WH_RELEASE; return WH_RELEASE; }
			break;
		/* A finger on a touchscreen, where its position is on the window.
		   Not a touchpad's: SDL 2.32 reports those as fingers too, at
		   fractions of the PAD, so a tap pressed somewhere else as well as
		   where the pointer was -- two presses and no click (2026-11-15,
		   Matthew's ThinkPad, a click on the floor demo doing nothing). A
		   touchpad's taps arrive as mouse clicks anyway. */
		case SDL_FINGERDOWN:
		case SDL_FINGERUP:
		case SDL_FINGERMOTION:
			if (SDL_GetTouchDeviceType(e.tfinger.touchId) != SDL_TOUCH_DEVICE_DIRECT) break;
			if (!point_of((int)(e.tfinger.x * S.w * S.scale),
			              (int)(e.tfinger.y * S.h * S.scale), out)) break;
			if (e.type == SDL_FINGERDOWN)
				{ out->kind = WH_PRESS; out->key = 0; out->d = 1; return WH_PRESS; }
			out->kind = (e.type == SDL_FINGERUP) ? WH_RELEASE : WH_MOVE;
			return out->kind;
		default:
			break;
		}
	}
	return WH_NOTHING;
}

void wh_wait(int ms) { SDL_WaitEventTimeout(NULL, ms); }

/* The clipboard is SDL's: the desktop's under X11 or Wayland, and SDL's own
   copy under its dummy driver, which is what the self test runs on. */
int wh_copy(const char *s, int n)
{
	char *t = (char *)malloc((size_t)n + 1);
	int e;
	if (t == NULL) return -1;
	memcpy(t, s, (size_t)n);
	t[n] = '\0';
	e = SDL_SetClipboardText(t);
	free(t);
	return (e == 0) ? 0 : -1;
}

int wh_paste(char *b, int cap)
{
	char *t;
	int n;
	if (!SDL_HasClipboardText() || (t = SDL_GetClipboardText()) == NULL) return -1;
	n = (int)strlen(t);
	if (n == 0 || n >= cap) { SDL_free(t); return -1; }
	memcpy(b, t, (size_t)n + 1);
	SDL_free(t);
	return n;
}

/* RV-9's own rasteriser draws the pixels here, so this host can draw exactly
   what /w0 can, and says nothing more (§5r). */
/* RV-9's /w0 decodes &lt; &gt; &amp; since its 8e0ef96 (2026-11-15), and
   this window compiles its drv_svgwin.c from that tree; nothing else yet --
   its font is ASCII, it draws no image and does not clip (§5r) */
int wh_caps(void) { return WH_CAP_ENTITY; }

void wh_close(void)
{
	if (!S.open) return;
	svgwin_close(&S.dev);
	if (S.tex) SDL_DestroyTexture(S.tex);
	if (S.ren) SDL_DestroyRenderer(S.ren);
	if (S.win) SDL_DestroyWindow(S.win);
	SDL_Quit();
	free(S.fb);
	S.open = 0;
}
