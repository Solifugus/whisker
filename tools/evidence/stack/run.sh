#!/usr/bin/env bash
#
# What stack does a whisker program need, and would buffering help?
#
#   ./run.sh
#
# Needs the ESP-IDF RISC-V toolchain and RV-9's module headers; set TOOLS and
# RV9 if they are not where this expects them. Nothing here needs a board --
# the question is frame sizes and a call graph, and both are in the object
# file. What a board is needed for is said so, below.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RV9="${RV9:-$HOME/development/esp32/rv9}"
SRC="$HERE/../../../src"
cd "$HERE"

TOOLS="${TOOLS:-$HOME/.espressif/tools/riscv32-esp-elf}"
CC9="$(echo "$TOOLS"/*/riscv32-esp-elf/bin/riscv32-esp-elf-gcc | cut -d' ' -f1)"
[[ -x "$CC9" ]] || { echo "no riscv32-esp-elf-gcc under $TOOLS"; exit 1; }
OBJDUMP="${CC9%gcc}objdump"; NM="${CC9%gcc}nm"

W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
FLAGS=(-march=rv32imac_zicsr_zifencei_zaamo_zalrsc -mabi=ilp32 -mcmodel=medany
       -Os -ffreestanding -fno-builtin -fno-jump-tables
       -fno-tree-switch-conversion -ffunction-sections -fdata-sections
       -I"$RV9/components/rv9_module/include" -I"$RV9/modules" -I"$SRC")

# Every indirect call in whisker is one of two pointers: the sink's put(),
# and the body wsk_compose takes. Naming them is what makes the call graph
# complete; a site left unnamed is reported rather than ignored.
SITES=(wsk__w wsk__s wsk__n wsk__col wsk__box wsk__corner wsk__doc wsk__field
       wsk_open wsk_part wsk_shut wsk__emit.constprop.0
       wsk_render.constprop.0 wsk_render_clipped.constprop.0
       wsk_render_clipped.constprop.0.isra.0)
PUT=()
for S in "${SITES[@]}"; do PUT+=("$S=to_path,to_buf"); done
# These call out through env->, which leaves the module: that is where RV-9's
# rasteriser begins, and its depth is not in this object file.
OUT=(rv9_module_entry= to_path= to_buf= flush= report= clip_rows.isra.0=
     m_say.constprop.0.isra.0=)

build () { ( cd "$W" && "$CC9" "${FLAGS[@]}" -w -fstack-usage -c -o "$1.o" "$2" ); }
depth () {
	local n="$1"; shift
	python3 "$HERE/depth.py" "$W/$n.o" "$W/$n.su" "$OBJDUMP" "$NM" \
		rv9_module_entry "${PUT[@]}" "${OUT[@]}" \
		"wsk_compose.constprop.0=comp_body,bar_body" "$@"
}
bytes () { depth "$@" | sed -n 's/.* \([0-9]\+\) bytes.*/\1/p' | head -1; }

echo "== 1. whisker's own frames: the deepest path through each module"
echo
for M in module_shape module_compose module_input; do
	build "$M" "$SRC/$M.c"
	echo "src/$M.c"
	depth "$M"
	echo
done

echo "== 2. the question: streaming or buffering?"
echo
echo "RV-9's session, 2026-10-02: \"a buffering sink would pay the two depths"
echo "separately and could declare a smaller stack. Worth a measurement rather"
echo "than a decision.\" The sink is the only difference between these two"
echo "programs -- counted rather than claimed, comments excluded so that a"
echo "rewritten prologue cannot flatter it:"
echo
D="$(diff -u "$SRC/module_compose.c" "$HERE/buffered.c" || true)"
BODY="$(printf '%s\n' "$D" | grep '^[-+][^-+]' \
	| grep -v '^[-+][[:space:]]*\(/\*\|\*\|//\)' | grep -v '^[-+][[:space:]]*$' || true)"
printf "   %d changed lines of code, in %d place(s)\n" \
	"$(printf '%s\n' "$BODY" | grep -c . || true)" \
	"$(printf '%s\n' "$D" | grep -c '^@@' || true)"
echo
build buffered "$HERE/buffered.c"
S=$(bytes module_compose --must=to_path)
B=$(bytes buffered --must=flush)
echo "   streaming  src/module_compose.c, put() writes as it is produced"
depth module_compose --must=to_path
echo
echo "   buffering  buffered.c, put() copies and the write happens at the top"
depth buffered --must=flush
echo
echo "Call the rasteriser's own depth R. It runs on the writer's stack, so it"
echo "is beneath the module either way and it cancels:"
echo
printf "   streaming   %4d + R bytes of stack,      0 bytes of statics\n" "$S"
printf "   buffering   %4d + R bytes of stack,   4096 bytes of statics\n" "$B"
echo
printf "   Buffering saves %d bytes of stack and spends 4096 of statics.\n" "$((S - B))"
echo "   Both come out of one process footprint -- rv9/module.h: \"its own"
echo "   stack, statics and descriptor\" -- so this is a trade, not a saving,"
printf "   and it is a bad one by %d bytes, whatever R turns out to be.\n" "$((4096 - (S - B)))"
echo
echo "   The saving is small because whisker's whole chain is small. Streaming"
echo "   puts that chain beneath the rasteriser; it does not pay for the"
echo "   rasteriser twice. The 4096 is the document, and it does not shrink."
echo
echo "   Note what the buffer may NOT be: a local array (that is the stack this"
echo "   was meant to save) or .bss (a module has none). env->statics is the"
echo "   only place left, and it is declared in build.conf, which is why the"
echo "   cost is visible rather than discovered by an overflow."

[[ -f "$RV9/modules/wflick/wflick.c" ]] || exit 0
echo
echo "== 3. and what R actually is, which nobody has measured"
echo
echo "RV-9's two whisker programs, built from their tree, bracketed by their"
echo "own board bisections (docs/rv9-answers-2026-10-02.md §2):"
echo
for M in wflick wcompose; do
	build "$M" "$RV9/modules/$M/$M.c"
	printf "modules/%s/%s.c\n" "$M" "$M"
	depth "$M" --must=to_path
	echo
done
WF=$(bytes wflick --must=to_path)
WC=$(bytes wcompose --must=to_path)
cat <<TXT
   board        module chain     so R is
   wflick     5120 overflows,        $WF      > $((5120 - WF))
              6144 runs                       <= $((6144 - WF))
   wcompose   4096 runs              $WC      <= $((4096 - WC))

Two things follow, and both are RV-9's rather than whisker's:

  * R is NOT 2080. That figure is modules/pic's, and it is the one a whisker
    program would reach for, because it is the only one written down. A
    whisker document costs the rasteriser at least $((5120 - WF)) bytes --
    more than twice pic's figure. Declaring a stack from 2080 would be wrong
    in the dangerous direction.
  * R depends on the document, by at least $(( (5120 - WF) - (4096 - WC) )) bytes between these two.
    So there is no single number for "drawing to /w0", and a program cannot
    be given one. What a whisker program can be given is its own chain --
    which is what section 1 prints, and what this tool is for.
TXT
