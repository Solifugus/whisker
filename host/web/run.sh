#!/usr/bin/env bash
#
# whisker in a browser (design.md §5k): host/linux/edit.c, unchanged, linked
# against the web host instead of SDL. Open the address it prints -- from
# this machine, or a phone on the same network.
#
#   ./run.sh                  edit.c at 1024x600 on port 8090
#   ./run.sh 800 480          another size
#   WHISKER_PORT=9000 ./run.sh
#   ./run.sh --code [file]    the code editor (§5v), code.c, in a browser
#   ./run.sh --floor          a conference floor (§5q), floor.c, in a browser
#   ./run.sh --dock           docked panes (§5ag), dock.c, in a browser
#   ./run.sh --tabs           notes in tabs (§5ah), tabs.c, in a browser
#   ./run.sh --test           the headless checks (test.py), and headless
#                             Chrome (chrome.mjs) when it is installed
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${OUT:-/tmp/whisker-web}"
mkdir -p "$OUT"
${CC:-gcc} -O2 -Wall -Wextra -Werror -o "$OUT/whisker-edit-web" \
	"$HERE/../linux/edit.c" "$HERE/host_web.c" \
	-I"$HERE/../linux" -I"$HERE/../../src"
if [[ "${1:-}" == "--code" ]]; then
	${CC:-gcc} -O2 -Wall -Wextra -Werror -o "$OUT/whisker-code-web" \
		"$HERE/../linux/code.c" "$HERE/host_web.c" \
		-I"$HERE/../linux" -I"$HERE/../../src"
	exec "$OUT/whisker-code-web" "${2:-$HERE/../linux/sample.bas}"
fi
if [[ "${1:-}" == "--floor" ]]; then
	${CC:-gcc} -O2 -Wall -Wextra -Werror -o "$OUT/whisker-floor-web" \
		"$HERE/../linux/floor.c" "$HERE/host_web.c" \
		-I"$HERE/../linux" -I"$HERE/../../src"
	exec "$OUT/whisker-floor-web"
fi
if [[ "${1:-}" == "--dock" ]]; then
	${CC:-gcc} -O2 -Wall -Wextra -Werror -o "$OUT/whisker-dock-web" \
		"$HERE/../linux/dock.c" "$HERE/host_web.c" \
		-I"$HERE/../linux" -I"$HERE/../../src"
	exec "$OUT/whisker-dock-web"
fi
if [[ "${1:-}" == "--tabs" ]]; then
	${CC:-gcc} -O2 -Wall -Wextra -Werror -o "$OUT/whisker-tabs-web" \
		"$HERE/../linux/tabs.c" "$HERE/host_web.c" \
		-I"$HERE/../linux" -I"$HERE/../../src"
	exec "$OUT/whisker-tabs-web"
fi
if [[ "${1:-}" == "--test" ]]; then
	python3 "$HERE/test.py" "$OUT/whisker-edit-web"
	${CC:-gcc} -O2 -Wall -Wextra -Werror -o "$OUT/whisker-floor-web" \
		"$HERE/../linux/floor.c" "$HERE/host_web.c" -I"$HERE/../linux" -I"$HERE/../../src"
	python3 "$HERE/seathost.py" "$OUT/whisker-floor-web"
	${CC:-gcc} -O2 -Wall -Wextra -Werror -o "$OUT/whisker-code-web" \
		"$HERE/../linux/code.c" "$HERE/host_web.c" -I"$HERE/../linux" -I"$HERE/../../src"
	python3 "$HERE/docsize.py" "$OUT/whisker-code-web" "$HERE/../linux/sample.bas"
	python3 "$HERE/editorfiles.py" "$OUT/whisker-code-web" "$HERE/../linux/sample.bas" "$OUT"
	if command -v google-chrome >/dev/null && command -v node >/dev/null; then
		node "$HERE/chrome.mjs" "$OUT/whisker-edit-web" "$OUT"
		${CC:-gcc} -O2 -Wall -Wextra -Werror -o "$OUT/whisker-code-web" \
			"$HERE/../linux/code.c" "$HERE/host_web.c" -I"$HERE/../linux" -I"$HERE/../../src"
		node "$HERE/code.mjs" "$OUT/whisker-code-web" "$HERE/../linux/sample.bas" "$OUT"
		${CC:-gcc} -O2 -Wall -Wextra -Werror -o "$OUT/whisker-floor-web" \
			"$HERE/../linux/floor.c" "$HERE/host_web.c" -I"$HERE/../linux" -I"$HERE/../../src"
		node "$HERE/floor.mjs" "$OUT/whisker-floor-web" "$OUT"
		node "$HERE/seats.mjs" "$OUT/whisker-floor-web" "$OUT"
		node "$HERE/repaint.mjs" "$OUT/whisker-floor-web"
		${CC:-gcc} -O2 -Wall -Wextra -Werror -o "$OUT/whisker-dock-web" \
			"$HERE/../linux/dock.c" "$HERE/host_web.c" -I"$HERE/../linux" -I"$HERE/../../src"
		node "$HERE/dock.mjs" "$OUT/whisker-dock-web" "$OUT"
		${CC:-gcc} -O2 -Wall -Wextra -Werror -o "$OUT/whisker-tabs-web" \
			"$HERE/../linux/tabs.c" "$HERE/host_web.c" -I"$HERE/../linux" -I"$HERE/../../src"
		node "$HERE/tabs.mjs" "$OUT/whisker-tabs-web" "$OUT"
	else
		echo "      no google-chrome or node: the real-browser check was skipped"
	fi
	exit 0
fi
exec "$OUT/whisker-edit-web" "$@"
