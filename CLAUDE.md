# CLAUDE.md

Guidance for Claude Code working in this repository.

## What this is

**whisker** — a GUI system for a window you write SVG to. **Its charter
changed on 2026-11-11 (D16):** it is to become a *complete* GUI system, in
tiers set by what the device can do — presentation only on the C5, touch
controls on the P4, and a desktop toolkit gBASIC can use instead of Qt.

What is **built** is the bottom layer: two pure functions, a widget tree
becomes SVG text and a point becomes the id of the widget under it — no
framebuffer, no device, no event loop, no allocation. What is **designed and
not built** (`docs/design.md` §8) is a **toolkit** above it — input events in,
the caller's widget structs updated in place, *what changed* out, with the
rows to repaint — and a **host** per platform below both. The C5 compiles the
toolkit out (`WSK_NO_INPUT`, in `WSK_MINIMAL`). Older documents that say
"nothing else" or "no event loop" describe the drawing layer, and are history
about whisker as a whole. **`docs/design.md` §9 is the map of directions**
(nesting and wrap, compact fonts, transitions, shape hit, the P4 keyboard,
served applications, the browser client, print) and §9.9 the order — layout
first (D17 nesting, D18 overflow: refuse | scroll | wrap), then §8's toolkit.

It serves two hosts and belongs to neither:

- **RV-9** — `~/development/esp32/rv9`. A small modular OS for RISC-V on an
  ESP32-C5: one core at 240 MHz, ~¼ MB of RAM, no PSRAM. It already has the
  surface (`/w0`, write SVG to it), the rasteriser (`raster.c`, no ESP
  dependencies) and text (`font.c`). It has **no touch** — that arrives on the
  P4 board in October (see Status). Its roadmap heads a section
  *"Open — damage tracking, before the GUI direction."*
- **gBASIC** — `~/development/gbasic`. A C11 BASIC-family interpreter. Already
  emits deterministic SVG from pure gBASIC (`stdlib/chart.bas`, zero
  dependencies). **Its GUI is Qt as of 2026-10-22** (gBASIC `19a830f`): GTK,
  the `gi` bridge and the `gui`/`gtkui` libraries are gone, and gBASIC Studio
  moved with it. What it has instead is a declarative `window` read by a
  backend, with `qt` the *first* backend (gBASIC `docs/qt_gui_design.md`).

**gBASIC keeps Qt — for now (D22).** Matthew's aim: one way to write a GUI
in gBASIC on either backend, and perhaps one day whisker replaces Qt and a
GUI program is one binary. Design §6.1 sets the two bars;
`docs/gbasic-ui-proposal.md` (2026-11-14) is the proposal to gBASIC — a
neutral `ui.` module for the 2,232 `qt.` calls, its front split from its
backend, and verbs a backend may refuse by name. whisker is a candidate second backend — now in a literal
sense, a second reader of the same `window` declaration — and has to earn the
place. `docs/design.md` §6 records what would have to be true first. **Every
whisker document written before 2026-10-22 that measures gBASIC measured its
GTK tree** (last GTK commit `d997c94`): `gbasic-gap.md` and `studio-gap.md`
are history. **`docs/window-gap.md` (2026-10-24) is the current distance**:
Studio's window is a pure record. **Superseded 2026-11-16 by
`docs/ui-backend-gap.md`**: gBASIC has since renamed `qt.` to `ui.`, ruled
one vocabulary, split a 148-member backend table and added `ui.can`'s
eight capability groups (gBASIC `docs/ui_design.md`); the question is now
what a `ui_whisker.c` needs — a **retained model** (gBASIC's code: the
table is an object API, whisker keeps nothing) and a list of named word
gaps, the first any program meets being **a tab character, which a memo
refuses**. Read it before choosing what to build. **Matthew ruled the
two questions it raised the same day**: one binary as far as is
reasonably possible (**D23** — the desktop host's rasteriser built in,
pinned), and accessibility **MCP first, not only MCP (D24)** — whisker's
default palette is now **gated** in `src/run.sh` (white ink 4.5:1 on
every fill, state colours 10 ΔE apart under three simulated colour
deficiencies; `tools/evidence/palette/audit.py`), and the selection and
the field's well were darkened to pass (17 of 45 outputs rebaselined,
colours only). **Recounted 2026-11-15 (`window-gap.md`
§8): whisker answers all 25 of its words** — 19 directly, 6 by
composition (21 of 25 on 2026-11-12). What was left was three
interactions and a widget: ~~a draggable divider (`split`)~~ (§5x,
2026-11-15), the ~~keyboard in a menu (`choice`)~~ (§5y), ~~docking
(`docks`)~~ (§5ag; floating a pane into its own window, step 2, the same
day), ~~tabs (`tabs`, `tab`)~~ (§5ah) — **since 2026-11-16 all 25
directly**.

## Status

**Twenty-five iterations are built, and two of them have now run on the C5.**

- **§5 — the renderer.** Labels and buttons in one single-axis stack, column or
  row. Done.
- **§5b — `hit`.** A point becomes an id. Done, built early so that board time
  when the P4 arrives is spent on the measurement nobody has taken
  (finger-down to pixel-changed) rather than on writing a hit test.
- **§5c — composition.** Several stacks in one document. Done. Chosen because
  §6's gBASIC measurement said the remaining gap is `input`, and drawing the
  keyboard that needs found that two stacks could not share a screen.
- **§5d — `input`.** A text field: left-aligned, tail-scrolled, caret on
  request. Done, and it is the last widget — nothing after it has a caller
  (`gbasic-gap.md`).
- **§5e — scrolling** (2026-10-24). Not a widget: a property of a stack,
  `overflow = WSK_SCROLL` plus a `scroll` offset the caller holds. Chosen by
  `window-gap.md` as the first step to gBASIC's Qt-era `window`. A widget half
  in view is **cut by arithmetic** (no `clipPath`, and `/w0`'s row clip may
  not be leaned on); **text is whole or not at all**; out of range is
  **refused** (`WSK_E_SCROLL`), never clamped. **A stack ends at row 32,767**
  (a rectangle is four shorts) and **a document costs the square of the
  list**, so a long list goes to whisker **a slice at a time** —
  `wsk_slice`, proven byte-identical to the whole list. In the default
  profile, out of `WSK_MINIMAL` (496 bytes). **On the P4, 2026-11-11** (`wscroll`, §5e):
  a 112-row pane repaints in **37.5 ms** (finger to pixels 42) and a whole
  screen of rows in **229** — a pane drags smoothly, a screen steps. The
  "~110 rows at 30 fps" derivation was 12 % optimistic; quote the measured.

