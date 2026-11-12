# CLAUDE.md

Guidance for Claude Code working in this repository.

## What this is

**whisker** — a GUI system for a window you write SVG to. **Its charter
changed on 2026-11-11 (D16):** it is to become a *complete* GUI system, in
tiers set by what the device can do — presentation only on the C5, touch
controls on the P4, and a desktop toolkit gBASIC can use instead of Qt.

What is **built** is the bottom layer: two pure functions, a widget tree
becomes SVG text and a point becomes the id of the widget under it — no
framebuffer, no device, no event loop, no allocation. What is **designed and
not built** (`docs/design.md` §8) is a **toolkit** above it — input events in,
the caller's widget structs updated in place, *what changed* out, with the
rows to repaint — and a **host** per platform below both. The C5 compiles the
toolkit out (`WSK_NO_INPUT`, in `WSK_MINIMAL`). Older documents that say
"nothing else" or "no event loop" describe the drawing layer, and are history
about whisker as a whole. **`docs/design.md` §9 is the map of directions**
(nesting and wrap, compact fonts, transitions, shape hit, the P4 keyboard,
served applications, the browser client, print) and §9.9 the order — layout
first (D17 nesting, D18 overflow: refuse | scroll | wrap), then §8's toolkit.

It serves two hosts and belongs to neither:

- **RV-9** — `~/development/esp32/rv9`. A small modular OS for RISC-V on an
  ESP32-C5: one core at 240 MHz, ~¼ MB of RAM, no PSRAM. It already has the
  surface (`/w0`, write SVG to it), the rasteriser (`raster.c`, no ESP
  dependencies) and text (`font.c`). It has **no touch** — that arrives on the
  P4 board in October (see Status). Its roadmap heads a section
  *"Open — damage tracking, before the GUI direction."*
- **gBASIC** — `~/development/gbasic`. A C11 BASIC-family interpreter. Already
  emits deterministic SVG from pure gBASIC (`stdlib/chart.bas`, zero
  dependencies). **Its GUI is Qt as of 2026-10-22** (gBASIC `19a830f`): GTK,
  the `gi` bridge and the `gui`/`gtkui` libraries are gone, and gBASIC Studio
  moved with it. What it has instead is a declarative `window` read by a
  backend, with `qt` the *first* backend (gBASIC `docs/qt_gui_design.md`).

**gBASIC keeps Qt.** whisker is a candidate second backend — now in a literal
sense, a second reader of the same `window` declaration — and has to earn the
place. `docs/design.md` §6 records what would have to be true first. **Every
whisker document written before 2026-10-22 that measures gBASIC measured its
GTK tree** (last GTK commit `d997c94`): `gbasic-gap.md` and `studio-gap.md`
are history. **`docs/window-gap.md` (2026-10-24) is the current distance**:
Studio's window is a pure record, and whisker answers 15 of its 25 words —
16 since §5e answered `scroll`.

## Status

**Eight iterations are built, and two of them have now run on the C5.**

- **§5 — the renderer.** Labels and buttons in one single-axis stack, column or
  row. Done.
- **§5b — `hit`.** A point becomes an id. Done, built early so that board time
  when the P4 arrives is spent on the measurement nobody has taken
  (finger-down to pixel-changed) rather than on writing a hit test.
- **§5c — composition.** Several stacks in one document. Done. Chosen because
  §6's gBASIC measurement said the remaining gap is `input`, and drawing the
  keyboard that needs found that two stacks could not share a screen.
- **§5d — `input`.** A text field: left-aligned, tail-scrolled, caret on
  request. Done, and it is the last widget — nothing after it has a caller
  (`gbasic-gap.md`).
- **§5e — scrolling** (2026-10-24). Not a widget: a property of a stack,
  `overflow = WSK_SCROLL` plus a `scroll` offset the caller holds. Chosen by
  `window-gap.md` as the first step to gBASIC's Qt-era `window`. A widget half
  in view is **cut by arithmetic** (no `clipPath`, and `/w0`'s row clip may
  not be leaned on); **text is whole or not at all**; out of range is
  **refused** (`WSK_E_SCROLL`), never clamped. **A stack ends at row 32,767**
  (a rectangle is four shorts) and **a document costs the square of the
  list**, so a long list goes to whisker **a slice at a time** —
  `wsk_slice`, proven byte-identical to the whole list. In the default
  profile, out of `WSK_MINIMAL` (496 bytes). **On the P4, 2026-11-11** (`wscroll`, §5e):
  a 112-row pane repaints in **37.5 ms** (finger to pixels 42) and a whole
  screen of rows in **229** — a pane drags smoothly, a screen steps. The
  "~110 rows at 30 fps" derivation was 12 % optimistic; quote the measured.

