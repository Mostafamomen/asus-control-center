/* ui/specs.c - "Laptop specifications" dialog
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * One button, one honest snapshot of the machine: every value comes from
 * the existing read-only probes (DMI, /proc, EDID, BAT0, nvidia-smi).
 * Anything unreadable is shown as "—", never guessed.
 */
#include "specs.h"
#include "theme.h"
#include "window.h"
#include "../i18n/i18n.h"
#include "../probe/dmi.h"
#include "../probe/sysinfo.h"
#include "../probe/gpu.h"
#include "../probe/display.h"
#include "../probe/battery.h"
#include "../probe/load.h"
#include "../probe/hwmon.h"
#include "../probe/probe.h"
#include "../probe/devs.h"
#include "../control/brightness.h"

#include <adwaita.h>

/* cross-module: open a dashboard page from the capability list */

/* label ......... value */
static void
add_row (GtkWidget *box, const gchar *key, const gchar *value)
{
  GtkWidget *r = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 16);
  gtk_widget_set_margin_top (r, 3);
  gtk_widget_set_margin_bottom (r, 3);

  GtkWidget *k = gtk_label_new (key);
  gtk_label_set_xalign (GTK_LABEL (k), 0.0);
  gtk_widget_add_css_class (k, "dim");

  GtkWidget *v = gtk_label_new (value && *value ? value : "—");
  gtk_label_set_xalign (GTK_LABEL (v), 1.0);
  gtk_label_set_ellipsize (GTK_LABEL (v), PANGO_ELLIPSIZE_END);
  gtk_label_set_max_width_chars (GTK_LABEL (v), 34);
  gtk_label_set_selectable (GTK_LABEL (v), TRUE);
  gtk_widget_set_hexpand (v, TRUE);
  gtk_widget_add_css_class (v, "val");

  gtk_box_append (GTK_BOX (r), k);
  gtk_box_append (GTK_BOX (r), v);
  gtk_box_append (GTK_BOX (box), r);
}

static GtkWidget *
add_section (GtkWidget *parent, const gchar *title)
{
  GtkWidget *card = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_add_css_class (card, "accentcard");

  GtkWidget *h = gtk_label_new (title);
  gtk_label_set_xalign (GTK_LABEL (h), 0.0);
  gtk_widget_add_css_class (h, "cardtitle");
  gtk_widget_set_margin_bottom (h, 4);
  gtk_box_append (GTK_BOX (card), h);

  gtk_box_append (GTK_BOX (parent), card);
  return card;
}

/* capability row: feature name on the left, live status pill on the right.
 * When the feature is supported, the whole row becomes a shortcut to the
 * page that owns the control (that is what "available" means in practice). */
static void
sup_open_cb (GtkButton *b, gpointer page)
{
  GtkRoot *r = gtk_widget_get_root (GTK_WIDGET (b));
  if (GTK_IS_WINDOW (r))
    gtk_window_destroy (GTK_WINDOW (r));
  if (page)
    acc_ui_nav_to ((const gchar *) page);
}

static GtkWidget *
sup_row_content (GtkWidget *row, const gchar *key, gboolean ok,
                 const gchar *page)
{
  GtkWidget *k = gtk_label_new (key);
  gtk_label_set_xalign (GTK_LABEL (k), 0.0);
  gtk_label_set_wrap (GTK_LABEL (k), TRUE);
  gtk_widget_set_hexpand (k, TRUE);
  gtk_widget_add_css_class (k, "supkey");

  GtkWidget *p = gtk_label_new (acc_tr (ok ? ACC_T_SUP_OK : ACC_T_SUP_NO));
  gtk_widget_add_css_class (p, ok ? "supok" : "supno");
  gtk_widget_set_valign (p, GTK_ALIGN_CENTER);

  gtk_box_append (GTK_BOX (row), k);
  gtk_box_append (GTK_BOX (row), p);

  if (ok && page)
    {
      GtkWidget *arrow = gtk_image_new_from_icon_name ("go-next-symbolic");
      gtk_image_set_pixel_size (GTK_IMAGE (arrow), 12);
      gtk_widget_add_css_class (arrow, "suparrow");
      gtk_widget_set_valign (arrow, GTK_ALIGN_CENTER);
      gtk_box_append (GTK_BOX (row), arrow);
    }
  return row;
}

static void
add_status_row (GtkWidget *box, const gchar *key, gboolean ok,
                const gchar *page)
{
  if (ok && page)
    {
      GtkWidget *b = gtk_button_new ();
      GtkWidget *r = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
      gtk_button_set_child (GTK_BUTTON (b), r);
      gtk_widget_add_css_class (b, "suprow");
      gtk_widget_add_css_class (b, "flat");
      gtk_widget_set_tooltip_text (b, acc_tr (ACC_T_APP_SUPPORT));
      g_signal_connect (b, "clicked", G_CALLBACK (sup_open_cb),
                        (gpointer) page);
      sup_row_content (r, key, ok, page);
      gtk_box_append (GTK_BOX (box), b);
      return;
    }

  GtkWidget *r = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_margin_top (r, 4);
  gtk_widget_set_margin_bottom (r, 4);
  sup_row_content (r, key, ok, NULL);
  gtk_box_append (GTK_BOX (box), r);
}

