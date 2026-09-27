/*
 * Applies one of the operations to a raw 8 bit image, for the
 * comparison with the original plug-ins (tests/orig-check.sh)
 *
 * eq-apply.c
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
 *   eq-apply <build folder> <operation> <width> <height> <channels>
 *            <in.raw> <out.raw> [name=value ...]
 *
 * The raw files are 8 bit R'G'B' (3 channels) or R'G'B'A (4), rows top
 * to bottom, as GIMP 2 gave them to the plug-ins. The operation works on
 * float, the result is rounded to 8 bits by babl.
 */

#include <gegl.h>
#include <stdio.h>
#include <stdlib.h>

#include "modules.h"

int
main (int    argc,
      char **argv)
{
  const Babl *fmt;
  GeglBuffer *in;
  GeglNode   *g, *src, *node;
  gchar      *dir, *data = NULL;
  gsize       len = 0;
  gint        w, h, ch, i;
  guchar     *out;
  FILE       *f;

  if (argc < 8)
    {
      fprintf (stderr, "usage: %s <build> <op> <width> <height> <channels> "
               "<in.raw> <out.raw> [name=value ...]\n", argv[0]);
      return 2;
    }
  w  = atoi (argv[3]);
  h  = atoi (argv[4]);
  ch = atoi (argv[5]);
  if (! g_file_get_contents (argv[6], &data, &len, NULL) ||
      len != (gsize) w * h * ch || (ch != 3 && ch != 4))
    {
      fprintf (stderr, "cannot read %s as %d x %d x %d\n", argv[6], w, h, ch);
      return 2;
    }

  dir = modules_setup (argv[1]);
  if (! dir)
    return 2;
  gegl_init (NULL, NULL);

  fmt = babl_format (ch == 3 ? "R'G'B' u8" : "R'G'B'A u8");
  in  = gegl_buffer_new (GEGL_RECTANGLE (0, 0, w, h), fmt);
  gegl_buffer_set (in, NULL, 0, fmt, data, GEGL_AUTO_ROWSTRIDE);
  g    = gegl_node_new ();
  src  = gegl_node_new_child (g, "operation", "gegl:buffer-source", "buffer", in, NULL);
  node = gegl_node_new_child (g, "operation", argv[2], NULL);
  for (i = 8; i < argc; i++)
    if (! node_set_from_string (node, argv[i]))
      {
        fprintf (stderr, "cannot set %s\n", argv[i]);
        return 2;
      }
  gegl_node_link (src, node);
  out = g_malloc ((gsize) w * h * ch);
  gegl_node_blit (node, 1.0, GEGL_RECTANGLE (0, 0, w, h), fmt, out,
                  GEGL_AUTO_ROWSTRIDE, GEGL_BLIT_DEFAULT);

  f = fopen (argv[7], "wb");
  if (! f || fwrite (out, 1, (gsize) w * h * ch, f) != (gsize) w * h * ch)
    {
      fprintf (stderr, "cannot write %s\n", argv[7]);
      return 2;
    }
  fclose (f);

  g_free (out);
  g_free (data);
  g_object_unref (g);
  g_object_unref (in);
  gegl_exit ();
  modules_cleanup (dir);
  return 0;
}
