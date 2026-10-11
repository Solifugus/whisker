/*
 * wslop -- how far from where you meant does a tap land?
 *
 *     wslop > /w0          five targets, eight taps each
 *     wslop 12 > /w0       twelve taps each
 *
 * The last thing on the roadmap that genuinely needed hardware. design.md
 * §5b left touch *slop* open on purpose: whether a tap two pixels into a gap
 * should resolve to the neighbour is a question about what a capacitive
 * surface actually reports, and a mouse has no answer to give -- it reports
 * the pixel you were over. D7 said so when it made the mouse stand in for
 * touch, and said what it could not stand in for. This is that.
 *
 * WHAT IT SEPARATES, and why one number would be useless.
 *
 *   BIAS      the mean offset from the mark. A tap that lands consistently
 *             five pixels high is a calibration or a parallax problem, it
 *             belongs to the driver or the glass, and no tolerance in
 *             whisker should paper over it -- that would hide a fixable
 *             fault behind a widget that is quietly bigger than it looks.
 *   SCATTER   the spread around that mean. THIS is whisker's question: it
 *             is the radius a hit test would have to forgive, and the gap
 *             between two widgets has to be wider than it or a confident
 *             tap lands on the neighbour.
 *   DRIFT     how far the finger moves between DOWN and UP. If that exceeds
 *             a widget, press and release belong to different widgets and
 *             the caller needs a policy -- which whisker does not have,
 *             because it holds no state (D5).
 *
 * Five marks rather than one, because scatter may not be uniform: the
 * corners of a capacitive grid are where the interpolation has least to work
 * with, and a figure measured only in the middle would be the flattering one.
 *
 * It reports pixels AND millimetres, which it could not have done before
 * 2026-10-11 -- RV9_GS_PHYSICAL is what makes a scatter figure portable
 * between two panels of different pitch (scale-sketch.md §7). A tolerance in
 * pixels is a tolerance for one panel.
 */
/*
 * Marks and the field compiled out (D8). This program draws a label and a
 * filled box and nothing else, so WSK_MINIMAL costs it nothing -- and it buys
 * something worth more than the 668 bytes: corner marks are the only <path>
 * whisker can emit, and a document with no path never reaches RV-9's do_path
 * or rv9_raster_fill_n, which is a 624-byte frame on the writer's own stack.
 * Compiled out, "this document happens to contain no path" becomes a property
 * of the binary, so the stack figure below stays true if somebody edits this.
 *
 * Only safe since 2026-10-04: modules/whisker.h predated profiles until then,
 * and this define would have compiled cleanly, changed nothing, and left the
 * path in -- a false guarantee readable in the source. whisker.from now says
 * e7d1a4a, which has them.
 */
#define WSK_MINIMAL 1

#include "modlib.h"
#include "whisker.h"

#define TARGETS   5
#define MAX_TAPS  32
#define MARK      14          /* the mark's box, device pixels */

typedef struct { const rv9_mod_env_t *env; int path; } out_t;

static void to_path(wsk_sink_t *s, const char *b, int n)
{
	out_t *o = (out_t *)s->ctx;
	o->env->write(o->path, b, (unsigned int)n);
}

/* Integer square root: a module has no libm, and the radius wants to be a
   radius rather than a sum of squares nobody can picture. */
static uint32_t isqrt(uint32_t v)
{
	uint32_t r = 0, b = 1u << 30;

	while (b > v) b >>= 2;
	while (b != 0) {
		if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
		else r >>= 1;
		b >>= 2;
	}
	return r;
}

static void say_num(const rv9_mod_env_t *env, const char *before, int32_t v,
                    const char *after)
{
	if (before != 0) m_say(env, RV9_STDERR, before);
	m_num(env, RV9_STDERR, v);
	if (after != 0) m_say(env, RV9_STDERR, after);
}

/*
 * Micrometres for a count of pixels, or -1 when the panel will not say how
 * big it is.
 *
 * 32-bit throughout, and that is a rule rather than a preference: the first
 * version of this reached for an int64_t and the linker answered with an
 * undefined reference to __divdi3, because a module links against no runtime
 * library. The product is safe unsigned -- a 1024-pixel panel 153,000 um wide
 * is 157 million, well inside 32 bits -- and the sign is handled outside it.
 */
static int32_t to_um(const rv9_physical_t *p, int px)
{
	uint32_t mag;

	if (p->kind == RV9_PHYS_UNKNOWN || p->width_px == 0 || p->width_um == 0)
		return -1;
	mag = (uint32_t)(px < 0 ? -px : px);
	mag = mag * p->width_um / p->width_px;
	return (px < 0) ? -(int32_t)mag : (int32_t)mag;
}

