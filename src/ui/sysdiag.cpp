/* ui/sysdiag.cpp - System Diagnosis page
 *
 * Two groups:
 *   Hardware Checks — screen, keyboard, ghost-touch/touchpad, RAM, USB
 *   Software Checks — battery health, thermal, storage SMART, memory usage,
 *                     fans, service status
 *
 * Interactive tests (screen, keyboard) show a "Launch Test" button that opens
 * a GTK window/dialog; result stays UNKNOWN until the user judges.  All other
 * checks read real kernel/tool data; UNKNOWN when data is unavailable.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "sysdiag.h"
#include "theme.h"
#include "mcui.h"
#include "window.h"
#include "../i18n/i18n.h"
#include "../control/asusd.h"
#include "../control/ctl.h"
#include "../control/gfx.h"
#include "../probe/battery.h"
#include "../probe/hwmon.h"
#include "../probe/load.h"
#include "../probe/probe.h"
#include <adwaita.h>
#include <gdk/gdk.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/statvfs.h>
#include <sys/wait.h>

/* ---------------------------------------------------------------- state */

/* hardware group */
static GtkWidget *sd_screen_val,   *sd_screen_pill;
static GtkWidget *sd_kbd_val,      *sd_kbd_pill;
static GtkWidget *sd_touch_val,    *sd_touch_pill;
static GtkWidget *sd_ram_val,      *sd_ram_pill;
static GtkWidget *sd_usb_val,      *sd_usb_pill;

/* software group */
static GtkWidget *sd_bat_val,      *sd_bat_pill;
static GtkWidget *sd_therm_val,    *sd_therm_pill;
static GtkWidget *sd_stor_val,     *sd_stor_pill;
static GtkWidget *sd_memuse_val,   *sd_memuse_pill;
static GtkWidget *sd_fans_val,     *sd_fans_pill;
static GtkWidget *sd_svc_val,      *sd_svc_pill;

static GtkWidget *sd_report;
static GString   *sd_report_text;

/* -------------------------------------------------- helpers ----------- */

static void
sd_pill (GtkWidget *pill, const gchar *text, const gchar *tone)
{
  if (!pill)
    return;
  gtk_label_set_text (GTK_LABEL (pill), text);
  gtk_widget_remove_css_class (pill, "on");
  gtk_widget_remove_css_class (pill, "off");
  gtk_widget_remove_css_class (pill, "soon");
  gtk_widget_add_css_class (pill, tone);
}

static void
sd_set (GtkWidget *lbl, const gchar *fmt, ...)
{
  if (!lbl)
    return;
  va_list ap;
  va_start (ap, fmt);
  gchar *s = g_strdup_vprintf (fmt, ap);
  va_end (ap);
  gtk_label_set_text (GTK_LABEL (lbl), s);
  g_free (s);
}

/* Build one diagnostic card.
 * If launch_label is non-NULL, a "Launch Test" button is shown instead of Run.
 */
static GtkWidget *
sd_card (const gchar *icon, AccStr title,
         GtkWidget **out_val, GtkWidget **out_pill,
         GCallback btn_cb, gpointer btn_data,
         AccStr btn_label)
{
  GtkWidget *c = mc_card ();
  gtk_widget_set_size_request (c, 340, -1);
  gtk_widget_set_hexpand (c, TRUE);

  GtkWidget *head = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *im = gtk_image_new_from_icon_name (icon);
  gtk_image_set_pixel_size (GTK_IMAGE (im), 18);
  gtk_widget_add_css_class (im, "cardicon");
  gtk_widget_set_valign (im, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (head), im);
  gtk_box_append (GTK_BOX (head), mc_title (acc_tr (title)));

  GtkWidget *fill = gtk_label_new (NULL);
  gtk_widget_set_hexpand (fill, TRUE);
  gtk_box_append (GTK_BOX (head), fill);

  if (btn_cb)
    {
      GtkWidget *rb = gtk_button_new_with_label (acc_tr (btn_label));
      gtk_widget_add_css_class (rb, "actionbtn");
      gtk_widget_set_valign (rb, GTK_ALIGN_CENTER);
      g_signal_connect (rb, "clicked", btn_cb, btn_data);
      gtk_box_append (GTK_BOX (head), rb);
    }

  gtk_box_append (GTK_BOX (c), head);

  GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_margin_top (row, 4);
  GtkWidget *v = mc_label ("—", "val", 0);
  gtk_widget_set_hexpand (v, TRUE);
  gtk_label_set_wrap (GTK_LABEL (v), TRUE);
  gtk_box_append (GTK_BOX (row), v);
  GtkWidget *p = mc_label ("—", "pill off", 1);
  gtk_box_append (GTK_BOX (row), p);
  gtk_box_append (GTK_BOX (c), row);

  if (out_val)  *out_val  = v;
  if (out_pill) *out_pill = p;
  return c;
}

/* Build a group box with title, optional "Run Group" button, and a flowbox. */
static GtkWidget *
sd_group_box (AccStr title_key, GCallback run_group_cb,
              GtkWidget **flow_out)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);

  /* group header */
  GtkWidget *hdr = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *lbl = mc_label (acc_tr (title_key), "pagetitle", 0);
  gtk_widget_set_hexpand (lbl, TRUE);
  gtk_box_append (GTK_BOX (hdr), lbl);

  if (run_group_cb)
    {
      GtkWidget *grpbtn = gtk_button_new_with_label (acc_tr (ACC_T_RUN_GROUP));
      gtk_widget_add_css_class (grpbtn, "actionbtn");
      gtk_widget_set_valign (grpbtn, GTK_ALIGN_CENTER);
      g_signal_connect (grpbtn, "clicked", run_group_cb, NULL);
      gtk_box_append (GTK_BOX (hdr), grpbtn);
    }
  gtk_box_append (GTK_BOX (box), hdr);

  GtkWidget *flow = gtk_flow_box_new ();
  gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (flow), GTK_SELECTION_NONE);
  gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (flow), TRUE);
  gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (flow), 14);
  gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (flow), 14);
  gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (flow), 3);
  gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (flow), 1);
  gtk_box_append (GTK_BOX (box), flow);

  if (flow_out) *flow_out = flow;
  return box;
}

/* ---------------------------------------- screen test window ---------- */

static const gchar *sd_screen_colors[] = {
  "black", "white", "red", "green", "blue",
  "gray", "yellow", "cyan", "magenta", NULL
};
static gint sd_screen_idx = 0;
static GtkWidget *sd_screen_win = NULL;
static GtkWidget *sd_screen_area = NULL;
static GtkWidget *sd_screen_hint = NULL;

