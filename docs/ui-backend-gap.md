# The distance to a whisker backend for gBASIC's `ui`

**Written 2026-11-16**, after whisker §5ah, against gBASIC `bdec61da`
(0.11.0 and after) and whisker `42106ca`. It replaces `window-gap.md` as
the measure of what is left, because the question changed: that note
asked whether whisker could draw **Studio's window** (25 words — now all
answered directly); this one asks what a **`ui_whisker.c`** must do to
stand behind gBASIC's `ui` module for **any** program — design §6.1's
first bar. Every claim below names the file it was read from or the
probe that measured it; the probes are in this session's notes, and
the two that found something are stated with their result.

---

## 1. Where gBASIC is

It did more than the proposal asked (`docs/gbasic-ui-proposal.md` §6),
the same day it was ruled (gBASIC `docs/ui_design.md`):

- **Push A** (`ad002240`, `eb783aa8`): `qt.` is `ui.`, no alias; **one
  vocabulary** — no Qt name passes through, `plainText` is refused with
  "did you mean `text`?"; the **theme record**
  `{ bg, base, ctl, hi, fg, faint, line, sel, on_sel }` plus four icon
  files. Studio migrated the same day.
- **Step 4, the split** (`ffa9d221`): `include/ui_backend.h`, a
  `GbUiBackend` table of **148 members** and **seven loop rules**; Qt's
  table is `ui_qt.c`; `GBASIC_UI` chooses at run time.
- **Step 5, `ui.can`** (`306e2936`): **eight capability groups**, each
  all or nothing, refused by name before any member is called;
  `GBASIC_UI_WITHHOLD` takes groups away to try a program against a
  smaller backend.
- **Step 6 is not built**: the contract tiers of the GUI suites, run
  under every backend. It is what will measure a whisker backend, and
  it is gBASIC's.

So **Bar 1 is now one piece of code away, plus its measure**: a
`ui_whisker.c` behind the table (gBASIC's ruling: in gBASIC, behind its
own `HAVE_`, whisker a header), checked by step 6's tiers.

## 2. The finding: the table is retained, whisker is not

`GbUiBackend` is an **object interface**. `window()` answers a handle;
`container(parent, word, …)` and `part(parent, word, …)` answer more;
properties are set by word and read back by name; a table's rows are
`table_size`/`table_set`/`table_get`; a tree is `tree_add(depth, cells)`;
a menu is built item by item; children are placed, captioned and dropped
(`child_place`, `child_drop`). The backend **keeps** all of it, and
answers questions about it.

whisker keeps nothing (D5): the caller owns every tree, widget, string
and buffer, often `static const`. That is right for a board and is why
whisker is small; it means **`ui_whisker.c` is mostly a retained model**
— per window, allocated: the trees in a `nest` array, widgets in arrays
it can grow and reorder, the strings, the buffers, a table's cells, a
tree's rows with depths, menus — rebuilt into whisker's arrays when a
child is placed or dropped, and handed to `wsk_feed` and the renderers
each event. Everything whisker draws and does is already there to drive;
the model is what is missing, and **it is gBASIC's code, not whisker's**:
it allocates, and whisker never does (D16b).

A first estimate, not a measurement: the model and the 148 members are
of the order of Qt's `qt_backend.cpp` (7,779 lines) **minus** what Qt
does for it and **plus** what whisker leaves to its caller — say 3–5
thousand lines of C.

## 3. The loop contract (`ui_backend.h`, rules 1–7)

| rule | whisker | |
|---|---|---|
| 1. the backend owns the wait; `tick` every 50 ms | `ui_whisker.c` owns a loop of `wh_poll` and `wh_wait(…)`, calling `tick` on the clock — whisker has no loop to fight (`wsk_feed` is one event) | **fits** |
| 2. ticks continue inside a modal | a modal is `wsk_ui_dialog` (§5ae) or a second window (§5af), served by the same loop with another target; nothing in whisker blocks | **fits** |
| 3. `can_display` cannot crash | SDL's host fails `SDL_Init` without aborting; the browser host always can | **fits** (one check to write: SDL with no display) |
| 4. closing the last window ends `run` | `WH_CLOSE` is the program's (§5af); the backend counts windows | **fits** |
| 5. `stop` ends `run`, windows open and readable | the model is the backend's own memory | **fits** |
| 6. every close reported once | `WH_CLOSE`, `close`, a modal's accept or reject all go through one path the backend writes | **fits** |
| 7. a handler may call back in | whisker keeps no state and no lock: a nested `wsk_feed` is a call; the depth count is the backend's | **fits** |

