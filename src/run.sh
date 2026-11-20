#!/usr/bin/env bash
#
# Every done-criterion in design.md §5 (the renderer), §5b (hit),
# §5c (composition), §5d (input), §5e (scrolling), §5f (nesting), §5g (wrap), §5h (text items), §5i (the toolkit)
# §5j (editing), §5l (overlays) and §5m (check, selection, focus).
#
#   ./run.sh              the suite, the picture, and the XML check
#   ./run.sh --module     and the module build, which is criterion 7
#
# Set RV9 if RV-9 is not at ~/development/esp32/rv9.
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
trap '[[ -n "$RV9_SNAP" ]] && rm -rf "$RV9_SNAP"' EXIT
cd "$HERE"

echo "-- 1..6  the suite"
${CC:-gcc} -O1 -Wall -Wextra -Werror -o /tmp/wsk_test test.c
/tmp/wsk_test
# §5au: the chooser as Windows builds it, a drive's root a top
${CC:-gcc} -O1 -Wall -Wextra -Werror -o /tmp/wsk_drives drives.c
/tmp/wsk_drives

echo
echo "-- 1  the document parses, by something that is not us"
python3 - <<'PY'
import xml.dom.minidom as m
for f in ("/tmp/wsk_full.svg", "/tmp/wsk_clip.svg", "/tmp/wsk_compose.svg", "/tmp/wsk_nest.svg", "/tmp/wsk_wrap.svg", "/tmp/wsk_entity.svg"):
	m.parse(f)
	print("ok    %s is well-formed XML" % f)
PY

echo
echo "-- 9  the same panel, compiled two ways"
${CC:-gcc} -O1 -Wall -Wextra -Werror -o /tmp/wsk_prof_full profile.c
${CC:-gcc} -O1 -Wall -Wextra -Werror -DWSK_MINIMAL -o /tmp/wsk_prof_min profile.c
/tmp/wsk_prof_full panel > /tmp/wsk_prof_full.svg
/tmp/wsk_prof_min  panel > /tmp/wsk_prof_min.svg
if cmp -s /tmp/wsk_prof_full.svg /tmp/wsk_prof_min.svg; then
	echo "ok    WSK_MINIMAL draws the shared panel byte for byte ($(stat -c%s /tmp/wsk_prof_full.svg) bytes)"
else
	echo "FAIL  WSK_MINIMAL draws the shared panel differently"
	diff <(fold -w100 /tmp/wsk_prof_full.svg) <(fold -w100 /tmp/wsk_prof_min.svg) | head
	exit 1
fi
${CC:-gcc} -O1 -Wall -Wextra -Werror -DWSK_PANEL -o /tmp/wsk_prof_panel profile.c
/tmp/wsk_prof_panel panel > /tmp/wsk_prof_panel.svg
if cmp -s /tmp/wsk_prof_full.svg /tmp/wsk_prof_panel.svg; then
	echo "ok    WSK_PANEL draws the shared panel byte for byte too (D20)"
else
	echo "FAIL  WSK_PANEL draws the shared panel differently"; exit 1
fi
# The panel profile keeps nesting and scrolling and refuses wrap and text.
for W in scrolled nested; do
	case "$(/tmp/wsk_prof_panel $W)" in
	drew*) echo "ok    WSK_PANEL draws $W" ;;
	*) echo "FAIL  WSK_PANEL refused $W"; exit 1 ;;
	esac
done
for W in wrapped text; do
	case "$(/tmp/wsk_prof_panel $W)" in
	refused\ -6*) echo "ok    WSK_PANEL refuses $W, by name" ;;
	*) echo "FAIL  WSK_PANEL did not refuse $W"; exit 1 ;;
	esac
done
# The controls. Without these, "byte for byte" is satisfied by a minimal build
# that draws a field as a label and says nothing.
for W in field marked scrolled nested wrapped text check memo arrow source; do
	F="$(/tmp/wsk_prof_full $W)"; M="$(/tmp/wsk_prof_min $W)"
	case "$F$M" in
	drew*refused\ -6*) echo "ok    CONTROL: $W -- full ${F%% bytes*} bytes, minimal ${M}" ;;
	*) echo "FAIL  CONTROL: $W -- full said '$F', minimal said '$M'"; exit 1 ;;
	esac
done

