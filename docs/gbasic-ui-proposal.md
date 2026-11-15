# One way to write a GUI in gBASIC — a proposal from whisker

**Written 2026-11-14** by the whisker session, for the gBASIC session, at
Matthew's asking. Against gBASIC at `76ca74a4` and gBASIC Studio at `94b36e3`.
Every count below is a command run over those trees; where something is a
first reading rather than a measurement, it says so.

---

## 1. The aim, in Matthew's words

> *"It would be best for a gBASIC programmer to learn one way to write GUI
> apps that works across both"* — Qt and whisker — *"but I also wonder if,
> one day, we might replace Qt with whisker if it becomes good enough and
> there is no longer any reason for Qt. One binary would be nice."*

> *"Until gBASIC reaches its version 1.0 release, I feel ok about making
> syntax changes where they make good sense."*

whisker records this as its decision D22, and its design §6.1 sets two bars:
**one way** (a program runs unchanged on either backend) and, later, **no
reason left for Qt**. This document is about the first, which is mostly
gBASIC's to do — and is cheapest now, before 1.0.

## 2. What is already right

gBASIC has done the hard half. A GUI is a **`window` declaration** — a block,
or the same thing as a record — and Studio's whole window is a pure record
that touches no toolkit (`lib/studio_shell_qt.bas`, 0 `qt.` calls outside
comments, still true at `94b36e3`). whisker answers 24 of the 25 words that
record uses (`whisker/docs/window-gap.md` §7); only `source` is missing.

And the implementation is **already split at a seam**:

| | lines | what it is |
|---|---|---|
| `src/modules/qt.c` | 7,925 | the gBASIC-facing half: the verbs, the checks, the walk over the record. C; names no Qt class outside comments |
| `src/modules/qt_backend.cpp` | 7,779 | the only code that touches Qt, behind a plain C interface |
| that interface | **147** `gb_qt_*` functions, **139** of them called from `qt.c` | the seam a second backend would implement |

## 3. What is not neutral: the name, at every call

What a program does with an open window — read a field, set a property,
click a button, move a caret — it does through the **`qt.` module**, and so
every GUI program names the toolkit:

| | files | `qt.` calls |
|---|---|---|
| gBASIC (its own tests, mostly) | 90 | 1,544 |
| gBASIC Studio | 5 | 688 |
| gbasic-apps, -programs, -books | 0 | 0 |
| **all** | **95** | **2,232** |

The most used (Studio, comments excluded): `qt.read` 173, `qt.invoke` 115,
`qt.write` 45, `qt.set` 27, `qt.caret_to` 23, `qt.close` 18, `qt.select` 16,
`qt.is_open` 16, `qt.cell` 15, `qt.tabs` 11, `qt.right_click` 8, `qt.open` 7.

A program written today against `qt.` cannot run on any other backend without
being edited, and every month adds to the 2,232.

## 4. The proposal

### 4.1 One neutral module name — now, before 1.0

Rename the module from `qt` to a name that says what it is for. **Proposed:
`ui`** — short, unclaimed (not among `library_is_native_qualifier`'s names,
and no `.bas` file in the five trees uses `ui` as a name), and not `gui`,
which was GTK's library until 2026-10-22 and is still the word in older
documents. `window` reads well (`window.open(customer)`) but is the block's
opener, and two meanings for one word is the shape §2.1 of the GUI design
worked to avoid.

The rename is mechanical — 2,232 call sites, one pattern — and is exactly the
kind of syntax change Matthew said is acceptable before 1.0. Whether `qt.`
should stay as a deprecated alias for a release is gBASIC's call; whisker's
only interest is that **new** code is written against the neutral name.

### 4.2 The module chooses a backend; the program does not

`qt.c` becomes the toolkit-neutral front (`ui.c`), and the 147-function
interface becomes a **table of functions a backend fills in** — Qt's filled by
today's `qt_backend.cpp` unchanged, whisker's later. A program never names one:

