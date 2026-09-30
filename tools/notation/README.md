# The panel notation — a prototype

A panel, written down for a person to read and edit, isomorphic to whisker's
widget tree rather than to the SVG it expands into.

```
.col pad=4 gap=4 fill=#10203a
..lbl title "Settings"
..btn ok h=64 "OK"
..btn cancel h=64 "Cancel"
```

Dots are depth. A section ends where the next one begins, so nothing is
closed; and because the level is *data* rather than indentation, a line is
addressable on its own — which is what makes an update a few lines instead of
a new document, and makes applying one report exactly which widgets moved.

```
./run.sh             round-trip, refusals, merge, and what it costs
./run.sh --module    and whether it links into an RV-9 module
```

`wsk_note.h` is the whole thing: header-only, no libc, no allocation, the
caller owns every byte. `docs/notation-sketch.md` is the argument, the
measurements, and what is deliberately not decided.

**This is not whisker's wire format.** `/w0` receives SVG and nothing here
changes that; `docs/notation-sketch.md` §1 prices the alternative and argues
against it.
