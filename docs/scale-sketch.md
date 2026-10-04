# Scale — sketch, not a decision

**Written 2026-10-03**, from a conversation with Matthew, and written down
because this project's discipline is that a design is argued on paper before it
is built. **Nothing here is ratified and none of it exists in the code.**
`notation-sketch.md` is the precedent for that status.

It supersedes nothing. D8 (profiles) stands; §7 is still true. What this adds is
the thing D8 turned out *not* to be.

---

## 1. What "scale" means, in Matthew's words

> *"I would like whisker to be able to utilize what its environment has to offer
> from the very small display only to a highly interactive desktop system. We do
> not need to look and feel conventional but we do need to look and feel
> intuitive, effective, and efficient."*

| surface | what the interface **is** there |
|---|---|
| **ESP32-C5** | a monitoring display |
| **ESP32-P4** | monitoring **and control** |
| **Linux / gBASIC** | a full high-intensity interactive human-machine work interface |

— *"and many variations of all these things and things I am not even thinking
about now."*

**This is not D8.** D8 is subtraction: compile the field out, save 364 bytes.
This is the opposite direction — **use what is there**. The same declared
interface should exploit a desktop and still be the right thing on a panel with
no input at all. Neither end is the "real" one being cut down or padded out.

**And neither target is abandoned.** The C5 is not a degraded desktop; a
monitoring display is a legitimate first-class product of this toolkit.

## 2. The division of labour

Settled in conversation, and the reason the rest of this is coherent:

> **The application informs; whisker decides; the author chooses the policy.**

- The **application** knows things whisker cannot: which reading is alarming,
  which two are read together, what must never be hidden. D5 says whisker holds
  no state, so it cannot discover any of this.
- **whisker** knows things the application should not have to work out: what
  fits, in which bands, at what size, what to repaint, what to rotate.
- The **author** picks between policies where more than one is defensible —
  *"I believe in options."*

### The guard, which matters more than the vocabulary

Options are how a toolkit becomes the thing you were disappointed by. So:

1. **Options with sane defaults** — Matthew's phrase, and in this project
   "sane" can usually be stronger than that: **measured**. Precedent exists —
   a button is 64 rows because `tools/evidence/finger_size/` measured 9 mm as
   comfortable, and `size` overrides it when your case differs.

   Where a default *cannot* be measured it is taste, and **it must be labelled
   as taste** so a later reader knows which kind it is. A default nobody can
   justify is the thing that quietly becomes a constant nobody dares change —
   `MAX_CONTOURS` was 16 for exactly that reason, and it cost a keyboard.
2. **whisker must do something sensible when told nothing at all.** That is a
   testable property, and it gets the control this project's evidence standard
   demands: the same panel, no hints, must still render.

## 3. Layout: containers, and the one piece that is nearly free

Matthew's proposal: vertical and horizontal containers, with flex-like
distribution — space-between, space-around, left/top, right/bottom, center.

Most of it exists. `WSK_COL`/`WSK_ROW` are the containers, per-child `weight`
is flex-grow, `gap` is the gap, and the cross axis centres.

**What is missing is justify, and it is cheap.** `wsk_rect` already computes
`extra = span - used`. Today it distributes that by weight and, **if no child
has weight, throws it away** — everything packs to the start. The five
distributions are arithmetic on a number already in hand: no new widget kinds,
no new elements, **zero extra bytes in the emitted document**.

**Nesting is the structural change**, and the objection to it has expired.
D3 refused deep nesting on cost. But nesting adds **no document bytes** — the
emitted SVG is the same flat list of rects and texts whatever computed it — and
`tools/evidence/p4_panel/` showed bytes do not bind at 1024×600 anyway. Its real
costs are code size, which is what D8 is for, and **stack depth**, which matters
because drawing runs on the writer's stack (§5) and which
`tools/evidence/stack/` can measure. **Price it; expect it to pass.**

## 4. Overflow: what to do when it does not fit

Today whisker does **none** of the below: `extra` clamps at zero, children keep
their natural size, and the surplus runs off the edge and is clipped. That is
recorded as iteration-1 behaviour in §5c and it is a gap, not a choice.

The proposal is a ladder, with the **host's capabilities** choosing the last
rung — which is §1's principle applied to overflow:

1. **Compress.** Squeeze padding and gaps first. Costs no legibility, so it is
   free and comes before anything else.
2. **Scale** — down to a floor that is **measured, not aesthetic**: 9 mm
   targets (`finger_size/`) and a legible glyph. Past that the interface is
   *present but unusable*, which is exactly what a 36-key keyboard on the C5
   already is at 4 × 4.8 mm per key.
3. **Then the environment decides:**
   - **input available → scroll.** Up/down and left/right.
   - **no input → promote and rotate** (§5).
   - **neither acceptable → refuse, naming the widget that did not fit**,
     rather than hiding it silently. Same instinct as refusing an over-budget
     document instead of truncating it (§2.3).

## 5. The display-only case: attention, not just pagination

A monitor with no input cannot scroll, and scaling has a floor, so the
remaining resource is **time**: show subsets in rotation until all have been
seen.

**Plain rotation has three faults, and they are worth stating because two of
them are dangerous:**

