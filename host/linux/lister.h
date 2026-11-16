/*
 * A folder's entries for whisker_chooser.h (design.md §5ae), from POSIX:
 * whisker never reads a filesystem, so the program hands the chooser this.
 * The chooser sorts, filters and leaves out hidden names itself.
 */
#ifndef WHISKER_LISTER_H
#define WHISKER_LISTER_H

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static int posix_lister(void *ctx, const char *folder, wsk_entry_t *e, int max, char *names, int cap)
{
	DIR *d = opendir(folder);
	struct dirent *de;
	int n = 0, at = 0;
	(void)ctx;
	if (d == NULL) return -1;
	while ((de = readdir(d)) != NULL && n < max) {
		char full[4096];
		struct stat st;
		int len = (int)strlen(de->d_name);
		/* hidden names left out here, not after: counted, they took the
		   places of files that are shown (found by review, 2026-11-16) */
		if (de->d_name[0] == '.') continue;
		if (at + len + 1 > cap) break;
		snprintf(full, sizeof full, "%s/%s", folder, de->d_name);
		if (stat(full, &st) != 0) continue;          /* a link to nowhere: not offered */
		e[n].name = at;
		e[n].dir = (unsigned char)S_ISDIR(st.st_mode);
		memcpy(names + at, de->d_name, (size_t)len + 1);
		at += len + 1;
		n++;
	}
	closedir(d);
	return n;
}

#endif