- **§5f — nesting** (2026-11-11). A widget may be a stack (`WSK_STACK`),
  named by **index** into the root's `nest` so panels stay `static const`.
  Layout, check, hit, rows and render all descend; **the id is prime and a
  path is checked link by link** (D19); clipping accumulates through every
  scrolling viewport above. A scrolling stack asks only its padding along its
  axis. **1,780 bytes, default profile only** (`WSK_NO_NEST`, in
  `WSK_MINIMAL`); **each level costs 256 bytes of stack** beneath the
  rasteriser. `wnest` ran on the P4 2026-11-11: seven taps, each named the right widget
  at depth.

- **§5g — wrap** (2026-11-11). `overflow = WSK_WRAP`, D18's third answer:
  items flow onto new lines, each line as deep as its deepest item, weight
  and justify a line at a time. A wrapping stack asks only for its widest
  item along its axis and for its lines' depth at the width it will get
  across. **A text window is composition**: word labels in a wrapping row
  inside a scrolling column. 752 bytes, default only (`WSK_NO_WRAP`). `lgap` spaces
  lines apart from words.

- **§5h — text items** (2026-11-11). `WSK_TEXT` names a string by index in
  the root's `str` (runtime array, so panels stay static). In a wrapping row
  its **words are units of the flow** — buttons may sit inline among them;
  elsewhere it is a long label. A line of words is **one `<text>` run**: the
  §5g paragraph drew the same pixels in 610 bytes instead of 1,415.
  `wsk_hit_word` names the word. 1,104 bytes, default only (`WSK_NO_TEXT`).
  **The default profile is now ~8.8 KB for a small panel** (4.7 before
  nesting), so **`WSK_PANEL` (D20)** drops wrap and text: 6.7 KB.