static void
sd_screen_apply_color (void)
{
  if (!sd_screen_area)
    return;
  gchar *css = g_strdup_printf ("drawingarea { background-color: %s; }",
                                sd_screen_colors[sd_screen_idx]);
  GtkCssProvider *prov = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (prov, css);
  g_free (css);
  gtk_style_context_add_provider_for_display (
      gdk_display_get_default (),
      GTK_STYLE_PROVIDER (prov),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (prov);
  gtk_widget_queue_draw (sd_screen_area);
  if (sd_screen_hint)
    {
      gchar *t = g_strdup_printf ("%s  (%d/%d)  —  %s",
                                  acc_tr (ACC_T_SCREEN_TEST_TITLE),
                                  sd_screen_idx + 1,
                                  9,
                                  acc_tr (ACC_T_SCREEN_TEST_HINT));
      gtk_label_set_text (GTK_LABEL (sd_screen_hint), t);
      g_free (t);
    }
}

static gboolean
screen_key_press (GtkEventControllerKey *ctl,
                  guint keyval, guint keycode, GdkModifierType state,
                  gpointer win)
{
  (void) ctl; (void) keycode; (void) state;
  if (!win)
    return FALSE;
  if (keyval == GDK_KEY_Escape || keyval == GDK_KEY_q || keyval == GDK_KEY_Q)
    {
      gtk_window_close (GTK_WINDOW (win));
      return TRUE;
    }
  if (keyval == GDK_KEY_Right || keyval == GDK_KEY_space || keyval == GDK_KEY_Return)
    {
      if (sd_screen_colors[sd_screen_idx + 1])
        {
          sd_screen_idx++;
          sd_screen_apply_color ();
        }
      return TRUE;
    }
  if (keyval == GDK_KEY_Left)
    {
      if (sd_screen_idx > 0)
        {
          sd_screen_idx--;
          sd_screen_apply_color ();
        }
      return TRUE;
    }
  return FALSE;
}

static void
screen_prev_cb (GtkButton *b, gpointer unused)
{
  (void) b; (void) unused;
  if (sd_screen_idx > 0)
    {
      sd_screen_idx--;
      sd_screen_apply_color ();
    }
}

static void
screen_next_cb (GtkButton *b, gpointer unused)
{
  (void) b; (void) unused;
  if (sd_screen_colors[sd_screen_idx + 1])
    {
      sd_screen_idx++;
      sd_screen_apply_color ();
    }
}

static void
screen_pass_cb (GtkButton *b, gpointer unused)
{
  (void) b; (void) unused;
  sd_set (sd_screen_val, "%s", acc_tr (ACC_T_SCREEN_TEST_PASS));
  sd_pill (sd_screen_pill, "PASS", "on");
  if (sd_screen_win)
    gtk_window_close (GTK_WINDOW (sd_screen_win));
}

static void
screen_fail_cb (GtkButton *b, gpointer unused)
{
  (void) b; (void) unused;
  sd_set (sd_screen_val, "%s", acc_tr (ACC_T_SCREEN_TEST_FAIL));
  sd_pill (sd_screen_pill, "WARN", "soon");
  if (sd_screen_win)
    gtk_window_close (GTK_WINDOW (sd_screen_win));
}

static void
screen_win_destroyed (GtkWidget *win, gpointer unused)
{
  (void) win; (void) unused;
  sd_screen_win = NULL;
  sd_screen_area = NULL;
  sd_screen_hint = NULL;
}

static void
launch_screen_cb (GtkButton *b, gpointer unused)
{
  (void) b; (void) unused;

  if (sd_screen_win)
    {
      gtk_window_present (GTK_WINDOW (sd_screen_win));
      return;
    }

  sd_screen_idx = 0;

  GtkWidget *win = gtk_window_new ();
  gtk_window_set_title (GTK_WINDOW (win), acc_tr (ACC_T_SCREEN_TEST_TITLE));
  gtk_window_fullscreen (GTK_WINDOW (win));
  sd_screen_win = win;
  g_signal_connect (win, "destroy", G_CALLBACK (screen_win_destroyed), NULL);

  sd_screen_area = gtk_drawing_area_new ();
  gtk_widget_set_hexpand (sd_screen_area, TRUE);
  gtk_widget_set_vexpand (sd_screen_area, TRUE);

  GtkWidget *overlay = gtk_overlay_new ();
  gtk_overlay_set_child (GTK_OVERLAY (overlay), sd_screen_area);

  sd_screen_hint = gtk_label_new ("");
  gtk_widget_add_css_class (sd_screen_hint, "pagesub");
  gtk_widget_set_halign (sd_screen_hint, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (sd_screen_hint, GTK_ALIGN_START);
  gtk_widget_set_margin_top (sd_screen_hint, 24);
  gtk_overlay_add_overlay (GTK_OVERLAY (overlay), sd_screen_hint);

  GtkWidget *ctrl = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_halign (ctrl, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (ctrl, GTK_ALIGN_END);
  gtk_widget_set_margin_bottom (ctrl, 24);

  GtkWidget *prev = gtk_button_new_with_label (acc_tr (ACC_T_BACK));
  gtk_widget_add_css_class (prev, "actionbtn");
  g_signal_connect (prev, "clicked", G_CALLBACK (screen_prev_cb), NULL);
  gtk_box_append (GTK_BOX (ctrl), prev);

  GtkWidget *next = gtk_button_new_with_label (acc_tr (ACC_T_NEXT));
  gtk_widget_add_css_class (next, "actionbtn");
  g_signal_connect (next, "clicked", G_CALLBACK (screen_next_cb), NULL);
  gtk_box_append (GTK_BOX (ctrl), next);

  GtkWidget *pass = gtk_button_new_with_label (acc_tr (ACC_T_SCREEN_TEST_PASS));
  gtk_widget_add_css_class (pass, "primarybtn");
  g_signal_connect (pass, "clicked", G_CALLBACK (screen_pass_cb), NULL);
  gtk_box_append (GTK_BOX (ctrl), pass);

  GtkWidget *fail = gtk_button_new_with_label (acc_tr (ACC_T_SCREEN_TEST_FAIL));
  gtk_widget_add_css_class (fail, "actionbtn");
  g_signal_connect (fail, "clicked", G_CALLBACK (screen_fail_cb), NULL);
  gtk_box_append (GTK_BOX (ctrl), fail);

  GtkWidget *exitb = gtk_button_new_with_label (acc_tr (ACC_T_CANCEL));
  gtk_widget_add_css_class (exitb, "actionbtn");
  g_signal_connect (exitb, "clicked", G_CALLBACK (+[](GtkButton*,gpointer w){ gtk_window_close(GTK_WINDOW(w)); }), win);
  gtk_box_append (GTK_BOX (ctrl), exitb);

  gtk_overlay_add_overlay (GTK_OVERLAY (overlay), ctrl);

  gtk_window_set_child (GTK_WINDOW (win), overlay);

  GtkEventController *key_ctl = gtk_event_controller_key_new ();
  g_signal_connect (key_ctl, "key-pressed",
                    G_CALLBACK (screen_key_press), win);
  gtk_widget_add_controller (win, key_ctl);

  gtk_window_present (GTK_WINDOW (win));
  sd_screen_apply_color ();

  sd_set (sd_screen_val, "%s", acc_tr (ACC_T_SCREEN_TEST_RUNNING));
  sd_pill (sd_screen_pill, "UNKNOWN", "off");
}

/* ---------------------------------------- keyboard test dialog -------- */

static GtkWidget *sd_kbd_diag  = NULL;
static GtkWidget *sd_kbd_grid  = NULL;

static gboolean
kbd_key_press_cb (GtkEventControllerKey *ctl,
                  guint keyval, guint keycode, GdkModifierType state,
                  gpointer grid)
{
  (void) ctl; (void) keycode; (void) state; (void) grid;
  const gchar *name = gdk_keyval_name (keyval);
  if (!name)
    return FALSE;

  /* find existing label or create new one */
  GtkWidget *child = gtk_widget_get_first_child (GTK_WIDGET (grid));
  while (child)
    {
      GtkWidget *inner = gtk_flow_box_child_get_child (GTK_FLOW_BOX_CHILD (child));
      if (inner && GTK_IS_LABEL (inner))
        {
          const gchar *t = gtk_label_get_text (GTK_LABEL (inner));
          if (g_strcmp0 (t, name) == 0)
            {
              gtk_widget_add_css_class (inner, "on");
              return FALSE;
            }
        }
      child = gtk_widget_get_next_sibling (child);
    }

  GtkWidget *pill = mc_label (name, "pill on", 1);
  gtk_flow_box_append (GTK_FLOW_BOX (grid), pill);
  return FALSE;
}

static void
kbd_win_destroyed (GtkWidget *win, gpointer unused)
{
  (void) win; (void) unused;
  sd_kbd_diag = NULL;
  sd_kbd_grid = NULL;
}

static void
launch_kbd_cb (GtkButton *b, gpointer unused)
{
  (void) b; (void) unused;

  if (sd_kbd_diag)
    {
      gtk_window_present (GTK_WINDOW (sd_kbd_diag));
      return;
    }

  GtkWidget *dlg = gtk_window_new ();
  gtk_window_set_title (GTK_WINDOW (dlg), acc_tr (ACC_T_KBD_TEST_TITLE));
  gtk_window_set_default_size (GTK_WINDOW (dlg), 560, 320);
  gtk_window_set_modal (GTK_WINDOW (dlg), FALSE);
  sd_kbd_diag = dlg;
  g_signal_connect (dlg, "destroy", G_CALLBACK (kbd_win_destroyed), NULL);

  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_start (box, 16);
  gtk_widget_set_margin_end (box, 16);
  gtk_widget_set_margin_top (box, 16);
  gtk_widget_set_margin_bottom (box, 16);

  GtkWidget *hint = gtk_label_new (acc_tr (ACC_T_KBD_TEST_HINT));
  gtk_label_set_wrap (GTK_LABEL (hint), TRUE);
  gtk_widget_add_css_class (hint, "pagesub");
  gtk_box_append (GTK_BOX (box), hint);

  GtkWidget *flow = gtk_flow_box_new ();
  gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (flow), GTK_SELECTION_NONE);
  gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (flow), 6);
  gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (flow), 6);
  gtk_widget_set_vexpand (flow, TRUE);
  gtk_box_append (GTK_BOX (box), flow);
  sd_kbd_grid = flow;

  gtk_window_set_child (GTK_WINDOW (dlg), box);

  GtkEventController *key_ctl = gtk_event_controller_key_new ();
  g_signal_connect (key_ctl, "key-pressed",
                    G_CALLBACK (kbd_key_press_cb), flow);
  gtk_widget_add_controller (dlg, key_ctl);

  gtk_window_present (GTK_WINDOW (dlg));

  sd_set (sd_kbd_val, "%s", acc_tr (ACC_T_KBD_TEST_RUNNING));
  sd_pill (sd_kbd_pill, "UNKNOWN", "off");
}

/* ---------------------------------------- hw checks ------------------- */

static void
check_touchpad (void)
{
  gboolean found = FALSE;
  gchar *contents = NULL;
  if (g_file_get_contents ("/proc/bus/input/devices", &contents, NULL, NULL))
    {
      /* look for a Touchpad or I2C entry */
      found = (strstr (contents, "Touchpad") != NULL)
           || (strstr (contents, "touchpad") != NULL)
           || (strstr (contents, "TouchPad") != NULL)
           || (strstr (contents, "I2C HID") != NULL);
      g_free (contents);
    }

  if (found)
    {
      sd_set (sd_touch_val, "%s", acc_tr (ACC_T_TOUCH_PRESENT));
      sd_pill (sd_touch_pill, "PASS", "on");
    }
  else
    {
      sd_set (sd_touch_val, "%s", acc_tr (ACC_T_TOUCH_ABSENT));
      sd_pill (sd_touch_pill, "WARN", "soon");
    }
  if (sd_report_text)
    g_string_append_printf (sd_report_text, "%s: %s\n",
                            acc_tr (ACC_T_CHECK_TOUCH),
                            found ? "PASS" : "WARN");
}

