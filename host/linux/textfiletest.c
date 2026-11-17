/*
 * textfile.h's checks (design.md §5aj step 1): each kind of file in and back
 * out byte for byte, and the controls -- a mixed file left alone, and a
 * Latin-1 file whose new text Latin-1 cannot hold refused, never written
 * lossily.
 */
#include "textfile.h"

#include <stdio.h>
#include <string.h>

static int fails;
static void ok(int c, const char *what) { printf("%s%s\n", c ? "ok    " : "FAIL  ", what); if (!c) fails++; }

/* in through tf_in, out through tf_out: what is edited, and what is written */
static int trip(const char *file, int len, const char *edited, int eol, int enc)
{
	char t[256], back[256];
	textfile_t f;
	int n, m;
	FILE *o;
	memcpy(t, file, (size_t)len);
	t[len] = '\0';
	n = tf_in(t, len, (int)sizeof t, &f);
	if (f.eol != eol || f.enc != enc || n != (int)strlen(edited) || memcmp(t, edited, (size_t)n) != 0) return 0;
	o = tmpfile();
	if (o == NULL || tf_out(o, t, n, &f) != 0) return 0;
	rewind(o);
	m = (int)fread(back, 1, sizeof back, o);
	fclose(o);
	return m == len && memcmp(back, file, (size_t)len) == 0;
}

int main(void)
{
	ok(trip("one\r\ntwo\r\n", 10, "one\ntwo\n", EOL_CRLF, ENC_UTF8), "a Windows file: edited as \\n, written back \\r\\n, byte for byte");
	ok(trip("one\rtwo\r", 8, "one\ntwo\n", EOL_CR, ENC_UTF8), "an old Mac file: edited as \\n, written back \\r");
	ok(trip("one\ntwo\n", 8, "one\ntwo\n", EOL_LF, ENC_UTF8), "a Unix file: untouched");
	ok(trip("no ending", 9, "no ending", EOL_LF, ENC_UTF8), "no line ending at all: untouched");
	ok(trip("one\r\ntwo", 8, "one\ntwo", EOL_CRLF, ENC_UTF8), "the last line without one: still CRLF, the same bytes back");
	ok(trip("one\r\ntwo\nthree\r\n", 16, "one\r\ntwo\nthree\r\n", EOL_LF, ENC_UTF8),
	   "CONTROL: mixed \\r\\n and \\n -- left exactly as it is, never converted");
	ok(trip("a\rb\r\nc", 6, "a\rb\r\nc", EOL_LF, ENC_UTF8), "and mixed \\r and \\r\\n: left as it is");
	ok(trip("", 0, "", EOL_LF, ENC_UTF8), "an empty file");
	ok(trip("caf\xc3\xa9\n", 6, "caf\xc3\xa9\n", EOL_LF, ENC_UTF8), "UTF-8: kept as it is");
	ok(trip("caf\xe9\n", 5, "caf\xc3\xa9\n", EOL_LF, ENC_LATIN1), "Latin-1 caf\\xe9: edited as UTF-8 caf\\xc3\\xa9, written back one byte");
	ok(trip("\xa9 1999\r\n\xb0""C\r\n", 12, "\xc2\xa9 1999\n\xc2\xb0""C\n", EOL_CRLF, ENC_LATIN1),
	   "Latin-1 with CRLF -- \\xa9 and \\xb0, bytes UTF-8 would take for continuations: both kept");
	{
		char t[64] = "caf\xe9";
		textfile_t f;
		int n = tf_in(t, 4, (int)sizeof t, &f);
		FILE *o = tmpfile();
		memcpy(t + n, " \xe2\x82\xac", 4);                /* a euro sign typed since: not Latin-1 */
		ok(f.enc == ENC_LATIN1 && o != NULL && tf_out(o, t, n + 4, &f) == -1 && ftell(o) == 0,
		   "CONTROL: a Latin-1 file given a euro sign: refused, and nothing written");
		if (o) fclose(o);
	}
	{
		char t[8] = "\xe9\xe9\xe9\xe9";
		textfile_t f;
		ok(tf_in(t, 4, 6, &f) == -1 && (unsigned char)t[0] == 0xe9, "Latin-1 that would not fit once decoded: -1, the bytes untouched");
	}
	printf(fails ? "\n%d text-file check(s) failed\n" : "\nall text-file checks passed\n", fails);
	return fails != 0;
}
