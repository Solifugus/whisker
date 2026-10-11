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
| **`SRC_MAX` on a 7-inch panel** | measured 2026-10-03: it does **not** bind; §2.3 corrected |
| **Open** | two gates, below, and two decisions that are Matthew's |

Eight iterations' worth of work has landed, and **G1 closed on 2026-10-03
without any** (D9). What is left is **not mostly code** — it is four
measurements only the P4 can give, one host's answer, and two decisions below
that are Matthew's.

---

## 3. The gates — one closed, two open

### G1 — the C5 is served — ✅ **closed 2026-10-03, with no code**

**Answered by the hardware, which is how every good question here has been
answered.** Matthew: *"The C5 panel is write only and has no controls on it. I
interact via serial console or over ssh for now though it has GPIO pins that I
will be attaching to sensors, later."*

So §2.6's second row — `focus_next`/`activate` and a focus mark, which D0 kept
*addable rather than built* — moves from **deferred** to **never**, and is
recorded as **D9**. `hit` is dead weight on the C5 and costs nothing anyway,
because it drops by linkage (D8). The C5's loop is: the console commands, the
panel shows.

**The GPIO pins are not a reprieve**, and it is worth being exact about why.
Sensors are data *in*, not a person acting on a widget. A sensor-driven C5
panel is a **readout**, and what a readout needs is what whisker already has:
`rows(tree, ids, n)` for the values that moved, and one clipped repaint. No
focus, no traversal, no activation.

**But it moves the next question.** A readout whose layout is a C struct
compiled into a module cannot be changed without rebuilding and reflashing —
and *"when a panel wants to live in a file"* is the exact condition
`notation-sketch.md` §6 names as the one that would earn that notation its
place. See §4.2 below. That is the first time one of these has had a
trigger rather than a preference.

### G2 — what the P4 is actually needed for

**Rewritten 2026-10-03, after Matthew asked the right question: *"why do we
need to wait for the P4 before working on this?"* Mostly we did not.** This
gate had four items and they were listed together because they were *about*
the P4, which is not the same as *needing* it. Taken one at a time:

**4. Does a realistic 1024×600 panel fit in `SRC_MAX`? — ✅ answered, no board
required.** §2.3 predicted the ceiling would bind on the P4. It does not: a
settings form is 1,286 bytes of 4,096, a 20-value readout is 1,026, and a
screen *filled* with a column of buttons never binds at all. What binds is
**density** — about forty controls visible at once, which only a grid reaches.
`tools/evidence/p4_panel/`, and §2.3 is corrected. This was always pure
arithmetic on whisker's output; it waited on nothing but being asked.

**1. Finger-down to pixel-changed — ✅ the software half is measured, 2026-10-04.**
`tools/evidence/tap/` times every link from "a point arrives" to "pixels
changed": whisker is **under 1 %** of a press and the window is the other 99 %.
What is left for the P4 is only *sensor → driver*, which is one subtraction
rather than an investigation. The original note follows.

**1a. Why it did not need the board.** The chain is
*sensor → driver → `hit` → `rows` → setstat → render → rasterise → blit*, and
only the first two links are the panel's. Everything from "a point arrives" to
"pixels changed" is measurable on the C5 and on Linux today, with a synthetic
point. Worth doing **before** the board arrives, so that when it does the
remaining subtraction is one number and not a whole investigation — the same
argument §5b used for building `hit` early.

