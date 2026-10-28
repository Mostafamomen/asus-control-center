/* ui/home.c - dashboard page, laid out to match the approved mockup:
 * hero with real laptop photo, laptop chips strip, six cards
 * (battery ring + limit slider/switch, GPU logo + green mode pill,
 * OLED preview + brightness slider + refresh row + eye-care pill,
 * system rows, fans ring + trend spark, quick apps with captions).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "home.h"
#include "theme.h"
#include "window.h"
#include "specs.h"
#include "../i18n/i18n.h"
#include "../control/brightness.h"
#include "../control/ctl.h"
#include "../probe/probe.h"
#include "../probe/load.h"
#include "../probe/battery.h"
#include "../probe/dmi.h"
#include "../probe/hwmon.h"
#include "../probe/gpu.h"
#include "../probe/display.h"
#include "../probe/sysinfo.h"
#include <adwaita.h>


/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

static GtkWidget *
lbl (const gchar *text, const gchar *cssclass, int xalign)
{
  GtkWidget *l = gtk_label_new (text);
  gtk_label_set_xalign (GTK_LABEL (l), xalign);
  if (cssclass)
    gtk_widget_add_css_class (l, cssclass);
  return l;
}

static GtkWidget *
stat_row (const gchar *icon, const gchar *value_fmt, GtkWidget **out_val)
/* icon | value (bold) over caption — like the mockup stats */
{
  GtkWidget *v = lbl ("—", "val", 0);
  gtk_widget_set_hexpand (v, TRUE);
  GtkWidget *col = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append (GTK_BOX (col), v);
  GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_box_append (GTK_BOX (h), col);
  GtkWidget *im = gtk_image_new_from_icon_name (icon);
  gtk_widget_add_css_class (im, "dim");
  gtk_widget_set_valign (im, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (h), im);
  if (out_val)
    *out_val = v;
  (void) value_fmt;
  return h;
}

static void
nav_btn (GtkButton *b, gpointer page)
{
  (void) b;
  acc_ui_nav_to ((const gchar *) page);
}

static GtkWidget *
go_btn (const gchar *text, const gchar *page)
{
  GtkWidget *b = gtk_button_new ();
  gtk_widget_add_css_class (b, "actionbtn");
  GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *im = gtk_image_new_from_icon_name ("go-next-symbolic");
  gtk_image_set_pixel_size (GTK_IMAGE (im), 12);
  gtk_widget_set_valign (im, GTK_ALIGN_CENTER);
  GtkWidget *t = lbl (text, NULL, 0);
  gtk_widget_set_hexpand (t, TRUE);
  gtk_box_append (GTK_BOX (h), im);
  gtk_box_append (GTK_BOX (h), t);
  gtk_button_set_child (GTK_BUTTON (b), h);
  if (page)
    g_signal_connect (b, "clicked", G_CALLBACK (nav_btn), (gpointer) page);
  return b;
}

static GtkWidget *
card_head (const gchar *icon, AccStr title)
{
  GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *im = gtk_image_new_from_icon_name (icon);
  gtk_image_set_pixel_size (GTK_IMAGE (im), 18);
  gtk_widget_add_css_class (im, "cardicon");
  gtk_widget_set_valign (im, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (h), im);
  gtk_box_append (GTK_BOX (h), lbl (acc_tr (title), "cardtitle", 0));
  return h;
}

static GtkWidget *
card (const gchar *icon, AccStr title, GtkWidget **out_body)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class (box, "card");
  GtkWidget *head = card_head (icon, title);
  gtk_box_append (GTK_BOX (box), head);
  if (out_body)
    *out_body = box;
  return box;
}

/* ------------------------------------------------------------------ */
/* battery card                                                        */
/* ------------------------------------------------------------------ */

static void
on_limit_switch (GtkSwitch *sw, GParamSpec *ps, gpointer user)
{
  (void) ps;
  (void) user;
  AccUI *u = ACC_UI_CAST (user);
  if (u->syncing)
    return;
  gboolean on = gtk_switch_get_active (sw);
  gboolean ok = acc_ctl_set_charge (on ? 80 : 100);
  acc_ui_toast (acc_tr (ok ? ACC_T_WROK : ACC_T_WRERR));
}

