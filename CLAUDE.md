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
  dependencies) and text (`font.c`). It has **no touch** — that arrives on the
  P4 board in October (see Status). Its roadmap heads a section
  *"Open — damage tracking, before the GUI direction."*
- **gBASIC** — `~/development/gbasic`. A C11 BASIC-family interpreter. Already
  emits deterministic SVG from pure gBASIC (`stdlib/chart.bas`, zero
  dependencies). Has an open question whisker would answer, in its
  `docs/gui_addressability_design.md` §0.3.

**gBASIC keeps GTK.** whisker is a candidate second backend and has to earn the
place. `docs/design.md` §6 records what would have to be true first, set now
while nothing is invested.

## Status

**Nothing is built. Both gates are open — start iteration 1.**

1. **The RV-9 review is answered** (`docs/rv9-answers.md`, 2026-09-29). Thirteen
   of sixteen claims stood. One of them was right *and undiscovered*: checking it
   found a live bug in RV-9's `flick`, now fixed, which is why the tap budget
   moved.
2. **All six decisions are ratified** (Matthew, 2026-09-29). `docs/decisions.md`
   is the ledger and wins wherever it disagrees with the design's §4 reasoning.

**D0 was settled by a purchase**: a Waveshare ESP32-P4-WIFI6-Touch-LCD-7B —
1024×600, five-point capacitive touch — arrives in October, so the second
function is `hit(tree, x, y)`. The C5 keeps `/gpio` and probably never gets
touch, so keep declaration order stable and a focus model stays addable without
being built.

**Iteration 1 is the renderer alone** (`docs/design.md` §5): labels and buttons
in a single-axis stack, column *or* row, header-only C. `hit` is iteration 2,
when there is a panel to tap.

## Read before working

1. `docs/decisions.md` — six ratified outcomes, one screen. **First**, so that
   the design's §4 arguments read as history rather than as open questions.
2. `docs/design.md` — all of it. It is the whole project, and it has been
   revised against the review, so it and the answers no longer disagree.
3. `docs/rv9-answers.md` — the measurements and the four things the review did
   not think to ask. Read §2.5 before writing any C, and §4 before deciding how
   anything is sized.
4. `docs/review-2026-09-30.md` — the substrate checked by running it. Five
   findings; §7 lists what they change. Read it before §5, because two of them
   change iteration 1's done-criteria and one changes what a tree may look
   like. The programs behind every number are in `tools/evidence/`.
5. `docs/rv9-review.md` — the questions as they were put. Historical; kept so
   question and answer stay separable.
6. `~/development/Software-Development-Conventions.md` — the house conventions.
7. For anything touching the substrate: `~/development/esp32/rv9` —
   `components/rv9_io/src/drv_svgwin.c` (its header states two design decisions
   whisker adopts), `raster.c`, `modules/flick/flick.c` (the only working example
   of a clipped repaint, and the source of the timing figures), RV-9's
   `docs/reference.md` §9 for `RV9_SVG_SS_ROWS` and §5 for the module rules, and
   `tools/hosttest/run.sh`, which builds the renderer natively and writes a PPM.

## Conventions

`~/development/Software-Development-Conventions.md` governs. What matters most
here:

- **Design document first**, updated before each iteration.
- **Iterations are working products**, not layers of scaffolding. The first is
  labels and buttons in one single-axis stack, column or row; see `design.md` §5.
- **Few to no dependencies**, and mature ones where unavoidable. whisker's whole
  argument is that the layer beneath it is already done.
- **Tabs for indentation.**
- Tests are kept for regression.

## The two budgets, which are not negotiable

Both are measured on real hardware and recorded in `docs/design.md` §2.2–§2.3.
Anything proposed here is priced against them:

- **4 KB of SVG** (`SRC_MAX`) is the ceiling on how much interface fits on a
  screen. It is *raisable in one line*, so respect it for the right reason: the
  cost is **time, paid once per band**, roughly 0.34 ms + 0.00036 ms/byte on the
  host, so a full 4 KB document costs ~2.5× a terse 1 KB one. It also does **not
  scale with the screen**, which makes it comfortable on the C5 and the binding
  constraint on the P4.
- **~38 ms to acknowledge a tap** at 320×172, ~66 ms at 1024×600, against
  ~56 ms and ~640 ms for a full redraw. A full re-render per interaction is
  therefore **not** affordable and a banded one is. Note the wording: `flick`
  times three *draws*, and **nothing has yet measured a tap arriving** — that is
  iteration 2's first measurement.

  *These were 30 ms and 59 ms until 2026-09-29.* The old figures were measured on
  a clipped repaint that left two black strips on the panel. Do not quote the old
  ones from any document that still carries them.

  The mechanism is **not** a fragment of XML — `RV9_SVG_SS_ROWS` takes a row
  range and you write a **complete** document containing only what intersects
  those rows, **carrying the backing for those rows** or the scene's background
  disappears from them. The range is in **device rows, not viewBox units**, and
  snaps outward to 8-row bands. The clip is **vertical only** *today*, which is a
  software choice rather than a hardware limit — so do not let the layout model
  depend on it.

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

## The module rules bind the code, not just the build

**Read `docs/rv9-answers.md` §2.5 before writing any C.** A program on RV-9 *is*
a module, so whisker inherits every rule a module obeys: no `.data`, no `.bss`,
no libc, **no 64-bit division**, **no `static const` array of pointers**, no
`switch` over string literals, no local array initialised from a literal. Each of
the last three compiles to a table of absolute addresses and breaks position
independence, which `tools/build_modules.sh` *proves* by linking each module
twice at different bases and comparing the bytes.

**A widget dispatch table is the natural design here and is exactly what that
build refuses.** RV-9 hit this twice; the second time cost a working module a
rewrite. Iteration 1's done-criteria include compiling under those flags, so the
failure happens loudly now rather than quietly in iteration 3.

## What whisker must never require

Recorded as not done in RV-9's window, so whisker may not depend on any of it:
font families and weights, rotated text, arc x-axis-rotation, opacity,
gradients, documents over 4 KB. And no CSS — presentation attributes only, which
is the line RV-9 drew to avoid a cascade, a selector engine and a box model.

whisker's look is flat because its substrate is.
