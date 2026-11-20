/*
 * The chooser on Windows (design.md §5au): a drive's root is a top, as "/"
 * is. Built with WSK_CH_DRIVES -- what _WIN32 turns on -- so it runs here;
 * test.c, built without, checks that on Linux "C:/x" is still a folder
 * named "C:" (the control).
 *
 *     cc -DWSK_CH_DRIVES drives.c && ./a.out
 */
#define WSK_CH_DRIVES
#include "whisker.h"
#include "whisker_chooser.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void ok(int cond, const char *what)
{
	printf("%s  %s\n", cond ? "ok  " : "FAIL", what);
	if (!cond) fails++;
}

/* A drive that is not a disk. */
static int drive_lister(void *ctx, const char *folder, wsk_entry_t *e, int max, char *names, int cap)
{
	static const char *const LS[] = {
		"C:/", "Users/", "readme.bas", 0,
		"C:/Users", "me/", 0,
		"C:/Users/me", "a.bas", 0,
		0,
	};
	int i = 0, n = 0, at = 0, found = 0;
	(void)ctx;
	while (LS[i] != 0) {
		int here = (strcmp(LS[i], folder) == 0);
		found |= here;
		i++;
		for (; LS[i] != 0; i++) {
			int len = (int)strlen(LS[i]), dir = (LS[i][len - 1] == '/');
			if (!here || n >= max || at + len + 1 > cap) continue;
			e[n].name = at; e[n].dir = (unsigned char)dir;
			memcpy(names + at, LS[i], (size_t)(len - dir));
			names[at + len - dir] = '\0';
			at += len - dir + 1;
			n++;
		}
		i++;
	}
	return found ? n : -1;
}

int main(void)
{
	static const wsk_widget_t MAINW[] = { { .kind = WSK_LABEL, .id = "main", .text = "the program", .weight = 1 } };
	static wsk_chooser_t ch;
	static const char *strs[2 + WSK_CH_MAX];
	wsk_buf_t cb[1];
	wsk_tree_t root, k[4];
	wsk_ui_t ui;
	wsk_note_t nt[4];
	wsk_event_t ev;
	int y0, y1, r;

	printf("\n-- §5au  a drive's root is a top (WSK_CH_DRIVES)\n");
	ok(wsk__ch_top("C:/x") == 3 && wsk__ch_top("d:/") == 3 && wsk__ch_top("/x") == 1,
	   "the top of C:/x is C:/, of d:/ d:/, of /x /");
	ok(wsk__ch_top("x") == 0 && wsk__ch_top("C:") == 0 && wsk__ch_top("C:x") == 0 && wsk__ch_top("1:/") == 0,
	   "CONTROL: x, C: (no slash), C:x and 1:/ are paths from here");

	memset(cb, 0, sizeof cb);
	wsk_init(&root, WSK_COL, 640, 400);
	root.item = MAINW; root.n = 1; root.nest = k; root.nests = 4;
	root.str = strs; root.strs = 2 + WSK_CH_MAX; root.buf = cb; root.bufs = 1;
	for (int i = 0; i < 4; i++) wsk_nest_init(&k[i], &root, WSK_COL);
	wsk_ui_init(&ui, &root);
	wsk_chooser_init(&ch, &root, strs, 0, 0, 0, drive_lister, 0);

#define CLICK(X, Y) (memset(&ev, 0, sizeof ev), ev.kind = WSK_EV_DOWN, ev.x = (X), ev.y = (Y), \
	wsk_feed(&ui, &ev, nt, 4, &y0, &y1), ev.kind = WSK_EV_UP, \
	wsk_chooser_feed(&ch, &ui, nt, wsk_feed(&ui, &ev, nt, 4, &y0, &y1), &y0, &y1))
	ok(wsk_chooser_open(&ch, &ui, WSK_CHOOSE_FILE, "Open", "C:/Users/me", 0, 0, 400, 300, &y0, &y1) == WSK_OK
	   && ch.n == 1 && strcmp(strs[2], "a.bas") == 0, "Open in C:/Users/me: a.bas");
	CLICK(160, 108);
	ok(strcmp(ch.folder, "C:/Users") == 0 && ch.n == 1, "Up: C:/Users");
	CLICK(160, 108);
	ok(strcmp(ch.folder, "C:/") == 0 && ch.n == 2 && strcmp(strs[2], "Users/") == 0,
	   "Up: C:/ -- the drive's root, its slash kept, its folder and file listed");
	CLICK(160, 108);
	ok(strcmp(ch.folder, "C:/") == 0 && ch.n == 2, "Up at C:/ stays there: never C:, never /, never .");
	CLICK(200, 166);
	r = CLICK(200, 166);
	ok(r == WSK_CHOSEN && strcmp(ch.path, "C:/readme.bas") == 0,
	   "readme.bas chosen in C:/: C:/readme.bas -- one slash, not two");

	wsk_chooser_open(&ch, &ui, WSK_CHOOSE_SAVE, "Save as", "C:/Users/me", "x.bas", 0, 400, 300, &y0, &y1);
	r = wsk_chooser_answer(&ch, &ui, "D:/elsewhere.bas", &y0, &y1);
	ok(r == WSK_CHOSEN && strcmp(ch.path, "D:/elsewhere.bas") == 0,
	   "a name that is a path on another drive is itself: D:/elsewhere.bas, not C:/Users/me/D:/...");
	wsk_chooser_open(&ch, &ui, WSK_CHOOSE_SAVE, "Save as", "C:/Users/me", "x.bas", 0, 400, 300, &y0, &y1);
	r = wsk_chooser_answer(&ch, &ui, "y.bas", &y0, &y1);
	ok(r == WSK_CHOSEN && strcmp(ch.path, "C:/Users/me/y.bas") == 0, "CONTROL: a plain name joins the folder");

	printf("\n%s\n", fails ? "drive checks FAILED" : "all drive checks passed");
	return fails ? 1 : 0;
}
