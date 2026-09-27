/*
 * Automated checks for equalizer:saturation and equalizer:sharpness
 *
 * check.c
 * Copyright 2026 by David
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * What the operations must do whatever the plug-ins did (the
 * comparison with the plug-ins themselves is tests/orig-check.sh):
 * identity at the neutral settings, also through the arithmetic; grays,
 * levels, corners, temperature, balance and the color limit doing what
 * they say; the unsharp mask's mask, amounts, selective blur, reach and
 * outputs; the same result in any tiles, with any number of threads,
 * for a layer anywhere; alpha kept bit for bit; NaN, infinities and
 * values outside 0 to 1; 8 and 16 bit, half, float, double, linear and
 * grayscale buffers, and another color space; extreme settings.
 *
 *   check <build folder>
 *
 * Prints PASS or FAIL per case and exits with 1 if any case failed.
 */

#include <gegl.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modules.h"

#define WORK "R'G'B'A float"
#define SAT  "equalizer:saturation"
#define SHP  "equalizer:sharpness"

static gint n_failed   = 0;
static gint n_passed   = 0;
static gint n_messages = 0;

static void
log_handler (const gchar    *domain,
             GLogLevelFlags  level,
             const gchar    *message,
             gpointer        data)
{
  (void) data;
  if (level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING))
    n_messages++;
  g_log_default_handler (domain, level, message, NULL);
}

static void report (const gchar *name, gboolean ok, const gchar *format, ...)
  G_GNUC_PRINTF (3, 4);

static void
report (const gchar *name,
        gboolean     ok,
        const gchar *format,
        ...)
{
  printf ("%s  %s", ok ? "PASS" : "FAIL", name);
  if (format)
    {
      va_list args;
      gchar  *detail;

      va_start (args, format);
      detail = g_strdup_vprintf (format, args);
      va_end (args);
      printf (": %s", detail);
      g_free (detail);
    }
  printf ("\n");
  fflush (stdout);
  if (ok)
    n_passed++;
  else
    n_failed++;
}

/* running an operation ------------------------------------------------ */

static void
set_props (GeglNode    *node,
           const gchar *props)
{
  gchar **p = g_strsplit (props ? props : "", " ", -1);
  gint    i;

  for (i = 0; p[i]; i++)
    if (p[i][0] && ! node_set_from_string (node, p[i]))
      report ("set property", FALSE, "%s", p[i]);
  g_strfreev (p);
}

/* op on a w x h image at (x0, y0) in the given format; the result of
 * the rectangle roi (NULL: all) as out_format; g_free it */
static gpointer
run_full (const gchar         *op,
          const gchar         *props,
          const Babl          *in_format,
          gconstpointer        in,
          gint                 x0,
          gint                 y0,
          gint                 w,
          gint                 h,
          const GeglRectangle *roi,
          const Babl          *out_format)
{
  GeglBuffer   *buf = gegl_buffer_new (GEGL_RECTANGLE (x0, y0, w, h), in_format);
  GeglNode     *graph, *src, *node;
  GeglRectangle r = roi ? *roi : *GEGL_RECTANGLE (x0, y0, w, h);
  gpointer      out;

  gegl_buffer_set (buf, NULL, 0, in_format, in, GEGL_AUTO_ROWSTRIDE);
  graph = gegl_node_new ();
  src   = gegl_node_new_child (graph, "operation", "gegl:buffer-source",
                               "buffer", buf, NULL);
  node  = gegl_node_new_child (graph, "operation", op, NULL);
  set_props (node, props);
  gegl_node_link (src, node);
  out = g_malloc0 ((gsize) r.width * r.height * babl_format_get_bytes_per_pixel (out_format));
  gegl_node_blit (node, 1.0, &r, out_format, out, GEGL_AUTO_ROWSTRIDE, GEGL_BLIT_DEFAULT);
  g_object_unref (graph);
  g_object_unref (buf);
  return out;
}

static gfloat *
run (const gchar  *op,
     const gchar  *props,
     const gfloat *in,
     gint          w,
     gint          h)
{
  return run_full (op, props, babl_format (WORK), in, 0, 0, w, h, NULL, babl_format (WORK));
}