None of the seven asks anything whisker lacks. That was not obvious: Qt
needed a forked child for rule 3 and a reference-counted pump for 2.

## 4. The capability groups

| group | what it is | whisker |
|---|---|---|
| `windows` | a second top-level window | **has it** — §5af, both hosts |
| `file_dialogs` | choose, save, folder | **has it** — §5ae, `whisker_chooser.h` over a lister |
| `docking` | `docks`/`dock`, `layout` save/restore, `floating` | **has it** — §5ag; its saved line is whisker's own text, which the table allows (`layout_save` answers the backend's text). Not: a dock **tabbed** with another (`dock_info`'s `tabbed`) — composition with §5ah, unbuilt |
| `icons` | `SP_` names, theme names | **no**, and need not: a name is a platform catalogue. An icon **file** is every backend's — the browser host draws images (`WSK_CAP_IMAGE`), **the SDL host does not** (§6) |
| `figures` | `render_svg`, `capture` | **close**: whisker's hosts rasterise SVG (RV-9's `raster.c`); what is missing is a PNG writer and, for `capture`, a window drawn to memory. Small, unbuilt |
| `highlighting` | KSyntaxHighlighting names and themes | **no**, by design: whisker draws the program's colour runs (§5v); a catalogue of languages is a toolkit's. gBASIC could answer its own language with runs |
| `notebook` | blocks between a source's lines, folds, links, `markdown` | **no** — the largest group unbuilt; Studio's notebook needs it |
| `stylesheet` | CSS | **never** — whisker's rule (no cascade); refused by name, as gBASIC intends |