static GtkWidget *
card_battery (AccUI *u)
{
  GtkWidget *c = card ("battery-symbolic", ACC_T_CARD_BATTERY, NULL);
  GtkWidget *li = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);

  /* ring + caption + three stat rows */
  GtkWidget *top = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 14);
  GtkWidget *ringcol = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  u->ring_bat = ACC_RING (acc_ring_new (acc_theme_col_accent ()));
  gtk_widget_set_size_request (GTK_WIDGET (u->ring_bat), 128, 128);
  gtk_box_append (GTK_BOX (ringcol), GTK_WIDGET (u->ring_bat));
  u->bat_small = lbl ("", "dim", 0);
  gtk_widget_set_halign (u->bat_small, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (ringcol), u->bat_small);
  gtk_box_append (GTK_BOX (top), ringcol);

  GtkWidget *right = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_valign (right, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (right),
                  stat_row ("battery-symbolic", NULL, &u->bat_wh_now));
  gtk_box_append (GTK_BOX (right),
                  stat_row ("battery-full-symbolic", NULL, &u->bat_wh_full));
  gtk_box_append (GTK_BOX (right), stat_row ("preferences-system-time-symbolic", NULL,
                                             &u->bat_cycles));
  gtk_box_append (GTK_BOX (top), right);
  gtk_box_append (GTK_BOX (li), top);

  gtk_box_append (GTK_BOX (li), go_btn (acc_tr (ACC_T_BAT_DETAIL), "battery"));

  /* recommended charging limit: read slider + functional toggle */
  GtkWidget *limbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_box_append (GTK_BOX (limbox),
                  lbl (acc_tr (ACC_T_BAT_LIMIT), "foot", 1));
  GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
  u->bat_limit_scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL,
                                                 (gdouble) 50, (gdouble) 100,
                                                 5);
  gtk_scale_set_draw_value (GTK_SCALE (u->bat_limit_scale), FALSE);
  gtk_widget_set_hexpand (u->bat_limit_scale, TRUE);
  gtk_widget_set_sensitive (u->bat_limit_scale, FALSE);
  u->bat_limit_val = lbl ("—", "val", 1);
  u->bat_switch = gtk_switch_new ();
  gtk_box_append (GTK_BOX (row), u->bat_limit_scale);
  gtk_box_append (GTK_BOX (row), u->bat_limit_val);
  gtk_box_append (GTK_BOX (row), u->bat_switch);
  gtk_box_append (GTK_BOX (limbox), row);
  gtk_box_append (GTK_BOX (li), limbox);
  g_signal_connect (u->bat_switch, "state-set", G_CALLBACK (on_limit_switch),
                    u);

  gtk_box_append (GTK_BOX (c), li);
  return c;
}

/* ------------------------------------------------------------------ */
/* gpu card                                                            */
/* ------------------------------------------------------------------ */

