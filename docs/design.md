# whisker — design

**Status:** **Iteration 1 is built** (`src/whisker.h`, 2026-09-30) and every
done-criterion in §5 passes — the suite, the XML check, the pixels through
RV-9's own rasteriser, and the module build with its position-independence
check. It has not run on the board. **All six decisions in §4 are ratified**
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

  **Read `modules/flick/flick.c` before quoting this.** It times *three draws*,
  not an input round trip: the whole window, then the button's rows in a pressed
  colour, then the button's rows back to normal. Its own header says *"2 and 3
  together are what a person experiences as one tap."* **No tap has ever been
  measured end to end** — the touch panel arrives in October (§2.6) — so what is
  known is what it costs to *answer* one, which is the number that was in doubt.
- widening to 1024×600 costs only **1.7×** (not 3.2× — the per-band re-parse does
  not care about width), so a tap is about **66 ms at 1024×600** against ~640 ms
  for a full screen.
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
  works at 320×172 and clips the wrong strip at 1024×600.
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

`SRC_MAX` 4,096 · `MAX_PTS` 256 · `MAX_CONTOURS` 16 · `MAX_DEPTH` 8.

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
1024×600 is 11.2× the pixels of 320×172 and the **same 4 KB of description**. At
320×172 four buttons fill the panel and 4 KB is enormously generous; at 1024×600
a realistic panel of twenty controls with labels is where it starts to bind. So
**`SRC_MAX` is comfortable on the C5 and is the binding constraint on the P4** —
the opposite of this document's assumption on every other axis.

Treat it as a **device property to ask about, not a constant**: it will differ
between the two boards. There is no getstat for it today; RV-9 has offered to add
one, which is cheap to get right before there is a caller.

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
It is also how a grid arrives later without the widget model changing: two
stacks are two trees over one device, each with its own area.

A widget also carries a **mark** — `WSK_MARK_CORNERS` today — which is how a
pressed state is shown without the shadows and gradients §2.4 rules out. It is
the caller's to set, because whisker holds no state: a press is something the
host knows and the tree records, like the fill beside it.

**Explicitly not in iteration 1:** `hit` (iteration 2), the gBASIC binding,
focus, keyboard, scrolling, text entry, a grid, nesting.

**What iteration 1 proves, stated plainly:** that a widget set can be described,
drawn, and repainted a band at a time within RV-9's budgets, in code that a
module can legally link. It proves nothing about interaction, and until the P4
arrives nothing here can.

### Iteration 2, named now so iteration 1 does not foreclose it

`hit(tree, x, y, device_h)`, against the touch panel, plus **the measurement
nobody has taken**: finger-down to pixel-changed, end to end. That number
includes an I²C controller, an interrupt, a process wakeup and a repaint, and
the 38 ms of §2.2 is only the last of those.

---

## 6. What would have to be true before gBASIC took this

Recorded so that the bar is set now, while nothing is invested, rather than
argued later when something is:

- whisker runs a real RV-9 interface on real hardware, and somebody uses it.
- The two functions are stable enough that gBASIC's renderer seam can be written
  against them without a version negotiation.
- A Linux host for the contract exists and is small — if it needs a toolkit
  underneath, whisker has bought gBASIC nothing, because gBASIC already has one.

If those are not true, the right answer is that whisker served RV-9 and gBASIC
kept GTK. That is a success, not a failure: the reason for a separate project is
to make *not* adopting it cheap.
