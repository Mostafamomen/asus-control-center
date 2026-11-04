/* ui/hubs.cpp - ASUSCraft section hubs
 *
 * The sidebar keeps six top-level sections. Customization groups the real
 * controls (performance, battery care, graphics, display); Hardware and
 * System Diagnosis are new live pages built from the same read-only probes
 * the rest of the app uses. Nothing here invents a value: a probe that the
 * kernel does not expose is printed as unavailable.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "hubs.h"
#include "pages.h"
#include "specs.h"
#include "theme.h"
#include "mcui.h"
#include "sysdiag.h"
#include "../i18n/i18n.h"
#include "../control/asusd.h"
#include "../control/ctl.h"
#include "../control/gfx.h"
#include "../probe/battery.h"
#include "../probe/devs.h"
#include "../probe/display.h"
#include "../probe/dmi.h"
#include "../probe/gpu.h"
#include "../probe/hwmon.h"
#include "../probe/load.h"
#include "../probe/probe.h"
#include "../probe/sysinfo.h"
#include <adwaita.h>
#include <stdarg.h>

/* ---------------------------------------------------------------- state */
static GtkStack *s_custom_stack = NULL;
static GtkWidget *s_custom_tabs[4] = { NULL, NULL, NULL, NULL };

static GtkWidget *h_model, *h_vendor, *h_bios;
static GtkWidget *h_cpu, *h_load;
static GtkWidget *h_gpu, *h_vram, *h_driver, *h_gpu_temp;
static GtkWidget *h_mem, *h_mem_used;
static GtkWidget *h_disk, *h_disk_used;
static GtkWidget *h_bat, *h_bat_health, *h_bat_cycles;
static GtkWidget *h_fan_cpu, *h_fan_gpu, *h_cpu_temp, *h_nvme_temp;
static GtkWidget *h_disp, *h_refresh, *h_brightness;


/* --------------------------------------------------------------- helpers */
static void
set_text (GtkWidget *w, const gchar *fmt, ...)
{
  if (!w)
    return;
  va_list ap;
  va_start (ap, fmt);
  gchar *s = g_strdup_vprintf (fmt, ap);
  va_end (ap);
  gtk_label_set_text (GTK_LABEL (w), s);
  g_free (s);
}

static GtkWidget *
kv (const gchar *label, GtkWidget **out)
{
  return mc_row (label, out);
}

static GtkWidget *
page_head (AccStr title, AccStr sub)
{
  GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_add_css_class (h, "pagehead");

  GtkWidget *col = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
  gtk_widget_set_hexpand (col, TRUE);
  gtk_widget_set_valign (col, GTK_ALIGN_CENTER);
  GtkWidget *eye = mc_label ("ASUSCraft", "eyebrow", 0);
  GtkWidget *t = mc_label (acc_tr (title), "pagetitle", 0);
  GtkWidget *s = mc_label (acc_tr (sub), "pagesub", 0);
  gtk_label_set_wrap (GTK_LABEL (s), TRUE);
  gtk_label_set_max_width_chars (GTK_LABEL (s), 80);
  gtk_box_append (GTK_BOX (col), eye);
  gtk_box_append (GTK_BOX (col), t);
  gtk_box_append (GTK_BOX (col), s);
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
  return h;
}

static GtkWidget *
hw_card (const gchar *icon, AccStr title)
{
  /* shared accent icon badge + title header, matching Home and the kit;
   * callers append their rows directly to the returned card */
  GtkWidget *c = mc_card ();
  GtkWidget *head = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *badge = mc_badge (icon, 18);
  gtk_widget_set_valign (badge, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (head), badge);
  gtk_box_append (GTK_BOX (head), mc_title (acc_tr (title)));
  gtk_box_append (GTK_BOX (c), head);
  return c;
}

/* ------------------------------------------------------- customization */
static void
custom_tab_cb (GtkToggleButton *b, gpointer data)
{
  if (!gtk_toggle_button_get_active (b) || !s_custom_stack)
    return;
  gtk_stack_set_visible_child_name (s_custom_stack, (const gchar *) data);
}