static GtkWidget *
card_gpu (AccUI *u)
{
  GtkWidget *c = card ("video-display-symbolic", ACC_T_CARD_GPU, NULL);
  GtkWidget *li = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);

  /* logo block + name + mode pill: the badge follows the real GPU vendor
   * (NVIDIA / AMD / Intel), read from PCI ids — never a model list */
  GtkWidget *head = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *nv = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class (nv, "nvlogo");
  switch (gpu_vendor ())
    {
    case ACC_GPU_NVIDIA: gtk_widget_add_css_class (nv, "brand-nvidia"); break;
    case ACC_GPU_AMD:    gtk_widget_add_css_class (nv, "brand-amd");    break;
    case ACC_GPU_INTEL:  gtk_widget_add_css_class (nv, "brand-intel");  break;
    default: break;
    }
  gtk_widget_set_size_request (nv, 46, 46);
  {
    /* vendor mark as an image instead of a text badge */
    const gchar *vasset = gpu_vendor_asset ();
    gchar *npath = vasset ? acc_asset_path (vasset) : NULL;
    GtkWidget *nim;
    if (npath)
      {
        nim = gtk_picture_new ();
        gtk_picture_set_filename (GTK_PICTURE (nim), npath);
        gtk_picture_set_content_fit (GTK_PICTURE (nim),
                                     GTK_CONTENT_FIT_CONTAIN);
        gtk_picture_set_can_shrink (GTK_PICTURE (nim), TRUE);
        gtk_widget_set_size_request (nim, 30, 30);
      }
    else
      {
        nim = gtk_image_new_from_icon_name ("video-display-symbolic");
        gtk_image_set_pixel_size (GTK_IMAGE (nim), 22);
      }
    g_free (npath);
    gtk_widget_set_halign (nim, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (nim, GTK_ALIGN_CENTER);
    gtk_box_append (GTK_BOX (nv), nim);
  }
  gtk_widget_set_halign (nv, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (nv, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (head), nv);

  GtkWidget *mid = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  u->gpu_name = lbl ("—", "cardtitle", 0);
  gtk_widget_set_hexpand (u->gpu_name, TRUE);
  gtk_box_append (GTK_BOX (mid), u->gpu_name);
  u->gpu_vram = lbl ("—", "pill", 1);
  gtk_box_append (GTK_BOX (mid), u->gpu_vram);
  gtk_box_append (GTK_BOX (head), mid);

  u->gpu_pill = lbl (acc_tr (ACC_T_PILL_L), "pill on", 1);
  gtk_widget_set_valign (u->gpu_pill, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (head), u->gpu_pill);
  gtk_box_append (GTK_BOX (li), head);

  /* usage row + bar */
  GtkWidget *use = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_append (GTK_BOX (use), lbl (acc_tr (ACC_T_GPU_UTIL), "dim", 0));
  GtkWidget *fill = lbl (NULL, NULL, 0);
  gtk_widget_set_hexpand (fill, TRUE);
  gtk_box_append (GTK_BOX (use), fill);
  u->gpu_util = lbl ("—", "val", 1);
  gtk_box_append (GTK_BOX (use), u->gpu_util);
  gtk_box_append (GTK_BOX (li), use);
  u->gpu_bar = gtk_progress_bar_new ();
  gtk_box_append (GTK_BOX (li), u->gpu_bar);

  /* temperature row */
  GtkWidget *tmp = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_append (GTK_BOX (tmp), lbl (acc_tr (ACC_T_GPU_TEMP), "dim", 0));
  GtkWidget *fill2 = lbl (NULL, NULL, 0);
  gtk_widget_set_hexpand (fill2, TRUE);
  gtk_box_append (GTK_BOX (tmp), fill2);
  u->gpu_temp = lbl ("—", "val", 1);
  gtk_box_append (GTK_BOX (tmp), u->gpu_temp);
  gtk_box_append (GTK_BOX (li), tmp);

  gtk_box_append (GTK_BOX (li), go_btn (acc_tr (ACC_T_GPU_MGR), "gpu"));
  gtk_box_append (GTK_BOX (c), li);
  return c;
}

/* ------------------------------------------------------------------ */
/* oled card                                                           */
/* ------------------------------------------------------------------ */

static void
on_oled_changed (GtkRange *rng, gpointer user)
{
  AccUI *u = ACC_UI_CAST (user);
  if (u->syncing)
    return;
  /* remember the manual interaction: refresh skips this slider for 3 s */
  u->oled_touch = g_get_monotonic_time () / 1000;
  acc_brightness_set (gtk_range_get_value (rng));
}

/* read a GSettings boolean without aborting when the schema is absent */
static gboolean
gs_bool_safe (const gchar *schema, const gchar *key, gboolean def)
{
  GSettingsSchemaSource *src = g_settings_schema_source_get_default ();
  if (!src)
    return def;
  GSettingsSchema *sch = g_settings_schema_source_lookup (src, schema, TRUE);
  if (!sch)
    return def;
  GSettings *st = g_settings_new_full (sch, NULL, NULL);
  g_settings_schema_unref (sch);
  gboolean v = g_settings_get_boolean (st, key);
  g_object_unref (st);
  return v;
}

/* one thin divider between the stacked rows */
static GtkWidget *
hsep (void)
{
  GtkWidget *s = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_add_css_class (s, "hsep");
  return s;
}

/* label on the left, read-only value on the right — MyASUS row style */
static GtkWidget *
kv_row (const gchar *name, GtkWidget **out_value)
{
  GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_margin_top (h, 2);
  gtk_widget_set_margin_bottom (h, 2);
  gtk_box_append (GTK_BOX (h), lbl (name, "dim", 0));
  GtkWidget *fill = lbl (NULL, NULL, 0);
  gtk_widget_set_hexpand (fill, TRUE);
  gtk_box_append (GTK_BOX (h), fill);
  GtkWidget *v = lbl ("—", "val", 1);
  gtk_box_append (GTK_BOX (h), v);
  if (out_value)
    *out_value = v;
  return h;
}

static GtkWidget *
card_oled (AccUI *u)
{
  GtkWidget *c = card ("display-brightness-symbolic", ACC_T_CARD_OLED, NULL);
  GtkWidget *li = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);

  /* 1. brightness: label + percentage on one line, slider below it */
  {
    GtkWidget *top = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append (GTK_BOX (top), lbl (acc_tr (ACC_T_OLED_BR), "dim", 0));
    GtkWidget *fill = lbl (NULL, NULL, 0);
    gtk_widget_set_hexpand (fill, TRUE);
    gtk_box_append (GTK_BOX (top), fill);
    u->oled_pct = lbl ("—", "val", 1);
    gtk_box_append (GTK_BOX (top), u->oled_pct);
    gtk_box_append (GTK_BOX (li), top);

    u->oled_scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL,
                                              (gdouble) 10, (gdouble) 100, 1);
    gtk_scale_set_draw_value (GTK_SCALE (u->oled_scale), FALSE);
    gtk_widget_set_hexpand (u->oled_scale, TRUE);
    gtk_box_append (GTK_BOX (li), u->oled_scale);
  }

  gtk_box_append (GTK_BOX (li), hsep ());

  /* 2. refresh rate */
  gtk_box_append (GTK_BOX (li), kv_row (acc_tr (ACC_T_REFRESH),
                                        &u->oled_mode));

  gtk_box_append (GTK_BOX (li), hsep ());

  /* 3. eye protection — real night-light state, never a fixed "active" */
  {
    GtkWidget *row = kv_row (acc_tr (ACC_T_OLED_EYE), NULL);
    gboolean eye = gs_bool_safe ("org.gnome.settings-daemon.plugins.color",
                                 "night-light-enabled", FALSE);
    GtkWidget *pill = lbl (acc_tr (eye ? ACC_T_ON : ACC_T_OFF),
                           eye ? "pill on" : "pill off", 1);
    gtk_box_append (GTK_BOX (row), pill);
    gtk_box_append (GTK_BOX (li), row);
  }

  gtk_box_append (GTK_BOX (li), go_btn (acc_tr (ACC_T_OLED_SET), "display"));
  g_signal_connect (u->oled_scale, "value-changed",
                    G_CALLBACK (on_oled_changed), u);
  gtk_box_append (GTK_BOX (c), li);
  return c;
}

