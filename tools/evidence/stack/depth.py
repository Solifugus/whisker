#!/usr/bin/env python3
"""
The deepest stack path through an RV-9 module, from -fstack-usage frames and
the disassembly's own call edges.

Why not just read the .su column: the deepest frame is not the deepest path,
and on this target the difference is most of the answer. -fstack-usage says
wcompose's deepest frame is 528 bytes; the deepest path is 832, and the
stack it must declare is that plus RV-9's rasteriser, which runs on whoever
wrote the document.

Three things this gets right that summing the column by eye does not:

  * Function starts come from the symbol table, so a local branch label is
    never mistaken for a callee. -Os emits plenty of `<.L42>:` headings.
  * A TAIL call (auipc + jr, which -Os also emits constantly) REPLACES the
    caller's frame rather than nesting under it, so those depths do not add.
    Treating them as calls overstates whisker's chain by about a third.
  * An indirect call (jalr through anything but ra) becomes an edge to every
    candidate named for that site. A function pointer is exactly what static
    analysis cannot resolve -- and in whisker's case there are only two of
    them, the sink's put() and the body wsk_compose takes, so naming the
    candidates is honest rather than a guess.

    usage: depth.py OBJ SU OBJDUMP NM ROOT [site=a,b ...] [--must=NAME]

`--must` counts only paths that reach NAME, which is how "how deep is the
stack when write() is called" gets asked separately from "how deep does this
module get". Those are different numbers and only the first one adds to the
rasteriser's.
"""
import re, subprocess, sys, collections

args = [a for a in sys.argv[1:] if a and not a.startswith("--")]
opts = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--"))
obj, su, objdump, nm, root = args[:5]
sites = {}
for a in args[5:]:
	k, v = a.split("=", 1)
	sites[k] = [x for x in v.split(",") if x]
must = opts.get("must")

frame = {}
for line in open(su):
	f = line.rstrip("\n").split("\t")
	if len(f) >= 2:
		frame[f[0].rsplit(":", 1)[-1]] = int(f[1])

funcs = set()
for line in subprocess.run([nm, obj], capture_output=True, text=True).stdout.splitlines():
	p = line.split()
	if len(p) == 3 and p[1] in "TtWw":
		funcs.add(p[2])

lines = subprocess.run([objdump, "-dr", obj], capture_output=True,
                       text=True).stdout.splitlines()
INSN  = re.compile(r"^\s+[0-9a-f]+:\t")
RELOC = re.compile(r"^\s+[0-9a-f]+: R_RISCV_(?:CALL|CALL_PLT|JAL)\s+(\S+)")

def links(i):
	"""Does the first instruction at or after line i link, or tail-jump?"""
	for j in range(i, min(i + 6, len(lines))):
		if INSN.match(lines[j]):
			if re.search(r"\tjalr\t", lines[j]): return True
			if re.search(r"\tjr?\t", lines[j]):  return False
	return True

call  = collections.defaultdict(set)
tail  = collections.defaultdict(set)
ind   = collections.defaultdict(int)
unres = set()
cur = None
for i, line in enumerate(lines):
	m = re.match(r"^[0-9a-f]+ <(.+)>:", line)
	if m:
		if m.group(1) in funcs: cur = m.group(1)
		continue
	if cur is None:
		continue
	m = RELOC.match(line)
	if m:
		(call if links(i + 1) else tail)[cur].add(m.group(1).split("+")[0])
		continue
	if not INSN.match(line) or RELOC.match(lines[i - 1] if i else ""):
		continue
	direct_tail = re.search(r"\tjr\t(?!ra\b)", line)
	if re.search(r"\tjalr\t(?:ra,\s*)?(?!ra\b)[a-z]", line) or direct_tail:
		ind[cur] += 1
		who = sites.get(cur)
		if who is None:
			unres.add(cur)
		else:
			(tail if direct_tail else call)[cur] |= set(who)

known = lambda g: g in frame or g in call or g in tail
memo = {}
def deepest(f, seen=()):
	"""(bytes, path, reaches_must) for the deepest path from f."""
	if f in seen: return 0, ["%s:cycle" % f], False
	if f in memo: return memo[f]
	fr  = frame.get(f, 0)
	hit = (f == must)
	best, path, got = fr, ["%s(%d)" % (f, fr)], hit
	for nests, edges in ((True, call.get(f, ())), (False, tail.get(f, ()))):
		for g in sorted(edges):
			if not known(g): continue
			c, p, h = deepest(g, seen + (f,))
			tot = fr + c if nests else max(fr, c)
			h = h or hit
			# a path that reaches `must` beats a deeper one that does not
			if (h, tot) > (got, best):
				best, path, got = tot, ["%s(%d)%s" % (f, fr, "" if nests else "~")] + p, h
	memo[f] = (best, path, got)
	return memo[f]

n, p, got = deepest(root)
if must and not got:
	print("  %-28s no path reaches %s" % (root, must)); sys.exit(0)
print("  %-28s %4d bytes%s" % (root, n, " to " + must if must else ""))
print("      " + " -> ".join(p))
print("      ~ = tail call: the frame is replaced, not nested")
for f in sorted(unres):
	print("      UNRESOLVED indirect call in %s -- name its candidates" % f)
