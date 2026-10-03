# whisker

**A widget layer for a window you write SVG to.**

*A cat's whiskers are how it reads a surface it cannot see. RV-9's mascot is an
armoured robot cat; this is the part that feels where you touched.*

**Status: whisker does everything gBASIC's declarative `gui` asks for.**
`src/whisker.h` renders a panel, repaints one band of it, says which widget is
under a point, composes several stacks into one document, and draws a text
field you can type into. `src/run.sh --module` runs every done-criterion in
`design.md` §5, §5b, §5c and §5d — **135 checks**, the pixels through RV-9's own
rasteriser, three module builds with the double-link, and a person looking at
the pictures. `host/linux/run.sh --test` adds **19** more.

**You can type on it now.** `host/linux/run.sh --input` puts a field and a
36-key keyboard on screen; click the keys and the characters appear. The touch
monitor on this desk enumerates and reports nothing, but a mouse click is a
single point, which is exactly what `hit` takes and exactly what the P4's panel
will deliver — so the whole loop was finished and watched rather than waited for
(D7). Typing one character costs 314 bytes and repaints four bands.

**The last three iterations were each chosen by a measurement.** The gap to
gBASIC's `gui` turned out to be one widget, `input`, and on RV-9 that means an
on-screen keyboard. Drawing one found that two stacks could not share a screen
at all — each render backed the whole device, so four keyboard rows came out as
one. Composing fixed that and is also what makes the keyboard fit. Then the
keyboard's cost was measured three ways, which found an undocumented
`MAX_CONTOURS` of 16 that silently dropped everything past the sixteenth
subpath of a path. Then the field itself, which is **115 bytes under `SRC_MAX`**
with a full keyboard beside it.

**Two of those went back to RV-9, and both came back changed.** The silent
truncation is now logged *and* the cap is 64 — different questions, different
answers — with the contour buffer moved off the stack so that raising it did not
raise the stack requirement of every program that writes to `/w0`. All four
ceilings are askable now, through `getstat(RV9_SVG_GS_LIMITS)`; they had been in
no RV-9 document at all. So the compact keyboard went from the broken form to
the **cheapest** one: 1,879 bytes against the widget form's 3,721, drawing
pixels identical to it (`tools/evidence/keyboard/`).

**And one thing came back that is whisker's own.** Bisecting two whisker
programs' stack declarations found both wrong, and the cause is not whisker's
frames: **drawing to `/w0` renders on the writer's stack**, and whisker streams,
so the `put()` that finishes a document runs RV-9's whole rasteriser with every
layout frame still live beneath it. The two depths add. The obvious fix —
buffer the document and write once — is **measured and refused**: it saves 256
bytes of stack and spends 4,096 of statics, out of one process footprint
(`tools/evidence/stack/`).

Which settles what an RV-9 *module* should pass, and nothing more. `/w0` is **a**
display system, not **the** display system — RV-9 has a second driver on the same
panel, the P4 moves every figure, and gBASIC's GTK renders on its own loop where
none of this arithmetic exists. whisker streams so that buffering stays the
*caller's* choice: a sink that appends to an array is the buffer form. The
design's §2.8 is the standing form of that caution.

- `docs/decisions.md` — **read first.** Six decisions, all ratified 2026-09-29.
- `docs/design.md` — the project, revised against the review.
- `docs/rv9-answers.md` — the RV-9 session's replies: the measurements, and four
  things the review did not think to ask.
- `docs/rv9-review.md` — the questions as they were put. Historical.
- `docs/review-2026-09-30.md` — the substrate run rather than read: five
  findings, three of them live defects in RV-9. It changed §5's done-criteria
  and the shape of the API. Evidence in `tools/evidence/`.
- `docs/notation-sketch.md` — a compact way to write a panel down, prototyped
  and measured in `tools/notation/`. Not a decision, and not the wire format.
- `docs/gbasic-gap.md` — the distance to gBASIC's declarative `gui`, measured
  rather than argued. It is one widget, and it chose what iteration 3 would be.
- `docs/rv9-ask-2026-10-02.md` and `docs/rv9-answers-2026-10-02.md` — a defect
  reported to RV-9, what they did about it, and the stack finding that came
  back the other way. Question and answer kept separable, as before.
- `src/` — the library. `whisker.h` is the whole of it.
- `host/linux/` — whisker running: a window, a blit, a point. 276 lines over
  RV-9's own rasteriser, so what it draws is what the board draws.

