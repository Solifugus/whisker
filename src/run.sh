#!/usr/bin/env bash
#
# Every done-criterion in design.md §5 (the renderer), §5b (hit) and
# §5c (composition).
#
#   ./run.sh              the suite, the picture, and the XML check
#   ./run.sh --module     and the module build, which is criterion 7
#
# Set RV9 if RV-9 is not at ~/development/esp32/rv9.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RV9="${RV9:-$HOME/development/esp32/rv9}"
cd "$HERE"

echo "-- 1..6  the suite"
${CC:-gcc} -O1 -Wall -Wextra -Werror -o /tmp/wsk_test test.c
/tmp/wsk_test

echo
echo "-- 1  the document parses, by something that is not us"
python3 - <<'PY'
import xml.dom.minidom as m
for f in ("/tmp/wsk_full.svg", "/tmp/wsk_clip.svg", "/tmp/wsk_compose.svg"):
	m.parse(f)
	print("ok    %s is well-formed XML" % f)
PY

echo
echo "-- 3, 8  through RV-9's own rasteriser, to a picture"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
cp "$RV9/components/rv9_io/src"/{raster.c,raster.h,drv_svgwin.c,font.c,font.h} "$W/"
${CC:-gcc} -O1 -w -I"$RV9/tools/hosttest/stub" -I"$W" -I"$HERE" \
	-o "$W/picture" picture.c "$W/raster.c" "$W/font.c"
( cd "$HERE" && "$W/picture" ) 2>&1 | grep -v '^I rv9-svgwin'

[[ "${1:-}" == "--module" ]] || exit 0

echo
echo "-- 7  it links as an RV-9 module, and is position independent"
TOOLS="${TOOLS:-$HOME/.espressif/tools/riscv32-esp-elf}"
CC9="$(echo "$TOOLS"/*/riscv32-esp-elf/bin/riscv32-esp-elf-gcc | cut -d' ' -f1)"
OBJCOPY="${CC9%gcc}objcopy"
[[ -x "$CC9" ]] || { echo "      no riscv32-esp-elf-gcc under $TOOLS; skipped"; exit 0; }
FLAGS=(-march=rv32imac_zicsr_zifencei_zaamo_zalrsc -mabi=ilp32 -mcmodel=medany -Os
       -ffreestanding -fno-builtin -fno-jump-tables -fno-tree-switch-conversion
       -ffunction-sections -fdata-sections -Wall -Wextra -Werror
       -I"$RV9/components/rv9_module/include" -I"$RV9/modules" -I"$HERE")
LINK=(-nostdlib -nostartfiles -T"$RV9/modules/module.ld" -Wl,--gc-sections)
for SRC in module_shape.c module_compose.c; do
	"$CC9" "${FLAGS[@]}" "${LINK[@]}" -o "$W/m.elf" "$SRC"
	"$CC9" "${FLAGS[@]}" "${LINK[@]}" -Wl,--defsym,RV9_BASE=0x8000 -o "$W/m2.elf" "$SRC"
	"$OBJCOPY" -O binary "$W/m.elf" "$W/m.bin"
	"$OBJCOPY" -O binary "$W/m2.elf" "$W/m2.bin"
	if cmp -s "$W/m.bin" "$W/m2.bin"; then
		echo "ok    $SRC links as a module, position independent ($(stat -c%s "$W/m.bin") bytes)"
	else
		echo "FAIL  $SRC is not position independent"
		exit 1
	fi
done
echo "      no libc, no .data, no .bss -- the linker script asserts the last two"
echo "      module_compose.c is where the function pointer wsk_compose takes,"
echo "      and the array of trees it walks, get checked rather than assumed"
