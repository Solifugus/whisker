#!/usr/bin/env bash
#
# wkeys, run here against RV-9's own window. See the prologue in keys.c.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RV9="${RV9:-$HOME/development/esp32/rv9}"
cd "$HERE"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
cp "$RV9/components/rv9_io/src"/{raster.c,raster.h,drv_svgwin.c,font.c,font.h} "$W/"
${CC:-gcc} -O1 -w -I"$RV9/tools/hosttest/stub" -I"$W" -c -o "$W/window.o" window.c
${CC:-gcc} -O1 -w -I"$RV9/tools/hosttest/stub" -I"$W" -c -o "$W/raster.o" "$W/raster.c"
${CC:-gcc} -O1 -w -I"$RV9/tools/hosttest/stub" -I"$W" -c -o "$W/font.o" "$W/font.c"
${CC:-gcc} -O1 -w -I"$HERE/../../../src" -I"$RV9/modules" \
	-I"$RV9/components/rv9_module/include" -c -o "$W/keys.o" keys.c
${CC:-gcc} -o "$W/keys" "$W/keys.o" "$W/window.o" "$W/raster.o" "$W/font.o"
"$W/keys" 2>&1 | grep -v '^I rv9-svgwin'