static void
check_ram (void)
{
  gchar *contents = NULL;
  guint64 total = 0, avail = 0;
  if (g_file_get_contents ("/proc/meminfo", &contents, NULL, NULL))
    {
      gchar *p = strstr (contents, "MemTotal:");
      if (p)  total = g_ascii_strtoull (p + 9, NULL, 10);
      p = strstr (contents, "MemAvailable:");
      if (p)  avail = g_ascii_strtoull (p + 13, NULL, 10);
      g_free (contents);
    }

  if (total > 0)
    {
      gdouble avail_pct = (gdouble) avail / (gdouble) total * 100.0;
      sd_set (sd_ram_val, "%.0f%% free · %.1f / %.1f GiB",
              avail_pct, avail / 1048576.0, total / 1048576.0);
      if (avail_pct < 15.0)
        sd_pill (sd_ram_pill, "WARN", "soon");
      else
        sd_pill (sd_ram_pill, "PASS", "on");
      if (sd_report_text)
        g_string_append_printf (sd_report_text, "%s: %.0f%% free %s\n",
                                acc_tr (ACC_T_CHECK_RAM), avail_pct,
                                avail_pct < 15.0 ? "WARN" : "PASS");
    }
  else
    {
      sd_set (sd_ram_val, "%s", acc_tr (ACC_T_UNAVAILABLE));
      sd_pill (sd_ram_pill, "UNKNOWN", "off");
      if (sd_report_text)
        g_string_append_printf (sd_report_text, "%s: UNKNOWN\n",
                                acc_tr (ACC_T_CHECK_RAM));
    }
}

static void
check_usb (void)
{
  GDir *d = g_dir_open ("/sys/bus/usb/devices", 0, NULL);
  if (!d)
    {
      sd_set (sd_usb_val, "%s", acc_tr (ACC_T_UNAVAILABLE));
      sd_pill (sd_usb_pill, "UNKNOWN", "off");
      if (sd_report_text)
        g_string_append_printf (sd_report_text, "%s: UNKNOWN\n",
                                acc_tr (ACC_T_CHECK_USB));
      return;
    }

  GString *names = g_string_new (NULL);
  gint real_devs = 0;
  const gchar *entry;
  while ((entry = g_dir_read_name (d)))
    {
      gchar *prod_path = g_build_filename ("/sys/bus/usb/devices", entry,
                                           "product", NULL);
      gchar *prod = NULL;
      if (g_file_get_contents (prod_path, &prod, NULL, NULL))
        {
          g_strstrip (prod);
          /* skip root hub entries */
          if (strstr (prod, "root hub") == NULL &&
              strstr (prod, "Root Hub") == NULL &&
              strstr (prod, "xHCI") == NULL)
            {
              if (names->len > 0) g_string_append (names, ", ");
              g_string_append (names, prod);
              real_devs++;
            }
          g_free (prod);
        }
      g_free (prod_path);
    }
  g_dir_close (d);

  if (real_devs > 0)
    {
      sd_set (sd_usb_val, "%d: %s", real_devs, names->str);
      sd_pill (sd_usb_pill, "PASS", "on");
    }
  else
    {
      sd_set (sd_usb_val, "%s", acc_tr (ACC_T_USB_NONE));
      sd_pill (sd_usb_pill, "WARN", "soon");
    }
  if (sd_report_text)
    g_string_append_printf (sd_report_text, "%s: %d device(s) %s\n",
                            acc_tr (ACC_T_CHECK_USB), real_devs,
                            real_devs > 0 ? "PASS" : "WARN");
  g_string_free (names, TRUE);
}

/* ---------------------------------------- sw checks ------------------- */

static void
check_battery (void)
{
  BatInfo b;
  battery_read (&b);

  gdouble health = (b.wh_full > 0 && b.wh_design > 0)
                   ? b.wh_full / b.wh_design * 100.0 : -1.0;

  if (health >= 0)
    {
      sd_set (sd_bat_val, "%.0f%% · %.1f / %.1f Wh · %d cycles",
              health, b.wh_full, b.wh_design,
              b.cycle_count >= 0 ? b.cycle_count : 0);
      sd_pill (sd_bat_pill, health < 80.0 ? "WARN" : "PASS",
               health < 80.0 ? "soon" : "on");
      if (sd_report_text)
        g_string_append_printf (sd_report_text, "%s: %.0f%% %s\n",
                                acc_tr (ACC_T_CHECK_BAT), health,
                                health < 80.0 ? "WARN" : "PASS");
    }
  else
    {
      sd_set (sd_bat_val, "%s", acc_tr (ACC_T_UNAVAILABLE));
      sd_pill (sd_bat_pill, "UNKNOWN", "off");
      if (sd_report_text)
        g_string_append_printf (sd_report_text, "%s: UNKNOWN\n",
                                acc_tr (ACC_T_CHECK_BAT));
    }
}

static void
check_thermal (void)
{
  HwInfo h;
  hwmon_read (&h);

  if (h.t_cpu > 0)
    {
      if (h.t_nvme > 0)
        sd_set (sd_therm_val, "CPU %.0f°C · NVMe %.0f°C", h.t_cpu, h.t_nvme);
      else
        sd_set (sd_therm_val, "CPU %.0f°C", h.t_cpu);
      sd_pill (sd_therm_pill, h.t_cpu > 85.0 ? "WARN" : "PASS",
               h.t_cpu > 85.0 ? "soon" : "on");
      if (sd_report_text)
        g_string_append_printf (sd_report_text, "%s: CPU %.0f°C %s\n",
                                acc_tr (ACC_T_CHECK_THERMAL), h.t_cpu,
                                h.t_cpu > 85.0 ? "WARN" : "PASS");
    }
  else
    {
      sd_set (sd_therm_val, "%s", acc_tr (ACC_T_UNAVAILABLE));
      sd_pill (sd_therm_pill, "UNKNOWN", "off");
      if (sd_report_text)
        g_string_append_printf (sd_report_text, "%s: UNKNOWN\n",
                                acc_tr (ACC_T_CHECK_THERMAL));
    }
}

static void
check_storage (void)
{
  const gchar *devs[] = {
    "/dev/nvme0", "/dev/nvme0n1", "/dev/sda", "/dev/vda", NULL
  };
  gboolean smart_tried = FALSE, smart_ok = FALSE;
  gchar *smart_msg = NULL;

  for (const gchar **dp = devs; *dp && !smart_tried; dp++)
    {
      if (!g_file_test (*dp, G_FILE_TEST_EXISTS))
        continue;
      gchar *argv[] = { (gchar *)"smartctl", (gchar *)"-H", (gchar *)*dp, NULL };
      gchar *out = NULL;
      gint status = 0;
      if (g_spawn_sync (NULL, argv, NULL,
                        (GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL),
                        NULL, NULL, &out, NULL, &status, NULL) && out)
        {
          smart_tried = TRUE;
          gboolean failing = (status & 8) != 0;
          gboolean passed  = strstr (out, "PASSED") || strstr (out, "OK");
          if (failing)       { smart_ok = FALSE; smart_msg = g_strdup_printf ("SMART FAIL (%s)", *dp); }
          else if (passed)   { smart_ok = TRUE;  smart_msg = g_strdup_printf ("SMART PASSED (%s)", *dp); }
          else               { smart_tried = FALSE; }
          g_free (out);
        }
    }

  if (smart_tried)
    {
      sd_set (sd_stor_val, "%s", smart_msg ? smart_msg : "SMART checked");
      sd_pill (sd_stor_pill, smart_ok ? "PASS" : "FAIL", smart_ok ? "on" : "soon");
      if (sd_report_text)
        g_string_append_printf (sd_report_text, "%s: %s\n",
                                acc_tr (ACC_T_CHECK_STORAGE), smart_ok ? "PASS" : "FAIL");
    }
  else
    {
      LoadInfo ld;
      acc_load_read (&ld);
      if (ld.disk_pct >= 0)
        {
          sd_set (sd_stor_val, "%.0f%% used · %.0f / %.0f GB",
                  ld.disk_pct, ld.disk_used_gb, ld.disk_total_gb);
          sd_pill (sd_stor_pill, ld.disk_pct > 90.0 ? "WARN" : "PASS",
                   ld.disk_pct > 90.0 ? "soon" : "on");
          if (sd_report_text)
            g_string_append_printf (sd_report_text, "%s: %.0f%% %s\n",
                                    acc_tr (ACC_T_CHECK_STORAGE), ld.disk_pct,
                                    ld.disk_pct > 90.0 ? "WARN" : "PASS");
        }
      else
        {
          sd_set (sd_stor_val, "%s", acc_tr (ACC_T_UNAVAILABLE));
          sd_pill (sd_stor_pill, "UNKNOWN", "off");
          if (sd_report_text)
            g_string_append_printf (sd_report_text, "%s: UNKNOWN\n",
                                    acc_tr (ACC_T_CHECK_STORAGE));
        }
    }
  g_free (smart_msg);
}

static void
check_memuse (void)
{
  LoadInfo ld;
  acc_load_read (&ld);

  if (ld.mem_pct >= 0)
    {
      gdouble used_gb = ld.mem_total_gb * ld.mem_pct / 100.0;
      sd_set (sd_memuse_val, "%.0f%% · %.1f / %.1f GiB used",
              ld.mem_pct, used_gb, ld.mem_total_gb);
      sd_pill (sd_memuse_pill, ld.mem_pct > 90.0 ? "WARN" : "PASS",
               ld.mem_pct > 90.0 ? "soon" : "on");
      if (sd_report_text)
        g_string_append_printf (sd_report_text, "%s: %.0f%% %s\n",
                                acc_tr (ACC_T_CHECK_MEMUSE), ld.mem_pct,
                                ld.mem_pct > 90.0 ? "WARN" : "PASS");
    }
  else
    {
      sd_set (sd_memuse_val, "%s", acc_tr (ACC_T_UNAVAILABLE));
      sd_pill (sd_memuse_pill, "UNKNOWN", "off");
      if (sd_report_text)
        g_string_append_printf (sd_report_text, "%s: UNKNOWN\n",
                                acc_tr (ACC_T_CHECK_MEMUSE));
    }
}

