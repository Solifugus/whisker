/*
 * wkeys, run on this machine: the module itself, unmodified, against
 * RV-9's own window for /w0 and a script of finger events for /touch --
 * tools/evidence/drag's harness, for the keyboard (design.md §5j).
 *
 * Two runs. The first has no fingers: it draws the keyboard, which is how
 * this learns two things -- how big the document is, against /w0's 4,096
 * (SRC_MAX), which is the number that decides whether a keyboard fits on the
 * P4 at all (CLAUDE.md, "the two budgets"); and where each key is, read out
 * of the document itself, so the script taps the keys whisker drew rather
 * than where a comment says they are. The second run types with them.
 */
#include "modlib.h"
#include "../../../src/module_keys.c"

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

static uint64_t now_us(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}

static int f_open(const char *name, uint32_t mode)
{ (void)mode; return strcmp(name, "/touch") == 0 ? TOUCH : -1; }
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
static int key_at(const char *label, int *x, int *y)
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
	*y -= 10;                    /* from the baseline into the key */
	return 1;
}

static int type(const char *s)
{
	int x, y;
	char one[2] = { 0, 0 };
	for (; *s; s++) {
		if (*s == ' ') { if (!key_at("space", &x, &y)) return 0; }
		else { one[0] = *s; if (!key_at(one, &x, &y)) return 0; }
		tap(x, y);
	}
	return 1;
}

static int press(const char *label)
{
	int x, y;
	if (!key_at(label, &x, &y)) return 0;
	tap(x, y);
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
	int r, fails = 0, bytes = 0, scripted = 1;
	const char *p;

#define CHECK(c, what) do { printf("%s  %s\n", (c) ? "ok  " : "FAIL", what); fails += !(c); } while (0)
	win_init();
	r = run();                                  /* no fingers: just the keyboard */
	CHECK(r == 0 && doc_done, "wkeys draws its keyboard");
	said[n_said] = '\0';
	p = strstr(said, "wkeys: ");
	if (p != 0) bytes = atoi(p + 7);
	printf("     the keyboard is %d bytes (n_doc %u) of /w0's 4096\n", bytes, n_doc);
	CHECK(bytes > 0 && bytes <= 4096, "and fits SRC_MAX, ids off");
	win_shot("keys.ppm");

	/* Type, edit, enter -- every finger on a key whisker drew. */
	scripted &= type("hi there");
	scripted &= press("Enter");
	scripted &= press("Left");
	scripted &= press("Left");
	scripted &= press("Del");
	scripted &= press("Enter");
	CHECK(scripted, "every key the script wants is on the keyboard");
	/* The 123 key swaps the layer; its digits are not on the first
	   document, so they are tapped where the letter row's keys were --
	   the digits row is the q row's place, key for key. */
	{
		int x, y;
		scripted = press("123");
		scripted &= key_at("w", &x, &y);    /* the 2 is where the w was */
		tap(x, y);
		scripted &= key_at("e", &x, &y);    /* and the 3 where the e was */
		tap(x, y);
		scripted &= key_at("123", &x, &y);  /* abc is where 123 was */
		tap(x, y);
		scripted &= press("Enter");
		CHECK(scripted, "and the layer keys are where the script expects");
	}
	/* Shift: a capital, then lower case again by itself. The capitals are
	   where their small letters were. */
	{
		int x, y;
		scripted = press("Clear");
		scripted &= press("Shift");
		scripted &= key_at("h", &x, &y); tap(x, y);   /* H, after Shift */
		scripted &= key_at("i", &x, &y); tap(x, y);   /* i, Shift spent */
		scripted &= press("Enter");
		CHECK(scripted, "Shift and the letters after it are where the script expects");
	}
	n_said = 0; next = 0;
	r = run();
	said[n_said] = '\0';
	CHECK(r == 0, "it ran to the end and stopped when asked");
	CHECK(strstr(said, "entered: hi there\n") != 0, "eight keys typed \"hi there\", and Enter said so");
	CHECK(strstr(said, "entered: hi thre\n") != 0,
	      "Left, Left, Del: the e before the caret went -- \"hi thre\"");
	CHECK(strstr(said, "entered: hi th23re\n") != 0,
	      "123 swaps in the digits, and 2 and 3 go in at the caret, which Del left after \"hi th\"");
	CHECK(strstr(said, "entered: Hi\n") != 0,
	      "Shift, h, i: \"Hi\" -- one capital, then lower case by itself");
	CHECK(strstr(said, "refused") == 0, "no repaint was refused");
	win_shot("keys_typed.ppm");
	if (fails) { printf("\n%d check(s) failed\n", fails); return 1; }
	printf("\nall keyboard checks passed\n");
	return 0;
}