**3. The stack — the P4's needs the P4, but the C5's does not.** `wstat` is
staged with a stack that is *declared, not bisected* (8192, reasoned from an
880-byte chain against `wflick`'s 592). That bisection is available on the
board that is already on the desk, and it is asked for in
`rv9-ask-2026-10-03.md`. Only the P4's own figure — 75 bands against 22, and
pixels in PSRAM — has to wait.

**And one new one that the C5 can answer today:** the legibility floor for
scaled text (`scale-sketch.md` §4 rung 2). `wfont` is staged for it — seven font
sizes on the glass, read from where you would really read it. It needed the
board rather than the P4, and it needed it because a development monitor has
41 % of that panel's pixel pitch and would set the floor two sizes too high.

**2. Touch slop — unblocked 2026-10-11, and the instrument is staged.** The P4
has a working `/touch` and RV-9's `wtap` already closes the loop on glass. The
measurement is `src/module_slop.c`, staged as **`wslop`**: five marks, eight
taps each, reporting **bias** (a constant offset, which is the driver's or the
glass's), **scatter** (the radius a hit test must forgive — whisker's question)
and **drift** between DOWN and UP (which decides whether press and release can
belong to different widgets). In pixels *and* millimetres, via
`RV9_GS_PHYSICAL`, so the answer is portable between panels of different pitch
rather than true of one. **This now needs a finger, not a program.**

**And one thing that is not a measurement:** somebody using a whisker interface
on the P4 for something real. That is half of §6's bar — and the C5 half of it
is already started, see G3.

**The lesson, worth keeping:** ask "does this need hardware" **per item**, not
per gate. Three of these four were filed behind a board because they were
filed behind each other.

### G3 — gBASIC answers

**Partly unblocked, 2026-10-03.** It was listed as waiting on G1 and G2; G1's
answer turned out to *supply* the missing piece rather than gate it, because a
readout is a real interface and the C5 is on the desk. The seam still does not
exist.

`design.md` §6 set the bar before anything was invested. Of its three items, one
is already struck through (a small Linux host — 276 lines, done 2026-10-01). The
other two:

- **whisker runs a real RV-9 interface on real hardware, and somebody uses it**
  — **started 2026-10-03, and it turned out not to need the P4 at all.** G1's
  answer said what a C5 panel is: a readout. `src/module_stat.c` is one — die
  temperature, heap and uptime, repainting only the rows whose value moved —
  staged in RV-9's tree as `modules/wstat/`. It is the first whisker program
  written to be *used* rather than to demonstrate a mechanism. What is left of
  this bar item is the half that cannot be written: flashing it and leaving it
  running, and finding out whether it is worth looking at.
- **The two functions are stable enough that gBASIC's renderer seam can be
  written against them without a version negotiation.** Nothing has moved the
  signatures since 2026-09-30; what has moved is what they *mean* on a reduced
  build (D8), which is exactly the kind of thing a seam has to be written
  against knowingly.

Then gBASIC either writes the binding or keeps GTK. Both end this project's
obligation.

**And there is a named destination beyond it, as of 2026-10-03.** Matthew:
*"eventually I am hoping to build a gbasic library around this and adapt the
gbasic-studio project to it."* `docs/studio-gap.md` measures that distance
rather than arguing it, and the headline is better than anyone had a right to
expect: **324 of Studio's 33,878 lines touch GTK — 0.96 %, in six files — and
none of them use `gtkui`**, the layer gBASIC's own §0.3 called unportable. Its
domain model is already headless; the shell is mounted over it.

The gap is **four capabilities**, all of which `roadmap.md` §5 currently lists
as *not on this map*: a multi-line editor, scrolling, a table, and a tree with
expansion. The byte objection to those is **gone** — §2.3's ceiling does not
bind on a seven-inch panel — but that is not the same as a commitment, and
Studio-on-whisker is not the next thing. See `studio-gap.md` §5 for the
sequencing, of which the first line is that **scrolling is the one to price
first**, because the other three need it. The groundwork is better than it looks: gBASIC's `gui` names five
component kinds and whisker has all five (§7.1), and its addressability
decision was already made *in favour of* the rule whisker uses (D4, and its
§0.3) — so the binding is a translation, not a negotiation.

---

## 4. Two decisions that are Matthew's, not mine

G1 was the third and is now D9. These two block nothing today, but both will be
reopened forever if they are not answered, and **one of them has just acquired
a trigger**.

### 4.1 Is the compact keyboard form built into whisker?

**What it is.** Today whisker draws a keyboard the way it draws everything: one
`<rect>` and one `<text>` per key, because a key *is* a button widget. 36 keys
cost 3,794 bytes at 1024×600. The alternative, measured as form C in
`tools/evidence/keyboard/`, emits all 36 key outlines as **one `<path>` with 36
subpaths** plus one `<text>` per row: **1,967 bytes, pixel-identical** — not
approximately, a difference of zero.

**Why it is not a micro-optimisation.** `SRC_MAX` is 4,096 and does not scale
with the screen (§2.3), so on the P4 the arithmetic is brutal:

| a text-entry panel at 1024×600 | bytes | left for everything else |
|---|---|---|
| a field + 36 keys as widgets — **measured today** | 3,981 | **115** |
| the same with the keys as one path | ~2,150 | ~1,950 |

**115 bytes.** That is a full-screen 7-inch panel with a keyboard, one text
field, and no room for a title, a prompt, a Cancel button, or any of the
interface the person is typing *into*. The keyboard fits; a panel containing a
keyboard does not.

**What it costs.** Three things, and none is free:

1. **One `<path>` has one `fill`.** A *pressed* key cannot live in the shared
   path, so the emitter has to lift it out and draw it separately. That is the
   one piece of real complexity, and it lands in the most-exercised path.
2. **Keys lose their individual SVG `id`s.** `hit` is unaffected — it is
   geometric, computed from the tree, and never reads the document — but D4
   makes the id the address space, so this is the first place whisker would
   emit a widget the *document* cannot name. Worth being deliberate about
   rather than discovering.
3. **It depends on a cap.** 36 subpaths against `MAX_CONTOURS`, which was 16
   until RV-9 raised it to 64 on 2026-10-02 *because this measurement found it
   silently truncating*. It fits now with 28 to spare — and the rule from that
   episode is to **ask** `getstat(RV9_SVG_GS_LIMITS)` rather than hope, so the
   compact form would have to carry that call.

**Where it does and does not matter.** Not on the C5 at all: D6 and D9 mean no
`input` there and no keyboard to draw. It is a **P4-and-Linux** question
entirely.

**Sharpened 2026-10-03 by `tools/evidence/p4_panel/`.** That measurement
asked the question this decision was waiting for, and answered it without a
board: **every realistic panel fits comfortably and the keyboard is the only
one that does not.** A settings form is 1,286 bytes, a readout 1,026, a
launcher 1,728 — and 36 keys with a field is 3,541, with 48 controls in a grid
going over at 5,025.

So the decision is narrower than it looked. This is **not** "whisker's output
is too fat for a seven-inch screen" — it is not. It is "a keyboard is the
densest grid an interface has, and whisker draws grids one control at a time."

**My recommendation: still defer, but the trigger is now specific.** Build a P4
text-entry screen; if the keyboard plus the interface *behind* it needs more
than the ~550 bytes left over, the compact form is how you get it, and nothing
else in the measurement wants it.

### 4.2 Does `tools/notation` ship?

**What it is, and what it is not.** It is **not** a wire format. §1 of
`notation-sketch.md` measured that idea and killed it: a compact replacement
for SVG on the wire buys about a quarter of a full redraw, and a clipped
repaint is ~200 bytes where compression buys nothing anybody can perceive. The
wire stays SVG.

What it *is* is a way for a panel to be **written down and stored** — by a
person with an editor, or by a gBASIC program:

```
.col pad=4 gap=4 fill=#10203a
..lbl title "Settings"
..btn ok h=64 "OK"
..btn cancel h=64 "Cancel"
```

Dots are depth. No closing tags, depth survives whitespace, and — the property
that earns it — **a line is addressable on its own**, located by its dots and
its name without reading the rest of the file.

**What that third property buys is the damage model.** An update is just the
lines that changed, and applying it reports **which nodes actually moved** —
not which were mentioned. Measured: a 347-byte panel, a 63-byte patch naming
three nodes, of which **two** actually moved because the third restated what
was already true. Those ids go straight to `rows(tree, ids, n)`, the rows go to
`RV9_SVG_SS_ROWS`, and whisker writes one clipped document instead of a frame.
**A patch that changes nothing costs no repaint at all.**

It already parses with refusals that each have a control, and it **links as an
RV-9 module** — 2,796 bytes, position-independent, double-linked — so the
question is genuinely "ship it or not" rather than "could it work".

**The trigger, which is new as of today.** Its own §6 says the earliest it
earns a place is *"when a panel wants to live in a file"*, and until this
morning no panel did. But a C5 that is a **write-only readout with sensors on
its GPIO pins** (D9) is a panel whose layout is a C struct compiled into a
module: adding a sensor, renaming a reading or moving a row means rebuilding
and reflashing. A stored panel plus a patch is exactly the shape that work
takes — and the patch's "which nodes actually moved" is exactly the set a
clipped repaint wants.

**My recommendation: not yet, but name the test.** The condition is not "does
it work" — it does — but *does a panel on that board actually want to change
without a rebuild?* That is answerable the first time a sensor readout exists
on the C5 and somebody wants to add a second sensor. If the answer is that
reflashing is fine because the panel changes once a month, it stays unshipped
and §1 of its own sketch is the reason it was never on the wire anyway.

## 5. What is explicitly *not* on this map

Listed so that completion is reachable rather than receding. Each of these was
ruled out with a reason and none should arrive by drift:

- **A grid that computes cells.** D3 allows a 2×2 when it comes; the caller
  places areas today and §5c says that is composition's answer.
- **A focus model and `activate`** — D9, settled rather than deferred: the C5
  panel is write-only and the P4 points. This was the live candidate and it is
  now closed.
- **Nesting a stack in a stack**, scrolling, gestures, multiple contacts,
  retained state, a table, a tree. **Reopened as *questions* rather than
  refusals, 2026-10-03**, by two things arriving together: `studio-gap.md`
  names four of them as what gBASIC Studio would need, and
  `tools/evidence/p4_panel/` removed the byte argument against them. They are
  still not on this map and nothing should be built from this paragraph — but
  the reason they were off it has changed from *"unaffordable"* to *"nobody
  has asked for them yet"*, and one caller has now half-asked.
- **A second renderer, ever** — D2, and now gated byte-for-byte by D8.
- **Anything RV-9's window cannot draw**: font families, rotated text, opacity,
  gradients, CSS. §2.4.

If one of these becomes necessary, it is a new iteration with a measurement
behind it, the way §5c and §5d were chosen — not an addition to this list.

---

## 6. Who owns what

| | ours | RV-9's | gBASIC's |
|---|---|---|---|
| ~~G1~~ | ~~the focus iteration~~ — closed, D9 | answered: a write-only panel | — |
| G2 | the programs that get measured | the board, the driver, `R` | — |
| G3 | a stable seam, and the binding if asked | — | the adoption decision |

Two things are already known to be RV-9's and have been handed over: the
rasteriser's stack depth `R`, which is not a constant and is not `pic`'s 2,080
(`rv9-answers-2026-10-02.md`); and whatever the P4 does to all of it.

---

## 7. The order, and why it is this order

**G1 is closed. Most of G2 turned out not to need the board.** What is left in
order: the software half of the tap measurement (available now), the C5 stack
bisection (RV-9's, available now), then the P4's own four numbers when it
lands, then G3.

G1 was first because it was a *question*, cost nothing to answer, and decided
whether there was one more iteration or none. It was worth asking before
building: the answer was *none*, and a focus model would have been a whole
iteration built for a panel with no controls on it.

G2's remaining P4-only parts cannot start early and should not be rushed when
they do: the P4 is the only
source of four numbers this project has been reasoning *about* rather than
*from*, and §2.2's correction on 2026-09-29 — where three months of quoted
figures turned out to have been measured on a picture that was wrong — is the
standing argument for taking them carefully.

G3 is last because §6 says so, and because the bar it sets is *use*, not
readiness.

**The honest estimate: whisker is closer to done than the open-items list makes
it look.** What is left is one question, one board, and one host's answer.
