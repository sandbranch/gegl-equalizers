#!/bin/sh
# The operations as non-destructive filters in the Flatpak GIMP, without
# a window (tests/gimp-check.py): on RGB images of every precision and a
# grayscale one, through an XCF save and load, against plain GEGL; then
# GIMP's results against the gegl command line. Uses the modules from
# tests/output/build-check (or $BUILD; tests/check.sh builds it), a
# throwaway GIMP profile in tests/output/gimp-profile (GIMP3_DIRECTORY)
# and a throwaway home (tests/common.sh), so the installed filters and
# the user's GIMP settings are not touched; the user's GIMP folders are
# listed before and after and must not change. GIMP loads no fonts
# (--no-fonts). The first run takes about a minute (GIMP sets up the new
# profile). Exits non-zero if a check fails.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=SCRIPTDIR/common.sh
. "$here/common.sh"
build=${BUILD:-tests/output/build-check}
case $build in /*) ;; *) build=$top/$build ;; esac
check_out="$out/gimp-check"
mod="$out/gimp-check-modules"
for m in saturation-equalizer advanced-unsharp-mask; do
    [ -f "$build/$m.so" ] || { echo "no $build/$m.so: run tests/check.sh first" >&2; exit 2; }
done
rm -rf "$mod" "$check_out"
mkdir -p "$mod" "$check_out" "$out/gimp-profile"
# GEGL loads every file in a module folder: only the modules
cp "$build"/saturation-equalizer.so "$build"/advanced-unsharp-mask.so "$mod/"

status=0
snapshot_begin gimp

GIMP3_DIRECTORY="$out/gimp-profile"
GEGL_PATH="$mod:/app/lib/gegl-0.4"
EQ_CHECK_OUT="$check_out"
export GIMP3_DIRECTORY GEGL_PATH EQ_CHECK_OUT
pass_env="GIMP3_DIRECTORY GEGL_PATH EQ_CHECK_OUT"
# the Flatpak's GIMP unless GIMP_FLATPAK=0 (as gimp-run.sh chooses)
if [ "${GIMP_FLATPAK:-}" != 0 ] && command -v flatpak >/dev/null 2>&1 &&
   flatpak info org.gimp.GIMP >/dev/null 2>&1; then
    console=gimp-console-3.2
else
    console=$(command -v gimp-console-3.2 || command -v gimp-console)
fi

run_prefix="--timeout=1800" in_gimp $console --no-interface --no-data --no-fonts \
  --batch-interpreter python-fu-eval \
  -b "exec(open('$here/gimp-check.py').read())" --quit >"$out/gimp-check.log" 2>&1
grep -E "^(PASS|FAIL|NOTE)|failed$|Traceback|^  File|Error" "$out/gimp-check.log"
[ "$(cat "$check_out/gimp-check.status" 2>/dev/null)" = 0 ] || status=1

# the same settings on the gegl command line, on the scene GIMP saved
python3 - "$check_out/cases.json" > "$check_out/cli.txt" <<'EOF' || status=1
import json, sys
for label, c in json.load(open(sys.argv[1])).items():
    props = []
    for k, v in c['props'].items():
        if isinstance(v, bool):
            v = 'true' if v else 'false'
        # the gegl command line takes GEGL's own names, with hyphens
        props.append('%s=%s' % (k.replace('_', '-'), v))
    print(label, c['op'], ' '.join(props))
EOF
while read -r label op props; do
    # shellcheck disable=SC2086
    in_gimp gegl "$check_out/scene.tif" -o "$check_out/cli-$label.tif" -- $op $props \
      >"$check_out/cli-$label.log" 2>&1 ||
      { echo "FAIL  gegl command line for $label"; status=1; }
done < "$check_out/cli.txt"
# GEGL's own count of buffers left at its exit. The gegl command line
# (GEGL 0.4.72 of the Flatpak) reports one for some of its own
# operations too (gegl:unsharp-mask, gegl:stretch-contrast), so a count
# here is checked against GEGL's gegl:unsharp-mask on the same image;
# tests/check.sh counts leaked buffers of the operations themselves
in_gimp gegl "$check_out/scene.tif" -o "$check_out/cli-reference.tif" -- \
  gegl:unsharp-mask std-dev=3 >"$check_out/cli-reference.log" 2>&1
for log in "$check_out"/cli-*.log; do
    [ "$log" = "$check_out/cli-reference.log" ] && continue
    if grep -q 'GeglBuffers leaked' "$log"; then
        if grep -q 'GeglBuffers leaked' "$check_out/cli-reference.log"; then
            echo "NOTE  the gegl command line counts a leaked buffer at its exit for $(basename "$log" .log), as for GEGL's own gegl:unsharp-mask"
        else
            echo "FAIL  the gegl command line leaked buffers: $log"
            status=1
        fi
    fi
done

in_gimp python3 - "$check_out" <<'EOF' || status=1
import array, json, os, sys
import gi
gi.require_version('Gegl', '0.4')
from gi.repository import Gegl

Gegl.init(None)
out = sys.argv[1]
bad = 0

def load(path):
    g = Gegl.Node()
    n = g.create_child('gegl:tiff-load')
    n.set_property('path', path)
    r = n.get_bounding_box()
    buf = Gegl.Buffer.new("R'G'B'A float", r.x, r.y, r.width, r.height)
    w = g.create_child('gegl:write-buffer')
    w.set_property('buffer', buf)
    n.link(w)
    w.process()
    del g, n, w
    return array.array('f', buf.get(r, 1.0, "R'G'B'A float", Gegl.AbyssPolicy.NONE))

for label in json.load(open(os.path.join(out, 'cases.json'))):
    a = load(os.path.join(out, 'gimp-%s.tif' % label))
    b = load(os.path.join(out, 'cli-%s.tif' % label))
    d = max(abs(p - q) for p, q in zip(a, b)) if len(a) == len(b) and a else float('inf')
    ok = d <= 1e-5
    bad += not ok
    print('%s  gimp_%s_same_as_gegl_command_line: max difference %.2g'
          % ('PASS' if ok else 'FAIL', label, d))
# the graphs and buffers go before GEGL counts what is left at its exit
import gc
gc.collect()
raise SystemExit(bad)
EOF

snapshot_end gimp || status=1
exit $status
