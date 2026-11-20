#!/usr/bin/env bash
#
# SDL 2's own releases, for building whisker's desktop programs for another
# platform from Linux (design.md §5au). Fetched once into ~/.cache/whisker,
# each pinned by SHA-256 -- a release that changes is refused, not used.
#
#   sdl-cross.sh windows   the MinGW release: prints the folder holding
#                          include/ and lib/
#   sdl-cross.sh mac       the source's headers: prints a folder holding
#                          SDL2/, for compiling (not linking) for macOS
#
# Prints nothing and fails when the release cannot be had.
set -euo pipefail
VER=2.30.9
DIR="${XDG_CACHE_HOME:-$HOME/.cache}/whisker"

fetch() {   # file, sha256
	[[ -f "$DIR/$1" ]] && return 0
	mkdir -p "$DIR"
	curl -sSLf -o "$DIR/$1.part" "https://github.com/libsdl-org/SDL/releases/download/release-$VER/$1" || return 1
	if [[ "$(sha256sum "$DIR/$1.part" | cut -d' ' -f1)" != "$2" ]]; then
		echo "sdl-cross.sh: $1 is not the release pinned here" >&2
		rm -f "$DIR/$1.part"; return 1
	fi
	mv "$DIR/$1.part" "$DIR/$1"
}

case "${1:-}" in
windows)
	OUT="$DIR/SDL2-$VER/x86_64-w64-mingw32"
	if [[ ! -f "$OUT/lib/libSDL2.a" ]]; then
		fetch "SDL2-devel-$VER-mingw.tar.gz" b188d165dc4a0372042bff3d0c18e9bb90772581e92a7b8b47b521b47714f75c
		tar -xzf "$DIR/SDL2-devel-$VER-mingw.tar.gz" -C "$DIR"
	fi
	echo "$OUT" ;;
mac)
	OUT="$DIR/sdl-headers-$VER"
	if [[ ! -f "$OUT/SDL2/SDL_config_macosx.h" ]]; then
		fetch "SDL2-$VER.tar.gz" 24b574f71c87a763f50704bbb630cbe38298d544a1f890f099a4696b1d6beba4
		rm -rf "$OUT.part"; mkdir -p "$OUT.part"
		tar -xzf "$DIR/SDL2-$VER.tar.gz" -C "$OUT.part" "SDL2-$VER/include"
		mv "$OUT.part/SDL2-$VER/include" "$OUT.part/SDL2" && rmdir "$OUT.part/SDL2-$VER"
		rm -rf "$OUT"; mv "$OUT.part" "$OUT"
	fi
	echo "$OUT" ;;
*)
	echo "usage: sdl-cross.sh windows|mac" >&2; exit 2 ;;
esac