/* ------------------------------------------------------------------ */
/* system + fans + quick apps                                          */
/* ------------------------------------------------------------------ */

static GtkWidget *
stat_simple (const gchar *title, GtkWidget **out_val, const gchar *icon)
{
  GtkWidget *v = lbl ("—", "val", 1);
  gtk_widget_set_hexpand (v, TRUE);
  GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_append (GTK_BOX (h), lbl (title, "dim", 0));
  gtk_box_append (GTK_BOX (h), v);
  if (icon)
    {
      GtkWidget *im = gtk_image_new_from_icon_name (icon);
      gtk_widget_add_css_class (im, "dim");
      gtk_widget_set_valign (im, GTK_ALIGN_CENTER);
      gtk_box_append (GTK_BOX (h), im);
    }
  if (out_val)
    *out_val = v;
  return h;
}

static GtkWidget *
card_sys (AccUI *u)
{
  GtkWidget *c = card ("dialog-information-symbolic", ACC_T_CARD_SYS, NULL);
  GtkWidget *li = gtk_box_new (GTK_ORIENTATION_VERTICAL, 9);
  gtk_box_append (GTK_BOX (li),
                  stat_simple (acc_tr (ACC_T_SYS_OS), &u->sys_os,
                               "applications-science-symbolic"));
  gtk_box_append (GTK_BOX (li),
                  stat_simple (acc_tr (ACC_T_SYS_RAM), &u->sys_ram,
                               "media-memory-symbolic"));
  gtk_box_append (GTK_BOX (li),
                  stat_simple (acc_tr (ACC_T_SYS_SSD), &u->sys_ssd,
                               "drive-harddisk-symbolic"));
  gtk_box_append (GTK_BOX (li),
                  stat_simple (acc_tr (ACC_T_SYS_TEMP), &u->sys_temp,
                               "weather-clear-symbolic"));
  gtk_box_append (GTK_BOX (li), go_btn (acc_tr (ACC_T_SYS_MORE), "system"));
  gtk_box_append (GTK_BOX (c), li);
  return c;
}

