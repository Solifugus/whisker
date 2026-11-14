# The distance to gBASIC's `window`, and to Studio on it — measured

> **Recounted 2026-11-12, against gBASIC Studio at `94b36e3`: twenty-one of
> twenty-five words answered, three in part, one not** (§7, at the end).
> Everything above §7 is October's count, kept as measured.

**Written 2026-10-24**, against gBASIC at `8e804d5` and gBASIC Studio at
`fb14cba` (2026-10-23). It replaces `gbasic-gap.md` and `studio-gap.md` as the
current answer; both measured a GTK tree that gBASIC removed on 2026-10-22
(gBASIC `19a830f`) and are kept as what was true then.

Same method as theirs: count, don't characterise. Every number below is a
command run over the source, quoted with what it was run on. Where a count is
of **construction sites** rather than of widgets on a screen, it says so.

---

## 1. The seam gBASIC built is the one whisker always assumed

gBASIC's GUI is now a declarative **`window`** — a block, or the same thing
built as an ordinary record (its `docs/qt_gui_design.md` §0.9) — read by a
**backend**, of which `qt` is the first. A declaration is data: gBASIC checks
it with no Qt and no display (`qt.check`, and a headless suite of its own).

**Studio already uses the record form, and keeps it pure on purpose.**
`lib/studio_shell_qt.bas` (1,297 lines) describes the whole window as a
function from Studio's model to a record, and says, as a checked rule rather
than an intention:

> *NOTHING IN HERE TOUCHES A TOOLKIT. No `qt.`, no `gi.`, no `gtk.`. That is
> checked by a tripwire (`qt_shell_pure`)…*

Measured: of its 1,297 lines, the ones calling the `qt` module that are not
comments number **zero**. That is whisker's own premise — a widget tree is data
the caller holds and a backend turns into pixels (D5) — adopted independently
on the other side of the seam. Under GTK, Studio *built* 100-odd widgets as it
decided what the window was; there was no tree to hand anybody.

| Studio, `app/` and `lib/`, comments excluded | GTK, 2026-10-03 | Qt, 2026-10-24 |
|---|---|---|
| lines of gBASIC | 33,878 in 50 files | **25,804 in 30 files** |
| lines calling the toolkit | 324 | **516** |
| files containing them | 6 | **2** (`app/studio.bas` 471, `lib/studio_view_qt.bas` 45) |
| share of the program | 0.96 % | **2.0 %** |
| the window's description | built imperatively | **a pure record, 0 toolkit lines** |

The share doubled and the files fell from six to two. Both are what moving the
window into a record does: the description left the toolkit entirely and what
is left is the runtime half, concentrated where the events are handled. *The
two columns are not quite the same count* — October's counted any line naming
a GTK-specific library, this one counts non-comment lines calling `qt.` — so
read the direction, not the decimal.

## 2. The vocabulary: what the record says, and what whisker has

The Qt backend dispatches on **26 part words** (`strcmp(word, …)` in
`src/modules/qt_backend.cpp`) plus `table`, `tree`, menus, dialogs and a few
more handled elsewhere. Studio's record uses **25 distinct words**. Counted as
construction sites in `studio_shell_qt.bas` — a site that builds one tab per
open document is one site, not one per tab:

| word | sites | what it is | whisker today |
|---|---|---|---|
| `text` | 26 | a label (one with `wordWrap`) | **`WSK_LABEL`** — single line, truncated; no wrap |
| `button` | 21 | | **`WSK_BUTTON`** |
| `column` / `row` | 11 / 9 | linear boxes | **`WSK_COL` / `WSK_ROW`** |
| `stretch` | 9 | a spacer that absorbs the rest | **weights / `justify`** (design §5) |
| `field` | 8 | one line of input | **`WSK_FIELD`** (§5d) |
| `separator` | 5 | a rule | a sized, filled widget — trivially |
| `toolbar` | 1 | a row of buttons | a `WSK_ROW` |
| `status` | 1 | a row of labels | a `WSK_ROW` |
| `form` | 1 | label/field rows | rows of a sized label and a field |
| `tabs` / `tab` | 1 / 2 | | tab buttons + an area (§5c) — the switching is the caller's |
| `split` | 1 | resizable panes | **areas** (§5c), minus a draggable divider |
| `docks` / `dock` | 1 / 1 | edge panels | areas, minus docking and floating |
| `check` | 5 | a checkbox | — |
| `choice` | 4 | a drop-down | — needs an overlay |
| `menu` / `popup` / `item` | 1 / 2 / 8 | menus, context menus | — needs an overlay |
| `scroll` | 1 | a scrolling region | **`overflow = WSK_SCROLL`** (§5e, built later the same day) |
| `table` | 2 | rows × columns, sortable | — needs scroll |
| `tree` | 1 | expandable rows | — needs scroll |
| `memo` | 7 | multi-line text | — needs scroll, and an editor |
| `source` | 2 | a code editor: highlighting, gutter, marks | — the largest single gap |

