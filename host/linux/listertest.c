/*
 * The POSIX lister (lister.h) over a folder crowded with hidden names
 * (design.md §5ae, the second review): 300 of them and 10 files, and the
 * chooser's 256 places -- all 10 must be offered. Before the fix the hidden
 * names took the places, in readdir's order, and files went missing.
 */
#include "whisker.h"
#include "whisker_chooser.h"
#include "lister.h"

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
	static wsk_entry_t e[WSK_CH_MAX];
	static char names[WSK_CH_NAMES];
	char dir[] = "/tmp/whisker-lister-XXXXXX", p[256];
	int n, fails = 0;
	if (mkdtemp(dir) == NULL) { perror("mkdtemp"); return 1; }
	for (int i = 0; i < 300; i++) { snprintf(p, sizeof p, "%s/.h%03d", dir, i); fclose(fopen(p, "w")); }
	for (int i = 0; i < 10; i++) { snprintf(p, sizeof p, "%s/v%02d.bas", dir, i); fclose(fopen(p, "w")); }
	n = posix_lister(0, dir, e, WSK_CH_MAX, names, WSK_CH_NAMES);
	printf("%s    a folder of 300 hidden names and 10 files: %d listed, all of them files\n",
	       n == 10 ? "ok  " : "FAIL", n);
	if (n != 10) fails++;
	snprintf(p, sizeof p, "rm -rf '%s'", dir);
	if (system(p) != 0) return 1;
	return fails != 0;
}
