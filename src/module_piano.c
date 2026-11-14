/*
 * wpiano -- two octaves to play with a finger.
 *
 *     wpiano > /w0
 *
 * design.md §9.4's first unconventional widget, and the reason the P4 has a
 * speaker worth using. C4 to B5: fourteen white keys and ten black, drawn by
 * whisker and sounded through /au0 the way RV-9's `play notes` sounds them --
 * a triangle with its octave above, struck and dying away.
 *
 * NO SHAPES NEEDED. A piano's black keys lie over its white ones, and
 * whisker's stacks never overlap -- so the keyboard is two rows. The lower
 * row is the fourteen white keys, full width. The upper row is the black keys
 * and, between them, the narrow upper parts of the white keys, each sized so
 * that it sits exactly over its own lower part; where two white keys meet
 * with no black key between (E-F, B-C), a two-pixel spacer keeps the line
 * between them. Every part a finger can land on is a rectangle, and the
 * picture is a piano. The sizes are worked out here, at run time, from the
 * screen's width -- which is why the rows are arrays in RAM, not static
 * panels.
 *
 * A NOTE SOUNDS ON TOUCH-DOWN, not on release: a key that waits for the
 * finger to lift is not a key. Sliding along the keys plays each one it
 * crosses. EVERY FINGER IS ITS OWN: /touch says which finger each event is,
 * and each of up to five holds its own key, so a chord is a chord. The first
 * version read every event as one finger's, and two fingers down re-struck
 * each other's keys -- Matthew heard it as "only one sound channel". Six
 * voices, mixed, so five held notes and one still ringing all sound.
 *
 * A HELD KEY LIGHTS A STRIP, not itself. A key is nearly the screen's
 * height, and repainting one costs about 200 ms on the P4 (a pane of 552 rows
 * measured 229, §5e) -- during which nothing else runs: no finger is read and
 * no note starts. Holding three keys and letting go was six such repaints, and
 * the notes after them came late; Matthew heard it as notes being queued. So
 * a thin strip across the top of the keys, one segment over each key, lights
 * instead: a dozen rows to repaint, not five hundred. A dark segment is an
 * empty label, which draws nothing, and only a lit one is a rectangle -- so
 * the strip costs SRC_MAX nothing until a key is held. And in every pass the
 * notes are struck and their first sound written BEFORE anything is drawn.
 *
 * LATENCY. Audio is written only a little ahead of what has played (LEAD
 * frames, 32 ms), so a new note waits behind 32 ms of the old one at worst,
 * not behind a second of it. The program reports the worst time from the
 * finger's report to the first samples of its note being written, which is
 * most of what a person hears as lag; the codec's own buffer is the rest, and
 * is not measured here.
 */
#include "modlib.h"
#include "whisker.h"

#define RATE    16000
#define LEAD    512              /* frames written ahead of the clock: 32 ms */
#define CHUNK   128              /* frames a write: 8 ms */
#define VOICES  6              /* five fingers and one still ringing */
#define FINGERS 5
#define WHITES  14
#define HEAD    64

/* Phase steps a sample at 16 kHz for octave 4, C to B: f * 2^32 / 16000,
   from millihertz, worked out by the compiler -- RV-9's `play` does the same,
   and a module has no 64-bit division at run time. */
#define STEP(mhz) ((uint32_t)((mhz##ull << 32) / (RATE * 1000ull)))
static const uint32_t OCTAVE4[12] = {
	STEP(261626), STEP(277183), STEP(293665), STEP(311127), STEP(329628),
	STEP(349228), STEP(369994), STEP(391995), STEP(415305), STEP(440000),
	STEP(466164), STEP(493883),
};
/* the semitone of each white key in an octave, and whether a black key
   follows it */
static const unsigned char WHITE_SEMI[7] = { 0, 2, 4, 5, 7, 9, 11 };
static const unsigned char BLACK_AFTER[7] = { 1, 1, 0, 1, 1, 1, 0 };

typedef struct { uint32_t inc, phase, age; int32_t amp; } voice_t;

static int32_t tri(uint32_t phase)
{
	int32_t x = (int32_t)(phase >> 16);
	return (x < 32768 ? x : 65535 - x) * 2 - 32768;
}

typedef struct { const rv9_mod_env_t *env; int path; } out_t;

static void to_path(wsk_sink_t *s, const char *b, int n)
{
	out_t *o = (out_t *)s->ctx;
	o->env->write(o->path, b, (unsigned int)n);
}