static GtkWidget *
card_fans (AccUI *u)
{
  GtkWidget *c = card ("applications-system-symbolic", ACC_T_CARD_FANS, NULL);
  GtkWidget *li = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);

  GtkWidget *top = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 14);
  GtkWidget *fcol = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  u->ring_fan = ACC_RING (acc_ring_new (acc_theme_col_warn ()));
  gtk_widget_set_size_request (GTK_WIDGET (u->ring_fan), 112, 112);
  gtk_box_append (GTK_BOX (fcol), GTK_WIDGET (u->ring_fan));
  u->fan_small = lbl (acc_tr (ACC_T_SYS_TEMP), "dim", 0);
  gtk_widget_set_halign (u->fan_small, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (fcol), u->fan_small);
  gtk_box_append (GTK_BOX (top), fcol);

  GtkWidget *right = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
  gtk_box_append (GTK_BOX (right),
                  stat_simple (acc_tr (ACC_T_FANS_CPU), &u->fan_cpu,
                               "weather-windy-symbolic"));
  gtk_box_append (GTK_BOX (right),
                  stat_simple (acc_tr (ACC_T_FANS_GPU), &u->fan_gpu,
                               "weather-windy-symbolic"));
  gtk_box_append (GTK_BOX (top), right);
  gtk_box_append (GTK_BOX (li), top);

  /* temperature trend spark */
  GtkWidget *sp = acc_spark_new (acc_theme_col_accent ());
  u->spark = ACC_SPARK (sp);
  gtk_widget_set_size_request (sp, -1, 52);
  gtk_box_append (GTK_BOX (li), sp);
  gtk_box_append (GTK_BOX (li), lbl (acc_tr (ACC_T_FANS_TREND), "foot", 1));

  gtk_box_append (GTK_BOX (li), go_btn (acc_tr (ACC_T_SYS_MORE), "performance"));
  gtk_box_append (GTK_BOX (c), li);
  return c;
}

typedef struct
{
  const gchar *icon;
  const gchar *name;    /* app name shown bold            */
  AccStr       cap;     /* localized caption (AccStr)     */
  const gchar *path;    /* absolute; hidden if absent     */
} QuickApp;

static void
launch_app (GtkButton *b, gpointer user)
{
  (void) b;
  GError *err = NULL;
  GSubprocessLauncher *l = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);
  GSubprocess *p = g_subprocess_launcher_spawn (l, &err, (const gchar *) user,
                                                NULL);
  g_object_unref (l);
  if (!p)
    {
      acc_ui_toast (err ? err->message : "launch failed");
      g_clear_error (&err);
    }
}

