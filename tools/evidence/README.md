# Evidence

The programs behind `docs/review-2026-09-30.md`, and since then behind whatever
else has needed measuring rather than arguing about. Each one answers a question
that was previously settled by reading, and each prints the claim it supports,
so a number in a document here can be re-derived rather than trusted.

They need RV-9 (`~/development/esp32/rv9`, or set `RV9`). Nothing here needs a
board: the driver is built natively through RV-9's own `tools/hosttest` stubs,
which is the same trick RV-9 uses to look at its pictures.

None of these is part of whisker. Its own tests live beside it in `src/`;
these stay as the record of what the substrate does.

## `module_shapes/`

Which shapes may a widget tree take inside an RV-9 module? Eight candidate
declarations, each built with `tools/build_modules.sh`'s exact flags and
`modules/module.ld`, then linked a second time at another base and compared
byte for byte — RV-9's own proof of position independence.

```
./run.sh                 the eight shapes, under three link schemes
./run.sh --all-modules   and every RV-9 module under the same three
```

The result that mattered: under the old base-0 link, *any named `static const`
object* failed, pointer or not, because the linker relaxed the PC-relative pair
into an absolute `li`. **RV-9 changed the base on 2026-09-30** — `0x4000`
against a `0x8000` probe, through `RV9_BASE` — for 71,732 bytes against 71,732,
no cost at all. Re-measured here against that tree: a `static const` widget tree
links, and a table of `const char *` or of function pointers still does not.

The `zerobase` column is kept because it is the evidence for why the base
moved, and because it shows the check never lost its teeth.

## `finger_size/`

Is a 64-row button a finger? The default came from arithmetic — 9 mm at the
P4's ~170 dpi — and arithmetic cannot say whether a target is comfortable. This
draws three candidate heights at matched **physical** size on a screen you do
have, so a fingertip can answer before the panel arrives.

```
./run.sh                 writes card.ppm, and prints the millimetres
gwenview -f card.ppm     full screen, 1:1, on the screen you measured
```

It needs **no touch input at all** — the question is millimetres and a finger,
not events, which is how it got answered on a monitor whose touch frame was
enumerating and reporting nothing. Change `MON_*` and `TGT_*` for another
screen or another device; every number in the labels is computed.

Answered 2026-10-01: 64 rows, the middle one. The default stands.

## `svgwin_facts/`

Four questions for `/w0` itself, and the byte budget.

```
./run.sh        all four sections, then the panel cost at both sizes
./run.sh 3      one section
```

1. **Over `SRC_MAX`**: the document is dropped and logged — and used to wedge
   the window for the life of the open path.
2. **Text metrics**: one fixed-width face, advance `font_size*10/20`, baseline
   at `15/20`, whitespace stripped before measuring, so labels can be laid out
   with integer arithmetic.
3. **What a label may contain**: entities are drawn literally and non-ASCII
   draws blank — both still true — and a raw `<` used to hang the driver, which
   is why it is still drawn in a child process the parent gives three seconds.
4. **Which rows a clipped repaint must carry**: the band-aligned ones, not the
   widget's. Carrying only the widget's leaves a strip at each end.

1 and 3 were defects when this was written and RV-9 fixed both on 2026-09-30.
Each section reports what it finds rather than what it found, so running it is
how to tell which RV-9 is in front of you.

`bytes.c` also writes `panel_320x172.ppm` and `panel_1024x600.ppm`. Look at
them. The second one shows something no measurement reports: a big screen
wants a grid, not a longer column.

## `keyboard/`

What does a keyboard cost? `docs/gbasic-gap.md` §3 offered three ways to afford
an `input` on RV-9 and marked the third **unmeasured**: *"a keyboard that is not
a widget set — one `<path>` of key outlines and one `<text>` per row rather than
36 of each."* This is that measurement.

```
./run.sh              the P4's panel, 1024x600
./run.sh 320 172      the C5's
```

The same 36-key keyboard three ways, **with whisker's own layout in all three**
— the outlines come from `wsk_rect`, not from a re-derivation — so "the same
keyboard" is a property of the programme rather than a claim about it. It is
also the first thing this measured: re-deriving the layout was 14 % wrong, and
would have been read as a rasterisation difference.

| | 1024×600 | 320×172 | |
|---|---|---|---|
| **A** widgets, with ids | 3,794 | 3,721 | a `<rect>` and a `<text>` per key |
| **A** widgets, no ids | 3,434 | 3,361 | |
| **B** one path per row | 2,048 | 1,960 | pixel-identical to A |
| **C** one path for all 36 | **1,967** | **1,879** | pixel-identical to A |