static GtkWidget *
hub_shell (AccStr title, AccStr sub, GtkWidget **out_stack)
{
  GtkWidget *root = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append (GTK_BOX (root), page_head (title, sub));

  GtkWidget *bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class (bar, "hubbar");
  gtk_widget_set_margin_start (bar, 22);
  gtk_widget_set_margin_end (bar, 22);
  gtk_widget_set_margin_bottom (bar, 10);
  gtk_box_append (GTK_BOX (root), bar);

  GtkWidget *stack = gtk_stack_new ();
  gtk_stack_set_transition_type (GTK_STACK (stack),
                                 GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  gtk_stack_set_transition_duration (GTK_STACK (stack), 180);
  gtk_widget_set_vexpand (stack, TRUE);
  gtk_box_append (GTK_BOX (root), stack);
  *out_stack = stack;
  return root;
}

static GtkWidget *
hub_tab (GtkWidget *bar, GtkToggleButton *group, const gchar *label,
         const gchar *page, gboolean first)
{
  GtkWidget *b = gtk_toggle_button_new_with_label (label);
  gtk_widget_add_css_class (b, "hubtab");
  if (group)
    gtk_toggle_button_set_group (GTK_TOGGLE_BUTTON (b), group);
  g_signal_connect (b, "toggled", G_CALLBACK (custom_tab_cb),
                    (gpointer) page);
  gtk_box_append (GTK_BOX (bar), b);
  if (first)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b), TRUE);
  return b;
}

GtkWidget *
acc_page_customization (AccUI *u)
{
  GtkWidget *stack = NULL;
  GtkWidget *root = hub_shell (ACC_T_NAV_CUSTOM, ACC_T_SUB_CUSTOM, &stack);
  s_custom_stack = GTK_STACK (stack);

  GtkWidget *bar = gtk_widget_get_next_sibling (
                        gtk_widget_get_first_child (root));

  gtk_stack_add_named (GTK_STACK (stack), acc_page_perf (u), "performance");
  gtk_stack_add_named (GTK_STACK (stack), acc_page_bat (u), "battery");
  gtk_stack_add_named (GTK_STACK (stack), acc_page_gpu (u), "gpu");
  gtk_stack_add_named (GTK_STACK (stack), acc_page_disp (u), "display");
  gtk_stack_set_visible_child_name (GTK_STACK (stack), "performance");

  GtkToggleButton *g = NULL;
  s_custom_tabs[0] = hub_tab (bar, NULL, acc_tr (ACC_T_TAB_PERF),
                              "performance", TRUE);
  g = GTK_TOGGLE_BUTTON (s_custom_tabs[0]);
  s_custom_tabs[1] = hub_tab (bar, g, acc_tr (ACC_T_TAB_BAT), "battery", FALSE);
  s_custom_tabs[2] = hub_tab (bar, g, acc_tr (ACC_T_TAB_GPU), "gpu", FALSE);
  s_custom_tabs[3] = hub_tab (bar, g, acc_tr (ACC_T_TAB_DISP), "display", FALSE);
  return root;
}

/* -------------------------------------------------------------- hardware */
static GtkWidget *
hardware_card (const gchar *icon, AccStr title, GtkWidget *rows)
{
  GtkWidget *c = hw_card (icon, title);
  gtk_box_append (GTK_BOX (c), rows);
  gtk_widget_set_size_request (c, 292, -1);
  gtk_widget_set_hexpand (c, TRUE);
  return c;
}

