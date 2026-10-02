# Evidence

The programs behind `docs/review-2026-09-30.md`. Each one answers a question
that was previously settled by reading, and each prints the claim it supports,
so a number in the review can be re-derived rather than trusted.

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
| **B** one path per row | **2,048** | **1,960** | pixel-identical to A |
| **C** one path for all 36 | 1,967 | 1,879 | **draws 16 keys** |

**B is 54 % of A and draws exactly the same pixels — zero difference, not a
small one.** The saving is per key, so it does not move with the screen.

**C is why the cap matters.** `drv_svgwin.c` has an undocumented `MAX_CONTOURS`
of 16, and a path with more subpaths than that is **silently truncated**: the
first 16 keys are drawn, the other 20 are not, and all 36 labels are. Look at
`keys_onepath.ppm` — it is a keyboard with twenty invisible keys, and nothing
reports an error. One path per row stays under the cap at 10, 10, 9 and 7, which
is luck worth knowing about rather than a rule to rely on.

Run it, then look at `keys_widgets.ppm` at **actual size**. On the C5 each key is
about 4 × 4.8 mm, against the 9 mm `finger_size/` measured as comfortable. The
bytes fit on that panel; the fingers do not.