static gdouble
max_diff (const gfloat *a,
          const gfloat *b,
          gint          n_pixels,
          gboolean      with_alpha)
{
  gdouble d = 0;
  gint    i, k;

  for (i = 0; i < n_pixels; i++)
    for (k = 0; k < (with_alpha ? 4 : 3); k++)
      {
        gdouble e = fabs ((gdouble) a[4 * i + k] - b[4 * i + k]);

        if (isnan (e))
          return INFINITY;
        d = MAX (d, e);
      }
  return d;
}

static gboolean
same_bits (const gfloat *a,
           const gfloat *b,
           gint          n_pixels)
{
  return memcmp (a, b, (gsize) n_pixels * 4 * sizeof (gfloat)) == 0;
}

static gboolean
alpha_kept (const gfloat *in,
            const gfloat *out,
            gint          n)
{
  gint i;

  for (i = 0; i < n; i++)
    if (memcmp (&in[4 * i + 3], &out[4 * i + 3], sizeof (gfloat)) != 0)
      return FALSE;
  return TRUE;
}

static gboolean
all_finite (const gfloat *p,
            gint          n)
{
  gint i, k;

  for (i = 0; i < n; i++)
    for (k = 0; k < 3; k++)
      if (! isfinite (p[4 * i + k]))
        return FALSE;
  return TRUE;
}

static GRand *rnd;

/* a photo-like image: smooth color, edges, texture; random alpha */
static gfloat *
scene (gint w,
       gint h)
{
  gfloat *p = g_new (gfloat, (gsize) w * h * 4);
  gint    x, y;

  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        gfloat *q = p + ((gsize) y * w + x) * 4;
        gfloat  n = (g_rand_double (rnd) - 0.5) * 0.04;

        q[0] = 0.5 + 0.4 * sinf (x * 0.05f) + n;
        q[1] = (gfloat) y / h + n;
        q[2] = ((x / 16 + y / 16) % 2) ? 0.8 : 0.15;
        if ((x - w / 2) * (x - w / 2) + (y - h / 2) * (y - h / 2) < h * h / 16)
          q[0] = q[1] = q[2] = 0.95f;
        q[3] = (x % 7 == 0) ? (gfloat) g_rand_double (rnd) : 1.0f;
      }
  return p;
}

static gfloat *
uniform (gint w, gint h, gfloat r, gfloat g, gfloat b)
{
  gfloat *p = g_new (gfloat, (gsize) w * h * 4);
  gsize   i;

  for (i = 0; i < (gsize) w * h; i++)
    {
      p[4 * i] = r; p[4 * i + 1] = g; p[4 * i + 2] = b; p[4 * i + 3] = 1;
    }
  return p;
}

static gdouble gam (gdouble v) { return v >= 0 ? pow (v, 1 / 2.2) : -pow (-v, 1 / 2.2); }
static gdouble ungam (gdouble v) { return v >= 0 ? pow (v, 2.2) : -pow (-v, 2.2); }

/* settings of each op that are not neutral */
static const gchar *const sat_settings[] = {
  "band_1=2 band_2=1.8 band_3=1.5 band_4=1.2",
  "band_1=0.3 band_6=2 color_mode=hsv",
  "band_2=2 color_mode=hsl temperature=0.4",
  "band_3=2.5 color_mode=yuv dark=0.1 light=0.9",
  "band_1=3 color_mode=safe-quadratic corner_top_left=0.1 corner_bottom_right=-0.15",
  "band_2=1.5 balance=layer balance_weight=0.8",
  "band_2=1.5 balance=color balance_color=#c0d0e0 limit_range=0.05",
  "band_1=2 limit_range=-0.05 limit_color=#40a040 color_mode=hsv",
};
static const gchar *const shp_settings[] = {
  "amount_1=1.2 amount_2=1.6 amount_3=2 amount_4=2 amount_5=1.5",
  "amount_2=2 blur=selective threshold=0.1 radius=12",
  "amount_3=2.5 blur=postblur postblur_radius=4 source=value",
  "amount_2=0.3 source=lightness dark=0.1 light=0.9 radius=40",
  "amount_4=2 source=red radius=7",
  "output_mode=mask radius=15 source=green",
  "output_mode=difference diff_boost=-4 central_brightness=0.4 radius=3",
  "amount_1=2 radius=100",
};

/* identity ------------------------------------------------------------ */

