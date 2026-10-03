/*
 * The same panel, compiled two ways.
 *
 * A reduced build (see Profiles in whisker.h) must differ from a full one in
 * exactly two ways and no others:
 *
 *   - it leaves out code for widgets it was told to leave out, and
 *   - it REFUSES a tree that uses one, naming the widget.
 *
 * It must NOT draw anything differently. A profile that quietly shifted a
 * button by a pixel would be a second renderer wearing the first one's name,
 * and D2's "one implementation, not a specification and two" would be gone
 * the moment somebody shipped a host on the small profile.
 *
 * So this prints one of three things and `run.sh` compares the builds:
 *
 *     ./profile panel    the shared panel, which must be byte-identical
 *     ./profile field    a tree with a field  -- full draws, minimal refuses
 *     ./profile marked   a pressed button     -- full draws, minimal refuses
 *
 * The refusals are the control. Without them a minimal build could satisfy
 * `panel` by drawing a field as a label, and the diff would still pass.
 */
#include <stdio.h>
#include <string.h>
#include "whisker.h"

static void to_out(wsk_sink_t *s, const char *b, int n)
{
	(void)s;
	fwrite(b, 1, (size_t)n, stdout);
}

/* Only what every profile has: no field, no mark. */
static const wsk_widget_t SHARED[] = {
	{ .kind = WSK_LABEL,  .id = "title",  .text = "Settings" },
	{ .kind = WSK_BUTTON, .id = "ok",     .text = "OK" },
	{ .kind = WSK_BUTTON, .id = "cancel", .text = "Cancel", .fill = 0x8f2a2au },
};

int main(int argc, char **argv)
{
	wsk_widget_t live[3];
	wsk_tree_t t;
	wsk_sink_t s;
	const char *what = (argc > 1) ? argv[1] : "panel";
	int which = -1, e;

	wsk_clone(live, SHARED, 3);
	wsk_init(&t, WSK_COL, 320, 172);
	t.pad = 4; t.gap = 4; t.ids = 1; t.item = live; t.n = 3;

	if (strcmp(what, "field") == 0) {
		live[1].kind = WSK_FIELD;
		live[1].text[0] = '\0';
	} else if (strcmp(what, "marked") == 0) {
		live[1].mark = WSK_MARK_CORNERS;
	}

	s.put = to_out; s.ctx = 0; s.len = 0;
	if (strcmp(what, "panel") == 0) {
		e = wsk_render(&t, &s, 4096);
		if (e < 0) { printf("refused %d\n", e); return 1; }
		return 0;
	}

	/* The two that a reduced build must refuse rather than draw. */
	e = wsk_check(&t, &which);
	if (e != WSK_OK) {
		printf("refused %d at widget %d (%s)\n", e, which, live[which].id);
		return 0;
	}
	s.put = 0; s.ctx = 0; s.len = 0;
	e = wsk_render(&t, &s, 4096);
	printf("drew %d bytes\n", e);
	return 0;
}