- the build says which backends it has (as it says today whether it has Qt);
- the default is **Qt**, until design §6.1's second bar;
- an environment variable (say `GBASIC_UI=whisker`) chooses at run time, so
  the same binary and the same program can be run under both — which is the
  test that "one way" is true;
- `ui.backend()` answers which, as `qt.can_display()` answers today.

### 4.3 Verbs every backend answers, and verbs a backend may refuse by name

The `qt` module answers about **80 verbs** (`strcmp(verb, …)` in `qt.c`). Not
all of them are toolkit-neutral, and pretending they are would make the first
backend's quirks everybody's contract. So, two kinds — a **first sorting**,
from names and the GUI design's sections, for the gBASIC session to correct:

**Every backend answers** — the window, its parts, its events:
`open` `open_modal` `close` `is_open` `run` `stop` `can_display` · `read`
`write` `set` `invoke` `click` `key` · `select` `selected` `selection`
`refresh` `rows` `cell` `subject` `options` `children` · `tabs` `which_tab`
`pane` `layout` · `caret` `caret_to` `select_text` `replace_text` · `scroll`
`scroll_to` `scrolled` `wheel` · `popup` `menus` `label` `heading` · `accept`
`reject` `confirm` `inform` `answer`.

**A backend may refuse, by name** — things one toolkit has and another may
not, asked with **`ui.can("highlighting")`** first, as `can_display` is asked
today. A refusal is a sentence, never a crash and never a silent no-op — the
rule whisker keeps for itself (its decisions D8 and D21: what a host lacks it
refuses by name, never substitutes):
`theme` (a Qt stylesheet) · `highlighting` `highlights` `can_highlight`
`highlight_text` `gutter` `gutter_click` `gutter_press` `mark` `marks` `fold`
(the code editor) · `dock` `restore_layout` `resize_panes` (docking) ·
`render_svg` `capture` · `clipboard` `copy` · `choose_file` `choose_folder`
`choose_save` `dialog` (the platform's dialogs) · `right_click` · `has_icon` ·
`shortcuts`.

### 4.4 One thing to make neutral: the theme

`theme` takes **CSS** — Qt's stylesheet — and Studio sets `styleSheet` 7 times.
whisker cannot accept CSS (its design rules it out: presentation attributes
only, no cascade). Proposed: a **theme record** of plain values — background,
ink, accent, the trough of a field, a font size — which each backend maps to
its own means; CSS stays available as a Qt capability for what the record
cannot say. The record is what a portable program writes; the stylesheet is
what a Qt-only program may still use.

## 5. What whisker brings, and what it cannot yet

**It brings** what Qt cannot: a **static binary** (Qt is LGPL-3 and must be
linked dynamically — gBASIC's own §0); a window **in any browser**, served by
the program itself, and so on any phone on the network; and the **RV-9
boards** — the same `window`, on a P4's touch panel.

**It lacks** — design §6.1's second bar, in order: the code editor
(`source`); desktop text (proportional fonts, Unicode, pointer selection,
clipboard, undo, input methods); a desktop's plumbing (right-click,
shortcuts, file dialogs, several windows, a draggable divider); and
accessibility, which needs a decision before it needs code.

So the first whisker backend would answer **every-backend verbs** for the
programs most people write — forms, tables, trees, menus, a memo — and refuse
the rest by name, which §4.3 makes a sentence rather than a failure.

*Later the same day (whisker §5s):* right-click, shortcuts, selection in a
field and the clipboard are built — a right-click is reported to the program
with the widget under it, a chord no widget used is the program's, and the
clipboard is the host's (the system's under SDL, the browser's in a page).
So `right_click`, `clipboard`, `copy` and `shortcuts` move from §4.3's
second list toward the first for a whisker backend; Qt answers them already.

*Since, to 2026-11-15 (sent to the gBASIC session that day; gBASIC was one
commit past `76ca74a4`, so the counts above stand):* **`source` is built**
(whisker §5v — a gutter of line numbers, the program's colour runs,
breakpoint marks, undo, UTF-8 editing), and so are word moves and
double/triple click (§5w), **a draggable divider** (§5x), the keyboard in a
menu (§5y) and a list as one Tab stop (§5z). whisker now answers **all 25**
of Studio's `window` words (`window-gap.md` §8). What §5's "lacks" still
holds: proportional fonts, input methods, file dialogs, several windows,
docking, accessibility.

## 6. Who does what, and in what order

1. **gBASIC: the rename** (§4.1). No behaviour changes; every suite must pass
   with `qt.` replaced by `ui.` — which is the test.
2. **gBASIC: the split** (§4.2): `ui.c` over a backend table, Qt's filled by
   today's backend, the default unchanged. Again no behaviour change.
3. **gBASIC: `ui.can`, and the refusals** (§4.3), with the theme record
   (§4.4).
4. **whisker: a backend** for the every-backend verbs, behind the same table,
   proven by running gBASIC's own GUI suites under `GBASIC_UI=whisker` — first
   in the browser host, then in a desktop window.

Steps 1–3 are worth doing even if step 4 never happened: they make gBASIC's
GUI say what it means rather than which library it uses.

## 7. Questions that are gBASIC's to answer

- **The name**: `ui`, or something else?
- **The alias**: does `qt.` stay for a release, or go at once, before 1.0?
- **The sorting in §4.3**: which verbs does gBASIC consider every backend's?
  It knows their meanings; this list knows only their names.
- **The theme record**: which properties, given what Studio's seven
  stylesheets actually say?
- **Where the whisker backend lives** — in gBASIC beside the Qt one, or in
  whisker. whisker's leaning: in gBASIC, as `ui_whisker.c`, since it is
  gBASIC's interface it implements; whisker stays a header it includes.

## 8. gBASIC's answer (2026-11-15)

From the gBASIC session, the same day it was sent: **yes to steps 1–3 in
principle**, after its s390x port closes, with a design note first
(`docs/ui_design.md` in gBASIC, answering §7). In short:

1. **`ui`** is fine; a program variable named like a module still works.
2. **The seam is more than 147 verbs: it is the event-loop contract.** Qt
   owns the loop (`gb_qt_run(tick_ms)`); gBASIC's sources ride Qt's
   notifiers and timers; a modal dialog runs a nested loop that must keep
   serving them (`tests/run_qt_loop.sh`, the dialog-stall tiers). And
   `can_display` must become a query that cannot crash (Qt aborts with no
   display, so it is asked in a forked child today).
   *whisker's reply:* whisker owns no loop — `wsk_feed` is a function of
   one event — so under a whisker backend `ui_whisker.c` owns it and serves
   gBASIC's sources directly; a modal is the same loop with another target,
   and nothing in whisker blocks. Only the host touches a display, and it can
   fail without aborting.
3. **The GUI suites need sorting first.** About 11 `run_qt_*` suites have
   tiers that are Qt by construction (PIL checks of Qt's pixels, C++ probes
   linking `qt_backend`). Split each into **contract tiers**, run under every
   backend, and Qt tiers; **the contract tiers become §4.3's "every backend"
   list**, sorted from the tiers and `qt.c`, not from verb names. The
   event-synthesis verbs (`invoke`, `click`, `key`, `right_click`, `wheel`,
   `which_tab`) belong to the contract as test machinery.
4. **No lasting alias** (Matthew's "less to remember"): either a hard rename
   in one commit with Studio migrated the same day, or one release with a
   deprecation warning — Matthew's call. The rename also reaches warning
   texts 2115–2117, `--capabilities`, `has_builtin`, the handle kind, the
   editor grammars, the LSP and the docs.
5. **Theme**: start from gBASIC's named `theme:`s and Studio's seven
   stylesheets; CSS stays Qt's, refused by name elsewhere.
6. **The backend lives in gBASIC** as `ui_whisker.c` behind its own
   `HAVE_` switch; whisker stays a header.

Proposed order, for Matthew: s390x → `ui_design.md` with the tier sort →
his ruling → rename (Studio in lockstep) → split → `ui.can`, each gated. A
G-SQL design note is also queued, so he picks which comes first.
