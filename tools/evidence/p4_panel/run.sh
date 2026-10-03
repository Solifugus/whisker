#!/usr/bin/env bash
#
# Does a realistic seven-inch panel fit in 4 KB? No board needed: see the
# prologue in panels.c.
#
#   ./run.sh
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RV9="${RV9:-$HOME/development/esp32/rv9}"
cd "$HERE"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
cp "$RV9/components/rv9_io/src"/{raster.c,raster.h,drv_svgwin.c,font.c,font.h} "$W/"
${CC:-gcc} -O1 -w -I"$RV9/tools/hosttest/stub" -I"$W" -I"$HERE/../../../src" \
	-o "$W/panels" panels.c "$W/raster.c" "$W/font.c"
"$W/panels" 2>&1 | grep -v '^I rv9-svgwin'