- **§5f — nesting** (2026-11-11). A widget may be a stack (`WSK_STACK`),
  named by **index** into the root's `nest` so panels stay `static const`.
  Layout, check, hit, rows and render all descend; **the id is prime and a
  path is checked link by link** (D19); clipping accumulates through every
  scrolling viewport above. A scrolling stack asks only its padding along its
  axis. **1,780 bytes, default profile only** (`WSK_NO_NEST`, in
  `WSK_MINIMAL`); **each level costs 256 bytes of stack** beneath the
  rasteriser. `wnest` ran on the P4 2026-11-11: seven taps, each named the right widget
  at depth.

- **§5g — wrap** (2026-11-11). `overflow = WSK_WRAP`, D18's third answer:
  items flow onto new lines, each line as deep as its deepest item, weight
  and justify a line at a time. A wrapping stack asks only for its widest
  item along its axis and for its lines' depth at the width it will get
  across. **A text window is composition**: word labels in a wrapping row
  inside a scrolling column. 752 bytes, default only (`WSK_NO_WRAP`). `lgap` spaces
  lines apart from words.

- **§5h — text items** (2026-11-11). `WSK_TEXT` names a string by index in
  the root's `str` (runtime array, so panels stay static). In a wrapping row
  its **words are units of the flow** — buttons may sit inline among them;
  elsewhere it is a long label. A line of words is **one `<text>` run**: the
  §5g paragraph drew the same pixels in 610 bytes instead of 1,415.
  `wsk_hit_word` names the word. 1,104 bytes, default only (`WSK_NO_TEXT`).
  **The default profile is now ~8.8 KB for a small panel** (4.7 before
  nesting); a middle profile is an open decision.

`src/run.sh --module` is the whole of it: **349 checks**, the pixels through
RV-9's own rasteriser, twelve module builds with the double-link, and a person
looking at the pictures. `host/linux/run.sh --test` adds 19, including the
typing loop end to end; `--input` is the same loop with a person in it.

Settled before any of that: **the RV-9 review is answered**
(`docs/rv9-answers.md`, 2026-09-29) — thirteen of sixteen claims stood, and one
was right *and undiscovered*, which found a live bug in RV-9's `flick` and moved
the tap budget. **All six decisions are ratified** (Matthew, 2026-09-29);
`docs/decisions.md` is the ledger and wins wherever it disagrees with §4.

**D0 was settled by a purchase**: a Waveshare ESP32-P4-WIFI6-Touch-LCD-7B —
1024×600, five-point capacitive touch — arrives in October. The C5 keeps `/gpio`
and probably never gets touch, so keep declaration order stable and a focus model
stays addable without being built.

**D6 and D7 (2026-10-02)** bound what comes next. `input` goes only to hosts
with an input device, so **not the C5** — not on bytes, which turned out to be
half the budget rather than 94 %, but because the C5 has nothing to press a key
with. And the **mouse on Linux stands in for touch** until the P4 arrives: a
click is one point, which is all `hit` takes.

**The board run happened, 2026-10-02.** `wflick` and `wcompose` are committed in
RV-9's tree and both draw what they claim. Three things came back
(`docs/rv9-answers-2026-10-02.md`): `MAX_CONTOURS` is **64** and a truncated
path now logs a warning, so the compact keyboard went from broken to cheapest;
all four ceilings are askable through `getstat(RV9_SVG_GS_LIMITS)`; and a stack
finding that is whisker's own, below.

**Profiles, 2026-10-02 (§7).** whisker now scales *down* as well as up. The
default is everything; `-DWSK_MINIMAL` is the C5's profile and drops the field
and the corner marks for **668 bytes, 20 %** of a small panel's code. A reduced
build draws **byte-identically** for the widgets it keeps and **refuses**
(`WSK_E_KIND`) what it lacks rather than substituting — both checked in
`src/run.sh`, the refusals being the control on the first. gBASIC's end needed
nothing *at the time*: its `gui` named five component kinds and whisker had all
five — but `gui` was removed with GTK, so that count wants redoing against
`window`.