**Fifteen of the twenty-five words have an answer today**: ten directly or by
plain composition, and five (`tabs`, `tab`, `split`, `docks`, `dock`) as
composition minus the interaction — switching, dragging, docking.
The 2026-10-03 count against GTK said four capabilities; against `window` it
is **eight missing kinds**, in three families.

## 3. The gap, in three families, in the order they depend on each other

**1. Scrolling, which the next three need.** `scroll`, then `table`, `tree`
and `memo` are all a viewport onto more rows than fit. Already priced
(`tools/evidence/scroll/`, corrected against the P4 on 2026-10-11): list
length is free because only visible rows are emitted, and a scroll step
damages the viewport, so a pane of up to ~110 rows animates at 30 fps on the
P4 while a whole screen is a discrete jump. On a desktop host none of that
budget exists (§2.8) and the question is only one of design. **Still first.**

**2. Overlays, which are a new design question rather than a new widget.**
`choice`, `menu`, `popup` and `item` all draw *on top of* other widgets.
whisker's composition draws stacks in the caller's order and `hit` is the
caller's loop over them (D5), so an overlay is *expressible* as a last stack
over the others, hit first. What is not yet decided is the backing — only the
first stack of a document carries one — and whether a band repaint under an
open menu stays correct. Unmeasured, and the one item here that touches the
renderer's model rather than adding a branch to it.

**3. Editors, which are a project.** `memo` is multi-line text with a caret;
`source` adds highlighting, a gutter and marks (Studio calls `qt.caret_to`,
`qt.mark`, `qt.marks`, `qt.gutter`, `qt.highlighting` 33 times between them,
comments excluded).
The 2026-10-03 advice stands: a *viewer* first — read-only text in a scrolled
pane — covers what most of Studio's modes show, and defers the hardest gap
rather than paying it up front.

`check` belongs to none of them and is small: a button with a state mark,
which is one branch in the emit loop and §7's profile question.

## 4. What the record cannot carry, and is the host's

Studio's 516 toolkit lines are mostly **runtime**: reading and writing state
after the window is open. By call, comments excluded: `qt.read` 150,
`qt.invoke` 96, `qt.set` 56, `qt.write` 47, then `qt.select`, `qt.is_open`
and `qt.cell` at 15 each, and a long tail; `qt.open` is 6. Two things in them matter to whisker:

- **Most of it is generic state, which whisker keeps as tree data.** The
  properties read and written are, in order: `text` 71, `plainText` 51,
  `visible` 22, `currentIndex` 10, `checked` 10, `windowTitle` 7. Under D5 the
  caller holds the tree, so each of those is a field the caller changes before
  asking for the rows to repaint — the `rows`/clipped-render loop whisker
  already has. `qt.invoke` is 42 menu `trigger`s and 36 programmatic `click`s,
  which against a pure tree are the caller calling its own handler.
- **A few are Qt by name, and one crosses a line whisker drew.** `styleSheet`
  is set **7 times** — Qt's CSS — and *no CSS* is in "What whisker must never
  require". `font` 5 and `palette` 2 are the same family. A whisker backend
  would have to map those to presentation attributes or refuse them, and which
  is right depends on what they style; that is worth reading before anything
  is built.

And the host capabilities a desktop needs that are **not widgets at all**:
keyboard focus and shortcuts (`qt.shortcuts`, `setFocus` ×7), the wheel,
right-click, the clipboard, modal and file dialogs, more than one window, and
draggable dividers. whisker is two pure functions; these belong to its host,
the way the typing loop belongs to `host/linux` (D7). The SDL host is where
they would be proven.

**And text.** whisker measures text with RV-9's fixed advance
(`wsk_text_w`). Studio's labels are proportional and one wraps. On `/w0`
that is a constraint of the substrate; on a desktop host it is a choice whisker
has not had to make, and §2.8's rule applies — name it as the host's, and push
it to the caller rather than settling it here.

## 5. What this does to the map

- **G3's second bar item gets easier, and the gap gets larger.** The seam
  exists and is the right shape; the distance is fifteen of twenty-five words
  answered, eight kinds missing, and a host's worth of input.
- **The order does not change**: scrolling, then overlays as a design
  question, then a viewer before an editor. Each is a new iteration with a
  measurement behind it (roadmap §5), and none of it is started by this
  document.
