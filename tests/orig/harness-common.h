/*
 * Shared by the harnesses of the original plug-ins: the image in memory
 * that the fake pixel regions of fake-gimp.h read and write, reading
 * and writing raw 8 bit files, and name=value arguments
 *
 * harness-common.h
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
 */

guchar  *fake_image_in, *fake_image_out, *fake_preview;
gint     fake_width, fake_height, fake_channels;
gint     fake_preview_width, fake_preview_height;
GdkColor fake_color;

void
export_to_layer ()
{
}

/* in.raw width height channels: the image the plug-in works on */
static void
harness_load (char **argv)
{
  gchar *data = NULL;
  gsize  len  = 0;

  fake_width    = atoi (argv[2]);
  fake_height   = atoi (argv[3]);
  fake_channels = atoi (argv[4]);
  if (! g_file_get_contents (argv[1], &data, &len, NULL) ||
      len != (gsize) fake_width * fake_height * fake_channels)
    {
      fprintf (stderr, "cannot read %s\n", argv[1]);
      exit (2);
    }
  fake_image_in  = (guchar *) data;
  fake_image_out = g_malloc0 (len);
}

static void
harness_save (const char *path)
{
  gsize len = (gsize) fake_width * fake_height * fake_channels;

  if (! g_file_set_contents (path, (gchar *) fake_image_out, len, NULL))
    {
      fprintf (stderr, "cannot write %s\n", path);
      exit (2);
    }
}

/* the value of name=value in arg, or NULL */
static const char *
arg_value (const char *arg, const char *name)
{
  size_t n = strlen (name);

  return strncmp (arg, name, n) == 0 && arg[n] == '=' ? arg + n + 1 : NULL;
}

/* RRGGBB as a GdkColor of 16 bits per channel, as GTK 2 gave an 8 bit
 * color (v * 257) */
static GdkColor
color_from_hex (const char *hex)
{
  GdkColor c = { 0, 0, 0, 0 };
  unsigned r, g, b;

  if (sscanf (hex, "%2x%2x%2x", &r, &g, &b) != 3)
    {
      fprintf (stderr, "not a color: %s\n", hex);
      exit (2);
    }
  c.red = r * 257; c.green = g * 257; c.blue = b * 257;
  return c;
}