**What is left is in `docs/roadmap.md`** (2026-10-03), which is the first thing
to read when picking this up cold. Three gates, and only one is code:

- ~~**G1 — what the C5 panel is *for*.**~~ **Closed 2026-10-03 with no code
  (D9):** the C5 panel is **write-only, no controls**, driven from the serial
  console or ssh. GPIO is for *sensors* — data in, not a person acting on a
  widget — so that panel is a **readout**, and a readout needs `rows` plus a
  clipped repaint, both of which exist. §2.6's row 2 (`focus_next`/`activate`,
  a focus mark) is **never built on the C5**. *D16 narrowed this:* a desktop
  needs keyboard focus, and design §8.7 puts it in the toolkit, which the C5
  build does not contain.
- **A readout exists.** `src/module_stat.c` / RV-9's `modules/wstat/` is the
  first whisker program meant to be used rather than demonstrated, and it is
  D9's shape: values on a write-only panel, only the changed rows repainted,
  and **nothing written at all** on a tick where nothing moved. It starts
  §6's first bar item, which G1's answer turned out to unblock rather than
  gate — a readout is a real interface and the C5 is on the desk.
- **The P4 is here, 2026-10-11**, with a working panel and `/touch`. It
  immediately made three of whisker's derived numbers wrong: §2.2's 640 ms
  full redraw and 66 ms tap were *extrapolations*, and the panel measures
  **175 ms** and **5 ms**. Anything derived from those was ~3.7× pessimistic —
  `tools/evidence/scroll/` among them, whose "never as an animation" became
  "not for a whole screen, but a pane of ~110 rows fits a 30 fps frame".
  **Check what a figure was derived from before quoting it.**
- **Touch slop is measured, 2026-10-24** (§5b, `tools/evidence/slop/` run 4):
  bias ≤ 10 px, scatter 9–21 px worst (≤ 3.1 mm), drift zero. **`hit` stays
  exact.** On the way, the "P4 draws ~45 rows low" I sent to RV-9 was
  `wslop`'s own marks, unsized buttons laid out 64 rows tall
  (`docs/rv9-answers-2026-10-24.md`). **A button with `size = 0` in a column
  is 64 rows whatever its area says** — which used to overflow silently and
  is now **refused with `WSK_E_FIT` (D10)**. Before blaming
  a layer below, check what whisker drew — RV-9 settled it by turning the
  picture over and watching the fault follow the colour.
- **G2 — the four things only the P4 can measure**: finger-down to
  pixel-changed (never measured, §2.2), ~~touch slop (§5b)~~, the stack bisected
  there rather than carried over (§5), and a panel where `SRC_MAX` actually
  binds (§2.3).
- **G3 — gBASIC answers.** §6's bar; a decline is a finish.

Two decisions are Matthew's and are explained in `roadmap.md` §4: whether the
compact keyboard form is built in (a **P4-only** question — 115 bytes is all a
field-plus-keyboard leaves of `SRC_MAX` at 1024×600, so expect to need it), and
whether `tools/notation` ships (which D9 just gave a trigger: a C5 readout whose
layout is compiled in cannot change without a reflash).

## Read before working

1. `docs/decisions.md` — twenty ratified outcomes (D0–D19), one screen. **First**, so that
   the design's §4 arguments read as history rather than as open questions.
   Then `docs/roadmap.md`, which is what is left and what would let us stop.
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
- **A new widget kind adds a branch to the shared emit loop**, so it arrives
  with §7's question attached: which profile is it in, what does it cost, and
  does the reduced build refuse it by name? The module rules forbid the
  dispatch table that would make this automatic (§4.1), so it has to be asked
  each time.
- **Tabs for indentation.**
- Tests are kept for regression.

## The two budgets, which are not negotiable

Both are measured on real hardware and recorded in `docs/design.md` §2.2–§2.3.
Anything proposed here is priced against them:

- **4 KB of SVG** (`SRC_MAX`) is the ceiling on how much interface fits on a
  screen. It is *raisable in one line*, so respect it for the right reason: the
  cost is **time, paid once per band**, roughly 0.34 ms + 0.00036 ms/byte on the
  host, so a full 4 KB document costs ~2.5× a terse 1 KB one. It also does **not
  scale with the screen** — but the old claim that this made it *"the binding
  constraint on the P4"* was **measured and overturned** 2026-10-03
  (`tools/evidence/p4_panel/`, §2.3): a realistic 1024×600 panel is 1–1.7 KB,
  and a screen filled with a column of buttons never binds at all. What binds
  is **density** — about forty controls visible at once — and only a grid gets
  there, which is why a keyboard is the one panel that approaches it.
