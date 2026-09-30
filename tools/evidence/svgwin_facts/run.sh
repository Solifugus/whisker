#!/usr/bin/env bash
#
# Facts about /w0, established by running it rather than reading it.
#
# RV-9's own sources, built natively against tools/hosttest's stubs -- the
# real parser and the real rasteriser with a fake panel behind them. Nothing
# here needs a board.
#
#   ./run.sh              every section, then the byte budget at both sizes
#   ./run.sh 3            one section of facts.c, by number
#
# Set RV9 if RV-9 is not at ~/development/esp32/rv9.

set -euo pipefail

RV9="${RV9:-$HOME/development/esp32/rv9}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$RV9/components/rv9_io/src"
STUB="$RV9/tools/hosttest/stub"

[[ -f "$SRC/drv_svgwin.c" ]] || { echo "no RV-9 at $RV9" >&2; exit 1; }

W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
cp "$SRC"/raster.c "$SRC"/raster.h "$SRC"/drv_svgwin.c "$SRC"/font.c "$SRC"/font.h "$W/"

CC="${CC:-gcc}"
FLAGS=(-O1 -w -I"$STUB" -I"$W")

$CC "${FLAGS[@]}" -o "$W/facts" "$HERE/facts.c" "$W/raster.c" "$W/font.c"
"$W/facts" "${1:-0}" 2>&1 | grep -v '^I rv9-svgwin'

[[ $# -gt 0 ]] && exit 0

for size in "320 172" "1024 600"; do
	set -- $size
	$CC "${FLAGS[@]}" -DPW=$1 -DPH=$2 -o "$W/bytes" "$HERE/bytes.c" "$W/raster.c" "$W/font.c"
	( cd "$HERE" && "$W/bytes" 2>&1 | grep -v '^I rv9-svgwin' )
	echo
done
