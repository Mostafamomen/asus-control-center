/* ui/mcui.c - MyASUS-style UI kit
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "mcui.h"

/* ------------------------------------------------------------------ */
/* basic text                                                          */
/* ------------------------------------------------------------------ */
GtkWidget *
mc_label (const gchar *text, const gchar *cls, int xalign)
{
  GtkWidget *l = gtk_label_new (text);
  gtk_label_set_xalign (GTK_LABEL (l), xalign ? 1.0 : 0.0);
  if (cls)
    gtk_widget_add_css_class (l, cls);
  return l;
}

GtkWidget *
mc_group (const gchar *text)
{
  GtkWidget *l = mc_label (text, "overline", 0);
  gtk_widget_set_margin_top (l, 4);
  gtk_widget_set_margin_bottom (l, 2);
  gtk_widget_set_margin_start (l, 4);
  return l;
}

GtkWidget *
mc_title (const gchar *text)
{
  return mc_label (text, "cardtitle", 0);
}

GtkWidget *
mc_spacer (void)
{
  GtkWidget *f = mc_label (NULL, NULL, 0);
  gtk_widget_set_hexpand (f, TRUE);
  return f;
}

/* ------------------------------------------------------------------ */
/* containers                                                          */
/* ------------------------------------------------------------------ */
GtkWidget *
mc_card (void)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_add_css_class (box, "card");
  return box;
}

GtkWidget *
mc_panel (const gchar *title)
{  /* Device-Settings-style section card: white card with the blue
    * accent edge on the leading side (MyASUS "Device Settings" look) */
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class (box, "accentcard");
  GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_append (GTK_BOX (h), mc_title (title));
  gtk_box_append (GTK_BOX (h), mc_spacer ());
  gtk_box_append (GTK_BOX (box), h);
  return box;
}

GtkWidget *
mc_page (GtkWidget *child)
{
  GtkWidget *sc = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sc),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  GtkWidget *wrap = gtk_box_new (GTK_ORIENTATION_VERTICAL, 14);
  gtk_widget_set_margin_top (wrap, 14);
  gtk_widget_set_margin_bottom (wrap, 20);
  gtk_widget_set_margin_start (wrap, 20);
  gtk_widget_set_margin_end (wrap, 20);
  gtk_box_append (GTK_BOX (wrap), child);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (sc), wrap);
  return sc;
}

/* ------------------------------------------------------------------ */
/* three-column MyASUS setting block                                   */
/* ------------------------------------------------------------------ */
GtkWidget *
mc_feature (const gchar *icon, const gchar *title, const gchar *desc,
            GtkWidget **out_ctl)
{
  GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
  {
    GtkWidget *cim = gtk_image_new_from_icon_name (icon);
    gtk_image_set_pixel_size (GTK_IMAGE (cim), 40);
    gtk_widget_add_css_class (cim, "cardicon");
    gtk_widget_set_valign (cim, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (row), cim);
  }
  {
    GtkWidget *cd = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append (GTK_BOX (cd), mc_title (title));
    if (desc)
      {
        GtkWidget *csub = mc_label (desc, "foot", 0);
        gtk_label_set_wrap (GTK_LABEL (csub), TRUE);
        gtk_label_set_width_chars (GTK_LABEL (csub), 24);
        gtk_box_append (GTK_BOX (cd), csub);
      }
    gtk_widget_set_valign (cd, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (row), cd);
  }
  GtkWidget *ctl = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_hexpand (ctl, TRUE);
  gtk_box_append (GTK_BOX (row), ctl);
  if (out_ctl)
    *out_ctl = ctl;
  return row;
}

/* ------------------------------------------------------------------ */
/* small pieces                                                        */
/* ------------------------------------------------------------------ */
GtkWidget *
mc_badge (const gchar *icon, int pixel)
{
  GtkWidget *badge = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class (badge, "iconbadge");
  GtkWidget *im = gtk_image_new_from_icon_name (icon);
  gtk_image_set_pixel_size (GTK_IMAGE (im), pixel);
  gtk_widget_set_valign (im, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (badge), im);
  return badge;
}

