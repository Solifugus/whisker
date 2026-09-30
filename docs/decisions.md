# Decisions

One line per decision, newest last. A decision is recorded here **only when
ratified**, with who ratified it and the argument that settled it — not the
argument that was merely persuasive. `docs/design.md` §4 holds the reasoning; this
file holds the outcome, so that a later session does not reopen a settled
question by reading the arguments and not the verdict.

**Status of §4 as a whole: six open, none ratified.** D0 now has an *answer*
from the RV-9 session (`docs/rv9-answers.md` §2.1) — touch, 1024×600,
five-point, hardware bought — but an answer is not a ratification, and this
table records outcomes only when Matthew has ratified them.

| id | Decision | Status | Outcome |
|---|---|---|---|
| **D0** | What is the input, and therefore what is the second function? (§4.0) | **answered, unratified** | *RV-9, 2026-09-29: touch. A 1024×600 five-point panel arrives in October, so `hit(tree, x, y)`. See `rv9-answers.md` §2.1.* |
| **D1** | What language? (§4.1) | open | *recommended: C* |
| **D2** | One implementation, or a specification and two? (§4.2) | open | *recommended: one* |
| **D3** | How much layout, given no box model? (§4.3) | open | *recommended: single-axis stack, column preferred — but RV-9 measured a row at only 1.7x a column, so the recommendation is over-cautious; see `rv9-answers.md` §2.4* |
| **D4** | Is the address space the SVG `id`? (§4.4) | open | *recommended: yes* |
| **D5** | Who retains the tree? (§4.5) | open | *recommended: the caller; whisker is pure* |

The italicised column is a **recommendation, not an outcome.** A recommendation
that has sat unratified for months is not a decision, and the difference is the
whole reason this file is separate from the design.

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
