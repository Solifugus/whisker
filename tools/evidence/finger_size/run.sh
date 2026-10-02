#!/usr/bin/env bash
#
# Draw candidate touch targets at true physical size, to be judged by a finger.
# Needs no touch input: see card.c.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RV9="${RV9:-$HOME/development/esp32/rv9}"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
cp "$RV9/components/rv9_io/src"/{raster.c,raster.h,drv_svgwin.c,font.c,font.h} "$W/"
${CC:-gcc} -O1 -w -I"$RV9/tools/hosttest/stub" -I"$W" -I"$HERE/../../../src" \
	-o "$W/card" "$HERE/card.c" "$W/raster.c" "$W/font.c"
( cd "$HERE" && "$W/card" ) 2>&1 | grep -v '^I rv9-svgwin'
command -v gwenview >/dev/null && echo "  gwenview -f $HERE/card.ppm" || true