static void repaint(const rv9_mod_env_t *env, const wsk_tree_t *w, wsk_sink_t *s,
                    const char *id)
{
	const char *ids[1];
	int y0 = 0, y1 = 0;
	uint32_t v;
	ids[0] = id;
	if (wsk_rows(w, ids, 1, &y0, &y1) != WSK_OK) return;
	v = ((uint32_t)y0 << 16) | ((uint32_t)y1 & 0xFFFFu);
	env->setstat(RV9_STDOUT, RV9_SVG_SS_ROWS, &v);
	(void)wsk_render_rows(w, y0, y1, s, 4096);
}

/* A key's name for its label and its id: C4, C#4 ... */
static void name_of(char *d, int semi, int upper)
{
	static const char L[7] = { 'C', 'D', 'E', 'F', 'G', 'A', 'B' };
	int oct = 4 + semi / 12, s = semi % 12, k = 0, i;
	for (i = 0; i < 7 && WHITE_SEMI[i] != s; i++) ;
	if (i == 7) {                            /* a black key: the white below, sharp */
		for (i = 0; i < 7 && WHITE_SEMI[i] != s - 1; i++) ;
		d[k++] = L[i]; d[k++] = '#';
	} else d[k++] = L[i];
	d[k++] = (char)('0' + oct);
	if (upper) d[k++] = 'u';               /* the upper part of a white key */
	d[k] = '\0';
}

static void key(wsk_widget_t *w, int semi, int size, int black, int upper)
{
	int i;
	for (i = 0; i < WSK_ID_MAX; i++) w->id[i] = '\0';
	for (i = 0; i < WSK_TEXT_MAX; i++) w->text[i] = '\0';
	w->kind = WSK_BUTTON;
	w->size = (short)size;
	w->weight = 0;
	w->fill = black ? 0x181818u : 0xf2efe6u;
	w->ink = black ? 0xe0e0e0u : 0x303030u;
	w->mark = 0;
	w->sub = (short)semi;                    /* a button's sub: whisker ignores it */
	name_of(w->id, semi, upper);
	if (!black && !upper) name_of(w->text, semi, 0);
}

static void spacer(wsk_widget_t *w, int size)
{
	int i;
	for (i = 0; i < WSK_ID_MAX; i++) w->id[i] = '\0';
	for (i = 0; i < WSK_TEXT_MAX; i++) w->text[i] = '\0';
	w->kind = WSK_LABEL;
	w->size = (short)size;
	w->weight = 0; w->fill = 0; w->ink = 0; w->mark = 0;
	w->sub = -1;
}