static void
check_identity (void)
{
  gint    w = 97, h = 61, n = w * h, i;
  gfloat *in = scene (w, h), *out;

  /* values outside 0 to 1 too */
  for (i = 0; i < n; i += 11)
    {
      in[4 * i] = 1.7f;
      in[4 * i + 2] = -0.3f;
    }

  out = run (SAT, NULL, in, w, h);
  report ("saturation_defaults_pass_through", same_bits (in, out, n), NULL);
  g_free (out);
  out = run (SHP, NULL, in, w, h);
  report ("sharpness_defaults_pass_through", same_bits (in, out, n), NULL);
  g_free (out);
  out = run (SHP, "radius=300 blur=postblur source=lightness", in, w, h);
  report ("sharpness_neutral_amounts_pass_through", same_bits (in, out, n), NULL);
  g_free (out);

  /* through the arithmetic: a zero shift forces the pixels to be worked
   * on; within 0 to 1 the result must be the input (Safe Quadratic,
   * HSV and HSL keep colors within 0 to 1 at any setting). YUV goes
   * back with the plug-in's matrix of three digits, which is not quite
   * the inverse: 5e-4 at most, 0.14 of an 8 bit level */
  for (i = 0; i < n; i++)
    {
      gint k;

      for (k = 0; k < 3; k++)
        in[4 * i + k] = CLAMP (in[4 * i + k], 0.0f, 1.0f);
    }
  {
    static const gchar *const modes[] = { "quadratic", "safe-quadratic", "hsv", "hsl", "yuv" };
    gint m;

    for (m = 0; m < 5; m++)
      {
        gchar *props = g_strdup_printf ("balance=color balance_color=#808080 color_mode=%s",
                                        modes[m]);
        gchar *name  = g_strdup_printf ("saturation_identity_through_the_arithmetic_%s", modes[m]);
        gdouble d;

        out = run (SAT, props, in, w, h);
        d = max_diff (in, out, n, TRUE);
        report (name, d < (m == 4 ? 1e-3 : 2e-6), "max difference %.2g", d);
        g_free (out);
        g_free (props);
        g_free (name);
      }
    for (m = 0; m < 3; m++)
      {
        static const gchar *const src[] = { "brightness", "value", "lightness" };
        gchar  *props = g_strdup_printf ("light=1.0000000001 source=%s blur=selective", src[m]);
        gchar  *name  = g_strdup_printf ("sharpness_identity_through_the_arithmetic_%s", src[m]);
        gdouble d;

        out = run (SHP, props, in, w, h);
        d = max_diff (in, out, n, TRUE);
        report (name, d < 2e-6, "max difference %.2g", d);
        g_free (out);
        g_free (props);
        g_free (name);
      }
  }
  g_free (in);
}

/* Saturation Equalizer ------------------------------------------------- */

static gdouble
sat_of (const gfloat *p)
{
  gdouble mx = MAX (p[0], MAX (p[1], p[2])), mn = MIN (p[0], MIN (p[1], p[2]));

  return mx - mn;
}