GtkWidget *
mc_row (const gchar *label, GtkWidget **out_value)
{
  GtkWidget *v = mc_label ("—", "val", 1);
  gtk_widget_set_hexpand (v, TRUE);
  GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_append (GTK_BOX (h), mc_label (label, "dim", 0));
  gtk_box_append (GTK_BOX (h), v);
  if (out_value)
    *out_value = v;
  return h;
}

GtkWidget *
mc_switch (gboolean active)
{
  GtkWidget *sw = gtk_switch_new ();
  gtk_switch_set_active (GTK_SWITCH (sw), active);
  gtk_widget_set_valign (sw, GTK_ALIGN_CENTER);
  return sw;
}

GtkWidget *
mc_primary (const gchar *label)
{
  GtkWidget *b = gtk_button_new_with_label (label);
  gtk_widget_add_css_class (b, "primarybtn");
  gtk_widget_set_halign (b, GTK_ALIGN_END);
  return b;
}

GtkWidget *
mc_link (const gchar *url, const gchar *label)
{
  GtkWidget *b = gtk_link_button_new_with_label (url, label);
  gtk_widget_add_css_class (b, "primarybtn");
  gtk_widget_set_halign (b, GTK_ALIGN_END);
  return b;
}

GtkWidget *
mc_pill (const gchar *label)
{
  GtkWidget *b = gtk_button_new_with_label (label);
  gtk_widget_add_css_class (b, "langpill");
  return b;
}

/* ------------------------------------------------------------------ */
/* grayscale ("E-Reading") container                                   */
/* ------------------------------------------------------------------ */
typedef struct { GtkBox parent_instance; } McGray;
typedef struct { GtkBoxClass parent_class; } McGrayClass;

G_DEFINE_TYPE (McGray, mc_gray, GTK_TYPE_BOX)

static gboolean s_gray_on = FALSE;
static GtkWidget *s_gray_widget = NULL;

static void
mc_gray_snapshot (GtkWidget *w, GtkSnapshot *snap)
{
  if (!s_gray_on)
    {
      GTK_WIDGET_CLASS (mc_gray_parent_class)->snapshot (w, snap);
      return;
    }
  /* Rec. 709 luma -> every channel becomes the same luminance value.
   * graphene_matrix_init_from_float() takes a COLUMN-major array, so the
   * matrix rows (R,G,B each equal to the luma weights) are written as
   * columns down the array. The result is a true grey, no hue shift. */
  const float gm[16] = {
    0.2126f, 0.2126f, 0.2126f, 0.0f, /* column 0 */
    0.7152f, 0.7152f, 0.7152f, 0.0f, /* column 1 */
    0.0722f, 0.0722f, 0.0722f, 0.0f, /* column 2 */
    0.0f,    0.0f,    0.0f,    1.0f  /* column 3 (alpha) */
  };
  graphene_matrix_t m;
  graphene_matrix_init_from_float (&m, gm);
  graphene_vec4_t off;
  graphene_vec4_init (&off, 0.0f, 0.0f, 0.0f, 0.0f);
  gtk_snapshot_push_color_matrix (snap, &m, &off);
  GTK_WIDGET_CLASS (mc_gray_parent_class)->snapshot (w, snap);
  gtk_snapshot_pop (snap);
}

static void
mc_gray_init (McGray *self)
{
  gtk_orientable_set_orientation (GTK_ORIENTABLE (self),
                                  GTK_ORIENTATION_VERTICAL);
}

static void
mc_gray_class_init (McGrayClass *klass)
{
  GTK_WIDGET_CLASS (klass)->snapshot = mc_gray_snapshot;
}

GtkWidget *
mc_gray_new (GtkWidget *child)
{
  GtkWidget *w = (GtkWidget *) g_object_new (mc_gray_get_type (), NULL);
  gtk_box_append (GTK_BOX (w), child);
  s_gray_widget = w;
  return w;
}

