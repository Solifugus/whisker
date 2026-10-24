/*
 * wscroll, run on this machine: the module itself, unmodified, against a
 * stand-in for RV-9's environment -- RV-9's own window and rasteriser for
 * /w0, and a script of finger events for /touch.
 *
 * Not a measurement of time. The repaint figures this prints are this
 * machine's and mean nothing about the P4; that number is what the board
 * run is for (design.md §5e). What this CAN check before the board does:
 *
 *   - that the module's loop drives whisker the way §5e says -- a drag is a
 *     run of pane-only repaints, a tap is named by hit through the slice,
 *     and the row it names is the row whose label is under the finger;
 *   - that every frame of a drag is accepted (no refusal halfway down a
 *     ten-thousand-row list), and the picture at the end is right, which a
 *     person checks in drag.ppm.
 *
 * The script: press at y 400, drag up 300 rows in 12-row MOVEs, lift; then
 * tap at y 330, which after a 300-row scroll is inside row 11 (rows are 48
 * high with a gap of 2, so row 11 spans 550..598 of the list, and 330 is
 * 282 rows into a pane that starts at 48 and has scrolled 300).
 */
#include "modlib.h"
#include "../../../src/module_scroll.c"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* window.c: RV-9's window, in its own translation unit because its host
   stubs and module.h each declare the same types differently. */
void win_init(void);
void win_write(const void *buf, uint32_t len);
int win_setstat(uint32_t code, void *arg);
void win_shot(const char *name);
uint16_t win_at(int x, int y);

#define PW 1024
#define PH 600

#define TOUCH 5
static char said[4096];          /* what the module told stderr */
static uint32_t n_said;
static rv9_touch_event_t script[64];
static int n_script, next;

static void ev(int kind, int x, int y)
{
	script[n_script].kind = (uint8_t)kind;
	script[n_script].id = 0;
	script[n_script].x = (uint16_t)x;
	script[n_script].y = (uint16_t)y;
	script[n_script].pressure = 0;
	script[n_script].at_ms = 0;
	n_script++;
}

static uint64_t now_us(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}

static int f_open(const char *name, uint32_t mode)
{ (void)mode; return strcmp(name, "/touch") == 0 ? TOUCH : -1; }
static int f_close(int p) { (void)p; return 0; }

/* Two events a read, so the module's coalescing has something to do. */
static int f_read(int p, void *buf, uint32_t len)
{
	int k = 0;
	if (p != TOUCH) return -1;
	while (next < n_script && k < 2 && (uint32_t)(k + 1) * sizeof(rv9_touch_event_t) <= len) {
		script[next].at_ms = (uint32_t)(now_us() / 1000u);
		memcpy((char *)buf + k * sizeof(rv9_touch_event_t), &script[next], sizeof script[next]);
		next++; k++;
	}
	return k * (int)sizeof(rv9_touch_event_t);
}

static int f_write(int p, const void *buf, uint32_t len)
{
	if (p == RV9_STDOUT) { win_write(buf, len); return (int)len; }
	fwrite(buf, 1, len, stdout);
	if (n_said + len < sizeof said) { memcpy(said + n_said, buf, len); n_said += len; }
	return (int)len;
}

static int f_getstat(int p, uint32_t code, void *arg)
{
	if (p == RV9_STDOUT && code == RV9_GS_SIZE) {
		*(uint32_t *)arg = ((uint32_t)PH << 16) | PW;
		return 0;
	}
	if (p == TOUCH && code == RV9_GS_READY) {
		*(uint32_t *)arg = (uint32_t)(n_script - next);
		return 0;
	}
	return -1;
}

static int f_setstat(int p, uint32_t code, void *arg)
{
	if (p != RV9_STDOUT) return -1;
	return win_setstat(code, arg);
}

static uint32_t f_signals(void) { return next >= n_script ? RV9_SIG_STOP : 0; }
static void f_sleep(uint32_t ms) { (void)ms; }

int main(void)
{
	rv9_mod_env_t env;
	int y, r;

	win_init();

	ev(RV9_TOUCH_DOWN, 512, 400);
	for (y = 388; y >= 100; y -= 12) ev(RV9_TOUCH_MOVE, 512, y);
	ev(RV9_TOUCH_UP, 512, 100);
	ev(RV9_TOUCH_DOWN, 512, 330);
	ev(RV9_TOUCH_UP, 514, 331);

	memset(&env, 0, sizeof env);
	env.abi_version = 13;
	env.open = f_open; env.close = f_close;
	env.read = f_read; env.write = f_write;
	env.getstat = f_getstat; env.setstat = f_setstat;
	env.time_us = now_us;
	env.signals_take = f_signals;
	env.sleep_ms = f_sleep;
	env.arg = "";

	r = rv9_module_entry(&env);
	printf("wscroll returned %d\n\n", r);
	{
		int fails = 0;
#define CHECK(c, what) do { printf("%s  %s\n", (c) ? "ok  " : "FAIL", what); fails += !(c); } while (0)
		said[n_said] = '\0';
		CHECK(r == 0, "it ran to the end and stopped when asked");
		CHECK(strstr(said, "drag ended at row 6\n") != 0,
		      "a 300-row drag up ends with row 6 at the top (300 / 50)");
		CHECK(strstr(said, "tap 512,330 -> r11, labelled Row 11\n") != 0,
		      "the tap through the scrolled slice names row 11, and row 11 is what is there");
		CHECK(strstr(said, "refused") == 0, "and no frame of it was refused");
		win_shot("drag.ppm");
		CHECK(win_at(300, 330) == 0x8b45u,
		      "at the pixels: the selected row's fill is under the tap");
		CHECK(win_at(300, 230) != 0x8b45u,
		      "CONTROL: and not under the row above it");
		if (fails) { printf("\n%d check(s) failed\n", fails); return 1; }
		printf("\nall drag checks passed\n");
	}

	return 0;
}
