# A request to RV-9: wnest with tooltips, and whisker refreshed

**Written 2026-11-15** by the whisker session, for the RV-9 session, at
Matthew's asking. Nothing here is urgent: it waits on the firmware work in
progress, and Matthew flashes when it suits.

## What

1. **Refresh `modules/whisker.h`** from whisker's `src/whisker.h` (commit
   given in the message; RV-9's copy is from `e34723f`, 2026-11-11).
2. **Replace `modules/wnest/wnest.c`** with whisker's `src/module_nest.c`.
3. **Raise wnest's `stack_size`** — see *Stack*, below; this one matters.

## Why

whisker now has tooltips (design §5ac): hold a finger on a button half a
second and a tip appears below it; lift, and the button does **not** click
(the hold was used). Matthew wants to try it on the P4. wnest is the board
program that shows it:

- every press from `/touch` is marked a finger's (`WSK_MOD_TOUCH`);
- time reaches whisker from `env->time_ms()` as ticks (`wsk_ui_tick`) —
  whisker has no clock;
- the module words each tip itself, into a 48-byte buffer on its stack
  (no `.bss`): "Back to the last screen", "Item 3, one of twelve", …

What to see on the board: hold a finger on **Back** for half a second — a
light box with dark letters appears under the toolbar, and stderr says
`tip back: Back to the last screen`; lift — **no** `click back`. A quick
tap still clicks. A finger that slides 16 px or more is a drag, no tip.

## Checked here, not on a board

- `src/run.sh --module`: 995 checks; every module links position
  independent with the double link; `module_nest.c` is **30,996 bytes**
  (28,816 without tips). A first draft routed ticks through `wsk_feed` and
  came to 35,800: inlined where only a finger's events reach it,
  `wsk_feed` compiles to the finger's part of the toolkit, and a tick
  through it brought the keyboard's part back in. Ticks now go to
  `wsk_ui_tick`, which is small.
- The same toolkit code is driven in the suite, at the pixels through
  `raster.c` (`tooltip.ppm`), and by a real finger (CDP touch) in Chrome.

## Stack — please raise it before flashing

whisker's header has grown since `e34723f` (worlds, posters, tooltips and
more; `wsk_tree_t` and `wsk_ui_t` with them). `tools/evidence/stack/run.sh`
in whisker now prints, for `src/module_nest.c`:

    rv9_module_entry  4400 bytes  (entry's own frame 2640)
    + three levels of nesting:  1184
    = whisker's own chain, about 5,584 bytes   (2,288 when wnest was staged)

and drawing to `/w0` runs the rasteriser beneath that — **at least 4,528**
bytes for a whisker document (wflick/wcompose bisections). That is
**more than 10,112**, and wnest declares 8,192. Suggested: **12,288**, then
bisect on the P4 as before. (A world's poster path, `wsk__nat_z`, is in the
counted chain but unreachable in wnest; the figure is an upper bound on
whisker's part, not on the rasteriser's.)

If stack is tight, wnest could be built `-DWSK_PANEL` — it uses nothing the
panel profile drops — which shrinks both the trees and the code; whisker's
suite keeps `module_nest.c` at the default profile only because it is where
the cost of each toolkit switch is measured.

## Other modules

The refreshed header is byte-identical in what it draws for every older
whisker output (42 files compared), and every `module_*.c` in whisker's
`src/` links under the module rules. A module staged from an older whisker
needs nothing changed; its stack figure may have grown in the same way, so
the same tool's line for it is worth a look.
