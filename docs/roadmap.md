# whisker — the map to completion

Written 2026-10-03, at Matthew's asking, after D8. The other documents say what
whisker *is* and why each piece is the way it is. This one says what is left,
in what order, and what would let us stop.

---

## 1. What "complete" means here, since there is no feature list

whisker is a layer that exists to serve two hosts and belongs to neither. It is
not complete when some set of widgets is built — it has every widget either
host has asked for. **It is complete when each of its three surfaces has either
a working interface somebody uses, or a recorded decision that it will not get
one, and nothing is left half-built between them.**

That definition matters because the alternative recedes. A widget library is
never finished; a *layer with two named consumers* is finished when both have
answered.

`design.md` §6 already set the bar for one of the three and said the quiet part:
*"the reason for a separate project is to make **not** adopting it cheap."* A
decline is a finish, not a failure. This map treats it that way throughout.

---

## 2. Where it stands, 2026-10-03

| | state |
|---|---|
| **The renderer** (§5) | built; 135 checks; pixels through RV-9's own rasteriser |
| **`hit`** (§5b) | built, ahead of hardware |
| **Composition** (§5c) | built; chosen by a measurement |
| **`input`** (§5d) | built; the last widget with a caller |
| **Profiles** (§7, D8) | built; the C5 pays for nothing it cannot use |
| **On the board** | `wflick` and `wcompose` run on the C5, committed in RV-9 |
| **On a desk** | `host/linux`, typing loop closed with a mouse (D7) |
| **Decisions** | D0–D8 ratified |
| **Open** | three gates, below, and three decisions that are Matthew's |

Eight iterations' worth of work has landed. What is left is **not mostly
code** — it is two measurements only hardware can give, one decision about what
the C5 is for, and one host's answer.

---

## 3. The three gates

### G1 — the C5 is served, and "served" is *decided*

**Blocked on a decision, not on hardware. This is the one in front.**

A whisker panel on the C5 displays today, and §7 means it pays nothing for the
field and the marks it cannot use. What is unresolved is whether it can be
**operated from the board**.

`hit(tree, x, y)` assumes a pointing device. The C5 has none and probably never
will. §2.6's own table says a host with `/gpio` buttons or a rotary encoder
needs a different second function — `focus_next(tree, from)` and
`activate(tree, focused)`, plus a visible focus mark — and **D0 deliberately
left that addable rather than built**, because declaration order is already a
traversal order.

RV-9's position (2026-10-02) is that *"text entry on the C5 is over the terminal
from a machine that has a keyboard, and a whisker panel there displays and
commands."* That sentence can be read two ways and the difference is an
iteration:

- **If the C5 panel is a display**, driven from the terminal, then G1 **closes
  with no code at all** — §7's minimal profile is the whole C5 story, and the
  answer goes in the ledger so nobody reopens it.
- **If a person should be able to press a `/gpio` button and act on the panel**,
  then there is one more iteration: focus traversal, `activate`, and a focus
  mark — which is §2.6's row 2, the half that was consciously not built.

**Nothing else on this map depends on which way this goes**, which is why it is
first: it is cheap to answer and it sizes the remaining work.

Note the shape of that second option if it happens. It is **not** a new widget
kind, so D8's profile question does not arise; it is a third pure function over
the same tree, and it would need its own test suite with the control the others
have — a traversal that visits every widget exactly once and refuses to claim
one that is not there.

### G2 — the P4 measures what only hardware can

**Blocked on the board, which was ordered 2026-09-27 and is due this month.**

Four things have been deferred to it by name, and they are deferred for the
right reason rather than out of reluctance:

1. **Finger-down to pixel-changed.** §2.2 is blunt that *"no tap has ever been
   measured end to end"* — `flick` times three *draws*. This is the number the
   whole latency budget is reasoning about and nobody has it.
2. **Touch slop** (§5b). Left open deliberately: whether a tap two pixels into a
   gap should resolve to the neighbour is a question about centroid scatter on a
   real capacitive surface, and a mouse has none to measure (D7).
3. **The stack, bisected on the P4** (§5). 75 bands against 22, and RV-9 owns
   the pixels in PSRAM rather than the panel holding them. §5 says explicitly:
   do not carry the C5's number over.
