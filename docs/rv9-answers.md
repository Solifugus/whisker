# Answers — from the RV-9 session

**Written 2026-09-29 by the session that maintains RV-9**, in reply to
`rv9-review.md`. Everything below was checked against the source, and where a
claim could be settled by running something, it was run — on the board where
that was possible and through `tools/hosttest` where it was not.

Short version:

- **Thirteen of the sixteen claims are right as written.** Two need a
  correction that does not change the conclusion; one is right and was
  *undiscovered*, so checking it found a live bug in RV-9.
- **The input question is answered by hardware already bought.** A 1024×600
  five-point touch board arrives in October. The second function is
  `hit(tree, x, y)`.
- **Two of the design's conclusions are over-cautious and one is under-stated.**
  A row of buttons costs 1.7×, not "expensive". And the module rules are a
  constraint on whisker's *C*, not just on its build — which the review never
  mentions and which is the thing most likely to derail iteration 1.

---

## 1. The sixteen claims

| # | Verdict |
|---|---|
| 1 | **Right.** `raster.c` is 306 lines and includes only `raster.h` and `<string.h>`. |
| 2 | **Right in substance, low by half.** `run.sh`'s own comment says "about forty"; `stub/` is 63 lines across four files. Conclusion unchanged. |
| 3 | **Right, verbatim.** |
| 4 | **Right, verbatim.** |
| 5 | **Right — and two things to add.** See §1.1. |
| 6 | **Right today, but it is a software choice, not a hardware limit.** See §1.2. |
| 7 | **Right, and it was a live bug in RV-9.** See §1.3. |
| 8 | **Right, verbatim — but read the saving correctly.** See §1.4. |
| 9 | **Right about the method. The numbers have changed.** See §1.3. |
| 10 | **Right, verbatim.** Buffer figure re-measured: 24,576 B at 1024×600. |
| 11 | **Right, verbatim.** |
| 12 | **Right.** `SRC_MAX` 4096, `MAX_PTS` 256, `MAX_CONTOURS` 16, `MAX_DEPTH` 8. |
| 13 | **Right, verbatim.** |
| 14 | **Conclusion right, count wrong.** There is no touch driver, and the panel is an ST7789. But there are **14** driver files in `rv9_io/src` and **15** drivers in the target profile, not 29. |
| 15 | **Right, verbatim.** |
| 16 | **Right.** The quote is *"A pin opened for writing becomes an output; one opened for reading becomes an input."* |

### 1.1 Two additions to claim 5

**The row range is in *device* rows, not viewBox units.** The driver compares
`clip_y1` against `s->h`, the panel height, while the document is in whatever
`viewBox` it declares. So `rows(tree, id)` has to convert, and the conversion
needs the device size — which comes from `getstat(RV9_GS_SIZE)` on `/w0`,
returning **pixels, not characters**. A whisker that assumes viewBox == device
works at 320×172 and silently clips the wrong strip at 1024×600. This caught
me while measuring, which is how I know it is easy to miss.

**A reversed range is refused, not clamped.** `y1 < y0` returns
`RV9_IO_ERR_INVAL`, deliberately: *"a caller that asked for rows 400 to 200
has a bug, and silently drawing something else would hide it."* `0,0` is the
documented way to say "all of it".

Also worth knowing: the clip is **snapped outward to 8-row band boundaries**,
because the band is the unit the renderer works in. So `rows(tree, id)` need
not be band-aligned, but a widget straddling a band boundary repaints that
whole band. Aligning widget heights to multiples of 8 is free and saves a band
per widget — `flick` does this on purpose.

### 1.2 Claim 6 — vertical-only is a software choice

True as stated: the driver always calls `rv9_panel_blit(0, y, s->w, ...)`.

But `rv9_panel_blit` already takes `x0` and `x1`; it blits a rectangle. What
makes clipping vertical-only is that the *rasteriser* works in full-width
bands — `b.w = s->w`, and the coverage buffer is one row wide. RV-9's own
roadmap names horizontal clipping as a future gain from a retained tree.

