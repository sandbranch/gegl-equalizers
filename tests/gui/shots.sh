#!/bin/sh
# The filters' dialogs in GIMP on a Broadway display, driven with a
# headless Chrome, for the screenshots in docs/: the Colors menu with
# Saturation Equalizer, Filters > Enhance with Advanced Unsharp Mask,
# both dialogs with settings changed and GIMP's split view; then
# Advanced Unsharp Mask applied with OK, the image saved, and the XCF
# checked in headless GIMP: the layer keeps the filter, live. Run
# tests/gimp-check.sh first (it puts the modules in place). Lists the
# user's folders before and after and fails if anything changed.
#
#   tests/gui/shots.sh                      all
#   EQ_SHOTS="sat au" tests/gui/shots.sh    some (colors enhance sat au live)
#
# Prints PASS or FAIL per step and exits non-zero if one failed.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
# a page tall enough for the dialogs without scrolling
EQ_VIEW=${EQ_VIEW:-1280,1150}
export EQ_VIEW
PW=${EQ_VIEW%,*}
PH=${EQ_VIEW#*,}
# shellcheck source=SCRIPTDIR/common.sh
. "$here/common.sh"
docs=$top/docs
mkdir -p "$docs"
status=0
pass () { echo "PASS  gui_$1"; }
fail () { echo "FAIL  gui_$1"; status=1; }

# the dialog: the topmost canvas wider than a menu (Broadway keeps a
# closed menu's canvas) and smaller than the page; Broadway draws each
# window as a canvas with its shadow (26 pixels here), the title bar's
# middle 36 pixels below the canvas's top
dialog_rect () {
    $cdp "$view" "eval:(() => { const c = [...document.querySelectorAll('canvas')]
      .map(e => [e.getBoundingClientRect(), Number(e.style.zIndex) || 0])
      .filter(([r, z], i, all) => r.width > 300 && r.width < $((PW - 60)) && r.height > 150 && r.height < $((PH - 30)) &&
              getComputedStyle(all[i] ? document.querySelectorAll('canvas')[i] : null).display !== 'none')
      .sort((a, b) => b[1] - a[1])[0];
      return c ? [c[0].left, c[0].top, c[0].width, c[0].height].map(Math.round).join(',') : '' })()" \
      2>/dev/null | tail -1
}