static void
check_fans (void)
{
  HwInfo h;
  hwmon_read (&h);

  if (h.fans_ok)
    {
      sd_set (sd_fans_val, "CPU %d rpm · GPU %d rpm", h.fan_cpu, h.fan_gpu);
      sd_pill (sd_fans_pill, "PASS", "on");
      if (sd_report_text)
        g_string_append_printf (sd_report_text,
                                "%s: CPU %d rpm · GPU %d rpm PASS\n",
                                acc_tr (ACC_T_CHECK_FANS), h.fan_cpu, h.fan_gpu);
    }
  else
    {
      sd_set (sd_fans_val, "%s", acc_tr (ACC_T_UNAVAILABLE));
      sd_pill (sd_fans_pill, "UNKNOWN", "off");
      if (sd_report_text)
        g_string_append_printf (sd_report_text, "%s: UNKNOWN\n",
                                acc_tr (ACC_T_CHECK_FANS));
    }
}

static void
check_services (void)
{
  AsusdProfileState ps;
  gboolean asusd = acc_asusd_profiles (&ps) && ps.available;
  AccGfxState gx;
  acc_gfx_state (&gx);
  gboolean gfx    = gx.supergfxd_running;
  gboolean helper = acc_ctl_available ();

  sd_set (sd_svc_val,
          "asusd %s · supergfxd %s · helper %s",
          acc_tr (asusd  ? ACC_T_RUNNING   : ACC_T_STOPPED),
          acc_tr (gfx    ? ACC_T_RUNNING   : ACC_T_STOPPED),
          acc_tr (helper ? ACC_T_INSTALLED : ACC_T_MISSING));

  /* supergfxd running is normal; only a leftover blacklist or a missing
   * helper is a real warning. */
  gboolean warn = gx.supergfx_leftover || !helper;
  sd_pill (sd_svc_pill, warn ? "WARN" : "PASS", warn ? "soon" : "on");
  if (sd_report_text)
    g_string_append_printf (sd_report_text,
                            "%s: asusd %s supergfxd %s helper %s\n",
                            acc_tr (ACC_T_CHECK_SERVICES),
                            acc_tr (asusd  ? ACC_T_RUNNING   : ACC_T_STOPPED),
                            acc_tr (gfx    ? ACC_T_RUNNING   : ACC_T_STOPPED),
                            acc_tr (helper ? ACC_T_INSTALLED : ACC_T_MISSING));
}

/* ---------------------------------------- run groups ------------------ */

static void
run_hw_group (void)
{
  check_touchpad ();
  check_ram ();
  check_usb ();
  /* screen/kbd stay UNKNOWN — user-driven */
  sd_set (sd_screen_val, "%s", acc_tr (ACC_T_SCREEN_TEST_INIT));
  sd_pill (sd_screen_pill, "UNKNOWN", "off");
  sd_set (sd_kbd_val, "%s", acc_tr (ACC_T_KBD_TEST_INIT));
  sd_pill (sd_kbd_pill, "UNKNOWN", "off");
}

static void
run_sw_group (void)
{
  check_battery ();
  check_thermal ();
  check_storage ();
  check_memuse ();
  check_fans ();
  check_services ();
}

static void
run_all (void)
{
  if (!sd_report_text)
    sd_report_text = g_string_new (NULL);
  else
    g_string_set_size (sd_report_text, 0);

  g_string_append_printf (sd_report_text, "ASUSCraft System Diagnosis\n%s\n\n",
                          "─────────────────────────");
  run_hw_group ();
  run_sw_group ();

  if (sd_report)
    gtk_label_set_text (GTK_LABEL (sd_report), sd_report_text->str);
}

/* ---------------------------------------- callbacks ------------------- */

static void run_all_cb       (GtkButton *b, gpointer u) { (void)b; (void)u; run_all (); }
static void run_hw_cb        (GtkButton *b, gpointer u) { (void)b; (void)u; run_hw_group (); }
static void run_sw_cb        (GtkButton *b, gpointer u) { (void)b; (void)u; run_sw_group (); }
static void run_touch_cb     (GtkButton *b, gpointer u) { (void)b; (void)u; check_touchpad (); }
static void run_ram_cb       (GtkButton *b, gpointer u) { (void)b; (void)u; check_ram (); }
static void run_usb_cb       (GtkButton *b, gpointer u) { (void)b; (void)u; check_usb (); }
static void run_bat_cb       (GtkButton *b, gpointer u) { (void)b; (void)u; check_battery (); }
static void run_therm_cb     (GtkButton *b, gpointer u) { (void)b; (void)u; check_thermal (); }
static void run_stor_cb      (GtkButton *b, gpointer u) { (void)b; (void)u; check_storage (); }
static void run_memuse_cb    (GtkButton *b, gpointer u) { (void)b; (void)u; check_memuse (); }
static void run_fans_cb      (GtkButton *b, gpointer u) { (void)b; (void)u; check_fans (); }
static void run_svc_cb       (GtkButton *b, gpointer u) { (void)b; (void)u; check_services (); }

static void
copy_cb (GtkButton *b, gpointer u)
{
  (void) b; (void) u;
  if (!sd_report_text || sd_report_text->len == 0)
    return;
  GdkDisplay *disp = gdk_display_get_default ();
  if (!disp)
    return;
  gdk_clipboard_set_text (gdk_display_get_clipboard (disp), sd_report_text->str);
  acc_ui_toast (acc_tr (ACC_T_TT_COPY));
}

/* ---------------------------------------------- PC Health Check --------- */
/* Read-only. All probing runs on a worker thread; results reach widgets via
 * g_idle_add.  A generation counter drops results from a scan whose page was
 * torn down or superseded. */

#define T(en, ar) (acc_i18n_lang () == ACC_LANG_AR ? (ar) : (en))

enum { HC_OK = 0, HC_ATTN, HC_WARN, HC_NA };

enum {
  HC_BAT = 0, HC_TEMP, HC_FANS,
  HC_RAM, HC_STORAGE, HC_CPU,
  HC_ASUSD, HC_NVIDIA, HC_GPU,
  HC_UPDATES, HC_FWUPD, HC_FLATPAK,
  HC_COUNT
};

typedef struct {
  const gchar *icon;
  const gchar *en, *ar;           /* check name                   */
  const gchar *exp_en, *exp_ar;   /* expected / reference         */
  const gchar *nav;               /* page for the action, or NULL */
  const gchar *act_en, *act_ar;
} HcDef;

static const HcDef hc_defs[HC_COUNT] = {
  { "battery-symbolic", "Battery health", "صحة البطارية",
    "Full capacity at least 80% of design", "السعة الكاملة 80٪ على الأقل من التصميمية",
    "hardware", "Battery settings", "إعدادات البطارية" },
  { "weather-clear-symbolic", "CPU temperature", "حرارة المعالج",
    "Below 80 °C", "أقل من 80 درجة",
    "hardware", "Performance modes", "أوضاع الأداء" },
  { "weather-windy-symbolic", "Fans", "المراوح",
    "Fan sensors readable", "حساسات المراوح مقروءة",
    "hardware", "Fan modes", "أوضاع المروحة" },
  { "media-memory-symbolic", "Memory usage", "استهلاك الذاكرة",
    "Below 85% in use", "أقل من 85٪ مستخدمة",
    "sysmgr", "Open System Manager", "فتح مدير النظام" },
  { "drive-harddisk-symbolic", "Storage space", "مساحة التخزين",
    "At least 15% free on /", "15٪ متاحة على الأقل في /",
    "sysmgr", "Storage Manager", "مدير التخزين" },
  { "utilities-system-monitor-symbolic", "CPU load", "حمل المعالج",
    "1-minute load below CPU count", "حمل الدقيقة أقل من عدد الأنوية",
    "sysmgr", "Process Manager", "مدير العمليات" },
  { "emblem-system-symbolic", "ASUS service (asusd)", "خدمة ASUS (asusd)",
    "asusd active", "asusd نشطة",
    "support", "Support", "الدعم" },
  { "video-display-symbolic", "NVIDIA driver", "تعريف NVIDIA",
    "Driver loaded when an NVIDIA GPU exists", "التعريف محمّل عند وجود بطاقة NVIDIA",
    "softcenter", "Drivers", "التعريفات" },
  { "video-joined-displays-symbolic", "GPU mode", "وضع بطاقة الرسوميات",
    "Hybrid or Ultimate, no conflicting supergfxd", "Hybrid أو Ultimate دون تعارض مع supergfxd",
    "hardware", "GPU settings", "إعدادات بطاقة الرسوميات" },
  { "system-software-update-symbolic", "System updates", "تحديثات النظام",
    "No pending package updates", "لا توجد تحديثات حزم معلّقة",
    "softcenter", "Software Center", "مركز البرمجيات" },
  { "application-x-firmware-symbolic", "Firmware updates", "تحديثات البرمجية الثابتة",
    "No pending firmware updates (fwupd)", "لا توجد تحديثات برمجية ثابتة (fwupd)",
    "softcenter", "Software Center", "مركز البرمجيات" },
  { "system-software-install-symbolic", "Flatpak apps", "تطبيقات Flatpak",
    "No pending Flatpak updates", "لا توجد تحديثات Flatpak معلّقة",
    "softcenter", "Software Center", "مركز البرمجيات" },
};

