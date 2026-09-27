/*
 * Advanced Unsharp Mask: Tibor Bamhor's GIMP 2 plug-in AUMASK as a GEGL
 * operation
 *
 * advanced-unsharp-mask.c
 * Copyright 2011-2013 Tibor Bamhor (the plug-in aumask.c 0.9.2)
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
 * The plug-in (aumask.c 0.9.2 of 2013, "Copyright Tibor Bamhor", GPLv3
 * as its author's pages on code.google.com/p/aumask and on
 * registry.gimp.org say) sharpens by the difference between a pixel's
 * brightness and a blurred "mask" of the brightness, like an unsharp
 * mask, but the factor for that difference depends on its size, set
 * with sliders like an equalizer: small differences (noise, fine
 * texture) can be left while larger ones (edges) are sharpened, or the
 * other way round, or softened.
 *
 *   1. brightness: 0.299 R + 0.587 G + 0.114 B of pow (value, 1 / 2.2)
 *      (the plug-in's default), or HSV's value or HSL's lightness of the
 *      values themselves, or R, G or B of pow (value, 1 / 2.2)
 *   2. the mask: the brightness blurred in up to 15 passes of growing
 *      radius that add up to the radius; each pass averages the pixel
 *      with about 24 points on a circle around it (points outside the
 *      layer are left out). Selective blur leaves out the points that
 *      differ from the pixel by more than a threshold (a smaller one in
 *      the first passes) and keeps the mask within the threshold of the
 *      brightness; with postblur the selective mask is blurred again, a
 *      little
 *   3. the new mask: dark + mask * (light - dark), like Levels, which
 *      changes the brightness but keeps local contrast
 *   4. the new brightness: new mask + (brightness - mask) * factor, the
 *      factor interpolated from the sliders at differences of 0,
 *      0.0075, 0.03, 0.07 and 0.20
 *   5. the color: R, G and B moved by the change of brightness (R, G, B
 *      modes: the brightness 0.299 R + 0.587 G + 0.114 B becomes the
 *      channel's new value, "R to brightness"), or HSV's V or HSL's L
 *      set; then pow (x, 2.2) (not in the HSV and HSL modes)
 *
 * The output can also be the mask, or the difference between brightness
 * and mask (times a boost, around a central brightness).
 *
 * Changes from the plug-in, each marked "Change" below and listed in
 * README.md (tests/orig-check.sh shows that the operation gives the
 * plug-in's results, and those of the plug-in with these changes made
 * in its own code):
 *
 *   - float in and out, no 8 bit rounding, values below 0 and above 1
 *     kept (the plug-in clipped at its 8 bit output)
 *   - the factor for differences above 0.20: a fifth slider. In the
 *     plug-in the factor went from a hidden fifth value at 0.20 (which
 *     followed the fourth slider when it moved by more than 0.3,
 *     depending on the history of the dialog) to 0 at a difference of
 *     1.1, read from past the end of its array: so at the default of 1
 *     the plug-in softened strong edges. Here all five are 1 by default
 *     and the fifth holds beyond 0.20
 *   - the layer's edge is the edge (the plug-in: the selection's bounding
 *     box, beyond which it did not look); GIMP's selection masks the
 *     result as usual
 *   - not ported, GIMP 3 does them: the preview with its Split mode,
 *     "Export as new layer", the Reset button; and the slider coupling
 *     of the dialog (at most 0.3 between neighbours), which only moved
 *     the other sliders
 */

#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

enum_start (eq_sharp_output)
  enum_value (EQ_SHARP_OUTPUT_IMAGE,      "image",      N_("Image"))
  enum_value (EQ_SHARP_OUTPUT_MASK,       "mask",       N_("Mask"))
  enum_value (EQ_SHARP_OUTPUT_DIFFERENCE, "difference", N_("Difference (mask to image)"))
enum_end (EqSharpOutput)

