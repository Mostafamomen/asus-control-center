/* ui/spark.c - minimal temperature trend line (pure cairo)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "spark.h"
#define SPARK_N 64

struct _AccSpark
{
  GtkDrawingArea parent;
  gdouble hist[SPARK_N];
  gint count;
  gchar *color;
};

G_DEFINE_TYPE (AccSpark, acc_spark, GTK_TYPE_DRAWING_AREA)

static void
spark_draw_cb (GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer user)
{
  (void) da;
  AccSpark *s = ACC_SPARK (user);
  if (s->count < 2)
    return;

  gdouble lo = 1e9, hi = -1e9;
  for (gint i = 0; i < s->count; i++)
    {
      if (s->hist[i] < lo)
        lo = s->hist[i];
      if (s->hist[i] > hi)
        hi = s->hist[i];
    }
  if (hi - lo < 1.0)
    hi = lo + 1.0;

  GdkRGBA col;
  gdk_rgba_parse (&col, s->color ? s->color : "#006CE1");

  cairo_set_line_width (cr, 2.0);
  cairo_set_source_rgba (cr, col.red, col.green, col.blue, 0.9);
  /* x scale follows the number of collected points, not the history
   * capacity — otherwise a fresh start draws a tiny stub on the left. */
  gdouble stepx = w / (gdouble) (s->count - 1);
  cairo_move_to (cr, 0, h - (s->hist[0] - lo) / (hi - lo) * (h * 0.9) - h * 0.05);
  for (gint i = 1; i < s->count; i++)
    {
      gdouble x = stepx * i;
      gdouble y = h - (s->hist[i] - lo) / (hi - lo) * (h * 0.9) - h * 0.05;
      cairo_line_to (cr, x, y);
    }
  cairo_stroke (cr);
}

static void
acc_spark_dispose (GObject *o)
{
  AccSpark *s = ACC_SPARK (o);
  g_clear_pointer (&s->color, g_free);
  G_OBJECT_CLASS (acc_spark_parent_class)->dispose (o);
}

static void
acc_spark_class_init (AccSparkClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = acc_spark_dispose;
}

static void
acc_spark_init (AccSpark *self)
{
  self->count = 0;
  self->color = NULL;
  gtk_widget_set_hexpand (GTK_WIDGET (self), TRUE);
  gtk_widget_set_vexpand (GTK_WIDGET (self), TRUE);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (self), spark_draw_cb,
                                  self, NULL);
}

GtkWidget *
acc_spark_new (const gchar *line_color)
{
  AccSpark *s = (AccSpark *) g_object_new (ACC_TYPE_SPARK, NULL);
  s->color = g_strdup (line_color ? line_color : "#006CE1");
  return GTK_WIDGET (s);
}

void
acc_spark_push (AccSpark *s, gdouble v)
{
  if (s->count >= SPARK_N)
    {
      memmove (s->hist, s->hist + 1, sizeof (gdouble) * (SPARK_N - 1));
      s->count = SPARK_N - 1;
    }
  s->hist[s->count++] = v;
  gtk_widget_queue_draw (GTK_WIDGET (s));
}
