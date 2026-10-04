#!/usr/bin/env bash
#
# What do the lower rungs of the overflow ladder buy? See fit.c's prologue.
#
#   ./run.sh              the C5's panel
#   ./run.sh 1024 600     the P4's
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RV9="${RV9:-$HOME/development/esp32/rv9}"
cd "$HERE"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
cp "$RV9/components/rv9_io/src"/{raster.c,raster.h,drv_svgwin.c,font.c,font.h} "$W/"
D=""; [[ $# -ge 2 ]] && D="-DPW=$1 -DPH=$2"
${CC:-gcc} -O1 -w $D -I"$RV9/tools/hosttest/stub" -I"$W" -I"$HERE/../../../src" \
	-o "$W/fit" fit.c "$W/raster.c" "$W/font.c"
"$W/fit" 2>&1 | grep -v '^I rv9-svgwin'