# moves the dialog to the top right corner of the page; sets vx, vy (its
# visible top left corner) and vw (its visible width)
move_dialog () {
    tries=0
    while [ $tries -lt 3 ]; do
        r=$(dialog_rect)
        [ -n "$r" ] || return 1
        l=${r%%,*}; r=${r#*,}; t=${r%%,*}; r=${r#*,}; w=${r%%,*}
        vw=$((w - 52))
        vx=$((PW - 2 - vw))
        vy=${EQ_DIALOG_Y:-70}
        # there already (the shadow is 26 pixels)
        [ $((l + 26 - vx)) -ge -3 ] && [ $((l + 26 - vx)) -le 3 ] && return 0
        gx=$((l + w / 2)); gy=$((t + 36))
        $cdp "$view" move:$gx,$gy wait:300 down:$gx,$gy wait:300 \
          move:$(( (gx + vx + vw / 2) / 2 )),$(( (gy + vy + 10) / 2 )) wait:200 \
          move:$((vx + vw / 2)),$((vy + 10)) up:$((vx + vw / 2 + 1)),$((vy + 10)) wait:1200 >/dev/null
        tries=$((tries + 1))
    done
    return 1
}
# a point in the dialog, from its visible top left corner
p () { echo "$((vx + $1)),$((vy + $2))"; }
# the x of value v on a slider from lo to hi (the bar starts 7 pixels in
# and ends 48 before the dialog's right edge)
# (in hundredths: slider 280 0 375 is 2.8 on a slider from 0 to 3.75)
slider () { echo $(( 7 + ($1 - $2) * (vw - 55) / ($3 - $2) )); }

MENU=321,49
# Filters, then Enhance (its submenu opens on hover, then Right)
FILTERS="click:426,49 wait:1200 move:450,176 wait:600 move:456,175 wait:1500 key:Right wait:1200"
SAT_ITEM=393,753
AU_ITEM=749,500
# Advanced Unsharp Mask: the four amounts for small to large differences
au_amounts () {
    echo click:"$(p "$(slider 200 0 250)" 165)" wait:1500 \
      click:"$(p "$(slider 220 0 250)" 193)" wait:1500 \
      click:"$(p "$(slider 200 0 250)" 221)" wait:1500 \
      click:"$(p "$(slider 160 0 250)" 249)" wait:1500
}
# open_filter "<steps to the menu>" <item> <name>
open_filter () {
    # shellcheck disable=SC2086
    $cdp "$view" $1 click:"$2" wait:4000 >/dev/null
    move_dialog || { fail "$3: no dialog"; return 1; }
    pass "$3: the dialog opened"
}

session () {
    name=$1
    tries=0
    until start_gimp "$name"; do
        stop_all
        tries=$((tries + 1))
        [ $tries -lt 3 ] || { fail "$name: GIMP did not start (log: $gui_out/gimp-$name.log)"; return; }
    done
    case $name in
    colors)
        $cdp "$view" click:$MENU wait:1200 shot:"$docs/colors-menu.png" >/dev/null &&
          pass "colors: the Colors menu with Saturation Equalizer";;
    enhance)
        # shellcheck disable=SC2086
        $cdp "$view" $FILTERS shot:"$docs/enhance-menu.png" >/dev/null &&
          pass "enhance: Filters > Enhance with Advanced Unsharp Mask";;
    sat)
        open_filter "click:$MENU wait:1000" "$SAT_ITEM" sat || return
        # the first four bands up (the upper half of a slider sets its
        # value where it is clicked), then the split view
        $cdp "$view" click:"$(p "$(slider 350 0 400)" 106)" wait:1500 \
          click:"$(p "$(slider 330 0 375)" 135)" wait:1500 \
          click:"$(p "$(slider 280 0 350)" 164)" wait:1500 \
          click:"$(p "$(slider 200 0 325)" 192)" wait:1500 \
          click:"$(p "$(slider 140 0 300)" 220)" wait:1500 \
          click:"$(p 360 642)" wait:6000 shot:"$docs/saturation-equalizer.png" >/dev/null &&
          pass "sat: bands raised, split view";;
    au)
        open_filter "$FILTERS" "$AU_ITEM" au || return
        # shellcheck disable=SC2046
        $cdp "$view" $(au_amounts) click:"$(p 400 474)" wait:6000 \
          shot:"$docs/advanced-unsharp-mask.png" >/dev/null &&
          pass "au: amounts raised, split view";;
    live)
        open_filter "$FILTERS" "$AU_ITEM" live || return
        # shellcheck disable=SC2046
        $cdp "$view" $(au_amounts) wait:3000 click:"$(p 450 509)" wait:4000 \
          shot:"$docs/advanced-unsharp-mask-applied.png" >/dev/null
        touch "$gui_out/save"
        if wait_for "$gui_out/saved" 60; then
            pass "live: the image saved with the filter"
        else
            fail "live: not saved (log: $gui_out/gimp-live.log)"
        fi;;
    esac
    stop_all
}

snapshot_begin gui
for s in ${EQ_SHOTS:-colors enhance sat au live}; do
    session "$s"
done

# the layer's filter in the saved XCF, in headless GIMP
xcf=$gui_out/gui-layer.xcf
if [ -f "$xcf" ]; then
    GIMP3_DIRECTORY="$out/gimp-profile"
    GEGL_PATH="$out/gimp-check-modules:/app/lib/gegl-0.4"
    export GIMP3_DIRECTORY GEGL_PATH
    pass_env="GIMP3_DIRECTORY GEGL_PATH"
    run_prefix="--timeout=600" in_gimp gimp-console-3.2 --no-interface --no-data --no-fonts \
      --batch-interpreter python-fu-eval -b "
import gi
gi.require_version('Gimp', '3.0')
from gi.repository import Gimp, Gio
img = Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path('$xcf'))
for l in img.get_layers():
    for f in l.get_filters():
        c = f.get_config()
        print('FILTER', l.get_name(), f.get_operation_name(), c.get_property('amount-3'))
" --quit >"$gui_out/xcf-check.log" 2>&1
    if grep -q "^FILTER picture equalizer:sharpness" "$gui_out/xcf-check.log"; then
        pass "live: after loading the XCF the layer has the live filter: $(grep FILTER "$gui_out/xcf-check.log")"
    else
        fail "live: the XCF's layer: $(grep FILTER "$gui_out/xcf-check.log")"
    fi
fi

snapshot_end gui || status=1
[ $status = 0 ] && echo "gui: all passed" || echo "gui: FAILED (screenshots in docs, logs in $gui_out)"
exit $status