**Both compact forms draw exactly the same pixels as the widget form — zero
difference, not a small one** — and the saving is per key, so it does not move
with the screen. C is the cheapest: **half** of A with ids, and 55–57 % of it
without, at either panel size.

**C is also how the cap was found, and the numbers above are the second
reading.** When this was first run on 2026-10-02 `drv_svgwin.c` had an
undocumented `MAX_CONTOURS` of 16, and a path with more subpaths was **silently
truncated**: C drew the first 16 keys, not the other 20, and all 36 labels —
and the driver logged `drew 1967 bytes in 3 ms`, a success line. It did not look
broken. It looked like a keyboard somebody had styled that way, which is why a
localised silent truncation is worse than the `SRC_MAX` wedge it resembles.

RV-9's session **logged it and raised the cap to 64** the same day, moving the
contour buffer off the stack to get there — so raising it did not raise the
stack requirement of every program that writes to `/w0`. So C went from the
broken form to the cheapest one, and `run.sh` now prints the cap it found
alongside the 36 outlines the keyboard wants, because the useful output of this
programme turned out to be the comparison between those two numbers rather than
the byte counts.

Run it, then look at `keys_widgets.ppm` at **actual size**. On the C5 each key is
about 4 × 4.8 mm, against the 9 mm `finger_size/` measured as comfortable. The
bytes fit on that panel; the fingers do not.

## `stack/`

What stack does a whisker program need, and would a buffering sink need less?

```
./run.sh
```

The second question is RV-9's, left on 2026-10-02 as *"worth a measurement
rather than a decision"* after they bisected two whisker programs' stack
declarations and found both wrong — one of them overflowing on the board. The
cause is not whisker's frames: **drawing to RV-9's `/w0` renders on the
writer's stack**, and whisker streams, so the `put()` that finishes a document
runs RV-9's whole rasteriser with every layout frame still live beneath it. The
two depths add.

That property belongs to `/w0`, not to drawing in general — see `design.md`
§2.8 — which is why the answer below is scoped to the host it was measured on.

This reads `-fstack-usage` frames and the call graph out of the object file.
Three things it gets right that summing the `.su` column does not: function
starts come from the symbol table, so an `-Os` branch label is not mistaken for
a callee; a **tail** call replaces the caller's frame instead of nesting under
it, which otherwise overstates whisker's chain by about a third; and the two
indirect calls — the sink's `put()` and the body `wsk_compose` takes — are
resolved by naming their candidates, which is honest where guessing would not
be. An unnamed site is reported rather than ignored.

| | stack | statics |
|---|---|---|
| **streaming** (`src/module_compose.c`) | 800 + R | 0 |
| **buffering** (`buffered.c`, the sink changed and nothing else) | 544 + R | 4,096 |

**Buffering saves 256 bytes of stack and spends 4,096 of statics**, out of one
process footprint — a bad trade by 3,840 bytes whatever the rasteriser's depth R
turns out to be.

**What that settles and what it does not.** It settles which sink an RV-9
*module* should pass. It says nothing about whisker's API, which streams
precisely so the choice stays the caller's: a sink that appends to an array is
the buffer form, so a host with an allocator and a real stack — gBASIC's GTK,
whisker's own SDL host — buffers by passing a different sink, and none of the
arithmetic above applies to it. Section 1's figures are the host-neutral part,
because they are whisker's own frames and nothing else.

**R is a function of the DOCUMENT, and now we know which part.** RV-9 bisected
`wstat` on the board on 2026-10-03: it runs at **5,120** and is declared at
6,144 — *less* than `wflick`'s 7,168, although `wstat` has the **deeper module
chain** of the two (880 against 592). The difference is that `wflick` draws
corner marks, so its document contains a `<path>` and the renderer enters
`do_path` and `rv9_raster_fill_n` — **a 624-byte frame on its own**. `wstat`'s
readout has no path in it.

So section 1 also reports, per module, **whether the program can emit a path at
all** — asked of the object rather than the source, because `-Os` inlines
`wsk__marks` away and leaves no symbol, while the `<path d=` literal survives in
rodata.