echo
echo "-- every switch builds, alone and in its tiers"
# A combination nobody builds is a combination that breaks unseen: two did,
# 2026-11-14, -DWSK_NO_FIELD and -DWSK_NO_SCROLL, each since an iteration
# that never compiled them. So all of them, every run.
cat > /tmp/wsk_switch.c <<'C'
#include "whisker.h"
#ifndef WSK_NO_INPUT
int f(wsk_ui_t *u, wsk_event_t *e) { wsk_note_t n[2]; int a, b; return wsk_feed(u, e, n, 2, &a, &b); }
#endif
int g(wsk_tree_t *t, wsk_sink_t *s) { return wsk_render(t, s, 4096) + wsk_hit(t, 1, 1); }
C
for D in "" -DWSK_PANEL -DWSK_MINIMAL -DWSK_NO_FIELD -DWSK_NO_MARKS -DWSK_NO_SCROLL \
         -DWSK_NO_NEST -DWSK_NO_WRAP -DWSK_NO_TEXT -DWSK_NO_OVER -DWSK_NO_CHECK \
         -DWSK_NO_MEMO -DWSK_NO_INPUT -DWSK_NO_UTF8 -DWSK_NO_IMAGE -DWSK_NO_TEXTSEL -DWSK_NO_UNDO -DWSK_NO_SOURCE -DWSK_NO_ENTITY \
         -DWSK_NO_WORDS -DWSK_NO_SPLIT -DWSK_NO_MENUKEYS \
         -DWSK_NO_WORLD -DWSK_NO_LISTKEYS -DWSK_NO_HOVER -DWSK_NO_TIP -DWSK_NO_GRAB -DWSK_NO_TABCHAR -DWSK_NO_UNDRAWN -DWSK_NO_GRID -DWSK_NO_SUBMENU; do
	if ${CC:-gcc} -O1 -Wall -Wextra -Werror $D -I"$HERE" -c -o /dev/null /tmp/wsk_switch.c 2>/tmp/wsk_switch.err; then
		:
	else
		echo "FAIL  whisker.h does not build with '${D:-the default}'"; head -5 /tmp/wsk_switch.err; exit 1
	fi
done
echo "ok    whisker.h builds with each of the 31 profiles and switches, -Werror"
# a switch that drops a thing refuses it, rather than drawing it some other
# way: WSK_NO_TABCHAR let a tab into a memo as a marker (the fourth read)
cat > /tmp/wsk_notab.c <<'C'
#include "whisker.h"
int main(void) { return wsk__ok_n("a\tb", 3, 0, 1) != 0; }
C
if ${CC:-gcc} -O1 -Wall -Wextra -Werror -DWSK_NO_TABCHAR -I"$HERE" -o /tmp/wsk_notab /tmp/wsk_notab.c && /tmp/wsk_notab; then
	echo "ok    -DWSK_NO_TABCHAR: a memo refuses a tab, not a marker for it"
else
	echo "FAIL  -DWSK_NO_TABCHAR lets a tab into a memo"; exit 1
fi
# and a grid (§5aq), in the panel tier, is refused there by name -- with the
# default build drawing the same tree the control
cat > /tmp/wsk_nogrid.c <<'C'
#include "whisker.h"
int main(void)
{
	wsk_widget_t w[1] = { { .kind = WSK_LABEL, .text = "a" } };
	wsk_tree_t t;
	int which = 0;
	wsk_init(&t, WSK_ROW, 100, 40);
	t.item = w; t.n = 1; t.overflow = WSK_GRID;
#ifndef WSK_NO_GRID
	static const wsk_cell_t c[1];
	t.cells = c;
	return wsk_check(&t, &which) != WSK_OK;
#else
	return wsk_check(&t, &which) != WSK_E_KIND;
#endif
}
C
if ${CC:-gcc} -O1 -Wall -Wextra -Werror -I"$HERE" -o /tmp/wsk_nogrid /tmp/wsk_nogrid.c && /tmp/wsk_nogrid &&
   ${CC:-gcc} -O1 -Wall -Wextra -Werror -DWSK_PANEL -I"$HERE" -o /tmp/wsk_nogrid /tmp/wsk_nogrid.c && /tmp/wsk_nogrid; then
	echo "ok    a grid: checks at the default (the control), refused WSK_E_KIND at -DWSK_PANEL"
else
	echo "FAIL  -DWSK_PANEL does not refuse a grid by name"; exit 1
