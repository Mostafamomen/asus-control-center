/* ui/pcmanager.cpp - PC Manager (compact Linux PC maintenance center)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Reuses existing engines (System Manager, System Diagnosis, Software Center)
 * through their public page builders instead of duplicating them.
 */
#include "pcmanager.h"
#include "theme.h"
#include "mcui.h"
#include "window.h"
#include "sysmgr.h"
#include "sysdiag.h"
#include "softcenter.h"
#include "pages.h"
#include "../i18n/i18n.h"
#include "../probe/load.h"
#include "../probe/sysinfo.h"
#include "../probe/battery.h"
#include "../probe/hwmon.h"
#include "../probe/gpu.h"
#include "../probe/display.h"
#include "../probe/probe.h"
#include <adwaita.h>
#include <glib.h>
#include <string.h>
#include <stdarg.h>

static GtkWidget *pm_stack = NULL;
static GtkWidget *pm_tabs = NULL;

/* ---- Quick Tools (argv-based, availability-checked) ---- */
typedef struct { const gchar *icon; const gchar *label; const gchar *const *argv; } QtDef;

static const gchar *const qt_fm[]   = { "nautilus", NULL };
static const gchar *const qt_term[] = { "gnome-terminal", NULL };
static const gchar *const qt_mon[]  = { "gnome-system-monitor", NULL };
static const gchar *const qt_calc[] = { "gnome-calculator", NULL };
static const gchar *const qt_disk[] = { "baobab", NULL };
static const gchar *const qt_set[]  = { "gnome-control-center", NULL };
static const gchar *const qt_sw[]   = { "gnome-software", NULL };
static const gchar *const qt_upd[]  = { "gnome-software", "--mode=updates", NULL };
static const gchar *const qt_hw[]   = { NULL }; /* handled specially: nav to hardware */

static const QtDef qt_defs[] = {
  { "system-file-manager-symbolic", "File Manager", qt_fm },
  { "utilities-terminal-symbolic",  "Terminal",     qt_term },
  { "utilities-system-monitor-symbolic", "System Monitor", qt_mon },
  { "accessories-calculator-symbolic", "Calculator", qt_calc },
  { "drive-harddisk-symbolic", "Disk Usage", qt_disk },
  { "preferences-system-symbolic", "Settings", qt_set },
  { "software-store-symbolic", "Software", qt_sw },
  { "system-software-update-symbolic", "Updates", qt_upd },
  { "emblem-system-symbolic", "Hardware Controls", NULL },
};

static void
qt_launch_cb (GtkButton *b, gpointer data)
{
  (void) b;
  const QtDef *q = (const QtDef *) data;
  if (!q)
    return;
  if (!q->argv)
    {
      acc_ui_nav_to ("hardware");
      return;
    }
  if (!g_find_program_in_path (q->argv[0]))
    {
      acc_ui_toast ("Tool not installed");
      return;
    }
  GSubprocess *pr = g_subprocess_newv (q->argv,
      (GSubprocessFlags)(G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                         G_SUBPROCESS_FLAGS_STDERR_SILENCE), NULL);
  if (pr)
    g_object_unref (pr);
}

static GtkWidget *
qt_card (const QtDef *q)
{
  GtkWidget *c = mc_card ();
  gtk_widget_set_size_request (c, 120, -1);
  GtkWidget *v = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_halign (v, GTK_ALIGN_CENTER);
  GtkWidget *im = gtk_image_new_from_icon_name (q->icon);
  gtk_image_set_pixel_size (GTK_IMAGE (im), 28);
  gtk_widget_add_css_class (im, "cardicon");
  gtk_box_append (GTK_BOX (v), im);
  GtkWidget *lab = mc_label (q->label, "dim", 0);
  gtk_label_set_wrap (GTK_LABEL (lab), TRUE);
  gtk_label_set_max_width_chars (GTK_LABEL (lab), 12);
  gtk_widget_set_halign (lab, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (v), lab);
  gboolean avail = q->argv == NULL || g_find_program_in_path (q->argv[0]) != NULL;
  if (!avail)
    {
      GtkWidget *un = mc_label ("Unavailable", "foot", 0);
      gtk_widget_set_halign (un, GTK_ALIGN_CENTER);
      gtk_box_append (GTK_BOX (v), un);
      gtk_widget_set_sensitive (c, FALSE);
    }
  gtk_box_append (GTK_BOX (c), v);
  GtkWidget *btn = gtk_button_new ();
  gtk_widget_set_opacity (btn, 0);
  gtk_widget_set_size_request (btn, 1, 1);
  /* make whole card clickable */
  GtkGesture *g = gtk_gesture_click_new ();
  g_signal_connect (g, "pressed", G_CALLBACK (+[](GtkGestureClick *g, int n, double x, double y, gpointer data) {
    (void)g;(void)n;(void)x;(void)y;
    qt_launch_cb (NULL, data);
  }), (gpointer) q);
  gtk_widget_add_controller (c, GTK_EVENT_CONTROLLER (g));
  return c;
}