static void
check_saturation (void)
{
  static const gchar *const modes[] = { "quadratic", "safe-quadratic", "hsv", "hsl", "yuv" };
  gint    m, w = 64, h = 1, i;
  gfloat *in = g_new (gfloat, 4 * w), *out, *out2;

  /* grays of every brightness, then colors of growing saturation */
  for (i = 0; i < 32; i++)
    {
      in[4 * i] = in[4 * i + 1] = in[4 * i + 2] = i / 31.0f;
      in[4 * i + 3] = 1;
    }
  for (i = 32; i < 64; i++)
    {
      gfloat s = (i - 32) / 31.0f;

      in[4 * i] = 0.5f + 0.3f * s;
      in[4 * i + 1] = 0.5f;
      in[4 * i + 2] = 0.5f - 0.3f * s;
      in[4 * i + 3] = 1;
    }

  for (m = 0; m < 5; m++)
    {
      gchar  *props = g_strdup_printf ("band_1=4 band_2=3 band_3=2 band_4=2 band_5=1.5 "
                                       "band_6=1.2 color_mode=%s", modes[m]);
      gchar  *name;
      gdouble dgray = 0;
      gint    more = 0, less = 0;

      out = run (SAT, props, in, w, h);
      for (i = 0; i < 32; i++)
        dgray = MAX (dgray, sat_of (out + 4 * i));
      name = g_strdup_printf ("saturation_grays_stay_gray_%s", modes[m]);
      report (name, dgray < 1e-5, "largest spread of R, G, B %.2g", dgray);
      g_free (name);
      for (i = 33; i < 64; i++)
        {
          more += sat_of (out + 4 * i) > sat_of (in + 4 * i) + 1e-6;
          less += sat_of (out + 4 * i) < sat_of (in + 4 * i) - 1e-6;
        }
      name = g_strdup_printf ("saturation_bands_above_1_saturate_%s", modes[m]);
      report (name, more > 25 && less == 0, "%d of 31 colors more saturated, %d less",
              more, less);
      g_free (name);
      g_free (out);
      g_free (props);

      props = g_strdup_printf ("band_1=0 band_2=0 band_3=0 band_4=0 band_5=0 band_6=0 "
                               "color_mode=%s", modes[m]);
      out = run (SAT, props, in, w, h);
      {
        gint fewer = 0;

        for (i = 33; i < 64; i++)
          fewer += sat_of (out + 4 * i) < sat_of (in + 4 * i) - 1e-4;
        name = g_strdup_printf ("saturation_bands_at_0_desaturate_%s", modes[m]);
        report (name, fewer == 31, "%d of 31 colors less saturated", fewer);
        g_free (name);
      }
      g_free (out);
      g_free (props);
    }

  /* levels: black becomes dark, white becomes light (grays, values of
   * pow (v, 1 / 2.2)) */
  out = run (SAT, "dark=0.1 light=0.8", in, w, h);
  {
    gdouble eb = fabs (out[0] - ungam (0.1)), ew = fabs (out[4 * 31] - ungam (0.8));

    report ("saturation_levels_move_black_and_white", eb < 1e-6 && ew < 1e-6,
            "black to %.6f (want %.6f), white to %.6f (want %.6f)", out[0], ungam (0.1),
            out[4 * 31], ungam (0.8));
  }
  g_free (out);

  /* temperature: warmer means more red and less blue at the same
   * quadratic brightness */
  out = run (SAT, "temperature=0.5", in, w, h);
  {
    gint    ok = 0;
    gdouble db = 0;

    for (i = 5; i < 28; i++)
      {
        gdouble r = gam (out[4 * i]), g = gam (out[4 * i + 1]), b = gam (out[4 * i + 2]);
        gdouble v = gam (in[4 * i]);

        ok += out[4 * i] > in[4 * i] && out[4 * i + 2] < in[4 * i + 2];
        db = MAX (db, fabs (sqrt (0.241 * r * r + 0.691 * g * g + 0.068 * b * b) - v));
      }
    report ("saturation_temperature_warms_at_the_same_brightness", ok == 23 && db < 2e-3,
            "%d of 23 grays warmer, brightness off by at most %.2g", ok, db);
  }
  g_free (out);
  g_free (in);

  /* per-corner brightness on a gray layer: the top left pixel gets the
   * top left amount, the far corners nearly theirs */
  {
    gint    cw = 40, ch = 30;
    gfloat *g  = uniform (cw, ch, 0.4f, 0.4f, 0.4f);
    gdouble v  = gam (0.4);

    out = run (SAT, "corner_top_left=0.1 corner_top_right=-0.1 corner_bottom_left=0.05 "
               "corner_bottom_right=-0.2", g, cw, ch);
    {
      gdouble tl = gam (out[0]) - v;
      gdouble br = gam (out[4 * (cw * ch - 1)]) - v;
      gdouble c2 = -0.2 * (1 - 1.0 / cw) * (1 - 1.0 / ch) + 0.05 * (1.0 / cw) * (1 - 1.0 / ch)
                   - 0.1 * (1 - 1.0 / cw) * (1.0 / ch) + 0.1 * (1.0 / cw) * (1.0 / ch);

      report ("saturation_corners", fabs (tl - 0.1) < 1e-5 && fabs (br - c2) < 1e-5,
              "top left +%.5f (want 0.1), bottom right %.5f (want %.5f)", tl, br, c2);
    }
    g_free (out);
    g_free (g);
  }

  /* balance: a layer of one color cast becomes gray, from the layer or
   * from the picked color; half the weight halves the shift */
  {
    gint    bw = 50, bh = 20;
    gfloat *c  = uniform (bw, bh, 0.55f, 0.6f, 0.75f);
    gdouble d1, d2, d3;

    out = run (SAT, "balance=layer", c, bw, bh);
    d1 = sat_of (out);
    out2 = run (SAT, "balance=color balance_color=rgb(0.2630,0.3185,0.5225)", c, bw, bh);
    d2 = max_diff (out, out2, bw * bh, TRUE);
    g_free (out2);
    out2 = run (SAT, "balance=layer balance_weight=0.5", c, bw, bh);
    d3 = fabs ((gam (out2[2]) - gam (out2[0])) - 0.5 * (gam (c[2]) - gam (c[0])));
    report ("saturation_balance_makes_the_cast_gray", d1 < 1e-6 && d2 < 2e-3 && d3 < 1e-6,
            "spread %.2g; picked color vs layer %.2g; half weight off by %.2g", d1, d2, d3);
    g_free (out);
    g_free (out2);
    g_free (c);
  }

  /* the limit: only colors near the picked one change (right of 0), or
   * only the others (left of 0) */
  {
    gfloat two[8] = { 0.72f, 0.52f, 0.45f, 1, 0.3f, 0.5f, 0.8f, 1 };  /* skin, blue */

#define ALL_BANDS "band_1=2.5 band_2=2.5 band_3=2.5 band_4=2.5 band_5=2.5 band_6=2.5 "
    out  = run (SAT, ALL_BANDS "limit_range=0.05", two, 2, 1);
    out2 = run (SAT, ALL_BANDS "limit_range=-0.05", two, 2, 1);
    report ("saturation_limit_to_color",
            sat_of (out) > sat_of (two) + 0.01 && max_diff (out + 4, two + 4, 1, TRUE) < 1e-6 &&
            max_diff (out2, two, 1, TRUE) < 1e-6 && sat_of (out2 + 4) > sat_of (two + 4) + 0.01,
            "limit: skin %.3f to %.3f, blue kept; inverted: skin kept, blue %.3f to %.3f",
            sat_of (two), sat_of (out), sat_of (two + 4), sat_of (out2 + 4));
    g_free (out);
    g_free (out2);
  }

  /* the whole-layer balance gives the same pixels for a small part of
   * the layer */
  {
    gint          sw = 120, sh = 80;
    gfloat       *s  = scene (sw, sh);
    GeglRectangle part = { 30, 20, 17, 9 };
    gfloat       *all, *some;
    gint          y;
    gboolean      same = TRUE;

    all  = run (SAT, "balance=layer band_2=1.5", s, sw, sh);
    some = run_full (SAT, "balance=layer band_2=1.5", babl_format (WORK), s, 0, 0, sw, sh,
                     &part, babl_format (WORK));
    for (y = 0; y < part.height; y++)
      same = same && memcmp (some + (gsize) y * part.width * 4,
                             all + ((gsize) (part.y + y) * sw + part.x) * 4,
                             (gsize) part.width * 4 * sizeof (gfloat)) == 0;
    report ("saturation_balance_of_the_layer_in_a_part", same, NULL);
    g_free (all);
    g_free (some);
    g_free (s);
  }
}

