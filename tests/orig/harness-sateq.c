/*
 * Runs the computing part of the original Saturation Equalizer 0.9.1
 * (Tibor Bamhor's saturate.c, taken out by extract.py, with or without
 * sateq-fixes.patch) on a raw 8 bit image, as GIMP 2 ran it on a layer
 * without a selection.
 *
 * harness-sateq.c
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
 *   harness-sateq <in.raw> <width> <height> <channels> <out.raw> [name=value ...]
 *
 * The names are the plug-in's settings (maindata): boost0 to boost5,
 * levels0 (dark), levels1 (light), corner0 to corner3 (top left, top
 * right, bottom left, bottom right), temperature, skin, balweight,
 * formula (quadratic, safe, hsv, hsl, yuv), skincolor=RRGGBB (the color
 * button), and calibrate=image or calibrate=RRGGBB: the "Calculate and
 * Apply" button of the color auto balance, with the preview showing the
 * whole image or only that color.
 */

/* the plug-in's code comes first: tests/orig-check.sh makes a file that
 * includes it and then this one */
#include "harness-common.h"

int
main (int argc, char **argv)
{
  GimpDrawable d = { 1 };
  const char  *calib = NULL;
  int          i;

  if (argc < 6)
    {
      fprintf (stderr, "usage: %s <in.raw> <width> <height> <channels> <out.raw> [name=value ...]\n",
               argv[0]);
      return 2;
    }
  harness_load (argv);
  fake_color = skincolor;

  for (i = 6; i < argc; i++)
    {
      const char *v = NULL;
      char        name[16];
      int         k;

      for (k = 0; k < SLIDERS && ! v; k++)
        {
          g_snprintf (name, sizeof name, "boost%d", k);
          if ((v = arg_value (argv[i], name)))
            maindata.boost[k] = (gfloat) g_ascii_strtod (v, NULL);
        }
      for (k = 0; k < 4 && ! v; k++)
        {
          g_snprintf (name, sizeof name, "corner%d", k);
          if ((v = arg_value (argv[i], name)))
            maindata.cornerbr[k] = (gfloat) g_ascii_strtod (v, NULL);
        }
      if (v)
        continue;
      if ((v = arg_value (argv[i], "levels0")))
        maindata.levels[0] = (gfloat) g_ascii_strtod (v, NULL);
      else if ((v = arg_value (argv[i], "levels1")))
        maindata.levels[1] = (gfloat) g_ascii_strtod (v, NULL);
      else if ((v = arg_value (argv[i], "temperature")))
        maindata.temperature = (gfloat) g_ascii_strtod (v, NULL);
      else if ((v = arg_value (argv[i], "skin")))
        maindata.skin = (gfloat) g_ascii_strtod (v, NULL);
      else if ((v = arg_value (argv[i], "balweight")))
        maindata.balweight = (gfloat) g_ascii_strtod (v, NULL);
      else if ((v = arg_value (argv[i], "formula")))
        maindata.formula = ! strcmp (v, "safe") ? QUADRSAFE : ! strcmp (v, "hsv") ? HSV :
                           ! strcmp (v, "hsl") ? HSL : ! strcmp (v, "yuv") ? YUV : QUADRATIC;
      else if ((v = arg_value (argv[i], "skincolor")))
        fake_color = skincolor = color_from_hex (v);
      else if ((v = arg_value (argv[i], "calibrate")))
        calib = v;
      else
        {
          fprintf (stderr, "unknown setting %s\n", argv[i]);
          return 2;
        }
    }

  /* what the preview had set: the size of the image (of the selection's
   * bounds) for the per-corner brightness */
  maindata.image_width  = fake_width;
  maindata.image_height = fake_height;

  if (calib)
    {
      /* the dialog ran saturate () for its preview first, which fills
       * gammed_values; then the button's calibrate () read the preview */
      maindata.docolbal = FALSE;
      saturate (&d, NULL);
      if (! strcmp (calib, "image"))
        {
          fake_preview        = fake_image_in;
          fake_preview_width  = fake_width;
          fake_preview_height = fake_height;
        }
      else
        {
          GdkColor c = color_from_hex (calib);
          int      k;

          fake_preview = g_malloc0 (fake_channels);
          fake_preview[0] = c.red / 257;
          fake_preview[1] = c.green / 257;
          fake_preview[2] = c.blue / 257;
          for (k = 3; k < fake_channels; k++)
            fake_preview[k] = 255;
          fake_preview_width = fake_preview_height = 1;
        }
      calibrate ();
    }

  saturate (&d, NULL);
  harness_save (argv[5]);
  return 0;
}
