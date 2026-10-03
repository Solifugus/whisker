#!/usr/bin/env bash
#
# What does a host pay for a widget it never uses?
#
#   ./run.sh
#
# Needs the ESP-IDF RISC-V toolchain and RV-9's module headers, because the
# question is bytes of RISC-V and the answer differs on a host compiler. No
# board.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RV9="${RV9:-$HOME/development/esp32/rv9}"
SRC="$HERE/../../../src"
cd "$HERE"

TOOLS="${TOOLS:-$HOME/.espressif/tools/riscv32-esp-elf}"
CC9="$(echo "$TOOLS"/*/riscv32-esp-elf/bin/riscv32-esp-elf-gcc | cut -d' ' -f1)"
[[ -x "$CC9" ]] || { echo "no riscv32-esp-elf-gcc under $TOOLS"; exit 1; }
OBJCOPY="${CC9%gcc}objcopy"; NM="${CC9%gcc}nm"

W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
# -I"$SRC" FIRST: RV-9 vendors its own modules/whisker.h, and a measurement
# that silently used that copy instead of this tree's would be worthless.
FLAGS=(-I"$SRC" -march=rv32imac_zicsr_zifencei_zaamo_zalrsc -mabi=ilp32
       -mcmodel=medany -Os -ffreestanding -fno-builtin -fno-jump-tables
       -fno-tree-switch-conversion -ffunction-sections -fdata-sections
       -Wall -Wextra -Werror
       -I"$RV9/components/rv9_module/include" -I"$RV9/modules")
LINK=(-nostdlib -nostartfiles -T"$RV9/modules/module.ld" -Wl,--gc-sections)

build () {  # build <name> [-Dmacro...] -> prints bytes
	"$CC9" "${FLAGS[@]}" "${@:2}" "${LINK[@]}" -o "$W/$1.elf" "$HERE/panel.c"
	"$OBJCOPY" -O binary "$W/$1.elf" "$W/$1.bin"
	stat -c%s "$W/$1.bin"
}

echo "== 1. what a label-and-two-buttons program carries that it never names"
echo
FULL=$(build full)
NOF=$(build nofield -DWSK_NO_FIELD)
NOM=$(build nomarks -DWSK_NO_MARKS)
MIN=$(build min -DWSK_MINIMAL)
printf "   %-34s %5s   %s\n" "profile" "bytes" "saved"
printf "   %-34s %5d\n"      "full (the default)"        "$FULL"
printf "   %-34s %5d   %4d\n" "-DWSK_NO_MARKS"           "$NOM" "$((FULL - NOM))"
printf "   %-34s %5d   %4d\n" "-DWSK_NO_FIELD"           "$NOF" "$((FULL - NOF))"
printf "   %-34s %5d   %4d  (%d%%)\n" "-DWSK_MINIMAL (both)" "$MIN" \
	"$((FULL - MIN))" "$(( (FULL - MIN) * 100 / FULL ))"
echo
echo "   This program draws no field and marks nothing. It carries both anyway,"
echo "   because the branch that chooses an emitter is inside the per-widget"
echo "   loop every caller links. The linker cannot drop a branch; it can only"
echo "   drop a section nothing reaches."
echo
echo "== 2. and what it does NOT carry, which is the larger half"
echo
echo "   Everything whisker exposes is static inline, so an entry point the"
echo "   caller never names is never emitted. Linked into the full build above:"
echo
"$NM" "$W/full.elf" | awk '$2 ~ /[Tt]/ && $3 ~ /^wsk/ { print "     " $3 }' | sort
echo
echo "   Absent, and each of them a feature this panel does not use:"
for SYM in wsk_hit wsk_compose wsk_rows wsk_render_clipped wsk_part wsk_field_view; do
	if "$NM" "$W/full.elf" | grep -q " [Tt] $SYM\$"; then
		echo "     $SYM  -- PRESENT, so this claim is wrong"
	else
		echo "     $SYM"
	fi
done
echo
echo "   So the preprocessor is needed for exactly the two things in section 1,"
echo "   and for nothing else. A dispatch table would let the linker drop those"
echo "   two as well -- and a dispatch table is what an RV-9 module may not have"
echo "   (design.md 4.1): it is addresses in rodata, and it broke two modules"
echo "   before whisker existed. The preprocessor is what is left, not a taste."
echo
echo "== 3. the reduced build is the same renderer, not a second one"
echo
echo "   Checked in src/run.sh rather than here, because it is a gate and not a"
echo "   measurement: the same panel compiled both ways is diffed byte for byte,"
echo "   with two controls -- a field and a mark, which the reduced build must"
echo "   REFUSE with WSK_E_KIND rather than draw as something else."