**The question that blocked everything was answered by a purchase.** A Waveshare
ESP32-P4-WIFI6-Touch-LCD-7B — 1024×600, five-point capacitive touch — was
ordered 2026-09-27 and arrives in October. So the second function is
`hit(tree, x, y)`. The ESP32-C5 stays the real-time target with `/gpio` buttons
and probably never gets touch, so a focus order stays *possible* without being
built.

**Asking for the review paid twice.** One claim was right *and undiscovered*:
checking it found a live bug in RV-9's `flick`, which had shipped two black
strips beside its button for months because a clipped repaint is cleared to the
device background rather than the scene's. Fixed — and the budget moved with it.
**A tap costs 38 ms of work against 56 for a full redraw**, not 30 against 59.
The old numbers were measured on a picture that was wrong. And the *latency*
is **19 ms** — the press alone — because the release repaint happens after the
finger has lifted (`rv9-answers.md` §7).

And one recommendation was overturned by measurement: a **row** of buttons costs
1.77× a column, not something to avoid, so the layout model allows both.

---

## What this is

Two pure functions over a description of a user interface:

1. given a widget tree, **produce SVG** — iteration 1
2. given a point, **say which widget was hit** — iteration 2, when the touch
   panel arrives

That is the whole surface. Everything else — drawing, text, rasterising,
clipping, getting the tap from the hardware — is already done by the host, and
whisker never touches a framebuffer.

## What this is not

- **Not a renderer.** RV-9's `raster.c` (306 lines, no ESP dependencies) already
  turns SVG into pixels, and `font.c` already draws text.
- **Not a window system.** RV-9's `/w0` already is one: a device you write SVG
  to, in the OS-9 tradition where a window is a path.
- **Not a GTK replacement for gBASIC.** gBASIC keeps GTK. whisker is a
  *candidate second backend*, and it earns that place by working, or it does
  not get it.
- **Not a browser.** Presentation attributes only, no CSS — the same line RV-9
  drew, for the same reason: a style language means a cascade, a selector engine
  and a box model.

## Two hosts, three surfaces

whisker exists because the same layer is missing in two places at once, and
neither should own it.

| | |
|---|---|
| **RV-9** (`~/development/esp32/rv9`) | Has the surface — `/w0`, plus a row-range setstat for banded repaint. A quarter of a megabyte of RAM, one RISC-V core at 240 MHz. **The harder constraint, so it sets the budget.** |
| **gBASIC** (`~/development/gbasic`) | Already emits deterministic SVG from pure gBASIC (`stdlib/chart.bas`, 1,612 lines, zero dependencies). Wants an addressable widget tree; see its `docs/gui_addressability_design.md` §0.3. |

But RV-9 runs on **two boards that differ where it matters**, so the contract
faces three surfaces, not two (`docs/design.md` §2.7):

- **ESP32-C5** — ST7789 panel that holds its own framebuffer, no touch, `/gpio`
  only. The real-time target, and the one the budget is measured on.
- **ESP32-P4** (October) — 1024×600 five-point touch, MIPI-DSI with **no
  framebuffer of its own**, so RV-9 owns the pixels in PSRAM and horizontal
  clipping becomes possible. The personal-computing target.
- **Linux** — everything, which is why it must not be the one that defines the
  contract.

A layer that satisfies the C5 will satisfy the other two. The reverse is not
true, which is why the small target is the one to build against — the same
argument gBASIC made for measuring its Windows port with a real cross-compiler
instead of estimating it. **But do not bake the C5's limits into the model**:
the machine that will actually run a seven-inch touch interface is the P4.

## Why it is a separate project

Because it serves two hosts and belongs to neither. Kept inside RV-9 it would
quietly grow ESP-IDF assumptions — `drv_svgwin.c` already needs about forty
lines of stubs to build off-target, while `raster.c` needs none, so RV-9 has
half-made this split already and knows why.

The precedent is gBASIC Studio: developed in its own project against gBASIC's
platform surface, and what came *back* into gBASIC was only what turned out to
be general.

## Conventions

`~/development/Software-Development-Conventions.md` governs, and `CLAUDE.md`
records what is specific here. In short: few dependencies, tabs, a design
document updated before each iteration, and iterations that are each a working
product rather than a layer of scaffolding.
