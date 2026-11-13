#!/usr/bin/env bash
#
# Every done-criterion in design.md §5 (the renderer), §5b (hit),
# §5c (composition), §5d (input), §5e (scrolling), §5f (nesting), §5g (wrap), §5h (text items), §5i (the toolkit)
# §5j (editing), §5l (overlays) and §5m (check, selection, focus).
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
for f in ("/tmp/wsk_full.svg", "/tmp/wsk_clip.svg", "/tmp/wsk_compose.svg", "/tmp/wsk_nest.svg", "/tmp/wsk_wrap.svg"):
	m.parse(f)
	print("ok    %s is well-formed XML" % f)
PY

echo
echo "-- 9  the same panel, compiled two ways"
${CC:-gcc} -O1 -Wall -Wextra -Werror -o /tmp/wsk_prof_full profile.c
${CC:-gcc} -O1 -Wall -Wextra -Werror -DWSK_MINIMAL -o /tmp/wsk_prof_min profile.c
/tmp/wsk_prof_full panel > /tmp/wsk_prof_full.svg
/tmp/wsk_prof_min  panel > /tmp/wsk_prof_min.svg
if cmp -s /tmp/wsk_prof_full.svg /tmp/wsk_prof_min.svg; then
	echo "ok    WSK_MINIMAL draws the shared panel byte for byte ($(stat -c%s /tmp/wsk_prof_full.svg) bytes)"
else
	echo "FAIL  WSK_MINIMAL draws the shared panel differently"
	diff <(fold -w100 /tmp/wsk_prof_full.svg) <(fold -w100 /tmp/wsk_prof_min.svg) | head
	exit 1
fi
${CC:-gcc} -O1 -Wall -Wextra -Werror -DWSK_PANEL -o /tmp/wsk_prof_panel profile.c
/tmp/wsk_prof_panel panel > /tmp/wsk_prof_panel.svg
if cmp -s /tmp/wsk_prof_full.svg /tmp/wsk_prof_panel.svg; then
	echo "ok    WSK_PANEL draws the shared panel byte for byte too (D20)"
else
	echo "FAIL  WSK_PANEL draws the shared panel differently"; exit 1
fi
# The panel profile keeps nesting and scrolling and refuses wrap and text.
for W in scrolled nested; do
	case "$(/tmp/wsk_prof_panel $W)" in
	drew*) echo "ok    WSK_PANEL draws $W" ;;
	*) echo "FAIL  WSK_PANEL refused $W"; exit 1 ;;
	esac
done
for W in wrapped text; do
	case "$(/tmp/wsk_prof_panel $W)" in
	refused\ -6*) echo "ok    WSK_PANEL refuses $W, by name" ;;
	*) echo "FAIL  WSK_PANEL did not refuse $W"; exit 1 ;;
	esac
done
# The controls. Without these, "byte for byte" is satisfied by a minimal build
# that draws a field as a label and says nothing.
for W in field marked scrolled nested wrapped text check; do
	F="$(/tmp/wsk_prof_full $W)"; M="$(/tmp/wsk_prof_min $W)"
	case "$F$M" in
	drew*refused\ -6*) echo "ok    CONTROL: $W -- full ${F%% bytes*} bytes, minimal ${M}" ;;
	*) echo "FAIL  CONTROL: $W -- full said '$F', minimal said '$M'"; exit 1 ;;
	esac
done

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
link_check () {   # link_check <source> <label> [extra flags...]
	local SRC="$1" LABEL="$2"; shift 2
	"$CC9" "${FLAGS[@]}" "$@" "${LINK[@]}" -o "$W/m.elf" "$SRC"
	"$CC9" "${FLAGS[@]}" "$@" "${LINK[@]}" -Wl,--defsym,RV9_BASE=0x8000 \
		-o "$W/m2.elf" "$SRC"
	"$OBJCOPY" -O binary "$W/m.elf" "$W/m.bin"
	"$OBJCOPY" -O binary "$W/m2.elf" "$W/m2.bin"
	SIZE="$(stat -c%s "$W/m.bin")"
	if cmp -s "$W/m.bin" "$W/m2.bin"; then
		echo "ok    $LABEL links as a module, position independent ($SIZE bytes)"
	else
		echo "FAIL  $LABEL is not position independent"
		exit 1
	fi
}
for SRC in module_shape.c module_compose.c module_input.c module_stat.c module_font.c module_slop.c module_scroll.c module_nest.c module_keys.c; do
	link_check "$SRC" "$SRC"
	# Named, not "whichever came last": this has been the wrong number twice,
	# both times because a module was added to the loop after it.
	[[ "$SRC" == module_shape.c ]] && FULL="$SIZE"
	[[ "$SRC" == module_nest.c ]] && NEST="$SIZE"
done
link_check module_shape.c "module_shape.c -DWSK_NO_NEST" -DWSK_NO_NEST
NONEST="$SIZE"
link_check module_shape.c "module_shape.c -DWSK_NO_WRAP" -DWSK_NO_WRAP
NOWRAP="$SIZE"
link_check module_shape.c "module_shape.c -DWSK_NO_TEXT" -DWSK_NO_TEXT
NOTEXT="$SIZE"
link_check module_nest.c "module_nest.c -DWSK_NO_OVER" -DWSK_NO_OVER
NOOVER="$SIZE"
link_check module_shape.c "module_shape.c -DWSK_NO_CHECK" -DWSK_NO_CHECK
NOCHECK="$SIZE"
link_check module_shape.c "module_shape.c -DWSK_PANEL" -DWSK_PANEL
PANEL="$SIZE"
link_check module_shape.c "module_shape.c -DWSK_MINIMAL" -DWSK_MINIMAL
MIN="$SIZE"
echo
echo "      THE TIERS (D21), measured on module_shape.c, a small panel:"
printf "        desktop, browser   (default)       %6d bytes\n" "$FULL"
printf "        touch panel, P4    -DWSK_PANEL     %6d\n" "$PANEL"
printf "        readout, C5        -DWSK_MINIMAL   %6d\n" "$MIN"
echo "      and what each part costs the default, by building without it:"
printf "        nesting %d, wrap %d, text items %d, the check %d;\n" \
	"$(( FULL - NONEST ))" "$(( FULL - NOWRAP ))" "$(( FULL - NOTEXT ))" "$(( FULL - NOCHECK ))"
printf "        overlays %d to a toolkit program (module_nest.c).\n" "$(( NEST - NOOVER ))"
echo "      A reduced build is the same renderer, not a second one (§7): what it"
echo "      keeps it draws byte for byte, and what it lacks it refuses by name."
echo
echo "      no libc, no .data, no .bss -- the linker script asserts the last two."
echo "      The programs: shape (flick, generated), compose (two stacks and the"
echo "      function pointer wsk_compose takes), input (a field, out of RAM),"
echo "      stat (a readout, D9's shape), font, slop (touch scatter), scroll"
echo "      (ten thousand rows, a slice at a time), nest (a window that is one"
echo "      tree, on the toolkit), keys (wkeys, two layers to fit SRC_MAX)."