That check earns its place by showing the distinction that matters:
`module_stat.c` **can** emit a path even though the document it actually
produces never does, because marks are compiled in and simply unused. Built
`-DWSK_MINIMAL` it cannot. **That is what makes a stack figure durable rather
than merely current** — with marks compiled in, "the richest document this
program can emit" and "the document it does emit" are different things, and the
bisection only measured the second.

**And R's absolute size is still the part nobody has measured.** `modules/pic`'s figure of 2,080 is
the only one written down, so it is the one a whisker program would reach for,
and against RV-9's board bisections a whisker document costs the rasteriser **at
least 4,528** — with at least 1,232 bytes between two different documents. So
there is no single number for "drawing to `/w0`". Section 1 prints what a
program *can* be told, which is its own chain; the rest has to be bisected on
the hardware.

## `profiles/`

What does a host pay for a widget it never uses?

```
./run.sh
```

Matthew, 2026-10-02: *"I want to ensure that at least a scaled down basic
version can run on the C5 but also for gBASIC (and maybe other things) on
larger systems."* `design.md` §7 is the answer; this is the number in it.

It builds the smallest honest whisker program — a label and two buttons,
written to `/w0` once — under each profile:

| | bytes | saved |
|---|---|---|
| full, the default | 3,272 | |
| `-DWSK_NO_MARKS` | 2,984 | 288 |
| `-DWSK_NO_FIELD` | 2,908 | 364 |
| `-DWSK_MINIMAL`, both | **2,604** | **668 — 20 %** |

**The second section is the more interesting one.** It lists, by `nm` rather
than by assertion, what that binary does *not* contain: `wsk_hit`,
`wsk_compose`, `wsk_rows`, `wsk_render_clipped`, `wsk_part` and
`wsk_field_view` are all absent, because everything here is `static inline` and
an entry point nobody names is never emitted. whisker already scales down by
linkage for everything a caller can decline to call. The preprocessor is needed
for exactly the two branches that live inside the per-widget loop every caller
links — and for nothing else.

Note the include order in `run.sh`: `-I$SRC` comes **first**, because RV-9
vendors its own `modules/whisker.h` and a measurement that silently used that
copy would be worthless. That is not hypothetical — it happened while this was
being written, and reported a saving of zero.

Whether a reduced build draws the *same* picture is a gate rather than a
measurement, so it lives in `src/run.sh`: the same panel compiled both ways,
diffed byte for byte, with a field and a mark as controls.

## `p4_panel/`

Does a realistic seven-inch panel fit in 4 KB?

```
./run.sh
```

`design.md` §2.3 predicted that `SRC_MAX`, which does not scale with the
screen, would be *"the binding constraint on the P4"*. **It is not**, and this
is the measurement that says so. It needs **no board**: how many bytes a tree
emits is a property of the tree and the emitter, and both are here. The earlier
reasoning was done before whisker's emitter existed, against hand-written SVG
(`svgwin_facts/bytes.c` still answers that older question).

| a realistic panel at 1024×600 | bytes of 4,096 |
|---|---|
| a readout of 20 values, two columns | 1,026 |
| a settings form — 6 labelled fields, two buttons | 1,286 |
| a launcher — 15 buttons in a 3×5 grid | 1,728 |
| a text-entry screen — a field and 36 keys | 3,541 |

A whole screen filled with one column of buttons never binds either: 8 at
finger size is 976 bytes, 24 crammed in at 16 rows each is 2,662. **The screen
runs out before the budget does.**

**What binds is density.** A grid can put far more on a screen than a column,
and that is where the ceiling is — 36 controls is 3,803 bytes and 48 is 5,025,
over. So **about forty visible at once**, and the one realistic panel that gets
there is a **keyboard**, which is the same fact from the other side: a keyboard
is the densest grid an interface has.

Two things the pictures caught that the numbers did not:

- An earlier version counted "how many buttons fit in `SRC_MAX`" and printed
  its own array bound twice while looking like an answer. A stack that overruns
  the panel emits only what still intersects it, so that question quietly
  becomes "how many fit on the glass". It is asked by filling the screen now.
- `p4_settings.ppm` first came out with **invisible fields**. The panel had set
  `bg` to its own taste and kept `wsk_init`'s `trough`, which is deliberately
  *darker* than the default background so a field reads as a well — move one
  without the other and the contrast goes. The byte counts were identical
  either way. It uses the shipped palette now.

## `scroll/`

What does a scroll cost against the band model?

```
./run.sh              the C5's panel
./run.sh 1024 600     the P4's
```

