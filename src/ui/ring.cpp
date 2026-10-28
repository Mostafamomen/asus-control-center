/* ui/ring.c - cairo doughnut progress widget
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ring.h"
#include "theme.h"

struct _AccRing
{
  GtkDrawingArea parent;
  gdouble value;
  gchar *big;
  gchar *small;
  gchar *color;
};

G_DEFINE_TYPE (AccRing, acc_ring, GTK_TYPE_DRAWING_AREA)

static void
draw_cb (GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer user)
{
  (void) da;
  AccRing *r2 = ACC_RING (user);
  if (!r2)
    return;

  gdouble cx = w / 2.0, cy = h / 2.0;
  gdouble rad = MIN (w, h) / 2.0 - 6.0;
  gboolean dark = acc_theme_is_dark ();
  /* track: light gray on light, faint white on dark */
  cairo_set_line_width (cr, rad / 4.2);
  if (dark)
    cairo_set_source_rgba (cr, 1, 1, 1, 0.12);
  else
    cairo_set_source_rgba (cr, 0.10, 0.14, 0.20, 0.10);
  cairo_arc (cr, cx, cy, rad, 0, 2 * G_PI);
  cairo_stroke (cr);
  /* value arc: start at top, clockwise */
  GdkRGBA col;
  gdk_rgba_parse (&col, r2->color ? r2->color : "#006CE1");
  cairo_set_source_rgba (cr, col.red, col.green, col.blue, 1.0);
  cairo_arc (cr, cx, cy, rad, -G_PI / 2, -G_PI / 2 + 2 * G_PI * r2->value);
  cairo_stroke (cr);

  /* big + small centered as one block inside the ring */
  PangoLayout *lb = gtk_widget_create_pango_layout (GTK_WIDGET (da), NULL);
  PangoLayout *ls = gtk_widget_create_pango_layout (GTK_WIDGET (da), NULL);
  int bw = 0, bh = 0, sw = 0, sh = 0;
  gboolean hasb = r2->big != NULL;
  gboolean hass = r2->small != NULL;
  if (hasb)
    {
      PangoAttrList *attrs = pango_attr_list_new ();
      pango_attr_list_insert (attrs, pango_attr_scale_new (2.2));
      pango_layout_set_text (lb, r2->big, -1);
      pango_layout_set_attributes (lb, attrs);
      pango_attr_list_unref (attrs);
      pango_layout_get_size (lb, &bw, &bh);
    }
  if (hass)
    {
      PangoAttrList *attrs = pango_attr_list_new ();
      pango_attr_list_insert (attrs, pango_attr_scale_new (1.0));
      pango_layout_set_text (ls, r2->small, -1);
      pango_layout_set_attributes (ls, attrs);
      pango_attr_list_unref (attrs);
      pango_layout_get_size (ls, &sw, &sh);
    }
  double bhpx = bh / (double) PANGO_SCALE;
  double shpx = sh / (double) PANGO_SCALE;
  double gap = 3.0;
  double total = (hasb ? bhpx : 0.0) + (hass ? gap + shpx : 0.0);
  double y = cy - total / 2.0;
  /* #FFFFFF on the dark canvas, #202939 on the light one */
  if (dark)
    cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);         /* #FFFFFF */
  else
    cairo_set_source_rgb (cr, 0.125, 0.161, 0.224);   /* #202939 */
  if (hasb)
    {
      cairo_move_to (cr, cx - bw / (PANGO_SCALE * 2.0), y);
      pango_cairo_show_layout (cr, lb);
      y += bhpx + gap;
    }
  if (hass)
    {
      cairo_move_to (cr, cx - sw / (PANGO_SCALE * 2.0), y);
      pango_cairo_show_layout (cr, ls);
    }
  g_object_unref (lb);
  g_object_unref (ls);
}

static void
acc_ring_init (AccRing *self)
{
  gtk_widget_set_hexpand (GTK_WIDGET (self), FALSE);
  gtk_widget_set_vexpand (GTK_WIDGET (self), FALSE);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (self), draw_cb, self, NULL);
}

static void
acc_ring_dispose (GObject *o)
{
  AccRing *self = ACC_RING (o);
  g_clear_pointer (&self->big, g_free);
  g_clear_pointer (&self->small, g_free);
  g_clear_pointer (&self->color, g_free);
  G_OBJECT_CLASS (acc_ring_parent_class)->dispose (o);
}

static void
acc_ring_class_init (AccRingClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = acc_ring_dispose;
}

/* queue a repaint on the main thread */
static void
queue (AccRing *self)
{
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

GtkWidget *
acc_ring_new (const gchar *arc_color)
{
  AccRing *self = (AccRing *) g_object_new (ACC_TYPE_RING, NULL);
  self->value = 0;
  self->color = g_strdup (arc_color ? arc_color : "#006CE1");
  self->big = NULL;
  self->small = NULL;
  /* init already registered draw_cb with self as user data */
  queue (self);
  return GTK_WIDGET (self);
}

void
acc_ring_set_value (AccRing *r, gdouble v)
{
  if (v < 0)
    v = 0;
  if (v > 1)
    v = 1;
  r->value = v;
  queue (r);
}

void
acc_ring_set_big (AccRing *r, const gchar *text)
{
  g_free (r->big);
  r->big = g_strdup (text);
  queue (r);
}

void
acc_ring_set_small (AccRing *r, const gchar *text)
{
  g_free (r->small);
  r->small = g_strdup (text);
  queue (r);
}

gdouble
acc_ring_get_value (AccRing *r)
{
  return r->value;
}

gdouble
acc_ring_get_max (const AccRing *r)
{
  (void) r;
  return 1.0;
}
