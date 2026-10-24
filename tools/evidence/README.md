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
the buffer form, so a host with an allocator and a real stack — gBASIC's Qt,
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

**3. Which in time is the finding — and the P4 column was wrong until
2026-10-11:**

| | C5 | P4 |
|---|---|---|
| one value in place | 5 ms | 4 ms |
| **one scroll step** | **35 ms** | **115 ms** |
| the whole panel | 55 ms | 172 ms |

The P4 figures used to read 425 and 637, derived from §2.2's *extrapolated*
640 ms full redraw. RV-9 ran `flick` on the real panel and measured **175 ms**
— so the per-band cost is 2.3 ms, not 8.5, and everything derived from it was
3.7× too pessimistic. The 172 ms this programme now derives against their
measured 175 is a decent check on the per-band model itself.

**So the conclusion moved, in the generous direction.** Scrolling the *whole*
screen is still a discrete jump — one item or one page per input event. But a
**pane** is a different question:

| | fits a 30 fps frame | fits a 60 fps frame |
|---|---|---|
| C5 | 13 bands — 104 rows | 6 bands — 48 rows |
| P4 | 14 bands — 112 rows | 7 bands — 56 rows |

**Nearly the same on both boards**, because the per-band cost barely differs
(2.5 ms against 2.3). So what decides whether a scroll can be animated is **how
tall the scrolling region is**, not which panel it is on.

*Implied, not demonstrated:* nobody has dragged anything on either panel and
timed it. RV-9 reached the same place from the other side the same day and put
it the right way — measure before assuming a drag is too expensive, rather than
prohibiting it.

**4. The lever is still the size of the scrolling region**, and it is linear:

| viewport | C5 | P4 |
|---|---|---|
| full screen | 35 ms | 105 ms |
| half | 17 ms | 55 ms |
| quarter | 10 ms | 27 ms |

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

**The floors are measured.** Matthew read them off the real C5 panel on
2026-10-04 with `wfont > /w0`: **readable down to font 8, comfortable from
about 16–20**. Two numbers rather than one, which is the same shape
`finger_size/` found for targets — a thing you *can* hit and a thing that is
pleasant to hit are different sizes.

| a readout of 32-row items, C5 | fit | |
|---|---|---|
| as laid out (pad 8, gap 8) | 4 | |
| 1. compressed (pad 0, gap 0) | 5 | +1 |
| 2. scaled to **comfort** (font 16) | 7 | **+75 %** |
| 2. scaled to **readable** (font 8) | 14 | +250 % |

**So the ladder is worth +75 % respecting comfort**, not the +125 % this file
reported before the floor was known — that figure scaled to font 12, which is
below comfort. The +250 % is available in an emergency and is not a design.

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
background. That is a *signal* — it says where the renderer struggles, not where
a person stops reading.

**And `finger_size/`'s trick does not work for type**, which was tried before it
was dismissed. That card draws at matched **physical** size on a monitor you
have; the monitor is 4.0 px/mm against the C5's 9.7, so it has **41 %** of the
pitch, and a font-10 cell — 1.03 mm — is **4.1 monitor pixels**. It would show a
smudge and the floor would come out two sizes too high. The trick works for a
touch target because a target is a *shape* and loses nothing to a coarser pitch.
Type is the fine detail that pitch carries.

So the floor had to be found **on the glass**, and that is `src/module_font.c`,
staged in RV-9's tree as `wfont`. **Answered 2026-10-04**: readable to 8,
comfortable from 16–20.

**The objective proxy was right.** Section 1's peak intensity holds to font 10
and falls away at 8 — and 8 is exactly where reading stops. The renderer stops
delivering full-intensity strokes at the same size the eye stops resolving them,
which is a pleasing coincidence and a reason to trust the proxy on a panel
nobody has yet.

**And the floor is physical, so it does not transfer as a font size.** C5 font
16 is **1.64 mm** of glass at 9.7 px/mm. The same 1.64 mm on the P4, at 6.7
px/mm, is **font 11** — a coarser panel needs a *smaller* font number for the
same readable size, which is the opposite of the intuition and exactly why
`RV9_GS_PHYSICAL` matters. The C5's floor must travel to the P4 as millimetres,
never as a number, and even then the viewing distance is a separate question.

## `tap/`

Of a tap, how much is whisker?

```
./run.sh              the C5's panel
./run.sh 1024 600     the P4's
```

`design.md` §2.2 says *"no tap has ever been measured end to end"*, and the
roadmap filed that under the P4. Most of it never needed the P4: the chain is
*sensor → driver → hit → rows → setstat → render → rasterise → blit*, and only
the first two links belong to a touch panel. Everything from "a point arrives"
to "pixels changed" runs here with a synthetic point.

| stage, one press | C5 | P4 |
|---|---|---|
| `hit` — a point becomes a name | 0.0 % | 0.0 % |
| `rows` — a name becomes a band range | 0.0 % | 0.0 % |
| `emit` — whisker writes the document | 0.7 % | 0.2 % |
| **parse + rasterise — the window** | **99.2 %** | **99.7 %** |

**whisker is under 1 % of a press.** Absolute microseconds on a development
machine are not a 240 MHz core's, and the programme says so — what transfers is
the *share*, because both halves are integer code doing the same work on either
machine.

**So whisker's cost is the document it writes, not the code that writes it.**
Terseness matters because *parsing* is expensive and is paid once per band, not
because emitting is. Optimising the emitter would be optimising 0.7 %.

That also closes out the `wflick` vs `flick` gap without needing another board
run: 32 ms over 8 bands is 4.0 ms a band, 19 ms over 6 is 3.2, and the
difference per band is the 342 bytes re-parsed against 146. None of it is
whisker executing.

The blit is excluded deliberately — here it is a memcpy into an array, on the
board it is SPI DMA to glass. Including it would import a number that means
nothing off the panel.

One harness bug worth keeping: the first run reported `hit` at **0.00 µs**,
because with `-O2` and a result nobody reads the compiler is entitled to delete
the loop. The timed results go into `volatile` sinks now. A benchmark of nothing
reports excellent performance.
