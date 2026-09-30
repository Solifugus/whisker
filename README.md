# whisker

**A widget layer for a window you write SVG to.**

*A cat's whiskers are how it reads a surface it cannot see. RV-9's mascot is an
armoured robot cat; this is the part that feels where you touched.*

**Status: nothing built. The review is answered; the design needs revising
against it.**

- `docs/design.md` — the project. Read all of it first, then read the answers,
  because ten of its claims need changing.
- `docs/decisions.md` — what is settled (scope only; every design decision is
  still open).
- `docs/rv9-review.md` — the questions put to the RV-9 session.
- `docs/rv9-answers.md` — **the replies, 2026-09-29.** Thirteen claims stand,
  two need a correction, and one was right *and undiscovered* — checking it
  found a live bug in RV-9, now fixed.

**The question that blocked everything is answered, and by a purchase.** A
Waveshare ESP32-P4-WIFI6-Touch-LCD-7B — 1024×600, five-point capacitive touch —
was ordered 2026-09-27 and arrives in October. So the second function is
`hit(tree, x, y)`. The ESP32-C5 stays the real-time target with `/gpio` buttons
and probably never gets touch, so keep a focus order *possible* without building
one. See `docs/rv9-answers.md` §2.1.

**Two numbers in `docs/design.md` are now wrong.** A tap acknowledgement costs
**38 ms against 56** for a full redraw, not 30 against 59 — the old figures were
measured on a clipped repaint that left two black strips on the panel. See
§1.3 of the answers.

---

## What this is

Two pure functions over a description of a user interface:

1. given a widget tree, **produce SVG**
2. given a point, **say which widget was hit**

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

## The two hosts

whisker exists because the same layer is missing in two places at once, and
neither should own it.

| | |
|---|---|
| **RV-9** (`~/development/esp32/rv9`) | Has the surface — `/w0`, plus a row-range setstat for banded repaint — and **not** the input. Its roadmap heads a section *"Open — damage tracking, before the GUI direction."* A quarter of a megabyte of RAM, one RISC-V core at 240 MHz. **The harder constraint, so it sets the budget.** |
| **gBASIC** (`~/development/gbasic`) | Already emits deterministic SVG from pure gBASIC (`stdlib/chart.bas`, 1,612 lines, zero dependencies). Wants an addressable widget tree; see its `docs/gui_addressability_design.md` §0.3. |

A layer that satisfies RV-9 will satisfy a Linux host. The reverse is not true,
which is why the small target is the one to build against — the same argument
gBASIC made for measuring its Windows port with a real cross-compiler instead of
estimating it.

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