static GtkWidget *
card_quick (void)
{
  GtkWidget *c = card ("view-grid-symbolic", ACC_T_CARD_QUICK, NULL);
  GtkWidget *li = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);

  QuickApp apps[] = {
    { "applications-engineering-symbolic", "ArtCraft",   ACC_T_QA_TERM,  NULL },
    { "visual-studio-code",  "VS Code",  ACC_T_QA_CODE, "/usr/bin/code" },
    { "firefox",             "Firefox",  ACC_T_QA_FX,   "/usr/bin/firefox" },
    { "utilities-terminal-symbolic", "Terminal", ACC_T_QA_TERM,
      "/usr/bin/gnome-terminal" },
    { "folder-symbolic",     "Files",    ACC_T_QA_FILES, "/usr/bin/nautilus" },
  };
  for (gsize i = 0; i < G_N_ELEMENTS (apps); i++)
    {
      if (apps[i].path && !acc_path_exists (apps[i].path))
        continue;
      GtkWidget *b = gtk_button_new ();
      gtk_widget_add_css_class (b, "qrow");
      GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
      GtkWidget *im = gtk_image_new_from_icon_name (apps[i].icon);
      gtk_image_set_pixel_size (GTK_IMAGE (im), 24);
      gtk_widget_set_valign (im, GTK_ALIGN_CENTER);
      GtkWidget *col = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
      GtkWidget *t = lbl (apps[i].name, "val", 0);
      gtk_widget_set_hexpand (t, TRUE);
      gtk_box_append (GTK_BOX (col), t);
      gtk_box_append (GTK_BOX (col), lbl (acc_tr (apps[i].cap), "qcap", 0));
      gtk_box_append (GTK_BOX (h), im);
      gtk_box_append (GTK_BOX (h), col);
      {
        GtkWidget *nx = gtk_image_new_from_icon_name ("go-next-symbolic");
        gtk_widget_set_valign (nx, GTK_ALIGN_CENTER);
        gtk_box_append (GTK_BOX (h), nx);
      }
      gtk_button_set_child (GTK_BUTTON (b), h);
      if (apps[i].path)
        g_signal_connect (b, "clicked", G_CALLBACK (launch_app),
                          (gpointer) apps[i].path);
      else
        g_signal_connect (b, "clicked", G_CALLBACK (nav_btn), (gpointer) "gpu");
      gtk_box_append (GTK_BOX (li), b);
    }

  gtk_box_append (GTK_BOX (li), go_btn (acc_tr (ACC_T_QUICK_ALL), "apps"));
  gtk_box_append (GTK_BOX (c), li);
  return c;
}

/* ------------------------------------------------------------------ */
/* MyASUS-style home band                                              */
/* ------------------------------------------------------------------ */

static void
copy_serial (GtkButton *b, gpointer user)
{
  GdkClipboard *cb = gtk_widget_get_clipboard (GTK_WIDGET (b));
  gdk_clipboard_set_text (cb, (const gchar *) user);
}

static GtkWidget *
feat_row (const gchar *icon, const gchar *text, const gchar *page)
{
  GtkWidget *b = gtk_button_new ();
  gtk_widget_add_css_class (b, "featrow");
  GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *im = gtk_image_new_from_icon_name (icon);
  gtk_image_set_pixel_size (GTK_IMAGE (im), 16);
  gtk_widget_set_valign (im, GTK_ALIGN_CENTER);
  GtkWidget *t = lbl (text, NULL, 0);
  gtk_widget_set_hexpand (t, TRUE);
  gtk_widget_set_valign (t, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (h), im);
  gtk_box_append (GTK_BOX (h), t);
  {
    GtkWidget *nx = gtk_image_new_from_icon_name ("go-next-symbolic");
    gtk_widget_set_valign (nx, GTK_ALIGN_CENTER);
    gtk_box_append (GTK_BOX (h), nx);
  }
  gtk_button_set_child (GTK_BUTTON (b), h);
  g_signal_connect (b, "clicked", G_CALLBACK (nav_btn), (gpointer) page);
  return b;
}