/* Advanced Unsharp Mask ------------------------------------------------ */

static void
check_sharpness (void)
{
  gint    w = 60, h = 40, n = w * h, x, y;
  gfloat *u = uniform (w, h, 0.3f, 0.5f, 0.2f), *out, *out2, *step;
  gdouble d;

  out = run (SHP, "amount_1=2.5 amount_2=0 amount_3=2 amount_4=2.5 amount_5=0 "
             "blur=selective radius=30", u, w, h);
  d = max_diff (u, out, n, TRUE);
  report ("sharpness_uniform_layer_unchanged", d < 2e-6, "max difference %.2g", d);
  g_free (out);
  out = run (SHP, "output_mode=difference", u, w, h);
  d = fabs (out[0] - ungam (0.5)) + fabs (out[4 * (n - 1) + 2] - ungam (0.5));
  report ("sharpness_difference_of_a_uniform_layer_is_the_central_brightness", d < 1e-6,
          "%.6f (want %.6f)", out[0], ungam (0.5));
  g_free (out);
  g_free (u);

  /* a gray step: dark left, light right */
  step = g_new (gfloat, n * 4);
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        gfloat *q = step + (y * w + x) * 4;

        q[0] = q[1] = q[2] = x < w / 2 ? 0.2f : 0.6f;
        q[3] = 1;
      }
  out = run (SHP, "amount_1=2 amount_2=2 amount_3=2 amount_4=2 amount_5=2 radius=6", step, w, h);
  {
    gint   r = 20 * w + w / 2, l = r - 1;
    gfloat far = out[4 * (20 * w + 2)];

    report ("sharpness_amounts_raise_edge_contrast",
            out[4 * r] > 0.6f + 0.02f && out[4 * l] < 0.2f - 0.02f && fabsf (far - 0.2f) < 1e-6,
            "next to the edge %.3f and %.3f (were 0.2 and 0.6), far away %.6f",
            out[4 * l], out[4 * r], far);
  }
  g_free (out);
  out  = run (SHP, "amount_1=0 amount_2=0 amount_3=0 amount_4=0 amount_5=0 radius=6", step, w, h);
  out2 = run (SHP, "output_mode=mask radius=6", step, w, h);
  d = max_diff (out, out2, n, FALSE);
  report ("sharpness_amounts_at_0_give_the_mask_on_grays", d < 1e-6, "max difference %.2g", d);
  g_free (out);
  g_free (out2);
  out = run (SHP, "output_mode=mask blur=selective threshold=0.1 radius=10", step, w, h);
  d = MAX (fabs (out[4 * (20 * w + w / 2 - 1)] - 0.2), fabs (out[4 * (20 * w + w / 2)] - 0.6));
  report ("sharpness_selective_mask_keeps_the_edge", d < 1e-6, "max difference %.2g", d);
  g_free (out);

  /* reach: a pixel changed farther away than the radii add up to makes
   * no difference; nearer it does */
  {
    gfloat *far = g_memdup2 (step, (gsize) n * 4 * sizeof (gfloat));
    gfloat *o1, *o2;

    far[4 * (5 * w + 5)] = 1.0f;         /* 15 pixels from (20, 20) */
    o1 = run (SHP, "amount_2=2 radius=14", step, w, h);
    o2 = run (SHP, "amount_2=2 radius=14", far, w, h);
    report ("sharpness_reach_is_the_radius",
            memcmp (o1 + 4 * (20 * w + 20), o2 + 4 * (20 * w + 20), 16) == 0 &&
            memcmp (o1 + 4 * (5 * w + 18), o2 + 4 * (5 * w + 18), 16) != 0,
            "a pixel 15 away from a radius 14 result changes nothing, one 13 away does");
    g_free (o1);
    g_free (o2);
    g_free (far);
  }
  g_free (step);
}