- **One argument is new, and it is gBASIC's, not whisker's.** Qt is LGPL-3 and
  cannot be statically linked (gBASIC `docs/qt_gui_design.md` §0), so every
  gBASIC GUI program needs an installed Qt, where gBASIC's releases otherwise
  link statically to start on a bare machine. A second backend with no
  dependencies would give a GUI program that property back. That is a reason
  gBASIC might want whisker; it is not evidence that whisker is ready.

## 6. The one number to remember

**Fifteen of twenty-five.** Studio's window record uses twenty-five words;
whisker answers fifteen of them today, five of those without their
interaction, and the record that would be handed to it already exists, is
pure, and touches no toolkit.


---

## 7. The recount, 2026-11-12

Against gBASIC Studio at `94b36e3` (2026-11-11), `lib/studio_shell_qt.bas`,
1,317 lines, comments excluded, by the same method: construction sites of
each word, counted from the record's own builders (`part`, `bare`, `box`,
`named`, `captioned` and the containers they make). **The vocabulary has
not moved: the same twenty-five words**, a few site counts shifted (`row`
9→7, `separator` 5→6, `item` 8→9). And the record still touches no toolkit:
0 `qt.` calls, as in October.

What moved is whisker. Twelve iterations since October's count (§5e–§5m):

| word | sites | whisker, 2026-11-12 | |
|---|---|---|---|
| `text` | 26 | `WSK_LABEL`; `WSK_TEXT` wrapping for `wordWrap` (§5h) | **yes** |
| `button` | 21 | `WSK_BUTTON`, pressed and clicked by the toolkit (§5i) | **yes** |
| `column` / `row` | 11 / 7 | stacks, **nested** (§5f) | **yes** |
| `stretch` | 9 | weights and justify | **yes** |
| `item` | 9 | a button in an overlay (§5l) | **yes** |
| `field` | 8 | `WSK_FIELD` that **edits**, with focus and Tab (§5j) | **yes** |
| `separator` | 6 | a sized, filled widget | **yes** |
| `check` | 5 | `WSK_CHECK` (§5m) | **yes** |
| `menu` | 1 | an overlay opened from its button (§5l) | **yes** |
| `scroll` | 1 | `overflow = WSK_SCROLL` (§5e) | **yes** |
| `toolbar` / `status` / `form` | 1 / 1 / 1 | nested rows and columns | **yes** |
| `tabs` / `tab` | 1 / 2 | tab buttons and a body the program swaps — the swap is a row's `item` pointed elsewhere (`wkeys` does exactly this) | by composition |
| `split` | 1 | areas, without a draggable divider | by composition |
| `docks` / `dock` | 1 / 1 | areas, without docking or floating | by composition |
| `popup` | 2 | an overlay — opened by **right-click**, which no host sends yet | by composition |
| `choice` | 4 | a button whose click opens an overlay, its label set by the program | by composition |
| `memo` | 7 | a **viewer** is answered — wrap, text items, a scrolling page (§5g–h); **editing** many lines is not | in part — **editing since §5o, 2026-11-14; soft wrap is not** |
| `table` | 2 | rows, a selecting stack (§5m), a slice for length (§5e) — **no columns, header or sort** | in part — **columns and header since §5n, the same night; sort is the program's** |
| `tree` | 1 | nested stacks could show one; **no expand and collapse** | in part |
| `source` | 2 | a code editor: highlighting, gutter, marks | **no** |

**Twenty-one of twenty-five answered** — fourteen directly, seven by
composition (October: fifteen, ten and five) — **three in part, one not.**
"By composition" means the window can be drawn and used; what is missing is
an interaction a person would expect for free (dragging a divider, a
right-click), not a widget.

### What is left, in the order Studio would feel it

1. **`table`'s columns** — a header row and cells aligned under it; the
   rows, selection and length are already answered. Small, and Studio's
   main views are tables.
2. **Many-line editing** — `memo`'s other half: a buffer with lines, a
   caret that moves up and down, a page that scrolls to it. The field's
   editing (§5j) is most of the machinery.
3. **`tree`** — expand and collapse, which is nesting plus a toggle.
4. **`source`** — still the largest single gap, and still last.

### And what is the host's, not whisker's (§4 of this document)

From `app/studio.bas`, comments excluded: `qt.right_click` **8** — so a
host must send a secondary press (the browser can; SDL can); keyboard
**shortcuts**; the **clipboard**; file and modal **dialogs** (an overlay can
draw one, a host must supply the file system); **more than one window**; a
**draggable divider**. And **proportional text**: `/w0` has one monospace
face, a browser has every font — which is D21's runtime half, a host that
has more declaring it, still to design.

### The one number

**Twenty-one of twenty-five**, from fifteen in October. The record whisker
would be handed is unchanged and still pure; the distance that remains is
three partial widgets, one editor, and a host's worth of input.