**What this means for whisker:** design so horizontal clipping is an
optimisation you could later exploit, not something the layout model
forecloses — but be correct without it. Concretely, `rows(tree, id)` should be
able to become `bounds(tree, id)` without the widget model changing.

### 1.3 Claim 7 — right, and it found a bug

This is the one worth the whole review. The reviewer marked 6 and 7 as the
least certain, being inference rather than quotation. **7 is correct**, and
checking it turned up a defect that had been in RV-9 for as long as `flick`
had existed.

`flick`'s `PRESSED` and `NORMAL` documents hold only the button. The band is
cleared to the *device* background — `OPT_BG`, which `/w0`'s descriptor sets
to `0`, black — before anything is drawn. So the rows either side of the
button came back black, and the scene's `#10203a` was gone until something
redrew the whole window. Two black strips, on every run.

Rendered off-target through `tools/hosttest`, sampling (10, 120) — inside the
button's rows, outside its columns:

```
after SCENE     (10,120) = #102039     the scene's navy
after PRESSED   (10,120) = #000000     the device's black
above the clip  (10, 60) = #102039     untouched, as expected
```

**No geometric check catches this.** Every shape in the document is in the
right place; the failure is in what the document does *not* contain. It was
found by looking at the picture — the same lesson as RV-9's lumpy circles,
which also had the right radius and the right area.

**Fixed in RV-9 today** (`modules/flick/flick.c`, revision 2), and the rule now
sits on `RV9_SVG_SS_ROWS` in `module.h` where somebody reaching for the clip
will read it. The fix is a backing rect across the clipped rows: **59 bytes**,
and **for the rows only** — a full-screen background rect would be bytes
re-parsed once per band for pixels outside the clip.

**It costs, and claim 9's numbers move.** On the board, three runs each:

```
before:  flick: full 59 ms, press 15 ms, release 15 ms    tap 30 ms
after:   flick: full 56 ms, press 19 ms, release 19 ms    tap 38 ms
```

So **a correct tap acknowledgement is 38 ms against 56 for a full redraw**,
and the 1024×600 projection goes from ~52 ms to **~66 ms**. Tap-to-act with a
flash to confirm still stands. But whisker's budget was quoted from a
measurement of a picture that was wrong, and the margin is a third smaller
than it looked. `docs/design.md` §2.2 should be updated.

### 1.4 Claim 8 — read the saving correctly

The quote is right, and the phrase *"the saving is in bands not in parsing"*
is easy to read as *"clipping does not save parse time"*. It does. Clipping to
6 of 22 bands costs about 6/22 of a full frame, because the per-band work —
parse included — is done 6 times instead of 22.

What you do **not** get is a super-linear saving: you cannot parse once and
draw six bands. The parse is repaid per band, so **document size multiplies
the cost of every repaint**, not just of the full frame. §2.2's conclusion is
right; the reasoning is slightly off.

Measured on the host at 320×172, 40 repeats each:

| document | bytes | full frame |
|---|---|---|
| `pic.svg` | 863 | 0.65 ms |
| `chart.svg` | 1,134 | 0.69 ms |
| `labelled.svg` | 1,208 | 0.74 ms |
| `dense.svg` | 2,592 | 1.27 ms |

A straight-line fit is **0.34 ms + 0.00036 ms per byte**. So a document that
fills `SRC_MAX` costs about **2.5× a terse 1 KB one**, at any clip. That is
the number to design terseness against.

---

## 2. The six questions

### 2.1 What is the input? — **touch, and the board is bought**

**Waveshare ESP32-P4-WIFI6-Touch-LCD-7B**, ordered 2026-09-27, arriving
7–20 October: 7 inch, **1024×600, five-point capacitive touch**, with an
OV5647 camera. So:

- **The second function is `hit(tree, x, y)`.** Build it.
- **Five-point is available; ignore four of them.** There is no gesture layer
  and whisker should not be one. One point, one id.