- **~19 ms of latency and ~38 ms of work per tap** at 320×172 (~33 and ~66 at
  1024×600), against ~56 ms and ~640 ms for a full redraw. Keep the two apart:
  `flick`'s press is 19 ms and its release is 19 ms, and the finger lifts
  between them — what a person waits for is the press. Quoting 38 as a latency
  budget is twice as pessimistic as the hardware deserves. A full re-render per interaction is
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
`switch` over string literals, no local array initialised from a literal.
**Plain `static const` data is fine** — a widget tree may be a static array
with inline `char[N]` fields or integer offsets into a string blob; it may not
be a table of pointers. It is addresses in rodata that break this. Each of
the last three compiles to a table of absolute addresses and breaks position
independence, which `tools/build_modules.sh` *proves* by linking each module
twice at different bases and comparing the bytes.

**Staging a module in RV-9's tree? Its `build.conf` device list is
SPACE-separated.** `devices="/w0,/tsens"` is one device name with a comma in
it, and `wstat` was refused at fork for needing a device no machine has —
correctly, and with an exact message. Guessing a separator from a
one-element example (`devices="/w0"`) is what caused it.

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

## `/w0` is not the display system, it is *a* display system

**The standing hazard in this repository**, and the easiest one to fall into,
because `/w0` is the only substrate that has been on a bench and it sets every
budget above. Counted honestly whisker already faces several display systems:
`/w0` on the C5; `/w0` on the P4, where RV-9 owns the pixels in PSRAM and every
figure moves; RV-9's own `lcdcon`, a second driver on the *same panel*
(`panel.h`: *"shared by every driver that draws on it"*); whisker's SDL host;
and gBASIC's Qt, which renders on its own loop where none of this arithmetic
exists at all. `design.md` §2.8 is the table.

**So name the substrate a finding came from.** A measurement on `/w0` is
evidence about `/w0`. It is evidence about *whisker* only where it shows
whisker making a choice it did not have to make — and the usual right answer to
such a finding is to push the choice back out to the caller, not to settle it
here. The worked example follows.

### Drawing to RV-9's `/w0` runs on the writer's stack

Measured 2026-10-02 after RV-9 bisected two whisker programs' stack
declarations and found both wrong, one of them overflowing on the board. The
cause is not whisker's frames. **whisker streams rather than buffers**, so the
`put()` that finishes a document runs RV-9's entire rasteriser *with every one
of whisker's layout frames still live beneath it*. The two depths add.

So `design.md` §5's "a sink costs nothing" is true about memory and false about
stack **on that host**, and three things follow:

- **An RV-9 module should not buffer, and that is measured, not assumed.**
  `tools/evidence/stack/` builds `module_compose.c` against a buffering twin
  that differs only in the sink: buffering saves **400 bytes of stack** and
  spends **4,096 of statics**, out of one process footprint. A bad trade by
  3,696 bytes. (256 and 3,840 before §5f moved the drawing loop out of line.) Note the scope — this settles what an RV-9 *module* passes, not
  what whisker's API is. A sink that appends to an array *is* the buffer form,
  so a host with an allocator and a real stack buffers by passing a different
  sink, and whisker never knows. Keep it that way.
- **Do not hand a whisker program a stack number from `modules/pic`.** Its
  2,080 is the only figure written down anywhere and it is *not* an upper
  bound: against RV-9's bisections a whisker document costs the rasteriser at
  least **4,528** bytes, and the figure moves by at least 1,232 bytes between
  two different documents. There is no single number for "drawing to `/w0`".
- **What a program can be told is its own chain**, which is what
  `tools/evidence/stack/run.sh` prints — **640 bytes for one stack, 960 for a
  composite, and 256 more for each level of nesting** (§5f; it was 560 and
  800 before nesting, which made drawing recursive). That part is host-neutral, because it is measured from whisker's
  own frames and nothing else. The rest has to be bisected on the hardware, and
  bisected again on the P4 rather than carried over from the C5.