static GtkWidget *hc_val[HC_COUNT], *hc_pill[HC_COUNT],
                 *hc_expl[HC_COUNT], *hc_btn[HC_COUNT];
static GtkWidget *hc_run_btn, *hc_bar, *hc_stamp, *hc_summary;
static guint      hc_gen;
static gboolean   hc_busy;
static int        hc_status[HC_COUNT];

typedef struct {
  guint gen;
  int   id;       /* -1: scan finished */
  int   status;
  gchar *val, *expl;
} HcMsg;

static void
hc_set_pill (GtkWidget *pill, int st)
{
  static const gchar *tone[] = { "on", "soon", "bad", "off" };
  if (!pill)
    return;
  const gchar *txt =
    st == HC_OK   ? T ("Healthy", "سليم") :
    st == HC_ATTN ? T ("Attention Required", "يحتاج انتباهًا") :
    st == HC_WARN ? T ("Warning", "تحذير") :
                    T ("Not Available", "غير متاح");
  gtk_label_set_text (GTK_LABEL (pill), txt);
  for (int i = 0; i < 4; i++)
    gtk_widget_remove_css_class (pill, tone[i]);
  gtk_widget_add_css_class (pill, tone[st]);
}


static gboolean
hc_apply (gpointer data)
{
  HcMsg *m = (HcMsg *) data;
  if (m->gen == hc_gen)
    {
      if (m->id >= 0)
        {
          hc_status[m->id] = m->status;
          if (hc_val[m->id])
            gtk_label_set_text (GTK_LABEL (hc_val[m->id]), m->val ? m->val : "");
          if (hc_expl[m->id])
            gtk_label_set_text (GTK_LABEL (hc_expl[m->id]), m->expl ? m->expl : "");
          hc_set_pill (hc_pill[m->id], m->status);
          if (hc_btn[m->id])
            gtk_widget_set_visible (hc_btn[m->id],
              hc_defs[m->id].nav && (m->status == HC_ATTN || m->status == HC_WARN));
          if (hc_bar)
            gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (hc_bar),
                                           (m->id + 1) / (gdouble) HC_COUNT);
        }
      else
        {
          hc_busy = FALSE;
          if (hc_bar) gtk_widget_set_visible (hc_bar, FALSE);
          if (hc_run_btn)
            {
              gtk_widget_set_sensitive (hc_run_btn, TRUE);
              gtk_button_set_label (GTK_BUTTON (hc_run_btn),
                                    T ("Run again", "إعادة الفحص"));
            }
          if (hc_stamp)
            {
              GDateTime *now = g_date_time_new_now_local ();
              gchar *t = g_date_time_format (now, "%Y-%m-%d %H:%M");
              gchar *s = g_strdup_printf ("%s %s", T ("Last scan:", "آخر فحص:"), t);
              gtk_label_set_text (GTK_LABEL (hc_stamp), s);
              g_free (s); g_free (t); g_date_time_unref (now);
            }
          if (hc_summary)
            {
              int n[4] = { 0, 0, 0, 0 };
              for (int i = 0; i < HC_COUNT; i++) n[hc_status[i]]++;
              gchar *s = g_strdup_printf (
                T ("%d healthy, %d need attention, %d warnings, %d not available",
                   "%d سليمة، %d تحتاج انتباهًا، %d تحذيرات، %d غير متاحة"),
                n[0], n[1], n[2], n[3]);
              gtk_label_set_text (GTK_LABEL (hc_summary), s);
              g_free (s);
            }
        }
    }
  g_free (m->val); g_free (m->expl); g_free (m);
  return G_SOURCE_REMOVE;
}

static void
hc_post (guint gen, int id, int st, gchar *val, gchar *expl)
{
  HcMsg *m = g_new0 (HcMsg, 1);
  m->gen = gen; m->id = id; m->status = st; m->val = val; m->expl = expl;
  g_idle_add (hc_apply, m);
}

/* argv only, no shell; `timeout` bounds network-backed tools. */
static gboolean
hc_spawn (const gchar *const *argv, gchar **out, gint *exit_code)
{
  gint status = 0;
  gchar *o = NULL;
  gboolean ok = g_spawn_sync (NULL, (gchar **) argv, NULL,
                              (GSpawnFlags) (G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL),
                              NULL, NULL, &o, NULL, &status, NULL);
  if (!ok) { g_free (o); return FALSE; }
  if (exit_code)
    *exit_code = WIFEXITED (status) ? WEXITSTATUS (status) : -1;
  if (out) *out = o; else g_free (o);
  return TRUE;
}

static int
hc_count_lines (const gchar *s)
{
  int n = 0;
  if (!s) return 0;
  gchar **v = g_strsplit (s, "\n", -1);
  for (gchar **p = v; *p; p++)
    if (g_strstrip (*p)[0]) n++;
  g_strfreev (v);
  return n;
}

