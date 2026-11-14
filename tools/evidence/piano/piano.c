/*
 * wpiano, run on this machine: the module itself, against RV-9's own window
 * for /w0, a script of fingers for /touch, and a /au0 that counts what it is
 * sent instead of playing it. tools/evidence/keys' harness, for the piano.
 *
 * What this can check before the board: that the keyboard fits SRC_MAX and
 * looks like a piano (piano.ppm, for a person); that a white key, a black key
 * and the upper part of a white key each strike their note; and that sound
 * is written. Not how it sounds, nor how soon -- that is the board's.
 */
#include "modlib.h"
#include "../../../src/module_piano.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void win_init(void);
void win_write(const void *buf, uint32_t len);
int win_setstat(uint32_t code, void *arg);
void win_shot(const char *name);
uint16_t win_at(int x, int y);

#define PW 1024
#define PH 600
#define TOUCH 5
#define AU 6
static uint32_t au_bytes;

static char said[8192];
static uint32_t n_said;
static char doc[8192];           /* the first document the module wrote */
static uint32_t n_doc;
static int doc_done;
static rv9_touch_event_t script[256];
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

static void tap(int x, int y) { ev(RV9_TOUCH_DOWN, x, y); ev(RV9_TOUCH_UP, x, y); }

/* one event of a given finger, for chords */
static void evf(int kind, int id, int x, int y) { ev(kind, x, y); script[n_script - 1].id = (uint8_t)id; }

static uint64_t now_us(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}

static int f_open(const char *name, uint32_t mode)
{ (void)mode; return strcmp(name, "/touch") == 0 ? TOUCH : strcmp(name, "/au0") == 0 ? AU : -1; }
static int f_close(int p) { (void)p; return 0; }

static int f_read(int p, void *buf, uint32_t len)
{
	int k = 0;
	if (p != TOUCH) return -1;
	while (next < n_script && k < 2 && (uint32_t)(k + 1) * sizeof(rv9_touch_event_t) <= len) {
		memcpy((char *)buf + k * sizeof(rv9_touch_event_t), &script[next], sizeof script[next]);
		next++; k++;
	}
	return k * (int)sizeof(rv9_touch_event_t);
}

static int f_write(int p, const void *buf, uint32_t len)
{
	if (p == AU) { au_bytes += len; return (int)len; }
	if (p == RV9_STDOUT) {
		if (!doc_done && n_doc + len < sizeof doc) {
			memcpy(doc + n_doc, buf, len);
			n_doc += len;
			if (n_doc >= 6 && memcmp(doc + n_doc - 6, "</svg>", 6) == 0) doc_done = 1;
		}
		win_write(buf, len);
		return (int)len;
	}
	fwrite(buf, 1, len, stdout);
	if (n_said + len < sizeof said) { memcpy(said + n_said, buf, len); n_said += len; }
	return (int)len;
}

static int f_getstat(int p, uint32_t code, void *arg)
{
	if (p == RV9_STDOUT && code == RV9_GS_SIZE) { *(uint32_t *)arg = ((uint32_t)PH << 16) | PW; return 0; }
	if (p == TOUCH && code == RV9_GS_READY) { *(uint32_t *)arg = (uint32_t)(n_script - next); return 0; }
	return -1;
}

static int f_setstat(int p, uint32_t code, void *arg)
{
	if (p != RV9_STDOUT) return -1;
	return win_setstat(code, arg);
}

static uint32_t f_signals(void) { return next >= n_script ? RV9_SIG_STOP : 0; }
static void f_sleep(uint32_t ms) { (void)ms; }

/* Where whisker drew a key's label: the x and baseline of its <text>, read
   back out of the document. The label is centred in its key, so a finger
   there is on the key. */

/* Where a white key's label is: its centre, in the lower row. */
static int label_at(const char *label, int *x, int *y)
{
	char want[24];
	const char *p, *q;
	snprintf(want, sizeof want, ">%s</text>", label);
	doc[n_doc] = '\0';
	p = strstr(doc, want);
	if (p == 0) return 0;
	for (q = p; q > doc && strncmp(q, "<text", 5) != 0; q--) ;
	if (sscanf(strstr(q, "x=\"") + 3, "%d", x) != 1) return 0;
	if (sscanf(strstr(q, "y=\"") + 3, "%d", y) != 1) return 0;
	return 1;
}

static int run(void)
{
	rv9_mod_env_t env;
	memset(&env, 0, sizeof env);
	env.abi_version = 13;
	env.open = f_open; env.close = f_close;
	env.read = f_read; env.write = f_write;
	env.getstat = f_getstat; env.setstat = f_setstat;
	env.time_us = now_us;
	env.signals_take = f_signals;
	env.sleep_ms = f_sleep;
	env.arg = "";
	return rv9_module_entry(&env);
}

int main(void)
{
	int r, fails = 0, cx, cy, dx, dy, ex, ey;
	const char *p;

#define CHECK(c, what) do { printf("%s  %s\n", (c) ? "ok  " : "FAIL", what); fails += !(c); } while (0)
	win_init();
	r = run();
	said[n_said] = '\0';
	CHECK(r == 0 && doc_done, "wpiano draws its keyboard");
	printf("     the keyboard is %u bytes of /w0's 4096\n", n_doc);
	CHECK(n_doc > 0 && n_doc <= 4096, "and fits SRC_MAX");
	win_shot("piano.ppm");

	/* C4 in the lower row; C#4 in the upper row, half way from C4's label
	   to D4's; E4's upper part, above E4's label. The upper row is 64 rows
	   down and three fifths of the rest deep. */
	CHECK(label_at("C4", &cx, &cy) && label_at("D4", &dx, &dy) && label_at("E4", &ex, &ey),
	      "the white keys are labelled where the script expects");
	tap(cx, cy);
	tap((cx + dx) / 2, 200);
	tap(ex, 200);
	n_said = 0; next = 0; au_bytes = 0;
	r = run();
	said[n_said] = '\0';
	CHECK(r == 0, "it played and stopped when asked");
	p = strstr(said, "wpiano: 3 notes");
	CHECK(p != 0, "three taps struck three notes: a white key, a black key, a white key's upper part");
	printf("     %u bytes of sound written\n", au_bytes);
	CHECK(au_bytes > 0, "and sound was written to /au0");
	CHECK(strstr(said, "refused") == 0, "no repaint was refused");

	/* A chord: two fingers down, C4 and E4, each moving a little on its own
	   key, interleaved as the controller reports them, then lifted. Two
	   notes, not a re-strike at every move -- which is what the first
	   version did, reading every event as one finger's. */
	n_script = 0; next = 0; n_said = 0;
	evf(RV9_TOUCH_DOWN, 0, cx, cy);
	evf(RV9_TOUCH_DOWN, 1, ex, ey);
	for (int i = 0; i < 6; i++) {
		evf(RV9_TOUCH_MOVE, 0, cx + i, cy);
		evf(RV9_TOUCH_MOVE, 1, ex - i, ey);
	}
	evf(RV9_TOUCH_UP, 0, cx, cy);
	evf(RV9_TOUCH_UP, 1, ex, ey);
	r = run();
	said[n_said] = '\0';
	CHECK(strstr(said, "wpiano: 2 notes") != 0,
	      "a chord: two fingers on two keys strike two notes, however they move");
	if (fails) { printf("\n%d check(s) failed\n", fails); return 1; }
	printf("\nall piano checks passed\n");
	return 0;
}
