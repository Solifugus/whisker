#!/usr/bin/env python3
# The default palette, held to two rules (decisions.md D24):
#   - every fill whisker draws text on gives white ink 4.5:1 or more (WCAG AA);
#   - every pair of colours that tells one state from another stays 10 delta-E
#     apart (CIE76, Lab) for normal vision and under protanopia, deuteranopia
#     and tritanopia, simulated at full severity (Machado, Oliveira and
#     Fernandes 2009).
# The colours are read from wsk_init in whisker.h, so a changed default is held
# too. Prints "ok"/"FAIL" lines for run.sh; exits 1 on any FAIL.
#
#   python3 audit.py [path/to/whisker.h] [name=rrggbb ...]   the second form tries values
import re, sys
args = sys.argv[1:]
hdr = args.pop(0) if args and not "=" in args[0] else "whisker.h"
init = open(hdr).read().split("static inline void wsk_init(", 1)[1].split("\n}", 1)[0]
P = {k: int(v, 16) for k, v in re.findall(r"t->(bg|ink|accent|trough|selfill) = 0x([0-9a-fA-F]{6})u", init)}
assert len(P) == 5, f"wsk_init's colours not all found: {P}"
for kv in args:
    k, v = kv.split("="); P[k] = int(v, 16)
def rgb(c): return [((c >> s) & 255) / 255 for s in (16, 8, 0)]
def lin(v): return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
def unlin(v):
    v = min(max(v, 0), 1)
    return 12.92 * v if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055
def lum(c): r, g, b = [lin(x) for x in rgb(c)]; return 0.2126 * r + 0.7152 * g + 0.0722 * b
def contrast(a, b):
    la, lb = sorted((lum(a), lum(b)), reverse=True); return (la + 0.05) / (lb + 0.05)
M = {"normal": [[1,0,0],[0,1,0],[0,0,1]],
     "protan": [[0.152286,1.052583,-0.204868],[0.114503,0.786281,0.099216],[-0.003882,-0.048116,1.051998]],
     "deutan": [[0.367322,0.860646,-0.227968],[0.280085,0.672501,0.047413],[-0.011820,0.042940,0.968881]],
     "tritan": [[1.255528,-0.076749,-0.178779],[-0.078411,0.930809,0.147602],[0.004733,0.691367,0.303900]]}
def sim(c, m):
    l = [lin(x) for x in rgb(c)]
    o = [sum(m[i][j] * l[j] for j in range(3)) for i in range(3)]
    return [unlin(x) for x in o]
def lab(srgb):
    r, g, b = [lin(x) for x in srgb]
    X = (0.4124*r + 0.3576*g + 0.1805*b) / 0.95047; Y = 0.2126*r + 0.7152*g + 0.0722*b; Z = (0.0193*r + 0.1192*g + 0.9505*b) / 1.08883
    f = lambda t: t ** (1/3) if t > 0.008856 else 7.787 * t + 16/116
    return 116*f(Y) - 16, 500*(f(X) - f(Y)), 200*(f(Y) - f(Z))
def dE(a, b, m):
    A, B = lab(sim(a, m)), lab(sim(b, m)); return sum((x - y) ** 2 for x, y in zip(A, B)) ** 0.5
fails = 0
def ok(c, what):
    global fails
    print(("ok    " if c else "FAIL  ") + what)
    if not c: fails += 1
for f in ("bg", "accent", "selfill", "trough"):
    c = contrast(P["ink"], P[f])
    ok(c >= 4.5, f"palette: white ink on {f} (#{P[f]:06x}) {c:.2f}:1, at least 4.5")
for a, b in [("selfill","accent"),("selfill","bg"),("accent","bg"),("trough","bg"),("selfill","trough"),("accent","trough")]:
    worst = min((dE(P[a], P[b], M[m]), m) for m in M)
    ok(worst[0] >= 10, f"palette: {a} and {b} {worst[0]:.1f} delta-E apart at the least ({worst[1]}), at least 10")
sys.exit(1 if fails else 0)
