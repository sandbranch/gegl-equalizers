/*
 * Saturation Equalizer: Tibor Bamhor's GIMP 2 plug-in as a GEGL
 * operation
 *
 * saturation-equalizer.c
 * Copyright 2011-2016 Tibor Bamhor (the plug-in saturate.c 0.9.1/0.9.2)
 * Copyright 2026 by David (this port)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * The plug-in (saturate.c, "GPL v.3", satequalizer 0.9.1 of 2013 and
 * 0.9.2 of 2016 on github.com/tibor95/gimp-plugin-satequalizer, which
 * differ only in two thread calls) changes each pixel's saturation by a
 * factor that depends on the pixel's own saturation: six sliders set the
 * factor at six saturations, like an equalizer, so that gray and weak
 * colors can get more saturation than colors that are strong already.
 * Before that it can shift the brightness like Levels, per corner, the
 * color temperature and a color cast; the effect can be limited to
 * colors near a picked one, or to the colors away from it.
 *
 * Each pixel, in the order of the plug-in (block_saturate):
 *
 *   1. R, G, B = pow (value, 1 / 2.2)
 *   2. color auto balance: minus the cast times its weight
 *   3. levels: brightness b becomes dark + (light - dark) * b, by adding
 *      the same amount to R, G and B
 *   4. per-corner brightness: an amount interpolated between the four
 *      corners of the layer, added to R, G and B
 *   5. temperature t: R + 0.12 t, G + 0.05 t, B - 0.17 t, then twice
 *      the brightness restored by adding the difference to R, G and B
 *   6. brightness and saturation in the color mode, the factor from the
 *      six bands, limited to the picked color's neighbourhood if asked
 *   7. the saturation times the factor, in the color mode
 *   8. pow (x, 2.2)
 *
 * Color modes (brightness; saturation; how the factor is applied):
 *
 *   Quadratic       sqrt (0.241 R^2 + 0.691 G^2 + 0.068 B^2) (linear
 *                   terms outside 0 to 1); the mean of |R - b|, |G - b|,
 *                   |B - b|; R, G, B moved away from b by the factor,
 *                   three rounds of restoring the brightness
 *   Safe Quadratic  the same, then shifted into 0 to 1 when possible
 *   HSV, HSL        GIMP's V or L; S; S times the factor, at most 1
 *   YUV             Y = 0.299 R + 0.587 G + 0.114 B; |UV|; U, V times
 *                   the factor
 *
 * The factor at the saturation s: the six bands are points at 0, 0.11,
 * 0.21, 0.43, 0.71 and 1.43 times the color mode's typical saturation
 * (0.07 quadratic, 0.09 safe quadratic, 0.24 HSV, 0.55 HSL, 0.17 YUV),
 * a seventh point is 1 at 14.29 times it, linear in between, 1 beyond.
 *
 * Changes from the plug-in, each marked "Change" below and listed in
 * README.md (tests/orig-check.sh shows that the operation gives the
 * plug-in's results, and those of the plug-in with these changes made
 * in its own code):
 *
 *   - float in and out, no 8 bit rounding, values below 0 and above 1
 *     kept (the plug-in clipped at its 8 bit output)
 *   - Limit to color: the distance to the picked color is that of the
 *     color mode (the plug-in used the YUV distance for Quadratic and
 *     HSL too, by a slip in an if/else chain), hue distances go round
 *     the hue circle, the picked color's hue is taken from the same
 *     pow (1 / 2.2) values as the pixels', and the inverted range (the
 *     left half of the slider) works: in the plug-in it used an unset
 *     neighbourhood and changed nothing, or the wrong colors
 *   - Safe Quadratic shifts R, G and B by the same amount (the plug-in
 *     took a new minimum after each channel, so the others moved less)
 *   - Color auto balance from the whole layer, in double precision, or
 *     from a picked color (the plug-in averaged every second pixel of
 *     its preview in float, from the area the preview was zoomed to)
 *   - per-corner brightness over the layer (the plug-in: the selection's
 *     bounding box); GIMP's selection masks the result as usual
 *   - not ported, GIMP 3 does them: the preview and its saturation
 *     statistics, "Export as new layer", the Reset button; and the
 *     slider coupling of the dialog, which only moved the other sliders
 */

#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

