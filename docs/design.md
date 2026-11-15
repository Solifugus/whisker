# whisker — design

**2026-11-11: the charter changed (D16).** whisker is to be a complete GUI
system, and everything in §1–§7 is its bottom layer, drawing. §8 is the
toolkit above it, proposed and not built. Read §8 with D16 before taking
anything earlier as a statement of what whisker is *for*.

**Status:** **Both functions are built** (`src/whisker.h`, 2026-09-30).
Iteration 1 is done. All eight
done-criteria in §5 pass — the suite, the XML check, the pixels through RV-9's
own rasteriser, the module build with its position-independence check, and the
last one, which is a person looking at the pictures: Matthew, 2026-09-30, on
the column, the pressed button and the row.

**It has run on the board, 2026-10-02.** `wflick` and `wcompose` are committed
in RV-9's tree and both draw what they claim; the clipped repaint of one stack
leaves the other untouched and the band comes back carrying the scene rather
than the device colour. The comparison that was the point of `wflick` — it
against hand-written `flick`, same panel, same session — is in
`docs/rv9-answers-2026-10-02.md` §2: a full draw of 79 ms against 57, a press
of 32 against 19, and two thirds of that difference is geometry rather than
whisker, because the tree gives the button 64 rows where `flick` chose 48 by
hand. The run also produced the one finding in this document that whisker
could not have reached on the host: §5's note on what a sink costs in stack.

**Iteration 2's `hit` is built too** (§5b), and for the reason §5b gives: it
needs no panel either, and board time in October is the scarce thing. Its six
criteria pass, including the one the others are worthless without — a point in
a gap is nothing at all, asserted at the pixels as well as in the return value.
What is left of iteration 2 is not whisker's: **an RV-9 touch driver, which
does not exist**, and then the measurement nobody has taken. **All six decisions in §4 are ratified**
(Matthew, 2026-09-29); `docs/decisions.md` is the ledger. `hit` is iteration 2,
when the panel arrives.

Revised 2026-09-29 against `docs/rv9-answers.md`, the RV-9 session's reply to
`docs/rv9-review.md`. Thirteen of sixteen claims stood; the corrections are
folded in here rather than left in the answers for a reader to reconcile.

Where this document says **measured**, it means somebody ran it and the source is
named. Where it says **propose** or **argue**, it is reasoning and may be wrong.

---

## 1. The thesis

A window you write SVG to needs no graphics API. RV-9 established that: `/w0` is
a path, a picture is a stream of bytes, and `cat picture.svg > /w0` puts it on
the screen. What such a window cannot do is tell you **which thing the user
touched**, because it holds a document, not a set of objects.

So the missing layer is small and it is two pure functions. Both are now
settled — the second by hardware (§2.6, D0):

```
render(tree)        -> SVG text
hit(tree, x, y)     -> the id of the widget at that point, or nothing
```

Everything stateful — the framebuffer, the touch controller, the event loop —
stays with the host. whisker never allocates a pixel and never reads a device.

That shape is chosen for a reason: **both functions are testable with no
hardware, no display and no toolkit.** A tree in, text out; a point in, a name
out. The host harness already exists on the RV-9 side and renders to PPM.

---

## 2. The ground this stands on

### 2.1 What RV-9 already has (measured)

Source: `~/development/esp32/rv9`, read 2026-09-29.

| Component | Size | Note |
|---|---|---|
| `components/rv9_io/src/drv_svgwin.c` | 1,335 lines | the `/w0` device; SVG in, picture out |
| `components/rv9_io/src/raster.c` | 306 lines | banded rasteriser, **zero ESP dependencies** |
| `components/rv9_io/src/font.c` / `font.h` | — | text drawing |
| `tools/hosttest/` | `render.c` 62, `path_test.c` 180, `scale.c` 242 | builds the renderer **natively**, asserts, writes PPM |

Two design decisions in that driver matter here, both quoted from its own header:

- *"The document is held as text and re-read once per band... So the SVG source
  **is** the display list: there is no compiled form to build, size, or keep in
  step with the source it came from."*
- *"SVG, and deliberately not CSS: presentation attributes only... A style
  language would mean a cascade, a selector engine and a box model, which is an
  enormous amount of machinery to arrive back at 'this shape is red'."*

**whisker adopts both.** They are not RV-9 quirks to work around; the second is
the reason this project is small enough to finish, and the first is why there is
no retained tree to keep in sync.

**And the third thing it already has, which the renderer cannot be written
without: text metrics that are exactly computable.** One fixed-width face, no
families, and every number a layout needs is a constant in `font.h`
(`review-2026-09-30.md` §3.5, measured against rendered ink):

| | |
|---|---|
| advance per character | `font_size * RV9_GLYPH_W / RV9_GLYPH_H` — that is `font_size / 2` |
| cell top, from the baseline `y` | `y - font_size * RV9_FONT_BASELINE / RV9_GLYPH_H` — `15/20` |
| cell height | `font_size` |
| glyphs that exist | 32..126; anything else advances a cell and draws nothing |
| leading and trailing space | **stripped before measuring or anchoring**, so `"  OK  "` and `"OK"` are the same width |

So whisker can size a label, centre it and truncate it to fit in 32-bit integer
arithmetic, with no font API and no device. And `text-anchor="middle"` is
honoured against that same advance box, so centring a button label costs **one
attribute** rather than a computed `x` — which is also the cheaper thing to
emit. RV-9's `reference.md` §9 is the citable form.

### 2.2 The performance already measured on the board

From RV-9's `docs/roadmap.md`, *"Open — damage tracking, before the GUI direction"*,
measured with `tools/hosttest/scale.c` and on the C5 at 320×172:

- full redraw **56 ms**; press **19 ms**; release **19 ms** — so **acknowledging**
  a tap costs **38 ms of work** against 56 for one full frame, with band
  clipping (`RV9_SVG_SS_ROWS`) — but the **latency** a finger waits for is
  **19 ms**, the press alone. The release repaint happens after the finger has
  lifted. **19 ms is the perceived-response budget; 38 ms is the cost budget**,
  and spending 38 against a responsiveness target is twice as pessimistic as
  the hardware deserves. At 1024×600: ~33 ms perceived, ~66 ms of work.

  **These were 59 / 15 / 15 and a 30 ms tap until 2026-09-29**, and the
  correction is worth more than the number. The old figures were measured on a
  clipped repaint that was **visually wrong**: `flick`'s pressed and normal
  documents held only the button, and a band is cleared to the *device*
  background before anything is drawn into it, so the rows either side came back
  black and the scene's background was gone. Two black strips, on every run, for
  as long as `flick` had existed. Fixed in RV-9; see `rv9-answers.md` §1.3 and
  §2.3 of this document's review.

  **And whisker's own panel costs more than the hand-written one, 2026-10-02.**
  `wflick` draws `flick`'s three draws from a widget tree, and on the same
  panel in the same session: full **79 ms** against 57, press **32 ms** against
  19, release **28 ms** against 19. That press is **over the 19 ms perceived
  budget**, so it is worth saying exactly where it goes. RV-9's session
  measured two thirds of it as *geometry rather than whisker*: the tree gives
  the button **64 rows** where `flick` chose 48 by hand, and per band the two
  are 4.1 ms against 3.2 ms. The remaining third is whisker's — about 1 ms a
  band on a document that is longer because it carries ids and a backing.

  The tension is real and it is not a defect to fix in the emitter: 64 rows is
  what `tools/evidence/finger_size/` measured as a comfortable target and §5
  defaults to deliberately. **A comfortable finger target costs bands**, and
  that is the trade, stated here so it is not rediscovered as a performance
  bug. A caller that wants `flick`'s latency can ask for `flick`'s button
  height; what whisker must not do is make 48 the default because it measures
  better.

  **And of that, almost none is whisker.** Measured 2026-10-04
  (`tools/evidence/profiles/`… `tools/evidence/tap/`): of the work one press
  causes, `hit` and `rows` are immeasurably small, whisker's emitter is **0.7 %**
  at 320×172 and **0.2 %** at 1024×600, and the window's parse-and-rasterise is
  the other **99 %**. The absolute microseconds are a development machine's and
  are not quoted; the share is what transfers, since both halves are integer
  code doing the same work either side.

  **So whisker's cost is the document it writes, not the code that writes it.**
  Terseness is a frame-budget requirement because *parsing* is paid once per
  band — not because emitting is slow. This also settles the `wflick`/`flick`
  gap above without another board run: 32 ms over 8 bands is 4.0 ms a band
  against `flick`'s 3.2 over 6, and the per-band difference is 342 bytes
  re-parsed against 146. None of it is whisker executing.

  **Read `modules/flick/flick.c` before quoting this.** It times *three draws*,
  not an input round trip: the whole window, then the button's rows in a pressed
  colour, then the button's rows back to normal. Its own header says *"2 and 3
  together are what a person experiences as one tap."* **No tap has ever been
  measured end to end** — the touch panel arrives in October (§2.6) — so what is
  known is what it costs to *answer* one, which is the number that was in doubt.
- ~~widening to 1024×600 costs only **1.7×**, so a tap is about **66 ms** against
  ~640 ms for a full screen~~ — **both measured and both wrong, 2026-10-11.**
  `flick > /w0` on the real 1024×600 panel: a **full screen is 175 ms** and a
  **tap is 5 ms** (3 press + 2 release). The old pair was never measured; it was
  extrapolated from host band timings, scaled for width, then for board, then
  implicitly for clock, and the pessimism compounded at each step. The full
  screen is 3.7× better than the figure this document carried and the tap is an
  **order of magnitude** better. RV-9 found it, and also found `module.h`
  quoting the same pair with the word *"measured"* beside it.

  **What survives, and harder than before:** 5 ms against 175 is a factor of
  **thirty-five**, so repainting only what changed is not an optimisation on
  this board — it is the difference between interactive and not.
- 1024×600 costs **24,576 bytes** of buffers and 2.9× the time of 320×172 for
  11.2× the pixels. *Resolution is not the problem.*
- the C5 is about **96×** slower than the host on `pic.svg` (82 ms vs 0.85 ms).

RV-9's own conclusion, which whisker takes as its budget: **"tap-to-act with a
flash to confirm is affordable today."** A retained object tree with per-object
bounding boxes is an optimisation for later, not a gate.

**The consequence for whisker's design, corrected after reading the driver.**
An earlier draft of this document said `render` must emit *"a fragment for one
widget"*. That is wrong, and the real mechanism is both simpler and more
constraining.

`RV9_SVG_SS_ROWS` is a **setstat carrying a row range**, stored as
`clip_y0, clip_y1` — *"rows the next document should repaint"*. You then write a
**complete SVG document**, and only the bands covering that range are rasterised
and pushed. There is no partial XML anywhere. `flick` does exactly this: its
`PRESSED` and `NORMAL` strings are whole `<svg>` documents with the same
`viewBox`, containing **only the button** — because nothing outside the clipped
rows would be drawn from them anyway.

So what whisker needs is not a fragment but:

```
render(tree)             -> a complete SVG document of everything
render(tree, ids, n)     -> a complete SVG document, same viewBox, containing
                            only what intersects those widgets' rows
rows(tree, ids, n)       -> the device row range to hand RV9_SVG_SS_ROWS
```

A **set** of ids rather than one, because a real interface changes more than one
widget at a time and the single form cannot be retrofitted once callers have
written loops around it. See §5.

**And the sharp part: clipping is vertical only.** There is no horizontal clip,
so repainting a widget repaints **the full width of its rows**, and the band is
first cleared to the *device* background (`rv9_raster_clear(&b, s->bg)`, the
`OPT_BG` descriptor option) — not to whatever the scene had drawn there. Anything
sharing rows with the widget, and any background the scene painted, must be in
that second document or it disappears.

**Confirmed, and it was a live bug rather than a hazard** — `rv9-answers.md`
§1.3. The rule is now on `RV9_SVG_SS_ROWS` in RV-9's `module.h`: *a clipped
document must carry every pixel of its rows.* And a refinement that matters
because it does not grow with screen height — **the backing covers the clipped
rows, not the screen.** A full-width rect over the widget's rows is 59 bytes; a
full-screen one is bytes re-parsed once per band for pixels outside the clip.

**Vertical-only is a software choice, not a hardware limit.**
`rv9_panel_blit` already takes `x0` and `x1`; what makes clipping vertical is
that the *rasteriser* works in full-width bands. RV-9's roadmap names horizontal
clipping as a future gain. So `rows(tree, ids)` should be able to become
`bounds(tree, ids)` without the widget model changing.

**Two further facts from the driver, either of which is a silent bug:**

- **The row range is in *device* rows, not viewBox units.** The driver compares
  the clip against the panel height while the document is in whatever `viewBox`
  it declares. Get the device size from `getstat(RV9_GS_SIZE)` on `/w0`, which
  returns **pixels, not characters**. A whisker that assumes viewBox == device
  works at 320×172 and clips the wrong strip at 1024×600. But note what that
  getstat *cannot* answer: **"is this a window"**. It is the generic
  how-big-is-this code and RBF answers it with a file's length, so
  `wflick > /r0/x` laid itself out on a 0×0 screen and wrote nonsense — found
  by RV-9's session on 2026-10-02, and present in all three of whisker's module
  examples because theirs were taken from them. The probe is
  `setstat(RV9_SVG_SS_ROWS)` with 0,0, which only `svgwin` answers and which
  means "all of it", so asking changes nothing.
- **The clip is snapped outward to 8-row band boundaries**, and a reversed range
  is refused with `RV9_IO_ERR_INVAL` rather than clamped. So `rows` need not be
  band-aligned, but a widget straddling a boundary repaints that whole band —
  **aligning widget heights to multiples of 8 is free and saves a band each.**
- **The clip is one-shot.** `render()` sets `clip_y0 = clip_y1 = 0` after every
  document, so the protocol is setstat-then-write, *every time*. A caller that
  sets the range once and writes twice gets a full redraw the second time.