static gpointer
hc_worker (gpointer data)
{
  guint gen = GPOINTER_TO_UINT (data);

  /* ---- Hardware ---- */
  {
    BatInfo b; memset (&b, 0, sizeof b);
    battery_read (&b);
    if (!b.ok || b.wh_design <= 0 || b.wh_full <= 0)
      hc_post (gen, HC_BAT, HC_NA, g_strdup (T ("No battery data", "لا توجد بيانات للبطارية")),
               g_strdup (T ("No battery was found, or it does not report its design capacity.",
                            "لم يُعثر على بطارية أو أنها لا تُبلغ عن سعتها التصميمية.")));
    else
      {
        int pct = (int) (b.wh_full * 100.0 / b.wh_design + 0.5);
        int st = pct >= 80 ? HC_OK : pct >= 60 ? HC_ATTN : HC_WARN;
        gchar *v = b.cycle_count >= 0
          ? g_strdup_printf (T ("%d%% of design, %d cycles", "%d٪ من التصميمية، %d دورة"), pct, b.cycle_count)
          : g_strdup_printf (T ("%d%% of design", "%d٪ من التصميمية"), pct);
        hc_post (gen, HC_BAT, st, v, g_strdup (
          st == HC_OK ? T ("Battery capacity is in good condition.", "سعة البطارية بحالة جيدة.") :
          st == HC_ATTN ? T ("The battery has worn noticeably. A charge limit slows further wear.",
                            "البطارية فقدت جزءًا ملحوظًا من سعتها. حدّ الشحن يبطئ التآكل.") :
                          T ("The battery has lost much of its capacity. Consider a replacement.",
                             "فقدت البطارية الكثير من سعتها. فكّر في استبدالها.")));
      }
  }
  HwInfo hw; memset (&hw, 0, sizeof hw);
  hwmon_read (&hw);
  {
    if (hw.t_cpu <= 0)
      hc_post (gen, HC_TEMP, HC_NA, g_strdup (T ("No sensor", "لا يوجد حساس")),
               g_strdup (T ("The CPU temperature sensor could not be read.", "تعذّرت قراءة حساس حرارة المعالج.")));
    else
      {
        int st = hw.t_cpu < 80 ? HC_OK : hw.t_cpu < 92 ? HC_ATTN : HC_WARN;
        hc_post (gen, HC_TEMP, st, g_strdup_printf ("%.0f °C", hw.t_cpu), g_strdup (
          st == HC_OK ? T ("Temperature is normal.", "الحرارة طبيعية.") :
          st == HC_ATTN ? T ("Running hot. Check what is using the CPU, or pick a quieter mode.",
                            "الحرارة مرتفعة. راجع ما يستهلك المعالج أو اختر وضعًا أهدأ.") :
                          T ("Very hot. Keep vents clear and reduce load.",
                             "الحرارة مرتفعة جدًا. أبقِ فتحات التهوية نظيفة وخفّف الحمل.")));
      }
  }
  {
    if (!hw.fans_ok || (hw.fan_cpu < 0 && hw.fan_gpu < 0))
      hc_post (gen, HC_FANS, HC_NA, g_strdup (T ("No fan sensor", "لا يوجد حساس مراوح")),
               g_strdup (T ("This laptop does not expose fan speeds to Linux.",
                            "هذا الحاسوب لا يُظهر سرعة المراوح لنظام لينكس.")));
    else
      {
        int st = (hw.fan_cpu == 0 && hw.t_cpu >= 85) ? HC_WARN : HC_OK;
        gchar *v = g_strdup_printf ("CPU %d RPM · GPU %d RPM",
                                    MAX (hw.fan_cpu, 0), MAX (hw.fan_gpu, 0));
        hc_post (gen, HC_FANS, st, v, g_strdup (st == HC_OK
          ? T ("Fans respond normally. 0 RPM at low temperature is expected.",
               "المراوح تعمل طبيعيًا. صفر دورة عند الحرارة المنخفضة أمر متوقع.")
          : T ("CPU fan reads 0 RPM while the CPU is very hot.",
               "مروحة المعالج تقرأ صفرًا بينما المعالج ساخن جدًا.")));
      }
  }

  /* ---- System ---- */
  {
    gchar *mi = NULL;
    long total = 0, avail = -1;
    if (g_file_get_contents ("/proc/meminfo", &mi, NULL, NULL))
      {
        const gchar *p;
        if ((p = strstr (mi, "MemTotal:"))) total = strtol (p + 9, NULL, 10);
        if ((p = strstr (mi, "MemAvailable:"))) avail = strtol (p + 13, NULL, 10);
      }
    g_free (mi);
    if (total <= 0 || avail < 0)
      hc_post (gen, HC_RAM, HC_NA, g_strdup ("—"),
               g_strdup (T ("/proc/meminfo could not be read.", "تعذّرت قراءة ‎/proc/meminfo.")));
    else
      {
        int used = (int) ((total - avail) * 100 / total);
        int st = used < 85 ? HC_OK : used < 95 ? HC_ATTN : HC_WARN;
        hc_post (gen, HC_RAM, st,
                 g_strdup_printf (T ("%d%% of %.1f GB in use", "%d٪ من %.1f جيجابايت مستخدمة"),
                                  used, total / 1048576.0),
                 g_strdup (st == HC_OK ? T ("Plenty of memory is free.", "توجد ذاكرة متاحة كافية.")
                                       : T ("Memory is almost full. Close heavy apps.",
                                            "الذاكرة شبه ممتلئة. أغلق التطبيقات الثقيلة.")));
      }
  }
  {
    struct statvfs sv;
    if (statvfs ("/", &sv) != 0 || sv.f_blocks == 0)
      hc_post (gen, HC_STORAGE, HC_NA, g_strdup ("—"),
               g_strdup (T ("Disk usage could not be read.", "تعذّرت قراءة استخدام القرص.")));
    else
      {
        gdouble total = (gdouble) sv.f_blocks * sv.f_frsize / 1e9;
        gdouble freeg = (gdouble) sv.f_bavail * sv.f_frsize / 1e9;
        int fp = (int) (freeg * 100 / total);
        int st = fp >= 15 ? HC_OK : fp >= 5 ? HC_ATTN : HC_WARN;
        hc_post (gen, HC_STORAGE, st,
                 g_strdup_printf (T ("%.0f GB free of %.0f GB (%d%%)", "%.0f جيجابايت متاحة من %.0f (%d٪)"),
                                  freeg, total, fp),
                 g_strdup (st == HC_OK ? T ("Enough free space.", "توجد مساحة كافية.")
                                       : T ("Low disk space can break updates. Review large files.",
                                            "قلة المساحة قد تُفشل التحديثات. راجع الملفات الكبيرة.")));
      }
  }
  {
    gchar *la = NULL;
    gdouble l1 = -1;
    if (g_file_get_contents ("/proc/loadavg", &la, NULL, NULL))
      l1 = g_ascii_strtod (la, NULL);
    g_free (la);
    int n = (int) g_get_num_processors ();
    if (l1 < 0)
      hc_post (gen, HC_CPU, HC_NA, g_strdup ("—"),
               g_strdup (T ("/proc/loadavg could not be read.", "تعذّرت قراءة ‎/proc/loadavg.")));
    else
      {
        int st = l1 < n ? HC_OK : l1 < 2 * n ? HC_ATTN : HC_WARN;
        hc_post (gen, HC_CPU, st,
                 g_strdup_printf (T ("%.2f on %d threads", "%.2f على %d خيط"), l1, n),
                 g_strdup (st == HC_OK ? T ("The CPU is not overloaded.", "المعالج غير مُثقل.")
                                       : T ("More work is queued than the CPU can run. Check running processes.",
                                            "العمل المنتظر أكبر من قدرة المعالج. راجع العمليات الجارية.")));
      }
  }

  /* ---- Drivers & Graphics ---- */
  {
    gchar *o = NULL; gint ec = -1;
    const gchar *argv[] = { "systemctl", "is-active", "asusd", NULL };
    gboolean ran = hc_spawn (argv, &o, &ec);
    if (!ran)
      hc_post (gen, HC_ASUSD, HC_NA, g_strdup ("—"),
               g_strdup (T ("systemctl is not available.", "systemctl غير متاح.")));
    else
      {
        gchar *state = g_strstrip (g_strdup (o ? o : ""));
        gboolean active = ec == 0;
        gboolean failed = g_strcmp0 (state, "failed") == 0;
        hc_post (gen, HC_ASUSD, active ? HC_OK : failed ? HC_WARN : HC_ATTN,
                 g_strdup (state[0] ? state : "unknown"),
                 g_strdup (active ? T ("Performance modes, charge limit and GPU switching are available.",
                                       "أوضاع الأداء وحد الشحن وتبديل بطاقة الرسوميات متاحة.")
                          : failed ? T ("asusd has failed. Some ASUSCraft controls will not work. Check: journalctl -u asusd",
                                        "فشلت خدمة asusd. بعض عناصر التحكم لن تعمل. راجع: journalctl -u asusd")
                                   : T ("asusd is not running. ASUSCraft falls back to direct sysfs where it can.",
                                        "asusd لا تعمل. يستخدم ASUSCraft واجهة sysfs مباشرة حيث أمكن.")));
        g_free (state);
      }
    g_free (o);
  }
  AccGfxState gs; memset (&gs, 0, sizeof gs);
  acc_gfx_state (&gs);
  {
    gchar *ver = NULL;
    gboolean loaded = g_file_get_contents ("/sys/module/nvidia/version", &ver, NULL, NULL);
    if (!gs.dgpu_present)
      hc_post (gen, HC_NVIDIA, HC_NA, g_strdup (T ("No NVIDIA GPU", "لا توجد بطاقة NVIDIA")),
               g_strdup (T ("No NVIDIA graphics card was detected.", "لم تُكتشف بطاقة NVIDIA.")));
    else if (loaded)
      hc_post (gen, HC_NVIDIA, HC_OK, g_strdup_printf ("nvidia %s", g_strstrip (ver)),
               g_strdup (T ("The proprietary NVIDIA driver is loaded.", "تعريف NVIDIA الرسمي محمّل.")));
    else
      {
        gboolean nouveau = g_file_test ("/sys/module/nouveau", G_FILE_TEST_IS_DIR);
        hc_post (gen, HC_NVIDIA, HC_WARN,
                 g_strdup (nouveau ? "nouveau" : T ("Not loaded", "غير محمّل")),
                 g_strdup (T ("An NVIDIA GPU exists but the NVIDIA driver is not loaded. Gaming and Ultimate mode need it.",
                              "توجد بطاقة NVIDIA لكن تعريفها غير محمّل. الألعاب ووضع Ultimate تحتاجه.")));
      }
    g_free (ver);
  }
  {
    gchar *mux = NULL;
    gboolean have = g_file_get_contents ("/sys/devices/platform/asus-nb-wmi/gpu_mux_mode",
                                         &mux, NULL, NULL);
    if (!have)
      hc_post (gen, HC_GPU, HC_NA, g_strdup (T ("No GPU MUX", "لا يوجد MUX")),
               g_strdup (T ("This laptop's firmware does not expose a display MUX.",
                            "البرمجية الثابتة لا توفّر MUX للشاشة.")));
    else
      {
        int m = atoi (mux);
        const gchar *name = m == 0 ? "Ultimate" : "Hybrid";
        int st = (gs.supergfxd_running || gs.supergfx_leftover) ? HC_ATTN : HC_OK;
        hc_post (gen, HC_GPU, st, g_strdup (name), g_strdup (
          gs.supergfxd_running ? T ("supergfxd is running and may conflict with MUX switching.",
                                    "supergfxd تعمل وقد تتعارض مع تبديل MUX.") :
          gs.supergfx_leftover ? T ("A leftover supergfxd blacklist still blocks the NVIDIA driver.",
                                    "ملف حظر متبقٍ من supergfxd ما زال يحظر تعريف NVIDIA.") :
          m == 0 ? T ("The internal panel is driven by NVIDIA. Battery life is shorter.",
                      "اللوحة الداخلية على NVIDIA. مدة البطارية أقصر.")
                 : T ("NVIDIA sleeps when idle. Best for battery.", "بطاقة NVIDIA تنام عند الخمول. الأفضل للبطارية.")));
      }
    g_free (mux);
  }

  /* ---- Application health ---- */
  {
    gchar *o = NULL; gint ec = -1;
    const gchar *argv[] = { "timeout", "30", "dnf", "check-update", "-q", "--cacheonly", NULL };
    gboolean has_dnf = g_find_program_in_path ("dnf") != NULL;
    if (!has_dnf || !hc_spawn (argv, &o, &ec) || (ec != 0 && ec != 100))
      hc_post (gen, HC_UPDATES, HC_NA, g_strdup ("—"),
               g_strdup (T ("Package update data is not available (dnf missing or no cached metadata).",
                            "بيانات التحديثات غير متاحة (dnf غير موجود أو لا توجد بيانات مخزّنة).")));
    else
      {
        int n = ec == 100 ? hc_count_lines (o) : 0;
        hc_post (gen, HC_UPDATES, n ? HC_ATTN : HC_OK,
                 g_strdup_printf (T ("%d pending", "%d معلّق"), n),
                 g_strdup (n ? T ("Updates bring security and driver fixes. Based on cached metadata.",
                                  "التحديثات تجلب إصلاحات أمنية وتعريفات. بحسب البيانات المخزّنة.")
                             : T ("The system is up to date (cached metadata).", "النظام محدّث (بحسب البيانات المخزّنة).")));
      }
    g_free (o);
  }
  {
    gchar *o = NULL; gint ec = -1;
    const gchar *argv[] = { "timeout", "30", "fwupdmgr", "get-updates", "--no-unreported-check",
                            "--no-metadata-check", "--json", NULL };
    gchar *fp = g_find_program_in_path ("fwupdmgr");
    if (!fp || !hc_spawn (argv, &o, &ec))
      hc_post (gen, HC_FWUPD, HC_NA, g_strdup ("—"),
               g_strdup (T ("fwupd is not installed.", "fwupd غير مثبّت.")));
    else
      {
        int n = 0;
        for (const gchar *p = o; p && (p = strstr (p, "\"Releases\"")); p++) n++;
        if (ec == 124 || (!o || !*o))
          hc_post (gen, HC_FWUPD, HC_NA, g_strdup ("—"),
                   g_strdup (T ("fwupd did not answer.", "لم تستجب fwupd.")));
        else
          hc_post (gen, HC_FWUPD, n ? HC_ATTN : HC_OK,
                   g_strdup_printf (T ("%d device(s) with updates", "%d جهاز به تحديثات"), n),
                   g_strdup (n ? T ("Firmware updates are available from the LVFS.", "تتوفر تحديثات للبرمجية الثابتة من LVFS.")
                               : T ("No firmware updates reported.", "لا توجد تحديثات للبرمجية الثابتة.")));
      }
    g_free (fp); g_free (o);
  }
  {
    gchar *o = NULL; gint ec = -1;
    const gchar *argv[] = { "timeout", "30", "flatpak", "remote-ls", "--updates", "--columns=application", NULL };
    gchar *fp = g_find_program_in_path ("flatpak");
    if (!fp || !hc_spawn (argv, &o, &ec) || ec != 0)
      hc_post (gen, HC_FLATPAK, HC_NA, g_strdup ("—"),
               g_strdup (fp ? T ("Flatpak could not reach its remotes.", "تعذّر على Flatpak الوصول إلى مصادره.")
                            : T ("Flatpak is not installed.", "Flatpak غير مثبّت.")));
    else
      {
        int n = hc_count_lines (o);
        hc_post (gen, HC_FLATPAK, n ? HC_ATTN : HC_OK,
                 g_strdup_printf (T ("%d pending", "%d معلّق"), n),
                 g_strdup (n ? T ("Some Flatpak apps have updates.", "بعض تطبيقات Flatpak لها تحديثات.")
                             : T ("All Flatpak apps are up to date.", "كل تطبيقات Flatpak محدّثة.")));
      }
    g_free (fp); g_free (o);
  }

  hc_post (gen, -1, 0, NULL, NULL);
  return NULL;
}