enum_start (eq_sharp_blur)
  enum_value (EQ_SHARP_BLUR_STANDARD,  "standard",  N_("Standard blur"))
  enum_value (EQ_SHARP_BLUR_SELECTIVE, "selective", N_("Selective blur (threshold)"))
  enum_value (EQ_SHARP_BLUR_POSTBLUR,  "postblur",  N_("Selective with postblur"))
enum_end (EqSharpBlur)

enum_start (eq_sharp_source)
  enum_value (EQ_SHARP_SOURCE_BRIGHTNESS, "brightness", N_("Brightness 0.299/0.587/0.114"))
  enum_value (EQ_SHARP_SOURCE_VALUE,      "value",      N_("Value in HSV"))
  enum_value (EQ_SHARP_SOURCE_LIGHTNESS,  "lightness",  N_("Lightness in HSL"))
  enum_value (EQ_SHARP_SOURCE_RED,        "red",        N_("R to brightness"))
  enum_value (EQ_SHARP_SOURCE_GREEN,      "green",      N_("G to brightness"))
  enum_value (EQ_SHARP_SOURCE_BLUE,       "blue",       N_("B to brightness"))
enum_end (EqSharpSource)

property_enum (output_mode, _("Output"),
               EqSharpOutput, eq_sharp_output, EQ_SHARP_OUTPUT_IMAGE)
  description (_("The sharpened image, the mask, or the difference "
                 "between image and mask (not called \"output\": GIMP "
                 "leaves out a property named like the output pad)"))

