# Review request — for the RV-9 session

**Written 2026-09-29 by the gBASIC session, which created this project and has
never run RV-9 on hardware.**

Everything whisker claims about RV-9 was read out of
`~/development/esp32/rv9` in a single sitting. Some of it is quotation, some is
inference, and inference from source you have not run is exactly where a
confident wrong reading comes from — **three of my first claims were wrong and I
caught them only by going back** (they are listed in §3 as a calibration on the
rest).

This document asks for two things: **check §1, answer §2.** Nothing should be
built here until §2.1 has an answer, because it decides what whisker's second
function is.

> **Answered 2026-09-29 — see `docs/rv9-answers.md`.** Thirteen of the sixteen
> claims stand. Claim 7 was right *and* undiscovered: checking it found a live
> bug in `flick`, which is why the tap figures in §1's claim 9 have changed.
> This document is left as written, so the questions and the replies stay
> separable.

---

## 1. Claims to check

Each is in `docs/design.md` at the section named. A wrong one is worth more to me
than a green tick.

| # | Claim | Where | Kind |
|---|---|---|---|
| 1 | `raster.c` is **306 lines with no ESP dependencies**, and `tools/hosttest/run.sh` builds it natively because of that | §2.1 | measured |
| 2 | `drv_svgwin.c` needs **~40 lines of stubs** to build off-target, and this is why whisker is a separate project rather than an rv9 component | §2.1, README | quoted from `run.sh` |
| 3 | The SVG text **is** the display list; there is no compiled form | §2.1 | quoted from the driver header |
| 4 | **Presentation attributes only, no CSS** — and whisker must adopt that line rather than work around it | §2.1, §2.4 | quoted |
| 5 | `RV9_SVG_SS_ROWS` is a **setstat carrying a row range**; you then write a **complete** SVG document and only those bands repaint | §2.2 | read from `drv_svgwin.c:1292`, `1152-1159` |
| 6 | **Clipping is vertical only** — no horizontal clip, so a repaint covers the full width of its rows | §2.2 | inferred from the band loop |
| 7 | The band is cleared to the **device** background (`OPT_BG`), not to what the scene drew, so a fragment must carry its own backing | §2.2 | inferred from `rv9_raster_clear(&b, s->bg)` |
| 8 | Every band **re-parses the whole document**, so parse cost scales with document size even for a 48-row repaint | §2.2 | quoted from the comment at `1148` |
| 9 | `flick` times **three draws, not a tap**: 59 ms full, 15 ms press, 15 ms release at 320×172 | §2.2 | quoted |
| 10 | 1024×600 costs **24,576 bytes** of buffers and 2.9× the time of 320×172 for 11.2× the pixels; widening costs 1.7× not 3.2× | §2.2 | quoted from `roadmap.md` |
| 11 | The C5 is about **96×** slower than the host on `pic.svg` (82 ms vs 0.85 ms) | §2.2 | quoted |
| 12 | The ceilings are `SRC_MAX` 4,096 · `MAX_PTS` 256 · `MAX_CONTOURS` 16 · `MAX_DEPTH` 8, and **4 KB is the real limit on how much interface fits a screen** | §2.3 | quoted |
| 13 | Not done in the window: font families and weights, rotated text, arc x-axis-rotation, opacity, gradients, documents over 4 KB — so **whisker's look is flat because its substrate is** | §2.4 | quoted |
| 14 | **There is no touch driver.** 29 drivers in `rv9_io/src`, none for touch; the panel is an ST7789, which is display-only | §2.6 | measured |
| 15 | **No local keyboard either** — *"a USB keyboard cannot be attached to this board at any price"* | §2.6 | quoted from `design.md` |
| 16 | `/gpio` can read pins, so **physical buttons are available today and a touch panel is not** | §2.6 | quoted from `drv_gpio.c` |

**6 and 7 are the two I am least sure of**, being inference rather than
quotation, and they are also the two that most shape the design — 6 decides
whether a row of buttons is cheap or expensive, and 7 decides whether every
fragment needs a backing rectangle.

---

## 2. Questions only you can answer

### 2.1 What is the input going to be? — blocks everything

`docs/design.md` §4.0. whisker's second function depends entirely on this and
there are three different libraries behind the three answers:

- **a touch panel** → `hit(tree, x, y)`; no focus needed
- **GPIO buttons or a rotary encoder** → `focus_next` / `activate`, a mandatory
  visible focus ring, and traversal *order* matters more than position
- **a remote terminal driving it** → something else again, and a rule for which
  input wins when two are live

Concretely: **is a touch controller planned, on which board, and is there a part
already chosen?** The P4 board is mentioned in the roadmap as not yet arrived —
does it change this answer?

If the answer is "buttons for now, touch later", say so plainly, because that is
a different first iteration from either alone.

### 2.2 Is `SRC_MAX` 4,096 raisable, and what does it cost?

§2.3 treats 4 KB as a hard design constraint, and terseness as a requirement
rather than an optimisation. If it is raisable cheaply, that is a very different
design; if it is 4 KB because of RAM that does not exist, whisker should stop
apologising and design to it.

### 2.3 Does a fragment need its own backing rectangle?

Claim 7. If the band clears to `OPT_BG` and a panel's scene background is a
`<rect>`, then every clipped repaint must redraw that rect or the widget flashes
against the device colour. `flick`'s `PRESSED` document has **no** background
rect while its `SCENE` does — is that deliberate, and does the button look right
on the board, or is it a latent visual bug that nobody has needed to care about?

### 2.4 Is a column the right first shape?

§2.2 argues the vertical-only clip rewards a single column, since no two widgets
share rows. Does that match what you would actually want on a 320×172 panel, or
is a 2×2 grid of large touch targets the realistic shape and the repaint cost
simply worth paying?

### 2.5 Should whisker be C, and should it be an ESP-IDF component?

§4.1 recommends C on the grounds that RV-9 is the binding constraint. The house
conventions prefer Zig for systems work. You know what adding a toolchain to this
build would cost; I am guessing.

And if C: does whisker want to be consumable as an rv9 **module** (like `flick`),
a **component**, or plain source dropped in? That decides its build shape.

### 2.6 Is any of this the wrong project?

The honest version of the question. If the GUI direction you have in mind does
not look like "a widget tree becomes SVG", say so now — nothing is built, the
whole repository is four documents, and throwing it away costs an afternoon.

---

## 3. What I already got wrong

Listed so the rest of §1 is read with the right amount of suspicion. All three
were caught by going back to the source, not by reasoning harder.

1. **"`render` must emit a fragment for one widget."** Wrong. `RV9_SVG_SS_ROWS`
   takes a row range and you write a *complete* document; there is no partial
   XML. Corrected in §2.2, and the corrected version is more constraining than
   the wrong one.
2. **"A tap costs 30 ms."** It is the cost of *acknowledging* one. `flick` times
   three draws and there is no input device to tap. Corrected in §2.2 and §2.6.
3. **"The touch controller is another device."** Asserted in §3's first draft as
   though it existed. It does not exist at all. Corrected in §2.6, which is now
   the most consequential section in the document.

The pattern in all three: I read the *output* path carefully and assumed the
*input* path by symmetry. Worth knowing when checking §1.
