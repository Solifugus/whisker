# CLAUDE.md

Guidance for Claude Code working in this repository.

## What this is

**whisker** — a widget layer for a window you write SVG to. Two pure functions:
a widget tree becomes SVG text, and a point becomes the id of the widget under
it. Nothing else: no framebuffer, no device, no event loop, no allocation of
pixels.

It serves two hosts and belongs to neither:

- **RV-9** — `~/development/esp32/rv9`. A small modular OS for RISC-V on an
  ESP32-C5: one core at 240 MHz, ~¼ MB of RAM, no PSRAM. It already has the
  surface (`/w0`, write SVG to it), the rasteriser (`raster.c`, no ESP
  dependencies), text (`font.c`) and touch. Its roadmap heads a section
  *"Open — damage tracking, before the GUI direction."*
- **gBASIC** — `~/development/gbasic`. A C11 BASIC-family interpreter. Already
  emits deterministic SVG from pure gBASIC (`stdlib/chart.bas`, zero
  dependencies). Has an open question whisker would answer, in its
  `docs/gui_addressability_design.md` §0.3.

**gBASIC keeps GTK.** whisker is a candidate second backend and has to earn the
place. `docs/design.md` §6 records what would have to be true first, set now
while nothing is invested.

## Status

**Nothing is built, and it is gated twice.**

1. **`docs/rv9-review.md` is waiting on the RV-9 session.** Every claim this
   project makes about RV-9 was *read*, in one sitting, by someone who has never
   run it on hardware — and **three of the first claims were wrong**, listed in
   that document's §3 as a calibration on the rest. The review checks 16 claims
   and asks 6 questions.
2. **`docs/design.md` §4 names six decisions and none is ratified.**
   `docs/decisions.md` is the ledger; it is empty of outcomes on purpose.

**§4.0 blocks everything and is not ours to answer.** RV-9 has **no input
device** — no touch driver among its 29 drivers, an ST7789 panel that is
display-only, and no local keyboard. What it has is GPIO pins that can be read.
So whether whisker's second function is `hit(tree, x, y)` or `focus_next` /
`activate` depends on hardware nobody has chosen yet, and those are two
different libraries with two different test suites. Building the pointing
version and then meeting a rotary encoder means discarding the half that took
the work.

`render` is common to all three input futures, which is why **iteration 1 is the
renderer alone** (§5). That much can be built as soon as D0..D5 are settled.

## Read before working

1. `docs/design.md` — all of it. It is the whole project.
2. `docs/decisions.md` — what is actually settled, as against what is merely
   recommended. Read this *before* the design's §4, or you will mistake a
   recommendation for a verdict.
3. `docs/rv9-review.md` — what is still unverified, and what I already got wrong.
4. `~/development/Software-Development-Conventions.md` — the house conventions.
5. For anything touching the substrate: `~/development/esp32/rv9` —
   `components/rv9_io/src/drv_svgwin.c` (its header states two design decisions
   whisker adopts), `raster.c`, `modules/flick/flick.c` (the only working example
   of a clipped repaint, and the source of the timing figures), and
   `tools/hosttest/run.sh`, which builds the renderer natively and writes a PPM.

## Conventions

`~/development/Software-Development-Conventions.md` governs. What matters most
here:

- **Design document first**, updated before each iteration.
- **Iterations are working products**, not layers of scaffolding. The first is
  labels and buttons in one vertical stack; see §5.
- **Few to no dependencies**, and mature ones where unavoidable. whisker's whole
  argument is that the layer beneath it is already done.
- **Tabs for indentation.**
- Tests are kept for regression.

## The two budgets, which are not negotiable

Both are measured on real hardware and recorded in `docs/design.md` §2.2–§2.3.
Anything proposed here is priced against them:

- **4 KB of SVG** (`SRC_MAX`) is the ceiling on how much interface fits on a
  screen. Verbose output hits this long before it hits the frame budget, so
  terse emission is a requirement rather than an optimisation.
- **~30 ms to acknowledge a tap** at 320×172, ~52 ms at 1024×600, against
  ~59 ms and ~640 ms for a full redraw. A full re-render per interaction is
  therefore **not** affordable and a banded one is. Note the wording: `flick`
  times three *draws*, and with no input device nothing has ever measured a tap
  arriving.

  The mechanism is **not** a fragment of XML — `RV9_SVG_SS_ROWS` takes a row
  range and you write a **complete** document containing only what intersects
  those rows. And the clip is **vertical only**, so a repaint covers the full
  width of its rows and the band is cleared to the *device* background, not to
  what the scene drew. Both facts are in §2.2 and both constrain the layout
  model more than §4.3 does.

## Evidence standards

The same as gBASIC's, and they are the reason its measurements are worth
quoting:

- Tests first where feasible; keep goldens byte-exact, and a behavioural change
  that moves a golden is a deliberate, listed rebaseline.
- **Measure, don't assume.** Report what you could not verify.
- A test that asserts an absence needs a control, or it is satisfied by a
  harness that ran nothing. `hit` returning an id is satisfied by "always return
  the first widget" unless a point in the gaps is asserted to return nothing.
- Never mark anything "verified".
- **A picture can be geometrically perfect and still be wrong in a way only an
  eye catches.** RV-9's own lesson, from circles that came out lumpy with the
  right radius and the right area. Render to a PPM and look at it.

## What whisker must never require

Recorded as not done in RV-9's window, so whisker may not depend on any of it:
font families and weights, rotated text, arc x-axis-rotation, opacity,
gradients, documents over 4 KB. And no CSS — presentation attributes only, which
is the line RV-9 drew to avoid a cascade, a selector engine and a box model.

whisker's look is flat because its substrate is.