GtkWidget *
acc_page_hardware (AccUI *u)
{
  (void) u;
  GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 14);
  gtk_box_append (GTK_BOX (page), page_head (ACC_T_NAV_HW, ACC_T_SUB_HW));

  GtkWidget *flow = gtk_flow_box_new ();
  gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (flow), GTK_SELECTION_NONE);
  gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (flow), TRUE);
  gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (flow), 14);
  gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (flow), 14);
  gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (flow), 3);
  gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (flow), 1);

  {
    GtkWidget *r = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append (GTK_BOX (r), kv (acc_tr (ACC_T_SPECS_MODEL), &h_model));
    gtk_box_append (GTK_BOX (r), kv (acc_tr (ACC_T_SPECS_VENDOR), &h_vendor));
    gtk_box_append (GTK_BOX (r), kv (acc_tr (ACC_T_BIOS), &h_bios));
    gtk_flow_box_append (GTK_FLOW_BOX (flow),
                         hardware_card ("computer-symbolic", ACC_T_HW_DEVICE, r));
  }
  {
    GtkWidget *r = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append (GTK_BOX (r), kv (acc_tr (ACC_T_CPU), &h_cpu));
    gtk_box_append (GTK_BOX (r), kv (acc_tr (ACC_T_HW_LOAD), &h_load));
    gtk_flow_box_append (GTK_FLOW_BOX (flow),
                         hardware_card ("application-x-executable-symbolic",
                                        ACC_T_HW_PROC, r));
  }
  {
    GtkWidget *r = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append (GTK_BOX (r), kv (acc_tr (ACC_T_CARD_GPU), &h_gpu));
    gtk_box_append (GTK_BOX (r), kv (acc_tr (ACC_T_GPU_VRAM), &h_vram));
    gtk_box_append (GTK_BOX (r), kv (acc_tr (ACC_T_GPU_DRV), &h_driver));
    gtk_box_append (GTK_BOX (r), kv (acc_tr (ACC_T_TEMP), &h_gpu_temp));
    gtk_flow_box_append (GTK_FLOW_BOX (flow),
                         hardware_card ("video-display-symbolic",
                                        ACC_T_HW_GPU, r));
  }
  {
    GtkWidget *r = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append (GTK_BOX (r), kv (acc_tr (ACC_T_SYS_RAM), &h_mem));
    gtk_box_append (GTK_BOX (r), kv (acc_tr (ACC_T_HW_USED), &h_mem_used));
    gtk_flow_box_append (GTK_FLOW_BOX (flow),
                         hardware_card ("media-flash-symbolic",
                                        ACC_T_HW_MEM, r));
  }
  {
    GtkWidget *r = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append (GTK_BOX (r), kv (acc_tr (ACC_T_SYS_SSD), &h_disk));
    gtk_box_append (GTK_BOX (r), kv (acc_tr (ACC_T_HW_USED), &h_disk_used));
    gtk_flow_box_append (GTK_FLOW_BOX (flow),
                         hardware_card ("drive-harddisk-symbolic",
                                        ACC_T_HW_STORAGE, r));
  }
  {
    GtkWidget *r = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append (GTK_BOX (r), kv (acc_tr (ACC_T_BAT_NOW), &h_bat));
    gtk_box_append (GTK_BOX (r), kv (acc_tr (ACC_T_BAT_HEALTH), &h_bat_health));
    gtk_box_append (GTK_BOX (r), kv (acc_tr (ACC_T_BAT_CYCLES), &h_bat_cycles));
    gtk_flow_box_append (GTK_FLOW_BOX (flow),
                         hardware_card ("battery-symbolic",
                                        ACC_T_HW_BATTERY, r));
  }
  {
    GtkWidget *r = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append (GTK_BOX (r), kv (acc_tr (ACC_T_FANS_CPU), &h_fan_cpu));
    gtk_box_append (GTK_BOX (r), kv (acc_tr (ACC_T_FANS_GPU), &h_fan_gpu));
    gtk_box_append (GTK_BOX (r), kv (acc_tr (ACC_T_TEMP), &h_cpu_temp));
    gtk_box_append (GTK_BOX (r), kv ("NVMe", &h_nvme_temp));
    gtk_flow_box_append (GTK_FLOW_BOX (flow),
                         hardware_card ("weather-windy-symbolic",
                                        ACC_T_HW_COOLING, r));
  }
  {
    GtkWidget *r = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append (GTK_BOX (r), kv (acc_tr (ACC_T_SPECS_RES), &h_disp));
    gtk_box_append (GTK_BOX (r), kv (acc_tr (ACC_T_REFRESH), &h_refresh));
    gtk_box_append (GTK_BOX (r), kv (acc_tr (ACC_T_BIOS), &h_brightness));
    gtk_flow_box_append (GTK_FLOW_BOX (flow),
                         hardware_card ("display-brightness-symbolic",
                                        ACC_T_HW_DISPLAY, r));
  }

  gtk_box_append (GTK_BOX (page), flow);

  GtkWidget *specs = acc_specs_button (acc_tr (ACC_T_SPECS), "primarybtn");
  gtk_widget_set_halign (specs, GTK_ALIGN_START);
  gtk_box_append (GTK_BOX (page), specs);

  acc_hubs_refresh ();
  return mc_page (page);
}