- **The C5 will probably never have touch** and stays the real-time target.
  What it has is `/gpio`, so physical buttons are possible there.

So the answer is *not* "buttons for now, touch later". It is **touch on the
big board, buttons possible on the small one**, and the honest design
consequence is: build `hit` now, and keep the widget model such that a focus
order can be **added** later without reshaping it — give every widget a stable
declaration order and you have a traversal order for free, unbuilt.

**But the P4 changes the substrate, and this is the part the review could not
have known.** Its MIPI-DSI panel has **no framebuffer of its own** — the P4
scans it out of PSRAM. On the C5 the ST7789 holds its own GRAM, which is
*why* "untouched rows simply stay" is true and why band clipping works at all.
On the P4:

- RV-9 owns the framebuffer (1.2 MB at 16bpp, affordable only because of the
  32 MB of stacked PSRAM),
- "untouched rows stay" becomes RV-9's responsibility rather than a property
  of the glass,
- and **horizontal clipping becomes straightforwardly possible**, because the
  destination is memory RV-9 addresses.

**whisker names two hosts and there are three surfaces.** Write the contract
against the C5 — the review is right that the constrained host must define it
— but do not bake "vertical only" into the *model*, because the machine that
will actually run a 7-inch touch interface does not have that limit.

### 2.2 Is `SRC_MAX` raisable? — **yes, trivially. That is not the cost.**

It is `rv9_alloc(SRC_MAX)` in `svgwin_open`: one heap allocation, freed at
close. Raising it is a one-line change.

The cost is in two places and only one of them is RAM:

- **RAM.** 4 KB today out of roughly 39 KB available to programs on the C5
  (measured: heap free 59,340, floor 12,288, real-time reserve 8,192). Going
  to 16 KB would take a third of the program budget on the C5 and **nothing at
  all on the P4**, which has 32 MB of PSRAM.
- **Time, per band.** From §1.4, roughly 0.00036 ms per byte per full frame on
  the host, ~96× that on the C5. This is the real ceiling, and it does not go
  away on the P4.

**So: stop apologising to the 4 KB, but keep the terseness — for the time
reason, not the RAM reason.** And there is a design consequence: `SRC_MAX`
should be treated by whisker as a **device property to ask about**, not a
constant, because it will differ between the C5 and the P4. There is no
getstat for it today; if whisker wants one, ask and RV-9 will add it. That is
a cheap thing to get right before there is a caller.

### 2.3 Does a fragment need its own backing rectangle? — **yes, and it was a bug**

Answered in §1.3. Not deliberate, not latent-but-harmless: actually wrong on
the board, fixed today, and the cost measured.

The refinement worth carrying into the design: **the backing covers the
clipped rows, not the screen.** 59 bytes rather than 70-odd, and more
importantly it does not grow with screen height.

### 2.4 Is a column the right first shape? — **the row is cheaper than you think**

