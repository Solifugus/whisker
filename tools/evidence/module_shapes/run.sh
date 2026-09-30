#!/usr/bin/env bash
#
# Which shapes may a widget tree take, inside an RV-9 module?
#
# A program on RV-9 is a module, so whisker inherits every rule a module
# obeys -- and the one that shapes the API is position independence, which
# tools/build_modules.sh proves by linking each module twice at different
# bases and comparing the bytes.
#
# The documents said "no static const array of pointers". This says what is
# actually refused, and under which of three link schemes:
#
#   zerobase   what RV-9 did until 2026-09-30: shipped at 0, probe at 0x4000
#   norelax    the same, with -Wl,--no-relax
#   now        what RV-9 does since: shipped at 0x4000, probe at 0x8000,
#              through the RV9_BASE symbol modules/module.ld now takes
#
# The difference is not academic. At base 0 the linker relaxes the auipc/addi
# pair into a single `li s0,96` -- an absolute address -- because it fits in a
# 12-bit immediate, so the *shipped* blob was the one holding it. That is what
# failed the check, and it failed for shapes that hold no pointer at all.
#
# RV-9 changed the base on 2026-09-30 (rv9-answers.md §7). The zerobase column
# is kept because it is the evidence for why, and because it is the column
# that still refuses a pointer table -- the check did not lose its teeth.
#
# Needs the IDF toolchain. Point $TOOLS elsewhere if yours is installed
# somewhere else.

set -uo pipefail

RV9="${RV9:-$HOME/development/esp32/rv9}"
TOOLS="${TOOLS:-$HOME/.espressif/tools/riscv32-esp-elf}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

CC="$(echo "$TOOLS"/*/riscv32-esp-elf/bin/riscv32-esp-elf-gcc | cut -d' ' -f1)"
OBJCOPY="${CC%gcc}objcopy"
[[ -x "$CC" ]] || { echo "no riscv32-esp-elf-gcc under $TOOLS" >&2; exit 1; }
[[ -f "$RV9/modules/module.ld" ]] || { echo "no RV-9 at $RV9" >&2; exit 1; }

CFLAGS=(
	-march=rv32imac_zicsr_zifencei_zaamo_zalrsc
	-mabi=ilp32
	-mcmodel=medany
	-Os
	-ffreestanding
	-fno-builtin
	-fno-jump-tables
	-fno-tree-switch-conversion
	-ffunction-sections
	-fdata-sections
	-Wall -Wextra -Werror
	-I"$RV9/components/rv9_module/include"
	-I"$RV9/modules"
	-I"$RV9/components/rv9_io/include"
	-I"$RV9/components/rv9_ssh/include"
)

W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
# modules/module.ld takes its base from RV9_BASE, so every scheme is two
# --defsym values against the one script -- no patched copy to keep in step.
LD="$RV9/modules/module.ld"

# scheme -> shipped base, probe base, extra link flags
link_pair()
{
	case "$1" in
	zerobase) echo "0x0|0x4000|" ;;
	norelax)  echo "0x0|0x4000|-Wl,--no-relax" ;;
	now)      echo "0x4000|0x8000|" ;;
	esac
}

check()   # $1 source, $2 scheme -> prints a verdict
{
	local src="$1" spec b1 b2 extra
	spec="$(link_pair "$2")"
	b1="${spec%%|*}"; spec="${spec#*|}"
	b2="${spec%%|*}"; extra="${spec#*|}"

	if ! "$CC" "${CFLAGS[@]}" -nostdlib -nostartfiles -T"$LD" \
		-Wl,--defsym,RV9_BASE="$b1" -Wl,--gc-sections \
		${extra:+$extra} -o "$W/a.elf" "$src" 2>"$W/err"; then
		if grep -q "undefined reference" "$W/err"; then
			printf 'link failed: %s' "$(grep -o "undefined reference to .*" "$W/err" | head -1)"
		else
			printf 'build failed'
		fi
		return
	fi
	"$CC" "${CFLAGS[@]}" -nostdlib -nostartfiles -T"$LD" \
		-Wl,--defsym,RV9_BASE="$b2" -Wl,--gc-sections \
		${extra:+$extra} -o "$W/b.elf" "$src" 2>/dev/null
	"$OBJCOPY" -O binary "$W/a.elf" "$W/a.bin"
	"$OBJCOPY" -O binary "$W/b.elf" "$W/b.bin"
	if cmp -s "$W/a.bin" "$W/b.bin"; then
		printf 'pass (%d B)' "$(stat -c%s "$W/a.bin")"
	else
		printf 'FAIL'
	fi
}

printf '%-22s  %-16s  %-16s  %s\n' shape 'zerobase (was)' norelax 'now'
printf '%-22s  %-16s  %-16s  %s\n' ---------------------- ---------------- ---------------- ----------------
for f in "$HERE"/*.c; do
	printf '%-22s  %-16s  %-16s  %s\n' "$(basename "${f%.c}")" \
		"$(check "$f" zerobase)" "$(check "$f" norelax)" "$(check "$f" now)"
done

# And the same three schemes over every real RV-9 module, which is what makes
# the nonzero column a proposal rather than a guess.
if [[ "${1:-}" == "--all-modules" ]]; then
	for scheme in zerobase norelax now; do
		pass=0; fail=0; failed=""; total=0
		for dir in "$RV9"/modules/*/; do
			[[ -f "$dir/descriptor.conf" ]] && continue
			srcs=("$dir"*.c); [[ -e "${srcs[0]}" ]] || continue
			spec="$(link_pair "$scheme")"
			b1="${spec%%|*}"; spec="${spec#*|}"
			b2="${spec%%|*}"; extra="${spec#*|}"
			"$CC" "${CFLAGS[@]}" -nostdlib -nostartfiles -T"$LD" \
				-Wl,--defsym,RV9_BASE="$b1" -Wl,--gc-sections \
				${extra:+$extra} -o "$W/a.elf" "${srcs[@]}" 2>/dev/null || continue
			"$CC" "${CFLAGS[@]}" -nostdlib -nostartfiles -T"$LD" \
				-Wl,--defsym,RV9_BASE="$b2" -Wl,--gc-sections \
				${extra:+$extra} -o "$W/b.elf" "${srcs[@]}" 2>/dev/null
			"$OBJCOPY" -O binary "$W/a.elf" "$W/a.bin"
			"$OBJCOPY" -O binary "$W/b.elf" "$W/b.bin"
			total=$((total + $(stat -c%s "$W/a.bin")))
			if cmp -s "$W/a.bin" "$W/b.bin"; then pass=$((pass+1))
			else fail=$((fail+1)); failed="$failed $(basename "$dir")"; fi
		done
		printf '\nevery RV-9 module, %-9s pass %d, fail %d%s; %d bytes in all\n' \
			"$scheme:" "$pass" "$fail" "${failed:+ --$failed}" "$total"
	done
fi