static void say_px_mm(const rv9_mod_env_t *env, const rv9_physical_t *p,
                      const char *label, int px)
{
	int32_t um = to_um(p, px);

	m_say(env, RV9_STDERR, label);
	m_num(env, RV9_STDERR, px);
	m_say(env, RV9_STDERR, " px");
	if (um >= 0) {
		m_say(env, RV9_STDERR, " (");
		m_num(env, RV9_STDERR, um / 1000);
		m_say(env, RV9_STDERR, ".");
		m_num(env, RV9_STDERR, (um % 1000) / 100);
		m_say(env, RV9_STDERR, " mm)");
	}
	m_say(env, RV9_STDERR, "\n");
}

__attribute__((section(".text.entry")))
int rv9_module_entry(const rv9_mod_env_t *env)
{
	wsk_widget_t mark[1], head[1];
	wsk_tree_t part[2];
	wsk_doc_t doc;
	wsk_sink_t s;
	out_t out;
	rv9_physical_t phys;
	int tx[TARGETS], ty[TARGETS];
	int dx[MAX_TAPS], dy[MAX_TAPS], drift[MAX_TAPS];
	uint32_t size = 0;
	int w, h, taps = 8, t, i, n, touch;
	int down_x = 0, down_y = 0, have_down = 0;

	if (env == NULL || env->abi_version < 11) return 1;

	{
		uint32_t none = 0;
		if (env->setstat(RV9_STDOUT, RV9_SVG_SS_ROWS, &none) < 0) {
			m_say(env, RV9_STDERR, "wslop: that is not a window\n");
			return 2;
		}
	}
	if (env->getstat(RV9_STDOUT, RV9_GS_SIZE, &size) < 0) return 2;
	w = (int)(size & 0xFFFFu);
	h = (int)(size >> 16);

	/* Ask how big the panel is in the world. A device with no opinion says
	   UNSUPPORTED and a device that knows the question but not the answer
	   says UNKNOWN; both mean "work in pixels" and both are fine here --
	   the pixel figures are the ones whisker needs, the millimetres are
	   what make them mean the same thing on another panel. */
	phys.kind = RV9_PHYS_UNKNOWN;
	phys.width_px = 0; phys.width_um = 0;
	if (env->getstat(RV9_STDOUT, RV9_GS_PHYSICAL, &phys) < 0)
		phys.kind = RV9_PHYS_UNKNOWN;

	if (env->arg != 0 && env->arg[0] != '\0') {
		uint32_t v = m_num_parse(env->arg, 0);
		if (v >= 3 && v <= MAX_TAPS) taps = (int)v;
	}

	touch = env->open("/touch", RV9_MODE_READ);
	if (touch < 0) {
		m_say(env, RV9_STDERR, "wslop: /touch will not open\n");
		return 3;
	}

	/* Centre, then the four corners inset by a mark's width: the corners
	   are where a capacitive grid interpolates with least to work with. */
	tx[0] = w / 2;          ty[0] = h / 2;
	tx[1] = MARK * 2;       ty[1] = MARK * 2;
	tx[2] = w - MARK * 2;   ty[2] = MARK * 2;
	tx[3] = MARK * 2;       ty[3] = h - MARK * 2;
	tx[4] = w - MARK * 2;   ty[4] = h - MARK * 2;

	out.env = env;
	out.path = RV9_STDOUT;

	m_say(env, RV9_STDERR, "wslop: tap the CENTRE of each mark, ");
	m_num(env, RV9_STDERR, taps);
	m_say(env, RV9_STDERR, " times. Aim; do not hurry.\n");

	for (t = 0; t < TARGETS; t++) {
		int sx = 0, sy = 0, mx, my, worst = 0, worst_drift = 0, rms = 0;

		n = 0;
		while (n < taps) {
			/* Draw the mark where this target is, and a count above it. */
			{
				int k;
				for (k = 0; k < (int)sizeof mark[0].id; k++) mark[0].id[k] = '\0';
				for (k = 0; k < (int)sizeof mark[0].text; k++) mark[0].text[k] = '\0';
				for (k = 0; k < (int)sizeof head[0].id; k++) head[0].id[k] = '\0';
				for (k = 0; k < (int)sizeof head[0].text; k++) head[0].text[k] = '\0';
			}
			mark[0].kind = WSK_BUTTON;
			mark[0].size = 0; mark[0].weight = 1;
			mark[0].fill = 0xf0d070u; mark[0].ink = WSK_INHERIT; mark[0].mark = 0;
			mark[0].id[0] = 'm';

			head[0].kind = WSK_LABEL;
			head[0].size = 0; head[0].weight = 1;
			head[0].fill = WSK_INHERIT; head[0].ink = WSK_INHERIT; head[0].mark = 0;
			head[0].id[0] = 'h';
			head[0].text[0] = (char)('1' + t);
			head[0].text[1] = '/';
			head[0].text[2] = (char)('0' + TARGETS);
			head[0].text[3] = ' ';
			head[0].text[4] = (char)('0' + n / 10);
			head[0].text[5] = (char)('0' + n % 10);
			head[0].text[6] = '/';
			head[0].text[7] = (char)('0' + taps / 10);
			head[0].text[8] = (char)('0' + taps % 10);
			head[0].text[9] = '\0';

			wsk_init(&part[0], WSK_COL, w, h);
			part[0].pad = 1; part[0].gap = 0; part[0].bg = 0x0b1420u;
			wsk_area(&part[0], tx[t] - MARK / 2, ty[t] - MARK / 2, MARK, MARK);
			part[0].item = mark; part[0].n = 1;

			wsk_init(&part[1], WSK_COL, w, h);
			part[1].pad = 2; part[1].gap = 0; part[1].font = 20;
			part[1].bg = 0x0b1420u; part[1].ink = 0x90a8c0u;
			wsk_area(&part[1], 0, 0, w, 28);
			part[1].item = head; part[1].n = 1;

			wsk_doc_init(&doc, &part[0]);
			{
				uint32_t v = 0;
				env->setstat(RV9_STDOUT, RV9_SVG_SS_ROWS, &v);
			}
			s.put = to_path; s.ctx = &out; s.len = 0;
			wsk_open(&doc, &s);
			wsk_part(&doc, &part[0], &s);
			wsk_part(&doc, &part[1], &s);
			wsk_shut(&s);

			/* Collect one tap: a DOWN, then the UP that ends it. */
			for (;;) {
				rv9_touch_event_t ev;

				if (env->signals_take() & RV9_SIG_STOP) goto done;
				if (env->read(touch, &ev, sizeof ev) < (int)sizeof ev) continue;

				if (ev.kind == RV9_TOUCH_DOWN) {
					down_x = (int)ev.x; down_y = (int)ev.y; have_down = 1;
				} else if (ev.kind == RV9_TOUCH_UP && have_down) {
					int ddx = (int)ev.x - down_x, ddy = (int)ev.y - down_y;
					int d;

					if (ddx < 0) ddx = -ddx;
					if (ddy < 0) ddy = -ddy;
					d = (int)isqrt((uint32_t)(ddx * ddx + ddy * ddy));

					dx[n] = down_x - tx[t];
					dy[n] = down_y - ty[t];
					drift[n] = d;
					sx += dx[n]; sy += dy[n];
					have_down = 0;
					n++;
					break;
				}
			}
		}

		/* Bias is the mean; scatter is the spread about it. */
		mx = sx / n; my = sy / n;
		for (i = 0; i < n; i++) {
			int ex = dx[i] - mx, ey = dy[i] - my;
			int d = (int)isqrt((uint32_t)(ex * ex + ey * ey));
			if (d > worst) worst = d;
			rms += d * d;
			if (drift[i] > worst_drift) worst_drift = drift[i];
		}
		rms = (int)isqrt((uint32_t)(rms / n));

		say_num(env, "\nmark ", t + 1, " at ");
		say_num(env, 0, tx[t], ",");
		say_num(env, 0, ty[t], "\n");
		m_say(env, RV9_STDERR, "  bias    ");
		m_num(env, RV9_STDERR, mx);
		m_say(env, RV9_STDERR, ",");
		m_num(env, RV9_STDERR, my);
		m_say(env, RV9_STDERR, " px -- a constant offset is the driver's, not whisker's\n");
		say_px_mm(env, &phys, "  scatter rms   ", rms);
		say_px_mm(env, &phys, "  scatter worst ", worst);
		say_px_mm(env, &phys, "  drift down-up ", worst_drift);
	}

done:
	env->close(touch);
	m_say(env, RV9_STDERR,
	      "\nthe worst SCATTER is the radius a hit test must forgive, and two\n"
	      "widgets must be further apart than that or a confident tap lands on\n"
	      "the neighbour. BIAS is a different fault and belongs further down.\n");
	return 0;
}