static void
hc_run (void)
{
  if (hc_busy)
    return;
  hc_busy = TRUE;
  hc_gen++;
  for (int i = 0; i < HC_COUNT; i++)
    {
      hc_status[i] = HC_NA;
      if (hc_val[i]) gtk_label_set_text (GTK_LABEL (hc_val[i]), T ("Checking…", "جارٍ الفحص…"));
      if (hc_btn[i]) gtk_widget_set_visible (hc_btn[i], FALSE);
    }
  if (hc_run_btn)
    {
      gtk_widget_set_sensitive (hc_run_btn, FALSE);
      gtk_button_set_label (GTK_BUTTON (hc_run_btn), T ("Scanning…", "جارٍ الفحص…"));
    }
  if (hc_bar)
    {
      gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (hc_bar), 0);
      gtk_widget_set_visible (hc_bar, TRUE);
    }
  if (hc_summary) gtk_label_set_text (GTK_LABEL (hc_summary), "");
  g_thread_unref (g_thread_new ("pc-health", hc_worker, GUINT_TO_POINTER (hc_gen)));
}

static void
hc_run_cb (GtkButton *b, gpointer d)
{
  (void) b; (void) d;
  hc_run ();
}

static void
hc_nav_cb (GtkButton *b, gpointer d)
{
  (void) b;
  acc_ui_nav_to ((const gchar *) d);
}

static GtkWidget *
hc_card (int id)
{
  const HcDef *d = &hc_defs[id];
  GtkWidget *c = mc_card ();
  gtk_widget_set_size_request (c, 280, -1);
  GtkWidget *top = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_append (GTK_BOX (top), gtk_image_new_from_icon_name (d->icon));
  GtkWidget *name = mc_title (T (d->en, d->ar));
  gtk_widget_set_hexpand (name, TRUE);
  gtk_box_append (GTK_BOX (top), name);
  hc_pill[id] = mc_pill (T ("Not Available", "غير متاح"));
  gtk_widget_add_css_class (hc_pill[id], "off");
  gtk_widget_set_valign (hc_pill[id], GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (top), hc_pill[id]);
  gtk_box_append (GTK_BOX (c), top);

  hc_val[id] = mc_label (T ("Not scanned yet", "لم يُفحص بعد"), "val", 0);
  gtk_label_set_wrap (GTK_LABEL (hc_val[id]), TRUE);
  gtk_box_append (GTK_BOX (c), hc_val[id]);

  gchar *exp = g_strdup_printf ("%s %s", T ("Expected:", "المتوقع:"), T (d->exp_en, d->exp_ar));
  GtkWidget *e = mc_label (exp, "dim", 0);
  g_free (exp);
  gtk_label_set_wrap (GTK_LABEL (e), TRUE);
  gtk_box_append (GTK_BOX (c), e);

  hc_expl[id] = mc_label ("", "dim", 0);
  gtk_label_set_wrap (GTK_LABEL (hc_expl[id]), TRUE);
  gtk_box_append (GTK_BOX (c), hc_expl[id]);

  if (d->nav)
    {
      hc_btn[id] = gtk_button_new_with_label (T (d->act_en, d->act_ar));
      gtk_widget_add_css_class (hc_btn[id], "actionbtn");
      gtk_widget_set_halign (hc_btn[id], GTK_ALIGN_START);
      gtk_widget_set_visible (hc_btn[id], FALSE);
      g_signal_connect (hc_btn[id], "clicked", G_CALLBACK (hc_nav_cb), (gpointer) d->nav);
      gtk_box_append (GTK_BOX (c), hc_btn[id]);
    }
  return c;
}

static GtkWidget *
hc_section (void)
{
  GtkWidget *panel = mc_panel (T ("PC Health Check", "فحص صحة الحاسوب"));
  GtkWidget *sub = mc_label (T ("Check your laptop's health and identify issues that may affect reliability and performance.",
                                "افحص صحة حاسوبك واكتشف المشاكل التي قد تؤثر على الاعتمادية والأداء."),
                             "pagesub", 0);
  gtk_label_set_wrap (GTK_LABEL (sub), TRUE);
  gtk_box_append (GTK_BOX (panel), sub);

  GtkWidget *bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  hc_run_btn = gtk_button_new_with_label (T ("Run Health Check", "تشغيل فحص الصحة"));
  gtk_widget_add_css_class (hc_run_btn, "primarybtn");
  g_signal_connect (hc_run_btn, "clicked", G_CALLBACK (hc_run_cb), NULL);
  gtk_box_append (GTK_BOX (bar), hc_run_btn);
  hc_stamp = mc_label (T ("Not scanned yet", "لم يُفحص بعد"), "dim", 0);
  gtk_widget_set_valign (hc_stamp, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (bar), hc_stamp);
  gtk_box_append (GTK_BOX (panel), bar);

  hc_bar = gtk_progress_bar_new ();
  gtk_widget_set_visible (hc_bar, FALSE);
  gtk_box_append (GTK_BOX (panel), hc_bar);
  hc_summary = mc_label ("", "val", 0);
  gtk_label_set_wrap (GTK_LABEL (hc_summary), TRUE);
  gtk_box_append (GTK_BOX (panel), hc_summary);

  static const struct { const gchar *en, *ar; int from, to; } cats[] = {
    { "Hardware", "العتاد", HC_BAT, HC_FANS },
    { "System", "النظام", HC_RAM, HC_CPU },
    { "Drivers and Graphics", "التعريفات والرسوميات", HC_ASUSD, HC_GPU },
    { "Application Health", "صحة التطبيقات", HC_UPDATES, HC_FLATPAK },
  };
  for (guint k = 0; k < G_N_ELEMENTS (cats); k++)
    {
      gtk_box_append (GTK_BOX (panel), mc_group (T (cats[k].en, cats[k].ar)));
      GtkWidget *flow = gtk_flow_box_new ();
      gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (flow), GTK_SELECTION_NONE);
      gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (flow), TRUE);
      gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (flow), 3);
      gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (flow), 12);
      gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (flow), 12);
      for (int i = cats[k].from; i <= cats[k].to; i++)
        gtk_flow_box_append (GTK_FLOW_BOX (flow), hc_card (i));
      gtk_box_append (GTK_BOX (panel), flow);
    }
  return panel;
}

static void
hc_teardown (void)
{
  hc_gen++;          /* drop in-flight results */
  hc_busy = FALSE;
  for (int i = 0; i < HC_COUNT; i++)
    hc_val[i] = hc_pill[i] = hc_expl[i] = hc_btn[i] = NULL;
  hc_run_btn = hc_bar = hc_stamp = hc_summary = NULL;
}

#undef T

/* ---------------------------------------- page builder ---------------- */