/* "—" helper: format a double or return NULL when it is negative */
static gchar *
fmt_double (const gchar *fmt, gdouble v)
{
  return v < 0 ? NULL : g_strdup_printf (fmt, v);
}

void
acc_specs_show (GtkWindow *parent)
{
  GtkWidget *win = adw_window_new ();
  gtk_window_set_title (GTK_WINDOW (win), acc_tr (ACC_T_SPECS));
  gtk_window_set_default_size (GTK_WINDOW (win), 580, 700);
  gtk_window_set_modal (GTK_WINDOW (win), TRUE);
  if (parent)
    gtk_window_set_transient_for (GTK_WINDOW (win), parent);

  GtkWidget *tv = adw_toolbar_view_new ();
  GtkWidget *hb = adw_header_bar_new ();
  {
    GtkWidget *t = gtk_label_new (acc_tr (ACC_T_SPECS));
    gtk_widget_add_css_class (t, "hbtitle");
    adw_header_bar_set_title_widget (ADW_HEADER_BAR (hb), t);
  }
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (tv), hb);

  GtkWidget *scroll = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  GtkWidget *body = gtk_box_new (GTK_ORIENTATION_VERTICAL, 14);
  gtk_widget_set_margin_top (body, 16);
  gtk_widget_set_margin_bottom (body, 20);
  gtk_widget_set_margin_start (body, 20);
  gtk_widget_set_margin_end (body, 20);

  /* ---------------- device ---------------- */
  GtkWidget *dev = add_section (body, acc_tr (ACC_T_SPECS_DEVICE));
  {
    gchar *s = dmi_product_full ();
    add_row (dev, acc_tr (ACC_T_SPECS_MODEL), s);
    g_free (s);
    s = dmi_vendor ();
    add_row (dev, acc_tr (ACC_T_SPECS_VENDOR), s);
    g_free (s);
    s = acc_read_trim ("/sys/class/dmi/id/product_serial");
    add_row (dev, acc_tr (ACC_T_SERIAL), s);
    g_free (s);
    s = dmi_bios_version ();
    add_row (dev, acc_tr (ACC_T_BIOS), s);
    g_free (s);
  }

  /* ---------------- processor / memory ---------------- */
  GtkWidget *cpu = add_section (body, acc_tr (ACC_T_SPECS_CPU));
  {
    gchar *s = sysinfo_cpu ();
    add_row (cpu, acc_tr (ACC_T_CPU), s);
    g_free (s);
  }

  GtkWidget *mem = add_section (body, acc_tr (ACC_T_SPECS_MEM));
  {
    LoadInfo ld;
    acc_load_read (&ld);

    gchar *s = fmt_double ("%.1f GB", ld.mem_total_gb);
    add_row (mem, acc_tr (ACC_T_SYS_RAM), s);
    g_free (s);

    s = sysinfo_nvme_model ();
    add_row (mem, acc_tr (ACC_T_SYS_SSD), s);
    g_free (s);

    if (ld.disk_total_gb >= 0)
      s = g_strdup_printf ("%.0f / %.0f GB", ld.disk_used_gb,
                           ld.disk_total_gb);
    else
      s = NULL;
    add_row (mem, acc_tr (ACC_T_TILE_DISK), s);
    g_free (s);
  }

  /* ---------------- graphics ---------------- */
  GtkWidget *gpu = add_section (body, acc_tr (ACC_T_CARD_GPU));
  {
    GpuInfo g;
    gpu_read (&g);
    if (g.ok)
      {
        add_row (gpu, acc_tr (ACC_T_CARD_GPU), g.short_name);
        gchar *s = g.mem_total_gb > 0
          ? g_strdup_printf ("%.0f GB", g.mem_total_gb) : NULL;
        add_row (gpu, acc_tr (ACC_T_SPECS_VRAM), s);
        g_free (s);
      }
    else
      {
        add_row (gpu, acc_tr (ACC_T_CARD_GPU), NULL);
        add_row (gpu, acc_tr (ACC_T_SPECS_VRAM), NULL);
      }
  }

  /* ---------------- display ---------------- */
  GtkWidget *disp = add_section (body, acc_tr (ACC_T_PG_DISP));
  {
    DispInfo d;
    display_read (&d);
    gchar *s = d.ok
      ? g_strdup_printf ("%d × %d @ %.0f Hz", d.width, d.height, d.refresh_hz)
      : NULL;
    add_row (disp, acc_tr (ACC_T_SPECS_RES), s);
    g_free (s);
  }

  /* ---------------- battery ---------------- */
  GtkWidget *bat = add_section (body, acc_tr (ACC_T_CARD_BATTERY));
  {
    BatInfo b;
    battery_read (&b);
    gchar *s = b.ok ? fmt_double ("%.1f Wh", b.wh_design) : NULL;
    add_row (bat, acc_tr (ACC_T_BAT_FULL), s);
    g_free (s);
    s = b.ok ? fmt_double ("%.1f Wh", b.wh_full) : NULL;
    add_row (bat, acc_tr (ACC_T_BAT_NOW), s);
    g_free (s);
    s = (b.ok && b.wh_design > 0)
      ? g_strdup_printf ("%.0f %%", b.wh_full / b.wh_design * 100.0) : NULL;
    add_row (bat, acc_tr (ACC_T_SPECS_HEALTH), s);
    g_free (s);
    s = (b.ok && b.cycle_count >= 0)
      ? g_strdup_printf ("%d", b.cycle_count) : NULL;
    add_row (bat, acc_tr (ACC_T_BAT_CYCLES), s);
    g_free (s);
  }

  /* ---------------- system ---------------- */
  GtkWidget *sys = add_section (body, acc_tr (ACC_T_CARD_SYS));
  {
    gchar *s = sysinfo_os ();
    add_row (sys, acc_tr (ACC_T_SYS_OS), s);
    g_free (s);
    s = acc_read_trim ("/proc/sys/kernel/osrelease");
    add_row (sys, acc_tr (ACC_T_KERNEL), s);
    g_free (s);
  }

  /* ---------------- what this app can do on this machine ---------------- */
  GtkWidget *sup = add_section (body, acc_tr (ACC_T_APP_SUPPORT));
  {
    HwInfo h;
    hwmon_read (&h);

    gchar *ch = acc_read_trim
      ("/sys/firmware/acpi/platform_profile_choices");
    add_status_row (sup, acc_tr (ACC_T_PERF),
                    ch != NULL && *ch != '\0', "performance");
    g_free (ch);

    gchar *f = acc_dev_charge_limit_file ();
    add_status_row (sup, acc_tr (ACC_T_BAT_LIMIT), f != NULL, "battery");
    g_free (f);
    f = acc_dev_wmi_file ("charge_mode");
    add_status_row (sup, acc_tr (ACC_T_CHG_POL), f != NULL, "battery");
    g_free (f);
    f = acc_dev_wmi_file ("gpu_mux_mode");
    add_status_row (sup, acc_tr (ACC_T_GPU_MODE), f != NULL, "gpu");
    g_free (f);
    f = acc_dev_wmi_file ("dgpu_disable");
    add_status_row (sup, acc_tr (ACC_T_DGPU), f != NULL, "gpu");
    g_free (f);
    f = acc_dev_kbd_led_file ();
    add_status_row (sup, acc_tr (ACC_T_KBD), f != NULL, "performance");
    g_free (f);
    add_status_row (sup, acc_tr (ACC_T_CARD_FANS), h.fans_ok,
                    "performance");
    add_status_row (sup, acc_tr (ACC_T_SYS_TEMP),
                    h.t_cpu >= 0 || h.t_board >= 0, "performance");
    add_status_row (sup, acc_tr (ACC_T_SOFTDIM),
                    acc_brightness_available (), "display");
    gboolean night = FALSE;
    GSettingsSchemaSource *ss = g_settings_schema_source_get_default ();
    if (ss)
      {
        GSettingsSchema *sch = g_settings_schema_source_lookup
          (ss, "org.gnome.settings-daemon.plugins.color", TRUE);
        night = sch != NULL;
        if (sch)
          g_settings_schema_unref (sch);
      }
    add_status_row (sup, acc_tr (ACC_T_NIGHT), night, "display");
    add_status_row (sup, acc_tr (ACC_T_CAP_HELPER),
                    g_file_test ("/usr/libexec/asus-control-center/acc-helper",
                                 G_FILE_TEST_IS_EXECUTABLE), "settings");

    GtkWidget *note = gtk_label_new (acc_tr (ACC_T_SUP_NOTE));
    gtk_label_set_xalign (GTK_LABEL (note), 0.0);
    gtk_label_set_wrap (GTK_LABEL (note), TRUE);
    gtk_widget_add_css_class (note, "foot");
    gtk_widget_set_margin_top (note, 6);
    gtk_box_append (GTK_BOX (sup), note);
  }

  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), body);
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (tv), scroll);
  adw_window_set_content (ADW_WINDOW (win), tv);

  gtk_window_present (GTK_WINDOW (win));
}

static void
specs_clicked (GtkButton *b, gpointer user)
{
  (void) user;
  GtkRoot *r = gtk_widget_get_root (GTK_WIDGET (b));
  acc_specs_show (GTK_IS_WINDOW (r) ? GTK_WINDOW (r) : NULL);
}

GtkWidget *
acc_specs_button (const gchar *label, const gchar *css_class)
{
  GtkWidget *b = gtk_button_new_with_label (label);
  if (css_class)
    gtk_widget_add_css_class (b, css_class);
  g_signal_connect (b, "clicked", G_CALLBACK (specs_clicked), NULL);
  return b;
}