/* tiles, threads, offsets ---------------------------------------------- */

/* the whole image at once, and in pieces of odd sizes */
static void
check_tiles (void)
{
  gint    w = 311, h = 203, n = w * h;
  gfloat *in = scene (w, h);
  guint   s;
  gint    threads;

  g_object_get (gegl_config (), "threads", &threads, NULL);
  for (s = 0; s < G_N_ELEMENTS (sat_settings) + G_N_ELEMENTS (shp_settings); s++)
    {
      gboolean     is_sat = s < G_N_ELEMENTS (sat_settings);
      const gchar *op     = is_sat ? SAT : SHP;
      const gchar *props  = is_sat ? sat_settings[s] : shp_settings[s - G_N_ELEMENTS (sat_settings)];
      gfloat      *whole, *pieces, *one, *moved;
      gint         px, py;
      gchar       *name;
      gboolean     ok1, ok2, ok3;

      whole  = run (op, props, in, w, h);
      pieces = g_new0 (gfloat, (gsize) n * 4);
      for (py = 0; py < h; py += 37)
        for (px = 0; px < w; px += 53)
          {
            GeglRectangle r = { px, py, MIN (53, w - px), MIN (37, h - py) };
            gfloat       *part = run_full (op, props, babl_format (WORK), in, 0, 0, w, h,
                                           &r, babl_format (WORK));
            gint          y;

            for (y = 0; y < r.height; y++)
              memcpy (pieces + ((gsize) (r.y + y) * w + r.x) * 4,
                      part + (gsize) y * r.width * 4, (gsize) r.width * 4 * sizeof (gfloat));
            g_free (part);
          }
      g_object_set (gegl_config (), "threads", 1, NULL);
      one = run (op, props, in, w, h);
      g_object_set (gegl_config (), "threads", threads, NULL);
      /* the layer somewhere else: the same pixels */
      moved = run_full (op, props, babl_format (WORK), in, 137, -61, w, h, NULL,
                        babl_format (WORK));
      ok1 = same_bits (whole, pieces, n);
      ok2 = same_bits (whole, one, n);
      ok3 = same_bits (whole, moved, n);
      name = g_strdup_printf ("%s_same_in_pieces_threads_and_places_%u",
                              is_sat ? "saturation" : "sharpness", s);
      report (name, ok1 && ok2 && ok3 && alpha_kept (in, whole, n) && all_finite (whole, n),
              "%s: in 53 x 37 pieces %s, 1 thread vs %d %s, moved layer %s, alpha %s", props,
              ok1 ? "same" : "DIFFERENT", threads, ok2 ? "same" : "DIFFERENT",
              ok3 ? "same" : "DIFFERENT", alpha_kept (in, whole, n) ? "kept" : "CHANGED");
      g_free (name);
      g_free (whole);
      g_free (pieces);
      g_free (one);
      g_free (moved);
    }
  g_free (in);
}

