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

The result that matters: *any named `static const` object* fails today, pointer
or not, because at base 0 the linker relaxes a PC-relative pair into an
absolute `li`. Linking both the module and the probe at a non-zero base fixes
it for **71,732 bytes against 71,732** — no cost at all — while still catching
the two shapes that genuinely hold addresses.

## `svgwin_facts/`

Four questions for `/w0` itself, and the byte budget.

```
./run.sh        all four sections, then the panel cost at both sizes
./run.sh 3      one section
```

1. **Over `SRC_MAX`** the window wedges for good, rather than truncating.
2. **Text metrics**: one fixed-width face, advance `font_size*10/20`, baseline
   at `15/20`, so labels can be laid out with integer arithmetic.
3. **What a label may contain**: entities are drawn literally, non-ASCII draws
   blank, and a raw `<` hangs the driver — run in a child process, since it
   does not return.
4. **Which rows a clipped repaint must carry**: the band-aligned ones, not the
   widget's. Carrying only the widget's leaves a black strip at each end.

`bytes.c` also writes `panel_320x172.ppm` and `panel_1024x600.ppm`. Look at
them. The second one shows something no measurement reports: a big screen
wants a grid, not a longer column.
