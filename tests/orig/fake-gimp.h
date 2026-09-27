/*
 * Just enough of libgimp 2 and GTK 2 for the computing part of the
 * original plug-ins (tests/orig/extract.py takes it out of their
 * sources) to compile and run without GIMP: the types, and stubs whose
 * pixel regions read and write the harness's image in memory. The HSV
 * and HSL conversions are GIMP 2.10.38's own, taken out of its
 * libgimpcolor/gimpcolorspace.c by tests/orig-check.sh.
 *
 * fake-gimp.h
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

#ifndef FAKE_GIMP_H
#define FAKE_GIMP_H

#define GLIB_DISABLE_DEPRECATION_WARNINGS
#include <glib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* libgimpmath/gimpmath.h of GIMP 2.10.38 */
#define ROUND(x) ((int) ((x) + 0.5))

/* libgimpcolor */
typedef struct _GimpRGB { gdouble r, g, b, a; } GimpRGB;
typedef struct _GimpHSV { gdouble h, s, v, a; } GimpHSV;
typedef struct _GimpHSL { gdouble h, s, l, a; } GimpHSL;
#define GIMP_HSL_UNDEFINED -1.0

/* gimprgb.c */
static inline gdouble
gimp_rgb_max (const GimpRGB *rgb)
{
  if (rgb->r > rgb->g)
    return (rgb->r > rgb->b) ? rgb->r : rgb->b;
  else
    return (rgb->g > rgb->b) ? rgb->g : rgb->b;
}

static inline gdouble
gimp_rgb_min (const GimpRGB *rgb)
{
  if (rgb->r < rgb->g)
    return (rgb->r < rgb->b) ? rgb->r : rgb->b;
  else
    return (rgb->g < rgb->b) ? rgb->g : rgb->b;
}

void gimp_rgb_to_hsv (const GimpRGB *rgb, GimpHSV *hsv);
void gimp_hsv_to_rgb (const GimpHSV *hsv, GimpRGB *rgb);
void gimp_rgb_to_hsl (const GimpRGB *rgb, GimpHSL *hsl);
void gimp_hsl_to_rgb (const GimpHSL *hsl, GimpRGB *rgb);

/* the GTK and GIMP widgets the code names; they are never made */
typedef void GtkWidget;
typedef void GtkObject;
typedef void GtkAdjustment;
typedef void GtkComboBox;
typedef void GtkColorButton;
typedef void GimpPreview;
typedef struct { guint32 pixel; guint16 red, green, blue; } GdkColor;

#define GTK_LABEL(x) (x)
#define GIMP_PREVIEW(x) (x)
#define GIMP_ZOOM_PREVIEW(x) (x)
#define GIMP_DRAWABLE_PREVIEW(x) (x)

/* the plug-ins' procedure types, for the declarations of query and run */
typedef enum { GIMP_RUN_INTERACTIVE, GIMP_RUN_NONINTERACTIVE, GIMP_RUN_WITH_LAST_VALS } GimpRunMode;
typedef enum { GIMP_PDB_SUCCESS, GIMP_PDB_CALLING_ERROR } GimpPDBStatusType;
typedef struct { gint type; union { gint32 d_int32; gint32 d_image; gint32 d_drawable; gint d_status; } data; } GimpParam;

/* the image of the harness: the drawable the plug-in reads and writes */
typedef struct { gint32 drawable_id; } GimpDrawable;

typedef struct
{
  gint x, y, w, h;
} GimpPixelRgn;

extern guchar *fake_image_in;     /* what the plug-in reads */
extern guchar *fake_image_out;    /* what it writes */
extern gint    fake_width, fake_height, fake_channels;
/* what gimp_zoom_preview_get_source () gives (calibrate () of the
 * Saturation Equalizer: the preview's pixels) */
extern guchar *fake_preview;
extern gint    fake_preview_width, fake_preview_height;
/* the color of the color button (the Saturation Equalizer's skin color) */
extern GdkColor fake_color;

static inline gboolean
gimp_drawable_mask_bounds (gint32 id, gint *x1, gint *y1, gint *x2, gint *y2)
{
  (void) id;
  *x1 = 0; *y1 = 0; *x2 = fake_width; *y2 = fake_height;
  return FALSE;
}

static inline gint
gimp_drawable_bpp (gint32 id)
{
  (void) id;
  return fake_channels;
}

static inline void
gimp_pixel_rgn_init (GimpPixelRgn *rgn, GimpDrawable *d, gint x, gint y,
                     gint w, gint h, gint dirty, gint shadow)
{
  (void) d; (void) dirty; (void) shadow;
  rgn->x = x; rgn->y = y; rgn->w = w; rgn->h = h;
}

static inline void
gimp_pixel_rgn_get_rect (GimpPixelRgn *rgn, guchar *buf, gint x, gint y, gint w, gint h)
{
  gint r;

  (void) rgn;
  for (r = 0; r < h; r++)
    memcpy (buf + (gsize) r * w * fake_channels,
            fake_image_in + ((gsize) (y + r) * fake_width + x) * fake_channels,
            (gsize) w * fake_channels);
}

static inline void
gimp_pixel_rgn_set_rect (GimpPixelRgn *rgn, const guchar *buf, gint x, gint y, gint w, gint h)
{
  gint r;

  (void) rgn;
  for (r = 0; r < h; r++)
    memcpy (fake_image_out + ((gsize) (y + r) * fake_width + x) * fake_channels,
            buf + (gsize) r * w * fake_channels, (gsize) w * fake_channels);
}

static inline guchar *
gimp_zoom_preview_get_source (void *preview, gint *w, gint *h, gint *ch)
{
  (void) preview;
  *w = fake_preview_width;
  *h = fake_preview_height;
  *ch = fake_channels;
  return g_memdup2 (fake_preview, (gsize) *w * *h * *ch);
}

static inline void
gtk_color_button_get_color (void *button, GdkColor *color)
{
  (void) button;
  *color = fake_color;
}

#define gimp_progress_init(x) ((void) 0)
#define gimp_progress_update(x) ((void) 0)
#define gimp_progress_end() ((void) 0)
#define gimp_drawable_flush(d) ((void) 0)
#define gimp_drawable_merge_shadow(id, u) ((void) 0)
#define gimp_drawable_update(id, x, y, w, h) ((void) 0)
#define gimp_displays_flush() ((void) 0)
#define gtk_label_set_label(l, t) ((void) 0)
#define gimp_preview_draw_buffer(p, b, s) ((void) 0)
#define gimp_drawable_preview_draw_region(p, r) ((void) 0)
#define gimp_preview_untransform(p, a, b, c, d) ((void) 0)
#define gimp_preview_get_position(p, x, y) ((void) 0)
#define gimp_preview_get_size(p, w, h) ((void) 0)

/* the GLib 2 of 2011 thread calls, with today's GLib */
#undef g_thread_supported
#define g_thread_supported() 1
#undef g_thread_init
#define g_thread_init(x) ((void) 0)
#undef g_thread_create
#define g_thread_create(func, data, joinable, error) g_thread_new (NULL, func, data)

/* the harness's stand-in for the plug-ins' export_to_layer () */
void export_to_layer ();

#endif