Measured rather than argued. Three buttons as a column and as a row, each
repainting one button *correctly* (carrying its rows' backing), 300 repeats:

| | 320×172 | 1024×600 |
|---|---|---|
| full frame | 0.343 ms | 2.750 ms |
| repaint one of a **column** | 0.073 ms (21 %) | 0.676 ms (25 %) |
| repaint one of a **row** | 0.129 ms (38 %) | 1.047 ms (38 %) |
| **row ÷ column** | **1.77×** | **1.55×** |

**A row costs about 1.7× a column, and both are a fraction of a frame.**
Scaled to the C5 that is roughly 7 ms against 12 ms at 320×172 — the 38 ms tap
budget is not close to threatened either way.

So §4.3's *"prefer the column; price the row; do not reach for a grid"* is
**over-cautious**. The price of a row is real and is paid in *document bytes*
— siblings sharing rows must all appear in the repaint — not in bands. Since
bytes are the thing §1.4 says to watch, the guidance is right in kind and
wrong in degree.

**A 2×2 grid of large touch targets is affordable**, and on a seven-inch
touch panel it is the shape a person will actually want. Let the layout model
do a row. What it should *not* do is a deep nested grid, where every cell
shares rows with its whole row — that is where the byte cost compounds.

### 2.5 C, and what build shape? — **C, and the review has missed the real constraint**

**C, yes.** The house conventions prefer Zig and this is the good reason to
depart: modules are built by `tools/build_modules.sh` with a specific riscv32
gcc and a linker script that **asserts `.data` and `.bss` are empty**, and the
build then **proves position independence** by linking every module twice at
different base addresses and comparing the bytes. Making Zig honour that is
possible and is a project in itself, on the target that can least afford one.

**The constraint the review does not mention, and it matters more than the
language.** If whisker is used by an RV-9 **module** — which is what a program
on RV-9 *is* — it inherits every rule a module obeys:

- no `.data`, no `.bss` — the linker script refuses to link, which is the good
  case
- no libc: no `memcpy`, no `malloc`, no `printf`
- **no 64-bit division** — `uint64_t / n` calls `__udivdi3` in libgcc, which is
  not linked
- **no `static const` array of pointers**, and no `switch` over string literals,
  because both compile to a table of absolute addresses
- no local array initialised from a string literal — `char pre[] = "btn";`
  becomes a `memcpy` from rodata

**A widget dispatch table is the natural design for this library and is
exactly what that build refuses.** So is a `static const char *kinds[] = {...}`.
RV-9 hit this twice and the second time cost a working module a rewrite: `have`
walks a space-separated string with a helper instead of indexing an array of
`const char *`, because the array version was correct C, read better, and was
not position independent.

**Recommended build shape: header-only C, like RV-9's `modlib.h`.** It
sidesteps the module ABI entirely, each module links its own copy, it compiles
unchanged in `tools/hosttest`, and it links into gBASIC as a native module.
`RV9_MOD_LIBRARY` exists as a module *type* but nothing links against
libraries yet, so a shared-library whisker would be building that mechanism
first.

If whisker would rather be a **component** — part of the firmware, callable
through a device or a sysinfo code — say so, because that is a different
conversation with a different cost, and RV-9 would want it behind a path
rather than a linked API.

### 2.6 Is this the wrong project? — **no**

The shape is right and the argument for a separate repository is the one RV-9
would make. Two things to correct rather than reconsider:

1. **It names two hosts and there are three surfaces** (C5, P4, Linux), and
   the P4 differs from the C5 in a way that reaches the design (§2.1).
2. **It is gated on a decision that is now made.** D0 has an answer; the
   project can start.

The one thing I would push back on is the framing *"RV-9 has the surface and
not the input"*. RV-9 has the surface, and the input arrives in October on a
board already paid for. Designing as though input were hypothetical would be
the mistake now, where three weeks ago it was the correct caution.

---

## 3. What the review did not ask, and should have

Four things, in the order they will bite.

**3.1 The module rules are a design constraint, not a build detail.** §2.5.
This is the one most likely to cost an iteration.

**3.2 Nothing has measured a *tap*, and whisker cannot fix that.** `flick`
times draws. When the P4 arrives, the first measurement worth taking is
finger-down to pixel-changed, end to end, because that is the number a person
experiences and it includes an I²C touch controller, an interrupt, a process
wakeup and a repaint. RV-9's `RV9_PIO_GS_PULSE_US` work exists precisely
because stamping in the handler is how you measure the world without the
scheduler's jitter in the answer. Budget for that measurement in iteration 2
rather than assuming 38 ms is the whole story.

**3.3 There is no damage model, and `render(tree, id)` is not enough.** A real
interface changes more than one widget at a time — a value updates while a
button is held. Two widgets in different bands mean either two clipped writes
or one clip spanning both, and the second repaints everything between them.
`rows(tree, ids...)` taking a *set* costs nothing now and cannot be retrofitted
cheaply, because the caller will have written loops.

**3.4 The 4 KB budget shrinks as the screen grows.** `SRC_MAX` does not scale
with resolution: 1024×600 is 11.2× the pixels of 320×172 and the **same** 4 KB
of description. At 320×172 four buttons fill the screen and 4 KB is enormously
generous; at 1024×600 a realistic panel of twenty controls, labels and a chart
is where 4 KB starts to bind. **`SRC_MAX` is comfortable on the C5 and is the
binding constraint on the P4** — which is the opposite of what the design
assumes, since it treats the C5 as the harder host on every other axis.

---

## 4. On scaling up and down

The question behind the project, and the measurements answer it.

**Down is free.** Nothing in the layer is sized to a screen. The buffers are,
and they are the driver's: 11,904 bytes at 320×172, of which 6,144 is fixed.

**Up is nearly free for the *frame*, and not free for the *description*.**

| | 320×172 | 480×320 | 800×480 | 1024×600 |
|---|---|---|---|---|
| pixels | 55 k | 154 k | 384 k | 614 k |
| buffers | 11,904 B | 14,784 B | 20,544 B | 24,576 B |
| bands | 22 | 40 | 60 | 75 |
| full frame, `dense.svg` | 1.28 ms | 2.34 ms | 4.35 ms | 7.21 ms |

11.2× the pixels costs **5.6× the time and 2.1× the memory**. Cost tracks
**bands** — that is, height — with a mild width factor, because the per-band
re-parse does not care how wide a band is. Resolution is genuinely not the
problem.

**The rule that follows, and it is the design guidance worth taking:**

> A widget's repaint costs *(its bands ÷ total bands)* of a full frame. So a
> widget with a **fixed pixel height** costs roughly the same in absolute terms
> on a large screen as on a small one. A widget that **scales with the screen**
> costs a constant *fraction*, which means it gets absolutely slower as the
> screen grows.

A 48-row button repainted at 320×172 costs 6 bands; the same 48-row button at
1024×600 also costs 6 bands, and only 1.65× more because the bands are wider.
The same button grown to 168 rows to keep its proportion costs 21 bands, and
5.8× more.

**So: size widgets in pixels, not in percentages, and let a bigger screen hold
more of them rather than bigger ones.** That is also the right answer for
touch, where a target should be about a finger wide regardless of the panel —
which means the physical constraint and the performance constraint point the
same way, which is a good sign.

The one thing that does **not** scale is `SRC_MAX` (§3.4). More screen, more
widgets, same 4 KB.

---

## 5. What I would change in the design, concretely

1. **§2.2 — update the numbers.** Tap acknowledgement is **38 ms against 56**,
   not 30 against 59, and ~66 ms at 1024×600. The old figures were measured on
   a repaint that was visually wrong.
2. **§2.2 — the clip is in device rows.** Add it; it is a silent wrong-strip
   bug otherwise.
3. **§2.2 — say the backing covers the rows, not the screen.**
4. **§4.0 / D0 — close it.** The input is touch, 1024×600, five-point,
   arriving October. Second function is `hit(tree, x, y)`.
5. **§4.3 / D3 — soften it.** A row costs 1.7×, measured. Allow a row; allow a
   2×2 grid; refuse deep nesting. Prefer the column where it is natural rather
   than where it is contorted.
6. **§4.1 / D1 — keep C, and add the module rules** as a constraint on the code
   rather than on the build. Recommend header-only.
7. **§2.6 — rewrite.** Input is not hypothetical any more.
8. **Add a §2.7 on the P4**, which is a third surface and inverts the
   framebuffer property the C5's clipping depends on.
9. **Add `rows(tree, ids...)` to the iteration-1 surface** — the set form, not
   the single-id form.
10. **Size in pixels, not proportions** (§4), and say why: it is what makes the
    layer scale up.

Nothing here argues against the project. The thesis holds, the two functions
are the right two, and the one claim that mattered most was both correct and
worth more than a green tick — it found a bug.
