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
#   ./run.sh --form          a form in cells (§5aq)
#   ./run.sh --client [host:]port [viewer]   a program served elsewhere (§5al)
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
# The desktop's font beyond ASCII (design §5at): RV-9's driver reads <text>
# as UTF-8 and draws from font_wide.c when built with RV9_FONT_WIDE. Until
# RV-9 takes rv9-font.patch (docs/rv9-request-font.md) it is applied to the
# copy here; one that no longer applies leaves the window ASCII, as it was,
# and --test fails on it.
FONT=("$OUT/font.c")
if grep -q RV9_FONT_WIDE "$OUT/font.h" || patch -s -d "$OUT" -p4 <"$HERE/rv9-font.patch" >/dev/null 2>&1; then
	FONT=("$OUT/font.c" "$HERE/font_wide.c" -DRV9_FONT_WIDE)
else
	echo "run.sh: rv9-font.patch no longer applies to RV-9's driver -- the window draws ASCII only" >&2
	[[ "${1:-}" == "--test" ]] && exit 1
fi
INC=(-I"$OUT/stub" -I"$OUT" -I"$HERE" -I"$HERE/../../src")

if [[ "${1:-}" == "--test" ]]; then
	# every demo program opens and keeps running: one that refuses its own
	# panel exits at once -- demo.c did, unnoticed, from 2026-10-24 to 11-15
	for P in demo input edit code floor dock tabs form; do
		${CC:-gcc} -O2 -Wall -Wextra -Werror -o "$OUT/whisker-$P" \
			"$HERE/$P.c" "$HERE/host_sdl.c" "$OUT/raster.c" "${FONT[@]}" \
			"${INC[@]}" $(pkg-config --cflags --libs sdl2)
		A=(); [[ $P == code ]] && A=("$HERE/sample.bas")   # < > in it: RV-9 decodes them
		set +e
		SDL_VIDEODRIVER=dummy timeout 1 "$OUT/whisker-$P" "${A[@]}" >/dev/null 2>"$OUT/$P.err"
		rc=$?
		set -e
		if [[ $rc == 124 ]]; then echo "ok    $P.c opens its window and keeps running"
		else echo "FAIL  $P.c exited ($rc): $(head -1 "$OUT/$P.err")"; exit 1; fi
	done
	# a file with tab characters in it (§5ai): refused, and the editor gone,
	# until a memo had columns for one
	printf 'program tabs\n\tprint "indented by a tab"\n\tif 1 then\n\t\tprint "two"\n\tend if\nend program\n' > "$OUT/tabs.bas"
	set +e
	SDL_VIDEODRIVER=dummy timeout 1 "$OUT/whisker-code" "$OUT/tabs.bas" >/dev/null 2>"$OUT/tabs.err"
	rc=$?
	set -e
	if [[ $rc == 124 ]]; then echo "ok    code.c opens a file indented with tabs and keeps running"
	else echo "FAIL  code.c refused a file with tabs ($rc): $(head -1 "$OUT/tabs.err")"; exit 1; fi
	# the same file written on Windows, its lines ended \r\n (§5aj step 1)
	sed 's/$/\r/' "$OUT/tabs.bas" > "$OUT/tabs-crlf.bas"
	set +e
	SDL_VIDEODRIVER=dummy timeout 1 "$OUT/whisker-code" "$OUT/tabs-crlf.bas" >/dev/null 2>"$OUT/crlf.err"
	rc=$?
	set -e
	if [[ $rc == 124 ]]; then echo "ok    code.c opens a file with Windows line endings and keeps running"
	else echo "FAIL  code.c refused a CRLF file ($rc): $(head -1 "$OUT/crlf.err")"; exit 1; fi
	# characters this window cannot draw -- e-acute, an Escape -- shown as
	# markers, and a Latin-1 file decoded to them (§5aj)
	printf 'program caf\303\251\n\tprint "\033[1mbold"\nend program\n' > "$OUT/marks.bas"
	printf 'program caf\351\r\nend program\r\n' > "$OUT/marks-latin1.bas"
	for F in marks marks-latin1; do
		set +e
		SDL_VIDEODRIVER=dummy timeout 1 "$OUT/whisker-code" "$OUT/$F.bas" >/dev/null 2>"$OUT/$F.err"
		rc=$?
		set -e
		if [[ $rc == 124 ]]; then echo "ok    code.c opens $F.bas, what it cannot draw as markers, and keeps running"
		else echo "FAIL  code.c refused $F.bas ($rc): $(head -1 "$OUT/$F.err")"; exit 1; fi
	done
	${CC:-gcc} -O2 -Wall -Wextra -Werror -o "$OUT/listertest" "$HERE/listertest.c" "${INC[@]}"
	"$OUT/listertest" || exit 1
	${CC:-gcc} -O2 -Wall -Wextra -Werror -o "$OUT/textfiletest" "$HERE/textfiletest.c" "${INC[@]}"
	"$OUT/textfiletest" || exit 1
	# §5al: the code editor served, shown by whisker-client through a real
	# SSH tunnel (a private sshd on 127.0.0.1, throwaway keys) -- detach,
	# come back, two viewers
	if command -v ssh >/dev/null && [[ -x /usr/sbin/sshd ]] && command -v python3 >/dev/null; then
		${CC:-gcc} -O2 -Wall -Wextra -Werror -o "$OUT/whisker-client" \
			"$HERE/client.c" "$HERE/host_sdl.c" "$OUT/raster.c" "${FONT[@]}" \
			"${INC[@]}" $(pkg-config --cflags --libs sdl2)
		${CC:-gcc} -O2 -Wall -Wextra -Werror -o "$OUT/whisker-code-web" \
			"$HERE/code.c" "$HERE/../web/host_web.c" "${INC[@]}"
		python3 "$HERE/../web/remote.py" "$OUT/whisker-code-web" "$OUT/whisker-client" "$OUT/whisker-code" || exit 1
		# §5am: the wire on one stream -- ssh -W, screens that say what they
		# draw, a program that dials a listening client through ssh -R
		python3 "$HERE/../web/duplex.py" "$OUT/whisker-code-web" "$OUT/whisker-client" "$OUT/whisker-code" || exit 1
		# the fourth independent read's host findings, each a check that
		# failed before its fix (clipboards, a dial, idle slots, long lines)
		python3 "$HERE/../web/review4.py" "$OUT/whisker-code-web" "$OUT/whisker-client" || exit 1
		python3 "$HERE/../web/keys.py" "$OUT/whisker-code-web" "$OUT/whisker-client" || exit 1   # §5as
		python3 "$HERE/../web/review5.py" "$OUT/whisker-code-web" "$OUT/whisker-client" || exit 1   # the fifth read
	else
		echo "      no ssh, sshd or python3: the remote check was skipped"
	fi
	# §5at: the desktop's font beyond ASCII, at the pixels -- built as the
	# board builds RV-9's driver (whose wide checks must fail: the control)
	# and as this window does, and ASCII drawn byte for byte the same by both
	${CC:-gcc} -O2 -Wall -Wextra -Werror -o "$OUT/fonttest-board" "$HERE/fonttest.c" \
		"$OUT/raster.c" "$OUT/font.c" -I"$OUT/stub" -I"$OUT"
	${CC:-gcc} -O2 -Wall -Wextra -Werror -o "$OUT/fonttest" "$HERE/fonttest.c" \
		"$OUT/raster.c" "${FONT[@]}" -I"$OUT/stub" -I"$OUT"
	if "$OUT/fonttest-board" "$OUT/font-board.ppm" >"$OUT/fonttest-board.out" 2>&1; then
		echo "FAIL  the board's build of RV-9's driver passed the wide font's checks: they test nothing"; exit 1
	fi
	n=$(grep -c '^FAIL' "$OUT/fonttest-board.out")
	echo "ok    the board's build of RV-9's driver: $n of the wide font's checks fail there (the control)"
	"$OUT/fonttest" "$OUT/font.ppm" | grep -v '^I ' || exit 1
	"$OUT/fonttest-board" "$OUT/ascii-board.ppm" ascii >/dev/null 2>&1
	"$OUT/fonttest" "$OUT/ascii.ppm" ascii >/dev/null 2>&1
	if cmp -s "$OUT/ascii-board.ppm" "$OUT/ascii.ppm" && [[ -s "$OUT/ascii.ppm" ]]; then
		echo "ok    every ASCII character and entity: the same pixels in both builds"
	else echo "FAIL  ASCII draws differently with the wide font"; exit 1; fi
	${CC:-gcc} -O2 -Wall -Wextra -o "$OUT/selftest" \
		"$HERE/selftest.c" "$OUT/raster.c" "${FONT[@]}" \
		"${INC[@]}" $(pkg-config --cflags --libs sdl2)
	exec env SDL_VIDEODRIVER=dummy "$OUT/selftest"
