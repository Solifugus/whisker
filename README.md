# whisker

**A GUI system for a window you write SVG to** — presentation on the
smallest boards, touch controls on the P4, and in time a desktop toolkit that
gBASIC can use in place of Qt (`docs/decisions.md` D16, `docs/design.md` §8).

*A cat's whiskers are how it reads a surface it cannot see. RV-9's mascot is an
armoured robot cat; this is the part that feels where you touched.*

**Where it is going (D16, 2026-11-11).** What is built is the bottom layer:
two pure functions, a widget tree to SVG and a point to a widget. Above it
comes a **toolkit** — input events in, widget state updated, *what changed*
out — for the devices that have input, and below both a **host** per
platform. A device with nothing to press compiles the toolkit out. Designed
in `design.md` §8; nothing of it is built yet.

**Status of the drawing layer.** `src/whisker.h` renders a panel, repaints one band of it, says which widget is
under a point, composes several stacks into one document, and draws a text
field you can type into. `src/run.sh --module` runs every done-criterion in
`design.md` §5, §5b, §5c, §5d, §5e, §5f, §5g, §5h, §5i, §5j, §5l, §5m and §7 — **457 checks** plus the profile gate,
the pixels through RV-9's own rasteriser, seventeen module builds with the
double-link, and a person looking at the pictures. `host/linux/run.sh --test`
adds **19** more.

**And any stack can scroll** (§5e, 2026-10-24) — the first step toward
gBASIC's Qt-era `window` and a desktop. `overflow = WSK_SCROLL` and an offset
the caller holds; a widget half in view is cut by arithmetic, because there is
no `clipPath` to cut it with, and its text is drawn whole or not at all. A long
list is handed over a slice at a time (`wsk_slice`), which draws byte for byte
what the whole list would: `wscroll` drags ten thousand rows on the P4 while
holding none of them. **On the board, 2026-11-11:** a 112-row pane drags
at 37.5 ms a frame, a full screen of rows at 229.

**And stacks nest** (§5f, 2026-11-11): a widget may be a stack, so a window
is one tree — a toolbar row, a body, a status row, each holding more. Every
widget is found by its id at any depth, or by a path checked link by link
(`/body/opts/b`), and a branch repaints by naming it.

**And stacks wrap** (§5g): a row that runs out of width carries on below,
a line at a time. A paragraph is word labels in a wrapping row, and a text
window is that row inside a scrolling column — nothing about text is special.
And a paragraph can be **one item** (§5h): its words flow as the row's units,
a line of them is one `<text>`, and the same page costs less than half the
bytes.

**And the toolkit has begun** (§5i): `wsk_feed` takes a touch or a click and
answers with what changed — this button was clicked, that list scrolled to 40
— and the rows to repaint. Pressing, tapping with a shaky finger and dragging
a list are whisker's now, not every program's.

**And fields edit** (§5j): typing goes to the focused field, a caret moves,
Tab walks the fields — with a real keyboard on Linux (`host/linux/run.sh
--edit`) and with `wkeys`, an on-screen keyboard, on the P4.

**And a browser is a screen** (§5k): `host/web/run.sh` serves a whisker
program to any browser — a laptop, or a phone on the same network — with no
change to the program. The page repaints only the rows each document names,
exactly as the board does, and a test in headless Chrome holds it to that
pixel for pixel.

**And menus open over the window** (§5l): any tree can be opened over the
rest, hanging from the widget that opened it, drawn on top and asked first
for taps — and a band repaint under an open menu is exact.

**And forms are complete enough to fill in** (§5m): checkboxes, a list that
keeps a choice, and Tab that reaches buttons as well as fields.

**And there is now a program to leave running.** `src/module_stat.c` — staged
in RV-9's tree as `wstat` — puts the board's die temperature, free heap and
uptime on the C5 panel and repaints only the rows whose value actually moved.
A tick on which nothing changed writes **nothing at all**: no setstat, no
bytes. That restraint is the whole point, and it is why the readings are at a
resolution a person would choose rather than the finest the sensor offers.
It is the first whisker program written to be *used* rather than to show that
something works, and it exists because D9 settled what a write-only panel with
no controls is for.

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
panel, the P4 moves every figure, and gBASIC's Qt renders on its own loop where
none of this arithmetic exists. whisker streams so that buffering stays the
*caller's* choice: a sink that appends to an array is the buffer form. The
design's §2.8 is the standing form of that caution.

**It scales down as well as up.** The default build is everything; `-DWSK_MINIMAL`
is the C5's profile and leaves out the text field and the corner marks — the two
things that live inside the per-widget loop every caller links, and so the only
two the linker cannot drop by itself. That is **668 bytes, 20 %** of a small
panel's code (`tools/evidence/profiles/`). Everything else already scales down
by linkage: a program that never names `wsk_hit`, `wsk_compose` or `wsk_rows`
does not contain them, checked with `nm`. A reduced build is **not a second
renderer** — it draws byte-identically for the widgets it keeps, and *refuses*
what it lacks by name rather than substituting, both gated in `src/run.sh`.
At the other end nothing was needed: gBASIC's declarative `gui` names five
component kinds and whisker has all five.

- `docs/roadmap.md` — **what is left**, in what order, and what would let us
  stop. Three gates: what the C5 is *for* (a question, not code), what only the
  P4 can measure, and gBASIC's answer.
- `docs/decisions.md` — **read first.** Nine decisions; D0–D5 ratified 2026-09-29, D6–D8 since.
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
- `docs/studio-gap.md` — the distance to **gBASIC Studio**, the IDE, measured
  the same way: four capabilities, not a port.
- **Both gap documents measured gBASIC's GTK tree, which no longer exists.**
  gBASIC removed GTK on 2026-10-22 and is Qt-only, with a declarative `window`
  read by a backend.
- `docs/window-gap.md` — the current answer, measured against `window` and
  Studio on it: Studio's window is now a pure record that touches no toolkit,
  and whisker answered 15 of the 25 words it uses — 16 since §5e's scrolling.
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
- **Not a Qt replacement for gBASIC.** gBASIC keeps Qt (it dropped GTK on
  2026-10-22). whisker is a *candidate second backend* to the same `window`
  declaration, and it earns that place by working, or it does not get it.
- **Not a browser.** Presentation attributes only, no CSS — the same line RV-9
  drew, for the same reason: a style language means a cascade, a selector engine
  and a box model.

## Two hosts, three surfaces

whisker exists because the same layer is missing in two places at once, and
neither should own it.

| | |
|---|---|
| **RV-9** (`~/development/esp32/rv9`) | Has the surface — `/w0`, plus a row-range setstat for banded repaint. A quarter of a megabyte of RAM, one RISC-V core at 240 MHz. **The harder constraint, so it sets the budget.** |
| **gBASIC** (`~/development/gbasic`) | Already emits deterministic SVG from pure gBASIC (`stdlib/chart.bas`, 1,612 lines, zero dependencies). Its GUI is Qt since 2026-10-22: a declarative `window` read by a backend, `qt` the first (its `docs/qt_gui_design.md`). |

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
