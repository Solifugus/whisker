# A request to RV-9: decode three entities in SVG text

**Written 2026-11-14** by the whisker session, for the RV-9 session, at
Matthew's asking that requests go through it. One small parser change.

## What

In `components/rv9_io/src/drv_svgwin.c`, where a `<text>` element's content
is read, decode **`&lt;` → `<`, `&gt;` → `>`, `&amp;` → `&`** before the
characters reach `font.c`. Nothing else: no numeric references, no other
names. An unknown `&...;` can stay as it draws today (its characters).

## Why

whisker now has a code editor (`WSK_SOURCE`, whisker design §5v), and code
is full of `<` and `>` — `if i < 10`, `a & b`. Today `/w0` draws `&lt;` as
four characters (measured 2026-09-30, whisker design §2.4), and a *raw* `<`
in text once hung the driver (since fixed). So whisker refuses those three
characters for any host that has not said it decodes entities, and the
desktop window — which compiles `drv_svgwin.c` — refuses gBASIC's own
sample file. The browser host decodes them and shows it fine.

With the change, a host built on RV-9's parser says `WSK_CAP_ENTITY`, and
the same files open on the SDL window — and on the C5 and the P4 once
whisker drops `WSK_NO_ENTITY` from its two board tiers, which carry no
escaping today because `/w0` could not use it. whisker's side is
already built and tested: it escapes the three characters when, and only
when, the host has said it decodes them, so **nothing RV-9 draws today
changes** — no document whisker sends to a `/w0` that has not been updated
contains an `&`.

## How to check it

`tools/hosttest/run.sh` with a document containing
`<text x="10" y="20">a &lt; b &amp; c &gt; d</text>`: the picture should
read `a < b & c > d`. whisker's `src/run.sh --module` will then be run with
the SDL host claiming the bit, and its code editor demo opened on
`host/linux/sample.bas`.
