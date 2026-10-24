# `slop/` — touch scatter on the P4's glass, 2026-10-24

The one directory here that **did** need a board, and so the one that holds a
record rather than a program. The program is `src/module_slop.c`, staged in
RV-9 as `modules/wslop/`; what it printed is here, verbatim from the console.

```
2026-10-24-run1.txt   the first run: summaries only, two marks contaminated
2026-10-24-run3.txt   the run the figures come from: every tap, every mark
```

Run 2 is not kept: the driver never saw a finger in over three minutes, and
whether anybody was tapping is not known, so it is evidence of nothing.

## Conditions

Waveshare ESP32-P4-WIFI6-Touch-LCD-7B, 1024×600, GT911 polled every 16 ms.
Matthew's finger, one person, board on a café table. Five marks of 14 px
(2 mm), eight taps each; 1 px is 0.146 mm by the panel's own
`RV9_GS_PHYSICAL`.

## What run 3 says

| mark | aimed at | bias x,y | scatter rms | scatter worst | drift |
|---|---|---|---|---|---|
| centre | 512,300 | −2, +48 | 9 px (1.3 mm) | 12 px (1.7 mm) | 0 |
| top-left | 28,28 | −4, +47 | 7 px (1.0 mm) | 14 px (2.0 mm) | 0 |
| top-right | 996,28 | −8, +37 | 17 px (2.5 mm) | 26 px (3.8 mm) | 0 |
| bottom-left | 28,572 | −13, +19 | 5 px (0.7 mm) | 12 px (1.7 mm) | 0 |
| bottom-right | 996,572 | 0, +22 | 5 px (0.7 mm) | 11 px (1.6 mm) | 0 |

**Trust the top three for scatter, not the bottom two.** The bottom marks ran
half off the glass (see the bias, below), so the finger aimed at a
centre it could not see and the taps piled against the bottom of the touch
range — y 587 to 597 of 599. That flattens the vertical spread, so their
scatter is an underestimate, not a good corner.

**The bias is void, and the first reading of it was wrong.** It was put to
RV-9 as the panel drawing ~45 rows low (`docs/rv9-ask-2026-10-24.md`). It was
`wslop`: every mark and edge bar was a button with `size = 0`, which in a
column is 64 rows whatever its area says, so each mark was a 12×64 bar whose
middle sat 26 rows below the target, and the 4-row red edge was a centimetre
thick (`docs/rv9-answers-2026-10-24.md`). Fixed in both trees on the same
day. The bias wants a fresh run with the sized marks.

**What that does to the scatter:** it mostly stands, because a constant
offset drops out of a spread. It is the spread of aims at the middle of a
tall bar rather than at a point, and the bottom two marks also ran off the
glass, which is the squeeze described above.
