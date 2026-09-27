/*
 * Shared by the operations of gegl-equalizers: the value encoding of
 * Tibor Bamhor's plug-ins, and GIMP 2's HSV and HSL conversions
 *
 * equalizer-common.h
 * Copyright 2026 by David
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
 * The four conversions below are those of GIMP 2.10.38,
 * libgimpcolor/gimpcolorspace.c (Copyright (C) 1995-1997 Peter Mattis
 * and Spencer Kimball, LGPL-3.0-or-later), which both plug-ins call:
 * the same arithmetic in double precision, without the g_return_if_fail
 * checks and the alpha member.
 */

#ifndef EQUALIZER_COMMON_H
#define EQUALIZER_COMMON_H

#include <math.h>
#include <glib.h>

/* The plug-ins read GIMP 2's 8 bit values v and work on
 * pow (v / 255, 1 / 2.2), "gammed" values in their words, and write
 * pow (x, 2.2) back. The operations do the same on the image's own
 * values (R'G'B' float in the image's color space, which for GIMP 2's
 * 8 bit images are those 8 bit values), with the sign kept for values
 * below 0 that high bit depth images can have (the plug-ins, at 8 bits,
 * clipped them: pow of a negative value is NaN, and their rounding made
 * that 0). */
static inline gfloat
eq_gam (gfloat v)
{
  return v >= 0.0f ? (gfloat) pow (v, 1 / 2.2) : - (gfloat) pow (-v, 1 / 2.2);
}

static inline gfloat
eq_ungam (gfloat v)
{
  return v >= 0.0f ? (gfloat) pow (v, 2.2) : - (gfloat) pow (-v, 2.2);
}

/* the input values the operations accept: NaN is 0 and the values are
 * kept within +-1e6, so that no infinity or NaN reaches the arithmetic
 * (a blur would spread it to the neighbours) */
#define EQ_LIMIT 1e6f

static inline gfloat
eq_sane (gfloat v)
{
  if (isnan (v))
    return 0.0f;
  return CLAMP (v, -EQ_LIMIT, EQ_LIMIT);
}

typedef struct { gdouble r, g, b; } EqRGB;
typedef struct { gdouble h, s, v; } EqHSV;
typedef struct { gdouble h, s, l; } EqHSL;

#define EQ_HSL_UNDEFINED -1.0

static inline gdouble
eq_rgb_max (const EqRGB *rgb)
{
  if (rgb->r > rgb->g)
    return (rgb->r > rgb->b) ? rgb->r : rgb->b;
  else
    return (rgb->g > rgb->b) ? rgb->g : rgb->b;
}

static inline gdouble
eq_rgb_min (const EqRGB *rgb)
{
  if (rgb->r < rgb->g)
    return (rgb->r < rgb->b) ? rgb->r : rgb->b;
  else
    return (rgb->g < rgb->b) ? rgb->g : rgb->b;
}

/* gimp_rgb_to_hsv */
static inline void
eq_rgb_to_hsv (const EqRGB *rgb,
               EqHSV       *hsv)
{
  gdouble max, min, delta;

  max = eq_rgb_max (rgb);
  min = eq_rgb_min (rgb);

  hsv->v = max;
  delta = max - min;

  if (delta > 0.0001)
    {
      hsv->s = delta / max;

      if (rgb->r == max)
        {
          hsv->h = (rgb->g - rgb->b) / delta;
          if (hsv->h < 0.0)
            hsv->h += 6.0;
        }
      else if (rgb->g == max)
        {
          hsv->h = 2.0 + (rgb->b - rgb->r) / delta;
        }
      else
        {
          hsv->h = 4.0 + (rgb->r - rgb->g) / delta;
        }

      hsv->h /= 6.0;
    }
  else
    {
      hsv->s = 0.0;
      hsv->h = 0.0;
    }
}

/* gimp_hsv_to_rgb; for a hue outside 0 to 1 (possible only for values
 * that GIMP 2's 8 bit images never had) rgb is left as it was, as in
 * GIMP: the callers fill it with the pixel's own color first */