__attribute__((section(".text.entry")))
int rv9_module_entry(const rv9_mod_env_t *env)
{
	static const wsk_widget_t WIN[] = {
		{ .kind = WSK_LABEL, .id = "title", .text = "whisker piano -- C4 to B5", .size = HEAD },
		{ .kind = WSK_STACK, .id = "lights", .sub = 2, .size = 12 },
		{ .kind = WSK_STACK, .id = "upper", .sub = 0, .weight = 3 },
		{ .kind = WSK_STACK, .id = "lower", .sub = 1, .weight = 2 },
	};
	wsk_widget_t up[WHITES + 10 + 4], low[WHITES], light[WHITES + 10 + 4];
	int lowmap[WHITES];                     /* a white key's upper piece, and light */
	int16_t pcm[CHUNK * 2];
	voice_t v[VOICES];
	wsk_tree_t w, k[3];
	wsk_sink_t s;
	out_t out;
	uint32_t size = 0, written = 0, worst_ms = 0, notes = 0, late = 0;
	uint64_t t0 = 0;
	int tp, au, wkey, gap = 2, n_up = 0, next_voice = 0;
	wsk_widget_t *finger[FINGERS];          /* the key each finger holds */
	int lit[WHITES + 10 + 4];               /* fingers on each segment's key */
	int c, x;

	if (env == NULL || env->abi_version < 13) return 1;
	{
		uint32_t none = 0;
		if (env->setstat(RV9_STDOUT, RV9_SVG_SS_ROWS, &none) < 0) {
			m_say(env, RV9_STDERR, "wpiano: stdout is not a window; send it to /w0\n");
			return 2;
		}
	}
	if (env->getstat(RV9_STDOUT, RV9_GS_SIZE, &size) < 0) return 2;

	/* The keys, sized from the screen. White key i spans x_i .. x_i + wkey,
	   x_i = i * (wkey + gap); a black key sits centred on the gap after a
	   white key that has one, 3/5 of a white key wide. The upper row is
	   built in order so that each piece lands where the lower row says. */
	wkey = ((int)(size & 0xFFFFu) - (WHITES - 1) * gap) / WHITES;
	{
		int black = wkey * 3 / 5, cur = 0;
		for (int i = 0; i < WHITES; i++) {
			int semi = WHITE_SEMI[i % 7] + 12 * (i / 7);
			int xi = i * (wkey + gap), end = xi + wkey;
			int has = BLACK_AFTER[i % 7] && i != WHITES - 1;
			key(&low[i], semi, wkey, 0, 0);
			if (i > 0) {                        /* this key's left: after a black key? */
				int prev = BLACK_AFTER[(i - 1) % 7];
				if (!prev) cur = xi;             /* the spacer brought us here */
			}
			c = end + gap / 2;                   /* the middle of the gap after it */
			x = has ? c - black / 2 : end;
			lowmap[i] = n_up;
			key(&up[n_up++], semi, x - cur, 0, 1);
			if (has) {
				key(&up[n_up++], semi + 1, black, 1, 0);
				cur = c - black / 2 + black;
			} else if (i != WHITES - 1) {
				spacer(&up[n_up++], gap);
				cur = end + gap;
			}
		}
	}

	wsk_init(&w, WSK_COL, (int)(size & 0xFFFFu), (int)(size >> 16));
	w.pad = 0; w.gap = 0; w.font = 24; w.bg = 0x10203au; w.ids = 0;
	w.item = WIN; w.n = 4; w.nest = k; w.nests = 3;
	wsk_nest_init(&k[0], &w, WSK_ROW);
	k[0].pad = 0; k[0].gap = 0; k[0].item = up; k[0].n = n_up; k[0].font = 20;
	wsk_nest_init(&k[1], &w, WSK_ROW);
	k[1].pad = 0; k[1].gap = gap; k[1].item = low; k[1].n = WHITES; k[1].font = 20;
	/* the strip: a segment over each piece of the upper row, the same widths */
	for (int i = 0; i < n_up; i++) {
		spacer(&light[i], up[i].size);
		light[i].fill = 0xffb040u;              /* used when it is lit */
		light[i].id[0] = 'l';
		light[i].id[1] = (char)('a' + i);       /* la, lb ... : 28 at most */
		lit[i] = 0;
	}
	wsk_nest_init(&k[2], &w, WSK_ROW);
	k[2].pad = 0; k[2].gap = 0; k[2].item = light; k[2].n = n_up;
	{
		int which = -1, e;
		const wsk_tree_t *in = 0;
		if ((e = wsk_check_in(&w, &which, &in)) != WSK_OK) {
			m_say(env, RV9_STDERR, "wpiano: the keyboard was refused, ");
			m_num(env, RV9_STDERR, e);
			m_say(env, RV9_STDERR, "\n");
			return 3;
		}
	}

	out.env = env; out.path = RV9_STDOUT;
	s.put = to_path; s.ctx = &out; s.len = 0;
	if (wsk_render(&w, &s, 4096) < 0) {
		m_say(env, RV9_STDERR, "wpiano: the keyboard is over SRC_MAX\n");
		return 4;
	}

	au = env->open("/au0", RV9_MODE_WRITE);
	if (au < 0) { m_say(env, RV9_STDERR, "wpiano: /au0: cannot open\n"); return 5; }
	tp = env->open("/touch", RV9_MODE_READ);
	if (tp < 0) { env->close(au); return 0; }
	for (int i = 0; i < VOICES; i++) { v[i].inc = 0; v[i].amp = 0; v[i].phase = 0; v[i].age = 0; }
	for (int i = 0; i < FINGERS; i++) finger[i] = 0;
	m_say(env, RV9_STDERR, "wpiano: play -- C4 to B5\n");

	for (;;) {
		uint32_t waiting = 0, due;
		int active = 0, strip = 0;

		if (env->signals_take() & RV9_SIG_STOP) break;

		/* Fingers first: a note starts on the frame it was touched. */
		if (env->getstat(tp, RV9_GS_READY, &waiting) == 0 && waiting > 0) {
			rv9_touch_event_t te[4];
			int n = env->read(tp, te, sizeof te), cnt;
			if (n < 0) break;
			cnt = n / (int)sizeof(rv9_touch_event_t);
			for (int i = 0; i < cnt; i++) {
				const wsk_tree_t *in = 0;
				int f = te[i].id % FINGERS, kk, semi = -1;
				wsk_widget_t *now = 0;
				if (te[i].kind != RV9_TOUCH_UP) {
					kk = wsk_hit_in(&w, (int)te[i].x, (int)te[i].y, &in);
					if (kk >= 0 && in->item[kk].kind == WSK_BUTTON && in != &k[2]) {
						now = (wsk_widget_t *)&in->item[kk];   /* our own RAM */
						semi = now->sub;
					}
				}
				if (now == finger[f]) continue;          /* the same key, still held */
				if (finger[f] != 0) {                    /* this finger lets go */
					int li = (finger[f] >= up && finger[f] < up + n_up)
					       ? (int)(finger[f] - up) : lowmap[finger[f] - low];
					if (--lit[li] == 0) { light[li].kind = WSK_LABEL; strip = 1; }
					finger[f] = 0;
				}
				if (now == 0) continue;
				{                                        /* and strikes this one */
					voice_t *vo = &v[next_voice];
					int li = (now >= up && now < up + n_up) ? (int)(now - up) : lowmap[now - low];
					next_voice = (next_voice + 1) % VOICES;
					vo->inc = OCTAVE4[semi % 12] << (semi / 12);
					vo->phase = 0; vo->age = 0; vo->amp = 32767;
					notes++;
					if (lit[li]++ == 0) { light[li].kind = WSK_BUTTON; strip = 1; }
				}
				finger[f] = now;
				{
					uint32_t ms = m_age_ms(env->time_us(), (uint64_t)te[i].at_ms * 1000u);
					if (ms > worst_ms) worst_ms = ms;
					/* one worst figure cannot say whether it was once or always:
					   the first board run's worst was 1,522 ms against a person
					   who heard every note at once */
					if (ms > 50) late++;
				}
			}
		}

		/* Then sound: keep LEAD frames written ahead of the clock, while any
		   voice is still ringing. */
		for (int i = 0; i < VOICES; i++) if (v[i].inc != 0) active = 1;
		if (!active) {
			written = 0;
			if (strip) repaint(env, &w, &s, "lights");
			env->sleep_ms(2);
			continue;
		}
		if (written == 0) t0 = env->time_us();
		/* frames due by now: 16 a millisecond. The clock restarts at every
		   silence, so the elapsed time fits 32 bits, and a module may not
		   divide 64 (rv9-answers.md §2.5). */
		due = (uint32_t)(env->time_us() - t0) / 125u * 2u + LEAD;
		/* Time the loop spent elsewhere is gone, not owed. Writing it now
		   would fill the codec's queue (6 x 320 frames, 120 ms) with sound
		   that should already have played, and every note after would wait
		   behind it -- the second board run's 5.7 s. So a gap is skipped. */
		if (due > written + LEAD) written = due - LEAD;
		/* and no pass writes more than LEAD, so the fingers are read again
		   within a few milliseconds whatever the sound is doing */
		if (due > written + LEAD) due = written + LEAD;
		while (written < due) {
			for (int f = 0; f < CHUNK; f++) {
				int32_t mix = 0;
				for (int i = 0; i < VOICES; i++) {
					voice_t *vo = &v[i];
					int32_t e, wv;
					if (vo->inc == 0) continue;
					e = vo->amp;
					if (vo->age < 32) e = e * (int32_t)vo->age / 32;      /* 2 ms rise */
					wv = tri(vo->phase) + tri(vo->phase * 2) / 3;
					mix += (wv / 3) * e / 32768;
					vo->phase += vo->inc;
					vo->amp -= vo->amp >> 12;                              /* struck, dying */
					vo->age++;
					if (vo->amp < 64 || vo->age > RATE * 2) vo->inc = 0;   /* two seconds at most */
				}
				if (mix > 32767) mix = 32767;
				if (mix < -32768) mix = -32768;
				pcm[2 * f] = (int16_t)mix;
				pcm[2 * f + 1] = (int16_t)mix;
			}
			if (env->write(au, pcm, sizeof pcm) != (int)sizeof pcm) break;
			written += CHUNK;
		}
		/* the sound is out; now the strip, a dozen rows */
		if (strip) repaint(env, &w, &s, "lights");
	}

	env->close(tp);
	env->close(au);
	m_say(env, RV9_STDERR, "wpiano: ");
	m_num(env, RV9_STDERR, (int32_t)notes);
	m_say(env, RV9_STDERR, " notes; finger to sound written, worst ");
	m_num(env, RV9_STDERR, (int32_t)worst_ms);
	m_say(env, RV9_STDERR, " ms, ");
	m_num(env, RV9_STDERR, (int32_t)late);
	m_say(env, RV9_STDERR, " over 50 ms\n");
	return 0;
}
