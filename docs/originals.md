# The original plug-ins, option by option

What Tibor Bamhor's two GIMP 2 plug-ins do, read from their source code
(not from their descriptions), so that the ports can be checked against
them. `tests/orig-check.sh` downloads exactly these files and runs their
code.

| | Saturation Equalizer | Advanced Unsharp Mask (AUMASK) |
|---|---|---|
| Source | `saturate.c` in `satequalizer-0.9.1.tgz` | `aumask.c` in `aumask-0.9.2.tgz` |
| Where | code.google.com/p/satequalizer (Google Code archive), registry.gimp.org/node/25300 | code.google.com/p/aumask (Google Code archive), registry.gimp.org/node/25326 |
| Checksum | SHA-1 a40b0fc917eaaea24514339d459b8367063e6aac, as in the archive's download list | SHA-1 667916816ea10d5cf1c4204af6737cfcd42646f4, as in the archive's download list; the registry's copy (Wayback Machine) is the same file |
| Date | 2013-06-14 (0.9.1); 0.9.2 of 2016-05-06 on github.com/tibor95/gimp-plugin-satequalizer differs only in `static inline` for two functions and `g_thread_new` for `g_thread_create` | 2013-06-14 |
| Licence | "GPL v.3": the procedure's copyright field, README.txt, the GitHub repository's LICENSE (the GPL 3 text), the Google Code project (`gpl3`), the registry page ("Code License: GPLv3") | the source says only "Copyright Tibor Bamhor"; the Google Code project (`gpl3`) and the author's registry page ("Code License: GPLv3", 2014 snapshot) say GPL 3 |
| Menu | Filters > Enhance > Saturation Equalizer, RGB and RGBA | Filters > Enhance > AUMASK sharpening, RGB and RGBA |
| Scripting | none that works: the procedure takes only run mode, image and drawable; non-interactive runs use the defaults (it sets a calling error but still returns success), and "last values" are never stored (no `gimp_set_data`) | none: always shows the dialog |

Neither version number says "or later", so the GPL applies in version 3
(its section 14); these ports are GPL-3.0-only for that reason.

## Common to both

