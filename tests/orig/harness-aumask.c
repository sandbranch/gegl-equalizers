/*
 * Runs the computing part of the original AUMASK 0.9.2 (Tibor Bamhor's
 * aumask.c, taken out by extract.py, with or without
 * aumask-fixes.patch) on a raw 8 bit image, as GIMP 2 ran it on a layer
 * without a selection.
 *
 * harness-aumask.c
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
 *   harness-aumask <in.raw> <width> <height> <channels> <out.raw> [name=value ...]
 *
 * The names are the plug-in's settings (maindata): boost0 to boost4
 * (boost4 is the hidden fifth value), radius, postradius, tresh,
 * newdark, newlight, mask_boost, mask_pos, and blurmode (standard,
 * selective, postblur), colormode (brightness, value, lightness, red,
 * green, blue), action (image, mask, difference), as the dialog's
 * combo boxes set them.
 */

/* the plug-in's code comes first: tests/orig-check.sh makes a file that
 * includes it and then this one */
#include "harness-common.h"

int
main (int argc, char **argv)
{
  GimpDrawable d = { 1 };
  int          i;

  if (argc < 6)
    {
      fprintf (stderr, "usage: %s <in.raw> <width> <height> <channels> <out.raw> [name=value ...]\n",
               argv[0]);
      return 2;
    }
  harness_load (argv);

  for (i = 6; i < argc; i++)
    {
      const char *v;
      char        name[16];
      int         k;

      for (k = 0; k < SLIDERS + 1; k++)
        {
          g_snprintf (name, sizeof name, "boost%d", k);
          if ((v = arg_value (argv[i], name)))
            break;
        }
      if (v)
        {
          maindata.boost[k] = (gfloat) g_ascii_strtod (v, NULL);
          continue;
        }
      if ((v = arg_value (argv[i], "radius")))
        maindata.radius = atoi (v);
      else if ((v = arg_value (argv[i], "postradius")))
        maindata.postradius = atoi (v);
      else if ((v = arg_value (argv[i], "tresh")))
        maindata.tresh = (gfloat) g_ascii_strtod (v, NULL);
      else if ((v = arg_value (argv[i], "newdark")))
        maindata.newdark = (gfloat) g_ascii_strtod (v, NULL);
      else if ((v = arg_value (argv[i], "newlight")))
        maindata.newlight = (gfloat) g_ascii_strtod (v, NULL);
      else if ((v = arg_value (argv[i], "mask_boost")))
        maindata.mask_boost = (gfloat) g_ascii_strtod (v, NULL);
      else if ((v = arg_value (argv[i], "mask_pos")))
        maindata.mask_pos = (gfloat) g_ascii_strtod (v, NULL);
      else if ((v = arg_value (argv[i], "blurmode")))
        maindata.blurmode = ! strcmp (v, "selective") ? SELBLUR :
                            ! strcmp (v, "postblur") ? POSTBLUR : NORMAL;
      else if ((v = arg_value (argv[i], "colormode")))
        maindata.colormode = ! strcmp (v, "value") ? HSV : ! strcmp (v, "lightness") ? HSL :
                             ! strcmp (v, "red") ? RCHAN : ! strcmp (v, "green") ? GCHAN :
                             ! strcmp (v, "blue") ? BCHAN : BRIGHT_OWN;
      else if ((v = arg_value (argv[i], "action")))
        maindata.action = ! strcmp (v, "mask") ? MASK :
                          ! strcmp (v, "difference") ? MASKDIFF : NORMAL;
      else
        {
          fprintf (stderr, "unknown setting %s\n", argv[i]);
          return 2;
        }
    }

  sharpen (&d, NULL);
  harness_save (argv[5]);
  return 0;
}