fi

if [[ "${1:-}" == "--edit" ]]; then
	shift
	${CC:-gcc} -O2 -Wall -Wextra -o "$OUT/whisker-edit" \
		"$HERE/edit.c" "$HERE/host_sdl.c" "$OUT/raster.c" "${FONT[@]}" \
		"${INC[@]}" $(pkg-config --cflags --libs sdl2)
	exec "$OUT/whisker-edit" "$@"
fi

if [[ "${1:-}" == "--floor" ]]; then
	${CC:-gcc} -O2 -Wall -Wextra -o "$OUT/whisker-floor" \
		"$HERE/floor.c" "$HERE/host_sdl.c" "$OUT/raster.c" "${FONT[@]}" \
		"${INC[@]}" $(pkg-config --cflags --libs sdl2)
	exec "$OUT/whisker-floor"
fi

if [[ "${1:-}" == "--dock" ]]; then
	${CC:-gcc} -O2 -Wall -Wextra -o "$OUT/whisker-dock" \
		"$HERE/dock.c" "$HERE/host_sdl.c" "$OUT/raster.c" "${FONT[@]}" \
		"${INC[@]}" $(pkg-config --cflags --libs sdl2)
	exec "$OUT/whisker-dock"
fi

if [[ "${1:-}" == "--tabs" ]]; then
	${CC:-gcc} -O2 -Wall -Wextra -o "$OUT/whisker-tabs" \
		"$HERE/tabs.c" "$HERE/host_sdl.c" "$OUT/raster.c" "${FONT[@]}" \
		"${INC[@]}" $(pkg-config --cflags --libs sdl2)
	exec "$OUT/whisker-tabs"