fi
# and a button that opens a menu (§5ar), the same way
cat > /tmp/wsk_nosub.c <<'C'
#include "whisker.h"
int main(void)
{
	wsk_widget_t w[1] = { { .kind = WSK_BUTTON, .text = "File", .size = 30, .sub = 0, .flags = 16 } };
	wsk_tree_t t, k[1];
	int which = 0;
	wsk_init(&t, WSK_COL, 100, 100);
	t.item = w; t.n = 1; t.nest = k; t.nests = 1;
	wsk_nest_init(&k[0], &t, WSK_COL);
#ifndef WSK_NO_SUBMENU
	return wsk_check(&t, &which) != WSK_OK;
#else
	return wsk_check(&t, &which) != WSK_E_KIND;
#endif
}
C
if ${CC:-gcc} -O1 -Wall -Wextra -Werror -I"$HERE" -o /tmp/wsk_nosub /tmp/wsk_nosub.c && /tmp/wsk_nosub &&
   ${CC:-gcc} -O1 -Wall -Wextra -Werror -DWSK_PANEL -I"$HERE" -o /tmp/wsk_nosub /tmp/wsk_nosub.c && /tmp/wsk_nosub; then
	echo "ok    a submenu button: checks at the default (the control), refused WSK_E_KIND at -DWSK_PANEL"
else
	echo "FAIL  -DWSK_PANEL does not refuse a submenu button by name"; exit 1
fi
# the fifth read: without the menu's keys, Enter on a focused menu-bar
# button that opens a menu closed the open one silently -- no CLOSE
cat > /tmp/wsk_nomk.c <<'C'
#include "whisker.h"
#include <string.h>
int main(void)
{
	static wsk_widget_t top[1], bar[2], m1[1], m2[1];
	wsk_tree_t t, k[3];
	wsk_ui_t ui;
	wsk_event_t ev;
	wsk_note_t nt[8];
	int y0, y1, n, closes = 0;
	memset(top, 0, sizeof top); memset(bar, 0, sizeof bar); memset(m1, 0, sizeof m1); memset(m2, 0, sizeof m2);
	top[0].kind = WSK_STACK; top[0].sub = 0; top[0].size = 30;
	bar[0].kind = bar[1].kind = WSK_BUTTON; bar[0].size = bar[1].size = 60;
	bar[0].text[0] = 'F'; bar[1].text[0] = 'H';
	bar[0].flags = bar[1].flags = WSK_SUBMENU; bar[0].sub = 1; bar[1].sub = 2;
	m1[0].kind = m2[0].kind = WSK_BUTTON; m1[0].size = m2[0].size = 24; m1[0].text[0] = 'a'; m2[0].text[0] = 'b';
	wsk_init(&t, WSK_COL, 300, 200);
	t.pad = 0; t.gap = 0; t.item = top; t.n = 1; t.nest = k; t.nests = 3;
	wsk_nest_init(&k[0], &t, WSK_ROW); k[0].item = bar; k[0].n = 2; k[0].pad = 0; k[0].gap = 0;
	wsk_nest_init(&k[1], &t, WSK_COL); k[1].item = m1; k[1].n = 1;
	wsk_nest_init(&k[2], &t, WSK_COL); k[2].item = m2; k[2].n = 1;
	wsk_ui_init(&ui, &t);
	memset(&ev, 0, sizeof ev); ev.kind = WSK_EV_DOWN; ev.x = 30; ev.y = 15; ev.d = 1;
	wsk_feed(&ui, &ev, nt, 8, &y0, &y1);
	ev.kind = WSK_EV_UP; wsk_feed(&ui, &ev, nt, 8, &y0, &y1);
	if (t.over != 1) return 2;                         /* the control: F's menu opened by a click */
	t.focus = &bar[1];
	memset(&ev, 0, sizeof ev); ev.kind = WSK_EV_KEY; ev.key = WSK_KEY_ENTER; ev.d = 1;
	n = wsk_feed(&ui, &ev, nt, 8, &y0, &y1);
	for (int i = 0; i < n; i++) closes += nt[i].kind == WSK_NOTE_CLOSE;
	return !(t.over == 2 && closes == 1);
}
C
if ${CC:-gcc} -O1 -Wall -Wextra -Werror -DWSK_NO_MENUKEYS -I"$HERE" -o /tmp/wsk_nomk /tmp/wsk_nomk.c && /tmp/wsk_nomk; then
	echo "ok    -DWSK_NO_MENUKEYS: Enter on a focused bar button opening another menu -- that menu, and CLOSE for the first"