/* ---- Dashboard ---- */
static GtkWidget *
pm_dashboard (void)
{
  GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_add_css_class (h, "pagehead");
  GtkWidget *col = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
  gtk_widget_set_hexpand (col, TRUE);
  gtk_widget_set_valign (col, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (col), mc_label ("ASUSCraft", "eyebrow", 0));
  gtk_box_append (GTK_BOX (col), mc_label (acc_tr (ACC_T_PC_MANAGER), "pagetitle", 0));
  GtkWidget *sub = mc_label ("PC maintenance, optimization, and quick tools.", "pagesub", 0);
  gtk_label_set_wrap (GTK_LABEL (sub), TRUE);
  gtk_label_set_max_width_chars (GTK_LABEL (sub), 80);
  gtk_box_append (GTK_BOX (col), sub);
  gtk_box_append (GTK_BOX (h), col);
  gtk_box_append (GTK_BOX (page), h);

  /* overall health */
  {
    LoadInfo ld; acc_load_read (&ld);
    GtkWidget *c = mc_card ();
    gtk_box_append (GTK_BOX (c), mc_title ("Overall PC Health"));
    gboolean warn = ld.disk_pct > 90.0 || ld.mem_pct > 90.0;
    GtkWidget *v = mc_label (warn ? "Needs Attention" : "Healthy", "val", 0);
    gtk_box_append (GTK_BOX (c), v);
    gchar *s = g_strdup_printf ("Disk: %.0f%% · RAM: %.0f%%", ld.disk_pct, ld.mem_pct);
    GtkWidget *d = mc_label (s, "dim", 0);
    gtk_box_append (GTK_BOX (c), d);
    g_free (s);
    gtk_box_append (GTK_BOX (page), c);
  }
  /* CPU/RAM/Storage tiles */
  {
    LoadInfo ld; acc_load_read (&ld);
    GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *c1 = mc_card (); gtk_box_append (GTK_BOX (c1), mc_title ("CPU"));
    gchar *t = g_strdup_printf ("%.0f%%", ld.cpu_pct >= 0 ? ld.cpu_pct : 0);
    gtk_box_append (GTK_BOX (c1), mc_label (t, "val", 0)); g_free (t);
    gtk_box_append (GTK_BOX (row), c1);
    GtkWidget *c2 = mc_card (); gtk_box_append (GTK_BOX (c2), mc_title ("RAM"));
    t = g_strdup_printf ("%.0f%%", ld.mem_pct >= 0 ? ld.mem_pct : 0);
    gtk_box_append (GTK_BOX (c2), mc_label (t, "val", 0)); g_free (t);
    gtk_box_append (GTK_BOX (row), c2);
    GtkWidget *c3 = mc_card (); gtk_box_append (GTK_BOX (c3), mc_title ("Storage"));
    t = g_strdup_printf ("%.0f%%", ld.disk_pct >= 0 ? ld.disk_pct : 0);
    gtk_box_append (GTK_BOX (c3), mc_label (t, "val", 0)); g_free (t);
    gtk_box_append (GTK_BOX (row), c3);
    gtk_box_append (GTK_BOX (page), row);
  }
  /* Battery */
  {
    BatInfo b; battery_read (&b);
    GtkWidget *c = mc_card (); gtk_box_append (GTK_BOX (c), mc_title ("Battery"));
    if (b.capacity >= 0)
      {
        gchar *t = g_strdup_printf ("%d%% · %s", b.capacity, acc_status_tr (b.status));
        gtk_box_append (GTK_BOX (c), mc_label (t, "val", 0)); g_free (t);
        if (b.wh_full > 0 && b.wh_design > 0)
          {
            gchar *h = g_strdup_printf ("Health: %.0f%%", b.wh_full / b.wh_design * 100.0);
            gtk_box_append (GTK_BOX (c), mc_label (h, "dim", 0)); g_free (h);
          }
      }
    else
      gtk_box_append (GTK_BOX (c), mc_label ("Unavailable", "dim", 0));
    gtk_box_append (GTK_BOX (page), c);
  }
  /* GPU */
  {
    GpuInfo g; gpu_read (&g);
    GtkWidget *c = mc_card (); gtk_box_append (GTK_BOX (c), mc_title ("GPU"));
    gtk_box_append (GTK_BOX (c), mc_label (g.ok && g.short_name[0] ? g.short_name : "Unavailable", "val", 0));
    if (g.temp_c > 0)
      {
        gchar *t = g_strdup_printf ("%.0f°C", g.temp_c);
        gtk_box_append (GTK_BOX (c), mc_label (t, "dim", 0)); g_free (t);
      }
    gtk_box_append (GTK_BOX (page), c);
  }
  /* Quick access */
  {
    GtkWidget *c = mc_card (); gtk_box_append (GTK_BOX (c), mc_title ("Quick Access"));
    const char *acts[][2] = { {"Boost", "pcmanager"}, {"Health Check", "diagnosis"}, {"Storage", "sysmgr"}, {"Apps", "softcenter"}, {"Protection", "support"} };
    for (int i = 0; i < 5; i++)
      {
        GtkWidget *b = gtk_button_new_with_label (acts[i][0]);
        gtk_widget_add_css_class (b, "actionbtn");
        gtk_widget_set_halign (b, GTK_ALIGN_START);
        g_signal_connect (b, "clicked", G_CALLBACK (+[](GtkButton *b, gpointer p){ (void) b; acc_ui_nav_to ((const gchar *)p); }), (gpointer) acts[i][1]);
        gtk_box_append (GTK_BOX (c), b);
      }
    gtk_box_append (GTK_BOX (page), c);
  }
  return mc_page (page);
}

