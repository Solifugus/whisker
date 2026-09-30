# A notation for a panel — sketch, not a decision

**Written 2026-09-30.** A prototype and the measurements behind it, so that the
question *"should a panel be written in something more compact than XML?"* can
be settled on numbers. Nothing here is ratified and none of it is in
iteration 1. `tools/notation/` holds the code; `./run.sh` reproduces every
figure below.

---

## 1. First, the version of the idea that does not pay

The question that started this was whether `/w0` should receive something
terser than SVG — a compact notation, still visible text, translated to and
from the XML. It was measured before it was argued
(`tools/evidence/` and `review-2026-09-30.md` §4). The same twelve-control
panel, five ways:

| notation | bytes | per control | fits under `SRC_MAX` |
|---|---|---|---|
| SVG, `<g>`-hoisted — what whisker would emit | 1,439 | 109 | 36 |
| named but short, no brackets or quotes | 891 | 69 | 57 |
| positional — `r 8 136 304 40 4 2a6b8f w3` | 558 | 43 | 90 |
| positional on a coarse grid | 515 | 41 | 97 |
| **the data alone** — `btn w3 "Heater"` | **134** | **11** | **332** |

**2.6× for an isomorphic notation, 10.7× for dropping to the data.** The gap
between those two rows is the whole finding: what costs the bytes is the
geometry, and a notation that round-trips with the XML must carry it. Losing
it is not a spelling change, it is a change of layer — and that layer is
whisker's tree.

And the time it would save is not where it looks. Instrumenting the driver on
that same panel:

```
320x172    document 1,439 bytes    attr() scanned 255,552 bytes/frame = 178x the document
1024x600   document 1,453 bytes    attr() scanned 882,525 bytes/frame = 607x the document
```

The document is re-read once per band, and within a band each tag is rescanned
about ten times — once per attribute the driver goes looking for. A frame
divides as:

```
22 bands cleared and blitted, empty document     0.060 ms    8 %
+ one full-screen background rect                0.161 ms   14 %
+ twelve buttons and twelve labels               0.741 ms   78 %
```

and the byte-proportional part of that is about a quarter of the frame:
**2.1× the bytes cost 1.24× the time**, measured by padding every tag with an
attribute nobody reads. So a perfect compact wire format saves perhaps a
quarter of a *full redraw* — and a full redraw is not what a tap costs. A
clipped repaint is two elements and a backing rect, around 200 bytes, where
compression buys nothing anybody can perceive.

**Conclusion: the wire stays SVG.** Ahead of a compact wire format, in order of
what they pay: one pass per tag instead of ten (RV-9's, ~10× on the dominant
term, no format change); parsing once per document instead of once per band
(RV-9's roadmap already names it); the `<g>`-hoisting whisker can do today
(measured 44% more interface, free); and raising `SRC_MAX` on the P4, which is
one `rv9_alloc` against 32 MB of PSRAM.

## 2. The version that does pay, and what it is for

A panel has to be *written* somewhere, by a person with an editor or by a
gBASIC program, and stored somewhere — RV-9 has `/r0` and `/sd0`, and `ed` on
the board. Nothing says that form has to be SVG, and it should not be: it is
the one place the 10.7× row is available, because the tree derives the geometry
rather than storing it.

```
.col pad=4 gap=4 fill=#10203a
..lbl title "Settings"
..btn ok h=64 "OK"
..btn cancel h=64 "Cancel"
```

**Dots are depth.** A section ends where the next one begins. Three things
follow, and the third is why dots rather than indentation:

1. **No closing tags.** `</text>` and its kind carry nothing the next line does
   not already say.
2. **Depth survives whitespace.** Tabs turned to spaces, a line re-wrapped, a
   file edited with `ed` — none of them move a node, because the level is data
   rather than layout.
3. **A line is addressable on its own.** Its dots and its name locate it
   without reading the rest of the file.

## 3. What the third property buys, which is the point

An update is the lines that changed:

```
..btn heater fill=#8fd0ff "Heater ON"
..lbl title "Settings *"
..btn fan h=40 "Fan"
```

Applying it reports **which nodes actually moved** — `title` and `heater`, not
`fan`, because `fan` restated what was already true. Measured:

```
the panel                347 bytes    12 controls, 28 bytes each
the patch, canonical      63 bytes    18 % of the panel
nodes it names             3
nodes it moved             2
```

**That list is the damage model.** The ids go to `rows(tree, ids, n)`, the rows
go to `RV9_SVG_SS_ROWS`, and whisker writes one clipped document instead of a
frame. It is also the argument for the set form of `rows` (`rv9-answers.md`
§3.3) arriving from a direction the design did not anticipate: the natural unit
of change in a stored panel is *several widgets at once*, and a merge hands you
exactly that set, in document order, with the ones that did not really change
already removed. A patch that changes nothing costs no repaint at all.

## 4. What it refuses, and why each refusal is a bug that already happened

The parser is where the boundary belongs, because it is the only place that can
name the line responsible:

| refused | because |
|---|---|
| `<`, `&`, `>` in a label | entities are drawn literally on `/w0`, so text cannot be escaped — only kept clean. A raw `<` hung the driver outright until 2026-09-30 (`review-2026-09-30.md` §3.3). |
| bytes outside 32..126 | they advance a cell and draw nothing |
| two nodes with one name | names are the address space (D4), document-unique by the same rule an SVG `id` obeys |
| a skipped level | `..` under nothing is a typo, not a tree |
| a kind that is not a widget | |
| a patch naming a node that is not there | a mistyped id would otherwise quietly become a new widget |

Each has a test, and the refusals have a **control** — the same shapes, legal,
are accepted — because a parser that refuses everything passes a suite that
only checks refusals.

## 5. It links into a module, which is the part that decides

```
ok    links as an RV-9 module, position independent (2,796 bytes)
      no libc, no .data, no .bss -- the linker script asserts the last two
```

Built with `tools/build_modules.sh`'s flags and `modules/module.ld`, then
linked again at another base and compared byte for byte. The header is
`static inline` throughout, like `modlib.h`; `wsk_kind_name` returns its
literals from code rather than indexing a table, because a `static const char *`
table is addresses in rodata and does not link (§3.1 of the review, and RV-9's
`have` learned it the expensive way). The caller owns the node array and the
text pool, which is what D5 asks for anyway.

2,796 bytes of code, against roughly 39 KB available to a program on the C5.

## 6. What is deliberately not decided

- **Whether whisker ships this at all.** It is not needed by iteration 1, which
  takes a tree in C from a caller that built it. The earliest it earns its
  place is when a panel wants to live in a file.
- **Attribute inheritance.** `h=40` on twelve lines is twelve times the bytes
  of `h=40` on their parent. Inheriting down the tree would take a control from
  28 bytes to about 20, and it is the same mechanism `<g>` already gives the
  SVG side. Not implemented, because it changes what a line means on its own —
  and a line meaning something on its own is the property §3 is built on.
- **Whether a patch may add a node.** Today it may only change one. The level
  on each line is exactly what an insert would need, so the notation is ready
  for it and the code is not.
- **The attribute set.** `pad`, `gap`, `h`, `fill` are enough to prototype and
  are not a proposal.
- **Whether gBASIC would emit this or the tree directly.** It can already write
  deterministic text (`stdlib/chart.bas`), so either is cheap; the question is
  which one `docs/gui_addressability_design.md` wants to address, and that is
  gBASIC's to answer.
