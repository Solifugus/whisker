# whisker on Linux

A window, a blit, a point. `design.md` §6 sets the bar that a Linux host must
be **small**, because gBASIC already has a toolkit and a whisker that needs
another one has bought it nothing. So this is the measurement rather than the
claim:

| | lines |
|---|---|
| `host.h` — the contract | 62 |
| `host_sdl.c` — all of it | 214 |
| **the host** | **276** |
| `demo.c` — an application that uses it | 117 |
| `selftest.c` — not shipped, just honest | 125 |

```
./run.sh                 320x172 at 3x -- the C5's panel
./run.sh 1024 600 1      the P4's, at actual size
./run.sh --test          the contract, headless, with no person
```

## The pixels are not SDL's

They are drawn by RV-9's `raster.c`, `font.c` and `drv_svgwin.c`, compiled out
of its tree through the forty lines of stubs it already keeps for the purpose.
The same parser, the same 8-row bands, the same clip, the same font — so **what
you see is what the board draws**, and a picture that is wrong here is wrong
there. SDL puts the finished buffer in a window and says where you clicked.

The one departure is scale: a 320×172 panel is a postage stamp on a modern
screen, so the window may be shown at 2× or 3×. That happens when the buffer is
presented, never when it is rasterised, and nearest-neighbour rather than
smooth — a magnified device pixel should look like a device pixel, or the host
flatters the picture the board will really draw. Input is divided back down.

## Four operations, the same four `/w0` offers

```c
wh_open(w, h, scale, title)   /* open /w0 */
wh_clip(y0, y1)               /* RV9_SVG_SS_ROWS, one-shot */
wh_write(bytes, n)            /* write SVG; "</svg>" draws it */
wh_poll(&event)               /* a point, press or release */
```

No widget lives on this side of the seam, and that is the whole discipline. The
moment this file needs a button of its own, it has stopped being a host.

## What the self test checks

Headless, through SDL's dummy driver, with events pushed onto the queue instead
of pressed with a finger. The one that matters: **a clipped write touches only
the rows it was given** — 19,968 pixels changed, every one inside rows 32..96,
none outside. A host that helpfully redrew the window on every write would pass
every other check here and quietly cost the entire band budget, which is the
thing the whole design is built on.

The rest: the screen reaches the framebuffer, a press comes back divided down
from the window to device pixels, whisker names the widget under it, and — the
control — a press in a gap names nothing.

## One thing SDL does that will catch somebody

`wh_poll` returning `WH_NOTHING` means nothing is ready *now*, not that nothing
is queued. SDL bounds how many events a single poll returns, so an event can
arrive on the next call instead of this one. A loop that keeps polling sees
everything in order; one that polls once and then sleeps can lag a cycle. Found
by testing it, not by reading about it.