/* ---- Quick Tools ---- */
static GtkWidget *
pm_quicktools (void)
{
  GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_add_css_class (h, "pagehead");
  GtkWidget *col = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
  gtk_widget_set_hexpand (col, TRUE);
  gtk_widget_set_valign (col, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (col), mc_label ("ASUSCraft", "eyebrow", 0));
  gtk_box_append (GTK_BOX (col), mc_label ("Quick Tools", "pagetitle", 0));
  gtk_box_append (GTK_BOX (h), col);
  gtk_box_append (GTK_BOX (page), h);

  GtkWidget *flow = gtk_flow_box_new ();
  gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (flow), GTK_SELECTION_NONE);
  gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (flow), TRUE);
  gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (flow), 10);
  gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (flow), 10);
  gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (flow), 4);
  gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (flow), 2);
  for (guint i = 0; i < G_N_ELEMENTS (qt_defs); i++)
    gtk_flow_box_append (GTK_FLOW_BOX (flow), qt_card (&qt_defs[i]));
  gtk_box_append (GTK_BOX (page), flow);
  return mc_page (page);
}

/* ---- Boost ---- */
static GtkWidget *
pm_boost (void)
{
  GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_add_css_class (h, "pagehead");
  GtkWidget *col = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
  gtk_widget_set_hexpand (col, TRUE);
  gtk_widget_set_valign (col, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (col), mc_label ("ASUSCraft", "eyebrow", 0));
  gtk_box_append (GTK_BOX (col), mc_label ("PC Boost", "pagetitle", 0));
  GtkWidget *sub = mc_label ("Safe cleanup and resource overview. No automatic changes.", "pagesub", 0);
  gtk_label_set_wrap (GTK_LABEL (sub), TRUE);
  gtk_box_append (GTK_BOX (col), sub);
  gtk_box_append (GTK_BOX (h), col);
  gtk_box_append (GTK_BOX (page), h);

  GtkWidget *c = mc_card ();
  gtk_box_append (GTK_BOX (c), mc_title ("Memory Usage"));
  LoadInfo ld; acc_load_read (&ld);
  gchar *t = g_strdup_printf ("RAM: %.0f%% used", ld.mem_pct >= 0 ? ld.mem_pct : 0);
  gtk_box_append (GTK_BOX (c), mc_label (t, "val", 0)); g_free (t);
  gtk_box_append (GTK_BOX (page), c);

  GtkWidget *c2 = mc_card ();
  gtk_box_append (GTK_BOX (c2), mc_title ("Temporary Files"));
  gtk_box_append (GTK_BOX (c2), mc_label ("Review cache sizes in Storage Management.", "dim", 0));
  GtkWidget *b = gtk_button_new_with_label ("Open Storage Management");
  gtk_widget_add_css_class (b, "actionbtn");
  gtk_widget_set_halign (b, GTK_ALIGN_START);
  g_signal_connect (b, "clicked", G_CALLBACK (+[](GtkButton*,gpointer){ acc_ui_nav_to ("sysmgr"); }), NULL);
  gtk_box_append (GTK_BOX (c2), b);
  gtk_box_append (GTK_BOX (page), c2);

  GtkWidget *c3 = mc_card ();
  gtk_box_append (GTK_BOX (c3), mc_title ("Boost Action"));
  gtk_box_append (GTK_BOX (c3), mc_label ("Clears user cache and thumbnails after confirmation. No process termination.", "dim", 0));
  GtkWidget *bb = gtk_button_new_with_label ("Boost (preview)");
  gtk_widget_add_css_class (bb, "primarybtn");
  gtk_widget_set_halign (bb, GTK_ALIGN_START);
  g_signal_connect (bb, "clicked", G_CALLBACK (+[](GtkButton*,gpointer){ acc_ui_nav_to ("sysmgr"); }), NULL);
  gtk_box_append (GTK_BOX (c3), bb);
  gtk_box_append (GTK_BOX (page), c3);
  return mc_page (page);
}