4. **A panel at 1024×600 where `SRC_MAX` actually binds.** §2.3's inversion —
   4 KB is generous on the C5 and is *the* constraint on the P4 — has never been
   met on glass. This is the one most likely to produce a surprise, because it
   is the only axis on which the big board is the harder one.

**And one thing that is not a measurement:** somebody using a whisker interface
on the P4 for something real. That is half of §6's bar and it cannot be faked.

### G3 — gBASIC answers

**Blocked on G1 and G2, and on a seam that does not exist yet.**

`design.md` §6 set the bar before anything was invested. Of its three items, one
is already struck through (a small Linux host — 276 lines, done 2026-10-01). The
other two:

- **whisker runs a real RV-9 interface on real hardware, and somebody uses it**
  — needs G1's answer and G2's board.
- **The two functions are stable enough that gBASIC's renderer seam can be
  written against them without a version negotiation.** Nothing has moved the
  signatures since 2026-09-30; what has moved is what they *mean* on a reduced
  build (D8), which is exactly the kind of thing a seam has to be written
  against knowingly.

Then gBASIC either writes the binding or keeps GTK. Both end this project's
obligation. The groundwork is better than it looks: gBASIC's `gui` names five
component kinds and whisker has all five (§7.1), and its addressability
decision was already made *in favour of* the rule whisker uses (D4, and its
§0.3) — so the binding is a translation, not a negotiation.

---

## 4. Three decisions that are Matthew's, not mine

These block nothing but will be reopened forever if they are not answered.

1. **Is the C5 panel operable from the board?** — G1. The only one that changes
   how much work is left.
2. **Is the compact keyboard form built into whisker?** `tools/evidence/keyboard/`
   prices it: one `<path>` for 36 keys is 1,879 bytes against the widget form's
   3,721, pixel-identical, and it now fits the cap RV-9 raised. It only matters
   on hosts that have `input` at all, so it is a P4-and-Linux question, and the
   honest framing is that it buys headroom rather than fixing a problem — the
   keyboard already fits.
3. **Does `tools/notation` ship?** `notation-sketch.md` §6 says the earliest it
   earns a place is *"when a panel wants to live in a file"*, and no panel has
   yet. The cheapest honest answer may be "not yet, and here is the test that
   would tell us" rather than yes or no.

---

## 5. What is explicitly *not* on this map

Listed so that completion is reachable rather than receding. Each of these was
ruled out with a reason and none should arrive by drift:

- **A grid that computes cells.** D3 allows a 2×2 when it comes; the caller
  places areas today and §5c says that is composition's answer.
- **Nesting a stack in a stack**, scrolling, gestures, multiple contacts,
  retained state, a focus model *if G1 says no*.
- **A second renderer, ever** — D2, and now gated byte-for-byte by D8.
- **Anything RV-9's window cannot draw**: font families, rotated text, opacity,
  gradients, CSS. §2.4.

If one of these becomes necessary, it is a new iteration with a measurement
behind it, the way §5c and §5d were chosen — not an addition to this list.

---

## 6. Who owns what

| | ours | RV-9's | gBASIC's |
|---|---|---|---|
| G1 | the focus iteration, if it happens | what the C5 panel is *for* | — |
| G2 | the programs that get measured | the board, the driver, `R` | — |
| G3 | a stable seam, and the binding if asked | — | the adoption decision |

Two things are already known to be RV-9's and have been handed over: the
rasteriser's stack depth `R`, which is not a constant and is not `pic`'s 2,080
(`rv9-answers-2026-10-02.md`); and whatever the P4 does to all of it.

---

## 7. The order, and why it is this order

**G1, then G2, then G3** — but only G2 and G3 are genuinely sequential.

G1 is first because it is a *question*, costs nothing to answer, and decides
whether there is one more iteration or none. Asking it late would mean either
building a focus model nobody wanted or discovering at G3 that the C5 was never
really served.

G2 cannot start early and should not be rushed when it does: the P4 is the only
source of four numbers this project has been reasoning *about* rather than
*from*, and §2.2's correction on 2026-09-29 — where three months of quoted
figures turned out to have been measured on a picture that was wrong — is the
standing argument for taking them carefully.

G3 is last because §6 says so, and because the bar it sets is *use*, not
readiness.

**The honest estimate: whisker is closer to done than the open-items list makes
it look.** What is left is one question, one board, and one host's answer.
