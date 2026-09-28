# Plan

## Done

`equalizer:saturation` (Saturation Equalizer, Colors) and
`equalizer:sharpness` (Advanced Unsharp Mask, Filters > Enhance), ports
of Tibor Bamhor's GIMP 2 plug-ins as non-destructive GEGL filters, with
the originals' options, ranges, defaults and arithmetic, and their bugs
fixed (README, "Changes from the plug-ins"; docs/originals.md for the
originals option by option). Tested with tests/orig-check.sh (the
originals' own code, downloaded and checksum-verified, as it is and with
the fixes: every pixel within 1 level of the fixed original, 99.987 %
identical), tests/check.sh (83 checks, also under ASan and UBSan),
tests/gimp-check.sh (GIMP 3.2.6 Flatpak: every precision, grayscale,
XCF, the command line), tests/gui/shots.sh (the menus and dialogs on
Broadway, looked at, and a live filter through an XCF), tests/bench.sh.

## Next

1. Publish: github.com/sandbranch/gegl-equalizers (not pushed yet), and a
   note to Tibor Bamhor (tiborb95 at gmail, github.com/tibor95) with the
   list of bugs found, in case he wants to fix the GIMP 2 versions or
   bless the port.
2. Zoomed-out previews: the operations ignore GEGL's mipmap level, as
   the other sandbranch area filters do; AUMASK's radius is then in
   pixels of the reduced image. Scale the radius by the level (the ring
   of points changes, so the preview would still differ slightly), or
   check whether GIMP 3.2 asks area filters for levels at all.
3. AUMASK with a radius in the hundreds is slow per tile, because each
   of GEGL's chunks recomputes a margin of the whole radius (1.8 s for
   24 megapixels at radius 500 with 24 threads, 17 s with one). A
   whole-layer cache (as for the Saturation Equalizer's balance) would do
   the blur once; worth it only if large radii turn out to be used.
4. The Saturation Equalizer's colors (the limit and the balance) have no
   on-canvas area picker beyond GIMP's picker with sample average; an
   "area" balance like the original's zoomed preview would need a
   rectangle in the dialog.
5. Translations: the strings are marked for gettext (GETTEXT_PACKAGE
   gegl-equalizers) but there is no po folder yet.

## Found along the way

- GIMP 3.2.6's filter dialog leaves out an operation property named
  `output` (the name of the output pad) without a word; AUMASK's output
  choice is `output-mode` for that reason. Worth a GIMP issue or a note
  in GEGL's operation docs.
- The gegl command line of GEGL 0.4.72 counts one leaked GeglBuffer at
  its exit for some operations, GEGL's own gegl:unsharp-mask and
  gegl:stretch-contrast among them (tests/gimp-check.sh notes it); the
  same operations in a program that frees its graph leak nothing.