/* ---- tab switch ---- */
static void
pm_tab_cb (GtkToggleButton *b, gpointer data)
{
  if (!gtk_toggle_button_get_active (b) || !pm_stack)
    return;
  gtk_stack_set_visible_child_name (GTK_STACK (pm_stack), (const gchar *) data);
}

GtkWidget *
acc_page_pcmanager (AccUI *u)
{
  (void) u;
  GtkWidget *root = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_box_set_spacing (GTK_BOX (page), 8);

  /* tabs */
  pm_tabs = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class (pm_tabs, "hubbar");
  gtk_widget_set_margin_start (pm_tabs, 22);
  gtk_widget_set_margin_end (pm_tabs, 22);
  gtk_widget_set_margin_bottom (pm_tabs, 8);
  const char *tabs[][2] = {
    { "Dashboard", "dashboard" },
    { "Boost", "boost" },
    { "Health", "health" },
    { "Storage", "storage" },
    { "Apps", "apps" },
    { "Quick Tools", "quicktools" },
    { "Protection", "protection" },
  };
  GtkToggleButton *g = NULL;
  for (int i = 0; i < 7; i++)
    {
      GtkWidget *b = gtk_toggle_button_new_with_label (tabs[i][0]);
      gtk_widget_add_css_class (b, "hubtab");
      if (g)
        gtk_toggle_button_set_group (GTK_TOGGLE_BUTTON (b), g);
      else
        g = GTK_TOGGLE_BUTTON (b);
      g_signal_connect (b, "toggled", G_CALLBACK (pm_tab_cb), (gpointer) tabs[i][1]);
      gtk_box_append (GTK_BOX (pm_tabs), b);
      if (i == 0)
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b), TRUE);
    }
  gtk_box_append (GTK_BOX (root), pm_tabs);

  pm_stack = gtk_stack_new ();
  gtk_stack_set_transition_type (GTK_STACK (pm_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  gtk_widget_set_vexpand (pm_stack, TRUE);
  gtk_stack_add_named (GTK_STACK (pm_stack), pm_dashboard (), "dashboard");
  gtk_stack_add_named (GTK_STACK (pm_stack), pm_boost (), "boost");
  gtk_stack_add_named (GTK_STACK (pm_stack), acc_sysdiag_page (u), "health");
  gtk_stack_add_named (GTK_STACK (pm_stack), acc_page_sysmgr (u), "storage");
  gtk_stack_add_named (GTK_STACK (pm_stack), acc_page_softcenter (u), "apps");
  gtk_stack_add_named (GTK_STACK (pm_stack), pm_quicktools (), "quicktools");
  gtk_stack_add_named (GTK_STACK (pm_stack), acc_page_support (u), "protection");
  gtk_stack_set_visible_child_name (GTK_STACK (pm_stack), "dashboard");
  gtk_box_append (GTK_BOX (root), pm_stack);

  return mc_page (root);
}

void
acc_pcmanager_teardown (void)
{
  pm_stack = NULL;
  pm_tabs = NULL;
}
