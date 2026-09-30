# Decisions

One line per decision, newest last. A decision is recorded here **only when
ratified**, with who ratified it and the argument that settled it — not the
argument that was merely persuasive. `docs/design.md` §4 holds the reasoning; this
file holds the outcome, so that a later session does not reopen a settled
question by reading the arguments and not the verdict.

**Status of §4 as a whole: all six ratified, 2026-09-29, by Matthew**, on the
RV-9 session's replies in `docs/rv9-answers.md`. Iteration 1 (`docs/design.md`
§5) can start.

| id | Decision | Status | Outcome, and the argument that settled it |
|---|---|---|---|
| **D0** | What is the input, and therefore what is the second function? (§4.0) | **ratified** | **`hit(tree, x, y)`.** Not a judgement call in the end: a Waveshare ESP32-P4-WIFI6-Touch-LCD-7B — 1024×600, five-point capacitive — was ordered 2026-09-27 and arrives in October. Five points available, whisker uses one. The C5 keeps `/gpio` and probably never gets touch, so declaration order is kept stable and a focus model stays *addable* without being built. |
| **D1** | What language? (§4.1) | **ratified** | **C**, departing from the house preference for Zig with the departure made explicitly. RV-9 modules are built with a riscv32 gcc, a linker script asserting `.data`/`.bss` are empty, and a proof of position independence by double-linking. Teaching Zig all of that is a project before whisker's first line. **Build shape: header-only, like `modlib.h`** — and the module rules bind whisker's *code*, not only its build. |
| **D2** | One implementation, or a specification and two? (§4.2) | **ratified** | **One.** Two representations of one declaration drift, and the failure would be a widget hit-tested in one place and drawn in another. A shared corpus makes drift visible, not impossible. |
| **D3** | How much layout, given no box model? (§4.3) | **ratified** | **A single-axis stack, row *or* column**, per-child weight, padding as one number. **Changed by the review**: RV-9 measured a row at 1.77× a column at 320×172 and 1.55× at 1024×600 — a fraction of a frame either way, ~7 ms against ~12 ms on the C5 against a 38 ms budget. The original "column preferred, price the row" was right in kind and wrong in degree. A 2×2 grid is affordable when it comes; deep nesting is not. |
| **D4** | Is the address space the SVG `id`? (§4.4) | **ratified** | **Yes.** Document-unique by the XML specification, and the same rule gBASIC's declarative `gui` ids already follow — so gBASIC's §0.3 tension resolves rather than being traded off. |
| **D5** | Who retains the tree? (§4.5) | **ratified** | **The caller; whisker is pure.** The host already holds the SVG text and the application already holds its state; a retained tree inside whisker would be a third copy that can disagree with both. |

These are outcomes, not recommendations. `docs/design.md` §4 keeps the reasoning
behind each, because a decision whose argument is lost gets reopened — but where
the two disagree, **this file wins**.

Two of the six were *changed* rather than confirmed by the review (D0 and D3),
which is the return on having asked.

---

## Already settled, before the repository existed

These were decided in the conversation that created whisker and are recorded so
they are not re-litigated. They are *scope* decisions rather than design ones.

- **2026-09-29 — whisker is its own project, not an RV-9 component.** Matthew.
  Because it serves two hosts and belongs to neither; kept inside RV-9 it would
  grow ESP-IDF assumptions, which RV-9 has already half-discovered
  (`drv_svgwin.c` needs stubs off-target, `raster.c` needs none). Precedent:
  gBASIC Studio, developed separately, with only the general parts coming back.
- **2026-09-29 — the name is `whisker`.** Matthew, from a shortlist. A cat reads a
  surface it cannot see with its whiskers; RV-9's mascot is an armoured robot cat;
  and the layer's job is to feel where you touched.
- **2026-09-29 — gBASIC keeps GTK, and whisker must earn adoption.** Matthew.
  `docs/design.md` §6 sets the bar now, while nothing is invested, so that *not*
  adopting whisker stays cheap. If it serves only RV-9, that is a success.
- **2026-09-29 — RV-9 reviews this before a whisker session starts.** Matthew.
  Because every claim about RV-9 here was read rather than run, and three of the
  first ones were wrong (`docs/rv9-review.md` §3).
