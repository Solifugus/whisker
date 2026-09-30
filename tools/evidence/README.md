# Evidence

The programs behind `docs/review-2026-09-30.md`. Each one answers a question
that was previously settled by reading, and each prints the claim it supports,
so a number in the review can be re-derived rather than trusted.

They need RV-9 (`~/development/esp32/rv9`, or set `RV9`). Nothing here needs a
board: the driver is built natively through RV-9's own `tools/hosttest` stubs,
which is the same trick RV-9 uses to look at its pictures.

Neither directory is part of whisker. When there is a whisker to test, its
tests live beside it; these stay as the record of what the substrate does.

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