So a whisker backend's `groups` today: **`windows file_dialogs
docking`** — three of eight, and `figures` close.

## 5. The vocabulary, word by word

From `src/window_check.c` (the closed lists) and `ui_design.md` §3.
**has** = whisker does it; **compose** = the backend builds it from what
whisker has; **gap** = missing in whisker, named.

**Parts** — 17 containers, 11 leaves:

| | |
|---|---|
| has | `row` `column` `form` `toolbar` `status` `scroll` `table` (§5n) `tree` (§5p, §5t) `tabs` `tab` (§5ah) `split` (§5x) `docks` `dock` (§5ag) `menu` `popup` `item` (§5l, §5y) `field` `button` `check` `text` (§5h) `memo` (§5o, §5u) `source` (§5v) `stretch` (a weighted empty item) |
| compose | `group` (a captioned column; whisker draws no frame line, a fill can stand for one) · `separator` (a thin filled stack, as the dock's drop bar is) · `choice` (a button opening an overlay, §5l — `window-gap.md` §8) |
| **gap** | **`cells`** — a grid with `across:`/`down:` spans; §5n's header aligns columns but nothing spans · **submenus** — `menu(window, parent_menu, …)` nests menus, and whisker opens one overlay at a time · `markdown` (the notebook group) |

**Properties** — 46:

| | |
|---|---|
| has | `text` `checked` `index` `count` `columns` `shortcut` (the program's, §5s) `max_length` (the buffer's cap) `wrap` (§5u; a label by `WSK_TEXT`) `font_size` (whole pixels) `x` `y` `width` `height` (`wsk_rect`) `tip` (§5ac) `headings` (§5n) `indent` `expanders` (§5p, §5t) `tab_side` (top and bottom only) `closable` (§5ah) `floating` (§5ag) `colors` `direction` `focused` `fixed_width` (always: one monospaced font) `title` (at opening only — see host) |
| compose | `visible` (a hidden part left out of its stack's items, rebuilt — the model's job) · `read_only` (a field shown without a buffer; a read-only **memo** is a gap) · `sortable` (a header cell's click reaches the program — probed: a tap on the second header cell is CLICK naming the header, item 1; sorting is the program's) · `min_width` … `max_height` (`size`, `weight`, `cross_max`; a minimum along the axis is not whisker's) · `checkable` (a button that toggles: a check styled as a button, unbuilt) · `count` of a menu |
| **gap** | **`enabled`** — whisker has no disabled part: none is greyed, none refuses a press · **`secret`** — a password field · **`hint`** — a field's placeholder · **`default`** — the button Enter presses · **`striped`** — alternate row fills · **`bold`** — never: no font weights, whisker's rule · `emphasis` (the teaching cue's mark) · `whitespace` (a source showing spaces) · `tab_width` (see below) · `movable` (§5ah step 2) · `tab_side` left and right · `icon` on SDL · `toolbar_shows` (icons) · `shown_text` (markdown) |

**Methods** — 10: `press` `focus` `undo` `redo` `close` `select_all`
`clear` **has**; `hide` **compose** (with `visible`); `raise` **gap**
(the host contract cannot raise a window); `fit_columns` **gap** (a
header's cells sized to the widest text — arithmetic, unbuilt).

**Events** — 11: `pressed` (CLICK) `changed` (TEXT, CHANGE, SELECT)
`chosen` (SELECT — arrows give SELECT alone) `activated` (CLICK on a
selecting stack's row — a tap, or **Enter, which gives CLICK and no
SELECT**: read from `wsk__stackkeys`, so gBASIC's rule that the two
are apart already holds) `entered` (ENTER) `context` (MENU) `closed`
(WH_CLOSE) `wheel` `gutter` **has**; `block` `link` **gap** (notebook).

**Measured on the way: a tab character in a memo is refused.** A
memo holding `"a\tb"` fails `wsk_check` and `wsk_render` with
`WSK_E_TEXT` — the whole window will not draw. So no file with a tab in
it can be opened in a whisker `source` or `memo` today, and `tab_width`
has nothing to set. This is the first gap a real program would hit.

## 6. The hosts

The backend draws through a host, and the two differ more than the
backend can hide:

| | SDL (desktop) | browser |
|---|---|---|
| draws with | **RV-9's rasteriser**, compiled in from RV-9's tree (`raster.c`, `font.c`, `drv_svgwin.c`) | the page's canvas |
| text | **ASCII only** (`wh_caps` claims `WSK_CAP_ENTITY` alone) — and one monospaced font | UTF-8, monospaced |
| images | **none** | yes |
| a document | **4,096 bytes**, so a big window is drawn in bands (`fit.h`) | no limit |
| windows | real ones | panels in the page |
| title, raise | set at opening; no raise | the same |

So **the first whisker backend is the browser host's**, as the proposal
said — it draws what programs write (UTF-8, images) — and the desktop
host is second. And the desktop host is **a packaging question before
it is a code one**: it compiles RV-9's rasteriser from RV-9's tree. A
gBASIC built with it needs those three files — vendored, or whisker
shipping them, or a desktop host that rasterises another way. That is
Matthew's to decide, and it decides Bar 2's "one static binary" too.

## 7. Bar 2, recounted

Design §6.1's four, as they stand:

1. **Studio on whisker** — every word of its window is answered
   (§5ah), so the window can be drawn. What it uses beyond the window:
   `highlighting` (KSyntaxHighlighting — gBASIC would colour its own
   language), `notebook` (blocks and markdown — the largest piece
   unbuilt), `icons` by name (files instead), tab characters in files.
2. **Desktop text** — selection, the clipboard, undo, UTF-8 editing:
   **built** (§5s, §5v). Left: **proportional fonts** (whisker measures
   one monospaced advance everywhere), **Unicode on the desktop host**
   (the browser has it), **input methods** (not built, not measured),
   **tabs in text** (§5 above).
3. **A desktop's plumbing** — right-click, shortcuts, file dialogs,
   several windows, a divider: **all built** (§5s, §5ae, §5af, §5x), and
   docking and tabs besides (§5ag, §5ah). One rule learned: a browser
   keeps Ctrl-T, Ctrl-W and Ctrl-N, so a shortcut there has an Alt twin.
4. **Accessibility** — **not decided**. A note for when it is: the
   browser host is the natural first place — a page can keep a hidden
   tree of roles beside its canvas for a screen reader, from the same
   widget tree whisker hit-tests; the desktop host has nothing to offer
   one.

## 8. What follows, in order

1. **whisker, small and first — the gaps any program meets.** A tab
   character drawn (a width in spaces, `tab_width`); `enabled` (drawn
   faint, never pressed, skipped by Tab); `secret`; `hint`; `default`;
   `striped`; a read-only memo. Each a whisker iteration with its
   profile question (§7), none touching the board budgets.
2. **whisker, larger**: `cells` with spans; submenus; raise and retitle
   in the host contract.
3. **gBASIC: step 6** — the contract tiers under `GBASIC_UI`. Until it
   exists a whisker backend has nothing to be measured by.
4. **`ui_whisker.c`** — the retained model and the 148 members, on the
   browser host first, `groups` = `windows file_dialogs docking`;
   proven by step 6's tiers, withholding nothing it claims.
5. **Matthew's decisions**, made the same day: **one binary as far as is
   reasonably possible** (D23 — the desktop host's rasteriser built in,
   pinned); **accessibility is MCP first, and not only MCP** (D24 —
   whisker's palette is now gated for contrast and colour vision, and
   two defaults changed to pass).

Steps 1–2 are whisker's and can start now; 3 is gBASIC's and gates 4.