/* Pick the laptop photo that matches this machine's model, if one exists.
 * Falls back to the generic "laptop.png" when no specific image is found. */
static gchar *
laptop_image_for_model (void)
{
  gchar *model = dmi_product_name ();
  if (!model || !*model || g_strcmp0 (model, "—") == 0)
    {
      g_free (model);
      return g_strdup ("laptop.png");
    }

  /* slugify: lowercase, spaces → underscores, drop non-alphanumeric */
  GString *slug = g_string_new ("");
  for (const gchar *p = model; *p; p++)
    {
      if (g_ascii_isalnum (*p))
        g_string_append_c (slug, g_ascii_tolower (*p));
      else if (*p == ' ' || *p == '-' || *p == '_')
        g_string_append_c (slug, '_');
    }
  g_free (model);

  /* try several naming conventions */
  gchar *candidates[] = {
    g_strdup_printf ("laptop_%s.png", slug->str),
    g_strdup_printf ("%s.png", slug->str),
  };
  g_string_free (slug, TRUE);

  for (guint i = 0; i < G_N_ELEMENTS (candidates); i++)
    {
      gchar *p = acc_asset_path (candidates[i]);
      if (p)
        {
          g_free (p);
          gchar *chosen = candidates[i];
          for (guint j = i + 1; j < G_N_ELEMENTS (candidates); j++)
            g_free (candidates[j]);
          return chosen;
        }
      g_free (candidates[i]);
    }

  return g_strdup ("laptop.png");
}

/* ---- device picture: real laptop photo ------------------------------
 * A real ASUS laptop photo replaces the drawn mock. No overlay text —
 * the photo speaks for itself. */
static GtkWidget *
lap_mock (AccUI *u)
{
  (void) u;

  gchar *img_name = laptop_image_for_model ();
  gchar *path = acc_asset_path (img_name);
  g_free (img_name);

  GtkWidget *ov = gtk_overlay_new ();
  gtk_widget_set_halign (ov, GTK_ALIGN_FILL);
  gtk_widget_set_valign (ov, GTK_ALIGN_CENTER);

  GtkWidget *pic = gtk_picture_new ();
  if (path)
    gtk_picture_set_filename (GTK_PICTURE (pic), path);
  g_free (path);
  gtk_picture_set_content_fit (GTK_PICTURE (pic), GTK_CONTENT_FIT_CONTAIN);
  gtk_picture_set_can_shrink (GTK_PICTURE (pic), TRUE);
  gtk_widget_set_hexpand (pic, TRUE);
  gtk_widget_set_vexpand (pic, TRUE);
  gtk_overlay_set_child (GTK_OVERLAY (ov), pic);

  /* keep the photo's aspect ratio at every window size */
  GtkWidget *af = gtk_aspect_frame_new (0.5, 0.5, 940.0 / 600.0, TRUE);
  gtk_widget_set_hexpand (af, TRUE);
  gtk_widget_set_valign (af, GTK_ALIGN_CENTER);
  gtk_aspect_frame_set_child (GTK_ASPECT_FRAME (af), ov);

  return af;
}

/* ------------------------------------------------------------------ */
/* page assembly                                                       */
/* ------------------------------------------------------------------ */