/* ------------------------------------------------------------- diagnosis */
GtkWidget *
acc_page_diagnosis (AccUI *u)
{
  return acc_sysdiag_page (u);
}

/* ------------------------------------------------------- updates/support */
GtkWidget *
acc_page_updates (AccUI *u)
{
  return acc_page_support (u);
}

/* -------------------------------------------------------------- refresh */
void
acc_hubs_refresh (void)
{
  if (!h_model)
    return;

  BatInfo b;
  battery_read (&b);
  HwInfo h;
  hwmon_read (&h);
  LoadInfo ld;
  acc_load_read (&ld);
  GpuInfo g;
  gpu_read (&g);
  DispInfo dp;
  display_read (&dp);

  gdouble health = (b.wh_full > 0 && b.wh_design > 0)
                   ? b.wh_full / b.wh_design * 100.0 : -1.0;

  if (h_model)
    {
      gchar *m = dmi_product_name ();
      gchar *v = dmi_vendor ();
      gchar *bios = dmi_bios_version ();
      gchar *cpu = sysinfo_cpu ();
      gchar *ssd = sysinfo_nvme_model ();
      set_text (h_model, "%s", m && *m ? m : acc_tr (ACC_T_UNAVAILABLE));
      set_text (h_vendor, "%s", v && *v ? v : "—");
      set_text (h_bios, "%s", bios && *bios ? bios : "—");
      set_text (h_cpu, "%s", cpu && *cpu ? cpu : acc_tr (ACC_T_UNAVAILABLE));
      set_text (h_disk, "%s", ssd && *ssd ? ssd : acc_tr (ACC_T_UNAVAILABLE));
      g_free (m);
      g_free (v);
      g_free (bios);
      g_free (cpu);
      g_free (ssd);

      if (ld.cpu_pct >= 0)
        set_text (h_load, "%.0f%%", ld.cpu_pct);
      else
        set_text (h_load, "—");

      set_text (h_gpu, "%s", g.ok && g.short_name[0] ? g.short_name
                                                     : acc_tr (ACC_T_UNAVAILABLE));
      if (g.mem_total_gb > 0)
        set_text (h_vram, "%.1f GB", g.mem_total_gb);
      else
        set_text (h_vram, "—");
      set_text (h_driver, "%s", g.driver[0] ? g.driver : "—");
      if (g.temp_c > 0)
        set_text (h_gpu_temp, "%.0f°C", g.temp_c);
      else
        set_text (h_gpu_temp, "—");

      if (ld.mem_total_gb > 0)
        set_text (h_mem, "%.1f GiB", ld.mem_total_gb);
      else
        set_text (h_mem, "—");
      if (ld.mem_pct >= 0)
        set_text (h_mem_used, "%.0f%% · %.1f GiB", ld.mem_pct,
                  ld.mem_total_gb * ld.mem_pct / 100.0);
      else
        set_text (h_mem_used, "—");

      if (ld.disk_total_gb > 0)
        set_text (h_disk_used, "%.0f%% · %.0f / %.0f GiB", ld.disk_pct,
                  ld.disk_used_gb, ld.disk_total_gb);
      else
        set_text (h_disk_used, "—");

      if (b.capacity >= 0)
        set_text (h_bat, "%d%% · %s", b.capacity, acc_status_tr (b.status));
      else
        set_text (h_bat, "—");
      if (health >= 0)
        set_text (h_bat_health, "%.0f%%", health);
      else
        set_text (h_bat_health, "—");
      if (b.cycle_count >= 0)
        set_text (h_bat_cycles, "%d", b.cycle_count);
      else
        set_text (h_bat_cycles, "—");

      if (h.fans_ok)
        {
          set_text (h_fan_cpu, "%d rpm", h.fan_cpu);
          set_text (h_fan_gpu, "%d rpm", h.fan_gpu);
        }
      else
        {
          set_text (h_fan_cpu, acc_tr (ACC_T_UNAVAILABLE));
          set_text (h_fan_gpu, acc_tr (ACC_T_UNAVAILABLE));
        }
      if (h.t_cpu > 0)
        set_text (h_cpu_temp, "%.0f°C", h.t_cpu);
      else
        set_text (h_cpu_temp, "—");
      if (h.t_nvme > 0)
        set_text (h_nvme_temp, "%.0f°C", h.t_nvme);
      else
        set_text (h_nvme_temp, "—");

      if (dp.width > 0 && dp.height > 0)
        set_text (h_disp, "%d × %d", dp.width, dp.height);
      else
        set_text (h_disp, "—");
      if (dp.refresh_hz > 0)
        set_text (h_refresh, "%.0f Hz", dp.refresh_hz);
      else
        set_text (h_refresh, "—");
      if (dp.backlight_now >= 0 && dp.backlight_max > 0)
        set_text (h_brightness, "%d%%", dp.backlight_now * 100 / dp.backlight_max);
      else
        set_text (h_brightness, "—");
    }

}

