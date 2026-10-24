# Decisions

One line per decision, newest last. A decision is recorded here **only when
ratified**, with who ratified it and the argument that settled it — not the
argument that was merely persuasive. `docs/design.md` §4 holds the reasoning; this
file holds the outcome, so that a later session does not reopen a settled
question by reading the arguments and not the verdict.

**Status of §4 as a whole: all six ratified, 2026-09-29, by Matthew**, on the
RV-9 session's replies in `docs/rv9-answers.md`.

**D6 through D9 came later** and are not part of §4: they are what
`gbasic-gap.md`'s measurement, a dead touch monitor, the two ends of §2.8's
table and the C5's actual wiring forced a choice about. They are here rather than in a document of their
own because the rule is one ledger.

| id | Decision | Status | Outcome, and the argument that settled it |
|---|---|---|---|
| **D0** | What is the input, and therefore what is the second function? (§4.0) | **ratified** | **`hit(tree, x, y)`.** Not a judgement call in the end: a Waveshare ESP32-P4-WIFI6-Touch-LCD-7B — 1024×600, five-point capacitive — was ordered 2026-09-27 and arrives in October. Five points available, whisker uses one. The C5 keeps `/gpio` and probably never gets touch, so declaration order is kept stable and a focus model stays *addable* without being built. |
| **D1** | What language? (§4.1) | **ratified** | **C**, departing from the house preference for Zig with the departure made explicitly. RV-9 modules are built with a riscv32 gcc, a linker script asserting `.data`/`.bss` are empty, and a proof of position independence by double-linking. Teaching Zig all of that is a project before whisker's first line. **Build shape: header-only, like `modlib.h`** — and the module rules bind whisker's *code*, not only its build. |
| **D2** | One implementation, or a specification and two? (§4.2) | **ratified** | **One.** Two representations of one declaration drift, and the failure would be a widget hit-tested in one place and drawn in another. A shared corpus makes drift visible, not impossible. |
| **D3** | How much layout, given no box model? (§4.3) | **ratified** | **A single-axis stack, row *or* column**, per-child weight, padding as one number. **Changed by the review**: RV-9 measured a row at 1.77× a column at 320×172 and 1.55× at 1024×600 — a fraction of a frame either way, ~7 ms against ~12 ms on the C5 against a 38 ms budget. The original "column preferred, price the row" was right in kind and wrong in degree. A 2×2 grid is affordable when it comes; deep nesting is not. |
| **D4** | Is the address space the SVG `id`? (§4.4) | **ratified** | **Yes.** Document-unique by the XML specification, and the same rule gBASIC's declarative `gui` ids already follow — so gBASIC's §0.3 tension resolves rather than being traded off. |
| **D5** | Who retains the tree? (§4.5) | **ratified** | **The caller; whisker is pure.** The host already holds the SVG text and the application already holds its state; a retained tree inside whisker would be a third copy that can disagree with both. |