- **§5i — the toolkit** (2026-11-11), §8's first iteration. `wsk_feed(&ui,
  &event, notes, max, &y0, &y1)`: DOWN/MOVE/UP/WHEEL in; CLICK and SCROLL
  notices out, naming the widget by tree and index, with the rows to repaint
  (`wsk_render_rows`). Press holds a button via **`held` on the root tree —
  the toolkit never writes a widget**, which may be in flash. Drag past a
  16-px slop scrolls the deepest scrolling stack, clamped by the toolkit.
  `WSK_NO_INPUT`, in `WSK_MINIMAL`. ~256 bytes over hand-written pressing.

- **§5j — editing and focus** (2026-11-11). A field's text, when it changes,
  lives in a **caller-owned `wsk_buf_t`** beside the tree (`root.buf`), named
  by the field's `sub` — never in the widget. `focus` on the root; a tap on a
  field focuses it with the caret where the finger was, **a tap on anything
  else leaves focus alone** (so keyboard keys don't steal it). TEXT and KEY
  events edit; Tab/Shift-Tab walk fields in declaration order. The Linux host
  has real keys, text, drag and wheel (`run.sh --edit`). **`wkeys`**, the P4
  keyboard, is two layers because 43 keys are 4,298 bytes of 4,096 — and
  Matthew typed "hello i am matthew.  i can type 42" on it, 2026-11-11.

- **§5k — the browser is a host** (2026-11-11, overnight). `host/web/`
  implements `host.h` over plain HTTP (Server-Sent Events out, POSTed events
  in, no library): **`edit.c`, `demo.c`, `input.c` run in a browser
  unchanged**. The page keeps the board's banded rule on a canvas — checked in
  headless Chrome: clipped repaints equal a full redraw, 0 pixels differ — and
  sets `letter-spacing` so its monospace advance is whisker's half-size.
  `WH_EXPOSE` joined the contract. `host/web/run.sh` serves on :8090.

- **§5l — overlays** (2026-11-12). A menu is any tree in `nest` opened over
  the root (`root.over`, placed by `wsk_over_open` from an anchor: below,
  else above, kept on screen). Drawn after the root on its own backing; hit
  first; nothing falls through it. Toolkit: `wsk_ui_open`; a press off it
  closes it (CLOSE, no click beneath); a button in it is CLICK then CLOSE;
  Escape closes. Band repaints under an open menu are exact. `WSK_NO_OVER`.

- **§5m — check, selection, focus on buttons** (2026-11-12). `WSK_CHECK`'s
  ticked state is a byte in the caller's `root.state`; selection is a stack's
  (`select`, `sel`, `selfill`) in its writable tree. Tab reaches fields,
  buttons and checks; a focused button wears a ring of thin rectangles and
  Enter clicks it. All in rectangles, no `<path>`. `WSK_NO_CHECK`.
  Default build for a small panel is now 10.5 KB; `WSK_PANEL` 8.4 KB.

- **§5n — table columns** (2026-11-12). The **header decides**: a stack with
  `head` makes every row in it take its cells' places and widths from the
  header's cells (laid out across the row's own area, so scrolling never
  moves columns). `talign = WSK_ALIGN_START` for cell text. Selection finds
  the deepest selecting stack, so a tap on a cell chooses its row.

- **§5o — the memo** (2026-11-14). `WSK_MEMO`: a field of many lines in the
  same caller-owned buffer, `\n` between lines; the buffer's `top` keeps the
  view, moved only to keep the caret in sight (drawing never writes it).
  Enter breaks a line, Up/Down keep the column. Soft wrap since §5u. 668
  bytes; `WSK_NO_MEMO`, in `WSK_MINIMAL`, kept by `WSK_PANEL`.

- **§5p — trees** (2026-11-14), as Studio's `window` says them: a **flat
  list with depths**, and **the program owns expansion** (gBASIC measured
  why). A widget's `depth` byte (in the struct's padding) times a stack's
  `indent`; in a table only the first cell indents. `▾`/`▸` are not ASCII
  and are refused — a D21 runtime question.

- **§5r — capabilities** (2026-11-14), D21's runtime half. A host says
  what it draws beyond `/w0` in `root.caps` (`wh_caps()` in the host
  contract): UTF-8 (measured in characters), `WSK_IMAGE`, clipping (a label
  slides at a scroll's edge, D12). Needing more than the host said is
  `WSK_E_HOST`. **caps = 0 is byte for byte what whisker always drew.**
  SDL/RV-9 claim nothing; the browser claims UTF-8, images, clipping.

- **§5s — a desktop's plumbing** (2026-11-14). Right-click is its own event
  (`WSK_EV_MENU` → `WSK_NOTE_MENU`; the program opens a menu with
  `wsk_ui_open_at`). Modifiers ride on `ev.key` (`WSK_MOD_*`; a chord is a
  lower-case character, Ctrl-S = `'s' | WSK_MOD_CTRL`), and **a chord no
  widget used is the program's** (`WSK_NOTE_KEY`) — that is all shortcuts
  are. Selection is the caret and a `span` in the caller's buffer, drawn
  only in the focused field. **The clipboard is the host's** (`wh_copy`,
  `wh_paste`): Ctrl-C copies into caller memory (`ui.clip`) and notes COPY;
  Ctrl-V notes PASTE and the program calls `wsk_ui_paste`, all or nothing.
  `WSK_NO_TEXTSEL` (844 bytes), set by `WSK_PANEL`. Escape is a key on SDL
  now, not a quit.

- **§5t — a tree's arrows** (2026-11-14). `WSK_MARK_SHUT`/`_OPEN`/`_LEAF`
  on a label or button: a filled triangle in a gutter of one font size, one
  `<path>` — **whisker's first shape that is not a rectangle, and on
  purpose its only one**. Every host draws it, so the SDL window and the P4
  show ▸/▾ where they used to need `+`/`-`. Refused under `WSK_NO_MARKS`.

- **§5u — a memo that wraps** (2026-11-14). `wsk_buf_t.wrap`: soft breaks
  at the last space that fits, a long word cut at the width; the caret, the
  view and selection move on rows. Off by default (`source` must not wrap),
  and an unwrapped memo runs the same code with a room of 32,767. +108 bytes.

- **§5v — `source`, step 1: `int` positions and undo** (2026-11-14).
  Studio's files are 200–326 KB, so `wsk_buf_t` positions are `int`. Every
  edit goes through one primitive (`wsk__put`) that records it in the
  buffer's **`wsk_undo_t`, caller memory**; Ctrl-Z, Ctrl-Y/Ctrl-Shift-Z.
  `WSK_NO_UNDO`, set by `WSK_PANEL`. **Zero a buffer before setting its
  fields** — it grows.
- **§5v step 2 — `WSK_SOURCE`** (2026-11-14): a memo for code — no wrap, a
  gutter of line numbers (GUTTER notice on a click), and **colour runs from
  the program** (`wsk_ink_t` in the buffer; whisker knows no syntax). Demo:
  `host/linux/code.c`, `run.sh --code`. And **`WSK_CAP_ENTITY`**: `<` `>` `&`
  in text for a host that decodes entities — the browser, and **since
  2026-11-15 the SDL window**: RV-9 decoded them at Matthew's asking (rv9
  `8e0ef96`, `docs/rv9-request-entities.md`). The boards keep
  `WSK_NO_ENTITY` until they are flashed with it.
- **§5v step 3 — UTF-8 editing** (2026-11-14): positions stay bytes,
  columns are characters — the caret, Backspace and Delete step over whole
  characters; the check refuses a caret or a colour run inside one. For a
  host that claims `WSK_CAP_UTF8` (the browser). 744 bytes, default only.
- **§5v step 4 — gutter marks** (2026-11-14): `wsk_gmark_t` in the buffer,
  one per line, a dot, §5t's arrow or a bar in the program's ink — which
  mark wins on a line (gBASIC's five categories) is the program's. A set
  `marks` pointer reserves the column. `code.c` draws breakpoints. 408 bytes.

- **§5w — words** (2026-11-15): Ctrl-Left/Right, Ctrl-Backspace/Delete,
  Ctrl-Home/End; **a double-click selects a word, a triple its line** —
  the host counts clicks and says so in `d` on a DOWN (SDL's `clicks`; the
  browser's *mousedown* `detail`, since a pointerdown's is always 0).
  `WSK_NO_WORDS`, set by `WSK_PANEL`.
- **§5x — dividers** (2026-11-15): `WSK_DIVIDER` between two panes, and
  `short *sizes` on the stack, the caller's, which a drag writes (as
  `scroll` is) — a weighted pane dragged becomes fixed; clamped, never
  refused; `WSK_NOTE_SPLIT`. `code.c` has an output pane under a divider.
  **Watch the render loop's `} else`** before the button box: a branch put
  between them draws every chosen button wrong (it happened; a check guards
  it now). `WSK_NO_SPLIT`, in both board tiers.
- **§5y — the keyboard in a menu** (2026-11-15): while an overlay is open,
  Up/Down/Home/End walk its buttons (the root's `ohi`, drawn as §5m's ring
  in the overlay's copy only) and Enter gives CLICK + CLOSE as a click
  does; a scrolling menu follows. `WSK_NO_MENUKEYS`, set by `WSK_PANEL`.
- **§5q step 1 — a world** (2026-11-15): `overflow = WSK_WORLD`, the
  caller's `wsk_place_t place[]` (32-bit x, y; size; own scale), a view
  `vx`, `vy`, `zoom` (256 = 1:1), open or bounded; clipped both ways; items
  drawn through one-item copies so their font scales; **hit topmost
  first**; `wsk_world_at`. Torus, posters (stacks inside — refused for now)
  and fade are step 2; pan/zoom and demos step 3. 1,156 bytes, default
  only; `WSK_NO_NEST` implies `WSK_NO_WORLD`.
- **§5q step 2** (2026-11-15): **the torus** (`WSK_TORUS`: offsets wrapped
  from the view, an item past the seam drawn again a world back — up to
  four times — and hit on every copy; a view bigger than the world refused)
  and **fade** (`place.fade` blends an item's colours toward the world's
  background; 0 byte-identical). **Posters** (stacks inside a world) wait
  on step 2b: a scale threaded through layout's measurements, written up in
  design §5q for Matthew to approve first.
- **§5q step 3** (2026-11-15): a drag pans a world, the wheel zooms about
  the pointer (clamped, never refused; `WSK_NOTE_VIEW`); bare floor is CLICK
  item −1 in a root world, and `wsk_world_point` maps any screen point to the
  world under it. **Demo: `host/linux/floor.c`** (`run.sh --floor`, either
  host) — a conference floor, speech scaled and faded by distance.
- **§5q step 4 — what touches what** (2026-11-15): `wsk_world_in` and
  `wsk_world_overlaps` — items overlapping a world rectangle, own scale and
  torus wrap included; what a collision does is the program's. The floor
  demo stops you at whoever or whatever you walk into.
- **§5q step 5 — a view that follows** (2026-11-15):
  `wsk_world_follow(root, "floor", item, margin, ease, &y0, &y1)` — the
  program decides when to follow (an RTS never does), whisker does the
  arithmetic: a margin % the item is kept from every edge, an easing per
  call, bounded clamp, torus short way round. The floor demo follows you.
- **§5q step 2b — posters** (2026-11-15, Matthew: *"everything in the
  world should zoom together"*): a stack placed in a world zooms as one —
  `wz`, whisker's, on the copies (0 outside a world, so every earlier
  output is byte-identical), scaling each copy's lengths and what
  measurement reads raw; **fit is checked at 1:1**, and off it the poster's
  rectangle cuts what rounding pushes over — a zoom never makes a document
  fail. Hit descends; fade reaches in. The floor's posters are posters, and
  **a frame over 4 KB is drawn in parts** (zoomed out, the desktop window
  dropped it). +804 bytes, default only.
- **§5q step 6 — moving an item, bringing it to the front** (2026-11-15):
  `place.move` (a byte in the padding) makes a drag move that item instead
  of panning — clamped in a bounded world, wrapped on a torus,
  `WSK_NOTE_MOVE`, no click after; `short *order` on the world, the
  caller's, is the z order (0 = array order, byte-identical), and
  `wsk_world_raise` or a press on a movable item puts it on top
  (`WSK_NOTE_RAISE`). The floor's posters are arranged by hand. +184 bytes.
- **§5ab step 3 — each seat repaints only what changed for it**
  (2026-11-16): `wsk_world_rows` gives the band an item covers on a
  screen; the floor sends each seat one document a frame from the first
  changed row to the last — where movers were and are, and their words —
  and the whole window only when that seat's own person or view moved.
  About half the bytes with nine people wandering; the canvas equals a
  full redraw to the pixel (`host/web/repaint.mjs`, proved able to fail).
  **One document a frame, not one a span**: each carries ~650 bytes of
  frame, and a document per span saved only 23 %.
- **§5ac — tooltips** (2026-11-15, Matthew chose press-and-hold for touch):
  a rest — a mouse resting (§5aa), a finger held (a press with
  `WSK_MOD_TOUCH`), Tab's focus — counted by **`WSK_EV_TICK`** (whisker has
  no clock; `wh_ms()` in the host contract), and after 500 ms
  `WSK_NOTE_TIP`, once. **The program gives the text** with `wsk_ui_tip`,
  or none; whisker places and draws it, inverted, over everything, never
  hit. A finger lifted after its tip showed does not click. A context
  menu during a press now ends its click. 912 bytes; kept by `WSK_PANEL`.
  **On the P4, 2026-11-16** (wnest, RV-9 `c068b04`): Matthew held a finger
  and the tip showed; a quick tap still clicked. **Board modules build
  `-DWSK_PANEL`** (their `build.conf`): at the default — the desktop's —
  each had grown ~7 KB of features it never reached.
  **Browser tests kill their Chrome however they end** — one left running
  rejoined the next run as extra seats.
- **§5ae — choosing a file** (2026-11-16): a **modal dialog**
  (`wsk_ui_dialog`: centred, nothing off it answers, the keys are its own,
  focus given back on closing; its buttons click without closing it) and
  **`src/whisker_chooser.h`**, a second header under the same rules, which
  builds gBASIC's three questions — choose a file, save as, choose a
  folder — in slots of the root the program sets aside, and reads folders
  only through a lister the program gives (POSIX: `host/linux/lister.h`).
  Cancel answers *cancelled*, never an empty path. The code editor opens by
  Ctrl-O and saves as by Ctrl-Shift-S, in both hosts. **Matthew used it in
  the SDL window, 2026-11-16: Cancel, then a file chosen and opened.**
- **§5ag — docking** (2026-11-16, Matthew: "docking sounds good"): a
  tree marked **`grab`** is dragged by itself — `WSK_NOTE_GRAB`, `DRAG`,
  `DROP`, the point the event's; a press that does not move is what it
  was, so a button in it still clicks (304 bytes; `WSK_NO_GRAB`, in
  `WSK_PANEL`). **`src/whisker_dock.h`**, a third header in the
  chooser's shape: panes (a name, a caption, a content tree, an edge)
  around a centre, built in `WSK_DOCK_TREES` slots the program sets
  aside; a caption dragged shows a bar in the accent at the nearest edge
  and dropped there the pane moves; dropped in the middle it stays; ×
  closes, `wsk_dock_show` reopens, dividers resize; **the layout is one
  readable line** (`wsk_dock_save`/`_load`). A pane's content must
  scroll or fit — whisker's rule, not the dock's. Demo
  `host/linux/dock.c` (`run.sh --dock`, both hosts); `dock.mjs` drags a
  pane left to right in Chrome. **Step 2, floating** (2026-11-16):
  a caption double-clicked takes its pane into a window of its own
  and back (Qt's gesture); the window is **the program's** —
  `shown` is 2 (`WSK_DOCK_FLOAT`), the program opens a window for it
  with a root from `wsk_dock_window` (the main root's trees, strings,
  buffers and state shared; its own focus) and feeds that window's
  toolkit to `wsk_dock_feed` too. A step-1 layout line loads as it
  did. No byte of `whisker.h`; `dock.mjs` floats and docks in Chrome.
  **Matthew used it, 2026-11-16: works in the SDL window and the
  browser.**
- **§5ah — tabs** (2026-11-16): **`src/whisker_tabs.h`**, a fourth
  header in the dock's shape, no change to `whisker.h`: tabs by **name**
  (the caption changes — Studio's `* `), closable — **the close asked**
  (`WSK_TABS_ASKED`, `t.asked`), the program deciding — and **STU-21**:
  a background tab's close or right-click never switches to it (the
  toolkit's selection did switch on a close button; the header passes
  over that SELECT). The bar scrolls and keeps the chosen tab in view;
  top or bottom. **The chosen tab is `selfill`**, as a chosen row is:
  "the body's colour" passed every check and could not be seen. Demo
  `host/linux/tabs.c` (`run.sh --tabs`, both hosts); `tabs.mjs` (12).
  **A struct copy is a `memcpy` at `-Os`**: compile a new header for
  RISC-V with the module flags and `nm -u` it. **A browser keeps Ctrl-T,
  Ctrl-W, Ctrl-N** (Matthew's Ctrl-W closed the browser tab): a program
  binding one gives an Alt chord too; Chrome checks cannot see this.
  **Matthew, after: works well in both hosts.**
- **§5al — running remotely** (D25: *both* a program's own window and a
  client, "like Xorg or RDP but better"; SSH first, attach not launch,
  detach keeps running — `docs/architecture.html`). **Built 2026-11-18**:
  the wire written down (`docs/wire.md`); **`whisker-client`**
  (`host/linux/client.c`, `run.sh --client [host:]port [viewer]`) — the
  SDL window and RV-9's rasteriser behind a socket; **`wh_who(seat)`**, a
  viewer's name from `?v=`, a claim never a credential; the browser host
  **on 127.0.0.1 unless `WHISKER_LISTEN`** (a phone now needs
  `WHISKER_LISTEN=0.0.0.0`) and `WHISKER_CAPS`; **the code editor a seat
  per viewer** over one document. A returning viewer: **caret and scroll
  follow** (moved by others' edits meanwhile), **selection and focus
  start fresh** (a dialog left open is gone). `host/web/remote.py` runs
  it all through **real OpenSSH** (a private sshd, throwaway keys): the
  client's frame equals the editor's own window to the byte; detach,
  return, both unsaved edits there; each viewer's keys reach only their
  seat; a forged generation dropped.
- **§5an — a part that is off** (2026-11-18, gBASIC's `enabled`): a
  byte `off` in the widget's padding (still 64 bytes); off goes down a
  branch; drawn faint by lightness through a faded copy of its tree (the
  fade, once the world's, now every profile's); pressed as nothing, a
  drag still scrolls; Tab passes it; a focused part turned off takes no
  keys. **`wsk_clone` must copy every field** — it missed the new byte
  and cloned widgets came out randomly off; the byte-identical
  comparison caught it. +944 bytes default, +1,152 panel.
- **§5ao — `default`, `secret`, `hint`** (2026-11-18): a byte `flags`,
  the widget's last padding (still 64), each flag refused on the wrong
  kind. **Default**: Enter in a field says ENTER and then CLICK on the
  first default button of the tree with the keys (a dialog's, else the
  window's), and with nothing focused the CLICK alone; a one-pixel frame
  in the ink, outside focus's ring. **Secret**: a star per character,
  everything else byte for byte an open field's; Ctrl-C/X do nothing and
  are not the program's either; word moves and a double-click take the
  whole. **Hint**: the widget's own text, ink half way to the well
  (5.29:1, now in the palette gate), while the buffer is empty — a
  field or memo, with a buffer or refused. +688 default, +700 panel.
- **§5am — the wire on one stream** (2026-11-18): a connection opening
  `WHISKER 1 v=… caps=…` is a seat on that connection, lines both ways
  (`docs/wire.md` §8) — nothing else speaks for it, a POST with its very
  generation dropped. So **`whisker-client --ssh you@host [port]`** is
  OpenSSH's own `ssh -W`, no tunnel; a screen **says what it draws**
  (`wh_seat_caps(seat)`; `wh_caps()` = every screen's) and the editor
  gives each seat its own — a page sent é, the client a marker, at once;
  **`WHISKER_DISPLAY`** makes a program dial a `whisker-client --listen`
  (several programs, a lobby window), re-dialling every 3 s while the
  display is away. `host/web/duplex.py` (14) through real OpenSSH,
  `ssh -R` included.
- **§5ak — Page Up/Down, and a window that behaves** (2026-11-17,
  Matthew: no focus on opening, the X default icon, no Page Up/Down):
  `WSK_KEY_PAGEUP`/`PAGEDOWN` — a memo moves caret and view a page
  together, column kept, Shift selecting; a list moves its choice by
  the rows it shows; elsewhere nothing (named keys are never the
  program's, only chords). The SDL host **raises and asks focus** for
  each window (untestable under the dummy driver — Matthew's to see),
  sets **whisker's icon, drawn in code** (blue square, whiskers, orange
  nose), class `whisker`; the page has it as an SVG favicon.
- **§5aj — what a file holds that a memo cannot draw** (2026-11-16,
  Matthew: files load "nicely except where unsupported characters are
  involved"). Measured: CRLF, a lone CR, Latin-1 and control characters
  refused in both hosts, everything beyond ASCII in the SDL window — and
  one refused the whole file. **Step 1, the program's**
  (`host/linux/textfile.h`): line endings converted only when consistent
  and written back as they came, mixed left alone; not-UTF-8 read as
  Latin-1, written back as Latin-1 or refused, never lossily; the code
  editor opens Windows and Latin-1 files. **Step 2, markers**: in a memo
  or source, what the host cannot draw (control characters; on SDL,
  everything beyond ASCII) is **one faint box in its column**, the
  buffer exact; labels and fields refuse as before (D8 for them).
  **Found by its picture: `/w0` drops a `<text>`'s leading spaces** — a
  memo line indented with spaces had always drawn at column 0 on RV-9;
  now pieces are placed past their spaces, never sent opening with them.
  `WSK_NO_UNDRAWN`.
- **§5ai — tab characters** (2026-11-16, the first gap `ui-backend-gap.md`
  measured): a tab in a memo or source buffer **stays a byte** and draws
  to the next stop (`wsk_buf_t.tab`, 0 = 8), counted from the row's
  start; each piece between tabs its own `<text>`. Four helpers turn
  bytes into columns and every memo path uses them — a click inside a
  tab goes to its **nearer edge**, Up/Down to **before** it, a soft
  break **at** it. A field still refuses one, as it refuses a line break.
  `tabkey` makes Tab type one (Shift-Tab still walks); the code editor
  sets it and **opens tab-indented files in both hosts**. +882 bytes
  default, +746 panel; `WSK_NO_TABCHAR`. One check rebaselined on
  purpose ("a memo holding a tab is refused").
- **A third independent read, 2026-11-16** (design, after §5ag): a grab
  ended by a menu, a dialog or a lost lifting left the dock mid-drag —
  and the program's own grab tree moved a pane; a triple-click floated
  a second pane; focus stayed in a floated pane; the layout line
  half-loaded a bad line and refused one that fitted exactly. SDL made
  a closed window's queued events window 0's (its close a quit); a
  page that missed a window's closing drew the next into its panel;
  each SDL window leaked its driver state. 15 + 3 + 3 checks.
- **§5af — several windows** (2026-11-16): the host contract gains
  `wh_window`, `wh_window_close`, `wh_on` (which window documents go to),
  `win` on every event, and **`WH_CLOSE`** — the program decides; a
  program that never opens a second window is the host it was (QUIT). SDL:
  a real window each, with its own `/w0`; the browser: a floating panel
  each, dragged by its bar, closed by its ×. **No change to `whisker.h`.**
  The code editor's Ctrl-N opens a scratch pad (Matthew, SDL: works).
- **A second independent read, 2026-11-16** (design, before §5af): a lost
  finger's lifting froze all input; a menu over a dialog broke it; the
  chooser cut long paths and answered the clicked file not the arrows';
  the editor cut big files, opened folders and said "saved" to a full
  disk; the lister let dotfiles hide files; and **a memo drew every
  visible line into every band** (an 8-row repaint carried all its text).
  **The suites build against RV-9's committed HEAD** (`git archive`), not
  its working tree — its session may be mid-change; `RV9_WORKTREE=1` to
  override.
- **A desktop program's frames must fit 4 KB too**: RV-9's window — the
  SDL host compiles it — drops a document over 4,096 bytes, so a program
  whose picture can grow (an editor scrolled to busy lines, a floor zoomed
  out) draws through `host/linux/fit.h` (halves on 8-row bands when one
  document is refused, `WSK_E_BUDGET`). The browser host has no limit, so
  only `host/web/docsize.py` would notice: it is in the web suite.
- **§5ad — pinch** (2026-11-16): events carry `finger` (the platform's
  number; a mouse 0); a second finger down on the world the first pressed
  starts a pinch — the first's press let go, the zoom the distance's ratio,
  the world point under the midpoint kept under it, so it pans too; one
  lifted leaves the other inert. Hosts pass pointer ids (browser), finger
  ids (SDL, numbered from 1), RV-9's `id` (wnest). **A browser's tap is
  followed by mouse events of its own** — with fingers told apart they were
  a second finger, so the page drops mouse events within 800 ms of a touch.
  704 bytes; none in `WSK_PANEL`. **Pinched on Matthew's phone, 2026-11-16.**
- **A second independent read, 2026-11-16** (design, before §5ad): eleven
  defects, each with a check that fails before its fix — the world walkers
  that choose and scroll ignored the z-order; a poster laid out from its
  clamped box; scroll in a poster (refused now); a POST crashing the web
  host; sixteen seats starving their own events; a reused seat number
  obeying a stale page; the floor walking at the mouse's speed. Seats now
  carry a generation; `host/web/seathost.py` drives the host over sockets.
- **§5ab — seats: several people at one program** (2026-11-15): the host
  contract gains `seat` on each event, `wh_seats(1)`, `WH_JOIN`/`WH_GONE`
  and `wh_to(seat)`; the browser host makes each page's event stream a
  seat (seatless programs mirror as before); SDL is one seat, 0. **No
  change to `whisker.h`** — whisker keeps no state (D5), so a seat is the
  program's second set of trees over the same widgets. The floor gives each
  page a person, its own view, status and speech fading; `seats.mjs`
  drives two Chrome pages. **Step 2**: a page says its size (`WH_SIZE`, CSS
  pixels — a phone gets a phone's floor), that it is still there every two
  seconds, and that it is going (a `pagehide` beacon); six seconds of
  silence is GONE. `wsk_world_view` sets a world's view clamped as a drag
  would — what a program needs when a resized window would see past a
  bounded world.
- **§5aa — hover** (2026-11-15): hosts say `WH_HOVER` (motion with nothing
  held; -1,-1 on leaving) — SDL and the browser, never a touch panel; the
  toolkit takes `WSK_EV_HOVER` and says `WSK_NOTE_HOVER` only when what the
  pointer rests on changes (in 0 for nothing). **Nothing is drawn** — a
  tooltip, a lit button, a status line are the program's. The floor shows
  a poster's abstract as you point. 48 bytes; `WSK_NO_HOVER`, in
  `WSK_PANEL`. **A host test's poll must set hover aside**: the window
  reports the pointer as it opens.
- **§5z — a list or a world from the keyboard** (2026-11-15, Matthew chose
  "one Tab stop"): a selecting list or a world is one Tab stop with a ring;
  a list's Up/Down/Home/End/Enter, a world's arrows and `+`/`-`.
  `wsk_ui_focus` takes them. §5m's Tab check rebaselined deliberately.
  `WSK_NO_LISTKEYS`, in `WSK_PANEL`.

**`wpiano`** (2026-11-14) — two octaves on the P4's speaker, drawn in
rectangles (no shapes needed), every finger its own; Matthew: "no delay...
very nice sounds". It found two rules for any board program: **draw little,
and after the sound** (a full-height key repaint is ~200 ms of nothing else),
and **measure the stack** (8 KB copied from `wkeys` panicked the board
twice; it needs over 11.7 KB). Design §9.4.

`src/run.sh --module` is the whole of it: **1,314 checks**, including a step
that builds every profile and switch under `-Werror`, the pixels through
RV-9's own rasteriser, eighteen module builds with the double-link, and a person
looking at the pictures. `host/linux/run.sh --test` adds 123 — every demo program started — including the
typing loop end to end, the real keyboard path, right-click and the clipboard; `--input` is the same loop with a person in it.
`host/web/run.sh --test` adds 32 over HTTP, 19 in headless Chrome, 16 for
the code editor (`code.mjs`) and 24 for the conference floor (`floor.mjs`), 14 for two people on it (`seats.mjs`),
18 for docking and floating (`dock.mjs`), 13 for tabs (`tabs.mjs`), 13 for files indented with tabs, with Windows line endings, in Latin-1 (`tabfile.py`) — 184 in all.

Settled before any of that: **the RV-9 review is answered**
(`docs/rv9-answers.md`, 2026-09-29) — thirteen of sixteen claims stood, and one
was right *and undiscovered*, which found a live bug in RV-9's `flick` and moved
the tap budget. **All six decisions are ratified** (Matthew, 2026-09-29);
`docs/decisions.md` is the ledger and wins wherever it disagrees with §4.

**D0 was settled by a purchase**: a Waveshare ESP32-P4-WIFI6-Touch-LCD-7B —
1024×600, five-point capacitive touch — arrives in October. The C5 keeps `/gpio`
and probably never gets touch, so keep declaration order stable and a focus model
stays addable without being built.

**D6 and D7 (2026-10-02)** bound what comes next. `input` goes only to hosts
with an input device, so **not the C5** — not on bytes, which turned out to be
half the budget rather than 94 %, but because the C5 has nothing to press a key
with. And the **mouse on Linux stands in for touch** until the P4 arrives: a
click is one point, which is all `hit` takes.

**The board run happened, 2026-10-02.** `wflick` and `wcompose` are committed in
RV-9's tree and both draw what they claim. Three things came back
(`docs/rv9-answers-2026-10-02.md`): `MAX_CONTOURS` is **64** and a truncated
path now logs a warning, so the compact keyboard went from broken to cheapest;
all four ceilings are askable through `getstat(RV9_SVG_GS_LIMITS)`; and a stack
finding that is whisker's own, below.

**Profiles, 2026-10-02 (§7).** whisker now scales *down* as well as up. The
default is everything; `-DWSK_MINIMAL` is the C5's profile and drops the field
and the corner marks for **668 bytes, 20 %** of a small panel's code. A reduced
build draws **byte-identically** for the widgets it keeps and **refuses**
(`WSK_E_KIND`) what it lacks rather than substituting — both checked in
`src/run.sh`, the refusals being the control on the first. gBASIC's end needed
nothing *at the time*: its `gui` named five component kinds and whisker had all
five — but `gui` was removed with GTK, so that count wants redoing against
`window`.

**What is left is in `docs/roadmap.md`** (2026-10-03), which is the first thing
to read when picking this up cold. Three gates, and only one is code:

- ~~**G1 — what the C5 panel is *for*.**~~ **Closed 2026-10-03 with no code
  (D9):** the C5 panel is **write-only, no controls**, driven from the serial
  console or ssh. GPIO is for *sensors* — data in, not a person acting on a
  widget — so that panel is a **readout**, and a readout needs `rows` plus a
  clipped repaint, both of which exist. §2.6's row 2 (`focus_next`/`activate`,
  a focus mark) is **never built on the C5**. *D16 narrowed this:* a desktop
  needs keyboard focus, and design §8.7 puts it in the toolkit, which the C5
  build does not contain.
- **A readout exists.** `src/module_stat.c` / RV-9's `modules/wstat/` is the
  first whisker program meant to be used rather than demonstrated, and it is
  D9's shape: values on a write-only panel, only the changed rows repainted,
  and **nothing written at all** on a tick where nothing moved. It starts
  §6's first bar item, which G1's answer turned out to unblock rather than
  gate — a readout is a real interface and the C5 is on the desk.
- **The P4 is here, 2026-10-11**, with a working panel and `/touch`. It
  immediately made three of whisker's derived numbers wrong: §2.2's 640 ms
  full redraw and 66 ms tap were *extrapolations*, and the panel measures
  **175 ms** and **5 ms**. Anything derived from those was ~3.7× pessimistic —
  `tools/evidence/scroll/` among them, whose "never as an animation" became
  "not for a whole screen, but a pane of ~110 rows fits a 30 fps frame".
  **Check what a figure was derived from before quoting it.**
- **Touch slop is measured, 2026-10-24** (§5b, `tools/evidence/slop/` run 4):
  bias ≤ 10 px, scatter 9–21 px worst (≤ 3.1 mm), drift zero. **`hit` stays
  exact.** On the way, the "P4 draws ~45 rows low" I sent to RV-9 was
  `wslop`'s own marks, unsized buttons laid out 64 rows tall
  (`docs/rv9-answers-2026-10-24.md`). **A button with `size = 0` in a column
  is 64 rows whatever its area says** — which used to overflow silently and
  is now **refused with `WSK_E_FIT` (D10)**. Before blaming
  a layer below, check what whisker drew — RV-9 settled it by turning the
  picture over and watching the fault follow the colour.
- **G2 — the four things only the P4 can measure**: finger-down to
  pixel-changed (never measured, §2.2), ~~touch slop (§5b)~~, the stack bisected
  there rather than carried over (§5), and a panel where `SRC_MAX` actually
  binds (§2.3).
- **G3 — gBASIC answers.** §6's bar; a decline is a finish.

Two decisions are Matthew's and are explained in `roadmap.md` §4: whether the
compact keyboard form is built in (a **P4-only** question — 115 bytes is all a
field-plus-keyboard leaves of `SRC_MAX` at 1024×600, so expect to need it), and
whether `tools/notation` ships (which D9 just gave a trigger: a C5 readout whose
layout is compiled in cannot change without a reflash).

## Read before working

1. `docs/decisions.md` — twenty-six ratified outcomes (D0–D25), one screen. **First**, so that
   the design's §4 arguments read as history rather than as open questions.
   Then `docs/roadmap.md`, which is what is left and what would let us stop.
2. `docs/architecture.html` — the bird's-eye view (layers, local and
   remote, languages, who owns what); then `docs/ui-backend-gap.md`, the
   distance to gBASIC, and `docs/wire.md`, the protocol a remote screen
   speaks.
3. `docs/design.md` — all of it. It is the whole project, and it has been
   revised against the review, so it and the answers no longer disagree.
4. `docs/rv9-answers.md` — the measurements and the four things the review did
   not think to ask. Read §2.5 before writing any C, and §4 before deciding how
   anything is sized.
5. `docs/review-2026-09-30.md` — the substrate checked by running it. Five
   findings; §7 lists what they change. Read it before §5, because two of them
   change iteration 1's done-criteria and one changes what a tree may look
   like. The programs behind every number are in `tools/evidence/`.
6. `docs/rv9-review.md` — the questions as they were put. Historical; kept so
   question and answer stay separable.
7. `~/development/Software-Development-Conventions.md` — the house conventions.
8. For anything touching the substrate: `~/development/esp32/rv9` —
   `components/rv9_io/src/drv_svgwin.c` (its header states two design decisions
   whisker adopts), `raster.c`, `modules/flick/flick.c` (the only working example
   of a clipped repaint, and the source of the timing figures), RV-9's
   `docs/reference.md` §9 for `RV9_SVG_SS_ROWS` and §5 for the module rules, and
   `tools/hosttest/run.sh`, which builds the renderer natively and writes a PPM.

## Conventions

`~/development/Software-Development-Conventions.md` governs. What matters most
here:

- **Design document first**, updated before each iteration.
- **Iterations are working products**, not layers of scaffolding. The first is
  labels and buttons in one single-axis stack, column or row; see `design.md` §5.
- **Few to no dependencies**, and mature ones where unavoidable. whisker's whole
  argument is that the layer beneath it is already done.
- **A new widget kind adds a branch to the shared emit loop**, so it arrives
  with §7's question attached: which profile is it in, what does it cost, and
  does the reduced build refuse it by name? The module rules forbid the
  dispatch table that would make this automatic (§4.1), so it has to be asked
  each time.
- **Tabs for indentation.**
- Tests are kept for regression.

## The two budgets, which are not negotiable

Both are measured on real hardware and recorded in `docs/design.md` §2.2–§2.3.
Anything proposed here is priced against them:

- **4 KB of SVG** (`SRC_MAX`) is the ceiling on how much interface fits on a
  screen. It is *raisable in one line*, so respect it for the right reason: the
  cost is **time, paid once per band**, roughly 0.34 ms + 0.00036 ms/byte on the
  host, so a full 4 KB document costs ~2.5× a terse 1 KB one. It also does **not
  scale with the screen** — but the old claim that this made it *"the binding
  constraint on the P4"* was **measured and overturned** 2026-10-03
  (`tools/evidence/p4_panel/`, §2.3): a realistic 1024×600 panel is 1–1.7 KB,
  and a screen filled with a column of buttons never binds at all. What binds
  is **density** — about forty controls visible at once — and only a grid gets
  there, which is why a keyboard is the one panel that approaches it.
- **~19 ms of latency and ~38 ms of work per tap** at 320×172 (~33 and ~66 at
  1024×600), against ~56 ms and ~640 ms for a full redraw. Keep the two apart:
  `flick`'s press is 19 ms and its release is 19 ms, and the finger lifts
  between them — what a person waits for is the press. Quoting 38 as a latency
  budget is twice as pessimistic as the hardware deserves. A full re-render per interaction is
  therefore **not** affordable and a banded one is. Note the wording: `flick`
  times three *draws*, and **nothing has yet measured a tap arriving** — that is
  iteration 2's first measurement.

  *These were 30 ms and 59 ms until 2026-09-29.* The old figures were measured on
  a clipped repaint that left two black strips on the panel. Do not quote the old
  ones from any document that still carries them.

  The mechanism is **not** a fragment of XML — `RV9_SVG_SS_ROWS` takes a row
  range and you write a **complete** document containing only what intersects
  those rows, **carrying the backing for those rows** or the scene's background
  disappears from them. The range is in **device rows, not viewBox units**, and
  snaps outward to 8-row bands. The clip is **vertical only** *today*, which is a
  software choice rather than a hardware limit — so do not let the layout model
  depend on it.

## Evidence standards

The same as gBASIC's, and they are the reason its measurements are worth
quoting:

- Tests first where feasible; keep goldens byte-exact, and a behavioural change
  that moves a golden is a deliberate, listed rebaseline.
- **Measure, don't assume.** Report what you could not verify.
- A test that asserts an absence needs a control, or it is satisfied by a
  harness that ran nothing. `hit` returning an id is satisfied by "always return
  the first widget" unless a point in the gaps is asserted to return nothing.
- Never mark anything "verified".
- **A picture can be geometrically perfect and still be wrong in a way only an
  eye catches.** RV-9's own lesson, from circles that came out lumpy with the
  right radius and the right area. Render to a PPM and look at it.

## The module rules bind the code, not just the build

**Read `docs/rv9-answers.md` §2.5 before writing any C.** A program on RV-9 *is*
a module, so whisker inherits every rule a module obeys: no `.data`, no `.bss`,
no libc, **no 64-bit division**, **no `static const` array of pointers**, no
`switch` over string literals, no local array initialised from a literal.
**Plain `static const` data is fine** — a widget tree may be a static array
with inline `char[N]` fields or integer offsets into a string blob; it may not
be a table of pointers. It is addresses in rodata that break this. Each of
the last three compiles to a table of absolute addresses and breaks position
independence, which `tools/build_modules.sh` *proves* by linking each module
twice at different bases and comparing the bytes.

**Staging a module in RV-9's tree? Its `build.conf` device list is
SPACE-separated.** `devices="/w0,/tsens"` is one device name with a comma in
it, and `wstat` was refused at fork for needing a device no machine has —
correctly, and with an exact message. Guessing a separator from a
one-element example (`devices="/w0"`) is what caused it.

**A widget dispatch table is the natural design here and is exactly what that
build refuses.** RV-9 hit this twice; the second time cost a working module a
rewrite. Iteration 1's done-criteria include compiling under those flags, so the
failure happens loudly now rather than quietly in iteration 3.

## What whisker must never require

Recorded as not done in RV-9's window, so whisker may not depend on any of it:
font families and weights, rotated text, arc x-axis-rotation, opacity,
gradients, documents over 4 KB. And no CSS — presentation attributes only, which
is the line RV-9 drew to avoid a cascade, a selector engine and a box model.

whisker's look is flat because its substrate is.

## `/w0` is not the display system, it is *a* display system

**The standing hazard in this repository**, and the easiest one to fall into,
because `/w0` is the only substrate that has been on a bench and it sets every
budget above. Counted honestly whisker already faces several display systems:
`/w0` on the C5; `/w0` on the P4, where RV-9 owns the pixels in PSRAM and every
figure moves; RV-9's own `lcdcon`, a second driver on the *same panel*
(`panel.h`: *"shared by every driver that draws on it"*); whisker's SDL host;
and gBASIC's Qt, which renders on its own loop where none of this arithmetic
exists at all. `design.md` §2.8 is the table.

**So name the substrate a finding came from.** A measurement on `/w0` is
evidence about `/w0`. It is evidence about *whisker* only where it shows
whisker making a choice it did not have to make — and the usual right answer to
such a finding is to push the choice back out to the caller, not to settle it
here. The worked example follows.

### Drawing to RV-9's `/w0` runs on the writer's stack

Measured 2026-10-02 after RV-9 bisected two whisker programs' stack
declarations and found both wrong, one of them overflowing on the board. The
cause is not whisker's frames. **whisker streams rather than buffers**, so the
`put()` that finishes a document runs RV-9's entire rasteriser *with every one
of whisker's layout frames still live beneath it*. The two depths add.

So `design.md` §5's "a sink costs nothing" is true about memory and false about
stack **on that host**, and three things follow:

- **An RV-9 module should not buffer, and that is measured, not assumed.**
  `tools/evidence/stack/` builds `module_compose.c` against a buffering twin
  that differs only in the sink: buffering saves **400 bytes of stack** and
  spends **4,096 of statics**, out of one process footprint. A bad trade by
  3,696 bytes. (256 and 3,840 before §5f moved the drawing loop out of line.) Note the scope — this settles what an RV-9 *module* passes, not
  what whisker's API is. A sink that appends to an array *is* the buffer form,
  so a host with an allocator and a real stack buffers by passing a different
  sink, and whisker never knows. Keep it that way.
- **Do not hand a whisker program a stack number from `modules/pic`.** Its
  2,080 is the only figure written down anywhere and it is *not* an upper
  bound: against RV-9's bisections a whisker document costs the rasteriser at
  least **4,528** bytes, and the figure moves by at least 1,232 bytes between
  two different documents. There is no single number for "drawing to `/w0`".
- **What a program can be told is its own chain**, which is what
  `tools/evidence/stack/run.sh` prints — **640 bytes for one stack, 960 for a
  composite, and 304 more for each level of nesting** (256 before wrap and text) (§5f; it was 560 and
  800 before nesting, which made drawing recursive). That part is host-neutral, because it is measured from whisker's
  own frames and nothing else. The rest has to be bisected on the hardware, and
  bisected again on the P4 rather than carried over from the C5.