else
	echo "FAIL  -DWSK_NO_MENUKEYS: Enter swapped the open menu without CLOSE (fifth read)"; exit 1
fi

echo
echo "-- the default palette: legible, and told apart in every kind of colour vision (D24)"
python3 "$HERE/../tools/evidence/palette/audit.py" "$HERE/whisker.h" || exit 1

echo
echo "-- 3, 8  through RV-9's own rasteriser, to a picture"
W="$(mktemp -d)"
trap 'rm -rf "$W" ${RV9_SNAP:+"$RV9_SNAP"}' EXIT
cp "$RV9/components/rv9_io/src"/{raster.c,raster.h,drv_svgwin.c,font.c,font.h} "$W/"
${CC:-gcc} -O1 -w -I"$RV9/tools/hosttest/stub" -I"$W" -I"$HERE" \
	-o "$W/picture" picture.c "$W/raster.c" "$W/font.c"
( cd "$HERE" && "$W/picture" ) 2>&1 | grep -v '^I rv9-svgwin'

[[ "${1:-}" == "--module" ]] || exit 0

echo
echo "-- 7  it links as an RV-9 module, and is position independent"
TOOLS="${TOOLS:-$HOME/.espressif/tools/riscv32-esp-elf}"
CC9="$(echo "$TOOLS"/*/riscv32-esp-elf/bin/riscv32-esp-elf-gcc | cut -d' ' -f1)"
OBJCOPY="${CC9%gcc}objcopy"
[[ -x "$CC9" ]] || { echo "      no riscv32-esp-elf-gcc under $TOOLS; skipped"; exit 0; }
FLAGS=(-march=rv32imac_zicsr_zifencei_zaamo_zalrsc -mabi=ilp32 -mcmodel=medany -Os
       -ffreestanding -fno-builtin -fno-jump-tables -fno-tree-switch-conversion
       -ffunction-sections -fdata-sections -Wall -Wextra -Werror
       -I"$RV9/components/rv9_module/include" -I"$RV9/modules" -I"$HERE")
LINK=(-nostdlib -nostartfiles -T"$RV9/modules/module.ld" -Wl,--gc-sections)
link_check () {   # link_check <source> <label> [extra flags...]
	local SRC="$1" LABEL="$2"; shift 2
	"$CC9" "${FLAGS[@]}" "$@" "${LINK[@]}" -o "$W/m.elf" "$SRC"
	"$CC9" "${FLAGS[@]}" "$@" "${LINK[@]}" -Wl,--defsym,RV9_BASE=0x8000 \
		-o "$W/m2.elf" "$SRC"
	"$OBJCOPY" -O binary "$W/m.elf" "$W/m.bin"
	"$OBJCOPY" -O binary "$W/m2.elf" "$W/m2.bin"
	SIZE="$(stat -c%s "$W/m.bin")"
	if cmp -s "$W/m.bin" "$W/m2.bin"; then
		echo "ok    $LABEL links as a module, position independent ($SIZE bytes)"
	else
		echo "FAIL  $LABEL is not position independent"
		exit 1
	fi
}
for SRC in module_shape.c module_compose.c module_input.c module_stat.c module_font.c module_slop.c module_scroll.c module_nest.c module_keys.c module_piano.c; do
	link_check "$SRC" "$SRC"
	# Named, not "whichever came last": this has been the wrong number twice,
	# both times because a module was added to the loop after it.
	[[ "$SRC" == module_shape.c ]] && FULL="$SIZE"
	[[ "$SRC" == module_nest.c ]] && NEST="$SIZE"