| **D6** | Which hosts get `input`, given that a keyboard is half the byte budget? (`gbasic-gap.md` §3) | **ratified 2026-10-02** | **Only hosts that have an input device.** Matthew: *"input is definitely not going to be on devices that have no input like the ESP32 C5."* So `gbasic-gap.md`'s option 1 holds for the C5 — a panel there displays and commands, and text arrives over the terminal as RV-9 already arranges — and option 2 holds for the P4 and Linux. This is a **hardware** criterion, not a byte one: the measurement (`tools/evidence/keyboard/`) found a keyboard costs half the budget rather than 94 % of it, so bytes stopped being the argument; what remains is that the C5 has nothing to press a key with, and that a key on its panel is about 4 × 4.8 mm against the 9 mm a finger wants. **A whisker binding must therefore report which widgets its host supports** rather than drawing a keyboard that cannot be used. |
| **D7** | What stands in for touch until the P4 arrives? (§2.6) | **ratified 2026-10-02** | **The mouse on Linux.** Matthew: *"even if we cannot get touch to work on this linux computer, the mouse does work."* A click is a single point, which is exactly `hit`'s input and exactly what the panel will deliver (five contacts, whisker uses one), so the whole interaction loop — point, name, band, repaint — is exercisable **now** rather than in October. It does not stand in for the one thing only the panel can answer: touch **slop**, which stays open (§5b), because a mouse has no centroid scatter to measure. |
| **D8** | How does whisker serve a host that cannot afford all of it? (§7) | **ratified 2026-10-03** | **Compile-time profiles, defaulting to everything, refusing rather than substituting.** Matthew: *"I want to ensure that at least a scaled down basic version can run on the C5 but also for gBASIC (and maybe other things) on larger systems."* Four parts, and the first is the one that constrains the rest. **(a) The mechanism is the preprocessor, and it is forced, not chosen:** everything is `static inline`, so whisker already scales down by linkage for every entry point a caller declines to name — `wsk_hit`, `wsk_compose`, `wsk_rows` are simply absent from a binary that does not call them. What cannot be dropped is a branch inside the per-widget loop every caller links, and the construct that *would* make those droppable is a dispatch table, which an RV-9 module may not have (§4.1). **(b) The default is everything and a host opts *down*** — §2.8's rule applied to the build, so nobody pays for the C5's limits unless they are on the C5. **(c) A reduced build is the same renderer**: byte-identical output for the widgets it keeps, gated by diffing the same panel compiled both ways, because this is where one widget library quietly becomes two and D2 would be gone. **(d) It refuses what it lacks** with `WSK_E_KIND` naming the widget, and never substitutes — a field silently drawn as a label is the failure shape §2.3 keeps finding in substrates, and worse here because whisker would be the one doing it. The refusals are also the control on (c). Measured at **668 bytes, 20 %** of a small panel (`tools/evidence/profiles/`). This is the mechanism **D6** already called for: *"a whisker binding must report which widgets its host supports."* |
| **D9** | Can a C5 panel be operated from the board, and does it need a focus model? (roadmap G1) | **ratified 2026-10-03** | **No, and no — the panel is write-only.** Matthew: *"The C5 panel is write only and has no controls on it. I interact via serial console or over ssh for now though it has GPIO pins that I will be attaching to sensors, later."* So §2.6's second row — `focus_next`/`activate` and a focus mark, which **D0** kept *addable rather than built* — is now **never built** rather than deferred, and `hit` is dead weight on that host (it costs nothing, because it drops by linkage: D8). The C5's whole interaction loop is: the console commands, the panel shows. **The GPIO detail is not a reprieve**: sensors are data *in*, not a user acting on a widget, so a sensor-driven C5 panel is a **readout** and the thing it needs is what whisker already has — `rows(tree, ids, n)` and a clipped repaint of the values that moved. It does change which question comes next, though: a readout whose layout is compiled into a module cannot be changed without reflashing, which is the condition `notation-sketch.md` §6 names as the one that would earn that notation its place. |
| **D10** | What happens when a stack's widgets want more than its area? (roadmap §4.3) | **ratified 2026-10-24** | **Refuse it, naming the widget — `WSK_E_FIT`.** Matthew: *"let's go with your recommendation: refuse."* It used to be drawn past the end of the area without a word, and `hit` agreed with the drawing; §5c saw it and recorded it as known rather than changed it. On 2026-10-24 it cost a day: `wslop`'s unsized marks and edge bars were 64 rows in 14- and 4-row areas, and whisker asked RV-9 why its panel drew low (`rv9-answers-2026-10-24.md`). Refusing is the rule whisker already follows twice — a document over budget (`WSK_E_BUDGET`) and a kind a reduced build lacks (D8, `WSK_E_KIND`) — and *clamping* was rejected because shrinking a widget to fit is the silent substitution D8 forbids. It is checked in `wsk_check`, so a single stack and a composite are refused the same way, before a byte is written. Turning it on found three test trees and one module that had been overflowing all along; none of the PPM goldens moved. |

These are outcomes, not recommendations. `docs/design.md` §4 keeps the reasoning
behind each, because a decision whose argument is lost gets reopened — but where
the two disagree, **this file wins**.

Two of the six were *changed* rather than confirmed by the review (D0 and D3),
which is the return on having asked.

**D8 is the first decision that constrains how whisker is *built* rather than
what it does**, and it is the one most likely to be reopened by a widget kind
that has not been invented yet. §7.5 says what would reopen it: a third axis,
at which point the small profile may be better named than enumerated.

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
  **Toolkit superseded 2026-10-22: gBASIC removed GTK and is Qt-only**
  (gBASIC `19a830f`); the rule stands with Qt in GTK's place.
- **2026-09-29 — RV-9 reviews this before a whisker session starts.** Matthew.
  Because every claim about RV-9 here was read rather than run, and three of the
  first ones were wrong (`docs/rv9-review.md` §3).
