#!/usr/bin/env bash
#
# whisker on Linux: a window, a blit, a point.
#
#   ./run.sh                 320x172 at 3x -- the C5's panel
#   ./run.sh 1024 600 1      the P4's, at actual size
#   ./run.sh --input         a field and a keyboard; click the keys
#   ./run.sh --test          the contract, headless, with no person
#
# The pixels come from RV-9's own rasteriser, built from its tree through the
# stubs it keeps for the purpose, so this draws what the board draws. SDL puts
# the buffer in a window and says where you clicked.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RV9="${RV9:-$HOME/development/esp32/rv9}"
OUT="${OUT:-/tmp/whisker-host}"
mkdir -p "$OUT"
cp "$RV9/components/rv9_io/src"/{raster.c,raster.h,drv_svgwin.c,font.c,font.h} "$OUT/"
INC=(-I"$RV9/tools/hosttest/stub" -I"$OUT" -I"$HERE" -I"$HERE/../../src")

if [[ "${1:-}" == "--test" ]]; then
	${CC:-gcc} -O2 -Wall -Wextra -o "$OUT/selftest" \
		"$HERE/selftest.c" "$OUT/raster.c" "$OUT/font.c" \
		"${INC[@]}" $(pkg-config --cflags --libs sdl2)
	exec env SDL_VIDEODRIVER=dummy "$OUT/selftest"
fi

if [[ "${1:-}" == "--input" ]]; then
	shift
	${CC:-gcc} -O2 -Wall -Wextra -o "$OUT/whisker-input" \
		"$HERE/input.c" "$HERE/host_sdl.c" "$OUT/raster.c" "$OUT/font.c" \
		"${INC[@]}" $(pkg-config --cflags --libs sdl2)
	exec "$OUT/whisker-input" "$@"
fi

${CC:-gcc} -O2 -Wall -Wextra -o "$OUT/whisker-demo" \
	"$HERE/demo.c" "$HERE/host_sdl.c" "$OUT/raster.c" "$OUT/font.c" \
	"${INC[@]}" $(pkg-config --cflags --libs sdl2)
exec "$OUT/whisker-demo" "$@"