done
link_check module_shape.c "module_shape.c -DWSK_NO_NEST" -DWSK_NO_NEST
NONEST="$SIZE"
link_check module_shape.c "module_shape.c -DWSK_NO_WRAP" -DWSK_NO_WRAP
NOWRAP="$SIZE"
link_check module_shape.c "module_shape.c -DWSK_NO_TEXT" -DWSK_NO_TEXT
NOTEXT="$SIZE"
link_check module_nest.c "module_nest.c -DWSK_NO_OVER" -DWSK_NO_OVER
NOOVER="$SIZE"
link_check module_nest.c "module_nest.c -DWSK_NO_TEXTSEL" -DWSK_NO_TEXTSEL
NOSEL="$SIZE"
link_check module_nest.c "module_nest.c -DWSK_NO_UNDO" -DWSK_NO_UNDO
NOUNDO="$SIZE"
link_check module_nest.c "module_nest.c -DWSK_NO_WORDS" -DWSK_NO_WORDS
NOWORDS="$SIZE"
link_check module_nest.c "module_nest.c -DWSK_NO_SPLIT" -DWSK_NO_SPLIT
NOSPLIT="$SIZE"
link_check module_nest.c "module_nest.c -DWSK_NO_MENUKEYS" -DWSK_NO_MENUKEYS
NOMENUKEYS="$SIZE"
link_check module_nest.c "module_nest.c -DWSK_NO_HOVER" -DWSK_NO_HOVER
NOHOVER="$SIZE"
link_check module_nest.c "module_nest.c -DWSK_NO_TIP" -DWSK_NO_TIP
NOTIP="$SIZE"
link_check module_shape.c "module_shape.c -DWSK_NO_SOURCE" -DWSK_NO_SOURCE
NOSOURCE="$SIZE"
link_check module_shape.c "module_shape.c -DWSK_NO_CHECK" -DWSK_NO_CHECK
NOCHECK="$SIZE"
link_check module_shape.c "module_shape.c -DWSK_NO_MEMO" -DWSK_NO_MEMO
NOMEMO="$SIZE"
link_check module_shape.c "module_shape.c -DWSK_NO_UTF8" -DWSK_NO_UTF8
NOUTF8="$SIZE"
link_check module_shape.c "module_shape.c -DWSK_NO_WORLD" -DWSK_NO_WORLD
NOWORLD="$SIZE"
link_check module_shape.c "module_shape.c -DWSK_NO_GRID" -DWSK_NO_GRID
NOGRID="$SIZE"
link_check module_nest.c "module_nest.c -DWSK_NO_SUBMENU" -DWSK_NO_SUBMENU
NOSUBMENU="$SIZE"
link_check module_shape.c "module_shape.c -DWSK_PANEL" -DWSK_PANEL
PANEL="$SIZE"
link_check module_shape.c "module_shape.c -DWSK_MINIMAL" -DWSK_MINIMAL
MIN="$SIZE"
echo
echo "      THE TIERS (D21), measured on module_shape.c, a small panel:"
printf "        desktop, browser   (default)       %6d bytes\n" "$FULL"
printf "        touch panel, P4    -DWSK_PANEL     %6d\n" "$PANEL"
printf "        readout, C5        -DWSK_MINIMAL   %6d\n" "$MIN"
echo "      and what each part costs the default, by building without it:"
printf "        nesting %d, wrap %d, text items %d, the check %d, the memo %d, source %d,\n" \
	"$(( FULL - NONEST ))" "$(( FULL - NOWRAP ))" "$(( FULL - NOTEXT ))" "$(( FULL - NOCHECK ))" \
	"$(( FULL - NOMEMO ))" "$(( FULL - NOSOURCE ))"
printf "        UTF-8 %d, the world %d, cells %d;\n" "$(( FULL - NOUTF8 ))" "$(( FULL - NOWORLD ))" "$(( FULL - NOGRID ))"
printf "        overlays %d, selection and the clipboard %d, undo %d, words %d, dividers %d,\n" \
	"$(( NEST - NOOVER ))" "$(( NEST - NOSEL ))" "$(( NEST - NOUNDO ))" "$(( NEST - NOWORDS ))" \
	"$(( NEST - NOSPLIT ))"
printf "        menu keys %d, hover %d, tooltips %d, submenus %d,\n" "$(( NEST - NOMENUKEYS ))" "$(( NEST - NOHOVER ))" "$(( NEST - NOTIP ))" "$(( NEST - NOSUBMENU ))"
echo "        to a toolkit program (module_nest.c)."
echo "      A reduced build is the same renderer, not a second one (§7): what it"
echo "      keeps it draws byte for byte, and what it lacks it refuses by name."
echo
echo "      no libc, no .data, no .bss -- the linker script asserts the last two."
echo "      The programs: shape (flick, generated), compose (two stacks and the"
echo "      function pointer wsk_compose takes), input (a field, out of RAM),"
echo "      stat (a readout, D9's shape), font, slop (touch scatter), scroll"
echo "      (ten thousand rows, a slice at a time), nest (a window that is one"
echo "      tree, on the toolkit), keys (wkeys, two layers to fit SRC_MAX)."