static inline void
eq_hsv_to_rgb (const EqHSV *hsv,
               EqRGB       *rgb)
{
  gint    i;
  gdouble f, w, q, t;
  gdouble hue;

  if (hsv->s == 0.0)
    {
      rgb->r = hsv->v;
      rgb->g = hsv->v;
      rgb->b = hsv->v;
    }
  else
    {
      hue = hsv->h;

      if (hue == 1.0)
        hue = 0.0;

      hue *= 6.0;

      i = (gint) hue;
      f = hue - i;
      w = hsv->v * (1.0 - hsv->s);
      q = hsv->v * (1.0 - (hsv->s * f));
      t = hsv->v * (1.0 - (hsv->s * (1.0 - f)));

      switch (i)
        {
        case 0:
          rgb->r = hsv->v;
          rgb->g = t;
          rgb->b = w;
          break;
        case 1:
          rgb->r = q;
          rgb->g = hsv->v;
          rgb->b = w;
          break;
        case 2:
          rgb->r = w;
          rgb->g = hsv->v;
          rgb->b = t;
          break;
        case 3:
          rgb->r = w;
          rgb->g = q;
          rgb->b = hsv->v;
          break;
        case 4:
          rgb->r = t;
          rgb->g = w;
          rgb->b = hsv->v;
          break;
        case 5:
          rgb->r = hsv->v;
          rgb->g = w;
          rgb->b = q;
          break;
        default:
          break;
        }
    }
}

/* gimp_rgb_to_hsl */
static inline void
eq_rgb_to_hsl (const EqRGB *rgb,
               EqHSL       *hsl)
{
  gdouble max, min, delta;

  max = eq_rgb_max (rgb);
  min = eq_rgb_min (rgb);

  hsl->l = (max + min) / 2.0;

  if (max == min)
    {
      hsl->s = 0.0;
      hsl->h = EQ_HSL_UNDEFINED;
    }
  else
    {
      if (hsl->l <= 0.5)
        hsl->s = (max - min) / (max + min);
      else
        hsl->s = (max - min) / (2.0 - max - min);

      delta = max - min;

      if (delta == 0.0)
        delta = 1.0;

      if (rgb->r == max)
        {
          hsl->h = (rgb->g - rgb->b) / delta;
        }
      else if (rgb->g == max)
        {
          hsl->h = 2.0 + (rgb->b - rgb->r) / delta;
        }
      else
        {
          hsl->h = 4.0 + (rgb->r - rgb->g) / delta;
        }

      hsl->h /= 6.0;

      if (hsl->h < 0.0)
        hsl->h += 1.0;
    }
}

static inline gdouble
eq_hsl_value (gdouble n1,
              gdouble n2,
              gdouble hue)
{
  gdouble val;

  if (hue > 6.0)
    hue -= 6.0;
  else if (hue < 0.0)
    hue += 6.0;

  if (hue < 1.0)
    val = n1 + (n2 - n1) * hue;
  else if (hue < 3.0)
    val = n2;
  else if (hue < 4.0)
    val = n1 + (n2 - n1) * (4.0 - hue);
  else
    val = n1;

  return val;
}

/* gimp_hsl_to_rgb */
static inline void
eq_hsl_to_rgb (const EqHSL *hsl,
               EqRGB       *rgb)
{
  if (hsl->s == 0)
    {
      rgb->r = hsl->l;
      rgb->g = hsl->l;
      rgb->b = hsl->l;
    }
  else
    {
      gdouble m1, m2;

      if (hsl->l <= 0.5)
        m2 = hsl->l * (1.0 + hsl->s);
      else
        m2 = hsl->l + hsl->s - hsl->l * hsl->s;

      m1 = 2.0 * hsl->l - m2;

      rgb->r = eq_hsl_value (m1, m2, hsl->h * 6.0 + 2.0);
      rgb->g = eq_hsl_value (m1, m2, hsl->h * 6.0);
      rgb->b = eq_hsl_value (m1, m2, hsl->h * 6.0 - 2.0);
    }
}

#endif
