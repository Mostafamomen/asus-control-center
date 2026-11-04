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

/* Map a status code to a localized label and the canonical colour tone, so
 * every check shows one of five clear states: Passed (green), Warning
 * (amber), Failed (red), Unsupported / Not tested (grey). The legacy `tone`
 * argument is ignored — the tone is derived from the status so FAIL is always
 * red and a check can never be mis-toned. Status codes: "PASS", "WARN",
 * "FAIL", "UNSUP", anything else -> Not tested. */
static void
sd_pill (GtkWidget *pill, const gchar *status, const gchar *tone)
{
  (void) tone;
  if (!pill)
    return;
  const gchar *text;
  const gchar *cls;
  if (g_strcmp0 (status, "PASS") == 0)       { text = acc_tr (ACC_T_DST_PASS);  cls = "on"; }
  else if (g_strcmp0 (status, "WARN") == 0)  { text = acc_tr (ACC_T_DST_WARN);  cls = "soon"; }
  else if (g_strcmp0 (status, "FAIL") == 0)  { text = acc_tr (ACC_T_DST_FAIL);  cls = "bad"; }
  else if (g_strcmp0 (status, "UNSUP") == 0) { text = acc_tr (ACC_T_DST_UNSUP); cls = "off"; }
  else                                        { text = acc_tr (ACC_T_DST_NT);    cls = "off"; }
  gtk_label_set_text (GTK_LABEL (pill), text);
  gtk_widget_remove_css_class (pill, "on");
  gtk_widget_remove_css_class (pill, "off");
  gtk_widget_remove_css_class (pill, "soon");
  gtk_widget_remove_css_class (pill, "bad");
  gtk_widget_add_css_class (pill, cls);
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
  gtk_widget_set_size_request (c, 320, -1);
  gtk_widget_set_hexpand (c, TRUE);

  /* accent icon badge + title — the shared ASUSCraft card header used on
   * Home, Hardware and System Tools, so Diagnostics matches the rest */
  GtkWidget *head = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *badge = mc_badge (icon, 18);
  gtk_widget_set_valign (badge, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (head), badge);
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
      sd_pill (sd_ram_pill, "UNSUP", "off");
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
      sd_pill (sd_usb_pill, "UNSUP", "off");
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
      sd_pill (sd_bat_pill, "UNSUP", "off");
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
      sd_pill (sd_therm_pill, "UNSUP", "off");
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
          sd_pill (sd_stor_pill, "UNSUP", "off");
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
      sd_pill (sd_memuse_pill, "UNSUP", "off");
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
      sd_pill (sd_fans_pill, "UNSUP", "off");
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

/* ---- export the report to a file the user chooses (Stage 5) ---- */
static void
save_done (GObject *src, GAsyncResult *res, gpointer u)
{
  (void) u;
  GFile *f = gtk_file_dialog_save_finish (GTK_FILE_DIALOG (src), res, NULL);
  if (!f)
    return;   /* cancelled */
  if (sd_report_text)
    g_file_replace_contents (f, sd_report_text->str, sd_report_text->len,
                             NULL, FALSE, G_FILE_CREATE_NONE, NULL, NULL, NULL);
  gchar *p = g_file_get_path (f);
  gchar *msg = g_strdup_printf ("%s: %s", acc_tr (ACC_T_DST_SAVED),
                                p ? p : "");
  acc_ui_toast (msg);
  g_free (msg);
  g_free (p);
  g_object_unref (f);
}

static void
save_cb (GtkButton *b, gpointer u)
{
  (void) b; (void) u;
  if (!sd_report_text || sd_report_text->len == 0)
    return;
  GtkFileDialog *d = gtk_file_dialog_new ();
  GDateTime *now = g_date_time_new_now_local ();
  gchar *stamp = g_date_time_format (now, "%Y%m%d-%H%M");
  gchar *name = g_strdup_printf ("asuscraft-diagnostics-%s.txt", stamp);
  gtk_file_dialog_set_initial_name (d, name);
  gtk_file_dialog_save (d, GTK_WINDOW (acc_ui_root ()), NULL, save_done, NULL);
  g_free (name);
  g_free (stamp);
  g_date_time_unref (now);
  g_object_unref (d);
}

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
    GtkWidget *sv = gtk_button_new_with_label (acc_tr (ACC_T_DST_SAVE));
    gtk_widget_add_css_class (sv, "actionbtn");
    g_signal_connect (sv, "clicked", G_CALLBACK (save_cb), NULL);
    gtk_box_append (GTK_BOX (bar), sv);
    gtk_box_append (GTK_BOX (page), bar);
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
      sd_card ("media-flash-symbolic", ACC_T_CHECK_RAM,
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
