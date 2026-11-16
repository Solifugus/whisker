#!/usr/bin/env bash
#
# whisker on Linux: a window, a blit, a point.
#
#   ./run.sh                 320x172 at 3x -- the C5's panel
#   ./run.sh 1024 600 1      the P4's, at actual size
#   ./run.sh --input         a field and a keyboard; click the keys
#   ./run.sh --edit          fields you type into with the real keyboard (§5j)
#   ./run.sh --code [file]   a code editor: whisker's source widget (§5v)
#   ./run.sh --floor         a conference floor: whisker's world (§5q)
#   ./run.sh --dock          docked panes: drag one by its caption (§5ag)
#   ./run.sh --tabs          notes in tabs (§5ah)
#   ./run.sh --test          the contract, headless, with no person
#
# The pixels come from RV-9's own rasteriser, built from its tree through the
# stubs it keeps for the purpose, so this draws what the board draws. SDL puts
# the buffer in a window and says where you clicked.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RV9="${RV9:-$HOME/development/esp32/rv9}"
# RV-9 as committed, not as its working tree: RV-9's own session may be
# half way through a change (2026-11-16: a driver field half-added there
# broke every build here). RV9_WORKTREE=1 builds against the working tree.
RV9_SNAP=""
if [[ -z "${RV9_WORKTREE:-}" ]] && git -C "$RV9" rev-parse --verify HEAD >/dev/null 2>&1; then
	RV9_SNAP="$(mktemp -d)"
	git -C "$RV9" archive HEAD components tools modules | tar -x -C "$RV9_SNAP"
	RV9="$RV9_SNAP"
fi
OUT="${OUT:-/tmp/whisker-host}"
mkdir -p "$OUT/stub"
cp "$RV9/components/rv9_io/src"/{raster.c,raster.h,drv_svgwin.c,font.c,font.h} "$OUT/"
cp -r "$RV9/tools/hosttest/stub/." "$OUT/stub/"
[[ -n "$RV9_SNAP" ]] && rm -rf "$RV9_SNAP"         # copied: this script ends by exec
INC=(-I"$OUT/stub" -I"$OUT" -I"$HERE" -I"$HERE/../../src")

if [[ "${1:-}" == "--test" ]]; then
	# every demo program opens and keeps running: one that refuses its own
	# panel exits at once -- demo.c did, unnoticed, from 2026-10-24 to 11-15
	for P in demo input edit code floor dock tabs; do
		${CC:-gcc} -O2 -Wall -Wextra -Werror -o "$OUT/whisker-$P" \
			"$HERE/$P.c" "$HERE/host_sdl.c" "$OUT/raster.c" "$OUT/font.c" \
			"${INC[@]}" $(pkg-config --cflags --libs sdl2)
		A=(); [[ $P == code ]] && A=("$HERE/sample.bas")   # < > in it: RV-9 decodes them
		set +e
		SDL_VIDEODRIVER=dummy timeout 1 "$OUT/whisker-$P" "${A[@]}" >/dev/null 2>"$OUT/$P.err"
		rc=$?
		set -e
		if [[ $rc == 124 ]]; then echo "ok    $P.c opens its window and keeps running"
		else echo "FAIL  $P.c exited ($rc): $(head -1 "$OUT/$P.err")"; exit 1; fi
	done
	${CC:-gcc} -O2 -Wall -Wextra -Werror -o "$OUT/listertest" "$HERE/listertest.c" "${INC[@]}"
	"$OUT/listertest" || exit 1
	${CC:-gcc} -O2 -Wall -Wextra -o "$OUT/selftest" \
		"$HERE/selftest.c" "$OUT/raster.c" "$OUT/font.c" \
		"${INC[@]}" $(pkg-config --cflags --libs sdl2)
	exec env SDL_VIDEODRIVER=dummy "$OUT/selftest"
fi

if [[ "${1:-}" == "--edit" ]]; then
	shift
	${CC:-gcc} -O2 -Wall -Wextra -o "$OUT/whisker-edit" \
		"$HERE/edit.c" "$HERE/host_sdl.c" "$OUT/raster.c" "$OUT/font.c" \
		"${INC[@]}" $(pkg-config --cflags --libs sdl2)
	exec "$OUT/whisker-edit" "$@"
fi

if [[ "${1:-}" == "--floor" ]]; then
	${CC:-gcc} -O2 -Wall -Wextra -o "$OUT/whisker-floor" \
		"$HERE/floor.c" "$HERE/host_sdl.c" "$OUT/raster.c" "$OUT/font.c" \
		"${INC[@]}" $(pkg-config --cflags --libs sdl2)
	exec "$OUT/whisker-floor"
fi

if [[ "${1:-}" == "--dock" ]]; then
	${CC:-gcc} -O2 -Wall -Wextra -o "$OUT/whisker-dock" \
		"$HERE/dock.c" "$HERE/host_sdl.c" "$OUT/raster.c" "$OUT/font.c" \
		"${INC[@]}" $(pkg-config --cflags --libs sdl2)
	exec "$OUT/whisker-dock"
fi

if [[ "${1:-}" == "--tabs" ]]; then
	${CC:-gcc} -O2 -Wall -Wextra -o "$OUT/whisker-tabs" \
		"$HERE/tabs.c" "$HERE/host_sdl.c" "$OUT/raster.c" "$OUT/font.c" \
		"${INC[@]}" $(pkg-config --cflags --libs sdl2)
	exec "$OUT/whisker-tabs"
fi

if [[ "${1:-}" == "--code" ]]; then
	shift
	${CC:-gcc} -O2 -Wall -Wextra -o "$OUT/whisker-code" \
		"$HERE/code.c" "$HERE/host_sdl.c" "$OUT/raster.c" "$OUT/font.c" \
		"${INC[@]}" $(pkg-config --cflags --libs sdl2)
	exec "$OUT/whisker-code" "${1:-$HERE/sample.bas}"
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
