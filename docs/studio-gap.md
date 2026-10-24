# The distance to gBASIC Studio — measured

> **Measured against a gBASIC that no longer exists.** On 2026-10-22 gBASIC
> removed GTK, the `gi` bridge and the `gui`/`gtkui` libraries and became
> Qt-only (gBASIC `19a830f`; last GTK commit `d997c94`), and Studio moved with
> it. GUI work there now goes through a declarative `window` read by a backend,
> `qt` the first. Kept as the record of what was measured then; its figures are
> not to be quoted about gBASIC as it is now until re-measured against `window`.

**Written 2026-10-03.** Matthew: *"eventually I am hoping to build a gbasic
library around this and adapt the gbasic-studio project to it."*

That names a destination the roadmap did not have. §6 of the design asks
whether gBASIC adopts whisker as a second backend for its declarative `gui`;
Studio is a bigger thing than `gui` — an IDE with a file browser, notebook
tabs, a source editor, menus, a status bar and tabular viewers. So the
distance is worth measuring before it is argued about, the way
`gbasic-gap.md` measured the distance to `gui` and found it was one widget.

Everything below is counted in `~/development/gbasic-studio`, not estimated.

---

## 1. The seam is already there, and it is small

| | |
|---|---|
| gBASIC in Studio | **33,878 lines**, 50 files |
| lines touching any GTK-specific library | **324** |
| files containing them | **6** |
| share of the program | **0.96 %** |

Studio's own entry point says why: *"The domain model + persistence live in the
stdlib `studio_*` libraries (**headless and fully testable**); this program is
the thin entry point that runs the lifecycle and, in the display modes,
**mounts the shell view over the model**."* Five of its seven run modes —
`startup`, `build`, `roundtrip`, `stress`, `cycles` — need no display at all.

**This is the single most favourable fact in the whole question.** A backend
swap is a rewrite of the shell, not of the application. The architecture that
makes that true was built for testability rather than for portability, and got
portability anyway.

## 2. And it is not built on the layer that could not be ported

```
gtkui     0 call sites
```

gBASIC's `docs/gui_addressability_design.md` §0.3 drew the line that mattered:
`gtkui` is alive and dynamic but **"GTK-specific by construction"** — *"It is
NOT a closed widget system... you drop straight to the `gi` bridge"* — and that
was the one thing a second backend structurally could not provide. **Studio
does not use it.** It goes to `gi` and `gtk` directly, which is lower-level but
*narrower*: a known vocabulary rather than an open door.

What that vocabulary actually is, counted:

| | sites | whisker today |
|---|---|---|
| `gi.*` (the GObject bridge) | 177 | host-level; not whisker's |
| `gtk.label` | 53 | **`WSK_LABEL`** |
| `gtk.button` | 25 | **`WSK_BUTTON`** |
| `gtk.box` | 25 | **`WSK_COL` / `WSK_ROW`** (§5) |
| `filetree` | 13 | — a tree, with expansion |
| `datagrid` | 10 | — a table |
| `sourceeditor` | 7 | — a multi-line editor |
| `gtk.scrolled` | 5 | — scrolling |
| `gtk.application_window` | 5 | the host's (`/w0`, SDL) |
| `gtk.paned` | 3 | **areas** (§5c), minus a draggable divider |
| `gtk.listbox` | 3 | a column of widgets, if it fits on screen |
| `gtk.notebook` | 1 | tab buttons + a content area (§5c) |

## 3. So the gap is four things, and whisker has refused all four

Stated plainly rather than softened. `roadmap.md` §5 lists these as explicitly
*not* on the map:

1. **A multi-line text editor** — caret, selection, syntax tags. whisker has a
   **single-line field** (§5d) and that is the whole of its text entry. This is
   the big one and it is not a widget, it is a sub-project.
2. **Scrolling.** `gtk.scrolled` five times, and implied by `filetree` and
   `listbox`: a file tree is longer than a pane.
3. **A table** — `datagrid`, Studio's tabular tier.
4. **A tree with expansion** — `filetree`.

Nesting is a fifth in effect: a pane containing a list containing rows is a
stack inside a stack, which §5c's composition does not do — it places *areas*
side by side, which is enough for a paned layout and not enough for a tree.

## 4. What changed on 2026-10-03, and what did not

**The byte objection is gone.** `tools/evidence/p4_panel/` measured what §2.3
had predicted and got the opposite answer: `SRC_MAX` does **not** bind on a
1024×600 panel. A settings form is 1,286 bytes of 4,096; a screen filled with
controls never binds; only a grid of ~40 reaches the ceiling. So "whisker
cannot afford richer widgets" is **not** an argument that survives measurement
— it was the main one, and it is spent.

**And the time budget moved too, 2026-10-11, in the generous direction.** This
section said a full redraw at 1024×600 was ~640 ms. Measured on the real panel
it is **175 ms**, and a tap is **5 ms** rather than the extrapolated 66 — the
old pair was never measured. A scrolling pane is a banded repaint, which
whisker is built for, and at 2.3 ms a band a *smooth* scroll of a small pane is
no longer obviously out of reach. It still has to be priced rather than
promised, and `tools/evidence/scroll/` is now priced against the real figure.

**And the refusals were right for the host that set the budget.** §2.4 exists
because RV-9's window has no clipPath, no opacity and no CSS. An editor and a
table on a 320×172 panel with no input device are not things the C5 wants (D9).
These four capabilities belong to the **P4-and-Linux** tier, which is what D8's
profiles are for: the C5 build would not carry them.

## 5. What this does to the map

It does not change G1, G2 or G3, and it is not a commitment. It names what
**G3+** looks like if gBASIC says yes, and it makes one thing concrete that was
vague: *"the two functions are stable enough that gBASIC's renderer seam can be
written against them"* (§6) now has a specific reader — Studio's
`lib/studio_shell.bas`, 2,626 lines, of which 120 touch GTK.

**The honest sequencing**, unchanged by enthusiasm:

- Studio on whisker is **not** the next thing, and should not become the next
  thing. §6's bar is use, and the C5 readout (`wstat`) is the first real use
  and is a day old.
- Of the four gaps, **scrolling was the one to price first**, because the other
  three need it and because it interacts with the band model rather than
  sitting on top of it. **Priced 2026-10-04** (`tools/evidence/scroll/`), and
  the answer is usable: a scroll is already expressible with today's whisker,
  list *length* is free because only the visible rows are emitted, but a scroll
  step damages the whole viewport — **35 ms on the C5 and 115 ms on the P4**.
  So scrolling a *whole screen* is a discrete jump per input event. **A pane is
  not**: up to about **110 rows** fits a 30 fps frame on either board, so a
  scrolling pane can plausibly be animated, which is implied by the numbers and
  not yet demonstrated by anyone dragging anything. (The P4 figure read 425 ms
  until 2026-10-11, derived from an extrapolation; the real panel measures 175
  ms for a full redraw, not 640.) The lever is the height of the scrolling
  region and it is linear — a multi-pane layout is cheaper than one scrolling
  screen, which suits Studio, which already is one.
- A text editor is a project, not an iteration. It is worth asking whether
  Studio-on-whisker needs one at all before it needs a *viewer* — five of
  Studio's seven modes are headless, and a read-only Studio on a touch panel is
  a coherent thing that skips the hardest gap entirely.

## 6. The one number to remember

**0.96 %.** Whatever is decided, the part of Studio that knows it is drawn with
GTK is 324 lines of 33,878, in six files, and none of it is `gtkui`. That is
not a port; it is a shell.