void
acc_hubs_teardown (void)
{
  s_custom_stack = NULL;
  for (guint i = 0; i < G_N_ELEMENTS (s_custom_tabs); i++)
    s_custom_tabs[i] = NULL;
  h_model = h_vendor = h_bios = h_cpu = h_load = NULL;
  h_gpu = h_vram = h_driver = h_gpu_temp = NULL;
  h_mem = h_mem_used = h_disk = h_disk_used = NULL;
  h_bat = h_bat_health = h_bat_cycles = NULL;
  h_fan_cpu = h_fan_gpu = h_cpu_temp = h_nvme_temp = NULL;
  h_disp = h_refresh = h_brightness = NULL;
  acc_sysdiag_teardown ();
}

/* --------------------------------------------------------------- router */
const gchar *
acc_hubs_parent (const gchar *page)
{
  if (!page)
    return NULL;
  if (g_strcmp0 (page, "battery") == 0 || g_strcmp0 (page, "gpu") == 0
      || g_strcmp0 (page, "display") == 0
      || g_strcmp0 (page, "performance") == 0)
    return "customization";
  if (g_strcmp0 (page, "system") == 0)
    return "hardware";
  return NULL;
}

gboolean
acc_hubs_select (const gchar *page)
{
  if (!s_custom_stack)
    return FALSE;
  for (guint i = 0; i < G_N_ELEMENTS (s_custom_tabs); i++)
    {
      GtkWidget *btn = s_custom_tabs[i];
      if (!btn)
        continue;
      const gchar *name = NULL;
      switch (i)
        {
        case 0: name = "performance"; break;
        case 1: name = "battery"; break;
        case 2: name = "gpu"; break;
        default: name = "display"; break;
        }
      if (g_strcmp0 (page, name) == 0)
        {
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), TRUE);
          gtk_stack_set_visible_child_name (s_custom_stack, name);
          return TRUE;
        }
    }
  return FALSE;
}
