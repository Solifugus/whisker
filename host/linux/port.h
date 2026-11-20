/*
 * What a desktop program needs of its platform that POSIX and Windows say
 * differently (design.md §5au). Three calls, each the POSIX one where there
 * is POSIX -- so Linux and macOS compile exactly what they did.
 *
 *   wh_fullpath(p, out)   an absolute path with '/' between its parts,
 *                         out PATH_MAX long; NULL if p is not there
 *   wh_sync(fd)           to the disk, not only to the system
 *   wh_replace(from, to)  from put in to's place, to there or not
 *
 * Windows' rename refuses a name that exists, so a save made the usual way
 * -- written beside, then renamed over -- would fail every time after the
 * first. Paths are UTF-8 there because the program's manifest says so
 * (host/linux/win/whisker.manifest).
 */
#ifndef WH_PORT_H
#define WH_PORT_H

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <io.h>

static inline char *wh_fullpath(const char *p, char *out)
{
	if (_fullpath(out, p, PATH_MAX) == NULL) return NULL;
	if (GetFileAttributesA(out) == INVALID_FILE_ATTRIBUTES) return NULL;
	for (char *c = out; *c; c++) if (*c == '\\') *c = '/';
	return out;
}
static inline int wh_sync(int fd) { return _commit(fd); }
static inline int wh_replace(const char *from, const char *to)
{
	return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
}
#else
#include <unistd.h>

static inline char *wh_fullpath(const char *p, char *out) { return realpath(p, out); }
static inline int wh_sync(int fd) { return fsync(fd); }
static inline int wh_replace(const char *from, const char *to) { return rename(from, to); }
#endif

#endif
