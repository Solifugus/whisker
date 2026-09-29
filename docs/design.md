# whisker — design

**Status:** Proposed. Nothing implemented. §4 lists the decisions that have to be
made before the first line of code.

Where this document says **measured**, it means somebody ran it and the source is
named. Where it says **propose** or **argue**, it is reasoning and may be wrong.

---

## 1. The thesis

A window you write SVG to needs no graphics API. RV-9 established that: `/w0` is
a path, a picture is a stream of bytes, and `cat picture.svg > /w0` puts it on
the screen. What such a window cannot do is tell you **which thing the user
touched**, because it holds a document, not a set of objects.

So the missing layer is small and it is two pure functions:

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

### 2.2 The performance already measured on the board

From `docs/roadmap.md`, *"Open — damage tracking, before the GUI direction"*,
measured with `tools/hosttest/scale.c` and on the C5 at 320×172:

- full redraw **59 ms**; press **15 ms**; release **15 ms** — so a whole tap is
  **30 ms** against 59 for one full frame, once band clipping is in
  (`RV9_SVG_SS_ROWS`).
- widening to 1024×600 costs only **1.7×** (not 3.2× — the per-band re-parse does
  not care about width), so a tap is about **52 ms at 1024×600** against ~640 ms
  for a full screen.
- 1024×600 costs **24,576 bytes** of buffers and 2.9× the time of 320×172 for
  11.2× the pixels. *Resolution is not the problem.*
- the C5 is about **96×** slower than the host on `pic.svg` (82 ms vs 0.85 ms).

RV-9's own conclusion, which whisker takes as its budget: **"tap-to-act with a
flash to confirm is affordable today."** A retained object tree with per-object
bounding boxes is an optimisation for later, not a gate.

**The consequence for whisker's design:** a full re-render per interaction is
*not* affordable, and a re-render of the touched band is. So `render` should be
able to produce **a fragment for one widget**, not only a whole document. That is
a requirement, and it comes from a measurement rather than from taste.

### 2.3 The ceilings that do not move with screen size

`SRC_MAX` 4,096 · `MAX_PTS` 256 · `MAX_CONTOURS` 16 · `MAX_DEPTH` 8.

**4 KB of SVG is the real constraint on how much interface fits on a screen**, and
it is whisker's to respect. A widget set that emits verbose SVG will hit this
long before it hits the frame budget. Terse output is a design requirement, not
an optimisation.

### 2.4 What RV-9's window does *not* do

Recorded as not done, in its own roadmap: font families and weights, rotated
text, arc x-axis-rotation, opacity, gradients, documents over 4 KB.

whisker must not require any of them. That rules out a good deal of ordinary
visual design — shadows, gradient fills, translucent overlays — and the honest
framing is that **whisker's look is flat because its substrate is**, not because
flat is fashionable.

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

---

## 3. The substrate contract

whisker talks to a host through **two byte channels and nothing else**:

```
out:  SVG text          (a whole document, or a fragment plus the band it occupies)
in:   pointer events    (down / up / move, with a coordinate)
```

This is not an abstraction invented for portability — **it is what RV-9 already
is.** `/w0` is a path you write to; the touch controller is another device. A
Linux host that satisfies the same contract is a small program: a window, a
rasteriser, and a loop that reports clicks.

**Argued, not measured:** because the contract is bytes rather than a function
table, a host may sit at the far end of a pipe, a socket or a serial line. A
gBASIC program on a workstation driving a panel on an RV-9 board is the same
contract with a longer wire. That is worth *not* designing against, but it costs
nothing to leave possible.

---

## 4. Five decisions, before any code

These are named rather than assumed, because the gBASIC document this grew out
of was criticised for exactly that — it designed three chapters on top of a layer
choice it never asked about.

### 4.1 What language?

| | For | Against |
|---|---|---|
| **C** | matches RV-9 exactly, builds as an ESP-IDF component, links into gBASIC as a native module; `raster.c` proves ESP-free C is achievable here | gBASIC gains a `HAVE_*` guard and a dependency |
| **Zig** | the house preference for systems work; exports a C ABI | adds a toolchain to an ESP32-C5 RISC-V build that currently needs none |
| **gBASIC** | zero toolchain, and `chart.bas` proves gBASIC writes SVG well | **cannot run on RV-9** — a tree-walking C interpreter does not fit a quarter megabyte, so the harder host would be unserved |

**Recommendation: C.** RV-9 is the binding constraint and C is what it speaks.
Zig's advantages are real and are about *authoring*; the cost lands on the
smaller target's build, which is the one that can least afford it.

### 4.2 One implementation, or a specification and two?

The alternative to a shared C core is a written specification plus a corpus of
`tree → expected SVG` and `point → expected id` fixtures, implemented twice —
once in C for RV-9, once in gBASIC for gBASIC.

It is genuinely tempting, because a gBASIC implementation would need no native
module, no build integration and no guard, and `chart.bas` shows it would be
pleasant to write.

**Recommendation: one implementation.** Two representations of one declaration
drift; gBASIC's own test suite has tripwires against exactly this shape in four
places. A shared corpus makes the drift *visible* but does not stop it, and the
failure would be a widget that is hit-tested in one place and drawn in another.

### 4.3 How much layout, given that a box model is refused?

RV-9 refused CSS partly to avoid a box model, and a widget set needs *some* way
to say "these three buttons, in a row, filling the width."

**Propose the smallest thing that is not nothing:** a single-axis stack (row or
column) with a per-child weight, and padding as one number. No grid, no
flex-wrap, no min/max, no baseline alignment. That is enough for a control panel
and not enough for a document, which matches what the substrate is for.

**This is the decision most likely to be got wrong by being generous.** Every
layout feature is paid for twice — once in the 4 KB output ceiling, once in the
30 ms tap budget.

### 4.4 Is the address space the SVG `id`?

**Propose: yes**, and this is the decision with a consequence outside whisker.
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

**Propose: the caller holds it, whisker is pure.** The host already holds the SVG
text; the application already holds its own state. A retained tree inside whisker
would be a third copy that can disagree with both. The cost is that the caller
passes the tree to every call, which on RV-9 is a pointer and in gBASIC is a
record — and gBASIC records are values, so this is the grain of both hosts rather
than a compromise.

---

## 5. Iteration 1 — the smallest thing that is a toolkit

Per house methodology: the first iteration is a working product, not scaffolding.

**Scope:** labels and buttons in a single vertical stack. `render` produces a
whole document. `hit` returns an id. Nothing else — no focus, no keyboard, no
scrolling, no text entry, no second axis.

**Done means:**

1. `render` output parses, and is under the 4 KB `SRC_MAX` ceiling for a
   realistic panel of a dozen controls — **asserted, because the ceiling is real**.
2. Every widget's declared rectangle is what `hit` reports, checked at the four
   corners and the centre, and a point in the gaps reports nothing — the control
   that stops `hit` from being satisfied by "always return the first widget".
3. Rendered through RV-9's own `tools/hosttest/render.c` to a PPM that a person
   looks at once — because *"a picture can be geometrically perfect and still be
   wrong in a way only an eye catches"*, which is RV-9's own lesson and is
   quoted here so it is not learned twice.
4. A **fragment** render for one widget, and the band it occupies, so §2.2's
   30 ms tap is reachable rather than promised.

**Explicitly not in iteration 1:** the gBASIC binding. gBASIC keeps GTK, and the
whole point of a separate project is that whisker has to be worth adopting before
anything adopts it.

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
