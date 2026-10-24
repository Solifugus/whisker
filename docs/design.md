# whisker — design

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