- They read the selection's bounding box (the whole layer without a
  selection) as 8 bit values v, and work on "gammed" values
  `pow (v / 255, 1 / 2.2)`, computed in float (`gammed_values[256]`), with
  the arithmetic in float and double as C gives it. They write
  `pow (x, 2.2) * 255`, rounded, and clipped to 0 to 255; a negative x
  gives NaN in `pow`, which their rounding turns into 0 on x86.
  (AUMASK's HSV and HSL modes are the exception, below.)
- Alpha is copied.
- They run in 3 (AUMASK) or 4 (Saturation Equalizer) threads over rows
  or columns; every pixel's result depends only on the input, so the
  threads change nothing.
- Each has a preview, Reset, Cancel, OK, and "Export as new layer"
  (the result as a new layer named "SatEq" or "AUMask", the dialog stays
  open).

## Saturation Equalizer

Per pixel, in this order (`block_saturate`):

1. **Color Auto Balance** (expander): "Calculate and Apply" averages the
   gammed R, G and B of every second pixel in each direction of the
   **preview** (so of what the preview is zoomed to), in float, and stores
   each channel's mean minus the mean of the three ("RGB shift: r, g, b").
   Every pixel then gets that shift times **Weight** (0 to 1, default 1)
   subtracted. Nothing happens until the button is pressed; Reset clears
   it.
2. **Brightness adjustment (~levels)**: **Dark** (-0.4 to 0.8, default 0)
   and **Light** (0.5 to 1.4, default 1). With the pixel's brightness b
   (in the color mode, below), `dark + (light - dark) * b - b` is added
   to R, G and B.
3. **Per-Corner Brightness** (expander): four spin buttons, -0.2 to 0.2,
   default 0, for the top left, top right, bottom left and bottom right
   corner. The amounts are interpolated, first down the left and right
   edges by y / height, then across by x / width, and added to R, G and B.
4. **Color Temperature modification** (expander): a slider t from -1
   (colder) to 1 (warmer), default 0. If not 0: R + 0.12 t, G + 0.05 t,
   B - 0.17 t, then twice: add (old brightness - new brightness) to R, G
   and B.
5. Brightness b and saturation s of the result, by **Color Mode**:
   - **Quadratic** (default): b = sqrt (0.241 R^2 + 0.691 G^2 + 0.068 B^2)
     (a channel outside 0 to 1 counts linearly, 0.241 R; no square root
     for a sum of 0 or less); s = (|b - R| + |b - G| + |b - B|) / 3
   - **Safe Quadratic**: the same
   - **HSV**, **HSL**: GIMP 2's `gimp_rgb_to_hsv` or `gimp_rgb_to_hsl`
     (double precision): b = V or L, s = S, also the hue
   - **YUV**: b = Y = 0.299 R + 0.587 G + 0.114 B,
     U = -0.147 R - 0.289 G + 0.436 B, V = 0.615 R - 0.515 G - 0.100 B,
     s = sqrt (U^2 + V^2)
6. The factor for s, from the **six vertical sliders** (no labels; the
   header's tooltip: "Sliders works like a saturation curve, the amount
   is a multiplicator of current saturation"). Their ranges are 0 to 4,
   3.75, 3.5, 3.25, 3 and 2.75, default 1. They are the factor at
   saturations 0, 0.11, 0.21, 0.43, 0.71 and 1.43 times the mode's
   "relative saturation" (0.07 Quadratic, 0.24 HSV, 0.55 HSL, 0.17 YUV,
   0.09 Safe Quadratic); a seventh value, 1, sits at 14.29 times it;
   linear in between; 1 beyond.
7. **Limit saturation to color** (expander): a slider from -0.08 to 0.08
   (default 0, off) and a color button (default 43010, 28305, 24990 of
   65535, a skin tone; "Brightness of selected color is ignored").
   Right of 0 (r = the slider): with d the distance of the pixel's color
   from the picked one, the factor is kept for d up to r, faded to 1
   between r and max (2 r, r + 0.05), and 1 beyond ("Restrict the
   saturation to selected color"). Left of 0 is meant as the inverted
   selection. The distance, as the code runs: the hue difference in HSV
   mode (not round the circle); the UV distance in every other mode (see
   the bugs).
8. The saturation is multiplied, by mode:
   - **Quadratic**: R, G, B = b + (c - b) * factor, then three rounds of
     adding the brightness lost
   - **Safe Quadratic**: the same, then shifted up if a channel is below
     0 and down if above 1 (see the bugs)
   - **HSV**, **HSL**: S = min (1, s * factor), back with GIMP 2's
     conversion
   - **YUV**: U and V times the factor, R = Y + 1.140 V,
     G = Y - 0.395 U - 0.581 V, B = Y + 2.032 U
9. `pow (x, 2.2)`, 8 bits.

Also in the dialog, with no effect on the pixels:

- **Sliders coupling**: "Sliders coupled" (off by default) and
  "Allowed difference" (0 to 8, default 0.6): moving a slider pulls its
  neighbours to within that difference.
- **Saturation statistics (in preview)**: lowest, average and highest
  saturation of the preview's pixels (each pixel's value is taken before
  it is computed, so it lags by one pixel).

Bugs found in the code:

1. The limit's distance is picked by `if (Quadratic) {...}` followed by
   `if (HSV) ... else if (HSV) ... else YUV`: Quadratic's own distance is
   computed and then overwritten by the YUV distance, and the second test
   was meant to be HSL, so HSL also gets the YUV distance.
2. The hue distance is |h1 - h2|, not round the circle, so a red of hue
   0.98 is "far" from one of 0.02.
3. The picked color's hue comes from its raw 8 bit values, the pixels'
   hues from the gammed values.
4. The inverted range does not work: the neighbourhood (`skinuplimit`)
   and the picked color's values are only computed for a slider above 0,
   so left of 0 they are 0 (nothing changes) or left over from an earlier
   positive setting (then the fade runs the wrong way).
5. Safe Quadratic shifts R, then takes the minimum again for G and again
   for B, so the three move by different amounts and the hue changes.
6. The color button's 16 bit values are divided by 65536, not 65535.
7. The "slider coupling" loop writes `boost[7]`, one past the end of the
   array (into the next member, which is recomputed; harmless).

## Advanced Unsharp Mask (AUMASK)

1. **Brightness/mask source**, the brightness B of each pixel:
   - **Brightness 0.299/0.587/0.114** (default): 0.299 R + 0.587 G +
     0.114 B of the gammed values
   - **Value in HSV**, **Lightness in HSL**: V or L of GIMP 2's
     conversion of v / 255, **not gammed**
   - **R to brightness**, **G to brightness**, **B to brightness**: that
     channel, gammed
2. **Mask options**, the mask M: a blur of B.
   - **Radius(px)**: 1 to 500, default 20. The blur is up to 15 passes;
     each pass takes about 0.45 of the radius still left, at most
     3 k + 1 in pass k (so 4, 7, 10 ...), at least 1, until the radius is
     used up; what is left after 15 passes goes to the first. Radius 20
     is 4, 7, 4, 2, 1, 1, 1. Each pass sets a pixel to the mean of itself
     and points on a circle of the pass's radius: for y from -r to r in
     steps of r / 6 + 1 the point (round (sqrt (r^2 - y^2)), y) and the
     mirrored one, about 24 points. Points outside the selection's
     bounding box are left out.
   - **Standard blur (no treshold)** (default): all points count.
   - **Selective blur (treshold)**: points whose value differs from the
     pixel's by more than a threshold are left out; the threshold is
     min (t, 2 (k + 1) / passes * t) in pass k, t is **Treshold** (0.02
     to 1, default 0.2). After the last pass M is clamped to B +- t.
   - **Selective with postblur**: the same selective blur without the
     final clamp, then the mask blurred again with **Postblur radius**
     (0 to 10, default 3; 0 is a radius of 1) and threshold 1, still
     selective (so the first postblur passes, with thresholds below 1,
     leave out very different points).
3. **Brightness adjustment (~levels)**: **Dark** (-0.4 to 0.7, default 0)
   and **Light** (0.5 to 1.4, default 1): M' = dark + M * (light - dark).
   "Modifies brightness via mask, so that local contrast is preserved."
4. **Sharpness equalizer**: four vertical sliders, 0 to 2.5, default 1,
   with the tooltips "Controls the distance 0.0075", "0.03", "0.07",
   "0.20". The factor f for d = |B - M| is interpolated between the
   values at d = 0, 0.0075, 0.03, 0.07 and 0.20 (the four sliders and a
   hidden fifth value), and from the fifth value at 0.20 to 0 at 1.1 (see
   the bugs). The sliders are always coupled: a neighbour follows when
   the difference exceeds 0.3, and so does the hidden fifth value, which
   starts at 1.
5. The new brightness B' = M' + (B - M) * f, and the color:
   - own brightness: R' = B' + (R - B), the same for G and B (gammed)
   - R, G or B source: the same with B recomputed as
     0.299 R + 0.587 G + 0.114 B: the pixel's brightness becomes the
     sharpened channel ("R to brightness"); even at the defaults this
     changes the colors
   - HSV and HSL: V or L set to B' clipped to 0 to 1, back with GIMP 2's
     conversion, **written without pow (x, 2.2)**
6. **Output mode** ("Selected mode affects preview as well as final
   image"):
   - **Standard (Image)** (default): the sharpened image
   - **Mask**: M' as gray (pow (x, 2.2) except in HSV and HSL)
   - **Split (comparison)**: the left half unchanged, a column of
     inverted pixels, the right half sharpened; also on OK
   - **Difference (Mask-to-Img)**: (B - M) * **Diff Boost** (-10 to 10,
     default 2) + **Central Brightness** (0 to 1, default 0.5), clipped
     to 0 to 1, as gray (pow (x, 2.2) except in HSV and HSL)

Reset sets everything back except the brightness source.

Bugs found in the code:

1. `get_new_sharp` interpolates up to `boost[5]`, one past the end of
   the five element array; it reads the next struct member,
   `borders[0]`, which is 0. So the factor falls from the fifth value at
   a difference of 0.20 to 0 at 1.1: at the defaults, which should leave
   the image as it is, strong edges are softened (in the tests up to 63
   levels at a black and white edge). It is undefined behaviour in C,
   so an optimising compiler could do something else; gimptool-2.0
   compiled without `-O` (the compile line a user posted on the registry
   page in 2013 has none), and the tests build it the same way.
2. The hidden fifth value follows the fourth slider only when that moves
   more than 0.3 away from it, so it depends on the dialog's history.
3. The coupling loop going down writes `boost[-1]`, before the array.
4. Split mode applies to the image on OK, inverted line included.
5. The postblur mask is allocated each time and never freed.

## What the ports keep and change

See README.md, "Changes from the plug-ins". In short: every option that
changes pixels is kept with the same range and default, in the same
arithmetic; the bugs above that change pixels are fixed (each as a patch
to the original code in `tests/orig/*-fixes.patch`, so that the
comparison shows each fix alone); dialog helpers that GIMP 3 has itself
(preview, split view, export as a layer, reset) or that only moved
sliders (coupling, statistics) are not ported.