GtkWidget *
acc_sysdiag_page (AccUI *u)
{
  (void) u;

  GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 18);

  /* page header */
  {
    GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
    gtk_widget_add_css_class (h, "pagehead");
    GtkWidget *col = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand (col, TRUE);
    gtk_widget_set_valign (col, GTK_ALIGN_CENTER);
    gtk_box_append (GTK_BOX (col), mc_label ("ASUSCraft", "eyebrow", 0));
    gtk_box_append (GTK_BOX (col), mc_label (acc_tr (ACC_T_NAV_DIAG), "pagetitle", 0));
    GtkWidget *sub = mc_label (acc_tr (ACC_T_SUB_DIAG), "pagesub", 0);
    gtk_label_set_wrap (GTK_LABEL (sub), TRUE);
    gtk_label_set_max_width_chars (GTK_LABEL (sub), 80);
    gtk_box_append (GTK_BOX (col), sub);
    gtk_box_append (GTK_BOX (h), col);
    gchar *mascot = acc_asset_path ("asuscraft.png");
    if (mascot)
      {
        GtkWidget *im = gtk_image_new_from_file (mascot);
        gtk_image_set_pixel_size (GTK_IMAGE (im), 58);
        gtk_widget_add_css_class (im, "pageicon");
        gtk_widget_set_valign (im, GTK_ALIGN_CENTER);
        gtk_box_append (GTK_BOX (h), im);
        g_free (mascot);
      }
    gtk_box_append (GTK_BOX (page), h);
  }

  /* ---- PC Health Check ---- */
  gtk_box_append (GTK_BOX (page), hc_section ());

  /* global action bar */
  {
    GtkWidget *bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start (bar, 2);
    GtkWidget *run = gtk_button_new_with_label (acc_tr (ACC_T_RUN_DIAG));
    gtk_widget_add_css_class (run, "primarybtn");
    g_signal_connect (run, "clicked", G_CALLBACK (run_all_cb), NULL);
    gtk_box_append (GTK_BOX (bar), run);
    GtkWidget *cp = gtk_button_new_with_label (acc_tr (ACC_T_DIAG_COPY));
    gtk_widget_add_css_class (cp, "actionbtn");
    g_signal_connect (cp, "clicked", G_CALLBACK (copy_cb), NULL);
    gtk_box_append (GTK_BOX (bar), cp);
    gtk_box_append (GTK_BOX (page), bar);
  }

  /* ---- System Testing shortcut panel ---- */
  {
    GtkWidget *panel = mc_card ();
    GtkWidget *row   = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);

    GtkWidget *im = gtk_image_new_from_icon_name ("media-playback-start-symbolic");
    gtk_image_set_pixel_size (GTK_IMAGE (im), 20);
    gtk_widget_add_css_class (im, "cardicon");
    gtk_widget_set_valign (im, GTK_ALIGN_CENTER);

    GtkWidget *col = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand (col, TRUE);
    gtk_widget_set_valign (col, GTK_ALIGN_CENTER);
    gtk_box_append (GTK_BOX (col),
                    mc_title (acc_i18n_lang () == ACC_LANG_AR
                              ? "اختبار النظام" : "System Testing"));
    GtkWidget *sub = mc_label (
        acc_i18n_lang () == ACC_LANG_AR
        ? "اختبارات تفاعلية للوحة اللمس والمكبر والميكروفون والكاميرا والشبكة."
        : "Interactive tests for touchpad, speaker, microphone, camera, and network.",
        "caption", 0);
    gtk_label_set_wrap (GTK_LABEL (sub), TRUE);
    gtk_label_set_max_width_chars (GTK_LABEL (sub), 60);
    gtk_box_append (GTK_BOX (col), sub);

    GtkWidget *btn = mc_primary (acc_i18n_lang () == ACC_LANG_AR
                                 ? "فتح" : "Open");
    gtk_widget_set_valign (btn, GTK_ALIGN_CENTER);
    g_signal_connect_swapped (btn, "clicked",
                              G_CALLBACK (acc_ui_nav_to),
                              (gpointer) "systest");

    gtk_box_append (GTK_BOX (row), im);
    gtk_box_append (GTK_BOX (row), col);
    gtk_box_append (GTK_BOX (row), btn);
    gtk_box_append (GTK_BOX (panel), row);
    gtk_box_append (GTK_BOX (page), panel);
  }

  /* ---- Hardware Checks group ---- */
  {
    GtkWidget *flow = NULL;
    GtkWidget *grp = sd_group_box (ACC_T_GRP_HW_CHECKS,
                                   G_CALLBACK (run_hw_cb), &flow);
    gtk_flow_box_append (GTK_FLOW_BOX (flow),
      sd_card ("video-display-symbolic", ACC_T_CHECK_SCREEN,
               &sd_screen_val, &sd_screen_pill,
               G_CALLBACK (launch_screen_cb), NULL, ACC_T_LAUNCH_TEST));
    gtk_flow_box_append (GTK_FLOW_BOX (flow),
      sd_card ("input-keyboard-symbolic", ACC_T_CHECK_KBD,
               &sd_kbd_val, &sd_kbd_pill,
               G_CALLBACK (launch_kbd_cb), NULL, ACC_T_LAUNCH_TEST));
    gtk_flow_box_append (GTK_FLOW_BOX (flow),
      sd_card ("input-touchpad-symbolic", ACC_T_CHECK_TOUCH,
               &sd_touch_val, &sd_touch_pill,
               G_CALLBACK (run_touch_cb), NULL, ACC_T_RUN));
    gtk_flow_box_append (GTK_FLOW_BOX (flow),
      sd_card ("media-memory-symbolic", ACC_T_CHECK_RAM,
               &sd_ram_val, &sd_ram_pill,
               G_CALLBACK (run_ram_cb), NULL, ACC_T_RUN));
    gtk_flow_box_append (GTK_FLOW_BOX (flow),
      sd_card ("drive-removable-media-symbolic", ACC_T_CHECK_USB,
               &sd_usb_val, &sd_usb_pill,
               G_CALLBACK (run_usb_cb), NULL, ACC_T_RUN));
    gtk_box_append (GTK_BOX (page), grp);
  }

  /* ---- Software Checks group ---- */
  {
    GtkWidget *flow = NULL;
    GtkWidget *grp = sd_group_box (ACC_T_GRP_SW_CHECKS,
                                   G_CALLBACK (run_sw_cb), &flow);
    gtk_flow_box_append (GTK_FLOW_BOX (flow),
      sd_card ("battery-symbolic", ACC_T_CHECK_BAT,
               &sd_bat_val, &sd_bat_pill,
               G_CALLBACK (run_bat_cb), NULL, ACC_T_RUN));
    gtk_flow_box_append (GTK_FLOW_BOX (flow),
      sd_card ("weather-clear-symbolic", ACC_T_CHECK_THERMAL,
               &sd_therm_val, &sd_therm_pill,
               G_CALLBACK (run_therm_cb), NULL, ACC_T_RUN));
    gtk_flow_box_append (GTK_FLOW_BOX (flow),
      sd_card ("drive-harddisk-symbolic", ACC_T_CHECK_STORAGE,
               &sd_stor_val, &sd_stor_pill,
               G_CALLBACK (run_stor_cb), NULL, ACC_T_RUN));
    gtk_flow_box_append (GTK_FLOW_BOX (flow),
      sd_card ("utilities-system-monitor-symbolic", ACC_T_CHECK_MEMUSE,
               &sd_memuse_val, &sd_memuse_pill,
               G_CALLBACK (run_memuse_cb), NULL, ACC_T_RUN));
    gtk_flow_box_append (GTK_FLOW_BOX (flow),
      sd_card ("weather-windy-symbolic", ACC_T_CHECK_FANS,
               &sd_fans_val, &sd_fans_pill,
               G_CALLBACK (run_fans_cb), NULL, ACC_T_RUN));
    gtk_flow_box_append (GTK_FLOW_BOX (flow),
      sd_card ("emblem-system-symbolic", ACC_T_CHECK_SERVICES,
               &sd_svc_val, &sd_svc_pill,
               G_CALLBACK (run_svc_cb), NULL, ACC_T_RUN));
    gtk_box_append (GTK_BOX (page), grp);
  }

  /* text report panel */
  {
    GtkWidget *panel = mc_panel (acc_tr (ACC_T_DIAG_HEAD));
    sd_report = mc_label (acc_tr (ACC_T_DIAG_INIT), "diagreport", 0);
    gtk_label_set_wrap (GTK_LABEL (sd_report), TRUE);
    gtk_label_set_selectable (GTK_LABEL (sd_report), TRUE);
    gtk_label_set_xalign (GTK_LABEL (sd_report), 0.0);
    gtk_box_append (GTK_BOX (panel), sd_report);
    gtk_box_append (GTK_BOX (page), panel);
  }

  run_all ();

  return mc_page (page);
}

void
acc_sysdiag_teardown (void)
{
  hc_teardown ();
  sd_screen_val = sd_screen_pill = NULL;
  sd_kbd_val    = sd_kbd_pill    = NULL;
  sd_touch_val  = sd_touch_pill  = NULL;
  sd_ram_val    = sd_ram_pill    = NULL;
  sd_usb_val    = sd_usb_pill    = NULL;
  sd_bat_val    = sd_bat_pill    = NULL;
  sd_therm_val  = sd_therm_pill  = NULL;
  sd_stor_val   = sd_stor_pill   = NULL;
  sd_memuse_val = sd_memuse_pill = NULL;
  sd_fans_val   = sd_fans_pill   = NULL;
  sd_svc_val    = sd_svc_pill    = NULL;
  sd_report     = NULL;
  sd_screen_win = NULL;
  sd_kbd_diag   = NULL;
  sd_kbd_grid   = NULL;
  if (sd_report_text)
    {
      g_string_free (sd_report_text, TRUE);
      sd_report_text = NULL;
    }
}
