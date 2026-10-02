#!/usr/bin/env bash
#
# What a keyboard costs, three ways. gbasic-gap.md §3 option 3, measured.
#
#   ./run.sh              the P4's panel, 1024x600
#   ./run.sh 320 172      the C5's
#
# RV-9's own parser and rasteriser, built natively through tools/hosttest's
# stubs. Nothing here needs a board.
#
# Set RV9 if RV-9 is not at ~/development/esp32/rv9.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RV9="${RV9:-$HOME/development/esp32/rv9}"
W="${1:-1024}"
H="${2:-600}"

[[ -f "$RV9/components/rv9_io/src/drv_svgwin.c" ]] || { echo "no RV-9 at $RV9" >&2; exit 1; }

T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
cp "$RV9/components/rv9_io/src"/{raster.c,raster.h,drv_svgwin.c,font.c,font.h} "$T/"

${CC:-gcc} -O1 -w -DPW="$W" -DPH="$H" \
	-I"$RV9/tools/hosttest/stub" -I"$T" -I"$HERE/../../../src" \
	-o "$T/keys" "$HERE/keys.c" "$T/raster.c" "$T/font.c"

cd "$HERE"
"$T/keys" 2>&1 | grep -v '^I rv9-svgwin'