`studio-gap.md` §5 says this is the first of the four Studio capabilities to
price, because the other three need it and because it is the one that interacts
with the band model rather than sitting on top of it. No board: bytes come from
the emitter, bands from RV-9's own rasteriser built natively, milliseconds are
**derived** from the board figures in `design.md` §2.2 and marked as such.

**Nothing in whisker changed to run it.** A scroll is already expressible —
put the stack's area at `y = -offset` with its full content height and clip the
document to the viewport. `wsk_part` skips every widget that misses those rows,
so what is scrolled off either end is simply not emitted. The question was never
*can* whisker scroll.

**1. Bytes track the viewport, not the content.** A 200-item list in a 6-item
viewport emits the same 369 bytes as a 10-item one. So list length is free and
`SRC_MAX` is not the constraint on a long list.

**2. A scroll step damages the whole viewport**, which an in-place update does
not:

| | C5, bands of 22 | P4, bands of 75 |
|---|---|---|
| one value changing in place | **2** | **2** |
| one scroll step | 14 | 50 |
| the whole panel | 22 | 75 |

**3. Which in time is the finding:**

| | C5 | P4 |
|---|---|---|
| one value in place | 5 ms | 17 ms |
| **one scroll step** | **35 ms** | **425 ms** |
| the whole panel | 55 ms | 637 ms |

Against the 19 ms a finger waits, a scroll step is **1.8×** the budget on the
C5 and **22×** on the P4.

**So: scroll in discrete jumps driven by an input event — one item or one page
— and never as an animation.** A smooth pixel scroll needs a frame every 16 ms
and one frame costs 35 ms at best. This is not a thing to optimise later; it is
a property of a window that re-parses the document once per band.

**4. And the only lever is the size of the scrolling region**, which is linear:

| viewport | C5 | P4 |
|---|---|---|
| full screen | 35 ms | 391 ms |
| half | 17 ms | 204 ms |
| quarter | 10 ms | 102 ms |

Convenient for the Studio direction: a multi-pane layout is **cheaper** than one
scrolling screen, and Studio already is multi-pane.

One thing the harness got wrong first, worth keeping. It set `d.y0/d.y1` — what
whisker *emits* — and not `RV9_SVG_SS_ROWS`, what the window *paints*. A clipped
document written without the setstat is painted across the whole screen, so
everything outside it goes to the device background. That is the defect §2.2
records `flick` shipping for months, reproduced by accident within the hour, and
it reported a one-row text change as dirtying 20 bands of 22 rather than 2.

## `fit/`

What do the lower rungs of the overflow ladder buy?

```
./run.sh              the C5's panel
./run.sh 1024 600     the P4's
```

`scale-sketch.md` §4 proposes compressing first, then scaling to a floor, and
only then scrolling. `scroll/` priced the last rung; this prices the first two
and asks whether the ladder earns its complexity at all.

**It does on a display, and not on a touch surface — and the inversion is the
finding.**

| a readout of 32-row items | C5 | P4 |
|---|---|---|
| as laid out (pad 8, gap 8) | 4 | 14 |
| 1. compressed (pad 0, gap 0) | 5 | 18 |
| 2. scaled to font 16 | 7 | 25 |
| 2. scaled to font 12 | **9** | **33** |
| | **+125 %** | **+135 %** |

But **for a touch surface the floor is the finger, not the pixels.** 9 mm is 88
px on the C5 and 60 on the P4 (`finger_size/`), so a touchable list holds 1 row
and 8 rows respectively, and compressing cannot move either. Scaling below the
finger produces targets nobody can hit.

So the rung that *helps* is decided by the same capability that decides the rung
that is *available*:

- **display-only** — scaling works and buys over twice the content; scrolling is
  not available at all (D9). Rungs 1–2 are the whole answer.
- **touch** — scrolling works; scaling is blocked by the finger almost
  immediately. Rung 3 is the answer and the first two are nearly free of effect.

**The legibility floor is deliberately not set here.** Section 1 draws the same
string at font 24 down to 6 and reports the brightest pixel the ink reached, of
63: it holds at 59–63 down to font 10 and falls to 45 at font 8, so there is a
cliff where strokes stop reaching full intensity and start averaging into the
background. That is a *signal*, not an answer — legibility is physical, and a
PPM looked at on a development monitor is at whatever magnification that monitor
happens to give. `finger_size/` had exactly this problem and solved it by
drawing at matched **physical** size on a screen you do have. The font floor
wants the same treatment and a person.
