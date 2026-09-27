#!/bin/sh
# The operations against Tibor Bamhor's original GIMP 2 plug-ins, on
# generated 8 bit images (tests/orig-compare.py).
#
# Downloads the original sources (once, into tests/output/orig) and
# checks their checksums:
#   aumask-0.9.2.tgz, satequalizer-0.9.1.tgz  from the Google Code
#     archive, SHA-1 as its download list gives them (the same AUMASK
#     file is on registry.gimp.org in the Wayback Machine)
#   gimpcolorspace.c of GIMP 2.10.38  the HSV and HSL conversions that
#     the plug-ins call
# takes the computing part out of the plug-ins (tests/orig/extract.py:
# only the dialog, its callbacks, query and run are dropped) and builds
# it with tests/orig/fake-gimp.h and a harness (tests/orig/harness-*.c)
# twice: as it is, and with the changes of the operations made in the
# plug-in's own code (tests/orig/*-fixes.patch). With the Flatpak GIMP
# everything builds and runs inside its SDK (gimp-devtools/gimp-build.sh).
# Lists the user's GIMP folders before and after and fails if anything
# changed.
#
#   tests/orig-check.sh
#
# Needs the network the first time. Exits with 1 if a check failed.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=SCRIPTDIR/common.sh
. "$here/common.sh"
o=$out/orig
mkdir -p "$o"
status=0

GC=https://storage.googleapis.com/google-code-archive-downloads/v2/code.google.com

# fetch <file> <url> <sha1> <sha256>
fetch () {
    if [ ! -f "$o/$1" ]; then
        curl -sfL -o "$o/$1.part" "$2" && mv "$o/$1.part" "$o/$1" ||
          { echo "FAIL  cannot download $2"; rm -f "$o/$1.part"; return 1; }
    fi
    if [ "$(sha1sum < "$o/$1" | cut -d' ' -f1)" = "$3" ] &&
       [ "$(sha256sum < "$o/$1" | cut -d' ' -f1)" = "$4" ]; then
        echo "PASS  $1: SHA-1 $3"
    else
        echo "FAIL  $1: wrong checksum"
        rm -f "$o/$1"
        return 1
    fi
}

snapshot_begin orig
fetch aumask-0.9.2.tgz "$GC/aumask/aumask-0.9.2.tgz" \
  667916816ea10d5cf1c4204af6737cfcd42646f4 \
  5972437485ec0be3e7f0d2a1698d632cfdd0e97fa5ad71a897fb3c974131a86a || status=1
fetch satequalizer-0.9.1.tgz "$GC/satequalizer/satequalizer-0.9.1.tgz" \
  a40b0fc917eaaea24514339d459b8367063e6aac \
  879fcd2ded007baf75b2ff8aacc26f61ff1efb8e93e466bff3a72432b90d0d70 || status=1
fetch gimpcolorspace.c \
  "https://gitlab.gnome.org/GNOME/gimp/-/raw/GIMP_2_10_38/libgimpcolor/gimpcolorspace.c" \
  f948f74a0e048f3ed786498fd1cf8f65d4b01203 \
  716ee40b1276847e32aa52a1e4f37212bf5f470d6099bfe2adaf9559f67cc87f || status=1
[ $status = 0 ] || { snapshot_end orig; exit 1; }

# the sources: as they are (orig) and with the fixes (fixed)
rm -rf "$o/src" "$o/orig" "$o/fixed"
mkdir -p "$o/src" "$o/orig" "$o/fixed"
tar -xzf "$o/aumask-0.9.2.tgz" -C "$o/src" &&
  tar -xzf "$o/satequalizer-0.9.1.tgz" -C "$o/src" || status=1
cp "$o/src/aumask/aumask.c" "$o/src/satequalizer/saturate.c" "$o/orig/"
cp "$o/orig/aumask.c" "$o/orig/saturate.c" "$o/fixed/"
patch -s -p1 -d "$o/fixed" < "$here/orig/aumask-fixes.patch" &&
  patch -s -p1 -d "$o/fixed" < "$here/orig/sateq-fixes.patch" ||
  { echo "FAIL  the fixes do not apply"; status=1; }

drop_aumask="query run exportwrapper cb_changed cb2_changed cb3_changed slider_update
  export_to_layer sharp_dialog free_memory response_callback"
drop_sateq="query run exportwrapper slider_update coupled_changed alg_changed
  export_to_layer sat_dialog response_callback"
python3 "$here/orig/extract.py" keep "$o/gimpcolorspace.c" "$o/gimp2-color.c" \
  gimp_rgb_to_hsv gimp_hsv_to_rgb gimp_rgb_to_hsl gimp_hsl_value gimp_hsl_to_rgb || status=1
for v in orig fixed; do
    # shellcheck disable=SC2086
    python3 "$here/orig/extract.py" drop "$o/$v/aumask.c" "$o/$v/aumask-core.c" $drop_aumask &&
      python3 "$here/orig/extract.py" drop "$o/$v/saturate.c" "$o/$v/sateq-core.c" $drop_sateq ||
      status=1
    printf '#include "aumask-core.c"\n#include "harness-aumask.c"\n' > "$o/$v/aumask-run.c"
    printf '#include "sateq-core.c"\n#include "harness-sateq.c"\n' > "$o/$v/sateq-run.c"
done
[ $status = 0 ] || { snapshot_end orig; exit 1; }

# built as gimptool-2.0 built them (no -O, so the plug-ins' reads past
# the end of an array stay plain reads); inline as in GCC before 5
cflags="-std=gnu99 -fgnu89-inline -O0 -w -pthread"
for v in orig fixed; do
    for p in aumask sateq; do
        in_sdk "cc $cflags -Itests/output/orig/$v -Itests/orig -Itests/output/orig \
          tests/output/orig/$v/$p-run.c tests/output/orig/gimp2-color.c \
          \$(pkg-config --cflags --libs glib-2.0) -lm -o tests/output/orig/$v/$p" ||
          { echo "FAIL  cannot build the $v $p"; status=1; }
    done
done

# the operations' side: eq-apply of the usual build
build=tests/output/build-check
if [ ! -f "$out/build-check/build.ninja" ]; then
    in_sdk meson setup "$build" -Dwarning_level=2 >/dev/null || status=1
fi
in_sdk ninja -C "$build" >/dev/null || status=1
[ $status = 0 ] || { snapshot_end orig; exit 1; }

in_sdk "EQ_ORIG=tests/output/orig EQ_BUILD=$build python3 tests/orig-compare.py" \
  >"$o/compare.log" 2>&1 || status=1
grep -E '^(PASS|FAIL|NOTE)|^  |Traceback|Error' "$o/compare.log"

snapshot_end orig || status=1
echo
[ $status = 0 ] && echo "orig: all checks passed" || echo "orig: SOME CHECKS FAILED"
exit $status