/* NaN, infinities, far out values, extremes ---------------------------- */

static void
check_bad_values (void)
{
  static const gchar *const extremes[] = {
    "band_1=4 band_2=3.75 band_3=3.5 band_4=3.25 band_5=3 band_6=2.75 temperature=1 dark=-0.4 light=1.4",
    "band_1=0 band_2=0 band_3=0 band_4=0 band_5=0 band_6=0 temperature=-1 dark=0.8 light=0.5 color_mode=hsv",
    "band_1=4 color_mode=hsl temperature=1 corner_top_left=0.2 corner_bottom_right=-0.2",
    "band_1=4 color_mode=yuv balance=layer limit_range=0.08",
    "band_1=4 color_mode=safe-quadratic limit_range=-0.08",
    NULL,
    "amount_1=2.5 amount_2=2.5 amount_3=2.5 amount_4=2.5 amount_5=2.5 radius=500",
    "amount_1=0 amount_5=2.5 blur=selective threshold=0.02 radius=3",
    "amount_2=2.5 blur=postblur threshold=1 postblur_radius=10 source=value dark=-0.4 light=1.4",
    "amount_3=2.5 source=lightness dark=0.7 light=0.5",
    "output_mode=difference diff_boost=10 source=blue",
    NULL,
  };
  gint    w = 40, h = 30, n = w * h, i, k;
  gfloat *in = scene (w, h);
  gboolean sat = TRUE;

  in[4 * (15 * w + 20) + 0] = NAN;
  in[4 * (15 * w + 21) + 1] = INFINITY;
  in[4 * (15 * w + 22) + 2] = -INFINITY;
  in[4 * (10 * w + 5) + 0] = 1e30f;
  in[4 * (10 * w + 6) + 1] = -1e30f;
  in[4 * (10 * w + 7) + 2] = 3.5f;
  in[4 * (10 * w + 8) + 2] = -0.5f;
  in[4 * (20 * w + 3) + 3] = NAN;        /* alpha as it is, even NaN */

  for (i = 0; i < (gint) G_N_ELEMENTS (extremes); i++)
    {
      gfloat *out;
      gchar  *name;

      if (! extremes[i])
        {
          sat = FALSE;
          continue;
        }
      out = run (sat ? SAT : SHP, extremes[i], in, w, h);
      name = g_strdup_printf ("%s_nan_infinity_and_extremes_%d", sat ? "saturation" : "sharpness", i);
      report (name, all_finite (out, n) && alpha_kept (in, out, n), "%s", extremes[i]);
      g_free (name);
      g_free (out);
    }

  /* 1 x 1 and 1 x n layers with a large radius */
  for (k = 0; k < 2; k++)
    {
      gint    lw = k ? 1 : 1, lh = k ? 17 : 1;
      gfloat *l = scene (lw, lh), *out;
      gchar  *name = g_strdup_printf ("sharpness_%d_x_%d_layer", lw, lh);

      out = run (SHP, "amount_2=2 radius=500 blur=postblur", l, lw, lh);
      report (name, all_finite (out, lw * lh), NULL);
      g_free (out);
      g_free (l);
      g_free (name);
    }
  g_free (in);
}

/* formats and color spaces ---------------------------------------------- */