/* ---------------------------------------------------------- brand mark -- */
/* Rounded-rect helper (cairo has no primitive for it). */
static void
round_rect (cairo_t *cr, double x, double y, double w, double h, double r)
{
  cairo_new_sub_path (cr);
  cairo_arc (cr, x + w - r, y + r,     r, -G_PI / 2, 0);
  cairo_arc (cr, x + w - r, y + h - r, r, 0,          G_PI / 2);
  cairo_arc (cr, x + r,     y + h - r, r, G_PI / 2,   G_PI);
  cairo_arc (cr, x + r,     y + r,     r, G_PI,       3 * G_PI / 2);
  cairo_close_path (cr);
}

/* Design space is 256x256 (same numbers as assets/logo.svg); everything is
 * scaled from that, so a 22 px sidebar mark and a 116 px login mark are the
 * same artwork. A bitmap GtkPicture would advertise its 256 px texture as
 * the natural size and ignore the widget request — cairo cannot. */
static void
brand_draw (GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer user)
{
  (void) da;
  (void) user;
  double s = (double) MIN (w, h) / 256.0;
  if (s <= 0)
    return;

  cairo_save (cr);
  cairo_translate (cr, (w - 256 * s) / 2.0, (h - 256 * s) / 2.0);
  cairo_scale (cr, s, s);

  /* tile: diagonal gradient in the two MyASUS blues */
  /* tile: one flat MyASUS blue — no gradients in this mark */
  round_rect (cr, 8, 8, 240, 240, 58);
  cairo_set_source_rgb (cr, 0.000, 0.424, 0.882);   /* #006CE1 */
  cairo_fill (cr);

  /* hairline highlight */
  round_rect (cr, 8, 8, 240, 240, 58);
  cairo_set_line_width (cr, 3);
  cairo_set_source_rgba (cr, 1, 1, 1, 0.22);
  cairo_stroke (cr);

  /* dial ring: 270° with round ends, gap at the bottom */
  cairo_set_line_width (cr, 24);
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_source_rgb (cr, 1, 1, 1);
  cairo_arc (cr, 128, 128, 70, G_PI * 0.75, G_PI * 2.25);
  cairo_stroke (cr);

  /* needle */
  cairo_set_source_rgb (cr, 1, 1, 1);
  cairo_set_line_width (cr, 20);
  cairo_move_to (cr, 128, 128);
  cairo_line_to (cr, 172, 90);
  cairo_stroke (cr);

  /* hub */
  cairo_set_source_rgb (cr, 1, 1, 1);
  cairo_arc (cr, 128, 128, 17, 0, 2 * G_PI);
  cairo_fill (cr);
  cairo_set_source_rgb (cr, 0.000, 0.424, 0.882);   /* #006CE1 */
  cairo_arc (cr, 128, 128, 7, 0, 2 * G_PI);
  cairo_fill (cr);

  cairo_restore (cr);
}

GtkWidget *
mc_brand_mark_new (int px)
{
  GtkWidget *da = gtk_drawing_area_new ();
  gtk_widget_add_css_class (da, "brandmark");
  /* size request is the *only* determinant here: the drawing area has no
   * intrinsic size, so the mark is exactly px x px at any scale factor */
  gtk_widget_set_size_request (da, px, px);
  gtk_widget_set_hexpand (da, FALSE);
  gtk_widget_set_vexpand (da, FALSE);
  gtk_widget_set_halign (da, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (da, GTK_ALIGN_CENTER);
  gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (da), px);
  gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (da), px);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (da), brand_draw, NULL,
                                  NULL);
  return da;
}

void
mc_gray_set (gboolean on)
{
  s_gray_on = on;
  if (s_gray_widget)
    gtk_widget_queue_draw (s_gray_widget);
}

gboolean
mc_gray_get (void)
{
  return s_gray_on;
}
