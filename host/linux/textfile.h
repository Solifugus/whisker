/*
 * A text file's line endings and encoding, for a program that edits files
 * (design.md §5aj step 1).
 *
 * whisker's memo breaks lines at "\n" and holds UTF-8. A file written on
 * Windows ends its lines "\r\n", an old Mac "\r"; an older file may be
 * Latin-1, where é is one byte that is not UTF-8. So a program reading one
 * into a memo says what it was, edits it as "\n" and UTF-8, and writes it
 * back the way it came -- the bytes on disk unchanged by having been
 * opened. Qt converts on load too; what it does not do is remember.
 *
 * Line endings are converted only when CONSISTENT -- every one "\r\n", or
 * every one a lone "\r". Mixed, the file is left as it is: converting would
 * lose which line had which, and its stray "\r"s are what the memo shows as
 * markers (§5aj step 2).
 *
 * The encoding: valid UTF-8 is kept; anything else is read as Latin-1,
 * where every byte is a character, so nothing is lost or guessed at beyond
 * that. Written back as Latin-1 -- and refused, never written lossily, if
 * text typed since has a character Latin-1 has not.
 *
 * The program's, not whisker's: whisker reads no files, and a gBASIC
 * backend will do the same at its own seam.
 */
#ifndef WHISKER_TEXTFILE_H
#define WHISKER_TEXTFILE_H

#include <stdio.h>
#include <string.h>

enum { EOL_LF = 0, EOL_CRLF, EOL_CR };
enum { ENC_UTF8 = 0, ENC_LATIN1 };

typedef struct { int eol, enc; } textfile_t;

/* Whether t[0..len) is UTF-8: well formed, none the long way round, no
   surrogate, nothing past U+10FFFF. */
static inline int tf_utf8(const unsigned char *t, int len)
{
	for (int i = 0; i < len; ) {
		unsigned char c = t[i];
		int more;
		if (c < 0x80) { i++; continue; }
		if (c >= 0xC2 && c <= 0xDF) more = 1;
		else if (c >= 0xE0 && c <= 0xEF) more = 2;
		else if (c >= 0xF0 && c <= 0xF4) more = 3;
		else return 0;
		for (int k = 1; k <= more; k++)
			if (i + k >= len || (t[i + k] & 0xC0) != 0x80) return 0;
		if ((c == 0xE0 && t[i + 1] < 0xA0) || (c == 0xED && t[i + 1] > 0x9F) ||
		    (c == 0xF0 && t[i + 1] < 0x90) || (c == 0xF4 && t[i + 1] > 0x8F)) return 0;
		i += more + 1;
	}
	return 1;
}

/* t[0..len), a file as read, made "\n" and UTF-8 in place (t has `cap`
   bytes): the new length, and in *f what it was -- or -1 if decoding
   Latin-1 would not fit in cap, t untouched. */
static inline int tf_in(char *t, int len, int cap, textfile_t *f)
{
	int crlf = 0, cr = 0, lf = 0, k = 0, high = 0;
	f->eol = EOL_LF;
	f->enc = ENC_UTF8;
	if (!tf_utf8((const unsigned char *)t, len)) {
		for (int i = 0; i < len; i++) if ((unsigned char)t[i] >= 0x80) high++;
		if (len + high > cap - 1) return -1;
		for (int i = len - 1, j = len + high - 1; i >= 0; i--) {   /* from the end: in place */
			unsigned char c = (unsigned char)t[i];
			if (c < 0x80) t[j--] = (char)c;
			else { t[j--] = (char)(0x80 | (c & 0x3F)); t[j--] = (char)(0xC0 | (c >> 6)); }
		}
		len += high;
		f->enc = ENC_LATIN1;
	}
	for (int i = 0; i < len; i++) {
		if (t[i] == '\r' && i + 1 < len && t[i + 1] == '\n') { crlf++; i++; }
		else if (t[i] == '\r') cr++;
		else if (t[i] == '\n') lf++;
	}
	if (crlf > 0 && cr == 0 && lf == 0) f->eol = EOL_CRLF;
	else if (cr > 0 && crlf == 0 && lf == 0) f->eol = EOL_CR;
	if (f->eol != EOL_LF) {
		for (int i = 0; i < len; i++) {
			if (t[i] == '\r' && f->eol == EOL_CRLF) continue;   /* every one is followed by \n */
			t[k++] = (t[i] == '\r') ? '\n' : t[i];
		}
		len = k;
	}
	t[len] = '\0';
	return len;
}

/* Whether t[0..len) can be written as Latin-1: no character past U+00FF. */
static inline int tf_latin1_ok(const char *t, int len)
{
	for (int i = 0; i < len; i++) {
		unsigned char c = (unsigned char)t[i];
		if (c >= 0x80 && c != 0xC2 && c != 0xC3 && (c & 0xC0) != 0x80) return 0;
	}
	return 1;
}

/* One byte, as `f` writes it: a line break its ending. */
static inline int tf_put(FILE *o, const char *s, int n)
{
	return (n <= 0 || (int)fwrite(s, 1, (size_t)n, o) == n) ? 0 : -1;
}

/* t[0..len) written to o as `f` was read: 0, or -1 if o took less or the
   text cannot be written in its encoding (nothing is written then). */
static inline int tf_out(FILE *o, const char *t, int len, const textfile_t *f)
{
	int from = 0;
	if (f->enc == ENC_LATIN1 && !tf_latin1_ok(t, len)) return -1;
	if (f->eol == EOL_LF && f->enc == ENC_UTF8) return tf_put(o, t, len);
	for (int i = 0; i <= len; i++) {
		unsigned char c = (i < len) ? (unsigned char)t[i] : 0;
		if (i < len && c != '\n' && !(f->enc == ENC_LATIN1 && c >= 0x80)) continue;
		if (tf_put(o, t + from, i - from) != 0) return -1;
		if (i == len) break;
		if (c == '\n') {
			const char *e = (f->eol == EOL_CRLF) ? "\r\n" : (f->eol == EOL_CR) ? "\r" : "\n";
			if (tf_put(o, e, (int)strlen(e)) != 0) return -1;
			from = i + 1;
		} else {                                   /* a two-byte UTF-8 character, one Latin-1 byte */
			char b = (char)(((c & 0x1F) << 6) | ((unsigned char)t[i + 1] & 0x3F));
			if (tf_put(o, &b, 1) != 0) return -1;
			i++;
			from = i + 1;
		}
	}
	return 0;
}

/* What the status line says: nothing for UTF-8 with \n. */
static inline const char *tf_name(const textfile_t *f, char *b, int cap)
{
	snprintf(b, (size_t)cap, "%s%s%s", f->enc == ENC_LATIN1 ? "Latin-1" : "",
	         (f->enc == ENC_LATIN1 && f->eol != EOL_LF) ? " " : "",
	         f->eol == EOL_CRLF ? "CRLF" : f->eol == EOL_CR ? "CR" : "");
	return b;
}

#endif