fi

if [[ "${1:-}" == "--form" ]]; then
	${CC:-gcc} -O2 -Wall -Wextra -o "$OUT/whisker-form" \
		"$HERE/form.c" "$HERE/host_sdl.c" "$OUT/raster.c" "${FONT[@]}" \
		"${INC[@]}" $(pkg-config --cflags --libs sdl2)
	exec "$OUT/whisker-form"
fi

if [[ "${1:-}" == "--client" ]]; then
	shift
	${CC:-gcc} -O2 -Wall -Wextra -o "$OUT/whisker-client" \
		"$HERE/client.c" "$HERE/host_sdl.c" "$OUT/raster.c" "${FONT[@]}" \
		"${INC[@]}" $(pkg-config --cflags --libs sdl2)
	exec "$OUT/whisker-client" "$@"
fi

if [[ "${1:-}" == "--code" ]]; then
	shift
	${CC:-gcc} -O2 -Wall -Wextra -o "$OUT/whisker-code" \
		"$HERE/code.c" "$HERE/host_sdl.c" "$OUT/raster.c" "${FONT[@]}" \
		"${INC[@]}" $(pkg-config --cflags --libs sdl2)
	exec "$OUT/whisker-code" "${1:-$HERE/sample.bas}"
fi

if [[ "${1:-}" == "--input" ]]; then
	shift
	${CC:-gcc} -O2 -Wall -Wextra -o "$OUT/whisker-input" \
		"$HERE/input.c" "$HERE/host_sdl.c" "$OUT/raster.c" "${FONT[@]}" \
		"${INC[@]}" $(pkg-config --cflags --libs sdl2)
	exec "$OUT/whisker-input" "$@"
fi

${CC:-gcc} -O2 -Wall -Wextra -o "$OUT/whisker-demo" \
	"$HERE/demo.c" "$HERE/host_sdl.c" "$OUT/raster.c" "${FONT[@]}" \
	"${INC[@]}" $(pkg-config --cflags --libs sdl2)
exec "$OUT/whisker-demo" "$@"
