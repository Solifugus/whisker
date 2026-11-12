#!/usr/bin/env bash
#
# whisker in a browser (design.md §5k): host/linux/edit.c, unchanged, linked
# against the web host instead of SDL. Open the address it prints -- from
# this machine, or a phone on the same network.
#
#   ./run.sh                  edit.c at 1024x600 on port 8090
#   ./run.sh 800 480          another size
#   WHISKER_PORT=9000 ./run.sh
#   ./run.sh --test           the headless checks (test.py), and headless
#                             Chrome (chrome.mjs) when it is installed
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${OUT:-/tmp/whisker-web}"
mkdir -p "$OUT"
${CC:-gcc} -O2 -Wall -Wextra -Werror -o "$OUT/whisker-edit-web" \
	"$HERE/../linux/edit.c" "$HERE/host_web.c" \
	-I"$HERE/../linux" -I"$HERE/../../src"
if [[ "${1:-}" == "--test" ]]; then
	python3 "$HERE/test.py" "$OUT/whisker-edit-web"
	if command -v google-chrome >/dev/null && command -v node >/dev/null; then
		node "$HERE/chrome.mjs" "$OUT/whisker-edit-web" "$OUT"
	else
		echo "      no google-chrome or node: the real-browser check was skipped"
	fi
	exit 0
fi
exec "$OUT/whisker-edit-web" "$@"
