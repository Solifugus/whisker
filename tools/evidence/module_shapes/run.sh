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
#   today      both links as RV-9 builds them: shipped at 0, probe at 0x4000
#   norelax    the same, with -Wl,--no-relax
#   nonzero    shipped at 0x4000, probe at 0x8000 -- neither link at zero
#
# The difference is not academic. At base 0 the linker relaxes an
# auipc/addi pair into a single `li s0,96` -- an absolute address -- because
# the address fits in an immediate. That is what fails the check, and it
# fails for shapes that hold no pointer at all.
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
base()  { sed "s/^    \\. = 0;\$/    . = $1;/" "$RV9/modules/module.ld"; }
base 0x0000 > "$W/zero.ld"
base 0x4000 > "$W/probe.ld"
base 0x8000 > "$W/far.ld"

# scheme -> shipped linker script, probe linker script, extra link flags
link_pair()
{
	case "$1" in
	today)   echo "$RV9/modules/module.ld|$W/probe.ld|" ;;
	norelax) echo "$RV9/modules/module.ld|$W/probe.ld|-Wl,--no-relax" ;;
	nonzero) echo "$W/probe.ld|$W/far.ld|" ;;
	esac
}

check()   # $1 source, $2 scheme -> prints a verdict
{
	local src="$1" n="${1%.c}" spec ld1 ld2 extra
	spec="$(link_pair "$2")"
	ld1="${spec%%|*}"; spec="${spec#*|}"
	ld2="${spec%%|*}"; extra="${spec#*|}"

	if ! "$CC" "${CFLAGS[@]}" -nostdlib -nostartfiles -T"$ld1" -Wl,--gc-sections \
		${extra:+$extra} -o "$W/a.elf" "$src" 2>"$W/err"; then
		if grep -q "undefined reference" "$W/err"; then
			printf 'link failed: %s' "$(grep -o "undefined reference to .*" "$W/err" | head -1)"
		else
			printf 'build failed'
		fi
		return
	fi
	"$CC" "${CFLAGS[@]}" -nostdlib -nostartfiles -T"$ld2" -Wl,--gc-sections \
		${extra:+$extra} -o "$W/b.elf" "$src" 2>/dev/null
	"$OBJCOPY" -O binary "$W/a.elf" "$W/a.bin"
	"$OBJCOPY" -O binary "$W/b.elf" "$W/b.bin"
	if cmp -s "$W/a.bin" "$W/b.bin"; then
		printf 'pass (%d B)' "$(stat -c%s "$W/a.bin")"
	else
		printf 'FAIL'
	fi
}

printf '%-22s  %-22s  %-14s  %s\n' shape today norelax nonzero
printf '%-22s  %-22s  %-14s  %s\n' ---------------------- ---------------------- -------------- -------
for f in "$HERE"/*.c; do
	printf '%-22s  %-22s  %-14s  %s\n' "$(basename "${f%.c}")" \
		"$(check "$f" today)" "$(check "$f" norelax)" "$(check "$f" nonzero)"
done

# And the same three schemes over every real RV-9 module, which is what makes
# the nonzero column a proposal rather than a guess.
if [[ "${1:-}" == "--all-modules" ]]; then
	for scheme in today norelax nonzero; do
		pass=0; fail=0; failed=""; total=0
		for dir in "$RV9"/modules/*/; do
			[[ -f "$dir/descriptor.conf" ]] && continue
			srcs=("$dir"*.c); [[ -e "${srcs[0]}" ]] || continue
			spec="$(link_pair "$scheme")"
			ld1="${spec%%|*}"; spec="${spec#*|}"
			ld2="${spec%%|*}"; extra="${spec#*|}"
			"$CC" "${CFLAGS[@]}" -nostdlib -nostartfiles -T"$ld1" -Wl,--gc-sections \
				${extra:+$extra} -o "$W/a.elf" "${srcs[@]}" 2>/dev/null || continue
			"$CC" "${CFLAGS[@]}" -nostdlib -nostartfiles -T"$ld2" -Wl,--gc-sections \
				${extra:+$extra} -o "$W/b.elf" "${srcs[@]}" 2>/dev/null
			"$OBJCOPY" -O binary "$W/a.elf" "$W/a.bin"
			"$OBJCOPY" -O binary "$W/b.elf" "$W/b.bin"
			total=$((total + $(stat -c%s "$W/a.bin")))
			if cmp -s "$W/a.bin" "$W/b.bin"; then pass=$((pass+1))
			else fail=$((fail+1)); failed="$failed $(basename "$dir")"; fi
		done
		printf '\nevery RV-9 module, %-8s pass %d, fail %d%s; %d bytes in all\n' \
			"$scheme:" "$pass" "$fail" "${failed:+ --$failed}" "$total"
	done
fi