**How much a row actually costs — measured, and less than this document
assumed.** Three buttons as a column and as a row, each repainting one button
correctly (carrying its rows' backing), 300 repeats on the host:

| | 320×172 | 1024×600 |
|---|---|---|
| full frame | 0.343 ms | 2.750 ms |
| repaint one of a **column** | 0.073 ms (21 %) | 0.676 ms (25 %) |
| repaint one of a **row** | 0.129 ms (38 %) | 1.047 ms (38 %) |
| **row ÷ column** | **1.77×** | **1.55×** |

**A row costs about 1.7× a column and both are a fraction of a frame** — roughly
7 ms against 12 ms on the C5 at 320×172, against a 38 ms budget. The price of a
row is real and is paid in *document bytes*, since siblings sharing rows must all
appear in the repaint; it is not paid in bands. §4.3 is settled accordingly:
rows are allowed.

**A second cost, from the same source:** *"Every band still re-parses the whole
document, so the saving is in bands not in parsing."* Parse cost therefore scales
with document **size** even when repainting 48 rows. Terse emission is not only
about the 4 KB ceiling in §2.3 — it is paid again on every band of every
repaint.

### 2.3 The ceilings that do not move with screen size

`SRC_MAX` 4,096 · `MAX_PTS` 256 · `MAX_CONTOURS` 64 · `MAX_DEPTH` 8 —
and since 2026-10-02 these are **askable** rather than copied, through
`getstat(RV9_SVG_GS_LIMITS)`. Ask, because two of the four have already
moved under whisker's feet once.

**4 KB of SVG is the real constraint on how much interface fits on a screen**, and
it is whisker's to respect. A widget set that emits verbose SVG will hit this
long before it hits the frame budget. Terse output is a design requirement, not
an optimisation.

**But respect it for the right reason.** `SRC_MAX` is one `rv9_alloc` in
`svgwin_open` and is raisable in a line (`rv9-answers.md` §2.2). The cost that
does not go away is **time, and it is paid once per band**: measured on the host
at 320×172, a full frame is about **0.34 ms + 0.00036 ms per byte**, so a
document that fills `SRC_MAX` costs roughly **2.5× a terse 1 KB one** — at any
clip, because every band re-parses the whole document. Terseness is a *frame
budget* requirement, not a memory one.

**And it does not scale with the screen, which inverts the usual assumption.**
1024×600 is 11.2× the pixels of 320×172 and the **same 4 KB of description**.

**This section used to go on to predict that `SRC_MAX` would therefore be "the
binding constraint on the P4". Measured 2026-10-03, with whisker's own
emitter, that is wrong** (`tools/evidence/p4_panel/`). It needed no board: how
many bytes a tree emits is a property of the tree and the emitter, and both
were already here. The earlier reasoning was done before the emitter existed
and against hand-written SVG.

| a realistic panel at 1024×600 | bytes of 4,096 |
|---|---|
| a readout of 20 values, two columns | 1,026 |
| a settings form — 6 labelled fields and two buttons | 1,286 |
| a launcher — 15 buttons in a 3×5 grid | 1,728 |
| a text-entry screen — a field and 36 keys | 3,541 |

And a **whole screen filled** with a single column of buttons never binds
either — 8 at finger size is 976 bytes, and even 24 crammed in at 16 rows each
is 2,662. **The screen runs out before the budget does.**

**What binds is density, not size.** A column cannot put more than a couple of
dozen controls on a screen; a grid can, and that is where the ceiling is:

| grid | controls | bytes |
|---|---|---|
| 5×5 | 25 | 2,707 |
| 6×6 | 36 | 3,803 |
| 6×8 | 48 | **5,025 — over** |

**So the ceiling is about forty controls visible at once, and only a grid gets
you there.** Which means the one realistic panel that approaches `SRC_MAX` is a
**keyboard** — and that is not a coincidence, it is the same fact from the
other side: a keyboard is the densest grid an interface has.

Treat it as a **device property to ask about, not a constant**: it will differ
between the two boards. There is a getstat for it now — RV-9 added
`RV9_SVG_GS_LIMITS` on 2026-10-02, reporting all four ceilings into an
`rv9_svg_limits_t`, after this document observed that none of them appeared in
any RV-9 document at all:

```c
rv9_svg_limits_t l;
env->getstat(w0, RV9_SVG_GS_LIMITS, &l);   /* src_max, pts_max,
                                              contours_max, depth_max */
```

**And going over it is not truncation — it is a lost picture, and it used to be
a lost window.** Until 2026-09-30 a document past `SRC_MAX` wedged `/w0` for the
life of the open path, silently: the bytes were dropped, so the `</svg>` that
ends a document could never land in the buffer again (`review-2026-09-30.md`
§3.2). RV-9 now drops the document and logs it, *"because half a picture is a
silent wrong answer where an unchanged screen plus a log line is a legible
one"*.

**So `render` needs a byte budget and a refusal, not an assertion in a test that
a realistic panel happens to fit.** A caller that adds a widget at runtime has to
be told **before** the write, in a way it can act on. And whisker must be correct
against an RV-9 older than that date too, because a whisker that only behaves on
a fixed one is a whisker that wedges somebody's board.

**`MAX_CONTOURS` used to fail quietly, and finding that out is what moved it.**
Measured 2026-10-02 (`tools/evidence/keyboard/`), when the cap was 16: a
`<path>` with more subpaths than that was **silently truncated**. No log line,
no dropped document, no error — the rest of the document drew normally around
the hole. The picture was a 36-key keyboard with twenty invisible keys and all
thirty-six labels, and the driver's own log line for it was `drew 1967 bytes in
3 ms`, a success. That is the worst shape a failure can take, and worse than
the `SRC_MAX` wedge it resembles in one specific way: it is **localised**, so
it reads as a bug in whichever widget happens to be missing.

RV-9's session **logged it and raised it** the same day, on the ground that
those answer different questions — the log is for the person whose picture came
out wrong, the cap is for the picture:

```
W rv9-svgwin: path truncated: 8 subpath(s) past 64, 0 point(s) past 256
              -- the rest of the document drew
```

**The cap is 64 now, and the way it got there is the part worth keeping.** The
contour buffer was a *local* in `do_path`, so the obvious way to raise it would
have raised the stack requirement of every program that writes to `/w0` —
whisker's included, and §5's note on the sink's stack cost explains why that
is a place whisker can least afford it. It sits in the device state instead, costing 512 bytes of the
window's own heap once. At four points a subpath `MAX_PTS` binds next, at 64
quads exactly.

So a 36-key keyboard as one path now fits, with 28 subpaths to spare, and
`tools/evidence/keyboard/` form C went from *broken* to the **cheapest of the
three** — 1,879 bytes against B's 1,960 and A's 3,361, drawing pixels identical
to A. But the rule this episode argues for is unchanged and is not "64 is
enough": **ask, and carry the answer**. A limit a program hopes to stay under
is not a limit, and whisker has now watched this number move once.
### 2.4 What RV-9's window does *not* do

Recorded as not done, in its own roadmap: font families and weights, rotated
text, arc x-axis-rotation, opacity, gradients, documents over 4 KB.

whisker must not require any of them. That rules out a good deal of ordinary
visual design — shadows, gradient fills, translucent overlays — and the honest
framing is that **whisker's look is flat because its substrate is**, not because
flat is fashionable.

**Nor does it decode entities, and that one reaches into the API.** `&lt;` draws
as the four characters `&`, `l`, `t`, `;` on the panel — measured. So text
**cannot be escaped**, only kept clean; and until 2026-09-30 a raw `<` in text
content hung the driver outright, inside `write`, holding the lock, on one core
(`review-2026-09-30.md` §3.3).

So **a label is restricted at whisker's boundary, not at its emitter**: ASCII
32..126, minus `<`, `&` and `>`. Bytes outside that range are the quiet version
of the same problem — they advance a cell and draw nothing. The refusal belongs
where the data arrives, because that is the only place that can say *which*
label was wrong. Note also that a document which **parses** as XML and one which
**draws** correctly are different documents the moment a `&` is in a label: the
escaped form is well-formed and wrong on the glass, the raw form is right on the
glass and not well-formed. Keeping such a label out is the only answer that is
both.

There is no `clipPath` either, so **text is not clipped to its widget**: an
overlong label runs out of its button and over its neighbour. whisker measures
and truncates, which §2.1 makes exact.

**What a flat substrate leaves for showing a state.** No shadow, no gradient,
no opacity — so a pressed button changes colour, and that is one signal doing
all the work. The panel is RGB565 and may be read at an angle, in sunlight, by
somebody who is not looking directly at it. So iteration 1 gives a widget a
second, geometric signal: **ticks driven inward from its four corners**, in the
label's ink, as one `<path>` of four two-point subpaths. **95 bytes**, drawn
only while the state is on — about 1.3 ms of C5 time against a 19 ms latency
budget. Shape and colour, rather than colour alone.

**Confirmed on the glass, 2026-09-30.** This was the one question a rendered
PPM could not answer, and Matthew watched `wflick` run on the board: the
pressed button reads well, ticks included. Worth keeping the arithmetic beside
the verdict, because it is what says the mark will survive the move to the P4:

| | C5 (1.47″, 247 PPI) | P4 (7″, 170 PPI) |
|---|---|---|
| tick length, 16 px | 1.64 mm — 9.4′ at arm's length | 2.40 mm — 13.7′ |
| tick stroke, 2 px | 0.21 mm — **1.2′** | 0.30 mm — 1.7′ |
| button height, 64 px | 6.6 mm | 9.6 mm |

20/20 acuity resolves about 1 arcmin, so 2 px is close to the floor on the C5
and reads anyway — which means it has margin on the bigger, coarser panel
rather than less. The 3 px stroke that was going to be suggested is not
needed.

The same table carries a separate warning that has nothing to do with marks:
a 64-row button is **6.6 mm** on the C5 against a ~10 mm fingertip. That panel
is not a touch target whatever is drawn on it. On the P4 the same 64 rows are
**9.6 mm**, right at the usual minimum — so widget heights in pixels want
checking against millimetres once, per panel, and 64 is the floor rather than
a comfortable choice.
### 2.5 What gBASIC already has (measured)

Source: `~/development/gbasic`, read 2026-09-29.

- **It already emits SVG with no dependencies at all**: `stdlib/chart.bas`,
  1,612 lines, **zero `load` statements**, deterministic output pinned byte-exact
  by `tests/run_chart.sh` and fed back through `xml.parse` for well-formedness.
  `stdlib/gpdf.bas` emits SVG too.
- **Its GTK surface is thin**: across the four `gi`-using libraries (`gtk`,
  `gtkui`, `sourceeditor`, `datagrid`) there are **21 distinct GTK types
  constructed and 70 `gi.*` call sites**. A renderer seam is a small job.
- **It has an unresolved question this answers**: `docs/gui_addressability_design.md`
  §0.3, *"which layer is the address space?"* — the declarative `gui` module has
  globally-unique validated ids but is GTK 3, experimental and unused; `gtkui` is
  alive and GTK 4 but its keys are sibling-scoped and it is GTK-specific by
  construction.
- **No SVG rasteriser is installed**: librsvg absent, no rsvg typelib. Pango is
  present (6 typelibs), so text metrics are reachable on a Linux host if needed.

**Superseded 2026-10-22: gBASIC removed GTK and is Qt-only** (gBASIC
`19a830f`; the last GTK commit is `d997c94`). Every GTK fact above, the `gi`
bridge, and both `gui` and `gtkui` are gone, and so is the addressability
question as it was put. What replaced them is a declarative **`window`**, read
by a backend, with `qt` the first (gBASIC `docs/qt_gui_design.md`). For
whisker that is a better seam than the one this section found: a second backend
is now a second reader of one declaration rather than a rival toolkit. Two
things from that design are worth carrying here, as facts about gBASIC rather
than arguments for whisker: Qt **cannot be statically linked** under LGPL-3, so
a gBASIC GUI program always needs an installed Qt, where gBASIC's releases
otherwise link statically to start on a bare machine; and the `chart.bas`
point above still stands. None of it is re-measured yet.

### 2.6 The input: none today, touch in October

**Revised 2026-09-29.** What follows about the *present* is measured and still
true; what it concluded about the future is superseded by a purchase.

**A Waveshare ESP32-P4-WIFI6-Touch-LCD-7B was ordered 2026-09-27 and arrives
7–20 October: 1024×600, five-point capacitive touch.** So the second function is
`hit(tree, x, y)` (D0, ratified). Five points are available; **whisker uses
one** — there is no gesture layer and whisker should not become one.

The C5 keeps `/gpio` and will probably never have touch, so a focus order must
stay *possible* without being built: give every widget a stable declaration
order and a traversal order exists, unimplemented, for free.

The rest of this section is the state of the C5 and remains accurate.

`components/rv9_io/src/` holds **14** driver files, and the target profile lists
15 drivers in all. **None of them is a touch driver.** (An earlier draft said 29;
the count was wrong, the conclusion was not.)
The panel is an **ST7789**, which is display-only; a touch panel needs a separate
controller (FT6236, CST816, GT911 or similar) and no driver for one exists. A
grep for touch across the whole tree returns only the English word.

Nor is there a local keyboard. RV-9's own design document, §on the terminal: *"a
USB keyboard cannot be attached to this board at any price... the terminal is the
only reason the shell has a keyboard at all."* Keyboard input arrives over UART
or SSH, from somewhere else.

**What does exist is `/gpio`**, where *"a pin opened for reading becomes an
input"*. So physical buttons are available today, and a touch panel is not.

Three consequences, and the third is the one that reaches into §4:

1. **§3's contract is half-real.** The output half *is* RV-9, exactly. The input
   half is **proposed and unbuilt**, and the earlier draft's claim that *"the
   touch controller is another device"* was false.
2. **whisker can still be built and fully tested**, because both its functions
   are pure and the host harness needs no hardware. But it cannot be *used* on
   RV-9 until some input device exists, and nothing here should pretend
   otherwise.
3. ~~**It is not yet known that the second function is `hit(tree, x, y)` at
   all.**~~ **Settled**: it is. See the top of this section and D0.

### 2.7 The P4 is a third surface, and it inverts one of these properties

whisker names two hosts and there are **three surfaces**. The P4 is not a bigger
C5 (`rv9-answers.md` §2.1):

| | C5 | P4 | Linux |
|---|---|---|---|
| panel | ST7789 over SPI, **holds its own framebuffer** | MIPI-DSI, **no framebuffer of its own** — the P4 scans it out of memory | whatever |
| who owns the pixels | the glass | **RV-9**, in 32 MB of stacked PSRAM (1.2 MB at 16bpp) | the toolkit |
| "untouched rows stay" | a property of the hardware | **RV-9's responsibility** | trivial |
| horizontal clip | not today (§2.2) | **straightforwardly possible** — the destination is memory RV-9 addresses | trivial |
| touch | none, `/gpio` only | 1024×600, five-point | every input you could want |

**Write the contract against the C5** — the constrained host must define it, and
that argument is still right. But do not bake *vertical only* into the widget
model, because the machine that will actually run a seven-inch touch interface
does not have that limit. §2.2's `rows` → `bounds` note is the concrete form of
this.

The P4 is also **dual-core** with **no radio of its own** (WiFi 6 arrives from a
companion ESP32-C6 over SDIO). Neither reaches whisker, which is pure — but they
are why the P4 is the *personal-computing* target and the C5 stays the real-time
one.

### 2.8 `/w0` is not the display system, it is *a* display system

Everything above measures RV-9's `drv_svgwin.c` on the C5's ST7789, because
that is the host that sets the budget and the only one that has been on a bench.
It would be easy to read this document as a description of what drawing *is*.
It is not. Counted honestly, whisker already faces more than one display system
and will face more:

| | what draws | what that changes |
|---|---|---|
| **RV-9 `/w0`, C5** | `drv_svgwin.c` → `raster.c` → ST7789 | the budget, and §5's stack note — the writer's own stack renders the document |
| **RV-9 `/w0`, P4** | same driver, MIPI-DSI, pixels in PSRAM | who owns the framebuffer, whether a horizontal clip exists, and every stack figure (§2.7) |
| **RV-9 `lcdcon`** | a text console on the **same panel** | not a whisker target — but it is why `/w0` is not privileged even inside RV-9. `panel.h`: *"shared by every driver that draws on it"* |
| **whisker's Linux host** | SDL2 over RV-9's own `raster.c` | the same pixels, with megabytes of stack and an allocator, so §5's trade is real and irrelevant at once |
| **gBASIC** | Qt, since 2026-10-22 (GTK removed) — a declarative `window` read by a backend, `qt` the first | a toolkit that renders on its own loop, where none of this arithmetic applies. A second backend to the same declaration is foreseeable; the GTK-era seam this row used to describe is gone (§2.5) |

**So the rule for this document, and for anything added to it: name the
substrate a finding came from.** A measurement on `/w0` is evidence about
`/w0`. It is evidence about *whisker* only where it shows whisker making a
choice it did not have to make — and the right response to such a finding is
usually to push the choice back out to the caller rather than to settle it
here. §5's note on what a sink costs in stack is the worked example: the
measurement is RV-9's, the conclusion binds RV-9's modules, and what whisker
takes from it is a reason to keep not deciding.

This is the same argument as §2.7's, one level up. §2.7 says *do not bake the
C5's limits into the model.* §2.8 says *do not bake `/w0` into it either.*

---

## 3. The substrate contract

whisker talks to a host through **two byte channels and nothing else**:

```
out:  a complete SVG document, plus the row range it needs repainted
in:   a point -- x, y, and whether it is a press or a release
```

Note what `out` does *not* say. It is never a fragment of XML and never a
drawing call: always a whole document, and separately a range of rows. That is
RV-9's mechanism exactly (§2.2), and it is the thing an earlier draft of this
document got wrong.

Note what `in` does *not* say: nothing about gestures, velocity, multiple
contacts or a stream. One point at a time, and the host decides what a sequence
of them means. The panel reports five and whisker uses one (§2.6).

**The `out` half is not an abstraction invented for portability — it is what
RV-9 already is.** `/w0` is a path you write to, plus a `RV9_SVG_SS_ROWS`
setstat for the row range (§2.2).

**The `in` half does not exist on the C5 and arrives in October on the P4**
(§2.6). A Linux host would satisfy both halves easily — a window, a rasteriser,
and a loop that reports clicks — which is precisely why the Linux host must not
be the one that defines them. Designing against the comfortable host is how a
layer arrives on the small target needing a mouse.

**Nothing has yet measured a tap end to end**, and whisker cannot fix that. When
the P4 arrives, the first measurement worth taking is finger-down to
pixel-changed: it includes an I²C touch controller, an interrupt, a process
wakeup and a repaint, and 38 ms is only the last of those. Budget for it in
iteration 2 rather than assuming the repaint figure is the whole story.

**Argued, not measured:** because the contract is bytes rather than a function
table, a host may sit at the far end of a pipe, a socket or a serial line. A
gBASIC program on a workstation driving a panel on an RV-9 board is the same
contract with a longer wire. That is worth *not* designing against, but it costs
nothing to leave possible.

---

## 4. Six decisions — all ratified 2026-09-29

These were named rather than assumed, because the gBASIC document this grew out
of was criticised for exactly that — it designed three chapters on top of a layer
choice it never asked about.

**All six are now ratified by Matthew**; `docs/decisions.md` is the ledger and is
the file to trust. The reasoning is kept below because a decision whose argument
is lost gets reopened, and two of the six were *changed* by the RV-9 review
rather than merely confirmed.

### 4.0 What is the input, and therefore what is the second function?

> **Ratified: `hit(tree, x, y)`.** Matthew, 2026-09-29, on the RV-9 session's
> answer — a 1024×600 five-point touch panel was bought on the 27th and arrives
> in October (§2.6). Not a judgement call in the end: the hardware settled it.

`hit(tree, x, y)` assumes **pointing**. The C5 has no pointing device (§2.6), and
what it does have is GPIO pins that can be read. So there were three futures and
they wanted different libraries:

| Input | Second function | Shape of the widget model |
|---|---|---|
| **A touch panel** (driver unwritten) | `hit(tree, x, y)` | pointing; no focus needed; any layout works |
| **GPIO buttons / a rotary encoder** | `focus_next(tree, from)` and `activate(tree, focused)` | **focus traversal**; a visible focus ring is mandatory; order matters more than position |
| **Both, or a remote terminal driving it** | both | the union, and a rule for which wins |

**This is not a detail to settle in iteration 2.** If the input is buttons, then
hit-testing is dead weight and the missing thing is a traversal order and a focus
indicator — a different library with a different test suite. Building the pointing
version first and discovering the hardware is a rotary encoder would mean
throwing away the half that took the work.

**Only the RV-9 side could answer it**, because it was a hardware question, and
the answer is the first row. The third row stays *possible* rather than built:
declaration order is a traversal order, so a focus model can be added to the C5
later without reshaping the widget tree.

**Iteration 1 is still the renderer alone** (§5) — not because the input is
unknown any more, but because `render` is the half that can be finished and
tested before a touch panel is on the desk.

### 4.1 What language?

| | For | Against |
|---|---|---|
| **C** | matches RV-9 exactly, builds as an ESP-IDF component, links into gBASIC as a native module; `raster.c` proves ESP-free C is achievable here | gBASIC gains a `HAVE_*` guard and a dependency |
| **Zig** | the house preference for systems work; exports a C ABI | adds a toolchain to an ESP32-C5 RISC-V build that currently needs none |
| **gBASIC** | zero toolchain, and `chart.bas` proves gBASIC writes SVG well | **cannot run on RV-9** — a tree-walking C interpreter does not fit a quarter megabyte, so the harder host would be unserved |

> **Ratified: C.** Matthew, 2026-09-29, departing from the house convention's
> preference for Zig with that departure made explicitly rather than by default.

RV-9 is the binding constraint and C is what it speaks. Zig's advantages are real
and are about *authoring*; the cost lands on the smaller target's build, which is
the one that can least afford it. Concretely, RV-9 modules are built by
`tools/build_modules.sh` with a specific riscv32 gcc, a linker script that
**asserts `.data` and `.bss` are empty**, and a build step that then **proves**
position independence by linking every module twice at different base addresses
and comparing the bytes. Teaching Zig to satisfy all of that is achievable and is
a project of its own, before whisker's first line.

#### The module rules are a constraint on the code, not just on the build

**This is the thing most likely to cost an iteration, and the review never asked
about it** (`rv9-answers.md` §2.5). A program on RV-9 *is* a module, so anything
a module links inherits every rule a module obeys:

- no `.data`, no `.bss` — the link **fails**, which is the good case
- no libc: no `memcpy`, no `malloc`, no `printf`
- **no 64-bit division** — `uint64_t / n` calls `__udivdi3` in libgcc, not linked
- **no `static const` array of pointers**, and no `switch` over string literals:
  both compile to a table of absolute addresses and break position
  independence. **Plain `static const` *data* is fine** — a byte table, a
  string blob with integer offsets, a struct array of inline `char[N]`. It is
  addresses in rodata that break this, not constants. (True since 2026-09-30:
  modules used to link at base 0, where the linker relaxed the PC-relative
  pair into an absolute load and *any* named `static const` object failed. See
  `rv9-answers.md` §7.)
- no local array initialised from a string literal — `char pre[] = "btn";`
  becomes a `memcpy` from rodata

**A widget dispatch table is the natural design for this library and is exactly
what that build refuses.** So is `static const char *kinds[] = {...}`. But a
`static const w_t TREE[] = {...}` with inline `char[N]` labels, or with
integer offsets into a string blob, **does** link — both measured. A widget
tree may be static data; it may not be a table of pointers. RV-9 hit
this twice; the second time a working module needed rewriting, and `have` now
walks a space-separated string with a helper instead of indexing an array of
`const char *` — the array version was correct C, read better, and was not
position independent.

**Build shape: header-only C, like RV-9's `modlib.h`.** Each module links its own
copy, there is no module ABI to satisfy, it compiles unchanged in
`tools/hosttest`, and it links into gBASIC as a native module. `RV9_MOD_LIBRARY`
exists as a module *type* but nothing links against libraries yet, so a
shared-library whisker would be building that mechanism first. If whisker later
wants to be a **component** — part of the firmware — RV-9 would want it behind a
path rather than a linked API, which is a different conversation.

### 4.2 One implementation, or a specification and two?

The alternative to a shared C core is a written specification plus a corpus of
`tree → expected SVG` and `point → expected id` fixtures, implemented twice —
once in C for RV-9, once in gBASIC for gBASIC.

It is genuinely tempting, because a gBASIC implementation would need no native
module, no build integration and no guard, and `chart.bas` shows it would be
pleasant to write.

> **Ratified: one implementation.** Matthew, 2026-09-29.

Two representations of one declaration drift; gBASIC's own test suite has tripwires against exactly this shape in four
places. A shared corpus makes the drift *visible* but does not stop it, and the
failure would be a widget that is hit-tested in one place and drawn in another.

### 4.3 How much layout, given that a box model is refused?

RV-9 refused CSS partly to avoid a box model, and a widget set needs *some* way
to say "these three buttons, in a row, filling the width."

> **Ratified: a single-axis stack, row *or* column.** Matthew, 2026-09-29, on the
> RV-9 session's measurement. This is the one recommendation the review
> **changed**: the original said column-preferred and priced a row as something
> to avoid.

**The smallest thing that is not nothing:** a single-axis stack (row or column)
with a per-child weight, and padding as one number. No grid, no flex-wrap, no
min/max, no baseline alignment. That is enough for a control panel and not enough
for a document, which matches what the substrate is for.

**This is the decision most likely to be got wrong by being generous.** Every
layout feature is paid for twice — once in the 4 KB output ceiling, once in the
38 ms repaint budget.

**An earlier draft of this section over-priced the row, and §2.2 now has the
measurement.** A row costs **1.77× a column at 320×172 and 1.55× at 1024×600**,
both a fraction of a frame — roughly 7 ms against 12 ms on the C5, against a
38 ms budget. The price is paid in *document bytes*, because siblings sharing
rows must all appear in the repaint, and not in bands.

So: **allow a row. Allow a 2×2 grid when it comes** — it is what a seven-inch
touch panel will want, and it is affordable. What to keep refusing is **deep
nesting**, where every cell shares rows with its whole row and columns with its
whole column and the byte cost compounds. Prefer a column where it is the natural
shape, not where it has to be contorted into one.

### 4.4 Is the address space the SVG `id`?

> **Ratified: yes.** Matthew, 2026-09-29.

This is the decision with a consequence outside whisker.
An SVG `id` is document-unique by the XML specification. So is a declarative
`gui` widget id in gBASIC — globally unique per window, validated, flat
(`win.cancel` resolves, `win.row.cancel` does not; measured 2026-09-29).

They are **the same rule**. Adopting SVG `id` as whisker's address space means
gBASIC's §0.3 tension resolves rather than being traded off: the identity rules
it should pick are the ones its eventual backend already enforces, and `#id` /
XPath is a path grammar that exists and is standardised.

### 4.5 Who retains the tree?

RV-9 deliberately retains **nothing** — the SVG text is the display list. But
`hit(tree, x, y)` needs a tree, and something has to hold it between a draw and a
tap.

> **Ratified: the caller holds it; whisker is pure.** Matthew, 2026-09-29.

The host already holds the SVG
text; the application already holds its own state. A retained tree inside whisker
would be a third copy that can disagree with both. The cost is that the caller
passes the tree to every call, which on RV-9 is a pointer and in gBASIC is a
record — and gBASIC records are values, so this is the grain of both hosts rather
than a compromise.

---

## 5. Iteration 1 — the renderer alone

Per house methodology: the first iteration is a working product, not scaffolding.
`render` is the half that can be finished and tested before a touch panel is on
the desk, and it is unaffected by anything `hit` will need.

**Scope:** labels and buttons in a single-axis stack, **column or row** (D3).
Header-only C (D1). The caller owns the tree (D5); the SVG `id` is the address
space (D4).

```
render(tree, sink, ctx)            -> a complete SVG document
render_clipped(tree, ids, n, sink, ctx)
                                   -> a complete document, same viewBox,
                                      containing only what intersects those
                                      widgets' rows, backing included
rows(tree, ids, n, device_h)       -> the device row range for RV9_SVG_SS_ROWS
```

**Why a sink rather than a returned string.** `render(tree) -> text` implies a
4 KB buffer, in a program budget of roughly 39 KB, with no allocator in
`rv9_mod_env_t` and modules typically declaring 1.5–3 KB of stack. A sink costs
nothing, keeps the buffer form for tests — a sink that appends to an array *is*
the buffer form — lets §2.3's byte budget be enforced **during** emission rather
than discovered after it, and is position-independent, because a function
pointer *passed* is fine and only a stored table of them is not.

**Note the set form.** `rows(tree, id)` taking one id cannot be retrofitted
cheaply, because by then callers have written loops. A real interface changes
more than one widget at a time — a value updates while a button is held — and two
widgets in different bands mean either two clipped writes or one clip spanning
both. The set form lets whisker make that choice; the single form makes the
caller make it badly. (`rv9-answers.md` §3.3.)

`rows` returns **device rows**, not viewBox units (§2.2), so it needs the device
height. Take it as a parameter rather than reading a device: whisker stays pure,
and the host got it from `getstat(RV9_GS_SIZE)` on `/w0`.

#### Justify — where the leftover space goes

**Built 2026-10-04**, the first thing taken from `scale-sketch.md` and the only
one; the rest of that sketch stays unratified. It is an extension of **D3**
rather than a new decision: D3 settled a single-axis stack with a per-child
weight, and this settles what happens to the surplus when **no child claims
it**.

```
WSK_START    all of it after the last child   (the default, and what
WSK_END      all of it before the first        whisker did before)
WSK_CENTER   half at each end
WSK_BETWEEN  shared between the children, none at the ends
WSK_AROUND   shared around them, half-shares at the ends
```

**`weight` and `justify` do not compete**, and that is the one interaction
worth stating: a stack with any weighted child has *no surplus left*, because
the children took it, so a justify has nothing to place and does nothing. That
is asserted with a control rather than left to be discovered.

**The arithmetic is computed from the index, not accumulated**, so rounding
cannot drift: `free * i / (n-1)` is exactly `free` at the last child whatever
the division threw away on the way — the same care the weighted path already
took by giving the last weighted child the remainder. Asserted: under
`WSK_BETWEEN` the gaps it makes are *equal*, and the last child ends exactly on
the far padding.

**What it cost, measured rather than guessed.** This document twice called it
"free" and "zero extra bytes" before it was built, and neither was quite right:

- **~96 bytes of code** per module — `module_shape.c` went 3,972 → 4,068.
- **+1 byte** in the document for the panel measured, and that is coordinate
  digits, not markup. The assertion that holds is the one worth making:
  **justify adds no elements**, it only moves them, which the suite checks by
  counting `<rect>` and `<text>` under two justifies.

`WSK_START` is zero, so an unset tree lays out byte for byte as it always did —
every golden in the suite is unmoved.

#### "A sink costs nothing" is wrong on one host, and the fix is the caller's

**First, whose problem this is.** What follows is true of RV-9's `/w0` and of
nothing else whisker has met. It is in this document because `/w0` sets the
budget (§2), not because it is a property of writing SVG — and the conclusion
it argues for is that whisker should keep **not deciding**, which is the only
conclusion that survives contact with a second display system. §2.8 is the
general form.

That done: on `/w0`, "a sink costs nothing" is true about *memory* and false
about *stack*, and the difference was found on 2026-10-02 by RV-9's session
bisecting two stack declarations for whisker programs that were both wrong —
one of which overflowed on the board.

**Drawing to `/w0` renders on the writer's stack.** RV-9's `modules/pic`
already recorded it. whisker streams rather than buffers, so the `put()` that
finishes a document runs RV-9's entire rasteriser **with every one of
whisker's layout frames still live beneath it**. The two depths add. That is a
real consequence of the choice above, and the trade is sharper than "a sink
costs nothing": the 4 KB buffer is saved and paid for in stack, at the worst
possible moment.

RV-9 asked the obvious follow-up — a buffering sink would pay the two depths
separately and could declare a smaller stack — and left it as *"worth a
measurement rather than a decision."* `tools/evidence/stack/` is that
measurement, from `-fstack-usage` frames and the call graph in the object file,
with tail calls separated from calls because `-Os` emits plenty of both:

| whisker's chain, down to the write | |
|---|---|
| `module_shape.c` — one stack | 560 bytes |
| `module_input.c` — a field | 576 bytes |
| `module_compose.c` — a composite | 800 bytes |

Call the rasteriser's own depth **R**. It is beneath the module either way, so
it cancels, and the comparison is clean — `buffered.c` is `module_compose.c`
with the sink changed and nothing else:

| | stack | statics |
|---|---|---|
| **streaming** | 800 + R | 0 |
| **buffering** | 544 + R | 4,096 |

**Buffering saves 256 bytes of stack and spends 4,096 of statics**, and both
come out of one process footprint — `rv9/module.h`: *"its own stack, statics
and descriptor"*. It is a bad trade by 3,840 bytes whatever R turns out to be.

*Remeasured 2026-11-11, after §5f made drawing recursive:* one stack 640,
a composite 960, each level of nesting 256 more; buffering now saves 400 and
is a bad trade by 3,696. The conclusion did not move.
Note where the buffer would have to live: not a local array, which is the
stack this was meant to save, and not `.bss`, which a module does not have —
only `env->statics`, declared in `build.conf`, which at least makes the cost
visible rather than discovered by an overflow.

**So an RV-9 module should not buffer, and that is now measured rather than
assumed.** The saving is small because whisker's own chain is small; streaming
puts that chain beneath the rasteriser, it does not pay for the rasteriser
twice.

**But note what that sentence does *not* say.** It is a finding about RV-9's
modules, not about whisker's API, and the two were never the same question.
whisker emits through a sink precisely so that **buffering is the caller's
choice**: a sink that appends to an array *is* the buffer form, as this section
said four paragraphs above it was written. A host that would rather hold the
document — one with an allocator, a stack measured in megabytes, or a renderer
that runs later on its own thread — passes a buffering sink and whisker never
knows. What the measurement settles is which sink an RV-9 *module* should pass.
Had it come out the other way it would still have settled only that.

**What R actually depends on — answered 2026-10-03, by being wrong about it.**
`wstat` was staged with a declared stack of 8,192, reasoned from its call graph
plus `wflick`'s board figure: *"a minimum somewhere near 6,432"*. RV-9 bisected
it. **It runs at 5,120**, and is declared at 6,144 — *less* than `wflick`'s
7,168, despite `wstat` having the **deeper module chain** of the two (880 bytes
against 592).

The reason is the one that matters:

> **The renderer's depth depends on what the DOCUMENT contains, not on the
> program.** `wflick` draws corner marks, so it reaches `do_path` and
> `rv9_raster_fill_n` — a **624-byte frame** on its own. `wstat`'s readout has
> no paths in it at all.

So the rule, and it is not the obvious one: **measure the stack with the
richest document the program can emit**, not with a typical one or with
another program's. A figure measured against one document does not transfer to
another even on the same board.

**And whisker's case is unusually clean.** `<path>` appears **exactly once** in
`whisker.h` — in `wsk__marks`. Corner marks are the only path whisker can
emit; everything else is `<rect>`, `<text>` and `<g>`. Two consequences:

- A program that never sets `WSK_MARK_CORNERS` never reaches the path
  renderer, and the measured difference between the two board figures is
  **about a kilobyte of declared stack**.
- **`-DWSK_NO_MARKS` makes that a guarantee rather than a habit** (§7). A build
  without marks *cannot* emit a path, so its lower stack requirement is a
  property of the binary rather than of the document it happens to produce
  today. That is a much better argument for the minimal profile than the 288
  bytes of code it was sold on — **stack is the scarcer thing**, and this buys
  roughly four times as much of it as the code saving.

**What is still not whisker's, and is worth knowing before writing anything
that draws:** R itself. `pic`'s figure of 2,080 bytes is the only one written down
anywhere, so it is the one a whisker program would reach for, and it is **not
an upper bound**. Against RV-9's board bisections, a whisker document costs the
rasteriser **at least 4,528 bytes** — more than twice that — and R differs by
**at least 1,232 bytes** between `wflick`'s document and `wcompose`'s. There is
therefore no single number for "drawing to `/w0`" and a program cannot be given
one. What a whisker program *can* be given is its own chain, which is what
`tools/evidence/stack/` prints — and that part *is* host-neutral, because it is
measured from whisker's own frames and nothing else. The rest has to be
bisected on the hardware, and on the P4 — 75 bands against 22, and RV-9 owning
the pixels in PSRAM rather than the panel holding them (§2.7) — bisected again
rather than carried over.

**Done means:**

1. `render(tree)` output parses, and a realistic panel of a dozen controls fits
   **under the 4 KB `SRC_MAX` ceiling** — asserted, because the ceiling is real
   and is re-paid per band (§2.2, §2.3).
2. `render(tree, ids, n)` and `rows(tree, ids, n)` agree: everything the document
   draws lies inside the reported rows, and every widget intersecting those rows
   is in the document. **With the control** that a widget *not* intersecting them
   is absent — without it, "the clipped document is correct" is satisfied by
   emitting the whole scene every time, which is the thing being optimised away.
3. **The band is cleared to the device background, not the scene's** (§2.2), so a
   clipped document over a coloured panel must carry its own backing. Asserted by
   rendering over a non-default `OPT_BG` and requiring the backing to be present.
   **This is not hypothetical**: RV-9's `flick` failed exactly this and shipped
   two black strips for months, because no geometric check sees it.
   Assert too that the backing covers **the rows and not the screen**, and that
   the rows are the **snapped** ones: asserted at a widget that is *not*
   8-aligned, because an aligned one passes without exercising the rule
   (`review-2026-09-30.md` §3.4).
4. Widget heights **land on multiples of 8** where the caller does not say
   otherwise, since the clip snaps outward to 8-row bands and a straddling widget
   costs a whole extra band for nothing (§2.2). **The default button is 64
   rows** — eight bands, band-aligned, and about 9 mm on the P4's ~170 dpi,
   which is a finger. That number comes from the panel a person will touch, not
   from the C5's proportions.

   **Checked against a finger, 2026-10-01, rather than left as arithmetic.**
   48, 64 and 80 rows were drawn at matched *physical* size on a 102 dpi
   monitor — 7.2, 9.6 and 12.0 mm, which is 29, 39 and 48 pixels there — and
   judged by touching them. **64 was the comfortable one**, so the default
   stands, on evidence rather than on a calculation.
   `tools/evidence/finger_size` is the card, and will redraw it for any target
   size on any screen. The pressed state was drawn too, so the corner marks
   were seen at the 2.4 mm they will really be.

   What that does **not** settle is touch *slop* (§5b), which needs a real
   capacitive surface and is still open — and it is one person's finger on a
   surrogate screen, which is worth more than arithmetic and less than the
   panel itself.
5. `render` **refuses** a document it cannot fit in the caller's byte budget,
   and says so before writing anything, rather than emitting a prefix (§2.3).
   Asserted with the control that a panel one widget smaller is accepted.
6. `render` **refuses a label** carrying `<`, `&`, `>` or a byte outside
   32..126, naming the widget (§2.4). Control: the same label, legal, is drawn.
7. It **compiles under RV-9's module rules** (§4.1), which is a test and not a
   build detail: build the iteration-1 source with `tools/build_modules.sh`'s
   flags and linker script, and let the double-link position-independence check
   run. A widget table that works on the host and is not position independent is
   the expected failure, and it should fail loudly in iteration 1 rather than
   quietly in iteration 3.
8. Rendered through RV-9's own `tools/hosttest/render.c` to a PPM **that a person
   looks at once** — because *"a picture can be geometrically perfect and still be
   wrong in a way only an eye catches"*, which is RV-9's own lesson, learned from
   circles that came out lumpy with the right radius and the right area, and
   re-learned by the `flick` strips.

**Sizing rule, and it is what makes the layer scale:** widgets are sized in
**pixels, not proportions**. A repaint costs *(its bands ÷ total bands)* of a
frame, so a fixed-height widget costs about the same in absolute terms on a large
screen as on a small one, while a proportionally-scaled one costs a constant
*fraction* and therefore gets absolutely slower as the screen grows. A 48-row
button is 6 bands at 320×172 and 6 bands at 1024×600; grown to keep its
proportion it is 21. **Let a bigger screen hold more widgets rather than bigger
ones** — which is also what touch wants, since a target should be about a finger
wide whatever the panel. (`rv9-answers.md` §4.)

**The device and the area are two different numbers, and conflating them is
the wrong-strip bug.** A tree carries the device size, which is the viewBox and
must stay the viewBox: user units are then device rows and `rows` needs no
conversion. Where the stack *sits* inside that is a separate rectangle,
`wsk_area`, the whole device until it is narrowed. A toolbar across the top of
a 172-row panel is an area of 84 rows, not a device of 84 rows — the second
scales the document onto the glass and every row number in it becomes a lie.
Found by drawing one and looking at it: the pressed button's lower two corner
marks were outside the clip and simply absent, which no geometric check saw.
It was also meant to be how a grid arrives later — two stacks as two trees over
one device, each with its own area — and **that part is not true as built**
(`gbasic-gap.md` §4): a full render paints its backing across the whole device,
so the second tree erases the first. `wsk_area` bounds where a stack lays out,
which is what fixed the toolbar; making two trees *compose* needs a tree that
paints no background, and then an answer for whose backing a clipped repaint
carries. That belongs to whichever iteration wants a grid.

A widget also carries a **mark** — `WSK_MARK_CORNERS` today — which is how a
pressed state is shown without the shadows and gradients §2.4 rules out. It is
the caller's to set, because whisker holds no state: a press is something the
host knows and the tree records, like the fill beside it.

**And the half of the sizing rule that was missing.** The rule above is about
the extent *along* the axis. Across it, a column simply took the whole area —
which at 1024 wide is a **1016×64 button, sixteen to one**, a letterbox no
finger is shaped like. Found by rendering the same tree at the P4's size and
looking at it, which is the first time whisker's own output had been seen
anywhere but the C5. So a tree carries a **cross-axis cap**: the widget is held
to it and centred in what is left, and a cap wider than the panel does nothing
at all — so one tree is sensible on both boards without every caller doing the
arithmetic from the device size it has just read. That is §2.7's instruction
not to bake the C5's limits into the model, pointing the other way for once.

**Explicitly not in iteration 1:** `hit` (iteration 2), the gBASIC binding,
focus, keyboard, scrolling, text entry, a grid, nesting.

**What iteration 1 proves, stated plainly:** that a widget set can be described,
drawn, and repainted a band at a time within RV-9's budgets, in code that a
module can legally link. It proves nothing about interaction, and until the P4
arrives nothing here can.

## 5b. Iteration 2 — `hit`, built before the panel arrives

**Scope:** `wsk_hit(tree, x, y)` → the widget at that point, or nothing. One
point; the panel reports five and whisker uses one (§2.6). No gestures, no
velocity, no stream — the host decides what a sequence of points means.

The point is in **device pixels**, which costs nothing because the viewBox is
the device (§5): a tap arrives in the same units `rows` returns.

**Why now rather than in October.** §5 deferred this because `render` was the
half that could be finished without a panel on the desk. That was true and is
not the whole argument: §1 says *both* functions are testable with no hardware
— a point in, a name out — and the real gate was D0, which is settled. What
will actually be scarce when the board arrives is **board time**, so the hit
test is better written now and the panel spent on the measurement nobody has
taken.

**Done means:**

1. For **every** widget, a point at its centre and at each of its corners
   returns that widget.
2. **It returns nothing in the gaps, nothing in the padding, and nothing
   outside the area.** This is the criterion the others are worthless without:
   *"`hit` returning an id is satisfied by 'always return the first widget'
   unless a point in the gaps is asserted to return nothing"* — the house
   evidence standard, written for exactly this function.
3. Edges are **half-open**, so two widgets with no gap between them never both
   claim the pixel they share, and no pixel between them belongs to neither.
4. **The loop closes**: a point becomes an id, the id becomes a row range, and
   the clipped document repaints that widget and no other. Asserted end to end
   through RV-9's rasteriser, at the pixels, because that is the whole of what
   an interaction is.
5. It compiles under the module rules and is position independent, as in §5.
6. The same tree hits correctly at both panel sizes and under a cross-axis cap,
   since the cap moves every widget sideways and a hit test that ignored it
   would be wrong only on the board that has touch.

**Explicitly not in iteration 2:** a focus model, gestures, multiple contacts,
and **touch slop** — the question of whether a tap two pixels into a gap should
count. That one is deliberately left open, because the right tolerance depends
on how accurate the controller's centroid actually is, and nobody has measured
it. Exact rectangles today, and a number after the board says what it needs.

**Measured on the P4, 2026-10-24** (`wslop`, `tools/evidence/slop/`). One
finger, five marks, eight taps each. **Scatter about the mean is 7–17 px rms
and 12–26 px worst — 1.7 to 3.8 mm**, the worst in the corner furthest from
the hand. **Drift between DOWN and UP was zero on every one of 40 taps**, so a
press and its release belong to the same widget. Against the default 64 px
target that is comfortable: a tap aimed at the centre lands inside it with
room to spare, and one in a 4–8 px gap is a near miss, not a choice.

**Bias, measured properly the same afternoon (run 4, sized marks): at most
10 px (1.5 mm), and nil at the corners**; scatter 9–21 px worst, drift zero
again. So **`hit` stays exact**: the touch lands where the finger aims to
within its own scatter, and the scatter turns into a layout rule rather than
a tolerance — a widget meant to be tapped wants to be at least twice the
worst scatter, about 42 rows, which the default 64 clears. A tap in a 4–8 px
gap is an aim at an edge, and nothing a hit test should guess at.

What follows is the account of the run before it, kept because it is how
the layout bug behind D10 was found.

**Bias was not measured, and the first account of it was wrong.** Every tap
landed +37 to +48 rows low, and I reported it to RV-9 as the panel drawing
~45 rows low. It was whisker's: the marks were buttons with `size = 0`, which
in a column take `WSK_BTN_ROWS` (64) whatever their area says and overflow
it downwards — so each "mark" was a 12×64 bar hanging below its target, and
the 4-row edge bar meant to check the panel was a centimetre thick
(`rv9-answers-2026-10-24.md`). The scatter stands, since a constant offset
drops out of a spread, with two qualifications: it is the spread of aims at
the middle of a tall bar rather than at a point, and the bottom two marks ran
off the glass, which squeezed their taps. A re-run with sized marks is what
measures bias, and is owed.

That re-run is the paragraph above.

**And the measurement nobody has taken**: finger-down to pixel-changed, end to
end. That number includes an I²C controller, an interrupt, a process wakeup and
a repaint, and the 19 ms of §2.2 is only the last of those. It is the first
thing to do when the panel is on the desk, and it needs an RV-9 touch driver,
which does not exist yet (§2.6).

## 5c. Iteration 3 — composition, because the keyboard asked for it

**Scope:** more than one stack over one device, in one document. Nothing else:
no nesting, no box model, no second axis inside a stack.

**Why this and not another widget.** §6's measurement (`gbasic-gap.md`) says the
distance to gBASIC's declarative `gui` is one widget, `input`, and that on RV-9
an input field means an on-screen keyboard. Drawing one is what found the defect
below. So this iteration is not a widget at all — it is the thing the only
remaining widget needs, and it was discovered by trying to build that widget
rather than by deciding what ought to come next.

**The defect, stated plainly.** §5 promised a grid as *"two stacks are two trees
over one device, each with its own area"*. It does not work: `wsk_render` emits a
backing rect across the whole device, so the second tree erases the first. Four
keyboard rows came out as one. A picture showed it at once and no assertion would
have — the geometry of every row was right.

**The answer, and it is one sentence: the background belongs to the document,
never to a stack.** That is the question §5 left open — *whose backing does a
clipped repaint carry* — and it dissolves once the document is a thing with a
name. The document owns the device size, the background, and the rows being
painted. A stack contributes widgets and no background at all. A clipped repaint
is then a document over the band's rows that offers itself to every stack in
turn, and each contributes only what intersects: the band comes out complete
whichever stack the tapped widget was in, and no stack has to know another
exists.

**Shape.** The emitter splits where it already had a seam:

```
wsk_open(doc, sink)        <svg>, the backing for doc's rows, the shared <g>
wsk_part(doc, tree, sink)  that stack's widgets, clipped to doc's rows
wsk_shut(sink)             </g></svg>
```

`wsk_render` becomes exactly `open` + `part` + `shut`, so the single-stack path
and the composite path **are** the same code and cannot drift — D2's reason,
applied to the emitter rather than to the two functions.

The two-pass refusal survives: `wsk_compose(body, ctx, sink, budget)` runs the
caller's body once against a counting sink and once for real, so a composite
over `SRC_MAX` is refused before a byte, as §5's criterion 5 demands of a single
tree. The body is a function pointer **in a parameter**, which is already how
the sink works and is position independent; it is a stored *table* of them the
module rules forbid (§4.1). The body must emit the same bytes twice — it is
called twice — which is a real constraint on the caller and is why it takes a
`ctx` rather than closing over anything.

A stack with its own `font` or `ink` gets a nested `<g>`; one that matches the
document's gets nothing, so the common case costs zero bytes.

**Done means:**

1. Two stacks over one device **both appear, at the pixels**, through RV-9's
   rasteriser, in a picture a person looks at. The control is the same two
   rendered the old way, where the second is asserted to have erased the first —
   without it this criterion is satisfied by a harness that drew one stack.
2. A **banded** repaint of a widget in one stack carries the other stack's
   widgets that intersect that band, and the control is a band intersecting
   nothing of the second stack, which must not carry it.
3. It **refuses** over budget before writing a byte, like §5's criterion 5, and
   the control is a composite one widget smaller being accepted.
4. A stack whose device differs from the document's is **refused**
   (`WSK_E_DEVICE`) rather than laid out against the wrong size. That is the
   wrong-strip bug's family (§5) and the one mistake this API newly makes
   possible.
5. **Byte-exact against the existing goldens.** The refactor is supposed to move
   nothing, and the whole §5 and §5b suite re-run is the assertion that it did
   not.
6. Module rules and the double-link, as §5 criterion 7.
7. **Measured, not assumed: what composition costs or saves.** Four keyboard
   rows as four documents pay four framings; as one composite they pay one. The
   number goes in the gap document beside the 3,848 it corrects.

**What this does not fix.** Not the keyboard's bytes — 36 keys were 94 % of
`SRC_MAX` and one framing back is a few per cent, not an order. Composition is
the structural half; the byte half is still the three options in
`gbasic-gap.md` §3, and still nobody's to decide today.

**Explicitly not in iteration 3:** nesting a stack in a stack, a real grid
layout that computes cells (the caller places areas), focus, scrolling, and
`input` itself.

### What it came to, 2026-10-02

Built, and all eight criteria met. 106 checks where §5 and §5b left 74.

**The measurement turned out to be the point.** 36 keys at 1024×600:

| | bytes |
|---|---|
| as four documents, one per row | **4,186** — and they erase each other |
| as one composite | **3,775**, 321 under `SRC_MAX` |
| the three framings composition drops | **411** |

411 is larger than 321. So a 36-key keyboard **does not fit without
composing** — this is a correctness iteration twice over, once in what
reaches the glass and once in what fits the ceiling. The 3,848 in
`gbasic-gap.md` was the same keyboard measured before it had ids and a TAB
key; the shape of the conclusion is unchanged and the number is now the
measured one.

**And a control that was not a control.** Criterion 6 asks whether the
function pointer `wsk_compose` takes links in a module. With one body, gcc
const-propagated it: `jal comp_body` inside `wsk_compose.constprop.0`, no
address ever materialised, and the double-link proved **nothing at all** about
the construct the criterion exists for — while passing. Found by disassembling
the module rather than by trusting that it had been tested. `module_compose.c`
now chooses between two bodies on a number only the window knows, which forces
`auipc`/`addi` into a register and an indirect call, and *that* is what the two
link bases now compare. The house rule about controls turns out to apply to
builds as well as to assertions.

**One thing seen in the pictures and deliberately not fixed:** a stack does not
clamp to its area. Two 64-row buttons in a 112-row area simply run off the
bottom of the panel. That is true of iteration 1 and is not composition's to
change; it is noted here so it is a known behaviour rather than a surprise, and
the pictures set sizes that fit.

**Changed 2026-10-24 (D10): such a stack is now refused**, with `WSK_E_FIT`
naming the widget that crosses the end. "Known rather than a surprise" held
for the pictures and not for the next program written — `wslop`'s marks were
exactly this and cost a day on the P4. Turning the check on found that
`module_compose.c` itself had been doing it on the C5 all along: the pictures
sized their buttons to 47, the module never did, and `wcompose` drew Cancel
28 rows off the panel. It sizes them now, on short screens only.

---

## 5d. Iteration 4 — `input`, which is the last widget

**Scope:** a text field. One widget kind, `WSK_FIELD`: a trough, its text, and
a caret where the next character goes. Nothing else — no selection, no cursor
keys, no scrolling by drag, no clipboard.

**Why it is the last one.** §6's measurement says gBASIC's declarative `gui`
accepts three component kinds, whisker had two, and the third is this. Nothing
after it has a caller at all (`gbasic-gap.md`). That is the whole argument for
building it and the whole argument for stopping afterwards.

**Two decisions arrived before it did.** **D6**: `input` goes only to hosts with
an input device, so the C5 is out — not on bytes, which turned out to be half
the budget rather than 94 % of it, but because the C5 has nothing to press a key
with and a key on its panel is 4 mm against the 9 mm a finger wants. **D7**: the
mouse stands in for touch until October. A click is one point, which is exactly
what `hit` takes and exactly what the panel will deliver, so the loop — point,
name, band, repaint — can be finished and watched now.

**What whisker supplies and what it does not.** whisker is pure (D5), so it
holds no text. The field's characters live in the widget the *caller* owns, and
the caller appends to them: whisker says which key was hit and draws what the
caller put there. There is no key-to-character map in here, because a layout is
an application's business and a shifted layout is just a different set of
labels. The on-screen keyboard is not a new widget either — it is button stacks
and §5c, which is why §5c came first.

**Three things a field does that a label does not**, and each is a real
difference rather than a style:

1. **It is left-aligned.** A label is centred by the document's `<g>`; a field
   spells `text-anchor="start"`, because text being typed must not move under
   the finger as it grows.
2. **It shows the tail, not the head.** A label truncates what overflows; a
   field that did would hide what you just typed. The last characters that fit
   are the visible ones.
3. **It carries a caret, and only when told to.** `WSK_MARK_CARET` beside
   `WSK_MARK_CORNERS` — the caller's to set, like the pressed fill, because
   focus is state and whisker has none.

**Done means:**

1. The text is **left-aligned in the trough** and the caret sits where the next
   character will land — asserted at the pixels, not only in the document.
2. Text longer than the field shows its **tail**, and the caret stays inside
   the trough. Control: a string that fits is not scrolled, and the two cases
   are both asserted to occur.
3. The caret is drawn **only when marked**. Control: the same field unmarked
   has none, checked at the pixels — a caret is two columns and an assertion
   on the document would pass on a caret drawn in the wrong place.
4. **The loop closes with a real pointing device.** On `host/linux`, clicking a
   key appends a character and repaints **only the field's band**: pixels
   inside it change, none outside. That is D7 being spent on something.
5. A **field and a full keyboard together fit `SRC_MAX`** at 1024×600, measured
   and recorded — and if they do not, that is the finding and the compact form
   in `tools/evidence/keyboard/` is what it buys.
6. Module rules and the double-link, as §5 criterion 7.
7. A person looks at it, as §5 criterion 8.

**Explicitly not in iteration 4:** a key-to-character map, shift state, a
cursor that moves, selection, multi-line text, scrolling, password masking, and
any notion of focus beyond the caller setting a mark.

### What it came to, 2026-10-02

Built, all seven criteria met. **135 checks** in the suite and **19** on the
host, where §5c left 106 and 12.

**The loop closes, and it closed with a mouse.** `host/linux/selftest.c` pushes
a click on a key, `wsk_hit` names it, the application copies that key's own
label into the field, and the repaint that follows changes **83 pixels, all of
them inside the field's four bands** — nothing outside, asserted with a control.
`./run.sh --input` is the same loop with a person in it.

**It all fits, and barely.** A field and 36 keys at 1024×600 compose to **3,981
bytes, 115 under `SRC_MAX`**. That is enough to be true and not enough to build
on: a panel behind the keyboard, a second field, or a title would not fit. The
compact form in `tools/evidence/keyboard/` is worth 1,746 bytes and is what
buys the room, when something needs it.

**A field holds 31 characters and a NUL**, because that is `WSK_TEXT_MAX`. Found
by writing the tail-scroll test and discovering it could not scroll: a 308-wide
field at font 16 holds **36** characters, so it outruns the buffer before it
outruns the trough. The tail matters at touch-sized fonts and in narrow fields,
which is where a field on a panel actually lives — at font 24 the same field
holds 24, and the test exercises it there. Raising the cap is one line and costs
every widget in RAM; nothing needs it yet, and a field that silently stopped
accepting characters would be worse than one that says 31.

**What a person sees**, and it is the check two of these criteria needed: the
text is hard left in the trough, the caret sits immediately after the last
character, the leading characters have scrolled off, and the trough reads as a
well rather than as a button beside the one next to it.

---

## 5e. Iteration 5 — scrolling, because the desktop asked for it

**Written 2026-10-24, before any code**, per the house rule. Matthew: *"go
ahead and start on scrolling"*, on `window-gap.md`'s ordering. It is the
first iteration since §5d said nothing after `input` had a caller, so the
caller is named first: **gBASIC's `window`**, where `scroll`, `table`,
`tree` and `memo` are all a viewport onto more rows than fit, and Matthew's
hope that whisker serves a desktop as well as a panel (2026-10-24). On the P4
it has a second caller, which is any list longer than 600 rows.

**Scope:** a stack may scroll along its own axis. Not a new widget kind and
not a container: **a property of a stack**, which is already the only
container whisker has. Two fields on the tree and nothing else —

```
t.overflow = WSK_SCROLL;     /* WSK_REFUSE, the default, is D10 */
t.scroll   = 120;            /* pixels along the axis, the caller's to move */
```

— and three small functions: `wsk_scroll_max(t)`, the furthest it may go;
`wsk_area_rows(t, &y0, &y1)`, the band-snapped rows a scroll step damages;
and the existing `hit`, `rows` and `part`, which learn the viewport.

**Why a property and not a kind.** Studio's `scroll` wraps one child, and that
child is always a column. whisker has no nesting (D3), and a stack *is* a
column, so the wrapper has nothing to wrap. `table`, `tree` and `memo` are
then each a scrolling stack of rows, which is the order `window-gap.md` gave.

**The viewport is the area.** The content is the stack laid out at its
natural extents — padding, widgets, gaps, padding — and slid back by
`scroll`. A stack whose content fits its area lays out **exactly** as it does
today, byte for byte, which is the control on everything below: scrolling
changes nothing until there is something to scroll.

**Five decisions, made here because they are cheap to state and expensive to
retrofit.**

1. **The caller holds the offset (D5).** whisker remembers nothing between
   calls, so there is no momentum, no drag tracking and no wheel: a wheel
   click or a finger's travel is the host's, and the host turns it into a new
   `scroll` before asking for a picture. That is the typing loop's shape
   (§5d) and D7's: on Linux the mouse wheel and a drag stand in for the
   finger.
2. **Out of range is refused, not clamped.** `scroll < 0` or past
   `wsk_scroll_max` is `WSK_E_SCROLL`, before a byte. The argument is D10's
   and D8's: clamping is a substitution, and the caller is the one that knows
   whether an overshoot is a bug or a bounce. Clamping is one line for a
   caller that wants it, and `wsk_scroll_max` is there so it is one line.
3. **whisker clips by arithmetic, and only what arithmetic can clip.** There
   is no `clipPath` on `/w0` (§2.4), and `/w0`'s row clip is both a property
   of one display system (§2.8) and *vertical only today* — the layout model
   must not lean on either. So a widget half in view is drawn **cut**: its
   box is intersected with the viewport, which is integer arithmetic on a
   rectangle. **Text cannot be cut that way**, so text is drawn **whole or
   not at all**: it appears when its cell is wholly inside the viewport and
   not before. The corner marks and the caret go with the text. A flat
   substrate's price, recorded rather than hidden: a label pops in at the
   edge instead of sliding in.
4. **Nothing outside the viewport answers `hit`.** A widget scrolled off the
   top still has a rectangle, and that rectangle may sit over a neighbouring
   stack. A hit test that consulted it would name a widget the person cannot
   see — the gap-is-nothing argument of §5b, one level up.
5. **`rows` is clipped too**, and a widget wholly out of view contributes no
   rows: asking to repaint only such widgets is `WSK_E_EMPTY`, which is the
   truth — nothing visible changed.

**What a scroll step costs is already priced** (`tools/evidence/scroll/`,
corrected against the P4 2026-10-11): the bytes track the viewport, not the
list, and a step damages the whole viewport. So a scroll is a repaint of
`wsk_area_rows`, which is a composite repaint whenever another stack shares
those bands — §5c's machinery, unchanged. The one new cost is that two
widgets at the edges are drawn cut, which is at most two boxes.

**Done means:**

1. **Bytes track the viewport.** A 200-item scrolling list draws the same
   document as a 20-item one at the same offset — and the control is that
   the same 200 items, not scrolling, are refused (`WSK_E_FIT`, D10), so the
   overflow being scrolled is real.
2. **A list that fits is byte-identical to one that does not scroll** — the
   control on the whole iteration, and the reason `WSK_REFUSE` is zero.
3. **Nothing crosses the viewport, at the pixels.** A scrolled pane between
   two neighbours, through RV-9's rasteriser: every pixel outside the pane is
   identical to the unscrolled picture, and the control is that pixels
   inside it changed.
4. **A cut widget has its box and not its text**, and the control is the same
   widget, wholly in view, with its text.
5. **`hit` sees the viewport.** A point inside it names the widget drawn
   there, and after scrolling one item the same point names the next one; a
   point over a scrolled-off widget's rectangle names nothing — the control
   being that the unscrolled tree names it there.
6. **Out of range is refused** with `WSK_E_SCROLL` and nothing written, at
   `-1` and at `max + 1`; `0` and `max` are drawn.
7. **`rows` is clipped**: a widget scrolled wholly out is `WSK_E_EMPTY`; one
   cut by the edge gets the viewport's side of the band, not its own.
8. **§7's question, answered with a number**: which profile scrolling is in,
   what it costs, and that the reduced build refuses it by name.
9. Module rules and the double-link, as §5 criterion 7.
10. A person looks at it.

**Explicitly not in iteration 5:** a scroll *bar* (a position mark is
something a caller can compose as a sized widget in a stack beside the pane,
from `scroll` and `wsk_scroll_max`; whether whisker should draw one waits for
somebody to want it), momentum, a second axis in one stack, scrolling a
composite as a whole, `table`, `tree` and `memo` themselves — each is a
scrolling stack and each is its own question — and anything a host does to
turn a gesture into an offset.

### What it came to, 2026-10-24

Built, the same day, criteria 1–9 met and 10 waiting for Matthew's eye.
**219 checks** in `src/run.sh --module`, where §5d and D10 left 168: 40 in the
suite, 9 at the pixels, 1 profile control and 1 module link. Not a byte of
any existing document moved — the five §5e controls that compare against the
non-scrolling path are the assertion, and `git status` on the goldens is the
other one.

**At the pixels** (`scroll_pane.ppm`): a 200-row list in a pane between a
toolbar and a status line, scrolled 12 rows. **17,191 pixels changed inside
the pane and 0 outside it.** Row 0's box is cut at the top in its own fill
with no ink in it; row 1's label is there. And a *banded* scroll step — a
composite repaint of `wsk_area_rows`, 40..144, which carries the status line
because it shares the last band — leaves the screen **byte-identical** to a
full redraw. 538 bytes a step.

**Three things the building found, and none was in the design above.**

1. **A rectangle is four shorts, so a stack ends at row 32,767.** Found by
   timing a 20,000-row list, which drew 17 KB of rows from the wrong places
   with no error: rows past the limit wrapped to negative coordinates and
   some wrapped into view. Now refused, `WSK_E_FIT` naming the row that
   would wrap — 1,200 rows of 24 are accepted, 1,400 refused at 1,258. The
   non-scrolling path always had the same limit and never met it, because
   D10 keeps a stack inside its area and no screen is 32,767 rows tall.
2. **A document costs the square of the list.** `wsk_rect` lays out from
   the top of the stack every time, which is D2's protection against two
   layouts drifting, so `wsk_part` is quadratic. Measured on the host: 0.11
   ms for 200 rows, 5.2 ms for 2,000. Fine for a pane, wrong for a table.
3. **Both have the same answer, and it is D5's: hand whisker the slice.**
   The caller holds the list; whisker only needs the rows in view.
   `wsk_slice(pane, total, size, scroll, &first)` is the arithmetic for rows
   of one size — which rows, how many, and the offset into them — and the
   document it produces is checked **byte-identical to the whole list's at
   nine offsets**, with a control that a slice one row out differs. A
   hundred thousand rows, two million rows down, hands over a handful. It is
   a shortcut to the same picture, not a second layout.

**§7's question: 496 bytes, in the default, out of `WSK_MINIMAL`.**
`module_shape.c` is 4,168 bytes without scrolling and 4,692 with; with
`-DWSK_NO_SCROLL` it is 4,196, the 28 being the refusal (`WSK_E_KIND`,
`which = -1` because a scroll belongs to the stack rather than to a
widget). The C5 profile is unchanged in what it draws and refuses a
scrolling stack by name, checked in `src/run.sh` beside the field and the
mark. A P4 program that never scrolls may define `WSK_NO_SCROLL` itself.

**The board program is written and not yet run.** `src/module_scroll.c`,
`wscroll`: ten thousand rows in a pane, dragged with a finger, built a
slice at a time on the stack — the program holds no rows at all. It links
as a module at 6,628 bytes, position independent, and its own chain to
`write()` is **1,856 bytes**, so with the rasteriser's ≥4,528 it wants more
than 6,384: declare 8,192 until RV-9 bisects it. It times every drag frame
twice, the repaint alone and the finger's report to the last byte, which is
the measurement `tools/evidence/scroll/` could only derive. Before the
board, `tools/evidence/drag/` runs the module itself here against RV-9's
window and a scripted finger: a 300-row drag ends on row 6, and a tap at
y 330 is named `r11` and lands on the row labelled 11.

**Recorded for Matthew rather than ratified**, because the ledger holds only
what he has agreed: the five decisions above (the caller holds the offset;
out of range is refused, not clamped; text whole or not at all; nothing
outside the viewport answers `hit`; `rows` is clipped), and that scrolling
is in the default profile and out of the C5's.

### On the board, 2026-11-11

`wscroll` ran on the P4 with Matthew dragging. Staged in RV-9's tree
(`modules/wscroll/`, `whisker.h` refreshed to `c109eee`) and **fetched onto
the card over WiFi rather than flashed**, so the GT911 kept scanning — the
first whisker program to reach the board without costing a power cycle.
Declared 8,192 bytes of stack and ran without overflow; not bisected.

| pane | drag frames | repaint mean / worst | finger to pixels, worst | what the person said |
|---|---|---|---|---|
| 112 rows | 67 | **37.5 / 39.6 ms** | **42 ms** | "works very nicely" |
| 552 rows (the screen below the header) | 116 | 228.7 / 235.8 ms | 236 ms | "not super smooth but it works" |

- **The derivation was close and on the wrong side.** `tools/evidence/scroll/`
  said a pane of ~110 rows repaints inside a 30 fps frame; measured, 112 rows
  is 37.5 ms, about 27 fps, and every frame was over 33. Twelve per cent
  optimistic — near enough that a person called it smooth, not near enough to
  quote "30 fps" again. **The figure to quote is 37.5 ms for 112 rows.**
- **A screen of rows costs more than the 175 ms "full redraw".** 552 rows of
  whisker buttons took 229 ms. The 175 was measured on a different, lighter
  document, so it is not a bound on a dense one — CLAUDE.md's rule about
  checking what a figure was derived from, met again. Per row the two runs
  are 0.33 and 0.41 ms, so the cost is roughly the rows repainted; where it
  goes between whisker's emit and RV-9's rasteriser is **not bisected**.
- **`hit` through a scrolled pane named the right rows.** A tap at (580, 77)
  at offset 0 was `r0`; after a drag, (588, 125) was `r7`, both by the row
  arithmetic. The selection drew in its orange, which Matthew saw; which
  label his finger was on was not asked, so the name is checked against the
  geometry and not yet against an eye.

What it settles for the design: a **pane** scrolls smoothly on the P4 and a
**screen** steps, as §2.2's arithmetic said it would, and the line between
them sits a little under 112 rows rather than at 110. Nothing in §5e's five
decisions moved.


---

## 5f. Iteration 6 — nesting, because a window is a tree

**Written 2026-11-11, before any code.** D17 (nesting is built), D19 (the id
is prime and every widget is also reachable by path) and §9.9 (layout before
the toolkit) are the decisions behind it.

### Scope

A widget may be a **stack**: a new kind, `WSK_STACK`, whose contents are
another tree laid out inside the widget's own rectangle. Everything that
walks a tree — check, layout, render, hit, rows, find — descends into it.
Not in this iteration: wrap (D18, next), alignment across the axis, the
toolkit.

### How a child is named: an index, not a pointer

A widget gets `short sub`, an **index** into an array of trees the caller
supplies once, on the root: `t.nest = trees; t.nests = N`. A pointer would be
simpler to read and is what a desktop toolkit would use, but **a pointer in a
`static const` panel is an address in rodata**, and that is exactly what an
RV-9 module may not link (§4.1). With an index, a panel stays `static const`
in a module — `{ .kind = WSK_STACK, .id = "toolbar", .sub = 1 }` — and the one
pointer, `nest`, is set at runtime, as `item` already is. It is also closer to
how gBASIC's record would be walked: a flat list, children by position.

`sub` fits in the padding `wsk_widget_t` already has: the struct stays 64
bytes.

A child tree is **laid out in a copy** whose device, area and `nest` come from
the parent — the area being the stack widget's rectangle — so a caller sets
only what the child *is*: axis, pad, gap, justify, overflow, scroll, colours,
font, items. `wsk_nest_init(child, parent, axis)` starts a child with the
parent's look; nothing is inherited silently.

### The rules

1. **A stack's natural extent** (when `size` is 0) is its content: along its
   own axis, the sum of its children and gaps; across, the largest child; plus
   its padding both ends. A toolbar row in a column is as tall as its tallest
   button and its padding. **Except along the axis a stack scrolls on**, where
   it asks only for its padding: being shorter than its content is what a
   scrolling stack is for, so its extent there comes from `size` or `weight`.
   (Found by the first test of a nested pane, which the root refused because
   the pane had asked for all 198 rows of its list.)
2. **D10 holds at every level.** A nested stack whose children want more than
   its rectangle is refused, `WSK_E_FIT`, naming the widget — unless it scrolls.
3. **Clipping accumulates.** A scrolling stack's viewport cuts everything
   inside it, at any depth; a widget inside a nested stack inside a scrolled
   pane is cut by the pane. The rectangle is cut by arithmetic and text is whole
   or not at all (D12) — against the intersection of every viewport above it,
   along both axes.
4. **The id is prime (D19)**, document-unique across the whole tree, so every
   lookup by id descends. **A path** is ids from the root, `/toolbar/save`,
   checked link by link: refused if `save` is not inside `toolbar`. An id may
   not contain `/`. A stack is a widget: its id names **the branch**, and its
   rows are its whole rectangle, so `rows("/main/sidebar")` repaints a subtree.
5. **`hit` returns the deepest widget under the point** — a leaf, or a stack
   when the point is in that stack's padding or gaps. In the root's padding,
   still nothing. `wsk_hit` keeps its old answer, an index in the root, so
   every flat caller is unchanged; `wsk_hit_in` returns the index *and* the
   tree it is in, and `wsk_hit_id` returns the id from any depth.
6. **Refused, by name** (`which` the widget, or -1): a `sub` out of range or a
   tree with no `nest` (`WSK_E_KIND`); a tree used twice, which would draw the
   same ids twice (`WSK_E_KIND`); deeper than `WSK_DEPTH_MAX`, which is also
   how a tree that contains itself is caught (`WSK_E_DEPTH`).
7. **In the document**, a nested stack is a `<g>` only when it has something
   to say — an `id` (when ids are on), a different font or ink — and a filled
   rectangle behind its children only when its widget has a `fill`. A stack
   that says nothing costs nothing. The `<g id>` is what the browser client
   will replace (§9.7).

### Profile (§7's question)

`WSK_NO_NEST`, in `WSK_MINIMAL`: the C5 keeps flat stacks (D17 narrowing D3)
and refuses `WSK_STACK` by name. The nesting code sits behind that macro in
every shared loop, so the minimal build is **the same code it was**, and its
module size is the evidence.

### Done criteria

1. **No flat tree moves.** All 219 checks pass, every golden byte-identical,
   and the PPMs compare equal to the ones made before the change.
2. A column holding a toolbar row, a body and a status row lays out to the
   rectangles the rules give, written out by hand in the test.
3. A size-0 stack takes its content's extent, along and across.
4. `hit`: the deepest widget; a stack in its own gap; **nothing** in the root's
   padding (the control); `wsk_hit` on a flat tree unchanged.
5. ids and paths: a nested id found; `rows` of a leaf and of a branch; a path
   resolved, a wrong chain refused, an id with `/` refused.
6. The refusals in rule 6, each with a control that the same tree minus the
   fault is accepted.
7. A scrolling list nested between a header and a footer, and a row nested
   inside a scrolled pane, cut by the pane.
8. The profile: `WSK_MINIMAL` byte-identical on the shared panel, refusing a
   nested tree as the control; the cost of nesting in bytes.
9. Through RV-9's rasteriser, a nested panel as a PPM, for a person to look at.
10. It links as a module, double-linked; and **the stack**: recursion adds to
    the rasteriser's depth on `/w0` (CLAUDE.md), so whisker's own chain for a
    nested document is measured and printed, not guessed.


### What it came to, 2026-11-11

Built the same day, criteria 1–10 met except a person looking at the
pictures and a run on the board. **286 checks** in `src/run.sh --module`,
where §5e left 219: 55 in the suite, 8 at the pixels, 1 XML, 1 profile
control, 2 module links. `host/linux/run.sh --test` still passes.

**No flat tree moved** (criterion 1): the 219 earlier checks pass unchanged,
and every saved output — ten PPMs, six SVG documents — was compared byte for
byte against a copy taken before the first line changed. All identical.

**At the pixels** (`nest_window.ppm`, `nest_pressed.ppm`): a toolbar row, a
body row holding a scrolling list beside a column of buttons, a filled status
row. Scrolling the list, three levels down, changed 8,425 pixels inside it
and **0 outside** — not the column beside it, which shares every one of its
rows. The list's first row is cut at the pane's edge in its own fill, with no
ink. A banded repaint of the branch **by path** (`/body/left`), and of one
button two deep, each leave exactly what a full redraw does.

**Three things the building found.**

1. **A scrolling stack must not ask for its content along its axis.** The
   first nested pane was refused by the root because the pane, measured by
   rule 1, wanted all 198 rows of its list. Rule 1 now says so (above).
2. **Depth has to be refused before layout.** Natural size measures every
   tree beneath a stack, so a chain one past the limit was measured short from
   the root and long from the level below, and its refusal came out as
   `WSK_E_FIT`. A structural pass now refuses it as `WSK_E_DEPTH` first. A
   cycle never reaches it: every tree in a cycle is inside two stacks, and is
   refused as that.
3. **A row with no padding cuts the last character of every label.** Not
   nesting's: a widget in a row is its text's width plus padding, and a
   label wants two pixels of slack, so at `pad = 0` "OK" draws "O". True of a
   flat row since §5 and never met, because every row had padding. **Fixed
   the same day** by giving a label all of its width rather than widening
   every row widget by two pixels: no golden and no picture moved, which says
   the slack had only ever mattered in this one case.

**§7's question.** Nesting is **1,780 bytes** of the default profile
(`module_shape.c` 6,608 with it, 4,732 with `-DWSK_NO_NEST`); with it off the
refusal costs 40, and `WSK_MINIMAL` is 28 bytes larger than before, the same
refusal. The bulk is real work — recursion stops the compiler inlining the
emit loop, plus the recursive check, id and path lookup, the child copy and
clipping by every viewport above. A tree copied field by field was 460 bytes
of it; a byte loop the compiler is told not to turn into `memcpy` is a few
dozen.

**The stack, measured** (criterion 10, `tools/evidence/stack/`). Drawing is
now recursive, and a call graph cut at its first cycle counts the loop once,
so the tool prints the rest: **each level of nesting adds 256 bytes** —
`wsk__items` 112 and `wsk__stack` 144 — all of it live while RV-9's
rasteriser runs beneath. The nested branch was moved out of line for this:
inside the loop, its child copy sat in every stack's frame and cost flat
programs 144 bytes for nothing. Flat programs now pay 640 (one stack) and 960
(a composite), from 560 and 800. `wnest`, three levels deep, is 2,288 of its
own; with the rasteriser's ≥ 4,528 it wants more than 6,816, so declare
8,192 until the board says otherwise.

**`wnest`** (`src/module_nest.c`) is the board program: the window above at
1024×600, a tap naming the deepest widget and its tree, a button pressed by
repainting only its rows, found by id at any depth. It links at 9,040 bytes,
position independent.

**On the P4, the same day**, fetched to the card over WiFi and run with
Matthew tapping: seven taps, each named the widget the layout puts there —
`back` and `go` in the toolbar row; `i2`, `i3`, `i5` in the list two levels
down; `yes` and `no` in the column beside it — and each pressed by a
repaint of its own rows only. Declared 8,192 bytes of stack; ran without
overflow; not bisected. No tap landed in a gap, so a stack answering for
its own padding was not seen on the glass (it is in the suite).


---

## 5g. Iteration 7 — wrap, the third answer to "it does not fit"

**Written 2026-11-11, before any code.** D18: `overflow` is one setting with
three answers — refuse (D10), scroll (§5e), wrap — and text is not special.
This iteration is wrap for **any** widget. A paragraph as *one* item, split
into words at layout, is the next one (§5h, to design): a widget's text is 32
bytes inline, a paragraph is longer, and where its words live is the same
static-panel question `nest` answered with an index.

### The rules

1. **`overflow = WSK_WRAP`.** Items are placed along the stack's axis; one
   that would cross the end starts a **line** below (a row) or beside (a
   column). Lines are separated by the stack's `gap`, as items are.
2. **A line is as deep as its deepest item**, measured across the axis, and
   every item in it is that deep — as every item of an unwrapped stack spans
   its whole depth today.
3. **Within a line, the rules of an unwrapped stack**: `weight` shares the
   line's leftover among that line's weighted items; with none, `justify`
   places it — START is a ragged-right paragraph, CENTER centred lines.
4. **What a wrapping stack asks for.** Along its own axis, only its widest
   item and its padding: it takes what it is given and wraps to fit, so
   asking for its content in one line would mean never wrapping. Across, the
   depth of its lines **when wrapped to the space it will get** — which its
   parent knows when it lays out (the parent's own span across), so the
   measurement carries that space down. Where nothing above knows it, a
   wrapping stack is measured as one line.
5. **D10 holds.** An item wider than the whole line is refused, `WSK_E_FIT`,
   naming it — a word is never broken, since breaking it is substituting.
   Lines deeper than the area are refused naming the first item that does
   not fit; a text window is a wrapping row **inside a scrolling column**
   (D17, D18), not a stack that both scrolls and wraps.
6. **Nothing else changes.** `hit`, `rows`, render and the viewport all ask
   `wsk_rect`, and wrap is only a different `wsk_rect`. An unwrapped tree lays
   out byte for byte as before.

### Profile

In the default; out of `WSK_MINIMAL` (`WSK_NO_WRAP`), refused by name, with
`which = -1` as a scroll is — the C5's panel is a fixed readout. Cost measured.

### Done criteria

1. Every existing check and golden unchanged.
2. A wrapping row of buttons lays out to rectangles written by hand: where
   each line breaks, each line's depth, each item's place.
3. Weight and justify per line, each with a line that has none.
4. A wrapping row in a column takes the depth of its lines at the width the
   column gives it; the same row at a different width wraps differently.
5. Refusals: an item wider than the line; lines deeper than a fixed area —
   each beside the same tree accepted.
6. A paragraph of one label per word, wrapping, inside a scrolling column:
   the text window, by composition, at the pixels — a person looks.
7. `hit` and `rows` on a wrapped item; a repaint of one word is byte-identical
   to a full redraw at the pixels.
8. Profile: `WSK_MINIMAL` refuses a wrapping stack, byte-identical otherwise;
   the cost in bytes. Module links, double-linked.


### What it came to, 2026-11-11

Built the same day; criteria 1–8 met except a person looking at the
picture. **319 checks** in `src/run.sh --module`, where §5f and the label fix
left 289: 23 in the suite, 9 at the pixels, 1 XML, 1 profile control, 1
module link. Every saved output before the first change — ten PPMs, nine SVG
documents — compared byte for byte afterwards: identical.

**At the pixels** (`wrap_text.ppm`): a text window by composition alone —
51 one-word labels in a wrapping row, inside a scrolling page, under a
title. It reads as a paragraph; scrolled 16 rows, its first line is above the
page's edge and gone whole (D12). One word, recoloured and repainted alone
(rows 104..136, 416 bytes), leaves exactly what a full redraw does.

**Found.**

1. **The check must name the item no line can hold before the line that
   runs out of depth.** In a 60-wide area every item takes a line, so the
   lines ran out at Gamma before the check reached Epsilon, which no height
   would fix. Widths are now checked first.
2. **One `gap` both ways makes text look double-spaced.** Words want a
   space's width between them; lines want much less, and a wrapping stack
   uses the same `gap` for both — so the paragraph above had 8 rows between
   lines of 24. **Fixed the same day, on Matthew's word:** `lgap`, the gap
   between lines, `WSK_SAME` (the word gap) unless set — so every wrapped
   stack already built keeps its spacing, and a paragraph says `lgap = 0`.

**§7's question: 752 bytes, in the default, out of `WSK_MINIMAL`**, which
refuses a wrapping stack by name (`which = -1`, as a scroll). The C5's build
is 4 bytes larger, that refusal.

**Not measured:** a wrapping stack is laid out by walking its lines from the
start for every item, so a document of one is quadratic in its words, as a
stack already was in its widgets (§5e). Fifty-one words drew in 1,169 bytes;
a page of five hundred would want §5h's paragraph, or a slice.


---

## 5h. Iteration 8 — a paragraph as one item

**Written 2026-11-11, before any code.** §5g made a text window out of one
label per word: right in rule, wrong in storage — 64 bytes a word, and 32
characters at most in any one widget. D18 said text is items "in rule, not
in storage"; this is the storage.

### The rules

1. **Strings live beside the tree, by index.** The root gets `str`, an array
   of string pointers, and `strs`, set at runtime and carried into every
   nested copy as `nest` is. A new kind, **`WSK_TEXT`**, names its string by
   index in `.sub`. A panel stays `static const`: the array of pointers is
   built at runtime, in RAM, which a module may do — what it may not have is
   a table of addresses in rodata (§4.1).
2. **In a wrapping row, a text item's words are units of the flow** (§5g):
   each is placed by the same rule as an item, a line breaks between two of
   them, and other items — a button, a field — may sit among the words. A
   word is its text's width, no padding; `gap` is the space between words.
   Words carry no weight; `justify` places a line's leftover as ever.
3. **Anywhere else a text item is a long label**: one line, centred,
   truncated as a label is.
4. **A word wider than a whole line is refused**, `WSK_E_FIT` naming the
   item — never broken (§5g rule 5). A string the window cannot draw is
   `WSK_E_TEXT`, as a label's is. A text item in a wrapping *column*, whose
   words would stand one per line, is refused, `WSK_E_KIND`.
5. **Fewer bytes on the glass.** Words of one item on one line, separated in
   the string by exactly one space while `gap` is exactly one character's
   advance, are one `<text>` — a run — rather than one per word. That is
   the common case, and it is what keeps a page of prose under `SRC_MAX`.
6. **`hit` names the text item** when the point is on any of its words, and
   `wsk_hit_word` says which word. **`rows` of a text item** are every line it
   is on; `wsk_rect` of one is the box around all its words.

### Profile

`WSK_NO_TEXT`, in `WSK_MINIMAL`; refused by name. Cost measured.

### Done criteria

1. Every earlier check and golden unchanged.
2. A text item wraps where §5g's word labels wrapped, rectangle for
   rectangle, at two widths — the same layout from a fraction of the storage.
3. A button inline among words lays out where the rules put it.
4. Runs: the paragraph's document is smaller than the same words as labels,
   says the same characters at the same places (checked at the pixels:
   byte-identical screens), and falls back to a word at a time when the gap
   is not one advance — a control.
5. Refusals: a word wider than the line, a bad character, a text item in a
   wrapping column, a string index out of range; each beside its control.
6. `hit` and `wsk_hit_word`; `rows` of a paragraph spanning lines.
7. Outside a wrapping row: a long label, truncated.
8. Profile: `WSK_MINIMAL` refuses it, byte-identical otherwise; cost; module
   links, double-linked.


### What it came to, 2026-11-11

Built the same day; criteria 1–8 met. **349 checks** in `src/run.sh
--module` (321 before): 23 in the suite, 3 at the pixels, 1 profile control,
1 module link. Every output of the commit before — 21 files — compared byte
for byte against the same run after: identical.

**The measurement that matters**: the 51-word paragraph of §5g, as **one
text item**, draws **exactly the pixels** the 51 word labels did — and in
**610 bytes of document, against 1,415**. Runs are most of that: a line of
words is one `<text>`. In the suite, eight words: two runs against eight
labels, 271 bytes against 394; with a gap that is not one advance, a word at
a time (the control).

**One structural change on the way**: the measuring functions took `nest`
beside the tree because a caller's child tree carries none; text needs `str`
the same way, so they now take the tree that carries both — the root or a
laid-out copy — instead of a list of its fields.

**§7's question.** Text items are **1,104 bytes** of the default
(`module_shape.c` 8,844, and 7,740 with `-DWSK_NO_TEXT`); wrap, which now
walks words as well as items, is 2,044. `WSK_MINIMAL` is 3,548, 12 bytes
more than before, which is the refusal.

**And so the default is now heavy for a small panel.** `module_shape.c` was
4,692 bytes before nesting and is 8,844 with nesting, wrap and text. The
C5 is untouched — it is `WSK_MINIMAL` — and a desktop will not notice; but
every whisker module on the P4 pays it, whether it nests, wraps or writes a
paragraph or not. Each has a switch (`WSK_NO_NEST`, `WSK_NO_WRAP`,
`WSK_NO_TEXT`), and §7.5 foresaw this: *"the right answer may eventually be
that the small profile is the one with a name."* A middle profile — a
panel's worth — is the obvious next profile, and is a decision.


---

## 5i. Iteration 9 — the toolkit's first: press, click, drag

**Written 2026-11-11, before any code.** §8 is the design; this is the
iteration, and the two places building it corrected §8.

### What §8 got wrong, found by reading the code

- **The toolkit may not write a widget.** §8.6 had it set a held button's
  mark — but a widget may live in a `static const` panel, which on RV-9 is
  flash, and a write there faults. **Trees are always built at runtime**
  (they hold pointers), so every piece of toolkit state goes in a tree: the
  held widget is **`held` on the root**, carried into nested copies as `nest`
  is, and drawn with the corner marks; a scroll is the tree's `scroll`, as
  ever. A widget is never written, so a panel stays `static const`.
- **A notice names its widget by tree and index** — `in`, `item` — as
  `wsk_hit_in` does, now that stacks nest (§5f), rather than by stack number.

### The API

    wsk_ui_t ui;               /* the caller's: whisker keeps nothing */
    wsk_ui_init(&ui, &root);
    n = wsk_feed(&ui, &ev, note, max, &y0, &y1);
    if (y1 > y0) wsk_render_rows(&root, y0, y1, sink, budget);

An event is §8.4's `wsk_event_t`; this iteration handles `DOWN`, `MOVE`,
`UP` and `WHEEL`, and `KEY`/`TEXT` return nothing until the field's
iteration. `wsk_render_rows` is new and small: the document for a band of
rows, which `wsk_render_clipped` always was underneath.

### The rules

1. **Press.** `DOWN` on a button holds it: `root.held` is set and its rows
   come back to repaint. `UP` over the same widget is a **`WSK_NOTE_CLICK`**;
   `UP` anywhere else is nothing. Either way the hold ends and is repainted.
   `DOWN` on anything that is not a button holds nothing, and `UP` on the same
   widget is still a click — a label or a word may mean something to the
   program; on a text item the notice's `value` is the word.
2. **Drag.** `DOWN` inside a scrolling stack — the deepest one under the
   point — arms a drag. A `MOVE` farther than `slop` along its axis (16 by
   default, from §5b's measured scatter) starts it: the hold is released, and
   from then the stack's `scroll` follows the finger, **clamped to its range
   by the toolkit** — layer 1 still refuses out of range (D11), and the
   toolkit is a caller that never asks it to. Each change is a
   **`WSK_NOTE_SCROLL`** with the new offset, and the stack's area to repaint.
   An `UP` after a drag is not a click.
3. **Wheel.** `WHEEL` scrolls the deepest scrolling stack under the point by
   `d`, clamped: a `WSK_NOTE_SCROLL`, or nothing where there is none.
4. **Repaint.** `y0..y1` is the union of every row the event changed, band
   snapped, or empty; one banded repaint per event.

### Profile

`WSK_NO_INPUT`, in `WSK_MINIMAL` — the C5 has nothing to press (D6, D16).
`WSK_PANEL` keeps it: a touch panel is what it is for.

### Done criteria

1. Every earlier check and golden unchanged; `held` unset draws as before.
2. Press, click, a release elsewhere (no click), a tap with jitter inside the
   slop (a click), a drag (scrolls, clamps both ends, no click), the wheel —
   each by a scripted sequence of events in the suite.
3. The rows each event returns, repainted alone, leave exactly what a full
   redraw does — at the pixels, for a press and for a drag step.
4. Profile: `WSK_MINIMAL` builds without the toolkit; cost in bytes.
5. `wnest` rewritten on `wsk_feed`, with a list long enough to drag, and
   shorter than it was; run on the P4 with a person.


### What it came to, 2026-11-11

Built the same day; criteria 1–4 met, 5 written and waiting for the board.
**378 checks** (355 before): 22 scripted event sequences in the suite, 6 at
the pixels. Every earlier output byte-identical — `held` unset draws as before.

**It passed first time, which is worth saying because of why**: the press,
the click rules, the slop, both clamps and the wheel are what `wscroll` and
`wnest` had been doing by hand, so the toolkit is code that had already run on
the glass, written once. At the pixels, a press, its release elsewhere and a
drag step each repainted only the rows `wsk_feed` returned and left exactly
what a full redraw does (`toolkit_press.ppm`, `toolkit_drag.ppm`).

**`wnest` on the toolkit**: its list doubled to twelve items so there is
something to drag, and the program is still 14 lines shorter — it no longer
finds, presses or repaints anything; it prints clicks and repaints rows. In
bytes, against the hand-written version built with the same header: 12,112
against 11,472, of which 384 are the six extra items, so **the toolkit costs
about 256 bytes more than doing it by hand** and adds dragging, the wheel and
the click rules. It costs nothing to a program that does not call it: every
function is `static inline`, and `module_shape.c` did not move.

**The stack**: whisker's own chain under `wnest` is 1,568 bytes, plus **304
for each level of nesting** — up from 256, because the drawing loop's frames
grew with wrap and text (§5g, §5h). Three levels deep it is about 2,180; the
8,192 declared for the board stands.

**On the P4, the same day**, fetched over WiFi and run with Matthew at the
glass: twelve clicks, each the widget under the finger — six list items,
then `i3` again after the list had moved, `yes`, `no`, `go`, `back`, `stop` —
and the list left scrolled 54 rows, so a drag scrolled rather than clicked.
A release elsewhere prints nothing by design, so whether it was tried is not
in the log.


---

## 5j. Iteration 10 — fields that edit, and focus

**Written 2026-11-11, before any code.** §8.9's second step: typing goes to
the focused field, a caret that moves, and the on-screen keyboard as the
P4's first real program.

### The problem, and the answer

§5i's rule — **the toolkit never writes a widget**, because a widget may be
in flash — meets a field whose text lives *inside* its widget. Writing it
anyway breaks the rule; sending only "a key was typed" notices hands the
editing back to every program, which is what a toolkit exists to stop.

**So editable text lives beside the tree, as `nest` and `str` do**: the
caller supplies writable buffers —

    typedef struct { char *b; short cap, len, caret; } wsk_buf_t;
    root.buf = bufs; root.bufs = N;

— and a field names one by index in `.sub`. A field **uses a buffer when the
root has buffers and its `.sub` is in range**; otherwise it is the inline,
read-only field of §5d, drawn byte for byte as before.

### The rules

1. **Focus** is `focus` on the root, as `held` is: the widget typing goes
   to. A focused field draws its caret — at the buffer's caret, not at the
   end — and the view slides to keep the caret in sight. This iteration
   focuses **fields only**; a focus mark for buttons is a later question.
2. **A tap on a field focuses it** and puts the caret at the character
   tapped. **A tap on anything else leaves focus alone** — on a touch panel
   the keyboard is buttons, and pressing a key must not take focus from the
   field it is typing into.
3. **`TEXT`** inserts its characters at the caret, if they fit the buffer
   and the window can draw them (§2.4's 32..126, no `<&>`); anything else is
   refused — nothing inserted, nothing changed. A notice **`WSK_NOTE_TEXT`**
   says the field changed.
4. **`KEY`**: `LEFT`, `RIGHT`, `HOME`, `END` move the caret; `BACKSPACE` and
   `DELETE` remove a character (a `TEXT` notice); `ENTER` is
   **`WSK_NOTE_ENTER`**; `TAB` and `BACKTAB` move focus to the next or
   previous editable field in declaration order, depth first — the order
   D0 kept stable for exactly this — with **`WSK_NOTE_FOCUS`**.
5. **Rows**: the field's own; on a focus change, both fields'.
6. **The host**: the Linux host grows `KEY`, `TEXT`, `MOVE` and `WHEEL`
   events (layer 3), so a person can type into whisker on a desktop with a
   real keyboard and drag with a real mouse.

### Profile

Part of the toolkit (`WSK_NO_INPUT`) and of the field (`WSK_NO_FIELD`); the
C5 has neither.

### Done criteria

1. Every earlier check and golden unchanged; a field with no buffer draws as
   before.
2. Insert, refuse (full buffer, bad character), move, delete both ways,
   home/end, enter, tab and backtab across nested stacks — scripted events,
   with the buffer and the notices checked.
3. A tap on a field puts the caret where the tap was; a tap on a button
   leaves focus where it was (the control for the keyboard).
4. The caret in the middle of a long field: the view keeps it in sight.
5. At the pixels: a keystroke's rows repainted alone equal a full redraw.
6. The Linux host types with a real keyboard (`host/linux/run.sh --edit`,
   a person) and its self-test feeds keys through the same path.
7. **`wkeys` on the P4**: a field and an on-screen keyboard, built on the
   toolkit — a key's CLICK becomes a `TEXT` event — run by a person.


### What it came to, 2026-11-11

Built the same day; criteria 1–6 met, 7 written and checked here, waiting
for the board. **407 checks** in `src/run.sh --module` (378 before): 37 in the
suite, 2 at the pixels, 1 module link; `host/linux/run.sh --test` adds 7 more
through the real SDL path. Every output of the commit before — 25 files —
byte-identical: a field with no buffer is §5d's field.

- **Editing**, by scripted events: insert, the all-or-nothing refusals (a
  character the window cannot draw, a full buffer), both deletes, home, end,
  enter, and Tab and Shift-Tab round three fields, one two stacks deep. A tap
  puts the caret on the character under it; a tap on a key leaves focus in
  the field — the control the keyboard rests on. At the pixels, each
  keystroke's rows repainted alone equal a full redraw, a focus change's
  two places included.
- **The host** (layer 3) grew `WH_MOVE`, `WH_WHEEL`, `WH_KEY` and `WH_TEXT`,
  and `host/linux/run.sh --edit` types into whisker with a real keyboard and
  scrolls a list with a real wheel. `q` no longer quits: it is a letter now.
- **`wkeys`** (`src/module_keys.c`), the P4's keyboard, run here first by
  `tools/evidence/keys/`, which reads each key's place out of the document
  whisker drew and taps it: "hi there", Left, Left, Del, then the digit layer
  — all as a person would.

**Two things the building found.**

1. **A whole keyboard does not fit `SRC_MAX`**: 43 keys and a field came to
   **4,298 bytes of 4,096** — the arithmetic CLAUDE.md carried ("115 bytes is
   all a field-plus-keyboard leaves") was right in kind. Answered the way a
   phone answers it, with **layers**: letters, and a 123 key that swaps three
   rows for digits and symbols; 34 keys at a time, **3,518 bytes**. The swap
   is a row's `item` pointed at another static array — trees are writable,
   widgets are never touched. The compact-keyboard decision in
   `roadmap.md` §4 is therefore still Matthew's, and no longer urgent.
2. **Struct assignment is `memcpy`, sometimes.** Twice a `wsk_rect_t` copied
   through a pointer compiled to a call the module cannot link — caught both
   times by the module build, not by a test. The toolkit now copies
   rectangles field by field (`wsk__rput`).

**Bytes**: `wkeys` is 16,596 under `WSK_PANEL` (18,984 with everything): 75
key widgets are 4.8 KB of it. Its own stack chain is about 2,500 counting
its nesting; declared 8,192.

**On the P4, the same day**, fetched over WiFi and typed on by Matthew:

    entered: hello i am matthew
    entered: hello i am matthew.  i can type 42

Letters, space, the period, and the digit layer for the 4 and the 2 — the
first text typed into an RV-9 board with no keyboard attached. The 8,192
bytes of stack held; not bisected.

Matthew's two questions after typing, and what they changed: *"I didn't see
any caps"* — there were none; **Shift** was added, one-shot as a phone's, a
third layer of static arrays (3,606 bytes on the glass); and *"what is Left
and Right?"* — they move the caret. Arrows would say so better, but `<` and
`>` cannot be drawn (§2.4) and the font has no arrows; drawing them as paths
is left for when the keyboard is revisited.

Shift on the board, the same afternoon:

    entered: My name is Matthew and I am 56 years old.

Three capitals, each one-shot, digits from the second layer and a full stop.


---

## 5k. Iteration 11 — the browser is a host

**Written 2026-11-11, before any code**, Matthew having said go and gone to
bed. §9.7: a browser is a full SVG renderer with a pointer and a keyboard,
so a page that shows whisker's documents and sends back events makes every
phone, tablet and laptop a whisker screen with nothing installed — and it
is the first desktop-tier screen (§9.9's step 4).

### The decision that shapes it: the browser is a HOST, not a new API

`host/linux/host.h` is the substrate contract — a window you write SVG to
with a row range, and events in. **The browser host implements exactly that
contract**, over HTTP, so every program written against it — `edit.c`,
`demo.c`, `input.c` — runs in a browser **unchanged**, by linking a
different host. That is the strongest test there is that the seam is in the
right place, and it costs whisker nothing: whisker does not know.

### How the contract maps

- **Out: a document and its rows.** `/w0`'s rule is that a document paints
  only its rows, over what was there. A browser that replaced the whole
  picture with each document would show a clipped repaint as a strip on
  black. So the page keeps a **canvas the size of the device**, draws each
  document into an image, and copies **only its rows** onto the canvas —
  the board's banded repaint, done by the page. The page is the host's, so
  it may do this; whisker still writes a complete SVG document every time.
- **In: events.** Pointer down, move (while pressed) and up; the wheel;
  keys a field cares about; text typed — the same `wh_event_t` as SDL's.
- **Transport, with no dependencies**: the page subscribes to
  **Server-Sent Events** for documents, and **POSTs** events, one short line
  each. Both are plain HTTP, so the server is a few hundred lines of C over
  sockets — no WebSocket handshake, no library. A browser that connects late
  is sent every document since the last full one, so it starts with the
  picture everyone else has.
- **Text.** Layout assumes a character is half its font size wide (§2.1);
  a browser's monospace is not. The page measures its monospace font and
  sets `letter-spacing` so that an advance is exactly half the size — the
  host making its renderer agree with the one whisker was written for,
  which is the host's job (§2.8). Not CSS in whisker: in the host's page.

### Done criteria

1. `host/web/` implements `host.h`; `edit.c` and `demo.c` link against it
   with no change to either.
2. A headless check, no browser: the page is served; a subscriber receives
   the first document; a POSTed press and typed text reach the program, and
   the document that comes back carries what was typed.
3. A real browser, headless Chrome in a throwaway profile: the page draws the
   program, a scripted click and typing change it, and a screenshot is left
   for a person (Matthew, in the morning).
4. A clipped repaint at the pixels: the canvas after it equals the canvas
   after a full redraw (the board's banded rule, kept by the page).
5. Nothing in `src/` changes; every check there still passes.


### What it came to, 2026-11-11 (overnight)

Built the same night; criteria 1–5 met. `host/web/host_web.c` is about 330
lines of C over sockets and one page of JavaScript, and `host/web/run.sh
--test` runs two sets of checks:

- **Without a browser** (`test.py`, 12 checks): the page is served; a
  subscriber is sent the whole picture first; typed text comes back as a
  **clipped** repaint of the field's rows; Tab and typing reach the second
  field; a click in the list arrives as a CLICK; a page that subscribes late
  is sent the picture since the last full document and sees what was typed
  before it came.
- **In headless Chrome** (`chrome.mjs`, 6 checks, a throwaway profile, driven
  through the DevTools protocol with Node's own WebSocket): real pointer and
  key events type "Hello web", click the city field, type "Paris" and press
  Enter, and the program prints `enter  city = "Paris"`. Then the check only
  a browser can make: **the canvas built from those clipped repaints equals
  the canvas after a full redraw, pixel for pixel — 0 differ.** The page keeps
  the board's banded rule exactly.

**`edit.c`, `demo.c` and `input.c` link against the web host with no change**
— which is the claim this iteration set out to test about where the seam is.
`edit.c` gained one branch, for `WH_EXPOSE`, which the SDL host never sends.

**Text**: the page measured its monospace at 0.6021 of the font size and set
`letter-spacing: -0.1021em`, so a character is exactly half its size wide,
as whisker lays it out; in `web_typed.png` the caret sits exactly after the
last letter of "Paris", which is the visible proof.

**Added to the contract**: `WH_EXPOSE`, X's Expose for X's reason — a screen
has come that has not seen the picture. The web host sends it when a page
subscribes.

**Not done, and named:** a phone's on-screen keyboard does not open for a
canvas (a hidden input would be needed, or a whisker keyboard like `wkeys`);
the page replaces pixels and so does not yet use the ids to patch elements
(§9.7's optimisation); and nothing here has been tried from a second machine,
though the host listens on every interface for exactly that.


---

## 5l. Iteration 12 — overlays: a menu over the window

**Written 2026-11-12, before any code.** `window-gap.md` §3 named this the
one item that touches the renderer's model rather than adding a branch to
it: `menu`, `popup`, `item` and `choice` all draw **on top of** other
widgets. Matthew: *"let's move ahead"*, and *"ensure whisker is as
maintenance free as reasonably possible"* — so the smallest model that
answers it, and no more.

### The model

**An overlay is a tree** — any tree in `nest`, laid out like any other —
**opened over the root at a place**: `root.over` is its index (or
`WSK_NONE`), and `root.ox, oy, ow, oh` is where it sits. One at a time; a
submenu is a later question.

1. **Placement** is layer 1 and pure: `wsk_over_open(root, sub, anchor)`
   sizes the overlay by its content — its widest item and its items' sum,
   with padding, never narrower than the anchor — and puts it **below the
   anchor**; if there is no room below, **above**; and moved left as far as
   it must to stay on the screen. Taller than the screen is refused,
   `WSK_E_FIT` (D10) — unless the overlay's own tree scrolls, in which case
   it is given the screen's height and scrolls.
2. **Drawing**: in the same document, **after** the root, so it is on top;
   with a **backing of its own** — the overlay's `bg`, under its items —
   because, unlike a stack, it lies over widgets rather than over the scene.
   A band repaint draws whatever of both lies in its rows, in that order, so
   repainting under an open menu stays right (§3's question, answered by
   the order of the document, which the board's band rule already keeps).
3. **Hit**: an open overlay is asked **first**. A point inside its
   rectangle is answered by it — a widget, or nothing in its padding — and
   **never falls through** to the widget beneath. Ids and paths find widgets
   in it as anywhere else (D19).
4. **The toolkit** (§5i): `wsk_ui_open(ui, sub, "anchor")` opens one under a
   widget. While it is open, a press **outside** it closes it, is a
   **`WSK_NOTE_CLOSE`**, and goes no further — the tap that dismisses a menu
   must not also press what is under it. A click on a button **in** it is
   the CLICK, and closes it (a menu's choice is made once). Escape
   (`WSK_KEY_ESCAPE`) closes it.
5. **What it is not**: a `choice` widget (a drop-down holding a selection)
   is not built here — it is a button whose click opens an overlay and whose
   label the program sets from the CLICK, which is program glue until a
   caller needs it to be whisker's.

### Profile

`WSK_NO_OVER`, in `WSK_MINIMAL`; `WSK_PANEL` keeps it — menus are a touch
panel's business too.

### Done criteria

1. Every earlier check and golden unchanged; no overlay draws as before.
2. Placement by hand: below, flipped above near the bottom, moved in from
   the right edge, too tall refused and the scrolling control accepted.
3. Document order: the overlay's backing and items come after the root's.
4. Hit: an item in it; its padding is nothing and **not** the widget beneath
   (the control); outside it, the widget beneath.
5. Toolkit: open, choose (CLICK then CLOSE, closed, rows); dismiss outside
   (CLOSE, and no click on the widget beneath — the control); press an item
   and slide off (no click, still open); Escape.
6. At the pixels: opening and closing, each repainted in its rows alone,
   equal a full redraw; closing leaves exactly the screen before opening.
7. A program a person can try: `edit.c`'s Save opens a menu, in SDL and in
   the browser, unchanged hosts.


### What it came to, 2026-11-12

Built the same evening; criteria 1–7 met. **433 checks** in `src/run.sh
--module` (407 before): 21 in the suite, 5 at the pixels; the browser check
grew by 2. Every output of the commit before — 26 files — byte-identical.

- **Placement**, by hand: under File, 0,40 106x108 — as wide as its anchor,
  as deep as its items; from a small anchor low on the right, flipped above
  and moved in from the edge; a menu taller than the screen refused, and the
  same menu, scrolling, given the screen's height.
- **The open question answered**: `window-gap.md` §3 asked whether a band
  repaint under an open menu stays right. It does, by the order of the
  document alone — the overlay after the root, on its own backing — and the
  board's band rule needs nothing new: at the pixels, a menu opened and then
  closed, each repainted in its own rows, equals a full redraw each time, and
  **closing leaves exactly the screen there was before it opened**.
- **Nothing falls through**: a point in the menu's padding is nothing, where
  with it closed the button beneath answers (the control); a press off the
  menu closes it and is no click on what is under it.
- **A person can try it**: `edit.c`'s Save opens "Save now / Save as... /
  Discard" — in SDL and in the browser, the hosts unchanged but for Escape,
  which the page now sends (`WH_KEY_ESCAPE`) and which closes a menu. Headless
  Chrome clicked Save, chose "Save now", and the canvas still equalled a full
  redraw afterwards (`web_menu.png`).

**§7's question**: 892 bytes to a toolkit program (`module_nest.c` against
`-DWSK_NO_OVER`); a small panel's default build grew about 800, to 9,640, and
`WSK_PANEL`'s to 7,540, since every document and every hit now asks whether an
overlay is open. `WSK_MINIMAL` is 28 bytes larger, the root's new fields being
set; it draws byte for byte as before. Out of `WSK_MINIMAL`, in `WSK_PANEL`.

**Not built, and named**: `choice` as a widget (a program's glue for now); a
submenu (one overlay at a time); and a focus model for menus from the
keyboard (arrows through items) — that waits for focus on buttons.


---

## 5m. Iteration 13 — check, selection, and focus on buttons

**Written 2026-11-12, before any code.** §8.9's third step, and the three
small things a real form needs that whisker still lacks: a **checkbox**
(`window-gap.md`'s `check`, 5 sites in Studio), a **selected row** in a list
(`table`, `tree` and every list that is a choice), and **Tab reaching
buttons** — today it reaches fields only (§5j rule 1).

### The rules, each answering to "the toolkit never writes a widget"

1. **`WSK_CHECK`**: a box and its label. Whether it is ticked is **state**,
   so it lives where editable text does — beside the tree, in the caller's
   memory: `root.state`, an array of bytes, and the check's `sub` indexes
   it. With no state array, a check is drawn unticked and is read-only.
   Drawn **in rectangles only** — a box in the trough colour, and when ticked
   a smaller box in the accent inside it — so it needs no `<path>` and costs
   the C5 nothing it does not already draw. Its label is left-aligned after
   the box, as a field's text is.
2. **A click on a check toggles it**: a **`WSK_NOTE_CHANGE`** with the new
   value, and its rows.
3. **Selection is a stack's**, not a widget's: a stack with `select` set
   keeps the index of its chosen item in `sel` (or `WSK_NONE`), in the
   tree — which is the caller's and writable. The chosen item is drawn in the
   stack's `selfill`. **A click on an item of a selecting stack selects it**:
   a **`WSK_NOTE_SELECT`** with the index, and the rows of the old choice and
   the new.
4. **Focus reaches buttons and checks.** Tab walks every **focusable**
   widget — field, button, check — in declaration order, depth first. A
   focused button or check is drawn with a **ring**: four thin rectangles in
   the ink colour, inset two pixels; `ENTER` on it is the CLICK (and toggles a
   check). A tap on a button still does not move focus (§5j rule 2 stands:
   the keyboard's keys are buttons).

### Profile

`WSK_NO_CHECK` drops the check kind; selection and the ring are part of the
toolkit (`WSK_NO_INPUT`). `WSK_MINIMAL` keeps none of them; `WSK_PANEL` all.

### Done criteria

1. Every earlier check and golden unchanged.
2. A check drawn unticked and ticked, at the bytes and at the pixels; read-only
   without state; refused by `WSK_MINIMAL` by name.
3. Toolkit: a click toggles a check (CHANGE, rows); a click on a list item of
   a selecting stack selects it (SELECT, both rows); a click on a plain
   stack's item selects nothing (the control).
4. Tab order across fields, buttons and checks, depth first; the ring drawn
   on the focused button and not on a field; ENTER on a focused button is a
   CLICK, on a focused check a toggle.
5. At the pixels: a toggle and a selection, repainted in their rows alone,
   equal a full redraw.
6. `edit.c` gains a check and a selecting list, in SDL and the browser.


### What it came to, 2026-11-12

Built the same evening; criteria 1–6 met. **457 checks** in `src/run.sh
--module` (433 before): 20 in the suite, 2 at the pixels, a profile control
and a module link; the browser check grew by 2. Every output of the commit
before — 27 files — byte-identical.

- **The check**, in rectangles only: `<path>` nowhere, so nothing the C5
  cannot draw — though the C5's profile refuses it by name, as a control.
  **Found by looking**: the first picture drew an unticked box in the trough
  colour alone, and on the scene it was all but invisible; no test could have
  said so. It now has a border in the ink, the trough inside, and the tick
  inside that.
- **Selection** is a stack's, in the caller's tree: a tap selects, the old
  choice's rows and the new one's repaint, and a stack that does not select
  is only clicked (the control).
- **Focus reaches buttons and checks**, with a ring of four thin rectangles;
  Enter on a focused button is its click and on a check its toggle. **This
  moved three checks of §5j on purpose** — they were written when Tab
  reached fields only, and now step over the Q and W buttons in their row;
  each was rewritten and says why.
- **At the pixels**, a toggle and a choice each repainted in the rows
  `wsk_feed` returned equal a full redraw.
- **`edit.c`** has a "Remember me" check and a list that keeps a choice; in
  headless Chrome a click ticked it (`change remember = 1`) and chose a row.

**§7's question**: the check is 508 bytes of the default. A small panel's
default build is now **10,540**, `WSK_PANEL`'s **8,404**, and `WSK_MINIMAL`'s
3,608 — 32 more than the iteration before, the root's new fields being set.


---

## 5n. Iteration 14 — table columns: the header decides

**Written 2026-11-12, before any code**, Matthew having said go and gone to
bed. `window-gap.md` §7 put `table`'s columns first of what Studio still
lacks: rows, selection and length were answered (§5e, §5m), and what was
missing is that a table's cells line up.

### The decision: columns belong to the header

Rows are separate trees (§5f), so each lays its cells out by itself, and
cells sized by their content would not line up. Two ways to make them:
measure every row and take each column's widest — which a sliced table
(§5e), holding only the rows in view, would answer differently as it
scrolled, so columns would jump — or **let the header decide**, which is
what a table is: the header's cells are sized by `size` and `weight` as any
row's are, and **every row that names the header takes each cell's
position and width from the header's cell of the same number.**

1. **`head` on a stack** names a tree in `nest` — the header. Every row
   (a `WSK_ROW` stack) inside that stack, at its first level, lays out its
   cells at the header's columns, measured across the row's own area. The
   header may be anywhere — above a scrolling body, typically, so that it
   does not scroll away.
2. **A row with more cells than the header has columns is refused**,
   `WSK_E_FIT`, naming the first cell without a column (D10).
3. **Cells read as a table**: a stack with `talign = WSK_START` draws its
   labels' and buttons' text from the start of the cell, not centred.
4. **A row is chosen as an item is**: a selecting body (§5m) draws its chosen
   row's backing in `selfill`.

Not here: sorting (a header cell's CLICK is the program's to act on), a
draggable column edge (a host's divider, `window-gap.md` §4), and cells
spanning columns.

### Done criteria

1. Every earlier check and golden unchanged.
2. Three rows under a header of a weighted, a fixed and a weighted column:
   every cell's rectangle, by hand, under its header cell — and the same
   rows without `head`, misaligned (the control).
3. The header above a scrolling body: scrolled, the cells still line up.
4. The refusal, beside its control.
5. Start-aligned text, by the bytes.
6. At the pixels: a table, a selected row, and a person looks.
7. `edit.c`'s list becomes a table — Name and Size, under a header — in SDL
   and the browser.


### What it came to, 2026-11-12 (overnight)

Built the same night; criteria 1–7 met. **472 checks** in `src/run.sh
--module` (457 before): 12 in the suite, 4 at the pixels. Every output of the
commit before — 28 files — byte-identical. Host checks and browser checks
pass.

- **Columns by hand**: a header of a weighted, a fixed and a weighted column
  over six rows — every cell under its header cell, at 0/202, 202/80,
  282/118 — and the **control**: the same rows without `head` lay their
  cells out by their own text, and the second cell lands at 72 in one row and
  8 in the next.
- **Scrolled**, row 1 at the top of the body, its cells still in their
  columns: the header is laid out across each row's own area, so a scroll
  moves rows and never columns.
- **Selection through a cell**: the toolkit now finds the deepest selecting
  stack under the finger, so a tap on any cell of a row chooses the row — a
  CLICK on the cell and a SELECT on the body. §5m's tests still pass on the
  same code.
- **At the pixels** (`table_chosen.ppm`): a file list, Name / Size / Date,
  alternate rows shaded, the fifth chosen by a tap on its size cell; the old
  choice and the new repainted in their rows equal a full redraw. A person
  should look.
- **`edit.c`'s list is a table** — a header that stays, a body that scrolls
  and chooses — in SDL and the browser; headless Chrome chose a row.

**§7's question**: a small panel's default build is 10,516, `WSK_PANEL`'s
8,520. **`WSK_MINIMAL` grew 88 bytes, to 3,720**: start-aligned text is in
every tier — a C5 readout of values in columns can use it — and the root sets
three more fields. Measured and recorded, not hidden; if the C5 ever needs
those bytes, `talign` is the one to put behind a switch.

**Not built**: sorting (a header click is the program's to act on), a
draggable column edge (a host divider), cells spanning columns.


---

## 5o. Iteration 15 — a memo: many lines, edited

**Written 2026-11-14, before any code.** Matthew: *"which ever order you
feel is best"* — so `memo` before shapes: it is Studio's (7 sites,
`window-gap.md` §7), it is §5j's machinery with lines, and it needs no board.

### The rules

1. **`WSK_MEMO`** is a field of many lines: the same caller-owned
   `wsk_buf_t` (§5j), the lines separated by `\n`. A memo with no buffer is
   read-only and shows its inline text as one line.
2. **The view.** Lines are a font and a quarter apart. The buffer gains
   **`top`**, the first line shown — caller memory, as the rest of the buffer
   is — so a memo scrolls where it was left and moves only as far as it must
   to keep the caret in sight. Drawing never writes it: a `top` that would
   hide the caret, or run past the end, is drawn as the nearest one that
   would not. **Long lines are not wrapped**: every line is shown from the
   same column, chosen as a field chooses its own (§5j) so the caret's column
   is in sight — a code editor's behaviour, and soft wrap is a design of its
   own, left until something needs it.
3. **Keys.** `ENTER` inserts a line break (it is not a notice in a memo);
   `UP` and `DOWN` move a line, keeping the column where the line is long
   enough; `HOME` and `END` go to the start and end of the caret's line; the
   rest are §5j's. A tap puts the caret at the line and column under it.
4. **What the window cannot draw** is still refused (§2.4): a memo's buffer
   holds 32..126 and `\n`, and nothing else.
5. **Rows**: an edit repaints the memo's rectangle.

### Profile

`WSK_NO_MEMO`, in `WSK_MINIMAL`; `WSK_PANEL` keeps it — a note on a touch
panel is editing, not prose layout.

### Done criteria

1. Every earlier check and golden unchanged.
2. Typing across lines, Enter, Up and Down keeping the column (and clamping
   on a short line), Home and End within a line, Backspace joining two lines
   — by scripted events, the buffer checked.
3. The view: `top` follows the caret down and up; a stale `top` is drawn
   as the nearest that shows the caret, and not written.
4. A tap puts the caret at the line and column under it.
5. A buffer holding a character the window cannot draw is refused.
6. At the pixels: a keystroke's repaint equals a full redraw.
7. `edit.c`'s Note becomes a memo, in SDL and in the browser.


### What it came to, 2026-11-14

Built the same day; criteria 1–7 met. **495 checks** in `src/run.sh
--module` (472 before): 18 in the suite, 2 at the pixels, a profile control,
a module link, and a new step that builds **every profile and switch**
under `-Werror`. Every output of the commit before — 30 files —
byte-identical. Host checks, web checks and browser checks pass.

- **Editing many lines**, by scripted events: Enter breaks a line and is a
  TEXT notice, not ENTER; Up and Down keep the column and clamp on a short
  line; Home and End are the line's; Backspace at a line's start joins it to
  the one above; a tap lands on the line and column under it.
- **The view**: eight lines in a memo of five, the caret on the last — lines
  4..8 drawn, and the buffer's stale `top` of 0 left as it was; five lines up
  the toolkit writes `top` = 2 and one line down leaves it there.
- **At the pixels** (`memo.ppm`): three lines typed, a line broken, the caret
  moved up and a character put in — each keystroke repainted in its rows
  equal to a full redraw. Over HTTP, two lines typed into `edit.c`'s Note come
  back as two `<text>` lines.

**Found on the way, and not the memo's**: building every switch found two
combinations broken since earlier iterations — `-DWSK_NO_FIELD` since §5m
(the toggle sat in the field-only block) and `-DWSK_NO_SCROLL` (a rectangle
the compiler could not prove was set). Neither tier used either, so nothing
had compiled them. Both fixed, and **`src/run.sh` now builds all thirteen
profiles and switches every run**, so a combination cannot break unseen
again.

**§7's question**: the memo is **668 bytes** of the default, measured by
building without it (a small panel is 11,272 in all, `WSK_PANEL` 9,196);
`WSK_MINIMAL` did not move, 3,720.

**Not built**: soft wrap of long lines (a design of its own); the wheel over
a memo; selecting a range of text, and so the clipboard, which is also the
host's.


---

## 5p. Iteration 16 — a tree is depths over a flat list

**Written 2026-11-14, before any code**, after reading how gBASIC Studio's
window says `tree` (Studio `94b36e3`, `lib/studio_shell_qt.bas`, the browser
pane): *"A TREE that shows a list ... The shape comes from a DEPTH COLUMN that
is not drawn ... over a FLAT list, which is exactly what a program owning its
own expansion hands over."* And: *"STUDIO OWNS EXPANSION AND THE WIDGET DOES
NOT, which is not a preference"* — gBASIC measured that a widget owning
expansion makes the program's row indices and the widget's disagree after a
collapse. The disclosure glyphs are characters in the row; clicking the row
toggles it, in the program.

So whisker's tree is what Studio asks of one, and no more:

1. **A widget has a `depth`** — a byte, in the struct's padding, so a widget
   is still 64. **A stack has an `indent`**: pixels a level. A widget at
   depth *d* in a stack with an indent is laid out *d* × `indent` in from the
   start, and narrower by as much. A stack with no indent lays out as ever.
2. **In a table** (§5n) a row's depth indents **its first cell only**, so the
   other columns stay under the header.
3. **Expansion is the program's.** It hands over the visible rows; a click
   on one is the CLICK and SELECT it already is (§5m), and the program
   rebuilds the rows. Length is `wsk_slice` (§5e).

**Found by reading, and recorded, not built**: Studio's disclosure glyphs,
`▾` and `▸`, are not ASCII, and whisker refuses them (§2.4: `/w0` draws
32..126). A browser host could draw them. That is D21's runtime half — a host
that has more saying so — and wants its own design before any host is
trusted to draw what `/w0` cannot. A whisker program writes `+` and `-`.

### Done criteria

1. Every earlier check and golden unchanged; a stack with no indent and
   widgets with no depth lay out as before.
2. A list at depths 0, 1, 2, 1, 0: every rectangle by hand.
3. A table whose rows have depths: the first cell indented, the rest under
   the header.
4. A program that owns expansion: a click toggles a node, the rows are
   rebuilt, and what is shown is what the program handed over.
5. At the pixels, a tree a person can look at; `edit.c` gains one — a
   folder tree — in SDL and the browser.


### What it came to, 2026-11-14

Built the same day; criteria 1–5 met. **507 checks** in `src/run.sh
--module` (496 before): 11 in the suite. Every output of the commit before —
31 files — byte-identical: no indent, no depth, no change.

- **A list at depths 0, 1, 2, 1, 0** under an indent of 16: in by 0, 16, 32,
  16, 0 and narrower by as much — and the control: no indent, no change.
- **In a table, the first cell only — and found by its test**: the first
  version indented the *row* in its body as well as the row's first cell, so
  the cell moved twice and the header's columns, laid across the shifted
  row, moved with it. A body that has a header now never indents its rows;
  the check asserts the row itself stays at 0.
- **Expansion is the program's**, tested as Studio does it: a model the test
  owns, taps through the toolkit, SELECT notices on which the program opens
  or closes a node and hands the rows over again. Closing `docs` hides
  `old` and `gtk.md` with it while `old` stays open in the model — which is
  exactly gBASIC's point: the shape is the program's, the rows are what it
  shows.
- **`edit.c`'s table is a file tree**: folders open and close on a tap, files
  indented under them, the Size column still under its header. Headless
  Chrome opened `docs`.

**§7's question**: indentation is a few lines in the column layout every tier
shares, so `WSK_MINIMAL` grew 72 bytes, to 3,792 — a C5 readout of nested
values can use it. Default 11,472, `WSK_PANEL` 9,336.

**Recorded, not built**: Studio's `▾` / `▸` are not ASCII and `/w0` cannot
draw them; whisker refuses them by name. A browser could. That is D21's
runtime half, wanting its own design.


---

**Found in use, 2026-11-14**: a folder clicked a second time, while its row
was still the chosen one, did not close — Matthew, in the desktop window.
`edit.c` opened and closed folders on **SELECT**, and a click on the row
already chosen selects nothing new, so no notice came. A toggle belongs on
**CLICK**, which every click is; SELECT says the choice moved. The toolkit
was right and the program asked it the wrong question; `host/web/test.py`
now clicks a folder twice, and fails on the old program.

## 5q. A world: a view onto a space larger than the screen — designed, not built

**Written 2026-11-14, design only**, at Matthew's asking: *"a container that
can window its physical size over a larger virtual size and zoom in/out.
Inside would be any other kinds of widgets"*, with **fixed, continuous or
toroidal** space. Name: **world** (Matthew). Pinch-to-zoom **later**.

### Who it is for, and what each asks

- **Games** — sprites that move every frame, overlapping; a toroidal field.
- **Flow charts, large diagrams** — a big fixed space, panned and zoomed.
- **Large images** — which `/w0` cannot draw at all (it has no `<image>`);
  a browser host could. D21's runtime half again: a host that has more.
- **Matthew's own: a virtual conference in gBASIC.** Each attendee an avatar
  walking a floor plan; posters (poster sessions) on the walls; nearby
  avatars visible, and **text between people shown above them, shrinking and
  fading with distance**, so that a virtual conference keeps the
  serendipity and exploration of a real one. Three requirements come from it
  and shape everything below:
  1. **A point on the floor means something** — click where to walk — so
     whisker must turn a screen point into a *world* position, not only into
     a widget.
  2. **Every placed thing has its own scale and its own fade.** The program
     decides how far is how small and how faint — that is the application's
     policy, not whisker's — and whisker draws what it is told.
  3. **A poster is a whole panel** — title, abstract, author, a stack of
     widgets — and it must zoom as one: sizes, padding, gaps and fonts.

### The model

1. **A world is a stack whose children are placed, not stacked.** `layout =
   WSK_PLACE` on a tree; its `item` array is the widgets, any kind,
   **including stacks** (a poster). They may **overlap**: array order is
   drawing order, later on top, and `hit` asks the topmost first — the first
   container in whisker where that is true.
2. **Places live beside the tree, in the caller's memory**, as buffers and
   state do (§5j, §5m): `place[i] = { int32 x, y; short w, h; scale; fade }`.
   A game moves a sprite by writing two numbers; whisker never writes a
   widget. **32-bit** world positions — a continuous world outgrows a short
   (§5e found the 32,767 wall once already) — converted to screen positions
   only for what is in view.
3. **The view is the tree's**, as `scroll` is: `vx, vy` (32-bit, the world
   point at the view's top left) and `zoom` (fixed point, 256 = 1:1; a module
   has no floating point and no 64-bit division). A child's screen rectangle is
   its place, less the view, times the zoom, times its own scale.
4. **Topology**, `world` = one of three:
   - **`WSK_BOUNDED`** — a fixed size; the view may not leave it. Out of range
     is **refused** (D11), and the toolkit clamps a drag, as it does a scroll.
   - **`WSK_OPEN`** — no edges.
   - **`WSK_TORUS`** — a fixed size that wraps both ways: positions are taken
     modulo the size, the view wraps, a thing straddling the seam is drawn
     **twice** (up to four times at a corner), and `hit` wraps with it.
5. **Everything inside scales.** A placed widget's rectangle, and — for a
   stack inside — its padding, gap and font, by the zoom times its own scale.
   RV-9 draws text at any size, so a font scaled is a font drawn.
6. **Fade is a colour, not an opacity.** `/w0` has no opacity (it is in
   *What whisker must never require*), so `fade` blends a widget's ink and
   fill toward the world's background by that fraction. On `/w0` it looks
   like fading; a host that has opacity could do it properly (D21).
7. **The view clips** — both axes, as a nested scrolling pane already does
   (§5f) — and only what is in view is emitted, so a conference hall of a
   thousand avatars costs the document only the dozen nearby. Text is whole
   or not drawn at the view's edge (D12).
8. **Screen to world**: `wsk_world_at(tree, x, y, &wx, &wy)` — the inverse
   of rule 3, toroidal wrap included — so a click on bare floor is a place to
   walk to.

### The toolkit (§5i), later than the drawing

A drag on bare floor pans; the wheel zooms about the pointer (zoom anchored
at a point, the view's centre being one such point); pinch is later, needing
two fingers, which the piano showed the panel reports well. A moved item
repaints its old rows and its new; a pan or a zoom repaints the view.

### On each tier (D21)

- **Desktop and browser**: all of it, smoothly; a browser host could zoom
  with an SVG transform instead of whisker's arithmetic.
- **The P4**: a moving sprite is cheap (its own rows); a pan or zoom of a
  full-screen view is the ~200 ms §5e measured — so a game there moves its
  sprites under a still view, or keeps its view small.
- **The C5**: none of it (`WSK_NO_WORLD`, in `WSK_MINIMAL`).

### Done criteria, for when it is built

1. Every earlier output unchanged.
2. Placement, view and zoom: rectangles by hand, at 1:1, zoomed in and out.
3. Overlap: draw order and hit order, with the topmost answering.
4. Each topology: bounded refusing a view off its edge; open far from the
   origin (past 32,767); a torus drawing a seam-straddler twice and hitting
   both halves.
5. Scale and fade per item: a label's font scaled, its ink blended — by the
   bytes and at the pixels.
6. A poster — a stack inside — zoomed as one.
7. `wsk_world_at`, toroidal wrap included.
8. A demo: a small toroidal game on the P4, and a conference floor in the
   browser — avatars walking, speech above them fading with distance.

### Step 1, written before its code (2026-11-15): placing, viewing, zooming

Built overnight under Matthew's "whatever else you can do", in steps so
each lands whole. **Step 1 is the drawing and the hit**; the torus, posters
and fade are step 2, the toolkit's pan and zoom and the demos step 3.

- `overflow = WSK_WORLD` makes a tree a world (the field that already
  chooses scroll and wrap, which also change how a stack lays out).
- `wsk_place_t { int x, y; short w, h; short scale; unsigned char fade; }`,
  one per item, `place` on the tree — **the caller's, writable** (a game
  moves a sprite by writing x and y). `scale` 256 is 1:1, as `zoom` is.
- The view: `vx`, `vy` (`int`) and `zoom` (`short`, 256 = 1:1) on the
  tree; `wspace` = `WSK_OPEN` or `WSK_BOUNDED` with `ww`, `wh`.
- An item's rectangle: its place less the view, times the zoom, from the
  area's corner; its size times the zoom times its scale. 64-bit products,
  shifted, never divided (the module rules); one too far away to be a
  coordinate is put wholly outside the area, so nothing far off can wrap a
  `short` onto the screen (§5e's lesson).
- **Clipped both ways** to the area — a box cut, text whole or not drawn,
  as a scrolling pane does along its axis; a host that clips is given a
  clip (§5r).
- **Text scales with the item**: a world draws in its font times the zoom,
  and an item whose own scale is not 1:1 in that times its scale, in a
  `<g font-size>` of its own. Its pad scales with it.
- **Overlap**: drawn in array order, **hit topmost first** — the reverse.
- **`wsk_world_at`**: a point on the screen to a point in the world, for a
  click on bare floor.
- **Refused**: no `place`, a zoom or scale ≤ 0, a bounded view leaving its
  world (`WSK_E_SCROLL`, as a scroll past its end is), and — in this step
  — **a stack inside a world** (`WSK_E_KIND`): unscaled, a poster would
  overflow its own rectangle at any zoom below 1:1, and refusing it until
  step 2 scales it is the honest answer.
- **`WSK_NO_WORLD`**, set by `WSK_PANEL` and `WSK_MINIMAL` for now — the P4
  game can claim it when it exists.

### What step 1 came to, 2026-11-15

**823 checks** (793 before): 27 in the suite, 2 at the pixels, the
`-DWSK_NO_WORLD` link; **23** profiles and switches build. Every earlier
output — 39 files — byte-identical.

- **As written, with one consequence of the build**: an item is drawn
  through a copy of the world holding it alone, its font and pad scaled —
  so every drawing function scales it without knowing, and the widget is
  the same widget, so focus, a held button and a field's buffer still find
  it. Copies are nesting's (`wsk__tcopy`), so **`WSK_NO_NEST` implies
  `WSK_NO_WORLD`** — a world's posters will be stacks anyway.
- In the suite: places at 1:1, the view moved (an item cut to 60 wide at
  the edge, its text not whole and not drawn — and drawn, the control, once
  moved wholly in), zoom 2 and ½, an item's own scale 2 with a
  `<g font-size="32">` of its own and its neighbour without; drawn in array
  order, **hit topmost first** — where Ann and Bo overlap, Bo, and a click
  there is Bo's; an open world **two billion units out**, with an item two
  billion away nowhere rather than wrapped onto the screen;
  `wsk_world_at` back to 32-bit world points, zoomed; a bounded view at 680
  of 1,000 inside and at 681 refused (`WSK_E_SCROLL`); zoom 0, scale 0 and a
  stack inside refused by name; for a host that clips, a clip, and the
  half-out label drawn for it to cut.
- **At the pixels through RV-9's rasteriser**: two buttons saying "Hello"
  under a zoom of 1.5, one at its own scale 2 — 90 x 30 and 180 x 60 in
  their fills, and the word **57 px wide at 24 px and 115 at 48**: twice,
  within a tenth. Looked at.

**§7's question**: the world is **1,156 bytes** in the default (16,388 for
a small panel); `WSK_PANEL` 10,348 and `WSK_MINIMAL` 3,792 — **4 bytes
each**, the check that refuses a world by name there, which is what a
reduced build owes (§7).

**Step 2**, named: the torus (seam-straddlers drawn twice, hit and
`wsk_world_at` wrapping), posters (a stack inside, its sizes, padding, gap
and font scaled as one — refused until then), and fade. **Step 3**: the
toolkit's pan and zoom, and the demos.

### Step 2, written before its code (2026-11-15): the torus and fade — and why not posters yet

**Posters are not in this step, and the reason is the code, not the
time.** Scaling a stack inside a world means every measurement of it —
a widget's `size`, a stack's `pad` and `gap`, a text's width, a button's
default 64 rows, a wrap's lines — multiplied by one factor at every depth.
Those measurements read the caller's nested trees directly (`wsk__nat_on`
measures `nest[sub]`, not a copy), so the factor has to be threaded through
the measuring functions themselves: a change to the core of layout, every
profile's, which deserves Matthew's eye before it is made rather than after.
**Proposed for step 2b**: the factor on the tree that carries `nest` (the
`r` every measurement already takes), applied where each measurement
returns, 256 for every tree that is not inside a world — so the arithmetic
is one multiply-and-shift that 1:1 leaves byte-identical.

**The torus** (§5q rule 4): `wspace = WSK_TORUS`, with `ww` and `wh`.

1. An item's place is taken modulo the world from the view: its offset
   from `vx` wrapped into `[0, ww)`, likewise down. That is its rectangle —
   `wsk_rect` answers one.
2. **An item across the seam is drawn twice** — also at its offset less
   `ww` — and at a corner four times; each copy is cut by the view as any
   item is. Hit tries every copy, topmost item first.
3. `wsk_world_at` wraps too: its answer is in `[0, ww) x [0, wh)`.
4. The view may be anywhere (it wraps), but **not larger than the world**
   — a view seeing an item twice side by side is two worlds — so a view
   wider or taller than the world is refused, `WSK_E_FIT`.

**Fade** (§5q rule 6): `place.fade`, 0..255, blends an item's colours
toward the world's background by that fraction — its fill and ink, and the
inherited ones (the window's ink, accent and trough) — because `/w0` has
no opacity. 0 changes nothing, byte for byte; 255 is the background. A
field's caret and selection inside a faded item are not faded (named).

Done when: wrapping both ways, a straddler drawn twice and four times and
hit on each copy, `wsk_world_at` wrapping, the view-size refusal; fade at
0 byte-identical, at 128 halfway — by the bytes and at the pixels.

### What step 2 came to, 2026-11-15

**840 checks** (823 before): 15 in the suite, 2 at the pixels. Every
earlier output — 40 files — byte-identical.

- **One thing the picture taught, about tori and not about code**: offsets
  are taken from the view, so an item is seen at both edges of a view only
  when it is **wider than what the world has over the view** (`ww` less the
  view's width). The first picture's check expected an item at a 360-wide
  world's seam to show at both edges of a 320-wide view at 0 — it cannot;
  the picture showed one piece, correctly, and the check was rewritten to a
  view where it must show twice (10 px at each edge, every pixel).
- In the suite: an item wrapped from the view's far side to a copy cut at
  the left edge, and hit there; seen from the other side of the seam,
  whole; at a corner, a copy a world back both ways; in a world barely
  bigger than its view, an item across both seams drawn **four times** and
  hit on each (the middle, bare, the control); `wsk_world_at` wrapping
  450 to 50; a view at -1000 accepted, and one zoomed out to see more than
  the world refused (`WSK_E_FIT`).
- **Fade by the bytes**: at 0 the fill exactly; at 128 each channel
  `(c x 127 + bg x 129) >> 8` — `#808080` over `#102030` is `#474f57`, and
  the inherited white text `#868e96`; at 255 the background. My first
  expectations were hand arithmetic, and wrong; the printed document was
  right to the bit, and the checks now say how it is computed.
- **At the pixels**: the faded fill, every pixel, the blended colour in
  RGB565; the seam's two 10-px pieces, every pixel, and nothing between.

**§7's question**: the world is now **1,848 bytes** in the default (17,080
for a small panel: the torus and fade +692 — fade is asked by every box and
label, a branch on a byte that is 0 outside a world); the board tiers
unchanged.

**Left, named**: posters (step 2b, above — a scale through layout, for
Matthew's eye first), and the toolkit's pan and zoom with the demos
(step 3).


---

## 5r. Iteration 17 — a host that has more says so (D21's runtime half)

**Written 2026-11-14, before any code**, Matthew having said go and gone out.
D21 put whisker's tiers in the build; this is the same principle at run
time. Everything whisker has drawn until now is what `/w0` can draw: ASCII,
one monospace face, no image, no clip, no opacity. Three iterations found the
cost of that being the only answer — Studio's `▾`/`▸` refused (§5p), a label
popping rather than sliding at a scroll's edge (D12), fade without opacity
(§5q) — and the browser host (§5k) can do all of it.

### The rule

**A host declares what it can draw beyond `/w0`; whisker uses no more than
that; and a tree that needs what its host lacks is refused, by name.** The
declaration is a set of bits on the root — `root.caps` — that a program fills
from its host (`wh_caps()` in the host contract; 0 on RV-9, whose modules
need not say anything). **With `caps` = 0 every document is byte for byte what
it was**, which is the first done criterion and the reason this is safe.

| bit | what the host can do | what whisker does with it |
|---|---|---|
| `WSK_CAP_UTF8` | draw text beyond ASCII | accepts UTF-8 in labels, buttons and text items; measures a character, not a byte |
| `WSK_CAP_IMAGE` | draw `<image>` | draws `WSK_IMAGE`, a picture from a URL; without the bit a tree with one is refused, **`WSK_E_HOST`** |
| `WSK_CAP_CLIP` | clip to a rectangle | a scrolling stack's content is clipped rather than cut: a label half out of view **slides** (D12's open question) |
| `WSK_CAP_OPACITY` | draw transparency | declared now; the world's fade will use it (§5q) |

### What is not changed, on purpose

- **Fields and memos stay ASCII.** Editing UTF-8 — a caret that steps over a
  character of several bytes, input that composes — is its own work, and a
  field that half-handles it is worse than one that refuses.
- **Monospace is still the measure.** A character is half its font size wide,
  whatever script it is in; a host with proportional fonts is the next step
  of this rule, not this one.
- **No substitution.** A host without `WSK_CAP_UTF8` is not sent `?` for `é`:
  the tree is refused (`WSK_E_TEXT`), as it always was.

### Done criteria

1. With `caps` = 0, every earlier output byte-identical.
2. UTF-8: accepted with the bit and refused without; widths counted in
   characters — `"▾ src"` is five wide, not seven; truncation never splits a
   character.
3. Images: drawn with the bit, refused by name without; an href that would
   break the document (`"`, `<`, `>`) refused.
4. Clipping: a scrolled pane with the bit draws a `<clipPath>` and its label
   half in view, and without the bit exactly what it did.
5. Hosts: SDL and RV-9 declare nothing; the browser host declares UTF-8,
   images and clipping, and in headless Chrome Studio's arrows draw, an image
   draws, and the canvas still equals a full redraw.
6. Profile and cost, by measurement.


### What it came to, 2026-11-14 (evening)

Built the same evening; criteria 1–6 met. **523 checks** in `src/run.sh
--module` (507 before): 18 in the suite; the every-switch build now covers
**15** profiles and switches. Every output of the commit before — 31 files —
byte-identical, which is criterion 1 and the point: a host that declares
nothing gets exactly what whisker always drew.

- **UTF-8**: refused without the bit, as ever, and never substituted;
  accepted with it; **measured in characters** (`▾ src` is 5 wide, not 7
  bytes); truncated to whole characters (three Greek letters are six bytes,
  never five); and malformed UTF-8 — a stray continuation byte, an overlong
  spelling, a character cut short — refused even by a host that draws UTF-8.
- **Images**: `WSK_IMAGE`, its URL by index in `str`; refused **by name,
  `WSK_E_HOST`**, for a host that has not said it can draw one; an href that
  would end its attribute refused. Drawn whole or not at all unless the host
  clips — a cut rectangle would squash a picture, not crop it.
- **Clipping**: a scrolling stack's widgets inside a `<g>` clipped to its
  area, drawn whole, so a half-visible label **slides** — D12's open question,
  answered for a host that can. The control: without the bit, the same pane
  is cut by arithmetic exactly as before. `hit` still cuts by arithmetic.
- **The hosts**: `wh_caps()` joins the contract. SDL — RV-9's rasteriser —
  says nothing; the browser says UTF-8, images and clipping, and serves
  `static/` (nothing above it). An SVG drawn as an image may load nothing
  from outside itself, so **the page fetches each image once and inlines
  it**. `edit.c` asks its host and uses what it has: Studio's own `▾`/`▸` in
  the tree and a logo in the browser, `+`/`-` and no logo under SDL — the
  same program, more where there is more (D21).

**§7's question**: the default is **12,576** for a small panel (11,472
before). `WSK_PANEL` leaves out images and UTF-8 — the P4's own screen draws
neither — and is **9,784**, the rest being the clip, which costs nothing until
a host claims it. `WSK_MINIMAL` **3,784**, eight bytes *less*.

**Not done**: opacity has its bit and no user until the world (§5q);
proportional fonts are the next step of the same rule; fields and memos stay
ASCII.

---

## 5s. Iteration 18 — a desktop's plumbing: right-click, selection, the clipboard, shortcuts

**Written 2026-11-14, before any code.** Matthew: *"The tree looks great. go
ahead next."* Next on the list agreed that morning was the host plumbing that
§6.1's bar 2 names under items 2 and 3. Of those, this iteration takes the
three that come up whenever a person sits at a desktop: **right-click**,
**the clipboard** (which needs **selection** in a field before it has
anything to copy), and **keyboard shortcuts**. File dialogs, a second
window, a draggable divider, undo and input methods are not in it.

### The rules

1. **A secondary press is its own event, not a button number.**
   `WSK_EV_MENU` at a point, which a host makes from a right-click (or, in a
   browser, from whatever the platform calls asking for a context menu —
   which on a phone is a long press). The toolkit answers with
   **`WSK_NOTE_MENU`**, naming the widget under the point as CLICK does
   (`in`, `item`, and `value` the word on a text item), or `item` = -1 over
   nothing. It does not open a menu: **which menu, if any, is the
   program's**, and it opens one where the pointer is with
   **`wsk_ui_open_at`** — `wsk_ui_open` with a point for an anchor (§5l's
   placing rule: below it, else above it, kept on screen). A MENU on a field
   that has not got focus focuses it, caret where the pointer is, so that the
   program's *Paste* goes there; on the focused field it leaves caret and
   selection alone, so *Copy* copies what was selected. A MENU off an open
   overlay closes it first (CLOSE, then MENU). A MENU while a press is held
   lets the press go, so the release that follows is not a click.
   The keyboard's way to the same thing is `WSK_KEY_MENU` (the Menu key,
   Shift-F10): MENU on the focused widget.
2. **Modifiers ride on the key.** `ev.key` gains three bits —
   `WSK_MOD_SHIFT` 0x100, `WSK_MOD_CTRL` 0x200, `WSK_MOD_ALT` 0x400 — and
   below them either a named key (1–13, as before) or **a printable
   character, lower-case**, for a chord: Ctrl-S is `'s' | WSK_MOD_CTRL`. A
   key without modifiers is exactly what it was, so every program and test
   so far is untouched. On a pointer press the same bits say whether Shift
   was held.
3. **A chord no widget used is the program's.** A key with Ctrl or Alt that
   the toolkit did not use comes out as **`WSK_NOTE_KEY`**, `value` the key.
   That is the whole of shortcuts: the program says what Ctrl-S means, and no
   table of them lives in whisker — §4.1 would forbid it anyway. Plain keys
   nobody used still say nothing, as before, so an arrow with no focus is
   not a notice.
4. **Selection is the caret and a span, in the caller's buffer.**
   `wsk_buf_t` gains `span`: the selection runs from the caret to caret +
   span, either way, **0 meaning none — so a buffer zeroed, or initialised
   as before, has no selection**. The anchor is the end the caret is not at.
   Shift with Left, Right, Home, End, Up and Down moves the caret and keeps
   the anchor; the same keys without Shift collapse the selection (Left to
   its start, Right to its end). Ctrl-A selects all. Typing, Backspace,
   Delete and Enter in a memo **replace** the selection. A press in a field
   puts the caret there and drags select from it; Shift-press extends from
   the anchor. `wsk_ui_init` clears every buffer's span, and **a selection is
   drawn only in the focused field** — a box of the tree's `selfill` behind
   its characters (the colour a chosen row already wears, §5m), on each line
   of a memo — so a buffer a program wrote without clearing `span` is never
   drawn wrong anywhere else.
5. **A drag in a field selects; a drag that leaves it scrolls.** On a touch
   panel a finger in a field inside a scrolling list must still scroll the
   list. So a press in the focused field selects while the pointer stays in
   the field's rows along the scrolling axis; once it is past them by the
   slop the drag becomes the list's, as it was. Across the axis — off the end
   of a one-line field — it keeps selecting, to the end.
6. **The clipboard is the host's; the copy is caller memory.** whisker has
   no clipboard and no way to reach one. The program gives the toolkit a
   buffer — `ui.clip`, `ui.clipcap` — and **Ctrl-C** copies the selection
   into it and notes **`WSK_NOTE_COPY`** (`value` its length), which the
   program hands to its host (`wh_copy`). **Ctrl-X** is COPY and then the
   selection deleted, a TEXT notice. A selection longer than the buffer is
   not copied at all — no part of it, no notice. Without a buffer, Ctrl-C and
   Ctrl-X are not used, and so come out as KEY (rule 3).
   **Ctrl-V** in a field notes **`WSK_NOTE_PASTE`**; the program asks its
   host for the clipboard (`wh_paste`) and gives it to
   **`wsk_ui_paste(ui, text, n, ...)`**, which replaces the selection **all
   or nothing**: a character the window cannot draw (§5j), or a line break
   in a one-line field, is `WSK_E_TEXT`, and text that would not fit is
   `WSK_E_BUDGET`, and the buffer is unchanged either way. Refused, not
   trimmed (D8).
7. **The host contract grows by one event and two calls.** `WH_MENU` at a
   point; `WH_MOD_*` on `key` (and on a press); `WH_KEY_MENU`;
   `int wh_copy(const char *s, int n)` and `int wh_paste(char *b, int cap)`
   — SDL's clipboard on Linux; on the web, the page's. A browser does not let
   a page read the clipboard when it likes, only in a `paste` event, so the
   page posts what was pasted and the host keeps it and turns it into Ctrl-V;
   and the host sends what is copied to the page, which writes it to the
   system clipboard where the browser allows that (a secure origin; plain
   http to a phone is not one, and is reported, not hidden).

### Profiles (§7)

Selection and the clipboard are **`WSK_NO_TEXTSEL`**, which **`WSK_PANEL`
sets** — the P4's keyboard has no Shift-arrows and its finger in a field
should scroll — and `WSK_MINIMAL` has no toolkit at all. MENU, the
modifiers, KEY and `wsk_ui_open_at` are routing, small enough to stay in
every build that has the toolkit, and are measured to say so.

### Done criteria

1. Every earlier output byte-identical; every earlier toolkit check passes
   unchanged (rule 2's promise).
2. MENU: on a button names it and is not a click; over nothing names
   nothing; on an unfocused field focuses it; off an open overlay closes it
   first; during a press, lets it go; `WSK_KEY_MENU` names the focused widget;
   `wsk_ui_open_at` hangs a menu from the point.
3. Shortcuts: an unused chord is KEY with its value, with a field focused or
   not; a plain key nobody used is still nothing (the control).
4. Selection: Shift-arrows, Home/End, Up/Down in a memo, Ctrl-A; collapse;
   replace by typing, Backspace, Delete; drawn as `selfill` boxes behind the
   right characters, on each line of a memo, only in the focused field; a
   press-and-drag and a Shift-press; a drag that leaves a field in a
   scrolling list scrolls it.
5. Clipboard: Ctrl-C, Ctrl-X (COPY then TEXT), Ctrl-V (PASTE),
   `wsk_ui_paste` and its refusals, each with the buffer unchanged; with no
   clip buffer, Ctrl-C is KEY.
6. Hosts: SDL right-click, Ctrl-chords and its clipboard; the browser's
   context menu, chords, `paste` event and copy, checked in headless Chrome
   with clipboard permission granted; `edit.c` gets a context menu — Cut,
   Copy, Paste, Select all, each **the same key fed to the toolkit**, so a
   menu command and its shortcut cannot disagree — and Ctrl-S for Save.
7. Profiles build, `WSK_PANEL` refuses nothing it used to draw, and the cost
   is measured.

### What it came to, 2026-11-14 (night)

Built the same night; criteria 1–7 met. **578 checks** in `src/run.sh
--module` (523 before): 48 in the suite, 6 at the pixels, one cost link; the
every-switch build covers **16** profiles and switches. Every output of the
commit before — 31 files — byte-identical, and every earlier toolkit check
passes unchanged, which is rule 2's promise kept: a key without modifiers is
what it always was.

- **Selection** is the caret and a `span` in the caller's buffer, drawn as
  `selfill` boxes behind the characters — one on a field, one per line on a
  memo — and only in the focused field (the control: the memo's selection is
  not drawn once the field has focus). At the pixels, every event's rows
  repainted alone equal a full redraw, and inside the box the letters are far
  lighter than it: the text is over the box, readable, which is the check an
  eye would make (the letters are anti-aliased, so no pixel is the ink
  exactly; asking for the ink was the first, wrong, version of the check).
- **The drag rule** (rule 5) is measured: a press in a field inside a
  scrolling list, dragged 33 rows up but still in the field, selects and the
  list does not move; out of its rows by more than the slop, the list
  scrolls 65.
- **The clipboard**: Ctrl-C, Ctrl-X (COPY *then* TEXT, so the copy is made
  before the text goes), Ctrl-V (PASTE); `wsk_ui_paste` refuses a character
  the window cannot draw, a line break in a one-line field, and text that
  would not fit — whole, never trimmed — and a selection longer than the
  clip buffer is not copied at all.
- **Right-click and shortcuts**: MENU names the widget, or nothing; focuses
  an unfocused field; leaves a focused one's selection for a Copy; lets a
  held press go; closes an open menu first. An unused chord is KEY, with a
  field focused or not; a plain key nobody used is still nothing.
- **The hosts**: SDL — the right button is MENU, chords and Shift where it
  means something (Shift-Enter is Enter), Alt-x is a chord and not an x, and
  the clipboard is SDL's (its own copy under the dummy driver, which is what
  the self test runs on). **Escape is now a key on SDL too**, for a menu to
  close; close the window to quit. The browser — the platform's
  `contextmenu` (a right-click, or a long press on a phone), chords, the
  `paste` event, and copies sent to the page as `C` and written with the
  Clipboard API. **In headless Chrome with clipboard permission granted, the
  whole round trip is real**: Shift-Home and Ctrl-C with real keys put
  "Hello web" on the system clipboard, read back; a real right-click opened
  the menu and its Paste pasted; a real Ctrl-V fired the browser's own paste
  event, which reached the program as Ctrl-V and replaced the selection —
  and the canvas after all of it still equals a full redraw, 0 pixels.
- **`edit.c`**: right-click a field for Cut, Copy, Paste, Select all, each
  the same key fed back to the toolkit; Ctrl-S is Save, the program's own.

**§7's question**: selection and the clipboard cost a toolkit program
**844 bytes** (`module_nest.c`, by building without them); the routing —
MENU, the modifiers, KEY — **16**, which is why it has no switch. The small
panel is **12,672** in the default (+96: a field's branch draws a
selection), **9,752** in `WSK_PANEL` (32 *less*, from folding the press
into one helper), **3,784** in `WSK_MINIMAL`, unchanged.

**Not done, and named**: undo; word-wise moves (Ctrl-Left is the program's
for now, as a KEY); double-click to select a word; a long press on the P4
(a touch program has no right button, and the toolkit has no timer — a host
with one sends MENU); file dialogs, a second window, a draggable divider;
and on a phone over plain http the browser will not let the page write the
clipboard, which the page reports in its console rather than hides.


---

## 5t. Iteration 19 — a tree's arrows, drawn rather than spelled

**Written 2026-11-14, before any code.** Matthew, having tried §5s on both
hosts: *"the tree view uses "+" and "-" instead of the arrows"* — on the
desktop window, which draws with RV-9's rasteriser and so has no `▾`/`▸`
(§5r: it says it cannot draw UTF-8, and the program fell back). Asked
whether to draw the arrow as a shape instead: *"yes... do that"*.

### The rules

1. **An arrow is a mark, not a character.** Three new values of the
   widget's existing `mark` byte: `WSK_MARK_SHUT` (▸, a closed folder),
   `WSK_MARK_OPEN` (▾) and `WSK_MARK_LEAF` (the same room, nothing drawn, so
   a file's name lines up with its folder's). No new field: a widget is
   still 64 bytes. The program sets the mark when it hands over its rows, as
   it already sets `depth` — expansion stays the program's (§5p).
2. **On a label or a button.** The mark takes a gutter of one font size —
   two characters, as `"▸ "` did — before the text, and the triangle is
   drawn in it as **one filled `<path>`** in the widget's ink, which every
   substrate draws: `/w0`, SDL (the same rasteriser), a browser. The first
   shape whisker draws that is not a rectangle, and deliberately the only
   one: a fixed triangle, not a shape language.
3. **Whole or not at all**, as text is: the triangle and its text are one
   cell for a scroll's cut (§5e).
4. **Profiles.** Disclosure marks are marks: `WSK_NO_MARKS` (and so
   `WSK_MINIMAL`) refuses them by name, `WSK_E_KIND`; a disclosure mark on
   anything but a label or a button is refused too.

### Done criteria

1. Every earlier output byte-identical.
2. The three marks: the triangle's points, the gutter, text truncated by
   two characters more; LEAF draws no path and keeps the room.
3. At the pixels through RV-9's rasteriser: the triangles are there, in the
   ink, pointing the right way — and a person looks.
4. `edit.c` draws its tree with marks on every host: the SDL window shows
   arrows, the browser the same; the browser's UTF-8 is still exercised.
5. Refusals and cost measured.

### What it came to, 2026-11-14 (late)

Built the same night; criteria 1–5 met. **594 checks** (578 before): 12 in
the suite, 3 at the pixels, one profile control. Every output of the commit
before — 33 files — byte-identical.

- **The triangle** is three points in a cell of one font size: ▸ 7/8 of its
  height wide, ▾ the same turned. In the suite its exact path; in a centred
  button the mark and text centred together; a label sized by its text is
  one font size wider marked, so it loses nothing to the mark, and a fixed
  one loses exactly two characters.
- **At the pixels through RV-9's rasteriser** — the substrate with no ▸ in
  its font — each triangle is 8 pixels broad at one end and 0 at its point,
  so it points the way it says; a leaf's cell holds no ink (the control);
  and a person looked: ▾ src, ▸ modules, names aligned under their folders.
- **`edit.c`** marks its rows instead of spelling `▾`/`+`: the desktop
  window and the browser draw the same arrows. The browser's UTF-8 is still
  exercised, by the help line (a real dash and ▸).
- **Refused by name**: on a field (`WSK_E_KIND`), and in `WSK_MINIMAL`
  (profile control: full draws 429 bytes, minimal refuses at the widget).

**§7's question**: **+420** bytes in the default for a small panel
(13,092), **+368** in `WSK_PANEL` (10,120) — the P4 gets arrows too — and
**+4** in `WSK_MINIMAL` (3,788), the refusal's wider test. More than a
triangle ought to cost, and not yet broken down by where it goes.

---

## 5u. Iteration 20 — a memo that wraps

**Written 2026-11-14, before any code**, Matthew having said *"drive on"*.
§5o's memo shows a long line by scrolling sideways, which is right for code
and wrong for a note: Studio's notes, a chat line, a comment box all wrap.
So both, chosen per memo.

### The rules

1. **Chosen per memo, in its buffer**: `wsk_buf_t.wrap`, beside `top`, the
   view's other state. 0 — every buffer so far — is §5o exactly: lines as
   typed, the view sliding sideways to the caret. 1 wraps. Not the default,
   because `source` (the code editor, still to come) must not wrap.
2. **Soft, at a space.** A line longer than the memo's width breaks after
   its last space that fits; the space is not drawn at the row's end. A
   word longer than the whole width breaks where the width ends. Nothing is
   inserted into the text: wrapping is drawing, and the buffer holds what
   was typed.
3. **Rows are what the caret moves on.** Up and Down keep the column across
   the rows of a wrapped line, Home and End are the row's ends, a tap lands
   on the row under it, and `top` counts rows — as every editor that wraps
   behaves.
4. **One implementation.** A row is found by one function given a room in
   characters; a room of 32,767 *is* a logical line, so the unwrapped memo
   runs the same code and draws byte for byte what it did.

### Done criteria

1. Every earlier output byte-identical.
2. Breaks: at the last space that fits, the space undrawn; a long word cut at
   the width; a line that fits exactly not broken; `\n` still a line.
3. Caret: Up/Down across rows keeping the column; Home/End a row's ends; the
   caret after a soft break at the next row's start; a tap on a row.
4. View: `top` in rows, following the caret as it types past the bottom.
5. Selection across rows; repaints equal a full redraw at the pixels; a
   person looks.
6. `edit.c`'s Note wraps; cost measured.

### What it came to, 2026-11-14 (later still)

Built the same night; criteria 1–6 met. **610 checks** (594 before): 14 in
the suite, 2 at the pixels. Every output of the commit before — 34 files —
byte-identical: the unwrapped memo now runs the wrapping code with a room of
32,767 and draws what it drew, which is rule 4 shown rather than claimed.

- **Breaks**: "the quick brown fox jumps" in a row of 12 is *the quick* /
  *brown fox* / *jumps*, the spaces undrawn; a 17-letter word is cut at 12;
  a line of exactly 12 is not broken; `\n` is still a line.
- **The caret on rows**: Up and Down keep the column across a wrapped line;
  End is the row's end, before the space it broke at; a caret at a soft break
  draws at the next row's start; a tap lands on its row.
- **The view in rows**: twelve words typed into five visible rows of two
  words each scroll the view to row 1, and rows 1–5 are what draws.
- **At the pixels** a sentence typed in twelve pieces wraps as it goes,
  every keystroke's rows repainted alone equal to a full redraw, and the
  buffer holds the sentence with no line break in it — wrapping is drawing.
- **`edit.c`**'s Note wraps (and holds 400 bytes now, not 80).

**§7's question**: **+108** bytes in the default (13,200), **+104** in
`WSK_PANEL` (10,224), `WSK_MINIMAL` unchanged — it has no memo.

**Not done**: End on a row broken mid-word (no space) puts the caret at the
next row's start, where it draws — the one place a caret's offset has two
positions on screen, and the simple choice was taken.

---

## 5v. Iterations 21–22 — `source`, the code editor

**Written 2026-11-14, before any code.** The one word of Studio's window
whisker could not answer (`window-gap.md` §7), and the first item of §6.1's
second bar. Matthew settled the question that decided its shape: *"yes,
colour runs from the program"* — **whisker never learns gBASIC's syntax**.

Measured first: gBASIC Studio's own files are **200–326 KB**
(`lib/studio_ui.bas` 325,773 bytes, `app/studio.bas` 208,890), and
`wsk_buf_t`'s positions were `short` — a 32 KB ceiling, a tenth of the file
Studio is written in. So the first change is not a feature.

### The rules

1. **A buffer's positions are `int`** (`cap`, `len`, `caret`, `top`,
   `span`). Nothing drawn changes; a buffer gets a few bytes bigger.
2. **Undo is the caller's memory too.** A buffer may point to a
   `wsk_undo_t` — a byte array the program gives it — and every edit the
   toolkit makes (typing, Backspace, Delete, Enter, a selection replaced, a
   cut, a paste) is recorded there as *what was removed and what was put
   in, where*. **Ctrl-Z** undoes, **Ctrl-Y** and **Ctrl-Shift-Z** redo;
   typing a run of characters is one step, as people expect. When the array
   is full the oldest steps go. No undo array, no undo: the chords stay the
   program's (§5s rule 3). `WSK_NO_UNDO`, set by `WSK_PANEL`.
3. **`WSK_SOURCE` is a memo for code.** It shares the memo's editing,
   selection and view, and differs in three things: it never wraps; it has
   a **gutter of line numbers** on its left, in the window's `bg`, so it
   reads as a column of its own; and its text is drawn in **colour runs**.
4. **Colour runs are the program's.** A buffer may point to an array of
   `wsk_ink_t { int at, len; unsigned int ink; }`, sorted, not
   overlapping — "these characters in this colour". Characters no run
   covers are in the window's ink. whisker draws each run as its own
   `<text>` at its column (monospace makes the column the position), so
   every substrate draws it, RV-9's rasteriser included. The program
   re-colours when it is told the text changed (a TEXT notice) — before it
   repaints, so a stale colour is never drawn; a run past the end of the
   text is ignored, not refused, since the text may have just got shorter.
5. **The gutter is a place to click.** A press there puts the caret at the
   start of that line, and the release notes **`WSK_NOTE_GUTTER`**, its
   `value` the line (from 0) — Studio's breakpoints.
6. **Profiles**: `WSK_NO_SOURCE`, set by `WSK_PANEL` (and implied by
   `WSK_NO_MEMO`); a source widget is refused by name without it.

### Done criteria

1. Every earlier output byte-identical (rule 1 included).
2. Undo and redo of each kind of edit, typing coalesced, the caret put
   back, a full array dropping its oldest, a new edit ending redo, and with
   no array Ctrl-Z the program's.
3. A source widget: the gutter's numbers and width, colour runs drawn as
   pieces at their columns, unsorted runs refused, stale runs ignored; long
   lines scroll sideways; the gutter's press and notice.
4. A 300 KB buffer edited and drawn — Studio's own file size.
5. At the pixels: a coloured listing through RV-9's rasteriser, a person
   looking; repaints equal a full redraw.
6. A demo whose program colours gBASIC itself, on both hosts.
7. Profiles and cost measured.

### What the first step came to, 2026-11-14: `int` positions and undo

**631 checks** (610 before): 20 in the suite. Every output of the commit
before — 35 files — byte-identical with the buffer's positions widened to
`int`.

- **Every edit goes through one primitive** now — `del` bytes at `at`
  replaced by `n` — which records the step first when the buffer has an
  undo array. Typing, Backspace, Delete, Enter, a selection typed over, a
  cut and a paste were seven pieces of code; they are seven calls.
- **Steps** are what people expect: a word and the space before it is one;
  a selection typed over is one, not a deletion and an insertion; a cut and
  a paste one each; each Backspace one. Undo puts the caret after what came
  back; an edit after an undo ends redo; a full array drops its oldest (three
  steps in 48 bytes keep two). With no array, Ctrl-Z is the program's.
- **Found on the way**: three tests set a buffer's fields one by one and
  left the rest as stack garbage — harmless until the struct grew a pointer.
  **A buffer is zeroed before its fields are set**; the struct will grow
  again. (`module_keys.c` does not, and need not: `WSK_PANEL` has neither
  undo nor selection, and a module cannot call `memset`.)
- `edit.c`'s three fields keep 2 KB of undo each.

**§7's question** (step 1): building *without* undo comes out **92 bytes larger**
for a toolkit program — the compiler inlines the one edit primitive
differently — so undo's cost is, as measured, nothing. `WSK_PANEL` +20
(10,244), from the wider positions; `WSK_MINIMAL` unchanged.

### What the second step came to, 2026-11-14: `WSK_SOURCE`

**664 checks** (631 before): 26 in the suite, 6 at the pixels, a profile
control; **19** profiles and switches build. Every earlier output
byte-identical. In a browser, **7 more** checks in headless Chrome
(`host/web/code.mjs`).

- **The widget**: a memo for code — it shares the memo's editing, selection,
  undo and view, and never wraps. A **gutter** of line numbers in the
  window's `bg`, two digits wide at least and widening as a file passes 99,
  999 lines; a press there puts the caret at the line's start and its
  release notes **GUTTER** with the line — Studio's breakpoints. The control:
  a press in the text is a click and no GUTTER.
- **Colour runs from the program**, drawn as a `<text>` per run at its
  column; gaps in the widget's ink; a piece's opening spaces not sent, its x
  says where. Runs out of order or overlapping refused; a run past the end
  ignored, since the text may just have shrunk. At the pixels through RV-9's
  rasteriser: "function" is blue and the comment green, "n * n" neither (the
  control) — measured **by hue**, because thin anti-aliased glyphs at 16 px
  put almost no pixel on their ink (two of 1,024 within 40 of it); the
  first, exact-colour version of the check was wrong, as §5s's was.
- **300 KB** — Studio's own file size: drawn with the last line's number and
  a 5-digit gutter, typed into past offset 32,767 by ten times, Home to the
  line's start. The memo's draw now walks the text once a picture, not once a
  row. The whole suite still runs in 15 ms.
- **Found, and not a source matter: `<`, `>` and `&`.** Code is full of them
  and whisker refused them in all text, because `/w0` draws `&lt;` as four
  characters (§2.4). That is a host's limit, so it became a capability:
  **`WSK_CAP_ENTITY`** — a host that decodes the three says so, and whisker
  accepts them and escapes them as it writes, in its one `<text>` emitter.
  Without the bit nothing changes, byte for byte (no `&` ever reached the
  emitter before). The browser claims it; SDL and RV-9 do not, so **the
  desktop window refuses `sample.bas`**, by name, until RV-9 decodes the
  three entities — `docs/rv9-request-entities.md` asks it to. The board tiers
  carry none of it (`WSK_NO_ENTITY`): `WSK_MINIMAL` is 3,788, as before.
- **Found in the browser: runs of spaces collapsed.** An SVG collapses them,
  `/w0` does not; indentation survived anyway (each run is placed at its
  column) but ten spaces inside a comment drew as one. `xml:space` on the
  root did nothing in Chrome — measured; `white-space:pre` in the page's
  style does — measured both ways, 450 against 369.
- **The demo, `host/linux/code.c`** (`run.sh --code`, either host): opens
  `sample.bas` or any file, colours it with gBASIC's own keyword list (from
  its lexer) — strings, comments, numbers, calls — re-colours on each TEXT
  notice before repainting, prints breakpoints from the gutter, never writes
  the file.

**§7's question**: source is **740 bytes** in the default (14,120 for a
small panel: +936 since step 1, with entities and the memo's single pass);
`WSK_PANEL` **10,284** (+40); `WSK_MINIMAL` **3,788**, unchanged.

**Left for step 3, and named**: UTF-8 in fields, memos and source —
1.2 % of gBASIC's lines; until then such a file is refused. And breakpoint
*marks* in the gutter, which the program can only print today.

### What the third step came to, 2026-11-14: UTF-8 editing

Written with the code, overnight, on Matthew's "go ahead with source" — the
rule it follows was settled in §5r and is only carried through here.

**The rule: positions are bytes, columns are characters.** A buffer's
`caret`, `span`, `len` and a colour run's `at` and `len` stay byte offsets —
a program indexes its own text, and nothing it holds changes meaning. Every
place that turns a position into a place on the glass counts characters
(`wsk__chars`), and every place that turns a place back into a position
finds the byte a column starts at (`wsk__byte_of`): a field's view and
caret, a memo's row, column and Up/Down, a soft wrap's room, a click, a
selection's box, and a colour run's x. **For ASCII the two are the same
number**, which is why nothing earlier moves.

- **The caret steps a character at a time**: Left and Right over a whole
  character, Backspace and Delete take all of its bytes, so the caret is
  never inside one. **The check refuses one that is** (`WSK_E_TEXT`), and a
  colour run that starts or ends inside a character, since its `<text>`
  would split it.
- **What goes in is checked as a label is** (§5r): well-formed UTF-8 for a
  host that claims `WSK_CAP_UTF8`, ASCII for one that does not — typed text,
  a paste (all or nothing, D8: one cut off before its end is refused) and a
  buffer filled by hand, through one function (`wsk__ok_n`).
- **Undo**: a character typed is one character whatever its bytes, so `é`
  joins the word it is typed in, as `e` does.
- **Hosts**: the browser's page now sends a typed character outside the
  Basic Multilingual Plane (an emoji is two UTF-16 units, and `e.key.length`
  said 2). The SDL window claims no UTF-8 — `/w0`'s font is ASCII — so
  there, such a file is still refused, by name.

**Checks: 694** (664 before): 29 in the suite, 20 of which fail against
the previous commit's header (the other nine are controls and ASCII that
worked before), and the `-DWSK_NO_UTF8` link. In a browser: over HTTP an `é`
typed and a Backspace taking it back whole; in headless Chrome, `sample.bas`
now has an em dash on line 3, and **its line ends at x 676 — 37 + 71
characters × 9 — not at its 73 bytes' 694**: the browser's monospace advance
holds for the dash, so columns after it do not drift. An `é` typed is
exactly 9 px. Every earlier output — 37 files — byte-identical.

**§7's question**: UTF-8 costs the default **744 bytes** (14,596 for a small
panel, +476); `WSK_PANEL` **10,340** (+56 — the shared validator and the
character steps, compiled down to ASCII and ±1); `WSK_MINIMAL` **3,788**,
unchanged.

**Not done, and named**: a character's *width* is taken to be one cell. True
of the em dash in Chrome's monospace (measured above), not of CJK or most
emoji, which are two cells wide; whisker has no width table and would put
what follows them a cell early. Combining marks count as a character each.
**Counted in gBASIC's 1,568 `.bas` files, 2,035 characters beyond ASCII:**
mostly `§` (927) and `—` (639), then accented Latin, arrows, Cyrillic; **67
are wide** (日本語, 中文, 😀) **and one combining**, all in twelve test
programs (`tests/string_nul_test.bas` has 30). **Studio's own files hold
none at all.** So a gBASIC test file with 日本語 opens and edits, and its
columns after the wide characters drift by a cell each — the next step if
Studio ever has to show those files. Breakpoint marks in the gutter are
still the program's to print.

### Step 4, written before its code (2026-11-14): marks in the gutter

The last thing §5v named. Studio's gutter is not a ruler: gBASIC's Qt
backend (its `docs/qt_gui_design.md` §0.29) gives it **five categories** —
`break`, `error`, `warning`, `section` (a `▶` at each runnable section),
`note` — with a priority, so a line with two shows the higher, and a
breakpoint, the one mark a person set, wins. Read as whisker must read it:
**the categories and their priority are the program's, as colours are.**
whisker draws a mark; it does not know what a breakpoint is.

1. **A buffer may point to marks**: `wsk_gmark_t { int line; unsigned int
   ink; unsigned char shape; }`, sorted by line, **one per line** — the
   program has already applied its priority, so whisker never chooses
   between two. Lines count from 0, as the GUTTER notice's do and as every
   index in whisker does; gBASIC's lines count from 1, and the one
   conversion belongs in its backend, next to the notice it already
   converts.
2. **Three shapes, all in what every substrate draws**: `WSK_GM_DOT`, a
   filled square half the font high (a breakpoint — no circles: a
   rectangle is what `/w0` is sure of, and §5t's triangle is on purpose the
   only other shape); `WSK_GM_ARROW`, §5t's right-pointing triangle (a
   section's `▶`); `WSK_GM_BAR`, the line's whole gutter cell filled (the
   line a debugger stopped on), its number drawn over it.
3. **The column is reserved by the pointer, not by the count**: a buffer
   whose `marks` is set has a mark column one font wide at the gutter's
   left even with none in it, so the first breakpoint does not shift every
   character of the text sideways. (gBASIC's `marks: true`, for the same
   reason.)
4. **Refused**: marks out of order, two on one line, a line below 0, a
   shape whisker does not know — `WSK_E_TEXT`, as colour runs are.
   **Ignored**: a mark past the last line, since the text may just have
   shrunk (gBASIC's `qt.marks` keeps what was set for the same reason).
5. A press anywhere in the gutter, the mark column included, is still the
   GUTTER notice with its line: setting a breakpoint is the program's.

Done when: each shape drawn in its cell at its line and none elsewhere (the
control: an unmarked line has nothing in the column), the column reserved
with no marks, the refusals, a mark past the end ignored, every earlier
output byte-identical, the pixels through RV-9's rasteriser, and `code.c`
toggling a red breakpoint on a gutter click on both hosts.

### What the fourth step came to, 2026-11-14: marks

**714 checks** (694 before): 15 in the suite, 5 at the pixels. Every
earlier output — 37 files — byte-identical; the new picture is
`source_marks.ppm`. In headless Chrome, **2 more** (`code.mjs`, 11 now).

- As written, nothing changed in the building. Each shape lands in its cell
  at its line, and the unmarked lines' cells are empty (the control); the
  column is reserved by the pointer, so the text moves from x 34 to 50 when
  the program first gives marks and never again; two on a line, out of
  order, below 0, or an unknown shape refused; past the end ignored;
  scrolled, only the lines in view are marked, found by one binary search a
  picture, as colour runs are.
- **At the pixels through RV-9's rasteriser**: the breakpoint's square is
  all 64 of its pixels red — a rectangle, so exact colour is a fair check
  here, unlike glyphs; the arrow green by hue; the bar 750 of its 800
  pixels, the rest the line number drawn over it. Looked at, zoomed: the
  picture reads as an editor's gutter. Looking found a fault, in the
  picture's own program: it had overwritten the `!` the test typed rather
  than removed it, so every colour run sat a character early — "function"
  drawn with its `n` uncoloured. Every check passed on that picture; the
  checks look at runs' starts, and the fault was at their ends.
- §5t's triangle now takes a mark and an ink, not a widget, so the tree's
  arrow and the gutter's are one function.
- **`code.c` draws its breakpoints**: a gutter click toggles a red square
  (and still prints it). In Chrome: 81 red pixels by line 10 after the
  click, none by line 9, none by line 10 after a second click.
  Breakpoints stay on their line numbers as text is inserted above them —
  following the text is the program's job, and that demo does not do it.

**§7's question**: marks cost **408 bytes** in the default (15,004 for a
small panel; source is 1,296 in all). `WSK_PANEL` 10,340 and `WSK_MINIMAL`
3,788, unchanged — neither has source.

**`source` is now everything §5v's done criteria named.** Not done, and not
asked yet: a whole-line highlight in the text (a debugger's current line is
a gutter bar here), wide characters (step 3's note), and marks that follow
edits.

## 5w. Iteration 23 — words: Ctrl-arrows, Ctrl-Backspace, double-click

**Written 2026-11-14, before its code**, overnight, under Matthew's "and
whatever else you can do". The code editor of §5v moves a character at a
time; every desktop editor a person has used moves a word with Ctrl, and
selects one with a double-click. Small, and the difference between an editor
and a text box.

1. **A word** is a run of letters, digits, `_` and any character beyond
   ASCII (so `é` and `日本` are inside words, not between them); a run of
   other marks (`:=`, `"(`) is a word of its own; spaces separate them. A
   line break is a space to Ctrl's moves, and a wall to a double-click.
2. **Ctrl-Left** goes back over spaces, then to the start of the word
   before; **Ctrl-Right** over spaces, then to the end of the word after —
   each lands on a word's edge, both ways, as VS Code does. With Shift they
   extend the selection, as every move does (§5s rule 4).
3. **Ctrl-Backspace** and **Ctrl-Delete** delete what Ctrl-Left and
   Ctrl-Right would have moved over — a selection instead, if there is one —
   as one undo step.
4. **Ctrl-Home** and **Ctrl-End**: the start and end of the whole text. In a
   memo or source, Home and End are the line's; in a field they were the
   text's already.
5. **A double-click selects the word under it; a triple-click, its line**
   (as typed, without its break). The *host* counts clicks — the platform
   owns double-click timing and distance, and SDL and the browser both
   report a count — and says so on the press: **`d` on a DOWN is the click
   count** (0 or 1, a single). `d` was the wheel's alone, so no event
   struct grows, and a host that counts nothing sends 0, a single click.
6. These chords become the focused field's: until now they reached the
   program as KEY notices (§5s rule 3), and with no field focused they still
   do. **`WSK_NO_WORDS`**, set by `WSK_PANEL` — the P4's keyboard has no
   Ctrl, and a finger no double-click.

Done when: each move, from inside a word, from its edge, over spaces, over
marks, over a line break, at either end of the text; extending with Shift;
both deletions and their undo; double- and triple-click in a field and a
memo, and a single click (the control) selecting nothing; UTF-8 inside a
word; every earlier output byte-identical; both hosts sending the count, and
a double-click in Chrome selecting a word that Ctrl-C then copies.

### What it came to, 2026-11-15

**748 checks** (714 before): 33 in the suite — **28 fail against the
previous header**, the other five are controls and things that held before
(a single click, Home in a memo, the chords with no field focused) — and
the `-DWSK_NO_WORDS` link; **20** profiles and switches build. Every earlier
output — 38 files — byte-identical. The Linux host adds 2 (41): SDL's own
click count reaches the press, and an uncounted press is 0. In headless
Chrome, 3 more (`code.mjs`, 14): a real double-click selects `powers` and
Ctrl-C copies it, a triple-click its line, Ctrl-Shift-Left three times from
a line's end selects `(n)`.

- **Found by the browser check, which failed first: a pointerdown's
  `detail` is always 0** in Chrome 156 — measured, three presses with click
  counts 1, 2, 3 gave pointerdown 0, 0, 0 and mousedown 1, 2, 3. So the
  page sends a *mouse's* press from mousedown, and a finger's or a pen's
  from pointerdown as a single, as before. The HTTP checks could not see
  this; only a real browser could.
- **Found by the suite: a word move must drop a selection itself.** Ctrl's
  chords are marked *used* so they do not reach the program as KEY notices,
  and *used* is also what keeps a selection through Ctrl-A and Ctrl-C — so
  Ctrl-Right without Shift kept one. It lets it go in its own branch now.
- **A double-click's word survives jitter**: the toolkit keeps the word
  (`ui.wa`, `ui.wz`), so a pointer moving a pixel before the release does not
  shrink the selection to a character, and a drag after it grows the
  selection out from whichever end of the word it leaves by.
- Double-clicking past a line's end selects the word the line ends with,
  as editors do; on an empty line, nothing.

**§7's question, and a measurement that is not one.** `module_nest.c`
built without words is **44 bytes** smaller — but that program has no
editable field, so the compiler drops most of what words add; a floor, not
the price (undo's 28 there has the same flaw, and §5v step 1's "92 bytes
larger without it" likely the same cause). Natively at `-Os`, `code.c` — an
editor — is **845 bytes** smaller without words (and 1,224 without UTF-8).
No RV-9 module edits text with a keyboard, so there is no board figure to
take, and the board tiers carry none of it: `WSK_PANEL` 10,340,
`WSK_MINIMAL` 3,788, both unchanged; the default's small panel, 15,004,
unchanged too — it has no toolkit.

## 5x. Iteration 24 — a divider a person can drag

**Written 2026-11-15, before its code.** First of the three interactions
`window-gap.md` §8 leaves: Studio's window *is* a split — navigator,
editor, output — and whisker's panes are fixed by their weights.

1. **The sizes are the caller's, beside the tree.** A stack may point to
   `short *sizes`, one per item, writable, as `scroll` is (D5): an item
   whose entry is above 0 is that many pixels along the axis, its `size`
   and `weight` set aside. Zero, or no array, and nothing changes — the
   widget, which may be in flash, is never written.
2. **`WSK_DIVIDER` is a widget**, a thin bar between two items, drawn as
   its fill (a separator that can be held). Its `size` is its thickness.
3. **A drag on it moves the boundary.** The toolkit resizes the pane
   *before* the divider — unless that pane is weighted and the one after
   is fixed, when it resizes the one after (Studio's output pane under a
   weighted editor). A weighted pane resized becomes fixed (its entry in
   `sizes`), and the weighted rest takes up the difference; between two
   fixed panes, one grows as the other shrinks.
4. **Clamped, never refused**: a dragged pane is at least two lines of the
   font high (or wide), and so is whatever a weight still shares; a drag
   past that stops there. The divider follows the pointer up to the clamp.
5. **What it tells**: `WSK_NOTE_SPLIT` on each change — the divider, and the
   new extent of the pane it moved — so a program can keep a layout across
   runs; the rows to repaint are the stack's. A press on a divider is not
   a click, and is not a scroll.
6. **Refused**: a divider at either end of a stack (nothing on one side),
   in a wrapping stack or a table's row (their layout is not a list of
   extents) — `WSK_E_KIND`. `WSK_NO_SPLIT`, set by `WSK_PANEL` and
   `WSK_MINIMAL`; a divider refused by name there.

Not here: the pointer changing shape over a divider (a host's cursor, and
no host has a cursor call), and a divider moved from the keyboard.

Done when: sizes moving layout, byte-identical with no array; each case of
rule 3 by drag, the clamps both ways, the notice, the refusals, no click; a
picture through RV-9's rasteriser; `edit.c` or `code.c` split by a divider a
person can drag on both hosts, and in Chrome a real drag moving it.

### What it came to, 2026-11-15

**774 checks** (748 before): 23 in the suite, 3 at the pixels, and the
`-DWSK_NO_SPLIT` link; **21** profiles and switches build. Every earlier
output — 38 files — byte-identical, **after one fix the comparison
forced**. In headless Chrome, one more (`code.mjs`, 15).

- **As written, with one addition**: a divider in a stack with no `sizes`
  is refused too (`WSK_E_KIND`) — it could not move anything, and a bar that
  does nothing when held is the silent kind of wrong.
- **Each case of rule 3 by drag**: a fixed pane before grows (30 px: 100 to
  130, the divider at 134 under the pointer); a fixed pane after a weighted
  one moves inversely; two weighted panes — the first becomes fixed at its
  width; two fixed — one grows as the other shrinks, their total kept.
  Clamps: 32 px (two lines of a 16-px font) for the pane and for what a
  weight shares; past a clamp, no notice and nothing to repaint. The
  release is not a click; a move after it moves nothing (the control).
  Nested: SPLIT names the nested stack, as every notice names its tree.
- **At the pixels through RV-9's rasteriser**: the bar every pixel its
  colour at 88; dragged 60, every pixel at 148 and none left at 88; the rows
  the drag named, repainted alone, equal a full redraw. Looked at.
- **Found by the byte comparison, passed by every check: a dangling
  `else`.** The divider's branch was first placed between the selection's
  `} else` and the box it guards (§5m), so the `else` took the divider and
  every chosen button's plain box was drawn over its selection fill.
  `check_select.ppm` moved — 8,008 pixels, the "Third" row orange to blue —
  and the picture's own check passed, because it compares a banded repaint
  with a full redraw and both were wrong the same way. Fixed by placing the
  branch before the selection, and **a check that fails on the broken
  ordering** now guards it (measured both ways).
- **`code.c`** has an output pane under the code with a divider between
  (Studio's shape); what the program says goes there. In Chrome, a real
  mouse drags it 100 px up: the output pane 220 high, the bar's colour at
  y 373 and not at 473. The pane's size on each step goes to stdout, not
  into the pane — every pixel of a drag would fill it.

**§7's question**: dividers cost **852 bytes** in a toolkit program
(`module_nest.c` — which has none, so this one is the layout's `sizes` and
the drag together, not a floor like words' 44); the default's small panel
**15,196** (+192, the layout's share); `WSK_PANEL` **10,344** (+4, the
weight read through one helper); `WSK_MINIMAL` **3,788**, unchanged.

## 5y. Iteration 25 — the keyboard in a menu

**Written 2026-11-15, before its code.** The second interaction
`window-gap.md` §8 leaves: a `choice` and a menu open and close, and are
chosen from, by a pointer only. A person with a hand on the keyboard expects
Down, Down, Enter.

1. **While an overlay is open, Up, Down, Home, End and Enter are its** —
   not the focused field's: Down in an open menu over a memo must not move
   the memo's caret. Escape already closes it (§5l).
2. **A highlight walks the overlay's buttons**: Down to the next, Up to the
   one before, wrapping at the ends; Home and End the first and last. Labels
   and separators in a menu are passed over. With no highlight, Down starts
   at the first button and Up at the last. Opening an overlay — by any means
   — starts with none.
3. **Drawn as keyboard focus is everywhere else**: §5m's ring of thin
   rectangles, on the highlighted button. The window's own focus is not
   touched — the field a menu was opened over keeps its caret and gets it
   back when the menu closes.
4. **Enter chooses**: CLICK on the highlighted button, then CLOSE — exactly
   what a click on it gives, so a program cannot tell which hand chose.
   With no highlight, Enter does nothing (and is still the menu's).
5. **A menu that scrolls** keeps the highlight in view: its `scroll` moves
   as little as shows the button whole.
6. The highlight is a `short` on the root (`ohi`), the caller's, beside
   `over` — drawing needs it and drawing has only the tree.
   **`WSK_NO_MENUKEYS`**, set by `WSK_PANEL` (no arrow keys on the P4's
   keyboard) and implied by `WSK_NO_OVER`.

Done when: each key, wrapping both ways, passing a label, Enter's two
notices matching a click's, Enter with no highlight, a field under the menu
untouched by Down (the control: with the menu closed, Down moves its
caret), the ring drawn and gone on close, a scrolling menu following, every
earlier output byte-identical, and in Chrome a menu chosen from the
keyboard.

### What it came to, 2026-11-15

**793 checks** (774 before): 18 in the suite and the `-DWSK_NO_MENUKEYS`
link; **22** profiles and switches build. Every earlier output — 39 files —
byte-identical. In headless Chrome, one more (19): Ctrl-S opens `edit.c`'s
Save menu, Down reaches "Save now" — the picture shows the ring on it — and
Enter chooses it, the program printing the same `click save-now` a click
prints.

- As written. The first draft of the ring check guessed the ring's
  rectangles at the button's edge; §5m draws them two pixels in. The check
  was wrong, not the ring.
- With the menu closed, Down moves the memo's caret (the control); open, the
  caret does not move, and after Enter closes it the memo has focus and
  Down is its own again. Enter with nothing reached does nothing, and does
  not reach the memo either — no line break.
- A six-item menu scrolling in a 200-px screen: Down to the sixth scrolls
  until it is whole; Home scrolls back to 0.

**§7's question**: 32 bytes to `module_nest.c` (a floor: it opens no menu
from the keyboard); the default's small panel **15,220** (+24 — the
overlay's ring and the reset on close); `WSK_PANEL` **10,344** and
`WSK_MINIMAL` **3,788**, unchanged.

---

## 6. What would have to be true before gBASIC took this

Recorded so that the bar is set now, while nothing is invested, rather than
argued later when something is:

- whisker runs a real RV-9 interface on real hardware, and somebody uses it.
- The two functions are stable enough that gBASIC's renderer seam can be written
  against them without a version negotiation.
- ~~A Linux host for the contract exists and is small~~ — **done, 2026-10-01**:
  `host/linux`, **276 lines**, of which 62 are the contract. A window, a blit
  and a point, over RV-9's own rasteriser, so what it draws is what the board
  draws. It needs SDL2 for the window and nothing else; no toolkit, and no
  widget on that side of the seam. The bar was that it be small, and 276 lines
  is the number rather than the adjective.

If those are not true, the right answer is that whisker served RV-9 and gBASIC
kept its toolkit — GTK when this was written, Qt since 2026-10-22. That is a success, not a failure: the reason for a separate project is
to make *not* adopting it cheap.

### 6.1 Two bars now, since 2026-11-14 (D22)

The bar above was for gBASIC **taking** whisker at all, and most of it is met:
whisker runs real interfaces on the P4 that a person used (`wkeys`, `wnest`,
`wpiano`), and the Linux host and the browser host (§5k) both exist. Matthew
has since set a longer aim, in two steps:

> *"It would be best for a gBASIC programmer to learn one way to write GUI
> apps that works across both"* — and — *"one day, we might replace Qt with
> whisker if it becomes good enough and there is no longer any reason for
> Qt. One binary would be nice."*

**Bar 1 — one way to write a GUI, on either backend.** Met when a gBASIC
program written against the neutral API (`docs/gbasic-ui-proposal.md`) runs
**unchanged** on Qt and on whisker, for the programs most people write —
forms, tables, trees, menus, a memo — checked by running the same program
under both and comparing what each says the window holds. Mostly gBASIC's
work (the neutral API, and a split of its `qt` module into a front and a
backend); whisker's part is a backend over the parts it has.

**Bar 2 — no reason left for Qt.** Met when all four hold, each checkable:

1. **Studio runs on whisker** in daily use — which means `source`, the code
   editor (the one word of Studio's twenty-five still unanswered,
   `window-gap.md` §7).
2. **Text is complete on a desktop**: proportional fonts, Unicode (Studio's
   own `▾`/`▸` among it), selection with the pointer, the clipboard, undo,
   and input methods for scripts typed by composition.
   *Since 2026-11-14: Unicode drawn where a host can (§5r — not yet edited
   in a field); selection with the pointer and keys, and the clipboard
   (§5s). Left: proportional fonts, undo, input methods, Unicode in fields.*
3. **A desktop's plumbing**: right-click, shortcuts, file dialogs, more than
   one window, a draggable divider.
   *Since 2026-11-14: right-click and shortcuts (§5s). Left: file dialogs,
   more than one window, a draggable divider.*
4. **Accessibility is decided** — screen readers, the one place Qt's decades
   are hardest to match: whisker grows it, or gBASIC decides in writing that
   its GUI programs can do without it.

When all four hold, switching the default backend is a small change, and a
gBASIC GUI program is **one static binary** — whisker is C with no
dependencies, and its desktop host needs SDL, which links statically; or no
windowing library at all, through the browser host (§5k). Until then Qt is
the default and whisker earns its place by the things Qt cannot do: a static
binary, any browser, an RV-9 board.

---

## 7. Profiles — one renderer, and what a small host may leave out

**Why this section exists.** Matthew, 2026-10-02: *"I want to ensure that at
least a scaled down basic version can run on the C5 but also for gBASIC (and
maybe other things) on larger systems."* Those are the two ends of §2.8's
table, and until now whisker had only checked the large one.

### 7.1 The large end is already reached

gBASIC's declarative `gui` names exactly five component kinds — counted across
its documents and examples, not estimated: `label`, `button`, `input`, `vert`
and `horiz`. whisker has all five: `WSK_LABEL`, `WSK_BUTTON`, `WSK_FIELD`, and
`WSK_COL`/`WSK_ROW` stacks composed into one document (§5c). `gbasic-gap.md`
said the gap was one widget; that widget is built, and nothing in `gui` is
left over. What remains before gBASIC takes this is §6's bar, which is about
evidence and stability rather than about missing widgets.

**Stale since 2026-10-22:** `gui` was removed with GTK. gBASIC's `window`
declaration has far more than five kinds (`table`, `tabs`, `cells`, `text`,
`memo`, `choice`, `source`, dialogs and menus among them), so "the large end is
already reached" is not true of gBASIC as it now is. **Recounted the same
day in `window-gap.md`**: Studio's record uses 25 words and whisker answers 15.

So the work is the **small** end, and the question is sharper than it looks:
not *can* the C5 run whisker — it does — but **does it pay for the parts it
cannot use?**

### 7.2 It did, and the measurement says where

`tools/evidence/profiles/` builds the smallest honest whisker program — a
label and two buttons, written to `/w0` once — and prices what it carries:

| | bytes | saved |
|---|---|---|
| full, the default | 3,272 | |
| `-DWSK_NO_MARKS` | 2,984 | 288 |
| `-DWSK_NO_FIELD` | 2,908 | 364 |
| `-DWSK_MINIMAL`, both | **2,604** | **668 — 20 %** |

**`-DWSK_NO_MARKS` buys something larger than its 288 bytes, found on
2026-10-03.** Corner marks are the only `<path>` whisker emits, and a document
containing a path costs RV-9's renderer a 624-byte frame it otherwise never
enters — worth about **a kilobyte of declared stack** between two real
programs. Compiling marks out turns "this document happens to have no path" into
a property of the binary. See §5's note on the writer's stack. **Stack is the
scarcer resource of the two**, so for a host that does not need a pressed-button
mark this is the profile's main benefit rather than a side effect.

That program draws no field and marks nothing, and carried both anyway.

**But the larger half of the answer is what it never carried.** Every function
here is `static inline`, so an entry point the caller does not name is never
emitted: `wsk_hit`, `wsk_compose`, `wsk_rows`, `wsk_render_clipped`,
`wsk_part` and `wsk_field_view` are all **absent** from that binary, checked
by `nm` rather than assumed. whisker already scales down by linkage for
everything a caller can decline to call.

**What it cannot decline is a branch inside the per-widget loop**, which every
caller links whatever it draws — the kind test that chooses between a field
and a label, and the mark test. The linker cannot drop a branch; it can only
drop a section nothing reaches.

### 7.3 So the mechanism is the preprocessor, and that is forced

A dispatch table would make those two droppable like everything else. **A
dispatch table is precisely what an RV-9 module may not have** (§4.1): it is
addresses in rodata, it fails the double-link, and it cost RV-9 a working
module a rewrite twice before whisker existed. The preprocessor is what is
left — not a taste, and worth saying so, because a reader who has used a
widget set will reach for the table first.

```
-DWSK_MINIMAL     label, button, stacks, hit, bands: what the C5 needs
-DWSK_NO_FIELD    drop the text field       (364 bytes)
-DWSK_NO_MARKS    drop the corner marks     (288 bytes)
```

**The default is everything, and a host opts *down*.** That is §2.8's rule
applied to the build: a large host configures nothing, and nobody pays for the
C5's limits unless they are on the C5. The inverse — a minimal core that hosts
opt *into* — would make the common case the configured one and would put the
small board in charge of the model again.

### 7.4 Two properties, and they are gates rather than intentions

A profile system is where a widget library quietly becomes two widget
libraries, so the two things that must stay true are checked in `src/run.sh`
and not merely intended:

1. **A reduced build draws identically.** The same panel compiled both ways is
   diffed **byte for byte**. If it ever differs, D2 is gone — "one
   implementation, not a specification and two" — and the failure would be a
   widget hit-tested in one place and drawn in another, on whichever host
   happened to be built small.
2. **It refuses what it lacks, naming the widget** — `WSK_E_KIND`, from
   `wsk_check`, before a byte is written. It never substitutes. A field
   silently drawn as a label is exactly the silent-wrong-answer shape this
   project keeps finding in substrates (§2.3's `MAX_CONTOURS`), and it would
   be worse here because whisker would be the one doing it.

The refusals are also the **control** on the first property: without them, a
reduced build could satisfy "byte for byte" by drawing a field as a label and
saying nothing. `WSK_FIELD`'s enum value never moves between profiles either —
a reduced build refuses it, it does not renumber around it — so a panel, an id
and a golden mean the same thing whatever whisker was compiled with.

### 7.5 What this does and does not settle

It is the mechanism D6 already called for: *"a whisker binding must report
which widgets its host supports rather than drawing a keyboard that cannot be
used."* D6 settled *which* hosts get `input`; this settles *how* a host that
does not get it avoids paying for it.

**Not settled, and deliberately:** whether the axes stay these two. Each new
widget kind adds a branch to the same loop and so arrives with the same
question — and the right answer may eventually be that the small profile is
the one with a name and a documented widget set, rather than a list of
subtractions. That is a decision to make when there is a third axis, not now,
on one data point.

**Also not a goal: shrinking the C5 build further.** 2,604 bytes of a ~39 KB
program budget is not where a C5 program runs out of room; `SRC_MAX` and the
stack (§5) are. The point of this section is that a host does not carry a
widget it was told it cannot have — a correctness and honesty property that
happens to save bytes, not a byte optimisation.

---

## 8. The toolkit — whisker as a complete GUI system

**Written 2026-11-11. Proposed, nothing built.** D16 is the decision behind
it; this section is the design that decision asks for, written before any
code as the conventions require. Where it says *propose*, it may be wrong.

### 8.1 What changed

Matthew, the same day `wscroll` first ran on the P4:

> *"I was wanting whisker to become a complete GUI system."*
> *"I would like whisker to work on those tiny devices for what it can do — at
> least presentation but also on the P4 with touch controls and ultimately on
> desktops to include as an alternative for Qt in gBASIC."*

Until now whisker was two pure functions and said "nothing else" in its first
paragraph. That was the right start and it is the wrong finish: every program
that scrolls has to turn a finger into an offset itself (`wscroll` spends about
twenty lines on it), every program with a field carries a typing loop
(`host/linux/input.c`), and a widget never tells anyone what happened to it —
the program has to work it out from a point. A toolkit is the layer that does
those once.

### 8.2 Three layers, three tiers

| layer | what it does | state | C5 (presentation) | P4 (touch) | desktop |
|---|---|---|---|---|---|
| **1. drawing** — built, §5–§5e | a tree becomes SVG; a point becomes a widget | none | **yes** | yes | yes |
| **2. toolkit** — this section | an input event updates the widgets and says what changed, and which rows to repaint | the caller's structs, updated in place | **no** | yes | yes |
| **3. host** — one per platform | reads the device, runs the loop, owns the window | the host's | RV-9 module | RV-9 module | SDL; the gBASIC backend |

A tier is a set of layers, and the line between them is **hardware**, as D6
drew it: a device with nothing to press compiles layer 2 out. That is D8's
mechanism with one more switch — **`WSK_NO_INPUT`**, defined by
`WSK_MINIMAL` — and D8's rules hold: the default is everything, a reduced
build draws byte-identically, and a call into what it lacks is refused by
name rather than quietly ignored.

### 8.3 What survives, and what does not

- **D5 survives in the form that mattered.** Its argument was that a tree
  retained *inside* whisker is a third copy that can disagree with the host's
  and the application's. The toolkit retains nothing either: its state —
  which button is held, where a drag started, which field has focus, where the
  caret is — lives in a struct **the caller allocates**, and the widgets it
  changes are the caller's array. whisker still allocates nothing and keeps no
  static data, which the module rules demand anyway.
- **What does not survive is "the caller decides everything".** In the
  toolkit a scrolling list moves when it is dragged and *reports* that it
  moved; a field takes a key and reports its new text. The program reads what
  changed instead of deriving it. That is §5e's choice 1, restated (D16a).
- **D2, D4, D8, D10–D15 are untouched.** One implementation; the id is the
  address, so a notice names a widget by id; refuse, never substitute.
- **The module rules bind layer 2 like layer 1.** That rules out the usual
  shape of a toolkit — a table of handlers, callbacks registered per widget —
  and is the reason for §8.5.

### 8.4 An event in

One struct, the same on every host, so a host's whole job is translation:

    typedef struct {
        unsigned char kind;    /* WSK_EV_DOWN, _MOVE, _UP, _WHEEL, _KEY, _TEXT */
        short x, y;            /* device pixels: pointer events, and the wheel's target */
        short d;               /* wheel: pixels along the stack's axis */
        unsigned short key;    /* WSK_KEY_TAB, _ENTER, _LEFT ... for _KEY */
        char  text[8];         /* one character, UTF-8, for _TEXT */
        unsigned int ms;       /* when the device saw it */
    } wsk_event_t;

**Touch and the mouse are the same three events** — D7 already found a click
and a tap are both one point, and the P4's five contacts are used as one.
Right-click and multi-touch are left out of the first version and named as
such, not forgotten (right-click arrived in §5s, as an event of its own). `_KEY` is a key that means something to a widget;
`_TEXT` is a character typed, so a host's keyboard layout stays the host's
business (the reason `input.c` keeps its keymap outside whisker).

### 8.5 What changed, out

    typedef struct {
        unsigned char kind;    /* WSK_NOTE_CLICK, _SCROLL, _TEXT, _SELECT, _FOCUS */
        unsigned char stack;   /* which stack */
        short         item;    /* which widget in it */
        int           value;   /* the new scroll, selection, caret ... */
    } wsk_note_t;

    int wsk_feed(wsk_ui_t *ui, const wsk_event_t *ev,
                 wsk_note_t *note, int max, short *y0, short *y1);

**Proposed: notices returned, not callbacks.** The program passes an event
and an array; `wsk_feed` returns how many things changed and the rows that
need repainting, and the program does what it likes with them. Three reasons,
in order of weight:

1. It is the shape gBASIC's `window` already has: a backend reports events to
   the program, and gBASIC's Qt backend turns Qt's signals into exactly this.
2. It obeys the module rules with nothing to prove — a function pointer may be
   *passed*, but a stack of them per widget is the table §4.1 forbids.
3. It keeps the order of things visible: the widget has changed, the program
   is told, the program decides what else changes, the host repaints once.

Callbacks can be built *on* this, in a host that can afford them; the other
way round cannot.

**The repaint comes back with the notice.** `y0..y1` is the union of every row
the event changed — a pressed button, a scrolled pane, a field's line — so
the host makes one banded repaint per event and the program never calls
`wsk_rows` itself. That is layer 1's machinery (`wsk_rows`, `wsk_area_rows`,
`wsk_doc_rows`) used where it belongs.

### 8.6 The toolkit's state

    typedef struct {
        wsk_tree_t   *stack;   /* the caller's stacks, drawn in order */
        int           n;
        wsk_widget_t *item[...]; /* mutable widgets -- see below */
        /* pointer: down?, where, which widget, dragging?, scroll at down */
        /* focus: which stack and widget; caret in the focused field */
    } wsk_ui_t;

One structural change is needed in layer 1: `wsk_tree_t.item` is
`const wsk_widget_t *`, so that a panel can live in rodata. A toolkit changes
widgets — a mark when held, the text of a field — so it needs the caller's
*mutable* copy, which every interactive program already makes with
`wsk_clone`. The toolkit takes that copy; layer 1 keeps reading through the
const pointer. To be settled in the first iteration, by building it.

New widget state, for the same reason: a selected row, a checked box, a
caret position. Today a caret is a mark and its position is "the end". Each
is a field in `wsk_widget_t`, so each costs every widget a few bytes — §7's
question, asked as each arrives.

### 8.7 Focus and the keyboard

D9 said *never build a focus model*, and D16 narrows that to the C5. A
desktop and a P4 with a keyboard need it: one widget has focus, Tab moves it,
a key goes to it, and the focused widget is marked. **Focus order is
declaration order** — which D0 kept stable for exactly this. A tap or a click
moves focus to what it hit. Focus is toolkit state (§8.6), so the C5 build
does not contain it.

### 8.8 What "complete" means, so that it ends

**gBASIC Studio's window running on whisker.** That is checkable, it is the
user the charter names, and `window-gap.md` already measured it: 25 widget
words, 16 answered. What remains, grouped by the layer that owns it:

- **toolkit (layer 2):** press and click; drag-scroll and the wheel; field
  editing (moved out of `input.c`); focus and Tab; `check`; selection in a
  list.
- **drawing (layer 1):** `check`'s mark; `table` and `tree` as scrolling
  stacks of rows; overlays for `choice`, `menu`, `popup` — the one design
  question in layer 1 still open (`window-gap.md` §3); `memo` as a viewer,
  then an editor; `source`, last.
- **host (layer 3):** a desktop host with mouse, wheel and keyboard — the SDL
  host grows into it; the clipboard; dialogs; more than one window;
  proportional text, which is the host's to measure (§2.8) and is the largest
  thing a desktop asks that `/w0` never did.

### 8.9 Order of building

Each a working product, as the conventions ask, and each **proved by an
existing program shrinking onto it** — the same behaviour from less code is
the test that the toolkit did what the program used to.

1. **The toolkit's first iteration — `wsk_feed` with press, click and
   drag-scroll** (iterations 6 and 7 became nesting and wrap, §5f and §5g,
   when §9.9 put layout first). `wtap` and
   `wscroll` are rewritten on it, and must behave identically: the same taps
   named, the same drags ending on the same rows (`tools/evidence/drag/`
   already scripts a finger, so the comparison is mechanical). The SDL host
   gets the wheel.
2. **Then the field and focus.** `input.c`'s typing loop moves into
   the toolkit; Tab, a focus mark, `_KEY` and `_TEXT`.
3. **Then `check`, and selection in a list.**
4. **Overlays** — a design section of their own first, because they touch
   layer 1's model rather than adding a branch to it.
5. **`table`, `tree`, `memo` as a viewer**; then the gBASIC backend, measured
   against Studio's record.

### 8.10 Done criteria for the toolkit's first iteration

1. `wsk_feed` exists, with `wsk_event_t`, `wsk_note_t` and `wsk_ui_t`, in
   the default profile; `WSK_MINIMAL` compiles it out and a reduced build
   still draws byte-identically (§7.4's gate).
2. A press marks the button and repaints its rows; a release over it notices
   `_CLICK`; a release elsewhere does not. A control: a release in a gap
   notices nothing.
3. A drag past the slop scrolls the stack under the finger and notices
   `_SCROLL` with the new offset, clamped to the range **by the toolkit** —
   layer 1 still refuses out of range (D11), and the toolkit is a caller that
   never asks for it.
4. A tap without a drag in a scrolled list notices `_CLICK` on the row under
   it, by D13's rule.
5. The repaint rows returned equal what `wsk_rows`/`wsk_area_rows` give for
   the same change, checked against a full redraw byte for byte (§5e's test,
   reused).
6. `wscroll` on `wsk_feed` reproduces `tools/evidence/drag/`'s six checks
   unchanged, and is shorter.
7. The module rules: it links as a module and passes the double link.
8. On the P4: `wscroll` and `wtap` rebuilt, run by a person, timings no worse
   than 2026-11-11's (37.5 ms a frame, 112 rows).

### 8.11 Not decided here

- **The gBASIC backend's home** — in gBASIC beside `qt`, or in whisker. It
  depends on gBASIC's backend interface, which is gBASIC's to say.
- **Proportional text on a desktop.** Named, and pushed to the host (§2.8);
  how a host tells layer 1 how wide a string is will be decided when the
  desktop host needs it.
- **Callbacks.** Possible on top of notices (§8.5) in a host that wants them;
  not proposed.

---

## 9. Directions — what D16 opened, and which hardware each suits

**Written 2026-11-11, from a conversation with Matthew. Proposals, nothing
built**, except where a line says *ratified*. This is a map of what whisker
could be, so that the next iterations are chosen against all of it rather
than one at a time. §8 is the toolkit; this is everything around it.

| direction | C5 (presentation) | P4 (touch) | desktop / browser | whose work |
|---|---|---|---|---|
| nesting (D17) | refused by profile | yes | yes | whisker, layer 1 |
| overflow: wrap (D18) | yes, if affordable | yes | yes | whisker, layer 1 |
| alignment across the axis | yes | yes | yes | whisker, layer 1 |
| fonts as compact outlines | a stroke face | stroke or outline | outline | **RV-9** renderer; whisker measures |
| transitions | no | small regions only | yes | whisker, layer 2 |
| hit by shape, not rectangle | — | yes | yes | whisker, layer 1 |
| unconventional widgets | readouts | yes | yes | whisker |
| on-screen keyboard | no (D6) | **the first real app** | as needed | whisker + an RV-9 module |
| remote client | the C5 served *to* | either end | client | protocol: whisker; transport: RV-9 |
| browser client | — | — | yes | whisker (a page) |
| print | — | — | yes | a print host |

### 9.1 Layout: nesting, overflow, alignment

**Nesting (D17).** A widget may be a stack. Layout recurses; `hit` descends;
`rows` names a nested widget's rows. The ids stay document-unique (D4), so a
nested widget is still addressed by its id, not its path. The C5 profile
refuses a nested tree (`WSK_E_KIND`) and keeps today's flat code. Two
questions to settle by building: whether a stack's children are a pointer to
another array (simplest, and a pointer *passed* is position independent) or
an index into one flat array (cheaper, and closer to how gBASIC's record is
walked); and the stack cost of recursion on `/w0`, which adds to the
rasteriser's (CLAUDE.md, the writer's stack) and must be measured on the P4.

**Overflow (D18): refuse, scroll, wrap.** Wrap breaks a row into lines across
the axis — a column wrapping into columns is the same rule turned. A wrapped
stack's extent across the axis is however many lines it took, which its
parent then refuses, scrolls or wraps. **Text is items**: a text item is one
string, split into words at layout, each word placed by the same rule as a
button and each hit-testable, so a tapped word is free. Breaking a line needs
each word's width, which is §9.2's.

**A fourth answer, proposed: page.** Content that does not fit continues in
the next area — a print host's pages, or columns of a newspaper. Not ratified.

**Alignment across the axis.** Today a narrower widget is always centred
across the stack (`cross_max`). Start, end and stretch are missing, and a
form's labels want start.

### 9.2 Fonts — compact outlines, described once (a request to RV-9)

Today `/w0` draws text by **scaling one 10×20 bitmap** of DejaVu Sans Mono
(`font.c`, 95 × 20 × 5 = **9,500 bytes**): one face, fixed width, soft or
blocky away from its own size. Matthew's proposal, from drawing letters with
a Color Computer 2's `DRAW` strings: **describe each glyph as drawing
instructions, once, and scale them when drawn.**

- **Where the description lives decides the cost.** Outlines inside every
  document are 50–200 bytes a letter and a label would fill `SRC_MAX`. Held
  by the renderer, the document still says `<text>Hello</text>` — a byte a
  letter — and picks a face with `font-family`.
- **Compact in memory, not XML.** Path text is only the authoring form; a
  tool compiles it (as `mkfont.py` compiles today's font). Per glyph: its
  advance, then packed steps — move, line, curve — with each coordinate one
  signed byte on a small grid, scaled at draw. A stroke glyph is ~20–60
  bytes; printable ASCII ~3–5 KB, **less than today's bitmap**, at every size.
- **Two kinds, by hardware.** **Stroke** faces — single lines, the CoCo's
  letters, the public-domain Hershey fonts (1967) — cheap to draw, for the
  C5. **Outline** faces, filled, compiled from TrueType, for the P4 and
  desktop.
- **The same file gives whisker the widths**, so it can lay out proportional
  text and break lines (§9.1). That is whisker's half; the renderer is
  RV-9's, and this goes to the RV-9 session as a request.
- **The browser client** loads the same file once and expands each glyph to
  an SVG `<symbol>`, so every screen draws the same letters.

### 9.3 Transitions

Done by the toolkit, not the renderer: a value moves a step a frame and the
region it touches is repainted. The P4 says what that affords — a 112-row
pane at 37.5 ms is ~27 fps (§5e, on the board), so a button easing or a
panel sliding is fine and a full screen is not. A host too slow for one
**skips to the end state** — the one substitution whisker allows, because a
skipped animation never shows anything false about the program's state.

### 9.4 Unconventional widgets, and the one ability they share

**Built first without shapes, 2026-11-14: `wpiano`** (`src/module_piano.c`),
two octaves on the P4 through its speaker. A piano's black keys overlap its
white ones and whisker's stacks never overlap, so the keyboard is two rows:
the white keys, and above them the black keys with the narrow upper parts of
the white keys between, sized to sit exactly over their lower parts, with a
two-pixel spacer where two white keys meet. Every part a finger can land on
is a rectangle, and it looks like a piano. Matthew, playing it: *"no delay...
very nice sounds."* Getting there taught four things, three of them about any
whisker program on the board:

1. **Every finger is its own.** `/touch` names the finger of every event; the
   first version read them all as one, and two fingers re-struck each other's
   keys — heard as "only one sound channel". The mixing was never the limit.
2. **A tall repaint is a long pause.** Pressing a key repainted it, and a key
   is nearly the screen's height: about 200 ms on the P4 (§5e's 229 ms for
   552 rows), during which no finger was read and no note started. Notes came
   seconds late (27 of 27 over 50 ms, worst 5.7 s). Now a held key lights a
   strip a dozen rows high, and in every pass the sound is struck and written
   **before** anything is drawn. **Draw little, and draw after the thing a
   person is waiting for.**
3. **Time lost is skipped, not owed.** Catching up the sound a long repaint
   had missed filled the codec's 120 ms queue with sound that should already
   have played, and every note after waited behind it.
4. **A stack figure copied is a stack figure wrong.** 8,192 was taken from
   `wkeys`; the light strip's 1,792 bytes of widgets on the stack took the
   program past it, and the board **panicked twice** the moment it started.
   Measured, its own frame is 6,000 and its chain over 11,700 with the
   rasteriser beneath; it declares 16,384. CLAUDE.md said this in October
   ("do not hand a whisker program a stack number from modules/pic") and the
   rule held: measure it.

The run after all four, Matthew playing chords and runs: **46 notes, the
worst 47 ms from the finger's report to its note's first sound being
written, none over 50 ms** — against the run before's 27 of 27 late and
5.7 s at worst. No reboot.

**Hit by shape**: `hit` today tests rectangles. Testing a widget's own path —
a circle, a sector, a room on a plan — is what opens: radial menus (better
than drop-downs under a finger), dials and knobs turned by dragging round,
arc sliders and gauges, a floor plan whose rooms are buttons, a chart you tap
into (gBASIC's `chart.bas` already emits SVG), the P4's camera with each
detected face a widget, a piano keyboard on the P4's speaker. Conventional
widgets come first; these are why SVG was worth choosing.

### 9.5 The on-screen keyboard — the P4's first real app

Most of it is prototyped: `host/linux/input.c` types through a drawn
keyboard, and `tools/evidence/keyboard/` priced it. It is §8.9's second step
(the field and focus) made useful — a P4 that types with no USB keyboard —
and it settles the compact-keyboard decision still open in `roadmap.md` §4.

### 9.6 Served applications — an X11 for appliances

whisker's two halves already have X's shape: the application runs where its
data is, the screen is wherever the client is, and between them go **SVG
documents (or changed bands) one way and points and keys the other**. Unlike
X there is no drawing protocol to standardise — SVG is it. With RV-9 headed
for RISC-V 64, an RV-9 machine serving panels to screens elsewhere is a real
product for *non-office* applications: instruments, control panels, kiosks,
the house. The C5, which has no input, could be operated from anywhere. RV-9
already has `httpd`, peers and `execd`; the protocol is whisker's to write.

### 9.7 The browser client

A browser is a full SVG renderer with mouse, keyboard and touch. A page that
receives whisker's documents and posts events back makes every phone, tablet
and laptop a whisker screen with nothing installed — and answers §8's
desktop question of *what draws the SVG* for free. **Every widget already
carries an `id` (D4)**, so the page can replace the changed elements rather
than the document. **The caution:** a browser draws far more SVG than `/w0`,
so a host must declare what it can draw and whisker must refuse the rest (D8's
rule), or a panel that looks right in the browser breaks on the board.

### 9.8 Print

SVG is vector, so it prints sharp at any resolution, and browsers and
`rsvg-convert` turn it into PDF. A print host sets three things: **units** —
the `viewBox` in millimetres rather than device pixels, a host setting and
not a different layout; **pages** — §9.1's proposed *page* overflow; and a
**print palette**, since a dark background is for screens.

### 9.9 The order this suggests

**Layout before the toolkit**, because the toolkit addresses widgets in
stacks and nesting changes how they are addressed — building §8 on flat
stacks would mean building it twice.

1. **Nesting** (D17), then **wrap** with text as items (D18), then
   cross-axis alignment. Monospace widths are enough to start wrap; the font
   request (§9.2) goes to RV-9 in parallel.
2. **The toolkit, §8** — press, click, drag-scroll — on nested stacks.
3. **The keyboard on the P4** (§9.5).
4. **The browser client** (§9.7) — the first desktop-tier screen.
5. **Hit by shape** and the first unconventional widget (§9.4).
6. Overlays, `table`/`tree`, served applications, print.
