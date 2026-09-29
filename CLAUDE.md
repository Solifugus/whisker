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

**Nothing is built.** This repository holds `docs/design.md` and the
measurements it rests on.

**Do not write code until `docs/design.md` §4 is decided.** It names five
decisions — language, one implementation or two, how much layout, whether the
address space is the SVG `id`, and who retains the tree. Each has a
recommendation and an argument; none has been ratified. The document this grew
out of was criticised for designing three chapters on top of a layer choice it
never asked about, and §4 exists so that is not repeated here.

## Read before working

1. `docs/design.md` — all of it. It is 261 lines and it is the whole project.
2. `~/development/Software-Development-Conventions.md` — the house conventions.
3. For anything touching the substrate: `~/development/esp32/rv9` —
   `components/rv9_io/src/drv_svgwin.c` (read its header comment, which states
   two design decisions whisker adopts), `raster.c`, and
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
- **~30 ms for a tap** at 320×172, ~52 ms at 1024×600, against ~59 ms and
  ~640 ms for a full redraw. A full re-render per interaction is therefore
  **not** affordable and a band re-render is — which is why `render` must be
  able to produce a fragment for one widget, not only a whole document.

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