- **It destroys `wstat`'s property.** A tick where nothing changed currently
  writes *nothing*. A rotation writes on every page turn regardless, turning an
  idle display into a permanently busy one. **The fix is to diff the next page
  against the current one** and repaint only the rows that differ — the same
  damage computation `wstat` already does on values. Rotate to a page sharing
  six of eight rows and you pay for two rows, not a frame.
- **You lose correlation.** The thing a monitor is for is often *two values at
  once* — the temperature spiked when the pressure dropped. On different pages
  that is never seen.
- **An alarm can be off-screen**, for as long as the cycle takes. On a robot
  that is the failure that matters.

**So the better model is attention, not pagination.** When space is short the
question is not only *how do I show everything* but *what deserves the space*:
whatever is out of range, whatever just changed, whatever must never hide. The
rest rotates through what is left.

That is the part a conventional toolkit will not do, because conventional
toolkits assume somebody is there to scroll. It is also nearly free here —
whisker already computes which values changed.

## 6. What the application tells whisker

A first cut at the vocabulary, which is where "options" either becomes useful
or becomes sprawl. Each is a **fact the application has and whisker cannot**:

| hint | what it is for |
|---|---|
| **attention** | routine / changed / alarming — which widgets claim the space now |
| **priority** | never hide · prefer to keep · droppable |
| **affinity** | *"these are read together"* — whisker must never split them across a page. **This is what answers §5's correlation fault** |
| **volatility** | changes often / rarely — what is worth a band of its own |

And what the **author** chooses, rather than the program:

- the overflow policy for a container (scroll · rotate · refuse)
- justify and align
- whether sizes are pixels or physical (§7)

## 7. Physical units — unblocked, 2026-10-03

**RV-9 is making screen drivers report pixel *and* physical size**, where the
physical size is hard set. That was the one thing in this sketch blocked on
somebody else, and it is no longer blocked.

It does not, however, mean "declare everything in millimetres and one
description fits all". Worked through with both panels' real geometry
(`tools/evidence/finger_size/` has the PPI arithmetic; the viewing distances
below are **assumptions**, marked as such, not measurements):

| | ESP32-C5 | ESP32-P4 |
|---|---|---|
| panel | 320×172 px, 247 PPI, **33 × 18 mm** | 1024×600 px, 170 PPI, **153 × 90 mm** |
| a 64 px button | 6.6 mm — 37 % of panel height | 9.6 mm — 11 % |
| **a 9 mm touch target** | **88 px — 51 % of the panel** | 60 px — 10 % |
| a 24 px font cell | 2.5 mm ≈ 21′ at 400 mm | 3.6 mm ≈ 35′ at 350 mm |

**Two things fall out, and the first is the useful one.**

**The right physical constant depends on what the surface is *for*.** A
finger-sized target on the C5 is **half the screen**. That is not a layout to
be tuned, it is a category error: the C5 has no touch (D9), so finger-sizing is
meaningless there *and* absurd. What a monitoring display needs is a **legible
glyph**, not a touchable target. So:

- a **touch** surface sizes controls by the finger — 9 mm, measured
- a **display-only** surface sizes text by legibility, and the control size is
  whatever reads well

Which means physical units do not collapse the three surfaces into one
constant. They let each surface be sized by **the constraint that actually
binds it** — and that is the same shape as the rest of this document.

**And a font declared in pixels is not the same font.** The identical 24 px
cell is ≈21′ on the C5 and ≈35′ on the P4 — 1.7× apparent size. If "one
description, every surface" is to be true for *text*, size wants to be angular,
which needs a viewing distance. whisker cannot know that; the **author** can
say it, or the host can declare it as a property of the installation. §2's
division of labour again. (`design.md` already reasons this way in one place —
the corner-mark table quotes 9.4′ and 13.7′ — so the arithmetic has precedent
here, it has just never been computable before.)

### What whisker would want from that getstat

Offered now, while it is cheap to get right and there is no caller yet — which
is exactly the position the `SRC_MAX` and `MAX_CONTOURS` limits getstat was in:

1. **Both dimensions**, not a diagonal. A diagonal plus an aspect ratio is a
   reconstruction, and it assumes square pixels.
2. **A distinct "unknown"**, because the size is only hard set *sometimes*. It
   must be tellable apart from a real zero so whisker can fall back and say so
   rather than compute nonsense from it. This is `RV9_GS_SIZE` not being able
   to answer *"is this a window"* over again, and it cost three modules a
   silent bug.
3. **Integers, in micrometres**, since a module has no floats and no 64-bit
   division. `uint32` micrometres reaches 4.3 m.
4. **In the same record as the pixel size**, so a caller gets a consistent pair
   from one call — the argument `RV9_SVG_GS_LIMITS` already settled by
   reporting all four ceilings together.

**And whisker must work when it is absent**, which it will be on some hosts
forever. A size in millimetres on a device that cannot say how big it is should
fall back to a declared nominal and **report that it did**, not guess quietly.

## 8. What this is not

Not a plan, not a commitment, and **not next**. §6's bar is *use*, and the
first real use (`wstat`) is one day old. Each item here gets priced before it
is built, the way §5c and §5d were chosen by measurement — and the cheap ones
(justify) should not be smuggled in beside the expensive ones (a table, an
editor) just because they were written down on the same afternoon.