static void
check_formats (void)
{
  static const gchar *const formats[] = {
    "R'G'B'A u8", "R'G'B' u16", "R'G'B'A half", "R'G'B'A double", "RGBA float", "Y' u8",
  };
  gint    w = 70, h = 44, n = w * h, f;
  gfloat *in = scene (w, h);
  guint   s;

  for (s = 0; s < 2; s++)
    for (f = 0; f < (gint) G_N_ELEMENTS (formats); f++)
      {
        const gchar *op    = s ? SHP : SAT;
        const gchar *props = s ? shp_settings[0] : sat_settings[0];
        const Babl  *fmt   = babl_format (formats[f]);
        gsize        bpp   = babl_format_get_bytes_per_pixel (fmt);
        guchar      *q     = g_malloc ((gsize) n * bpp);
        gfloat      *qf    = g_new (gfloat, (gsize) n * 4), *want, *got;
        gchar       *name;
        gdouble      d, tol;

        /* the input as stored in that format, back to float */
        babl_process (babl_fish (babl_format (WORK), fmt), in, q, n);
        babl_process (babl_fish (fmt, babl_format (WORK)), q, qf, n);
        want = run (op, props, qf, w, h);
        got  = run_full (op, props, fmt, q, 0, 0, w, h, NULL, babl_format (WORK));
        d    = max_diff (want, got, n, FALSE);
        tol  = strstr (formats[f], "half") ? 1e-6 : 1e-6;
        name = g_strdup_printf ("%s_on_%s", s ? "sharpness" : "saturation", formats[f]);
        report (name, d <= tol, "max difference to the same pixels in float %.2g", d);
        g_free (name);
        g_free (want);
        g_free (got);
        g_free (q);
        g_free (qf);
      }

  /* another color space: the operation works on that space's own
   * values, so the same numbers give the same numbers */
  for (s = 0; s < 2; s++)
    {
      const Babl *space = babl_space ("ProPhoto");
      const Babl *fmt   = babl_format_with_space (WORK, space);
      gfloat     *a     = run (s ? SHP : SAT, s ? shp_settings[2] : sat_settings[1], in, w, h);
      gfloat     *b     = run_full (s ? SHP : SAT, s ? shp_settings[2] : sat_settings[1],
                                    fmt, in, 0, 0, w, h, NULL, fmt);
      gdouble     d     = max_diff (a, b, n, TRUE);
      gchar      *name  = g_strdup_printf ("%s_in_the_values_of_another_color_space",
                                           s ? "sharpness" : "saturation");

      report (name, space != NULL && d == 0, "ProPhoto: max difference %.2g", d);
      g_free (name);
      g_free (a);
      g_free (b);
    }
  g_free (in);
}

/* the operations' keys ------------------------------------------------- */

static void
check_keys (void)
{
  static const gchar *const ops[][3] = {
    { SAT, "<Image>/Colors", "Saturation Equalizer..." },
    { SHP, "<Image>/Filters/Enhance", "Advanced Unsharp Mask..." },
  };
  gint i;

  for (i = 0; i < 2; i++)
    {
      const gchar *path  = gegl_operation_get_key (ops[i][0], "gimp:menu-path");
      const gchar *label = gegl_operation_get_key (ops[i][0], "gimp:menu-label");
      gchar       *name  = g_strdup_printf ("menu_%s", ops[i][0]);

      report (name, g_strcmp0 (path, ops[i][1]) == 0 && g_strcmp0 (label, ops[i][2]) == 0,
              "%s, %s", path, label);
      g_free (name);
    }
}

int
main (int    argc,
      char **argv)
{
  gchar *dir;

  if (argc != 2)
    {
      fprintf (stderr, "usage: %s <build folder>\n", argv[0]);
      return 2;
    }
  setlocale (LC_ALL, "C");
  dir = modules_setup (argv[1]);
  if (! dir)
    return 2;
  gegl_init (NULL, NULL);
  g_log_set_default_handler (log_handler, NULL);
  rnd = g_rand_new_with_seed (7);

  if (! gegl_has_operation (SAT) || ! gegl_has_operation (SHP))
    {
      report ("operations_load", FALSE, "not found in %s", argv[1]);
      return 1;
    }
  check_keys ();
  check_identity ();
  check_saturation ();
  check_sharpness ();
  check_tiles ();
  check_bad_values ();
  check_formats ();

  report ("no_warnings_from_glib_or_gegl", n_messages == 0, "%d", n_messages);
  g_rand_free (rnd);
  gegl_exit ();
  if (! g_getenv ("EQ_CHECK_KEEP_MODULE"))
    modules_cleanup (dir);
  printf ("%d passed, %d failed\n", n_passed, n_failed);
  return n_failed ? 1 : 0;
}
