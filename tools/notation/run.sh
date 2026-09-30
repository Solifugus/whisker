#!/usr/bin/env bash
#
# The panel notation: round-trip, refusals, merge, and what it costs.
#
#   ./run.sh              the checks
#   ./run.sh --module     and the position-independence check, which is the
#                         one that decides whether it may go on the board
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"
${CC:-gcc} -O1 -Wall -Wextra -Werror -o /tmp/wsk_note_test test.c
/tmp/wsk_note_test
[[ "${1:-}" == "--module" ]] || exit 0

# Does it link into an RV-9 module? Same flags, same linker script, and the
# same double-link check tools/build_modules.sh runs -- the notation is no
# use on the board if the answer is no.
RV9="${RV9:-$HOME/development/esp32/rv9}"
TOOLS="${TOOLS:-$HOME/.espressif/tools/riscv32-esp-elf}"
CC9="$(echo "$TOOLS"/*/riscv32-esp-elf/bin/riscv32-esp-elf-gcc | cut -d' ' -f1)"
OBJCOPY="${CC9%gcc}objcopy"
[[ -x "$CC9" ]] || { echo "no riscv32-esp-elf-gcc under $TOOLS; skipping"; exit 0; }

W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
FLAGS=(-march=rv32imac_zicsr_zifencei_zaamo_zalrsc -mabi=ilp32 -mcmodel=medany -Os
       -ffreestanding -fno-builtin -fno-jump-tables -fno-tree-switch-conversion
       -ffunction-sections -fdata-sections -Wall -Wextra -Werror
       -I"$RV9/components/rv9_module/include" -I"$RV9/modules" -I"$HERE")
LINK=(-nostdlib -nostartfiles -T"$RV9/modules/module.ld" -Wl,--gc-sections)

echo
"$CC9" "${FLAGS[@]}" "${LINK[@]}" -o "$W/a.elf" module_shape.c
"$CC9" "${FLAGS[@]}" "${LINK[@]}" -Wl,--defsym,RV9_BASE=0x8000 -o "$W/b.elf" module_shape.c
"$OBJCOPY" -O binary "$W/a.elf" "$W/a.bin"
"$OBJCOPY" -O binary "$W/b.elf" "$W/b.bin"
if cmp -s "$W/a.bin" "$W/b.bin"; then
	echo "ok    links as an RV-9 module, position independent ($(stat -c%s "$W/a.bin") bytes)"
	echo "      no libc, no .data, no .bss -- the linker script asserts the last two"
else
	echo "FAIL  the module is not position independent"
	exit 1
fi