enum_start (eq_saturation_mode)
  enum_value (EQ_SATURATION_QUADRATIC,      "quadratic",      N_("Quadratic"))
  enum_value (EQ_SATURATION_SAFE_QUADRATIC, "safe-quadratic", N_("Safe Quadratic"))
  enum_value (EQ_SATURATION_HSV,            "hsv",            N_("HSV"))
  enum_value (EQ_SATURATION_HSL,            "hsl",            N_("HSL"))
  enum_value (EQ_SATURATION_YUV,            "yuv",            N_("YUV"))
enum_end (EqSaturationMode)

enum_start (eq_saturation_balance)
  enum_value (EQ_SATURATION_BALANCE_OFF,   "off",   N_("Off"))
  enum_value (EQ_SATURATION_BALANCE_LAYER, "layer", N_("Whole layer"))
  enum_value (EQ_SATURATION_BALANCE_COLOR, "color", N_("Picked color"))
enum_end (EqSaturationBalance)

#define EQ_BAND(n, label, top, at)                                       \
  property_double (band_##n, label, 1.0)                                 \
    description (_("Multiplies the saturation of pixels whose "          \
                   "saturation is near " at " times the color mode's "   \
                   "typical saturation; between the bands the factor "  \
                   "is interpolated"))                                    \
    value_range (0.0, top)                                               \
    ui_digits (2)

EQ_BAND (1, _("Grays"),                 4.0,  "0")
EQ_BAND (2, _("Very low saturation"),   3.75, "0.11")
EQ_BAND (3, _("Low saturation"),        3.5,  "0.21")
EQ_BAND (4, _("Medium saturation"),     3.25, "0.43")
EQ_BAND (5, _("High saturation"),       3.0,  "0.71")
EQ_BAND (6, _("Very high saturation"),  2.75, "1.43")

#undef EQ_BAND

property_enum (color_mode, _("Color mode"),
               EqSaturationMode, eq_saturation_mode, EQ_SATURATION_QUADRATIC)
  description (_("How brightness and saturation are measured and the "
                 "saturation changed: Quadratic (the plug-in's own), "
                 "Safe Quadratic (kept within 0 to 1 when possible), "
                 "HSV, HSL or YUV"))

property_double (dark, _("Dark"), 0.0)
  description (_("Levels: the new brightness of black (the plug-in's "
                 "values run from 0 to 1)"))
  value_range (-0.4, 0.8)
  ui_digits (3)

property_double (light, _("Light"), 1.0)
  description (_("Levels: the new brightness of white"))
  value_range (0.5, 1.4)
  ui_digits (3)

property_double (temperature, _("Temperature"), 0.0)
  description (_("Warmer to the right, colder to the left"))
  value_range (-1.0, 1.0)
  ui_digits (2)

property_double (corner_top_left, _("Top left corner"), 0.0)
  description (_("Per-corner brightness: added to the brightness at the "
                 "layer's top left corner, fading towards the others"))
  value_range (-0.2, 0.2)
  ui_digits (3)

property_double (corner_top_right, _("Top right corner"), 0.0)
  description (_("Per-corner brightness at the top right corner"))
  value_range (-0.2, 0.2)
  ui_digits (3)

property_double (corner_bottom_left, _("Bottom left corner"), 0.0)
  description (_("Per-corner brightness at the bottom left corner"))
  value_range (-0.2, 0.2)
  ui_digits (3)

property_double (corner_bottom_right, _("Bottom right corner"), 0.0)
  description (_("Per-corner brightness at the bottom right corner"))
  value_range (-0.2, 0.2)
  ui_digits (3)

property_enum (balance, _("Color auto balance"),
               EqSaturationBalance, eq_saturation_balance,
               EQ_SATURATION_BALANCE_OFF)
  description (_("Removes a color cast: the difference between the "
                 "average of R, G and B and each of them, taken from the "
                 "whole layer or from a picked color (e.g. snow that "
                 "should be white)"))

property_color (balance_color, _("Neutral color"), "#808080")
  description (_("A color of the image that should be neutral gray; "
                 "pick it from the image (the filter tool's sample "
                 "average option picks an area's average)"))
  ui_meta ("visible", "balance {color}")

property_double (balance_weight, _("Weight"), 1.0)
  description (_("How much of the cast is removed"))
  value_range (0.0, 1.0)
  ui_digits (2)
  ui_meta ("visible", "! balance {off}")

property_double (limit_range, _("Limit to color"), 0.0)
  description (_("Right of 0: only colors near the color below are "
                 "changed, fading out up to twice the range (at least "
                 "0.05 more); left of 0 (inverted): the colors near it "
                 "are kept and the others changed. 0: off"))
  value_range (-0.08, 0.08)
  ui_digits (3)

property_color (limit_color, _("Color"), "#a76e61")
  description (_("The color that the limit is around (the plug-in's "
                 "default, a skin tone); its brightness is ignored"))

#else

#define GEGL_OP_FILTER
#define GEGL_OP_NAME     saturation_equalizer
#define GEGL_OP_C_SOURCE saturation-equalizer.c

#include "gegl-op.h"

#include <math.h>
#include "equalizer-common.h"

/* the plug-in's numbers for its color modes (maindata.formula) */
#define QUADRATIC 0
#define HSV       1
#define HSL       2
#define YUV       3
#define QUADRSAFE 4
#define SLIDERS   6

static const gfloat relborders[7] = { 0, 0.11, 0.21, 0.43, 0.71, 1.43, 14.29 };
static const gfloat relsaturation[] = { 0.07, 0.24, 0.55, 0.17, 0.09 };

/* the plug-in's maindata, as the pixels need it */
typedef struct
{
  gint     formula;
  gfloat   boost[SLIDERS + 1];
  gfloat   borders[SLIDERS + 1];
  gfloat   levels[2];
  gfloat   cornerbr[4];
  gboolean docornbr;
  gfloat   temperature;
  gfloat   skin;
  gfloat   skinuplimit;
  gfloat   skincolors[8];
  gboolean docolbal;
  gfloat   colorbal[3];
  gfloat   balweight;
  gint     width;           /* of the layer */
  gint     height;
} Params;

/* pixeldata: a pixel's brightness, saturation and hue */
typedef struct
{
  gfloat brightness;
  gfloat saturation;
  gfloat hue;
} Basic;

/* the plug-in's pow (x, 2): for a float x the product in double is
 * exact, and so the same */
#define SQ(x) ((gdouble) (x) * (gdouble) (x))

static gfloat
get_quad_bright (gfloat R, gfloat G, gfloat B)
{
  gfloat partial1, partial2, partial3, sum;

  if (R < 0 || R > 1) partial1 = 0.241 * R;
  else partial1 = 0.241 * SQ (R);
  if (G < 0 || G > 1) partial2 = 0.691 * G;
  else partial2 = 0.691 * SQ (G);
  if (B < 0 || B > 1) partial3 = 0.068 * B;
  else partial3 = 0.068 * SQ (B);

  sum = partial1 + partial2 + partial3;
  if (sum > 0) sum = pow (sum, (gfloat) 1 / 2);

  return sum;
}

static void
get_bright (const Params *p, gfloat R, gfloat G, gfloat B, Basic *d)
{
  gfloat avg, tmp1, tmp2;

  if (p->formula == QUADRATIC || p->formula == QUADRSAFE)
    {
      d->brightness = get_quad_bright (R, G, B);
      avg = d->brightness;
      d->saturation = (fabsf (avg - R) + fabsf (avg - G) + fabsf (avg - B)) / 3;
      d->hue = 0;
    }
  else if (p->formula == HSV)
    {
      EqRGB rgb = { R, G, B };
      EqHSV hsv;

      eq_rgb_to_hsv (&rgb, &hsv);
      d->brightness = hsv.v;
      d->saturation = hsv.s;
      d->hue = hsv.h;
    }
  else if (p->formula == HSL)
    {
      EqRGB rgb = { R, G, B };
      EqHSL hsl;

      eq_rgb_to_hsl (&rgb, &hsl);
      d->brightness = hsl.l;
      d->saturation = hsl.s;
      d->hue = hsl.h;
    }
  else
    {
      avg  =  0.299 * R + 0.587 * G + 0.114 * B;
      tmp1 = -0.147 * R - 0.289 * G + 0.436 * B;
      tmp2 =  0.615 * R - 0.515 * G - 0.100 * B;
      d->brightness = avg;
      d->saturation = pow (SQ (tmp1) + SQ (tmp2), (gfloat) 1 / 2);
      d->hue = 0;
    }
}

static gfloat
get_sat_boost (const Params *p, gfloat oldsat)
{
  gfloat satboost, weight;
  gint   i;

  for (i = 1; i < SLIDERS + 1; i++)
    if (oldsat < p->borders[i])
      {
        weight = (oldsat - p->borders[i - 1]) / (p->borders[i] - p->borders[i - 1]);
        satboost = p->boost[i - 1] * (1 - weight) + p->boost[i] * weight;
        return satboost;
      }
  return 1;
}

static inline gfloat
max3 (gfloat x, gfloat y, gfloat z)
{
  if (x < y) x = y;
  if (x < z) x = z;
  return x;
}

static inline gfloat
min3 (gfloat x, gfloat y, gfloat z)
{
  if (x > y) x = y;
  if (x > z) x = z;
  return x;
}

static void
apply_quad_sat (gfloat *R, gfloat *G, gfloat *B, gfloat avg_orig, gfloat sat_boost)
{
  gfloat R_local = 0, G_local = 0, B_local = 0, cur_avg, avg_diff = 0;
  gint   c;

  for (c = 0; c < 3; c++)
    {
      R_local = avg_orig + (*R - avg_orig) * sat_boost + avg_diff;
      G_local = avg_orig + (*G - avg_orig) * sat_boost + avg_diff;
      B_local = avg_orig + (*B - avg_orig) * sat_boost + avg_diff;

      cur_avg = get_quad_bright (R_local, G_local, B_local);
      avg_diff = avg_diff - (cur_avg - avg_orig);
    }
  *R = R_local; *G = G_local; *B = B_local;
}

static void
apply_sat_boost (const Params *p, gfloat *R, gfloat *G, gfloat *B,
                 gfloat avg, gfloat sat, gfloat sat_boost)
{
  gfloat tmp1, tmp2;

  if (p->formula == QUADRATIC)
    {
      apply_quad_sat (R, G, B, avg, sat_boost);
    }
  else if (p->formula == QUADRSAFE)
    {
      gfloat m;

      apply_quad_sat (R, G, B, avg, sat_boost);
      /* Change: the same shift for the three channels; the plug-in took
       * min3 and max3 again after each channel had moved */
      m = min3 (*R, *G, *B);
      if (m < 0)
        {
          *R = *R - m;
          *G = *G - m;
          *B = *B - m;
        }
      m = max3 (*R, *G, *B);
      if (m > 1)
        {
          *R = *R - m + 1;
          *G = *G - m + 1;
          *B = *B - m + 1;
        }
    }
  else if (p->formula == HSV)
    {
      EqRGB rgb = { *R, *G, *B };
      EqHSV hsv;

      eq_rgb_to_hsv (&rgb, &hsv);
      hsv.s = MIN (1, sat * sat_boost);
      eq_hsv_to_rgb (&hsv, &rgb);
      *R = rgb.r; *G = rgb.g; *B = rgb.b;
    }
  else if (p->formula == HSL)
    {
      EqRGB rgb = { *R, *G, *B };
      EqHSL hsl;

      eq_rgb_to_hsl (&rgb, &hsl);
      hsl.s = MIN (1, sat * sat_boost);
      eq_hsl_to_rgb (&hsl, &rgb);
      *R = rgb.r; *G = rgb.g; *B = rgb.b;
    }
  else
    {
      tmp1 = -0.147 * *R - 0.289 * *G + 0.436 * *B;
      tmp2 =  0.615 * *R - 0.515 * *G - 0.100 * *B;
      tmp1 = tmp1 * sat_boost;
      tmp2 = tmp2 * sat_boost;
      *R = avg + 1.140 * tmp2;
      *G = avg - 0.395 * tmp1 - 0.581 * tmp2;
      *B = avg + 2.032 * tmp1;
    }
}

static void
set_tmpr (const Params *p, gfloat *R, gfloat *G, gfloat *B)
{
  Basic  d;
  gfloat avg, diff;

  get_bright (p, *R, *G, *B, &d);
  avg = d.brightness;
  *R = *R + 0.12 * p->temperature;
  *G = *G + 0.05 * p->temperature;
  *B = *B - 0.17 * p->temperature;
  get_bright (p, *R, *G, *B, &d);
  diff = avg - d.brightness;
  *R = *R + diff;
  *G = *G + diff;
  *B = *B + diff;
  get_bright (p, *R, *G, *B, &d);
  diff = avg - d.brightness;
  *R = *R + diff;
  *G = *G + diff;
  *B = *B + diff;
}

/* the distance between two hues on the circle of 0 to 1; a gray's HSL
 * hue is undefined (-1): as far as can be */
static inline gfloat
hue_distance (gfloat a, gfloat b)
{
  gfloat d;

  if (a < 0 || b < 0)
    return 0.5f;
  d = fabsf (a - b);
  return MIN (d, 1 - d);
}

/* skincolorchanged: the picked color, as the pixels are measured.
 * Change: the hues from the pow (1 / 2.2) values, as the pixels' (the
 * plug-in took them from the color's 8 bit values) */
static void
set_limit_color (Params *p, const gfloat rgb[3])
{
  Basic  d;
  gfloat R, G, B, avg;
  EqRGB  c;
  EqHSV  hsv;
  EqHSL  hsl;

  R = eq_gam (eq_sane (rgb[0]));
  G = eq_gam (eq_sane (rgb[1]));
  B = eq_gam (eq_sane (rgb[2]));
  get_bright (p, R, G, B, &d);
  avg = d.brightness;

  p->skincolors[0] = R - avg;
  p->skincolors[1] = G - avg;
  p->skincolors[2] = B - avg;

  c.r = R; c.g = G; c.b = B;
  eq_rgb_to_hsv (&c, &hsv);
  p->skincolors[3] = hsv.h;
  eq_rgb_to_hsl (&c, &hsl);
  p->skincolors[4] = hsl.h;

  p->skincolors[5] =  0.299 * R + 0.587 * G + 0.114 * B;
  p->skincolors[6] = -0.147 * R - 0.289 * G + 0.436 * B;
  p->skincolors[7] =  0.615 * R - 0.515 * G - 0.100 * B;
}

static gfloat
get_yuv_skindist (const Params *p, gfloat R, gfloat G, gfloat B)
{
  gfloat tmp1, tmp2;

  tmp1 = -0.147 * R - 0.289 * G + 0.436 * B;
  tmp2 =  0.615 * R - 0.515 * G - 0.100 * B;
  return pow ((SQ (tmp1 - p->skincolors[6]) + SQ (tmp2 - p->skincolors[7])), 0.5);
}

/* block_saturate for one pixel; x and y are the pixel's position in the
 * layer, startdiff and enddiff the row's per-corner amounts */
static void
saturate_pixel (const Params *p,
                const gfloat *pixel,
                gfloat       *out,
                gint          x,
                gfloat        startdiff,
                gfloat        enddiff)
{
  const gfloat in[4] = { pixel[0], pixel[1], pixel[2], pixel[3] };
  Basic  d;
  gfloat R, G, B, avg, sat, sat_boost, bright_diff, skindist, weight;

  R = eq_gam (eq_sane (in[0]));
  G = eq_gam (eq_sane (in[1]));
  B = eq_gam (eq_sane (in[2]));

  if (p->docolbal)
    {
      R = R - p->colorbal[0] * p->balweight;
      G = G - p->colorbal[1] * p->balweight;
      B = B - p->colorbal[2] * p->balweight;
    }

  get_bright (p, R, G, B, &d);
  avg = d.brightness;
  bright_diff = p->levels[0] + (-p->levels[0] + p->levels[1]) * avg - avg;
  R = R + bright_diff; G = G + bright_diff; B = B + bright_diff;

  if (p->docornbr)
    {
      bright_diff = (1.0 - (gfloat) x / p->width) * startdiff
                    + (gfloat) x / p->width * enddiff;
      R = R + bright_diff; G = G + bright_diff; B = B + bright_diff;
    }

  if (p->temperature != 0)
    set_tmpr (p, &R, &G, &B);

  get_bright (p, R, G, B, &d);
  avg = d.brightness;
  sat = d.saturation;

  sat_boost = get_sat_boost (p, sat);

  if (p->skin != 0)
    {
      /* Change: each color mode's own distance (the plug-in's chain of
       * ifs gave Quadratic and HSL the YUV distance) */
      if (p->formula == QUADRATIC || p->formula == QUADRSAFE)
        skindist = pow ((SQ (R - avg - p->skincolors[0])
                         + SQ (G - avg - p->skincolors[1])
                         + SQ (B - avg - p->skincolors[2])), 0.5);
      else if (p->formula == HSV)
        skindist = hue_distance (p->skincolors[3], d.hue);
      else if (p->formula == HSL)
        skindist = hue_distance (p->skincolors[4], d.hue);
      else
        skindist = get_yuv_skindist (p, R, G, B);

      if (p->skin > 0)
        {
          if (skindist > p->skinuplimit)
            sat_boost = 1;
          else if (skindist > p->skin)
            {
              weight = (p->skinuplimit - skindist) / (p->skinuplimit - p->skin);
              sat_boost = sat_boost * weight + (1 - weight);
            }
        }
      else
        {
          /* Change: the inverted range, the mirror image of the above
           * (the plug-in never set its neighbourhood for it) */
          gfloat s = -p->skin;

          if (skindist > p->skinuplimit)
            ;
          else if (skindist > s)
            {
              weight = (skindist - s) / (p->skinuplimit - s);
              sat_boost = sat_boost * weight + (1 - weight);
            }
          else
            sat_boost = 1;
        }
    }

  apply_sat_boost (p, &R, &G, &B, avg, sat, sat_boost);

  out[0] = eq_ungam (R);
  out[1] = eq_ungam (G);
  out[2] = eq_ungam (B);
  out[3] = in[3];

  /* nothing that is not a number leaves the operation: a pixel whose
   * arithmetic failed (values far outside 0 to 1) keeps its value */
  if (! (isfinite (out[0]) && isfinite (out[1]) && isfinite (out[2])))
    {
      out[0] = eq_sane (in[0]);
      out[1] = eq_sane (in[1]);
      out[2] = eq_sane (in[2]);
    }
}

/* maskdiff: the per-corner amounts at the left and right end of row y */
static void
maskdiff (const Params *p, gint y_abs, gfloat *startdiff, gfloat *enddiff)
{
  *startdiff = p->cornerbr[0] * (1.0 - (gfloat) y_abs / p->height)
               + p->cornerbr[2] * (gfloat) y_abs / p->height;
  *enddiff   = p->cornerbr[1] * (1.0 - (gfloat) y_abs / p->height)
               + p->cornerbr[3] * (gfloat) y_abs / p->height;
}

static gboolean
is_neutral (GeglProperties *o)
{
  return o->band_1 == 1.0 && o->band_2 == 1.0 && o->band_3 == 1.0 &&
         o->band_4 == 1.0 && o->band_5 == 1.0 && o->band_6 == 1.0 &&
         o->dark == 0.0 && o->light == 1.0 && o->temperature == 0.0 &&
         o->corner_top_left == 0.0 && o->corner_top_right == 0.0 &&
         o->corner_bottom_left == 0.0 && o->corner_bottom_right == 0.0 &&
         (o->balance == EQ_SATURATION_BALANCE_OFF || o->balance_weight == 0.0);
}

static void
prepare (GeglOperation *operation)
{
  const Babl *space  = gegl_operation_get_source_space (operation, "input");
  const Babl *format = babl_format_with_space ("R'G'B'A float", space);

  gegl_operation_set_format (operation, "input", format);
  gegl_operation_set_format (operation, "output", format);
}

static GeglRectangle
get_bounding_box (GeglOperation *operation)
{
  const GeglRectangle *in = gegl_operation_source_get_bounding_box (operation, "input");

  return in ? *in : *GEGL_RECTANGLE (0, 0, 0, 0);
}

/* the whole layer when the balance comes from it (as gegl:stretch-contrast
 * does), otherwise the pixel itself */
static gboolean
needs_layer (GeglOperation *operation)
{
  GeglProperties      *o  = GEGL_PROPERTIES (operation);
  const GeglRectangle *in = gegl_operation_source_get_bounding_box (operation, "input");

  return o->balance == EQ_SATURATION_BALANCE_LAYER && o->balance_weight != 0.0 &&
         in && ! gegl_rectangle_is_infinite_plane (in) && ! is_neutral (o);
}

static GeglRectangle
get_required_for_output (GeglOperation       *operation,
                         const gchar         *input_pad,
                         const GeglRectangle *roi)
{
  if (needs_layer (operation))
    return *gegl_operation_source_get_bounding_box (operation, "input");
  return *roi;
}

static GeglRectangle
get_cached_region (GeglOperation       *operation,
                   const GeglRectangle *roi)
{
  if (needs_layer (operation))
    return *gegl_operation_source_get_bounding_box (operation, "input");
  return *roi;
}

static GeglRectangle
get_invalidated_by_change (GeglOperation       *operation,
                           const gchar         *input_pad,
                           const GeglRectangle *input_region)
{
  if (needs_layer (operation))
    return *gegl_operation_source_get_bounding_box (operation, "input");
  return *input_region;
}

/* the color cast of the layer: calibrate() over every pixel, with the
 * sums in double precision */
typedef struct
{
  GeglBuffer          *input;
  const Babl          *format;
  const GeglRectangle *bounds;
  gdouble             *sums;       /* R, G, B per row */
} MeanJob;

static void
mean_rows (gsize offset, gsize size, gpointer data)
{
  MeanJob *job = data;
  gint     w   = job->bounds->width;
  gfloat  *row = g_new (gfloat, (gsize) w * 4);
  gsize    r;

  for (r = offset; r < offset + size; r++)
    {
      gdouble s[3] = { 0, 0, 0 };
      gint    x, c;

      gegl_buffer_get (job->input,
                       GEGL_RECTANGLE (job->bounds->x, job->bounds->y + (gint) r, w, 1),
                       1.0, job->format, row, GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
      for (x = 0; x < w; x++)
        for (c = 0; c < 3; c++)
          s[c] += eq_gam (eq_sane (row[x * 4 + c]));
      for (c = 0; c < 3; c++)
        job->sums[r * 3 + c] = s[c];
    }
  g_free (row);
}

/* the cast of R, G, B means (pow (1 / 2.2) values): each minus their
 * average */
static void
set_colorbal (Params *p, const gfloat mean[3])
{
  gfloat avg;

  avg = (mean[0] + mean[1] + mean[2]) / 3.0;
  p->colorbal[0] = mean[0] - avg;
  p->colorbal[1] = mean[1] - avg;
  p->colorbal[2] = mean[2] - avg;
}

typedef struct
{
  const Params        *p;
  GeglBuffer          *input;
  GeglBuffer          *output;
  const Babl          *format;
  const GeglRectangle *result;
  const GeglRectangle *bounds;
} RowJob;

static void
saturate_rows (gsize offset, gsize size, gpointer data)
{
  RowJob        *job = data;
  const Params  *p   = job->p;
  GeglRectangle  rect = { job->result->x, job->result->y + (gint) offset,
                          job->result->width, (gint) size };
  gsize          n   = (gsize) rect.width * rect.height;
  gfloat        *buf = g_new (gfloat, n * 4);
  gint           x, y;

  gegl_buffer_get (job->input, &rect, 1.0, job->format, buf,
                   GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
  for (y = 0; y < rect.height; y++)
    {
      gint   y_abs = rect.y + y - job->bounds->y;
      gfloat startdiff = 0, enddiff = 0;
      gfloat *px = buf + (gsize) y * rect.width * 4;

      if (p->docornbr)
        maskdiff (p, y_abs, &startdiff, &enddiff);
      for (x = 0; x < rect.width; x++)
        saturate_pixel (p, px + x * 4, px + x * 4, rect.x + x - job->bounds->x,
                        startdiff, enddiff);
    }
  gegl_buffer_set (job->output, &rect, 0, job->format, buf, GEGL_AUTO_ROWSTRIDE);
  g_free (buf);
}

static gboolean
process (GeglOperation       *operation,
         GeglBuffer          *input,
         GeglBuffer          *output,
         const GeglRectangle *result,
         gint                 level)
{
  GeglProperties      *o      = GEGL_PROPERTIES (operation);
  const Babl          *format = gegl_operation_get_format (operation, "output");
  const GeglRectangle *bounds = gegl_operation_source_get_bounding_box (operation, "input");
  GeglRectangle        layer;
  Params               p      = { 0 };
  RowJob               job;
  gint                 c;

  if (result->width <= 0 || result->height <= 0)
    return TRUE;
  /* the layer that the corners and the balance refer to */
  layer = (bounds && ! gegl_rectangle_is_infinite_plane (bounds)) ? *bounds : *result;

  switch (o->color_mode)
    {
    case EQ_SATURATION_SAFE_QUADRATIC: p.formula = QUADRSAFE; break;
    case EQ_SATURATION_HSV:            p.formula = HSV;       break;
    case EQ_SATURATION_HSL:            p.formula = HSL;       break;
    case EQ_SATURATION_YUV:            p.formula = YUV;       break;
    default:                           p.formula = QUADRATIC; break;
    }
  /* the plug-in keeps its settings as floats */
  p.boost[0] = o->band_1;
  p.boost[1] = o->band_2;
  p.boost[2] = o->band_3;
  p.boost[3] = o->band_4;
  p.boost[4] = o->band_5;
  p.boost[5] = o->band_6;
  p.boost[6] = 1.0;
  for (c = 0; c < SLIDERS + 1; c++)
    p.borders[c] = relborders[c] * relsaturation[p.formula];
  p.levels[0]   = o->dark;
  p.levels[1]   = o->light;
  p.cornerbr[0] = o->corner_top_left;
  p.cornerbr[1] = o->corner_top_right;
  p.cornerbr[2] = o->corner_bottom_left;
  p.cornerbr[3] = o->corner_bottom_right;
  p.docornbr    = ! (p.cornerbr[0] == 0 && p.cornerbr[1] == 0 &&
                     p.cornerbr[2] == 0 && p.cornerbr[3] == 0);
  p.temperature = o->temperature;
  p.width       = MAX (layer.width, 1);
  p.height      = MAX (layer.height, 1);

  p.skin = o->limit_range;
  if (p.skin != 0)
    {
      gfloat rgb[3] = { 0, 0, 0 };
      gfloat s      = fabsf (p.skin);

      gegl_color_get_pixel (o->limit_color, babl_format_with_space ("R'G'B' float",
                            babl_format_get_space (format)), rgb);
      set_limit_color (&p, rgb);
      p.skinuplimit = MAX (s * 2.0, s + 0.05);
    }

  p.balweight = o->balance_weight;
  if (o->balance == EQ_SATURATION_BALANCE_COLOR && p.balweight != 0)
    {
      gfloat rgb[3] = { 0, 0, 0 };
      gint   k;

      gegl_color_get_pixel (o->balance_color, babl_format_with_space ("R'G'B' float",
                            babl_format_get_space (format)), rgb);
      for (k = 0; k < 3; k++)
        rgb[k] = eq_gam (eq_sane (rgb[k]));
      set_colorbal (&p, rgb);
      p.docolbal = TRUE;
    }
  else if (o->balance == EQ_SATURATION_BALANCE_LAYER && p.balweight != 0 &&
           layer.width > 0 && layer.height > 0)
    {
      MeanJob mj = { input, format, &layer,
                     g_new0 (gdouble, (gsize) layer.height * 3) };
      gdouble s[3] = { 0, 0, 0 };
      gfloat  mean[3];
      gdouble count = (gdouble) layer.width * layer.height;
      gint    r;

      /* per row, added up in order: the same sums with any number of
       * threads */
      gegl_parallel_distribute_range (layer.height,
                                      gegl_operation_get_pixels_per_thread (operation)
                                        / MAX (layer.width, 1),
                                      mean_rows, &mj);
      for (r = 0; r < layer.height; r++)
        for (c = 0; c < 3; c++)
          s[c] += mj.sums[r * 3 + c];
      g_free (mj.sums);
      for (c = 0; c < 3; c++)
        mean[c] = s[c] / count;
      set_colorbal (&p, mean);
      p.docolbal = TRUE;
    }

  job.p      = &p;
  job.input  = input;
  job.output = output;
  job.format = format;
  job.result = result;
  job.bounds = &layer;
  gegl_parallel_distribute_range (result->height,
                                  gegl_operation_get_pixels_per_thread (operation)
                                    / MAX (result->width, 1),
                                  saturate_rows, &job);
  return TRUE;
}

/* at the neutral settings the input is passed on as it is */
static gboolean
operation_process (GeglOperation        *operation,
                   GeglOperationContext *context,
                   const gchar          *output_prop,
                   const GeglRectangle  *result,
                   gint                  level)
{
  GeglOperationClass *parent = GEGL_OPERATION_CLASS (gegl_op_parent_class);

  if (is_neutral (GEGL_PROPERTIES (operation)))
    {
      gpointer in = gegl_operation_context_get_object (context, "input");

      gegl_operation_context_take_object (context, "output",
                                          in ? g_object_ref (in) : NULL);
      return TRUE;
    }
  return parent->process (operation, context, output_prop, result,
                          gegl_operation_context_get_level (context));
}

static void
gegl_op_class_init (GeglOpClass *klass)
{
  GeglOperationClass       *operation_class = GEGL_OPERATION_CLASS (klass);
  GeglOperationFilterClass *filter_class    = GEGL_OPERATION_FILTER_CLASS (klass);

  operation_class->prepare                   = prepare;
  operation_class->process                   = operation_process;
  operation_class->get_bounding_box          = get_bounding_box;
  operation_class->get_required_for_output   = get_required_for_output;
  operation_class->get_cached_region         = get_cached_region;
  operation_class->get_invalidated_by_change = get_invalidated_by_change;
  /* rows are shared out among GEGL's threads in process (), once */
  operation_class->threaded                  = FALSE;
  filter_class->process                      = process;

  gegl_operation_class_set_keys (operation_class,
    "name",            "equalizer:saturation",
    "title",           _("Saturation Equalizer"),
    "categories",      "color",
    "description",     _("Changes saturation by the pixels' own saturation, "
                         "with six bands like an equalizer, so that dull "
                         "colors can gain more than strong ones; with "
                         "levels, per-corner brightness, temperature, color "
                         "auto balance and a limit to a picked color. A port "
                         "of Tibor Bamhor's GIMP 2 plug-in"),
    "gimp:menu-path",  "<Image>/Colors",
    "gimp:menu-label", _("Saturation Equalizer..."),
    NULL);
}

#endif