GtkWidget *
acc_home_build (AccUI *u)
{
  GtkWidget *scroll = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

  GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 14);
  gtk_widget_set_name (page, "page");
  gtk_widget_set_margin_top (page, 14);
  gtk_widget_set_margin_bottom (page, 16);
  gtk_widget_set_margin_start (page, 20);
  gtk_widget_set_margin_end (page, 20);

  /* ---- hero band: VivoBook title + serial + Featured card ---- */
  GtkWidget *hero = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 24);
  gtk_widget_add_css_class (hero, "achero");
  gtk_widget_set_name (hero, "hero");
  {
    GtkWidget *hcol = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_name (hcol, "hcol");
    gtk_widget_set_valign (hcol, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand (hcol, TRUE);
    gchar *model = dmi_product_name ();
    GtkWidget *title = lbl (model, "hero-title", 0);
    gtk_label_set_ellipsize (GTK_LABEL (title), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars (GTK_LABEL (title), 34);
    g_free (model);
    gtk_box_append (GTK_BOX (hcol), title);

    /* serial number row (root-only file; "—" when unreadable) */
    GtkWidget *srow = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gchar *serial = acc_read_trim ("/sys/class/dmi/id/product_serial");
    gtk_box_append (GTK_BOX (srow), lbl (acc_tr (ACC_T_SERIAL), "foot", 0));
    GtkWidget *sval = lbl (serial && *serial ? serial : "—", "serialval", 0);
    gtk_box_append (GTK_BOX (srow), sval);
    if (serial && *serial)
      {
        GtkWidget *cp = gtk_button_new_from_icon_name ("edit-copy-symbolic");
        gtk_widget_add_css_class (cp, "flat");
        gtk_widget_set_tooltip_text (cp, acc_tr (ACC_T_TT_COPY));
        /* the button owns the serial string for the copy callback */
        g_signal_connect (cp, "clicked", G_CALLBACK (copy_serial), serial);
        g_object_set_data_full (G_OBJECT (cp), "serial", serial, g_free);
        gtk_box_append (GTK_BOX (srow), cp);
      }
    gtk_box_append (GTK_BOX (hcol), srow);

    /* system info row: label + flat "more details" nav button */
    GtkWidget *irow = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);    gtk_box_append (GTK_BOX (irow), lbl (acc_tr (ACC_T_CARD_SYS), "foot", 0));
    GtkWidget *more = gtk_button_new_with_label (acc_tr (ACC_T_SYS_MORE));
    gtk_widget_add_css_class (more, "primarybtn");
    g_signal_connect (more, "clicked", G_CALLBACK (nav_btn), (gpointer) "system");
    gtk_box_append (GTK_BOX (irow), more);

    /* one click → the full hardware summary */
    GtkWidget *specs = acc_specs_button (acc_tr (ACC_T_SPECS), "actionbtn");
    gtk_box_append (GTK_BOX (irow), specs);
    gtk_box_append (GTK_BOX (hcol), irow);

    gtk_box_append (GTK_BOX (hero), hcol);

    /* device pictogram with live battery % — the MyASUS home banner */
    gtk_box_append (GTK_BOX (hero), lap_mock (u));

    /* Featured: honest quick links into this app's pages */
    GtkWidget *fc = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class (fc, "featcard");
    gtk_widget_set_size_request (fc, 300, -1);
    gtk_widget_set_valign (fc, GTK_ALIGN_CENTER);
    gtk_box_append (GTK_BOX (fc), lbl (acc_tr (ACC_T_FEATURED),
                                       "overline", 0));
    gtk_box_append (GTK_BOX (fc), feat_row ("display-brightness-symbolic",
                                            acc_tr (ACC_T_NAV_DISP),
                                            "display"));
    gtk_box_append (GTK_BOX (fc), feat_row ("video-display-symbolic",
                                            acc_tr (ACC_T_NAV_GPU), "gpu"));
    gtk_box_append (GTK_BOX (fc), feat_row ("applications-system-symbolic",
                                            acc_tr (ACC_T_NAV_FANS),
                                            "performance"));
    gtk_box_append (GTK_BOX (hero), fc);
  }
  gtk_box_append (GTK_BOX (page), hero);

  /* cards grid: 3 × 2 — the six quick actions */
  GtkWidget *grid = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (grid), 14);
  gtk_grid_set_row_spacing (GTK_GRID (grid), 14);
  gtk_grid_set_column_homogeneous (GTK_GRID (grid), TRUE);

  gtk_grid_attach (GTK_GRID (grid), card_battery (u), 0, 0, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), card_gpu (u),      1, 0, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), card_oled (u),     2, 0, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), card_sys (u),      0, 1, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), card_fans (u),     1, 1, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), card_quick (),     2, 1, 1, 1);
  gtk_box_append (GTK_BOX (page), grid);

  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), page);
  return scroll;
}
