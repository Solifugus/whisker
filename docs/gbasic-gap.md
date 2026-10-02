# What stands between whisker and gBASIC

**Written 2026-10-01**, measured against `~/development/gbasic` rather than
argued. `docs/design.md` §6 sets three bars whisker must clear before gBASIC
would take it; one is now done (`host/linux`), one waits on a board, and this
is the third, reframed by `review-2026-09-30.md` §5: not *"does whisker replace
GTK"* but *"how far is whisker from the layer gBASIC decided to make
addressable"*.

The answer is closer than expected in one direction and further in another,
and the thing in between turns out to be a hardware question rather than a
widget one.

---

## 1. The target is `gui`, not Studio

gBASIC has two GUI layers and they are not the same job.

**The declarative `gui` module** is the one `gui_addressability_design.md` §0.3
chose to build identity on, and it is tiny. `stdlib/gui.bas` is a stub — the
module is native, in `src/eval.c` — and the whole widget vocabulary its
validator accepts is:

| | |
|---|---|
| component kinds | **`button`, `label`, `input`** |
| structure | `contains` (a list of children), `spacing` |
| geometry | `width`, `height` |
| state | `enabled`, `visible`, `value` |
| identity | `id`, unique per window, exposable as `win.<id>` |
| window | `title` |

It constructs **six** GTK types in all, three of which are real widgets:
`gtk_label_new`, `gtk_entry_new`, `gtk_toggle_button_new_with_label`, plus
`gtk_box_new`, `gtk_window_new`, `gtk_application_window_new`.

**The live layer — `gtk`, `gtkui`, `sourceeditor`, `datagrid` — is a different
scale**: measured 2026-10-01, **19 distinct `Gtk.*` types** across 67 `gi` call
sites, and the list says what it is for: `ColumnView`, `ColumnViewColumn`,
`SignalListItemFactory`, `SingleSelection`, `Notebook`, `Paned`,
`ScrolledWindow`, `Stack`, `TextTag`, `ListBox`, `Frame`. That is an
application toolkit, and gBASIC Studio is 25,000 lines written against it.

**whisker is not a candidate to back Studio and should never pretend to be.**
It is a candidate to back `gui`.

## 2. Against `gui`, the gap is one widget

| `gui` needs | whisker today |
|---|---|
| `label` | **has it** |
| `button` | **has it** |
| `id`, unique per window, flat | **has it** — D4, and the same rule (`wsk_find`) |
| `spacing` | `gap` |
| `width` / `height` | `size`, `cross_max`, `wsk_area` |
| `visible` | a widget not emitted; one line |
| `enabled` | a fill and an ink; no new mechanism |
| `value` | the `input`'s content |
| `contains` | **one level**, as two trees over one device — see §4 |
| **`input`** | **nothing. This is the gap.** |

Everything except `input` and nesting maps onto something whisker already has,
most of it one-to-one. That is a much smaller distance than §6 assumed when it
wrote the bar.

## 3. The gap is a keyboard, and a keyboard is the whole byte budget

An `input` is a text field, and a text field on a touch panel needs somewhere
for the text to come from. **RV-9 has no local keyboard** — its own design
document says a USB keyboard cannot be attached to the C5 at any price, and
the terminal is the only reason the shell has one at all; keyboard input
arrives over UART or SSH, from somewhere else (`design.md` §2.6).

So an `input` on RV-9 means an on-screen keyboard. Measured, with whisker, at
1024×600:

```
  row 1: 10 keys, 1054 bytes
  row 2: 10 keys, 1054 bytes
  row 3:  9 keys,  962 bytes
  row 4:  7 keys,  778 bytes
  ----
  36 keys: 3,848 bytes, against a SRC_MAX of 4,096
```

**A bare alphanumeric keyboard is 94 % of the description budget**, before the
field being typed into, before the panel behind it, and before shift, space,
backspace or enter. `SRC_MAX` is raisable in a line on the P4 (§2.3) and is not
on the C5, where 4 KB is a third of what a program gets.

That is the real finding. The distance between whisker and gBASIC's declarative
`gui` is one widget, and that widget is the one the smaller host has no
hardware for and no room to draw.

**Three honest ways forward**, none of which needs deciding now:

1. **Text entry stays where RV-9 already puts it** — over the terminal, from a
   machine that has a keyboard. A whisker panel displays and commands; it does
   not accept prose. This is the cheapest and the most in keeping with what
   RV-9 is.
2. **`input` is a P4-and-Linux feature**, with `SRC_MAX` raised on the boards
   that can afford it, and a gBASIC backend that reports honestly which widgets
   a given host supports rather than drawing a keyboard that does not fit.
3. **A keyboard that is not a widget set** — one `<path>` of key outlines and
   one `<text>` per row rather than 36 of each. Unmeasured, and the only route
   that keeps option 2 affordable on a small screen.

## 4. And one claim in the design was wrong

`design.md` §5 says a grid will arrive as *"two stacks are two trees over one
device, each with its own area"*. **Measured while drawing the keyboard: it
does not work as built.** Each full render emits a backing rect across the
whole device, so the second tree paints over the first and only the last row
survives. The picture showed it immediately; no assertion would have.

`wsk_area` bounds where a stack *lays out*, which is what it was built for and
what fixed the toolbar. It does not make a tree composable with another. A
second tree would need to not paint a background — and then a clipped repaint
of one of its widgets must still carry the backing for its rows, which belongs
to whoever does own the background. That is a real design question and it
belongs to whichever iteration wants a grid, not to a sentence in §5.

Corrected there; recorded here because the sentence had been read as a promise.

---

## What this changes

Nothing immediately, which is the point of measuring it now. §6's bar was
written to make *not* adopting whisker cheap, and the measurement says adoption
would be cheaper than feared in widgets and dearer than feared in text entry.
The useful consequence is that **the next widget whisker builds should not be
chosen by what a toolkit usually has**, but by this table — and on this table
nothing after `input` has a caller at all.
