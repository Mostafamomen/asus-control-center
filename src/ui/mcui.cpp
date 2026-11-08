/* ui/mcui.c - MyASUS-style UI kit
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "mcui.h"
#include "theme.h"

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
  gtk_widget_set_size_request (sw, 40, 24);
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
GtkWidget *
mc_brand_mark_new (int px)
{
  gchar *path = acc_asset_path ("asuscraft.png");
  GtkWidget *mark;
  if (path)
    {
      mark = gtk_image_new_from_file (path);
      gtk_image_set_pixel_size (GTK_IMAGE (mark), px);
      g_free (path);
    }
  else
    {
      mark = gtk_image_new_from_icon_name ("io.github.asuscontrolcenter.AsusControlCenter");
      gtk_image_set_pixel_size (GTK_IMAGE (mark), px);
    }
  gtk_widget_add_css_class (mark, "brandmark");
  gtk_widget_set_size_request (mark, px, px);
  gtk_widget_set_hexpand (mark, FALSE);
  gtk_widget_set_vexpand (mark, FALSE);
  gtk_widget_set_halign (mark, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (mark, GTK_ALIGN_CENTER);
  return mark;
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