#define EQ_AMOUNT(n, label, at)                                          \
  property_double (amount_##n, label, 1.0)                               \
    description (_("Multiplies the difference between a pixel's "        \
                   "brightness and the mask where it is " at             \
                   " (brightness runs from 0 to 1): 1 keeps it, more "   \
                   "sharpens, less softens; between the sliders the "    \
                   "factor is interpolated"))                             \
    value_range (0.0, 2.5)                                               \
    ui_digits (2)                                                        \
    ui_meta ("visible", "output-mode {image}")

EQ_AMOUNT (1, _("Contrast 0"),              "0")
EQ_AMOUNT (2, _("Contrast 0.0075"),         "0.0075")
EQ_AMOUNT (3, _("Contrast 0.03"),           "0.03")
EQ_AMOUNT (4, _("Contrast 0.07"),           "0.07")
EQ_AMOUNT (5, _("Contrast 0.20 and more"),  "0.20 or more")

#undef EQ_AMOUNT

property_enum (blur, _("Mask"),
               EqSharpBlur, eq_sharp_blur, EQ_SHARP_BLUR_STANDARD)
  description (_("Standard blur, selective blur (only similar "
                 "brightness is blurred together, so strong edges keep "
                 "no halo), or selective blur softened by a postblur"))

property_int (radius, _("Radius"), 20)
  description (_("The radius of the blur, in pixels"))
  value_range (1, 500)
  ui_range (1, 100)
  ui_meta ("unit", "pixel-distance")

property_double (threshold, _("Threshold"), 0.2)
  description (_("Selective blur: the largest difference of brightness "
                 "that is blurred together"))
  value_range (0.02, 1.0)
  ui_digits (3)
  ui_meta ("visible", "! blur {standard}")

property_int (postblur_radius, _("Postblur radius"), 3)
  description (_("Softens the edges of the selective mask (0 is a "
                 "radius of 1, as in the plug-in)"))
  value_range (0, 10)
  ui_meta ("unit", "pixel-distance")
  ui_meta ("visible", "blur {postblur}")

property_enum (source, _("Brightness/mask source"),
               EqSharpSource, eq_sharp_source, EQ_SHARP_SOURCE_BRIGHTNESS)
  description (_("How the brightness of a pixel is taken"))

property_double (dark, _("Dark"), 0.0)
  description (_("Brightness adjustment through the mask, like Levels: "
                 "the new brightness of black; local contrast is kept"))
  value_range (-0.4, 0.7)
  ui_digits (3)
  ui_meta ("visible", "! output-mode {difference}")

property_double (light, _("Light"), 1.0)
  description (_("Brightness adjustment through the mask: the new "
                 "brightness of white"))
  value_range (0.5, 1.4)
  ui_digits (3)
  ui_meta ("visible", "! output-mode {difference}")

property_double (central_brightness, _("Central brightness"), 0.5)
  description (_("Difference output: the brightness where image and "
                 "mask are equal"))
  value_range (0.0, 1.0)
  ui_digits (2)
  ui_meta ("visible", "output-mode {difference}")

property_double (diff_boost, _("Difference boost"), 2.0)
  description (_("Difference output: multiplies the difference between "
                 "mask and image"))
  value_range (-10.0, 10.0)
  ui_digits (1)
  ui_meta ("visible", "output-mode {difference}")

#else

#define GEGL_OP_AREA_FILTER
#define GEGL_OP_NAME     advanced_unsharp_mask
#define GEGL_OP_C_SOURCE advanced-unsharp-mask.c

#include "gegl-op.h"

#include <math.h>
#include <string.h>
#include "equalizer-common.h"

#define BLURSTEPS 15
#define SLIDERS   4
#define MAXPOINTS 201
#define BRIGHT(R,G,B) 0.299*R + 0.587*G + 0.114*B
#define ROUND(x) ((int) ((x) + 0.5))

/* the plug-in's colormode */
enum { BRIGHT_OWN, HSV, HSL, RCHAN, GCHAN, BCHAN };
/* its action */
enum { NORMAL, MASK, MASKDIFF };
/* its blurmode */
enum { B_NORMAL, SELBLUR, POSTBLUR };

static const gfloat borders[SLIDERS + 2] = { 0, 0.0075, 0.03, 0.07, 0.2, 1.1 };

/* the passes of one do_mask3 () */
typedef struct
{
  gint   steps;
  gint   radius[BLURSTEPS];
  gfloat treshold[BLURSTEPS];
} Schedule;

static void
schedule_make (Schedule *s,
               gint      radius,
               gfloat    treshold)
{
  gint remaining_radius = radius;
  gint uc;

  s->steps = BLURSTEPS;
  for (uc = 0; uc < BLURSTEPS; uc++)
    {
      s->radius[uc] = remaining_radius * 0.45;
      s->radius[uc] = MIN (s->radius[uc], (uc + 1) * 3 + 1);
      if (s->radius[uc] == 0)
        s->radius[uc] = 1;
      remaining_radius = remaining_radius - s->radius[uc];
      if (remaining_radius <= 0)
        {
          s->steps = uc + 1;
          break;
        }
    }
  if (remaining_radius > 0)
    s->radius[0] = s->radius[0] + remaining_radius;

  for (uc = 0; uc < s->steps; uc++)
    s->treshold[uc] = MIN (treshold, (gfloat) (uc + 1) * 2.0 / s->steps * treshold);
}

static gint
schedule_reach (const Schedule *s)
{
  gint i, sum = 0;

  for (i = 0; i < s->steps; i++)
    sum += s->radius[i];
  return sum;
}

/* the circle of one pass (circledata) */
typedef struct
{
  gint n;
  gint x_rel[MAXPOINTS];
  gint y_rel[MAXPOINTS];
} Circle;

static void
circle_make (Circle *cd,
             gint    radius)
{
  gint rad_stepping = radius / 6 + 1;
  gint c, f = 0;

  for (c = -radius; c < radius; c = c + rad_stepping)
    {
      cd->y_rel[f] = c;
      cd->x_rel[f] = (int) ROUND (pow ((gfloat) radius * radius - c * c, (gfloat) 1 / 2));
      f = f + 1;
    }
  for (c = radius; c > -radius; c = c - rad_stepping)
    {
      cd->y_rel[f] = c;
      cd->x_rel[f] = - (int) ROUND (pow ((gfloat) radius * radius - c * c, (gfloat) 1 / 2));
      f = f + 1;
    }
  cd->n = f;
}

/* one blur pass (do_mask_thread) over the rows of a region */
typedef struct
{
  const gfloat        *source;
  gfloat              *target;
  const gfloat        *rect_in;       /* the brightness, for the final check */
  const GeglRectangle *plane;         /* the planes' extent */
  const GeglRectangle *layer;         /* points outside are left out */
  GeglRectangle        region;        /* where this pass computes */
  const Circle        *circle;
  gint                 radius;
  gboolean             selective;
  gfloat               treshold;
  gboolean             finalcheck;
  gfloat               totaltresh;
} Pass;

static void
pass_rows (gsize    offset,
           gsize    size,
           gpointer data)
{
  const Pass   *ps = data;
  const Circle *cd = ps->circle;
  const gint    pw = ps->plane->width;
  const gint    lx0 = ps->layer->x, lx1 = ps->layer->x + ps->layer->width;
  const gint    ly0 = ps->layer->y, ly1 = ps->layer->y + ps->layer->height;
  gint          off[MAXPOINTS];
  gboolean      row_ok[MAXPOINTS];
  gint          c, x, y;

  for (c = 0; c < cd->n; c++)
    off[c] = cd->y_rel[c] * pw + cd->x_rel[c];

  for (y = ps->region.y + (gint) offset; y < ps->region.y + (gint) (offset + size); y++)
    {
      /* the row, indexed by x - plane->x */
      gsize         row = (gsize) (y - ps->plane->y) * pw;
      const gfloat *src = ps->source + row;
      gfloat       *dst = ps->target + row;
      gboolean      all_rows = TRUE;

      for (c = 0; c < cd->n; c++)
        {
          row_ok[c] = y + cd->y_rel[c] >= ly0 && y + cd->y_rel[c] < ly1;
          all_rows = all_rows && row_ok[c];
        }

      for (x = ps->region.x; x < ps->region.x + ps->region.width; x++)
        {
          gint     i = x - ps->plane->x;
          gfloat   centralvalue = src[i];
          gfloat   sum = centralvalue;
          gint     count = 1;
          gboolean inner = all_rows && x - ps->radius >= lx0 && x + ps->radius < lx1;

          for (c = 0; c < cd->n; c++)
            {
              gfloat localvalue;

              if (! inner &&
                  (! row_ok[c] || x + cd->x_rel[c] < lx0 || x + cd->x_rel[c] >= lx1))
                continue;
              localvalue = src[i + off[c]];
              if (ps->selective && localvalue > centralvalue + ps->treshold) continue;
              if (ps->selective && localvalue < centralvalue - ps->treshold) continue;
              sum = sum + localvalue;
              count = count + 1;
            }
          dst[i] = sum / count;
        }

      if (ps->finalcheck)
        {
          const gfloat *in = ps->rect_in + row;

          for (x = ps->region.x; x < ps->region.x + ps->region.width; x++)
            {
              gint   i = x - ps->plane->x;
              gfloat invalue = in[i];

              if (dst[i] > invalue + ps->totaltresh) dst[i] = invalue + ps->totaltresh;
              if (dst[i] < invalue - ps->totaltresh) dst[i] = invalue - ps->totaltresh;
            }
        }
    }
}

/* the part of want that is needed with reach more pixels of margin */
static GeglRectangle
grown (const GeglRectangle *want,
       gint                 reach,
       const GeglRectangle *layer)
{
  GeglRectangle r = { want->x - reach, want->y - reach,
                      want->width + 2 * reach, want->height + 2 * reach };

  gegl_rectangle_intersect (&r, &r, layer);
  return r;
}

typedef struct
{
  gint            colormode;
  gint            action;
  gint            blurmode;
  gfloat          boost[SLIDERS + 2];
  gfloat          newdark, newlight;
  gfloat          mask_boost, mask_pos;
} Params;

/* do_mask3 (): the passes of a schedule, from source to target; later
 * is the reach of the passes that come after these (the postblur) */
static void
do_mask3 (GeglOperation       *operation,
          const Params        *p,
          const Schedule      *s,
          const gfloat        *source_rect,
          gfloat              *targ_rect,
          gfloat              *tmp1,
          gfloat              *tmp2,
          const gfloat        *rect_in,
          const GeglRectangle *plane,
          const GeglRectangle *layer,
          const GeglRectangle *want,
          gint                 later,
          gfloat               totaltresh)
{
  gint remaining = schedule_reach (s);
  gint ud;

  for (ud = 0; ud < s->steps; ud++)
    {
      Circle cd;
      Pass   ps;

      remaining -= s->radius[ud];
      circle_make (&cd, s->radius[ud]);
      ps.source     = ud == 0 ? source_rect : (ud % 2 == 0 ? tmp2 : tmp1);
      ps.target     = ud == s->steps - 1 ? targ_rect : (ud % 2 == 0 ? tmp1 : tmp2);
      ps.rect_in    = rect_in;
      ps.plane      = plane;
      ps.layer      = layer;
      ps.region     = grown (want, remaining + later, layer);
      ps.circle     = &cd;
      ps.radius     = s->radius[ud];
      ps.selective  = p->blurmode != B_NORMAL;
      ps.treshold   = s->treshold[ud];
      ps.finalcheck = p->blurmode == SELBLUR && ud == s->steps - 1;
      ps.totaltresh = totaltresh;
      if (ps.region.width <= 0 || ps.region.height <= 0)
        continue;
      gegl_parallel_distribute_range (ps.region.height,
                                      gegl_operation_get_pixels_per_thread (operation)
                                        / MAX (ps.region.width * cd.n / 8, 1),
                                      pass_rows, &ps);
    }
}

/* get_new_sharp (). Change: a factor at 0.20 of its own, which holds up
 * to 1.1 (and beyond, which only float images reach): the plug-in went
 * down to boost[5], past the end of its array, which was 0 */
static gfloat
get_new_sharp (const Params *p,
               gfloat        oldsharp)
{
  gfloat weight;
  gint   i;

  if (oldsharp < 0)
    oldsharp = -1 * oldsharp;
  for (i = 0; i < SLIDERS + 1; i++)
    if (oldsharp < borders[i + 1])
      {
        weight = (oldsharp - borders[i]) / (borders[i + 1] - borders[i]);
        return p->boost[i] * (1 - weight) + p->boost[i + 1] * weight;
      }
  return p->boost[SLIDERS];
}

/* the brightness of a pixel (populate_rect_in) */
static inline gfloat
brightness (const Params *p,
            const gfloat *px)
{
  gfloat R, G, B;

  switch (p->colormode)
    {
    case HSV:
      {
        EqRGB rgb = { eq_sane (px[0]), eq_sane (px[1]), eq_sane (px[2]) };
        EqHSV hsv;

        eq_rgb_to_hsv (&rgb, &hsv);
        return hsv.v;
      }
    case HSL:
      {
        EqRGB rgb = { eq_sane (px[0]), eq_sane (px[1]), eq_sane (px[2]) };
        EqHSL hsl;

        eq_rgb_to_hsl (&rgb, &hsl);
        return hsl.l;
      }
    case RCHAN:
      return eq_gam (eq_sane (px[0]));
    case GCHAN:
      return eq_gam (eq_sane (px[1]));
    case BCHAN:
      return eq_gam (eq_sane (px[2]));
    default:
      R = eq_gam (eq_sane (px[0]));
      G = eq_gam (eq_sane (px[1]));
      B = eq_gam (eq_sane (px[2]));
      return BRIGHT (R, G, B);
    }
}

/* get_new_RGB3 () for one pixel */
static void
new_rgb (const Params *p,
         const gfloat *pixel,
         gfloat        br_old,
         gfloat        mask_old,
         gfloat       *out)
{
  const gfloat px[4] = { pixel[0], pixel[1], pixel[2], pixel[3] };
  gfloat mask_new, diff_new, br_new, sharp_boost;
  gfloat Rnew, Gnew, Bnew, R, G, B;
  gboolean own = p->colormode == BRIGHT_OWN || p->colormode == RCHAN ||
                 p->colormode == GCHAN || p->colormode == BCHAN;

  sharp_boost = get_new_sharp (p, br_old - mask_old);
  mask_new = p->newdark + mask_old * (p->newlight - p->newdark);

  if (p->action == MASKDIFF)
    {
      Rnew = (br_old - mask_old) * p->mask_boost + p->mask_pos;
      if (Rnew < 0) Rnew = 0;
      if (Rnew > 1) Rnew = 1;
      Gnew = Rnew;
      Bnew = Gnew;
    }
  else if (p->action == MASK)
    {
      /* HSV and HSL: the gray of that value or lightness */
      Rnew = Gnew = Bnew = mask_new;
    }
  else
    {
      diff_new = (br_old - mask_old) * sharp_boost;
      br_new = mask_new + diff_new;

      if (own)
        {
          R = eq_gam (eq_sane (px[0]));
          G = eq_gam (eq_sane (px[1]));
          B = eq_gam (eq_sane (px[2]));
          if (p->colormode != BRIGHT_OWN)
            br_old = BRIGHT (R, G, B);
          Rnew = br_new + (R - br_old);
          Gnew = br_new + (G - br_old);
          Bnew = br_new + (B - br_old);
        }
      else if (p->colormode == HSV)
        {
          EqRGB rgb = { eq_sane (px[0]), eq_sane (px[1]), eq_sane (px[2]) };
          EqHSV hsv;

          eq_rgb_to_hsv (&rgb, &hsv);
          /* Change: at most the pixel's own value when that is above 1
           * (the plug-in: KEEP_WITHIN_0_1, the same for its 8 bit values) */
          hsv.v = MAX (MIN (br_new, MAX (1.0, hsv.v)), 0.0);
          eq_hsv_to_rgb (&hsv, &rgb);
          Rnew = rgb.r; Gnew = rgb.g; Bnew = rgb.b;
        }
      else
        {
          EqRGB rgb = { eq_sane (px[0]), eq_sane (px[1]), eq_sane (px[2]) };
          EqHSL hsl;

          eq_rgb_to_hsl (&rgb, &hsl);
          hsl.l = MAX (MIN (br_new, MAX (1.0, hsl.l)), 0.0);
          eq_hsl_to_rgb (&hsl, &rgb);
          Rnew = rgb.r; Gnew = rgb.g; Bnew = rgb.b;
        }
    }

  if (own)
    {
      out[0] = eq_ungam (Rnew);
      out[1] = eq_ungam (Gnew);
      out[2] = eq_ungam (Bnew);
    }
  else
    {
      out[0] = Rnew;
      out[1] = Gnew;
      out[2] = Bnew;
    }
  out[3] = px[3];

  if (! (isfinite (out[0]) && isfinite (out[1]) && isfinite (out[2])))
    {
      out[0] = eq_sane (px[0]);
      out[1] = eq_sane (px[1]);
      out[2] = eq_sane (px[2]);
    }
}

typedef struct
{
  const Params        *p;
  gfloat              *pixels;      /* RGBA over the plane */
  gfloat              *rect_in;
  const gfloat        *rect_out;
  const GeglRectangle *plane;
  const GeglRectangle *want;
} Final;

static void
brightness_rows (gsize    offset,
                 gsize    size,
                 gpointer data)
{
  Final *f = data;
  gsize  i, n = (gsize) f->plane->width;

  for (i = offset * n; i < (offset + size) * n; i++)
    f->rect_in[i] = brightness (f->p, f->pixels + i * 4);
}

static void
final_rows (gsize    offset,
            gsize    size,
            gpointer data)
{
  Final *f = data;
  gint   x, y;

  for (y = f->want->y + (gint) offset; y < f->want->y + (gint) (offset + size); y++)
    for (x = f->want->x; x < f->want->x + f->want->width; x++)
      {
        gsize i = (gsize) (y - f->plane->y) * f->plane->width + (x - f->plane->x);

        new_rgb (f->p, f->pixels + i * 4, f->rect_in[i], f->rect_out[i],
                 f->pixels + i * 4);
      }
}

static void
params_get (GeglProperties *o,
            Params         *p)
{
  static const gint modes[] = { BRIGHT_OWN, HSV, HSL, RCHAN, GCHAN, BCHAN };

  p->colormode = modes[CLAMP ((gint) o->source, 0, 5)];
  p->action = o->output_mode == EQ_SHARP_OUTPUT_MASK ? MASK :
              o->output_mode == EQ_SHARP_OUTPUT_DIFFERENCE ? MASKDIFF : NORMAL;
  p->blurmode = o->blur == EQ_SHARP_BLUR_SELECTIVE ? SELBLUR :
                o->blur == EQ_SHARP_BLUR_POSTBLUR ? POSTBLUR : B_NORMAL;
  /* the plug-in keeps its settings as floats */
  p->boost[0] = o->amount_1;
  p->boost[1] = o->amount_2;
  p->boost[2] = o->amount_3;
  p->boost[3] = o->amount_4;
  p->boost[4] = o->amount_5;
  p->boost[5] = o->amount_5;
  p->newdark = o->dark;
  p->newlight = o->light;
  p->mask_boost = o->diff_boost;
  p->mask_pos = o->central_brightness;
}

/* how far the result looks: the radii of all passes */
static gint
reach (GeglProperties *o)
{
  Schedule s;
  gint     r;

  schedule_make (&s, MAX (o->radius, 1), o->threshold);
  r = schedule_reach (&s);
  if (o->blur == EQ_SHARP_BLUR_POSTBLUR)
    {
      schedule_make (&s, o->postblur_radius, 1);
      r += schedule_reach (&s);
    }
  return r;
}

static gboolean
is_neutral (GeglProperties *o)
{
  return o->output_mode == EQ_SHARP_OUTPUT_IMAGE &&
         o->amount_1 == 1.0 && o->amount_2 == 1.0 && o->amount_3 == 1.0 &&
         o->amount_4 == 1.0 && o->amount_5 == 1.0 &&
         o->dark == 0.0 && o->light == 1.0 &&
         (o->source == EQ_SHARP_SOURCE_BRIGHTNESS ||
          o->source == EQ_SHARP_SOURCE_VALUE ||
          o->source == EQ_SHARP_SOURCE_LIGHTNESS);
}

static void
prepare (GeglOperation *operation)
{
  GeglOperationAreaFilter *area   = GEGL_OPERATION_AREA_FILTER (operation);
  const Babl              *space  = gegl_operation_get_source_space (operation, "input");
  const Babl              *format = babl_format_with_space ("R'G'B'A float", space);
  gint                     r      = reach (GEGL_PROPERTIES (operation));

  area->left = area->right = area->top = area->bottom = r;

  gegl_operation_set_format (operation, "input", format);
  gegl_operation_set_format (operation, "output", format);
}

/* the result covers the layer, not the margin around it */
static GeglRectangle
get_bounding_box (GeglOperation *operation)
{
  const GeglRectangle *in = gegl_operation_source_get_bounding_box (operation, "input");

  return in ? *in : *GEGL_RECTANGLE (0, 0, 0, 0);
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
  GeglRectangle        layer, want, plane;
  Params               p;
  Schedule             main_s, post_s;
  gint                 post_reach = 0;
  gfloat              *pixels, *rect_in, *rect_out, *tmp1, *tmp2;
  gsize                n;
  Final                f;

  if (! bounds || gegl_rectangle_is_infinite_plane (bounds))
    return FALSE;
  layer = *bounds;
  if (! gegl_rectangle_intersect (&want, result, &layer))
    return TRUE;

  params_get (o, &p);
  schedule_make (&main_s, MAX (o->radius, 1), o->threshold);
  if (p.blurmode == POSTBLUR)
    {
      schedule_make (&post_s, o->postblur_radius, 1);
      post_reach = schedule_reach (&post_s);
    }
  plane = grown (&want, schedule_reach (&main_s) + post_reach, &layer);

  n        = (gsize) plane.width * plane.height;
  pixels   = g_new (gfloat, n * 4);
  rect_in  = g_new (gfloat, n);
  rect_out = g_new (gfloat, n);
  tmp1     = g_new (gfloat, n);
  tmp2     = g_new (gfloat, n);

  gegl_buffer_get (input, &plane, 1.0, format, pixels, GEGL_AUTO_ROWSTRIDE,
                   GEGL_ABYSS_NONE);

  f.p        = &p;
  f.pixels   = pixels;
  f.rect_in  = rect_in;
  f.rect_out = rect_out;
  f.plane    = &plane;
  f.want     = &want;

  gegl_parallel_distribute_range (plane.height,
                                  gegl_operation_get_pixels_per_thread (operation)
                                    / MAX (plane.width, 1),
                                  brightness_rows, &f);

  do_mask3 (operation, &p, &main_s, rect_in, rect_out, tmp1, tmp2, rect_in,
            &plane, &layer, &want, post_reach, (gfloat) o->threshold);

  if (p.blurmode == POSTBLUR)
    {
      /* rect_post: a copy of the mask, blurred again into rect_out; the
       * copy is only read by the first pass, which writes tmp1 */
      memcpy (tmp2, rect_out, n * sizeof (gfloat));
      do_mask3 (operation, &p, &post_s, tmp2, rect_out, tmp1, tmp2,
                rect_in, &plane, &layer, &want, 0, 1);
    }

  gegl_parallel_distribute_range (want.height,
                                  gegl_operation_get_pixels_per_thread (operation)
                                    / MAX (want.width, 1),
                                  final_rows, &f);

  gegl_buffer_set (output, &want, 0, format,
                   pixels + ((gsize) (want.y - plane.y) * plane.width + (want.x - plane.x)) * 4,
                   plane.width * 4 * sizeof (gfloat));

  g_free (pixels);
  g_free (rect_in);
  g_free (rect_out);
  g_free (tmp1);
  g_free (tmp2);
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

  operation_class->prepare          = prepare;
  operation_class->process          = operation_process;
  operation_class->get_bounding_box = get_bounding_box;
  /* the rows of each pass are shared out among GEGL's threads in
   * process (), so that the margins are computed once */
  operation_class->threaded         = FALSE;
  filter_class->process             = process;

  gegl_operation_class_set_keys (operation_class,
    "name",            "equalizer:sharpness",
    "title",           _("Advanced Unsharp Mask"),
    "categories",      "enhance:sharpen",
    "description",     _("Sharpens by the difference between each pixel's "
                         "brightness and a blurred mask, with a factor "
                         "for each size of that difference like an "
                         "equalizer; standard or selective blur, and "
                         "brightness levels through the mask. A port of "
                         "Tibor Bamhor's GIMP 2 plug-in AUMASK"),
    "gimp:menu-path",  "<Image>/Filters/Enhance",
    "gimp:menu-label", _("Advanced Unsharp Mask..."),
    NULL);
}

#endif
