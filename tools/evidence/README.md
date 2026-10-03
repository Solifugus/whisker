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
cause is not whisker's frames: **drawing to `/w0` renders on the writer's
stack**, and whisker streams, so the `put()` that finishes a document runs
RV-9's whole rasteriser with every layout frame still live beneath it. The two
depths add.

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
turns out to be. Streaming stays, now for a measured reason.

**And R is the part nobody has measured.** `modules/pic`'s figure of 2,080 is
the only one written down, so it is the one a whisker program would reach for,
and against RV-9's board bisections a whisker document costs the rasteriser **at
least 4,528** — with at least 1,232 bytes between two different documents. So
there is no single number for "drawing to `/w0`". Section 1 prints what a
program *can* be told, which is its own chain; the rest has to be bisected on
the hardware.
