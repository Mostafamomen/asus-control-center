/* ui/pages.c - the seven feature pages with real controls (M2)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Buttons call the polkit helper via control/ctl.c; every write shows a
 * toast (Applied / Failed-denied) in the current language. All sysfs
 * states are re-read every refresh and after every user action.
 */
#include "pages.h"
#include "theme.h"
#include "mcui.h"
#include "specs.h"
#include "../i18n/i18n.h"
#include "../control/backlight.h"
#include "../control/brightness.h"
#include "../control/ctl.h"
#include "../control/asusd.h"
#include "../control/gfx.h"
#include "../control/softdim.h"
#include "../probe/probe.h"
#include "../probe/devs.h"
#include "../probe/battery.h"
#include "../probe/dmi.h"
#include "../probe/hwmon.h"
#include "../probe/gpu.h"
#include "../probe/display.h"
#include "../probe/sysinfo.h"
#include "../probe/load.h"
#include <adwaita.h>
#include <gio/gio.h>
#include <string.h>

/* ------------------------------------------------ module state ------- */
static gboolean s_sync = FALSE;      /* refresh guard for callbacks     */

/* battery page */
static AccRing   *f_bat_ring;
static GtkWidget *f_bat_status, *f_bat_whnow, *f_bat_whf, *f_bat_cyc;
static GtkWidget *f_bat_health;
static GtkWidget *f_lim_full, *f_lim_bal, *f_lim_life;
static GtkWidget *f_bat_limit, *f_bat_pol;
/* gpu page */
static GtkWidget *f_gpu_name, *f_gpu_bar, *f_gpu_util, *f_gpu_temp;
static GtkWidget *f_gpu_drv, *f_gpu_now, *f_gpu_vram, *f_gpu_power, *f_gpu_out;
static GtkWidget *f_gpu_notice, *f_gpu_notice_t, *f_gpu_notice_b;
static GtkWidget *f_gpu_cancel;    /* drop a queued switch */
static GtkWidget *f_gpu_pill;      /* live / queued status */
static GtkWidget *f_gpu_spin;      /* busy indicator */
static gint       s_gpu_jobs = 0;  /* in-flight MUX jobs (0 = idle)   */
static GtkWidget *f_gpu_hybrid, *f_gpu_ult, *f_gpu_eco;
static GtkWidget *f_gpu_eco_note;  /* why iGPU-only is (not) available */
static gboolean   s_gpu_can_hybrid = TRUE, s_gpu_can_ultimate = TRUE;
static gboolean   s_gpu_can_eco = FALSE;
static AccGfxMode s_gfx_mode = ACC_GFX_UNKNOWN; /* supergfxd live mode */
static AccGfxMode s_gfx_pending = ACC_GFX_UNKNOWN; /* waits for logout */
/* display page */
static GtkWidget *f_dsp_res, *f_dsp_slider, *f_dsp_pct;
static GtkWidget *f_bl_slider, *f_bl_pct;
static gint64      s_dsp_touch = 0;   /* ms of last user slider drag    */
static gint64      s_bl_touch = 0;    /* same, for the backlight slider  */
/* perf page */
static AccRing   *f_perf_ring;
static GtkWidget *f_fan_cpu, *f_fan_gpu, *f_perf_temp;
static GtkWidget *f_perf_q, *f_perf_b, *f_perf_p, *f_kbd_cur, *f_kbd[4];
static GtkWidget *f_perf_spin, *f_perf_act, *f_perf_ac, *f_perf_bat;
static GtkWidget *f_perf_note;
static gboolean   s_perf_can_q = TRUE, s_perf_can_b = TRUE, s_perf_can_p = TRUE;
static gint       s_perf_jobs = 0;   /* in-flight profile jobs          */
/* CPU priority selector on the GPU page: the very same firmware profile,
 * so both pages are driven by one state and one job counter. */
static GtkWidget *f_cpu_q, *f_cpu_b, *f_cpu_p;
static GtkWidget *f_cpu_spin, *f_cpu_act;

/* defined with the perf page below, used by the GPU page's CPU selector */
static void prof_caps_update (void);
static void perf_cb (GtkCheckButton *b, gpointer idx);
/* system page */
static GtkWidget *f_sys_os, *f_sys_cpu, *f_sys_ram, *f_sys_ssd,
                 *f_sys_bios, *f_sys_kernel;
/* system page — connectivity card (read-only live values) */
static GtkWidget *f_conn_wifi, *f_conn_wifi_sub, *f_conn_bt;
/* settings page */
static GtkWidget *f_helper;
/* support page */
static GtkWidget *f_diag_res;

/* ------------------------------------------------ helpers ------------
 * Thin local aliases over the reusable MyASUS UI kit (ui/mcui.h) — the
 * library owns the widget construction and the CSS-class contract. */
static GtkWidget *
w_lbl (const gchar *text, const gchar *cls, int xalign)
{
  return mc_label (text, cls, xalign);
}

static GtkWidget *
w_row (const gchar *title, GtkWidget **out_val)
{
  return mc_row (title, out_val);
}

static GtkWidget *
w_picard (const gchar *title)
{
  return mc_panel (title);
}

/* --- MyASUS-style 3-column setting block: icon | text | controls ------- */
static GtkWidget *
w_feat (const gchar *icon, const gchar *title, const gchar *desc,
        GtkWidget **out_ctl)
{
  return mc_feature (icon, title, desc, out_ctl);
}

/* --- GSettings bridges (GNOME desktop settings, no root) -------------- */
static GSettings *
gs_open (const gchar *schema)
{
  GSettingsSchemaSource *src = g_settings_schema_source_get_default ();
  if (!src)
    return NULL;
  GSettingsSchema *s = g_settings_schema_source_lookup (src, schema, TRUE);
  if (!s)
    return NULL;
  GSettings *st = g_settings_new_full (s, NULL, NULL);
  g_settings_schema_unref (s);
  return st;
}

static gboolean
gs_bool (const gchar *schema, const gchar *key, gboolean def)
{
  GSettings *s = gs_open (schema);
  if (!s)
    return def;
  gboolean v = g_settings_get_boolean (s, key);
  g_object_unref (s);
  return v;
}

static void
gs_set_bool (const gchar *schema, const gchar *key, gboolean v)
{
  GSettings *s = gs_open (schema);
  if (!s)
    return;
  g_settings_set_boolean (s, key, v);
  g_object_unref (s);
}

static gchar *
gs_str (const gchar *schema, const gchar *key)
{
  GSettings *s = gs_open (schema);
  if (!s)
    return NULL;
  gchar *v = g_settings_get_string (s, key);
  g_object_unref (s);
  return v;
}

static void
gs_set_str (const gchar *schema, const gchar *key, const gchar *v)
{
  GSettings *s = gs_open (schema);
  if (!s)
    return;
  g_settings_set_string (s, key, v);
  g_object_unref (s);
}

/* --- read-only connectivity probes (kernel + iw, no root) ------------- */
static gchar *
wifi_iface (void)
{
  GDir *d = g_dir_open ("/sys/class/net", 0, NULL);
  if (!d)
    return NULL;
  gchar *found = NULL;
  const gchar *n;
  while (!found && (n = g_dir_read_name (d)))
    {
      gchar *p = g_build_filename ("/sys/class/net", n, "wireless", NULL);
      if (g_file_test (p, G_FILE_TEST_IS_DIR))
        found = g_strdup (n);
      g_free (p);
    }
  g_dir_close (d);
  return found;
}

static gchar *
wifi_ssid (const gchar *iface)
{
  if (!iface)
    return NULL;
  gchar *out = NULL;
  gchar *argv[] = { (gchar *) "iw", (gchar *) "dev", (gchar *) iface,
                    (gchar *) "link", NULL };
  if (g_spawn_sync (NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
                    &out, NULL, NULL, NULL) && out)
    {
      gchar *s = strstr (out, "SSID: ");
      if (s)
        {
          s += 6;
          gchar *e = strchr (s, '\n');
          if (e)
            *e = '\0';
          gchar *r = g_strdup (s);
          g_free (out);
          return r;
        }
      g_free (out);
    }
  return NULL;
}

static gchar *
bt_radio (void)
/* "On"/"Off" from rfkill; NULL when the machine has no Bluetooth radio */
{
  GDir *d = g_dir_open ("/sys/class/rfkill", 0, NULL);
  if (!d)
    return NULL;
  gchar *res = NULL;
  const gchar *n;
  while ((n = g_dir_read_name (d)))
    {
      gchar *tp = g_build_filename ("/sys/class/rfkill", n, "type", NULL);
      gchar *type = NULL;
      if (g_file_get_contents (tp, &type, NULL, NULL))
        {
          g_strstrip (type);
          if (g_strcmp0 (type, "bluetooth") == 0)
            {
              gchar *sp = g_build_filename ("/sys/class/rfkill", n,
                                            "soft", NULL);
              gchar *hp = g_build_filename ("/sys/class/rfkill", n,
                                            "hard", NULL);
              gchar *sv = NULL, *hv = NULL;
              gboolean on = g_file_get_contents (sp, &sv, NULL, NULL)
                            && g_strcmp0 (g_strstrip (sv), "0") == 0;
              if (on && g_file_get_contents (hp, &hv, NULL, NULL))
                on = g_strcmp0 (g_strstrip (hv), "0") == 0;
              g_free (sv);
              g_free (hv);
              g_free (sp);
              g_free (hp);
              res = g_strdup (on ? "on" : "off");
            }
          g_free (type);
        }
      g_free (tp);
      if (res)
        break;
    }
  g_dir_close (d);
  return res;
}

/* MyASUS pages open with an uppercase group label above the cards */
static GtkWidget *
w_page_titled (GtkWidget *child, AccStr group)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_box_append (GTK_BOX (box), mc_group (acc_tr (group)));
  gtk_box_append (GTK_BOX (box), child);
  return mc_page (box);
}

static void
w_done (gboolean ok)
{
  acc_ui_toast (acc_tr (ok ? ACC_T_WROK : ACC_T_WRERR));
}

/* --- busy indicator for the async apply jobs -------------------------
 * The hardware write runs on a short g_timeout so GTK can paint this
 * spinner first; the job stops it when the service answered. */
static void
w_busy (GtkWidget *spin, gboolean on)
{
  if (!spin)
    return;
  if (on)
    gtk_spinner_start (GTK_SPINNER (spin));
  else
    gtk_spinner_stop (GTK_SPINNER (spin));
  gtk_widget_set_visible (spin, on);
}

static GtkWidget *
w_spin (void)
{
  GtkWidget *s = gtk_spinner_new ();
  gtk_widget_set_valign (s, GTK_ALIGN_CENTER);
  gtk_widget_set_visible (s, FALSE);
  return s;
}

/* helpers around stored ring pointers */
static void
w_ring_big (AccRing *r, const gchar *s)
{
  if (r)
    acc_ring_set_big (r, s);
}

static void
w_ring_val (AccRing *r, gdouble v)
{
  if (r)
    acc_ring_set_value (r, v);
}

/* touchpad on/off — GNOME desktop setting, no root needed */
static void
touchpad_cb (GtkSwitch *sw, GParamSpec *ps, gpointer u)
{
  (void) ps;
  (void) u;
  gs_set_str ("org.gnome.desktop.peripherals.touchpad", "send-events",
              gtk_switch_get_active (sw) ? "enabled" : "disabled");
}

/* eye-care toggle — maps to GNOME's night-light (blue-light filter) */
static void
eyecare_cb (GtkSwitch *sw, GParamSpec *ps, gpointer u)
{
  (void) ps;
  (void) u;
  gs_set_bool ("org.gnome.settings-daemon.plugins.color",
               "night-light-enabled", gtk_switch_get_active (sw));
}

/* E-Reading: render the whole app window in grayscale (MyASUS Splendid
 * "E-Reading" mode) and remember the choice across launches. */
static void
eread_cb (GtkSwitch *sw, GParamSpec *ps, gpointer u)
{
  (void) ps;
  (void) u;
  gboolean on = gtk_switch_get_active (sw);
  mc_gray_set (on);
  acc_theme_save_gray (on);
}

/* ------------------------------------------------ battery page ------- */
static void
limit_cb (GtkCheckButton *b, gpointer pct){
  if (s_sync || !gtk_check_button_get_active (b))
    return;
  w_done (acc_ctl_set_charge ((int) (gintptr) pct));
  acc_page_refresh (NULL);
}

GtkWidget *
acc_page_bat (AccUI *u)
{
  (void) u;
  GtkWidget *c = w_picard (acc_tr (ACC_T_PG_BAT));
  GtkWidget *li = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);

  GtkWidget *top = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  f_bat_ring = ACC_RING (acc_ring_new (acc_theme_col_accent ()));
  gtk_widget_set_size_request (GTK_WIDGET (f_bat_ring), 108, 108);
  gtk_box_append (GTK_BOX (top), GTK_WIDGET (f_bat_ring));

  GtkWidget *right = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_valign (right, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (right), w_row (acc_tr (ACC_T_STATUS), &f_bat_status));
  gtk_box_append (GTK_BOX (right), w_row (acc_tr (ACC_T_BAT_NOW), &f_bat_whnow));
  gtk_box_append (GTK_BOX (right), w_row (acc_tr (ACC_T_BAT_FULL), &f_bat_whf));
  gtk_box_append (GTK_BOX (right), w_row (acc_tr (ACC_T_BAT_CYCLES), &f_bat_cyc));
  gtk_box_append (GTK_BOX (right),
                  w_row (acc_tr (ACC_T_BAT_HEALTH), &f_bat_health));
  gtk_box_append (GTK_BOX (top), right);
  gtk_box_append (GTK_BOX (li), top);

  /* Battery-Care-style block: icon + description left, controls right */
  GtkWidget *care = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
  {
    GtkWidget *cim = gtk_image_new_from_icon_name ("battery-symbolic");
    gtk_image_set_pixel_size (GTK_IMAGE (cim), 40);
    gtk_widget_add_css_class (cim, "cardicon");
    gtk_widget_set_valign (cim, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (care), cim);
  }
  {
    GtkWidget *cd = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append (GTK_BOX (cd), w_lbl (acc_tr (ACC_T_CARE), "cardtitle", 0));
    GtkWidget *csub = w_lbl (acc_tr (ACC_T_CARE_SUB), "foot", 0);
    gtk_label_set_wrap (GTK_LABEL (csub), TRUE);
    gtk_label_set_width_chars (GTK_LABEL (csub), 24);
    gtk_box_append (GTK_BOX (cd), csub);
    gtk_widget_set_valign (cd, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (care), cd);
  }
  {
    GtkWidget *ctl = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_hexpand (ctl, TRUE);
    gtk_box_append (GTK_BOX (ctl), w_lbl (acc_tr (ACC_T_LIMIT_SET), "dim", 0));
    GtkWidget *limrow = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_set_homogeneous (GTK_BOX (limrow), TRUE);
    f_lim_life = gtk_check_button_new_with_label (acc_tr (ACC_T_LIMIT_LIFE));
    f_lim_bal  = gtk_check_button_new_with_label (acc_tr (ACC_T_LIMIT_BAL));
    f_lim_full = gtk_check_button_new_with_label (acc_tr (ACC_T_LIMIT_FULL));
    gtk_check_button_set_group (GTK_CHECK_BUTTON (f_lim_bal),
                                GTK_CHECK_BUTTON (f_lim_life));
    gtk_check_button_set_group (GTK_CHECK_BUTTON (f_lim_full),
                                GTK_CHECK_BUTTON (f_lim_life));
    g_signal_connect (f_lim_life, "toggled", G_CALLBACK (limit_cb),
                      (gpointer) (gintptr) 60);
    g_signal_connect (f_lim_bal,  "toggled", G_CALLBACK (limit_cb),
                      (gpointer) (gintptr) 80);
    g_signal_connect (f_lim_full, "toggled", G_CALLBACK (limit_cb),
                      (gpointer) (gintptr) 100);
    gtk_widget_add_css_class (f_lim_life, "optcard");
    gtk_widget_add_css_class (f_lim_bal,  "optcard");
    gtk_widget_add_css_class (f_lim_full, "optcard");
    gtk_box_append (GTK_BOX (limrow), f_lim_life);
    gtk_box_append (GTK_BOX (limrow), f_lim_bal);
    gtk_box_append (GTK_BOX (limrow), f_lim_full);
    gtk_widget_set_valign (f_lim_life, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (f_lim_bal,  GTK_ALIGN_CENTER);
    gtk_widget_set_valign (f_lim_full, GTK_ALIGN_CENTER);
    /* gate on the EC file (name varies by kernel): no file → no write,
     * so no clickable card */
    gchar *limf = acc_dev_charge_limit_file ();
    if (!limf)
      {
        gtk_widget_set_sensitive (f_lim_life, FALSE);
        gtk_widget_set_sensitive (f_lim_bal,  FALSE);
        gtk_widget_set_sensitive (f_lim_full, FALSE);
      }
    g_free (limf);
    gtk_box_append (GTK_BOX (ctl), limrow);
    gtk_box_append (GTK_BOX (ctl), w_row (acc_tr (ACC_T_LIMIT_CUR), &f_bat_limit));
    gtk_box_append (GTK_BOX (ctl), w_row (acc_tr (ACC_T_CHG_POL), &f_bat_pol));
    gtk_box_append (GTK_BOX (care), ctl);
  }
  gtk_box_append (GTK_BOX (li), care);

  gtk_box_append (GTK_BOX (li), w_lbl (acc_tr (ACC_T_LIMIT_HINT), "foot", 0));
  gtk_box_append (GTK_BOX (c), li);
  return w_page_titled (c, ACC_T_NAV_FANS);
}

/* ------------------------------------------------ gpu page ----------- */

/* Hybrid/Ultimate move the firmware MUX. iGPU only (Eco) has no firmware
 * switch here (no dgpu_disable attribute), so it goes through supergfxd,
 * which removes the dGPU from the PCI bus and finishes at logout. It is
 * offered only when supergfxd is running, lists Integrated, and the MUX
 * is in Hybrid. */
static void
gpu_radios_sensitive (gboolean on)
{
  if (f_gpu_hybrid)
    gtk_widget_set_sensitive (f_gpu_hybrid, on && s_gpu_can_hybrid);
  if (f_gpu_ult)
    gtk_widget_set_sensitive (f_gpu_ult, on && s_gpu_can_ultimate);
  if (f_gpu_eco)
    gtk_widget_set_sensitive (f_gpu_eco, on && s_gpu_can_eco);
}

/* job modes: the two MUX values, then the two supergfxd requests */
enum { GPU_JOB_ULT = 0, GPU_JOB_HYBRID = 1,
       GPU_JOB_GFX_INTEGRATED = 2, GPU_JOB_GFX_HYBRID = 3 };

typedef struct { gint mode; } GpuJob;

/* One place that ends a MUX job: clear the spinner, unlock the radios and
 * re-read the hardware. Refreshes also consult s_gpu_jobs, so a stale
 * frame can never leave the page "busy" or dimmed after the job is done. */
static void
gpu_job_end (void)
{
  if (s_gpu_jobs > 0)
    s_gpu_jobs--;
  if (s_gpu_jobs == 0)
    {
      w_busy (f_gpu_spin, FALSE);
      gpu_radios_sensitive (TRUE);
    }
  acc_page_refresh (NULL);
}

/* supergfxd answered (worker thread done): report, then unlock the page */
static void
gfx_done_cb (GObject *src, GAsyncResult *res, gpointer data)
{
  (void) src;
  (void) data;
  AccGfxAction act = ACC_GFX_ACT_NONE;
  gchar *err = NULL;
  if (acc_gfx_set_mode_finish (res, &act, &err))
    acc_ui_toast (acc_tr (act == ACC_GFX_ACT_LOGOUT ? ACC_T_GFX_LOGOUT
                          : act == ACC_GFX_ACT_REBOOT ? ACC_T_GFX_REBOOT
                          : ACC_T_WROK));
  else
    {
      g_printerr ("supergfxctl: %s\n", err ? err : "?");
      acc_ui_toast (acc_tr (ACC_T_ERR_SERVICE));
    }
  g_free (err);
  gpu_job_end ();
}

static gboolean
gpu_apply_job (gpointer data)
{
  GpuJob *j = (GpuJob *) data;
  gint mode = j->mode;
  gchar *svc_err = NULL;
  gboolean ok = FALSE;
  gboolean queued = FALSE;

  /* supergfxd request: no firmware write, the daemon finishes it at
   * logout (or reboot). It runs in a worker thread because supergfxd can
   * take seconds to answer; gfx_done_cb ends the job. */
  if (mode == GPU_JOB_GFX_INTEGRATED || mode == GPU_JOB_GFX_HYBRID)
    {
      acc_gfx_set_mode_async (mode == GPU_JOB_GFX_INTEGRATED
                              ? ACC_GFX_INTEGRATED : ACC_GFX_HYBRID,
                              gfx_done_cb, NULL);
      g_free (j);
      return G_SOURCE_REMOVE;
    }

  /* 1) the firmware write itself — the same value MyASUS writes, through
   *    the whitelisted polkit helper. */
  if (acc_ctl_available () && !acc_ctl_helper_stale ())
    ok = acc_ctl_set_gpu_mux (mode);

  /* 2) When the EC refuses it, asusd can queue the value for shutdown:
   *    asus-shutdown waits for the dGPU to go idle, unloads the NVIDIA
   *    stack and only then writes the MUX. That is the one sequence this
   *    firmware accepts, so use it instead of pretending the switch
   *    failed. Never queue on battery — the EC refuses it there too. */
  if (!ok)
    {
      gint c = acc_ctl_last_exit ();
      if (c == 127)                       /* polkit prompt dismissed */
        {
          acc_ui_toast (acc_tr (ACC_T_ERR_AUTH));
          goto out;
        }
      if (!acc_dev_on_ac ())
        {
          acc_ui_toast (acc_tr (ACC_T_GPU_AC));
          goto out;
        }
      if (acc_asusd_available () && acc_asusd_set_gpu_mux (mode, &svc_err))
        {
          ok = TRUE;
          queued = TRUE;
        }
    }

  if (ok)
    acc_ui_toast (acc_tr (queued ? ACC_T_GPU_QUEUED : ACC_T_WROK));
  else if (svc_err)
    acc_ui_toast (acc_tr (ACC_T_ERR_SERVICE));
  else
    acc_ui_toast (acc_tr (ACC_T_ERR_FW));

out:
  g_free (svc_err);
  gpu_job_end ();
  g_free (j);
  return G_SOURCE_REMOVE;
}

static void
gpu_start_job (gint mode)
{
  s_gpu_jobs++;
  gpu_radios_sensitive (FALSE);
  w_busy (f_gpu_spin, TRUE);
  GpuJob *j = g_new0 (GpuJob, 1);
  j->mode = mode;
  g_timeout_add (80, gpu_apply_job, j);
}

/* asusd has no "clear the queue" call. Queueing the mode that is already
 * live makes the shutdown write a no-op, which is the same outcome and
 * keeps the UI honest: QueuedGpuValue == CurrentValue again. */
static gboolean
gpu_cancel_job (gpointer data)
{
  (void) data;
  AsusdGpuState gs;
  gboolean ok = FALSE;

  if (acc_asusd_gpu (&gs) && gs.current >= 0)
    ok = acc_asusd_set_gpu_mux (gs.current, NULL);

  acc_ui_toast (acc_tr (ok ? ACC_T_GPU_CANCEL_OK : ACC_T_ERR_SERVICE));
  gpu_job_end ();
  return G_SOURCE_REMOVE;
}

static void
gpu_cancel_cb (GtkButton *b, gpointer data)
{
  (void) b;
  (void) data;
  s_gpu_jobs++;
  gpu_radios_sensitive (FALSE);
  w_busy (f_gpu_spin, TRUE);
  g_timeout_add (80, gpu_cancel_job, NULL);
}

/* Ultimate rewires the internal panel and iGPU only removes the dGPU:
 * ask before either. data carries the job mode. */
static void
gpu_confirm_cb (GObject *src, GAsyncResult *res, gpointer data)
{
  GError *e = NULL;
  int choice = gtk_alert_dialog_choose_finish (GTK_ALERT_DIALOG (src),
                                               res, &e);
  if (e || choice != 0)
    {
      g_clear_error (&e);
      acc_page_refresh (NULL);   /* snap the radio back to the real mode */
      return;
    }
  gpu_start_job (GPOINTER_TO_INT (data));
}

static void
gpu_mode_cb (GtkCheckButton *b, gpointer data)
{
  if (s_sync || !gtk_check_button_get_active (b))
    return;

  gint mode = GPOINTER_TO_INT (data);
  gboolean in_igpu = s_gfx_mode == ACC_GFX_INTEGRATED;

  /* leaving iGPU only (or dropping a request for it that still waits for
   * logout): supergfxd brings the dGPU back, no MUX write */
  if (mode == GPU_JOB_HYBRID && s_gfx_pending == ACC_GFX_INTEGRATED)
    in_igpu = TRUE;
  if (mode == GPU_JOB_HYBRID && in_igpu)
    {
      gpu_start_job (GPU_JOB_GFX_HYBRID);
      return;
    }
  /* Ultimate from iGPU only would wire the panel to a removed GPU */
  if (mode == GPU_JOB_ULT && in_igpu)
    {
      acc_ui_toast (acc_tr (ACC_T_GFX_HYBRID_FIRST));
      acc_page_refresh (NULL);
      return;
    }

  /* an old helper writes the dead legacy node: say so, do not fail mute
   * (MUX writes only; iGPU only goes through supergfxd, not the helper) */
  if (mode <= GPU_JOB_HYBRID && acc_ctl_helper_stale ())
    {
      acc_ui_toast (acc_tr (ACC_T_HELPER_STALE));
      acc_page_refresh (NULL);
      return;
    }

  if (mode == GPU_JOB_HYBRID)    /* the safe direction, no dialog */
    {
      gpu_start_job (GPU_JOB_HYBRID);
      return;
    }

  gboolean eco = mode == GPU_JOB_GFX_INTEGRATED;
  GtkAlertDialog *d = gtk_alert_dialog_new
                        (acc_tr (eco ? ACC_T_GFX_T : ACC_T_GPU_SWITCH_T));
  const gchar *btns[] = { acc_tr (ACC_T_SWITCH),
                          acc_tr (ACC_T_CANCEL), NULL };
  gtk_alert_dialog_set_buttons (d, btns);
  gtk_alert_dialog_set_cancel_button (d, 1);
  gtk_alert_dialog_set_default_button (d, 1);
  gtk_alert_dialog_set_detail (d, acc_tr (eco ? ACC_T_GFX_B
                                              : ACC_T_GPU_SWITCH_B));
  gtk_alert_dialog_choose (d, GTK_WINDOW (acc_ui_root ()), NULL,
                           gpu_confirm_cb, GINT_TO_POINTER (mode));
  g_object_unref (d);
}

GtkWidget *
acc_page_gpu (AccUI *u)
{
  (void) u;
  GtkWidget *c = w_picard (acc_tr (ACC_T_PG_GPU));
  GtkWidget *li = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);

  /* GPU-mode block: icon + description left, live state + controls right */
  GtkWidget *gr0 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
  {
    GtkWidget *cim = gtk_image_new_from_icon_name
      ("video-display-symbolic");
    gtk_image_set_pixel_size (GTK_IMAGE (cim), 40);
    gtk_widget_add_css_class (cim, "cardicon");
    gtk_widget_set_valign (cim, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (gr0), cim);
  }
  {
    GtkWidget *cd = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append (GTK_BOX (cd),
                    w_lbl (acc_tr (ACC_T_GPU_MODE), "cardtitle", 0));
    GtkWidget *csub = w_lbl (acc_tr (ACC_T_GPU_HINT), "foot", 0);
    gtk_label_set_wrap (GTK_LABEL (csub), TRUE);
    gtk_label_set_width_chars (GTK_LABEL (csub), 24);
    gtk_box_append (GTK_BOX (cd), csub);
    GtkWidget *note = w_lbl (acc_tr (ACC_T_GPU_NS_HINT), "foot", 0);
    gtk_label_set_wrap (GTK_LABEL (note), TRUE);
    gtk_label_set_width_chars (GTK_LABEL (note), 24);
    gtk_box_append (GTK_BOX (cd), note);
    gtk_widget_set_valign (cd, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (gr0), cd);
  }
  {
    GtkWidget *ctl = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_hexpand (ctl, TRUE);

    /* hardware name + live/queued pill + busy indicator */
    GtkWidget *nrow = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    f_gpu_name = w_lbl ("—", "cardtitle", 0);
    gtk_widget_set_hexpand (f_gpu_name, TRUE);
    f_gpu_pill = w_lbl ("", "pill", 0);
    gtk_widget_set_visible (f_gpu_pill, FALSE);
    f_gpu_spin = w_spin ();
    gtk_box_append (GTK_BOX (nrow), f_gpu_name);
    gtk_box_append (GTK_BOX (nrow), f_gpu_pill);
    gtk_box_append (GTK_BOX (nrow), f_gpu_spin);
    gtk_box_append (GTK_BOX (ctl), nrow);

    gtk_box_append (GTK_BOX (ctl),
                    w_row (acc_tr (ACC_T_GPU_DRV), &f_gpu_drv));
    gtk_box_append (GTK_BOX (ctl),
                    w_row (acc_tr (ACC_T_GPU_NOW), &f_gpu_now));

    f_gpu_hybrid = gtk_check_button_new_with_label (acc_tr (ACC_T_GPU_HYBRID));
    f_gpu_ult    = gtk_check_button_new_with_label (acc_tr (ACC_T_GPU_ULT));
    f_gpu_eco    = gtk_check_button_new_with_label (acc_tr (ACC_T_GPU_ECO));
    gtk_check_button_set_group (GTK_CHECK_BUTTON (f_gpu_ult),
                                GTK_CHECK_BUTTON (f_gpu_hybrid));
    gtk_check_button_set_group (GTK_CHECK_BUTTON (f_gpu_eco),
                                GTK_CHECK_BUTTON (f_gpu_hybrid));
    g_signal_connect (f_gpu_hybrid, "toggled", G_CALLBACK (gpu_mode_cb),
                      GINT_TO_POINTER (1));
    g_signal_connect (f_gpu_ult,    "toggled", G_CALLBACK (gpu_mode_cb),
                      GINT_TO_POINTER (GPU_JOB_ULT));
    g_signal_connect (f_gpu_eco,    "toggled", G_CALLBACK (gpu_mode_cb),
                      GINT_TO_POINTER (GPU_JOB_GFX_INTEGRATED));
    GtkWidget *gr = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_set_homogeneous (GTK_BOX (gr), TRUE);
    gtk_widget_add_css_class (f_gpu_hybrid, "optcard");
    gtk_widget_add_css_class (f_gpu_ult,    "optcard");
    gtk_widget_add_css_class (f_gpu_eco,    "optcard");
    gtk_widget_set_valign (f_gpu_hybrid, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (f_gpu_ult,    GTK_ALIGN_CENTER);
    gtk_widget_set_valign (f_gpu_eco,    GTK_ALIGN_CENTER);
    gtk_box_append (GTK_BOX (gr), f_gpu_hybrid);
    gtk_box_append (GTK_BOX (gr), f_gpu_ult);
    gtk_box_append (GTK_BOX (gr), f_gpu_eco);

    /* no MUX node → the two real modes cannot work, so show them disabled */
    gchar *muxf = acc_dev_wmi_file ("gpu_mux_mode");
    s_gpu_can_hybrid = s_gpu_can_ultimate = (muxf != NULL);
    g_free (muxf);
    gtk_box_append (GTK_BOX (ctl), gr);

    /* the refresh fills in why iGPU only is (not) available */
    f_gpu_eco_note = w_lbl (acc_tr (ACC_T_GFX_HINT), "foot", 0);
    gtk_label_set_wrap (GTK_LABEL (f_gpu_eco_note), TRUE);
    gtk_label_set_width_chars (GTK_LABEL (f_gpu_eco_note), 30);
    gtk_box_append (GTK_BOX (ctl), f_gpu_eco_note);

    /* CPU priority: exactly the same three firmware profiles as the
     * Power & Performance page, beside the MUX switch. One live state,
     * one job counter, so changing it on either page updates both. */
    prof_caps_update ();
    {
      GtkWidget *pr = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
      gtk_widget_add_css_class (pr, "prio");
      gtk_box_append (GTK_BOX (pr),
                      w_lbl (acc_tr (ACC_T_CPU_PRIO), "dim", 0));
      f_cpu_act = w_lbl ("—", "val", 1);
      gtk_box_append (GTK_BOX (pr), f_cpu_act);
      f_cpu_spin = w_spin ();
      gtk_box_append (GTK_BOX (pr), f_cpu_spin);
      gtk_box_append (GTK_BOX (pr), mc_spacer ());

      f_cpu_q = gtk_check_button_new_with_label (acc_tr (ACC_T_PERF_QUIET));
      f_cpu_b = gtk_check_button_new_with_label (acc_tr (ACC_T_PERF_BAL));
      f_cpu_p = gtk_check_button_new_with_label (acc_tr (ACC_T_PERF_BOOST));
      gtk_check_button_set_group (GTK_CHECK_BUTTON (f_cpu_b),
                                  GTK_CHECK_BUTTON (f_cpu_q));
      gtk_check_button_set_group (GTK_CHECK_BUTTON (f_cpu_p),
                                  GTK_CHECK_BUTTON (f_cpu_q));
      g_signal_connect (f_cpu_q, "toggled", G_CALLBACK (perf_cb),
                        GINT_TO_POINTER (ACC_PROF_QUIET));
      g_signal_connect (f_cpu_b, "toggled", G_CALLBACK (perf_cb),
                        GINT_TO_POINTER (ACC_PROF_BALANCED));
      g_signal_connect (f_cpu_p, "toggled", G_CALLBACK (perf_cb),
                        GINT_TO_POINTER (ACC_PROF_PERFORMANCE));
      GtkWidget *pbtn[] = { f_cpu_q, f_cpu_b, f_cpu_p };
      for (guint i = 0; i < G_N_ELEMENTS (pbtn); i++)
        {
          gtk_widget_add_css_class (pbtn[i], "optcard");
          gtk_widget_add_css_class (pbtn[i], "compact");
          gtk_widget_set_valign (pbtn[i], GTK_ALIGN_CENTER);
          gtk_box_append (GTK_BOX (pr), pbtn[i]);
        }

      /* no advertised profile at all → hide the block instead of showing
       * three dead buttons */
      gboolean any = s_perf_can_q || s_perf_can_b || s_perf_can_p;
      gtk_widget_set_visible (pr, any);
      gtk_box_append (GTK_BOX (ctl), pr);

      GtkWidget *ph = w_lbl (acc_tr (ACC_T_CPU_PRIO_HINT), "foot", 0);
      gtk_label_set_wrap (GTK_LABEL (ph), TRUE);
      gtk_label_set_width_chars (GTK_LABEL (ph), 40);
      gtk_widget_set_visible (ph, any);
      gtk_box_append (GTK_BOX (ctl), ph);
    }

    /* queued / restart notice: never claim a mode is live before it is.
     * Blue = informational (queued, needs a restart), amber = the user has
     * to act before the change can happen. */
    f_gpu_notice = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class (f_gpu_notice, "notice");
    gtk_widget_set_visible (f_gpu_notice, FALSE);
    f_gpu_notice_t = w_lbl ("", "noticehead", 0);
    gtk_label_set_wrap (GTK_LABEL (f_gpu_notice_t), TRUE);
    gtk_label_set_width_chars (GTK_LABEL (f_gpu_notice_t), 32);
    f_gpu_notice_b = w_lbl ("", "foot", 0);
    gtk_label_set_wrap (GTK_LABEL (f_gpu_notice_b), TRUE);
    gtk_label_set_width_chars (GTK_LABEL (f_gpu_notice_b), 32);
    f_gpu_cancel = gtk_button_new_with_label (acc_tr (ACC_T_GPU_CANCEL_Q));
    gtk_widget_add_css_class (f_gpu_cancel, "actionbtn");
    gtk_widget_set_halign (f_gpu_cancel, GTK_ALIGN_START);
    gtk_widget_set_visible (f_gpu_cancel, FALSE);
    g_signal_connect (f_gpu_cancel, "clicked", G_CALLBACK (gpu_cancel_cb),
                      NULL);
    gtk_box_append (GTK_BOX (f_gpu_notice), f_gpu_notice_t);
    gtk_box_append (GTK_BOX (f_gpu_notice), f_gpu_notice_b);
    gtk_box_append (GTK_BOX (f_gpu_notice), f_gpu_cancel);
    gtk_box_append (GTK_BOX (ctl), f_gpu_notice);

    gpu_radios_sensitive (TRUE);
    gtk_box_append (GTK_BOX (gr0), ctl);
  }
  gtk_box_append (GTK_BOX (li), gr0);

  /* read-only telemetry; each row is "—" when the tool cannot read it */
  f_gpu_bar = gtk_progress_bar_new ();
  gtk_widget_set_size_request (f_gpu_bar, 240, 6);
  gtk_widget_set_halign (f_gpu_bar, GTK_ALIGN_START);
  gtk_box_append (GTK_BOX (li), w_row (acc_tr (ACC_T_GPU_UTIL), &f_gpu_util));
  gtk_box_append (GTK_BOX (li), f_gpu_bar);
  gtk_box_append (GTK_BOX (li), w_row (acc_tr (ACC_T_GPU_VRAM), &f_gpu_vram));
  gtk_box_append (GTK_BOX (li), w_row (acc_tr (ACC_T_GPU_TEMP), &f_gpu_temp));
  gtk_box_append (GTK_BOX (li), w_row (acc_tr (ACC_T_GPU_POWER), &f_gpu_power));
  gtk_box_append (GTK_BOX (li), w_row (acc_tr (ACC_T_GPU_OUT), &f_gpu_out));
  gtk_box_append (GTK_BOX (c), li);
  return w_page_titled (c, ACC_T_NAV_GPU);
}

/* ------------------------------------------------ display page ------- */
/* "Turn on / Enable OLED dimming": install or enable the bundled Soft
 * Brightness Plus for this user, then explain the logout it needs */
static void
softdim_cb (GtkButton *b, gpointer data)
{
  (void) data;
  gchar *err = NULL;
  gboolean fresh = acc_softdim_status () == ACC_SD_MISSING;
  gboolean ok = fresh ? acc_softdim_install (&err)
                      : acc_softdim_enable (&err);
  if (ok)
    {
      acc_ui_toast (acc_tr (fresh ? ACC_T_SD_DONE : ACC_T_SD_ENABLED));
      gtk_widget_set_sensitive (GTK_WIDGET (b), FALSE);
    }
  else
    {
      g_printerr ("OLED dimming: %s\n", err ? err : "?");
      acc_ui_toast (acc_tr (ACC_T_SD_FAIL));
    }
  g_free (err);
}

static void
dsp_cb (GtkRange *rng)
{
  if (s_sync)
    return;
  s_dsp_touch = g_get_monotonic_time () / 1000;
  acc_brightness_set (gtk_range_get_value (rng));
}

/* the normal hardware backlight, on its own */
static void
bl_cb (GtkRange *rng)
{
  if (s_sync)
    return;
  s_bl_touch = g_get_monotonic_time () / 1000;
  acc_backlight_set (gtk_range_get_value (rng));
}

GtkWidget *
acc_page_disp (AccUI *u)
{
  (void) u;
  GtkWidget *c = w_picard (acc_tr (ACC_T_PG_DISP));
  GtkWidget *li = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);

  GtkWidget *block = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
  {
    GtkWidget *cim = gtk_image_new_from_icon_name
      ("display-brightness-symbolic");
    gtk_image_set_pixel_size (GTK_IMAGE (cim), 40);
    gtk_widget_add_css_class (cim, "cardicon");
    gtk_widget_set_valign (cim, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (block), cim);
  }
  {
    GtkWidget *cd = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append (GTK_BOX (cd),
                    w_lbl (acc_tr (ACC_T_PG_DISP), "cardtitle", 0));
    GtkWidget *csub = w_lbl (acc_tr (ACC_T_SOFTDIM_SUB), "foot", 0);
    gtk_label_set_wrap (GTK_LABEL (csub), TRUE);
    gtk_label_set_width_chars (GTK_LABEL (csub), 24);
    gtk_box_append (GTK_BOX (cd), csub);
    gtk_widget_set_valign (cd, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (block), cd);
  }
  {
    GtkWidget *ctl = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_hexpand (ctl, TRUE);
    gtk_box_append (GTK_BOX (ctl),
                    w_row (acc_tr (ACC_T_DISPLAY_SET), &f_dsp_res));

    /* normal backlight first, then the OLED dimming below it */
    GtkWidget *bl = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    /* both slider labels share one width so the two tracks line up */
    GtkWidget *bl_l = w_lbl (acc_tr (ACC_T_HWRO), "dim", 0);
    gtk_label_set_width_chars (GTK_LABEL (bl_l), 16);
    gtk_box_append (GTK_BOX (bl), bl_l);
    f_bl_slider = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL,
                                            (gdouble) 1, (gdouble) 100, 1);
    gtk_scale_set_draw_value (GTK_SCALE (f_bl_slider), FALSE);
    gtk_widget_set_hexpand (f_bl_slider, TRUE);
    f_bl_pct = w_lbl ("—", "val", 1);
    gtk_box_append (GTK_BOX (bl), f_bl_slider);
    gtk_box_append (GTK_BOX (bl), f_bl_pct);
    gtk_box_append (GTK_BOX (ctl), bl);
    gdouble blv = acc_backlight_get ();
    if (blv >= 0)
      {
        gtk_range_set_value (GTK_RANGE (f_bl_slider), blv);
        g_signal_connect (f_bl_slider, "value-changed",
                          G_CALLBACK (bl_cb), NULL);
      }
    else
      gtk_widget_set_sensitive (f_bl_slider, FALSE);

    GtkWidget *dim = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *dim_l = w_lbl (acc_tr (ACC_T_SOFTDIM), "dim", 0);
    gtk_label_set_width_chars (GTK_LABEL (dim_l), 16);
    gtk_box_append (GTK_BOX (dim), dim_l);
    f_dsp_slider = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL,
                                             (gdouble) 10, (gdouble) 100, 1);
    gtk_scale_set_draw_value (GTK_SCALE (f_dsp_slider), FALSE);
    gtk_widget_set_hexpand (f_dsp_slider, TRUE);
    f_dsp_pct = w_lbl ("—", "val", 1);
    gtk_box_append (GTK_BOX (dim), f_dsp_slider);
    gtk_box_append (GTK_BOX (dim), f_dsp_pct);
    gtk_box_append (GTK_BOX (ctl), dim);
    if (acc_brightness_available ())
      {
        gtk_range_set_value (GTK_RANGE (f_dsp_slider),
                             acc_brightness_get ());
        g_signal_connect (f_dsp_slider, "value-changed",
                          G_CALLBACK (dsp_cb), NULL);
      }
    else
      gtk_widget_set_sensitive (f_dsp_slider, FALSE);

    /* not installed / switched off: offer the bundled extension */
    AccSoftDim sd = acc_softdim_status ();
    if (sd != ACC_SD_ON)
      {
        gboolean can = sd == ACC_SD_DISABLED || acc_softdim_bundled ();
        GtkWidget *need = w_lbl (acc_tr (!can ? ACC_T_SOFTDIM_NEED
                                         : sd == ACC_SD_MISSING
                                         ? ACC_T_SD_HINT_MISSING
                                         : ACC_T_SD_HINT_OFF),
                                 "foot", 0);
        gtk_label_set_wrap (GTK_LABEL (need), TRUE);
        gtk_label_set_width_chars (GTK_LABEL (need), 30);
        gtk_box_append (GTK_BOX (ctl), need);
        if (can)
          {
            GtkWidget *btn = gtk_button_new_with_label
              (acc_tr (sd == ACC_SD_MISSING ? ACC_T_SD_INSTALL
                                            : ACC_T_SD_ENABLE));
            gtk_widget_add_css_class (btn, "primarybtn");
            gtk_widget_set_halign (btn, GTK_ALIGN_START);
            g_signal_connect (btn, "clicked", G_CALLBACK (softdim_cb), NULL);
            gtk_box_append (GTK_BOX (ctl), btn);
          }
      }
    gtk_box_append (GTK_BOX (block), ctl);
  }
  gtk_box_append (GTK_BOX (li), block);

  /* Eye-care block — MyASUS "Splendid / Eye Care" equivalent, mapped to
   * GNOME's night-light so the toggle does something real. */
  {
    GtkWidget *ctl;
    GtkWidget *row = w_feat ("night-light-symbolic",
                             acc_tr (ACC_T_OLED_EYE),
                             acc_tr (ACC_T_OLED_HINT), &ctl);
    GtkWidget *sr = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *fill = w_lbl (NULL, NULL, 0);
    gtk_widget_set_hexpand (fill, TRUE);
    GtkWidget *sw = gtk_switch_new ();
    gtk_switch_set_active (GTK_SWITCH (sw),
                           gs_bool ("org.gnome.settings-daemon.plugins.color",
                                    "night-light-enabled", FALSE));
    gtk_widget_set_valign (sw, GTK_ALIGN_CENTER);
    g_signal_connect (sw, "state-set", G_CALLBACK (eyecare_cb), NULL);
    gtk_box_append (GTK_BOX (sr), fill);
    gtk_box_append (GTK_BOX (sr), sw);
    gtk_box_append (GTK_BOX (ctl), sr);
    gtk_box_append (GTK_BOX (li), row);
  }

  /* E-Reading block — MyASUS Splendid's black-and-white mode */
  {
    GtkWidget *ctl;
    GtkWidget *row = w_feat ("applications-graphics-symbolic",
                             acc_tr (ACC_T_EREAD),
                             acc_tr (ACC_T_EREAD_SUB), &ctl);
    GtkWidget *sr = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append (GTK_BOX (sr), mc_spacer ());
    GtkWidget *sw = mc_switch (acc_theme_gray_pref ());
    g_signal_connect (sw, "state-set", G_CALLBACK (eread_cb), NULL);
    gtk_box_append (GTK_BOX (sr), sw);
    gtk_box_append (GTK_BOX (ctl), sr);
    gtk_box_append (GTK_BOX (li), row);
  }

  gtk_box_append (GTK_BOX (c), li);
  return w_page_titled (c, ACC_T_GRP_AV);
}

/* ------------------------------------------------ perf page ---------- */
static const gchar *
prof_label (AccProfile p)
{
  switch (p)
    {
    case ACC_PROF_QUIET:       return acc_tr (ACC_T_PERF_QUIET);
    case ACC_PROF_BALANCED:    return acc_tr (ACC_T_PERF_BAL);
    case ACC_PROF_PERFORMANCE: return acc_tr (ACC_T_PERF_BOOST);
    case ACC_PROF_LOW_POWER:   return acc_tr (ACC_T_PROF_LOWPOWER);
    default:                   return "—";
    }
}

static void
perf_lock (gboolean on)
{
  if (f_perf_q)
    gtk_widget_set_sensitive (f_perf_q, on && s_perf_can_q);
  if (f_perf_b)
    gtk_widget_set_sensitive (f_perf_b, on && s_perf_can_b);
  if (f_perf_p)
    gtk_widget_set_sensitive (f_perf_p, on && s_perf_can_p);
  if (f_cpu_q)
    gtk_widget_set_sensitive (f_cpu_q, on && s_perf_can_q);
  if (f_cpu_b)
    gtk_widget_set_sensitive (f_cpu_b, on && s_perf_can_b);
  if (f_cpu_p)
    gtk_widget_set_sensitive (f_cpu_p, on && s_perf_can_p);
}

typedef struct { AccProfile p; } ProfJob;

static gboolean
perf_apply_job (gpointer data)
{
  ProfJob *j = (ProfJob *) data;
  /* asusd must actually expose the platform interface, not merely run:
   * on other machines it may be installed for a different subsystem */
  AsusdProfileState probe;
  gboolean asusd = acc_asusd_profiles (&probe);
  gchar *err = NULL;
  gboolean ok = FALSE;

  if (asusd)
    ok = acc_asusd_set_profile (j->p, &err);
  else
    {
      const gchar *key = acc_profile_key (j->p);
      ok = key != NULL && acc_ctl_set_perf (key);
    }

  if (ok)
    acc_ui_toast (acc_tr (ACC_T_WROK));
  else if (asusd)
    acc_ui_toast (acc_tr (ACC_T_ERR_SERVICE));
  else if (acc_ctl_last_exit () == 127)
    acc_ui_toast (acc_tr (ACC_T_ERR_AUTH));
  else
    acc_ui_toast (acc_tr (ACC_T_WRERR));

  g_free (err);
  if (s_perf_jobs > 0)
    s_perf_jobs--;
  if (s_perf_jobs == 0)
    {
      w_busy (f_perf_spin, FALSE);
      w_busy (f_cpu_spin, FALSE);
      perf_lock (TRUE);
    }
  acc_page_refresh (NULL);
  g_free (j);
  return G_SOURCE_REMOVE;
}

static void
perf_cb (GtkCheckButton *b, gpointer idx)
{
  if (s_sync || !gtk_check_button_get_active (b))
    return;

  /* paint the spinner before the (fast) service round-trip */
  s_perf_jobs++;
  perf_lock (FALSE);
  w_busy (f_perf_spin, TRUE);
  w_busy (f_cpu_spin, TRUE);
  ProfJob *j = g_new0 (ProfJob, 1);
  j->p = (AccProfile) GPOINTER_TO_INT (idx);
  g_timeout_add (80, perf_apply_job, j);
}

/* Gate the three options on what this firmware/service really advertises,
 * so a profile that would do nothing is never offered as if it worked. */
static void
prof_caps_update (void)
{
  s_perf_can_q = s_perf_can_b = s_perf_can_p = FALSE;

  AsusdProfileState ps;
  if (acc_asusd_profiles (&ps) && ps.has_choices)
    {
      s_perf_can_q = ps.choice_quiet;
      s_perf_can_b = ps.choice_balanced;
      s_perf_can_p = ps.choice_performance;
      return;
    }

  gchar *ch = acc_read_trim ("/sys/firmware/acpi/platform_profile_choices");
  if (ch && *ch)
    {
      s_perf_can_q = strstr (ch, "quiet") != NULL;
      s_perf_can_b = strstr (ch, "balanced") != NULL;
      s_perf_can_p = strstr (ch, "performance") != NULL;
    }
  g_free (ch);
}

/* One reader for both pages: captions and radios follow the LIVE profile,
 * never the last request. Works whichever page is currently built. */
static void
prof_refresh (void)
{
  AsusdProfileState ps;
  gboolean asusd = acc_asusd_profiles (&ps);
  AccProfile act = ACC_PROF_UNKNOWN;

  if (asusd)
    {
      act = ps.active;
      if (f_perf_act)
        {
          gtk_label_set_text (GTK_LABEL (f_perf_act), prof_label (ps.active));
          gtk_label_set_text (GTK_LABEL (f_perf_ac), prof_label (ps.on_ac));
          gtk_label_set_text (GTK_LABEL (f_perf_bat), prof_label (ps.on_bat));
          gtk_label_set_text (GTK_LABEL (f_perf_note),
                              acc_tr ((ps.change_on_ac && ps.change_on_bat)
                                      ? ACC_T_PROF_AUTO : ACC_T_PROF_MANUAL));
        }
      if (ps.has_choices)
        {
          s_perf_can_q = ps.choice_quiet;
          s_perf_can_b = ps.choice_balanced;
          s_perf_can_p = ps.choice_performance;
        }
    }
  else
    {
      gchar *cur = acc_read_trim ("/sys/firmware/acpi/platform_profile");
      if (g_strcmp0 (cur, "quiet") == 0)
        act = ACC_PROF_QUIET;
      else if (g_strcmp0 (cur, "balanced") == 0)
        act = ACC_PROF_BALANCED;
      else if (g_strcmp0 (cur, "performance") == 0)
        act = ACC_PROF_PERFORMANCE;
      g_free (cur);

      if (f_perf_act)
        {
          gtk_label_set_text (GTK_LABEL (f_perf_act), prof_label (act));
          gtk_label_set_text (GTK_LABEL (f_perf_ac), "—");
          gtk_label_set_text (GTK_LABEL (f_perf_bat), "—");
          gtk_label_set_text (GTK_LABEL (f_perf_note),
                              acc_tr (ACC_T_PROF_SRC_SYSFS));
        }
    }

  if (f_cpu_act)
    gtk_label_set_text (GTK_LABEL (f_cpu_act), prof_label (act));

  if (f_perf_q)
    {
      gtk_check_button_set_active (GTK_CHECK_BUTTON (f_perf_q),
                                   act == ACC_PROF_QUIET);
      gtk_check_button_set_active (GTK_CHECK_BUTTON (f_perf_b),
                                   act == ACC_PROF_BALANCED);
      gtk_check_button_set_active (GTK_CHECK_BUTTON (f_perf_p),
                                   act == ACC_PROF_PERFORMANCE);
    }
  if (f_cpu_q)
    {
      gtk_check_button_set_active (GTK_CHECK_BUTTON (f_cpu_q),
                                   act == ACC_PROF_QUIET);
      gtk_check_button_set_active (GTK_CHECK_BUTTON (f_cpu_b),
                                   act == ACC_PROF_BALANCED);
      gtk_check_button_set_active (GTK_CHECK_BUTTON (f_cpu_p),
                                   act == ACC_PROF_PERFORMANCE);
    }

  if (s_perf_jobs == 0)
    {
      w_busy (f_perf_spin, FALSE);
      w_busy (f_cpu_spin, FALSE);
      perf_lock (TRUE);
    }
}

static void
kbd_cb (GtkCheckButton *b, gpointer lvl)
{
  if (s_sync || !gtk_check_button_get_active (b))
    return;
  w_done (acc_ctl_set_kbd ((int) (gintptr) lvl));
  acc_page_refresh (NULL);
}

GtkWidget *
acc_page_perf (AccUI *u)
{
  (void) u;
  GtkWidget *c = w_picard (acc_tr (ACC_T_PERF));
  GtkWidget *li = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);

  GtkWidget *top = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
  {
    GtkWidget *cim = gtk_image_new_from_icon_name
      ("applications-system-symbolic");
    gtk_image_set_pixel_size (GTK_IMAGE (cim), 40);
    gtk_widget_add_css_class (cim, "cardicon");
    gtk_widget_set_valign (cim, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (top), cim);
  }
  {
    GtkWidget *cd = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append (GTK_BOX (cd),
                    w_lbl (acc_tr (ACC_T_CARD_FANS), "cardtitle", 0));
    GtkWidget *csub = w_lbl (acc_tr (ACC_T_DIAG_SUB), "foot", 0);
    gtk_label_set_wrap (GTK_LABEL (csub), TRUE);
    gtk_label_set_width_chars (GTK_LABEL (csub), 24);
    gtk_box_append (GTK_BOX (cd), csub);
    gtk_widget_set_valign (cd, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (top), cd);
  }

  GtkWidget *readings = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 14);
  gtk_widget_set_hexpand (readings, TRUE);
  f_perf_ring = ACC_RING (acc_ring_new (acc_theme_col_warn ()));
  gtk_widget_set_size_request (GTK_WIDGET (f_perf_ring), 100, 100);
  gtk_widget_set_valign (GTK_WIDGET (f_perf_ring), GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (readings), GTK_WIDGET (f_perf_ring));

  GtkWidget *right = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_valign (right, GTK_ALIGN_CENTER);
  gtk_widget_set_hexpand (right, TRUE);
  gtk_box_append (GTK_BOX (right), w_row (acc_tr (ACC_T_FANS_CPU), &f_fan_cpu));
  gtk_box_append (GTK_BOX (right), w_row (acc_tr (ACC_T_FANS_GPU), &f_fan_gpu));
  gtk_box_append (GTK_BOX (right), w_row (acc_tr (ACC_T_TEMP), &f_perf_temp));
  gtk_box_append (GTK_BOX (readings), right);
  gtk_box_append (GTK_BOX (top), readings);
  gtk_box_append (GTK_BOX (li), top);

  /* Power-profile block: live state, configured AC/battery profiles and
   * the three profiles the firmware really advertises. asusd owns the
   * write when it is running (it also reapplies fan curves/EPP). */
  GtkWidget *sel = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
  {
    GtkWidget *cim = gtk_image_new_from_icon_name
      ("weather-windy-symbolic");
    gtk_image_set_pixel_size (GTK_IMAGE (cim), 40);
    gtk_widget_add_css_class (cim, "cardicon");
    gtk_widget_set_valign (cim, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (sel), cim);
  }
  {
    GtkWidget *cd = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append (GTK_BOX (cd),
                    w_lbl (acc_tr (ACC_T_PERF), "cardtitle", 0));
    GtkWidget *csub = w_lbl (acc_tr (ACC_T_PROF_HINT), "foot", 0);
    gtk_label_set_wrap (GTK_LABEL (csub), TRUE);
    gtk_label_set_width_chars (GTK_LABEL (csub), 24);
    gtk_box_append (GTK_BOX (cd), csub);
    GtkWidget *note = w_lbl (acc_tr (ACC_T_FAN_NOTE), "warncare", 0);
    gtk_label_set_wrap (GTK_LABEL (note), TRUE);
    gtk_label_set_width_chars (GTK_LABEL (note), 24);
    gtk_box_append (GTK_BOX (cd), note);
    gtk_widget_set_valign (cd, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (sel), cd);
  }
  GtkWidget *selctl = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_hexpand (selctl, TRUE);
  {
    /* one "Active now" row: dim caption, spinner while a job runs, value */
    GtkWidget *hd = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append (GTK_BOX (hd), w_lbl (acc_tr (ACC_T_PROF_ACTIVE), "dim", 0));
    f_perf_spin = w_spin ();
    gtk_box_append (GTK_BOX (hd), f_perf_spin);
    f_perf_act = w_lbl ("—", "val", 1);
    gtk_widget_set_hexpand (f_perf_act, TRUE);
    gtk_box_append (GTK_BOX (hd), f_perf_act);
    gtk_box_append (GTK_BOX (selctl), hd);
  }
  gtk_box_append (GTK_BOX (selctl),
                  w_row (acc_tr (ACC_T_PROF_ON_AC), &f_perf_ac));
  gtk_box_append (GTK_BOX (selctl),
                  w_row (acc_tr (ACC_T_PROF_ON_BAT), &f_perf_bat));

  f_perf_q = gtk_check_button_new_with_label (acc_tr (ACC_T_PERF_QUIET));
  f_perf_b = gtk_check_button_new_with_label (acc_tr (ACC_T_PERF_BAL));
  f_perf_p = gtk_check_button_new_with_label (acc_tr (ACC_T_PERF_BOOST));
  gtk_check_button_set_group (GTK_CHECK_BUTTON (f_perf_b),
                              GTK_CHECK_BUTTON (f_perf_q));
  gtk_check_button_set_group (GTK_CHECK_BUTTON (f_perf_p),
                              GTK_CHECK_BUTTON (f_perf_q));
  g_signal_connect (f_perf_q, "toggled", G_CALLBACK (perf_cb),
                    GINT_TO_POINTER (ACC_PROF_QUIET));
  g_signal_connect (f_perf_b, "toggled", G_CALLBACK (perf_cb),
                    GINT_TO_POINTER (ACC_PROF_BALANCED));
  g_signal_connect (f_perf_p, "toggled", G_CALLBACK (perf_cb),
                    GINT_TO_POINTER (ACC_PROF_PERFORMANCE));
  GtkWidget *pr = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_set_homogeneous (GTK_BOX (pr), TRUE);
  gtk_widget_add_css_class (f_perf_q, "optcard");
  gtk_widget_add_css_class (f_perf_b, "optcard");
  gtk_widget_add_css_class (f_perf_p, "optcard");

  /* gate on the choices the firmware/service actually advertises */
  prof_caps_update ();
  gtk_widget_set_sensitive (f_perf_q, s_perf_can_q);
  gtk_widget_set_sensitive (f_perf_b, s_perf_can_b);
  gtk_widget_set_sensitive (f_perf_p, s_perf_can_p);
  gtk_widget_set_valign (f_perf_q, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (f_perf_b, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (f_perf_p, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (pr), f_perf_q);
  gtk_box_append (GTK_BOX (pr), f_perf_b);
  gtk_box_append (GTK_BOX (pr), f_perf_p);
  gtk_box_append (GTK_BOX (selctl), pr);

  /* explains where the profile comes from and whether AC/battery switching
   * is automatic — read from asusd, never assumed */
  f_perf_note = w_lbl ("", "foot", 0);
  gtk_label_set_wrap (GTK_LABEL (f_perf_note), TRUE);
  gtk_label_set_width_chars (GTK_LABEL (f_perf_note), 30);
  gtk_box_append (GTK_BOX (selctl), f_perf_note);

  gtk_box_append (GTK_BOX (sel), selctl);
  gtk_box_append (GTK_BOX (li), sel);

  /* keyboard backlight, 0..3 like the BIOS value */
  GtkWidget *kbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
  {
    GtkWidget *cim = gtk_image_new_from_icon_name ("input-keyboard-symbolic");
    gtk_image_set_pixel_size (GTK_IMAGE (cim), 40);
    gtk_widget_add_css_class (cim, "cardicon");
    gtk_widget_set_valign (cim, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (kbox), cim);
  }
  {
    GtkWidget *cd = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append (GTK_BOX (cd),
                    w_lbl (acc_tr (ACC_T_KBD), "cardtitle", 0));
    GtkWidget *csub = w_lbl (acc_tr (ACC_T_KBD_HINT), "foot", 0);
    gtk_label_set_wrap (GTK_LABEL (csub), TRUE);
    gtk_label_set_width_chars (GTK_LABEL (csub), 24);
    gtk_box_append (GTK_BOX (cd), csub);
    gtk_widget_set_valign (cd, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (kbox), cd);
  }
  GtkWidget *kctl = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_hexpand (kctl, TRUE);
  {
    GtkWidget *kt = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    f_kbd_cur = w_lbl ("—", "val", 1);
    gtk_widget_set_hexpand (f_kbd_cur, TRUE);
    gtk_box_append (GTK_BOX (kt), w_lbl (acc_tr (ACC_T_KBD), "dim", 0));
    gtk_box_append (GTK_BOX (kt), f_kbd_cur);
    gtk_box_append (GTK_BOX (kctl), kt);
  }
  GtkWidget *krow = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_set_homogeneous (GTK_BOX (krow), TRUE);
  for (int i = 0; i < 4; i++)
    {
      if (i == 0)
        f_kbd[0] = gtk_check_button_new_with_label (acc_tr (ACC_T_KBD_OFF));
      else
        {
          gchar *l = g_strdup_printf ("%d", i);
          f_kbd[i] = gtk_check_button_new_with_label (l);
          g_free (l);
        }
      if (i)
        gtk_check_button_set_group (GTK_CHECK_BUTTON (f_kbd[i]),
                                    GTK_CHECK_BUTTON (f_kbd[0]));
      g_signal_connect (f_kbd[i], "toggled", G_CALLBACK (kbd_cb),
                        (gpointer) (gintptr) i);
      gtk_widget_add_css_class (f_kbd[i], "optcard");
      gtk_widget_set_valign (f_kbd[i], GTK_ALIGN_CENTER);
      gtk_box_append (GTK_BOX (krow), f_kbd[i]);
    }
  /* gate on what the EC actually exposes (see --specs) */
  {
    gchar *kf = acc_dev_kbd_led_file ();
    if (!kf)
      for (int i = 0; i < 4; i++)
        gtk_widget_set_sensitive (f_kbd[i], FALSE);
    g_free (kf);
  }

  gtk_box_append (GTK_BOX (kctl), krow);
  gtk_box_append (GTK_BOX (kbox), kctl);
  gtk_box_append (GTK_BOX (li), kbox);

  /* touchpad block — Input Device group, real GNOME setting */
  {
    GtkWidget *ctl;
    GtkWidget *row = w_feat ("input-touchpad-symbolic",
                             acc_tr (ACC_T_TOUCHPAD),
                             acc_tr (ACC_T_TOUCHPAD_SUB), &ctl);
    gchar *se = gs_str ("org.gnome.desktop.peripherals.touchpad",
                        "send-events");
    GtkWidget *sr = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *fill = w_lbl (NULL, NULL, 0);
    gtk_widget_set_hexpand (fill, TRUE);
    GtkWidget *sw = gtk_switch_new ();
    gtk_switch_set_active (GTK_SWITCH (sw), g_strcmp0 (se, "disabled") != 0);
    gtk_widget_set_valign (sw, GTK_ALIGN_CENTER);
    g_signal_connect (sw, "state-set", G_CALLBACK (touchpad_cb), NULL);
    gtk_box_append (GTK_BOX (sr), fill);
    gtk_box_append (GTK_BOX (sr), sw);
    gtk_box_append (GTK_BOX (ctl), sr);
    gtk_box_append (GTK_BOX (li), row);
    g_free (se);
  }

  gtk_box_append (GTK_BOX (c), li);
  return w_page_titled (c, ACC_T_NAV_FANS);
}

/* system page ------------------------------------------------------------ */
GtkWidget *
acc_page_sys (AccUI *u)
{
  (void) u;
  GtkWidget *c = w_picard (acc_tr (ACC_T_NAV_SYS));
  GtkWidget *li = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);

  GtkWidget *block = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
  {
    GtkWidget *cim = gtk_image_new_from_icon_name
      ("org.gnome.Settings-symbolic");
    gtk_image_set_pixel_size (GTK_IMAGE (cim), 40);
    gtk_widget_add_css_class (cim, "cardicon");
    gtk_widget_set_valign (cim, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (block), cim);
  }
  {
    GtkWidget *cd = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append (GTK_BOX (cd),
                    w_lbl (acc_tr (ACC_T_NAV_SYS), "cardtitle", 0));
    GtkWidget *csub = w_lbl (acc_tr (ACC_T_DIAG_SUB), "foot", 0);
    gtk_label_set_wrap (GTK_LABEL (csub), TRUE);
    gtk_label_set_width_chars (GTK_LABEL (csub), 24);
    gtk_box_append (GTK_BOX (cd), csub);
    gtk_widget_set_valign (cd, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (block), cd);
  }
  {
    GtkWidget *ctl = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_hexpand (ctl, TRUE);
    gtk_box_append (GTK_BOX (ctl), w_row (acc_tr (ACC_T_SYS_OS), &f_sys_os));
    gtk_box_append (GTK_BOX (ctl), w_row (acc_tr (ACC_T_CPU), &f_sys_cpu));
    gtk_box_append (GTK_BOX (ctl), w_row (acc_tr (ACC_T_SYS_RAM), &f_sys_ram));
    gtk_box_append (GTK_BOX (ctl), w_row (acc_tr (ACC_T_SYS_SSD), &f_sys_ssd));
    gtk_box_append (GTK_BOX (ctl), w_row (acc_tr (ACC_T_BIOS), &f_sys_bios));
    gtk_box_append (GTK_BOX (ctl), w_row (acc_tr (ACC_T_KERNEL), &f_sys_kernel));
    GtkWidget *spb = acc_specs_button (acc_tr (ACC_T_SPECS), "actionbtn");
    gtk_widget_set_halign (spb, GTK_ALIGN_START);
    gtk_widget_set_margin_top (spb, 6);
    gtk_box_append (GTK_BOX (ctl), spb);
    gtk_box_append (GTK_BOX (block), ctl);
  }
  gtk_box_append (GTK_BOX (li), block);

  /* Connectivity card — live, read-only (kernel + iw, no root) */
  {
    GtkWidget *ctl;
    GtkWidget *conn = w_feat ("network-wireless-symbolic",
                              acc_tr (ACC_T_CONN), NULL, &ctl);
    gchar *iface = wifi_iface ();
    gchar *ssid = wifi_ssid (iface);
    f_conn_wifi = w_lbl (ssid ? ssid : acc_tr (ACC_T_NOT_CONN), "val", 0);
    gtk_widget_set_hexpand (f_conn_wifi, TRUE);
    f_conn_wifi_sub = w_lbl (iface ? iface : "—", "foot", 0);
    GtkWidget *wr = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append (GTK_BOX (wr), w_lbl (acc_tr (ACC_T_WIFI), "dim", 0));
    gtk_box_append (GTK_BOX (wr), f_conn_wifi);

    gchar *bt = bt_radio ();
    f_conn_bt = w_lbl (bt ? acc_tr (g_strcmp0 (bt, "on") == 0
                                    ? ACC_T_ON : ACC_T_OFF) : "—",
                       "val", 1);
    gtk_widget_set_hexpand (f_conn_bt, TRUE);
    GtkWidget *br = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append (GTK_BOX (br), w_lbl (acc_tr (ACC_T_BT), "dim", 0));
    gtk_box_append (GTK_BOX (br), f_conn_bt);

    gtk_box_append (GTK_BOX (ctl), wr);
    gtk_box_append (GTK_BOX (ctl), f_conn_wifi_sub);
    gtk_box_append (GTK_BOX (ctl), br);
    gtk_box_append (GTK_BOX (li), conn);
    g_free (bt);
    g_free (ssid);
    g_free (iface);
  }

  gtk_box_append (GTK_BOX (c), li);
  return w_page_titled (c, ACC_T_GRP_SYS);
}

/* ------------------------------------------------ settings page ------ */
static void
lang_cb2 (GtkButton *b, gpointer user)
{
  (void) b;
  (void) user;
  acc_ui_lang_toggle ();
}

static void
theme_sw_cb (GtkSwitch *sw, GParamSpec *ps, gpointer user)
{
  (void) ps;
  (void) user;
  if (s_sync)
    return;
  if (gtk_switch_get_active (sw) != acc_theme_is_dark ())
    acc_ui_theme_toggle ();   /* rebuilds the UI in the new skin */
}

GtkWidget *
acc_page_settings (AccUI *u)
{
  GtkWidget *c = w_picard (acc_tr (ACC_T_NAV_SET));
  GtkWidget *li = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);

  GtkWidget *block = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
  {
    GtkWidget *cim = gtk_image_new_from_icon_name
      ("emblem-system-symbolic");
    gtk_image_set_pixel_size (GTK_IMAGE (cim), 40);
    gtk_widget_add_css_class (cim, "cardicon");
    gtk_widget_set_valign (cim, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (block), cim);
  }
  {
    GtkWidget *cd = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append (GTK_BOX (cd),
                    w_lbl (acc_tr (ACC_T_NAV_SET), "cardtitle", 0));
    GtkWidget *csub = w_lbl (acc_tr (ACC_T_M2_NOTICE), "foot", 0);
    gtk_label_set_wrap (GTK_LABEL (csub), TRUE);
    gtk_label_set_width_chars (GTK_LABEL (csub), 24);
    gtk_box_append (GTK_BOX (cd), csub);
    gtk_widget_set_valign (cd, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (block), cd);
  }
  {
    GtkWidget *ctl = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_hexpand (ctl, TRUE);

    /* privileged helper status */
    f_helper = w_lbl ("—", "val", 0);
    gtk_widget_set_hexpand (f_helper, TRUE);
    GtkWidget *helper_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append (GTK_BOX (helper_row),
                    w_lbl (acc_tr (ACC_T_CAP_HELPER), "dim", 0));
    gtk_box_append (GTK_BOX (helper_row), f_helper);
    gtk_box_append (GTK_BOX (ctl), helper_row);

    /* static build version — accurate, not read from the hardware */
    GtkWidget *ver_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append (GTK_BOX (ver_row), w_lbl (acc_tr (ACC_T_VER), "dim", 0));
    gtk_box_append (GTK_BOX (ver_row), w_lbl ("0.3", "val", 0));
    gtk_box_append (GTK_BOX (ctl), ver_row);

    /* language pill: EN <-> ع */
    GtkWidget *lang_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append (GTK_BOX (lang_row), w_lbl (acc_tr (ACC_T_LANG), "dim", 0));
    GtkWidget *langfill = w_lbl (NULL, NULL, 0);
    gtk_widget_set_hexpand (langfill, TRUE);
    gtk_box_append (GTK_BOX (lang_row), langfill);
    GtkWidget *lang = gtk_button_new_with_label ("EN / ع");
    gtk_widget_add_css_class (lang, "langpill");
    g_signal_connect (lang, "clicked", G_CALLBACK (lang_cb2), u);
    gtk_box_append (GTK_BOX (lang_row), lang);
    gtk_box_append (GTK_BOX (ctl), lang_row);

    /* theme row: same action as the header moon/sun button */
    GtkWidget *theme_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append (GTK_BOX (theme_row), w_lbl (acc_tr (ACC_T_THEME),
                                                "dim", 0));
    GtkWidget *theme_fill = w_lbl (NULL, NULL, 0);
    gtk_widget_set_hexpand (theme_fill, TRUE);
    gtk_box_append (GTK_BOX (theme_row), theme_fill);
    GtkWidget *tsw = gtk_switch_new ();
    gtk_switch_set_active (GTK_SWITCH (tsw), acc_theme_is_dark ());
    gtk_widget_set_valign (tsw, GTK_ALIGN_CENTER);
    g_signal_connect (tsw, "state-set", G_CALLBACK (theme_sw_cb), NULL);
    gtk_box_append (GTK_BOX (theme_row), tsw);
    gtk_box_append (GTK_BOX (ctl), theme_row);

    gtk_box_append (GTK_BOX (block), ctl);
  }
  gtk_box_append (GTK_BOX (li), block);

  /* About / support card — the MyASUS "customer support" block */
  GtkWidget *about = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
  {
    GtkWidget *cim = gtk_image_new_from_icon_name ("help-about-symbolic");
    gtk_image_set_pixel_size (GTK_IMAGE (cim), 40);
    gtk_widget_add_css_class (cim, "cardicon");
    gtk_widget_set_valign (cim, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (about), cim);
  }
  {
    GtkWidget *cd = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append (GTK_BOX (cd),
                    w_lbl (acc_tr (ACC_T_ABOUT), "cardtitle", 0));
    GtkWidget *csub = w_lbl (acc_tr (ACC_T_DESC), "foot", 0);
    gtk_label_set_wrap (GTK_LABEL (csub), TRUE);
    gtk_label_set_width_chars (GTK_LABEL (csub), 24);
    gtk_box_append (GTK_BOX (cd), csub);
    gtk_widget_set_valign (cd, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (about), cd);
  }
  {
    GtkWidget *ctl = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_hexpand (ctl, TRUE);
    GtkWidget *web = gtk_link_button_new_with_label
      ("https://www.asus.com/support/", acc_tr (ACC_T_OPEN_WEB));
    gtk_widget_add_css_class (web, "primarybtn");
    gtk_widget_set_halign (web, GTK_ALIGN_END);
    gtk_box_append (GTK_BOX (ctl), web);
    gtk_box_append (GTK_BOX (about), ctl);
  }
  gtk_box_append (GTK_BOX (li), about);

  gtk_box_append (GTK_BOX (c), li);
  return w_page_titled (c, ACC_T_GRP_GEN);
}

/* ------------------------------------------------ support page ------- */
static void
diag_cb (GtkButton *b, gpointer u)
{
  (void) b;
  (void) u;
  BatInfo bat;
  HwInfo h;
  LoadInfo ld;
  battery_read (&bat);
  hwmon_read (&h);
  acc_load_read (&ld);
  acc_load_read (&ld);   /* second sample so CPU% has a delta */

  gboolean warn = FALSE;
  GString *s = g_string_new (NULL);

  g_string_append_printf (s, "%s ", acc_tr (ACC_T_NAV_BAT));
  if (bat.wh_now > 0 && bat.wh_design > 0)
    {
      gdouble hp = bat.wh_now / bat.wh_design * 100.0;
      g_string_append_printf (s, "%.0f%%", hp);
      if (hp < 80.0)
        warn = TRUE;
    }
  else
    g_string_append (s, "—");

  g_string_append_printf (s, "   ·   %s ", acc_tr (ACC_T_CPU));
  if (h.t_cpu > 0)
    {
      g_string_append_printf (s, "%.0f°C", h.t_cpu);
      if (h.t_cpu > 85.0)
        warn = TRUE;
    }
  else
    g_string_append (s, "—");

  g_string_append_printf (s, "   ·   %s ", acc_tr (ACC_T_TILE_DISK));
  if (ld.disk_pct >= 0)
    {
      g_string_append_printf (s, "%.0f%%", ld.disk_pct);
      if (ld.disk_pct > 90.0)
        warn = TRUE;
    }
  else
    g_string_append (s, "—");

  g_string_append_printf (s, "   ·   %s ", acc_tr (ACC_T_TILE_MEM));
  if (ld.mem_pct >= 0)
    {
      g_string_append_printf (s, "%.0f%%", ld.mem_pct);
      if (ld.mem_pct > 90.0)
        warn = TRUE;
    }
  else
    g_string_append (s, "—");

  g_string_append (s, "\n");
  g_string_append (s, acc_tr (warn ? ACC_T_DIAG_WARN : ACC_T_DIAG_OK));
  gtk_label_set_text (GTK_LABEL (f_diag_res), s->str);
  g_string_free (s, TRUE);
}

GtkWidget *
acc_page_support (AccUI *u)
{
  (void) u;
  GtkWidget *c = w_picard (acc_tr (ACC_T_SUPPORT));
  GtkWidget *li = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);

  /* warranty — honestly handled on ASUS's own site */
  {
    GtkWidget *ctl;
    GtkWidget *row = w_feat ("security-high-symbolic",
                             acc_tr (ACC_T_WARRANTY),
                             acc_tr (ACC_T_WARRANTY_SUB), &ctl);
    GtkWidget *web = gtk_link_button_new_with_label
      ("https://www.asus.com/support/", acc_tr (ACC_T_OPEN_WEB));
    gtk_widget_add_css_class (web, "primarybtn");
    gtk_widget_set_halign (web, GTK_ALIGN_END);
    gtk_box_append (GTK_BOX (ctl), web);
    gtk_box_append (GTK_BOX (li), row);
  }

  /* drivers & downloads */
  {
    GtkWidget *ctl;
    GtkWidget *row = w_feat ("software-update-available-symbolic",
                             acc_tr (ACC_T_SUPPORT),
                             acc_tr (ACC_T_SUPPORT_SUB), &ctl);
    GtkWidget *web = gtk_link_button_new_with_label
      ("https://www.asus.com/support/Download-Center/",
       acc_tr (ACC_T_DRIVERS));
    gtk_widget_add_css_class (web, "primarybtn");
    gtk_widget_set_halign (web, GTK_ALIGN_END);
    gtk_box_append (GTK_BOX (ctl), web);
    gtk_box_append (GTK_BOX (li), row);
  }

  /* service links — MyASUS "Customer Support" quick actions */
  {
    GtkWidget *ctl;
    GtkWidget *row = w_feat ("system-help-symbolic",
                             acc_tr (ACC_T_SERVICE),
                             acc_tr (ACC_T_SERVICE_SUB), &ctl);
    struct { const gchar *url; AccStr txt; } links[] = {
      { "https://www.asus.com/support/warranty-status-inquiry",
        ACC_T_WARRANTY_CHECK },
      { "https://www.asus.com/support/rma-location",
        ACC_T_REPAIR },
      { "https://www.asus.com/support/service-center-location",
        ACC_T_SERVICE_LOC },
      { "https://www.asus.com/support/CallUs",
        ACC_T_CONTACT },
    };
    for (gsize i = 0; i < G_N_ELEMENTS (links); i++)
      {
        GtkWidget *b = gtk_link_button_new_with_label (links[i].url,
                                                       acc_tr (links[i].txt));
        gtk_widget_add_css_class (b, "qrow");
        gtk_widget_set_halign (b, GTK_ALIGN_FILL);
        gtk_box_append (GTK_BOX (ctl), b);
      }
    gtk_box_append (GTK_BOX (li), row);
  }

  /* diagnostics — real sensor readings, run on demand */
  {
    GtkWidget *ctl;
    GtkWidget *row = w_feat ("applications-utilities-symbolic",
                             acc_tr (ACC_T_DIAG_TITLE),
                             acc_tr (ACC_T_DIAG_SUB), &ctl);
    GtkWidget *run = gtk_button_new_with_label (acc_tr (ACC_T_RUN_DIAG));
    gtk_widget_add_css_class (run, "primarybtn");
    gtk_widget_set_halign (run, GTK_ALIGN_END);
    g_signal_connect (run, "clicked", G_CALLBACK (diag_cb), NULL);
    gtk_box_append (GTK_BOX (ctl), run);
    f_diag_res = w_lbl ("—", "dim", 0);
    gtk_label_set_wrap (GTK_LABEL (f_diag_res), TRUE);
    gtk_widget_set_hexpand (f_diag_res, TRUE);
    gtk_box_append (GTK_BOX (ctl), f_diag_res);
    gtk_box_append (GTK_BOX (li), row);
  }

  gtk_box_append (GTK_BOX (c), li);
  return w_page_titled (c, ACC_T_SUPPORT);
}

void
acc_page_refresh (AccUI *u)
{
  gchar buf[128];
  s_sync = TRUE;
  (void) u;

  /* battery page */
  if (f_bat_status)
    {
      BatInfo b;
      battery_read (&b);
      w_ring_val (f_bat_ring, b.capacity > 0 ? b.capacity / 100.0 : 0.0);
      w_ring_big (f_bat_ring,
                  b.capacity >= 0
                  ? (g_snprintf (buf, sizeof buf, "%d%%", b.capacity), buf)
                  : "—");
      g_snprintf (buf, sizeof buf, "%d%% %s",
                  b.capacity >= 0 ? b.capacity : 0, acc_status_tr (b.status));
      gtk_label_set_text (GTK_LABEL (f_bat_status), buf);
      g_snprintf (buf, sizeof buf, "%.1f Wh", b.wh_now);
      gtk_label_set_text (GTK_LABEL (f_bat_whnow), b.wh_now > 0 ? buf : "—");
      g_snprintf (buf, sizeof buf, "%.0f Wh", b.wh_design);
      gtk_label_set_text (GTK_LABEL (f_bat_whf), b.wh_design > 0 ? buf : "—");
      g_snprintf (buf, sizeof buf, "%d", b.cycle_count);
      gtk_label_set_text (GTK_LABEL (f_bat_cyc), b.cycle_count >= 0 ? buf : "—");
      /* honest health number: needs both readings to be meaningful */
      gtk_label_set_text (GTK_LABEL (f_bat_health),
                          (b.wh_now > 0 && b.wh_design > 0)
                          ? (g_snprintf (buf, sizeof buf, "%.0f%%",
                                         b.wh_now / b.wh_design * 100.0), buf)
                          : "—");

      gchar *thrf = acc_dev_charge_limit_file ();
      int thr = thrf ? acc_read_int (thrf, -1) : -1;
      g_free (thrf);
      if (thr == 100 || thr <= 0)
        gtk_check_button_set_active (GTK_CHECK_BUTTON (f_lim_full), TRUE);
      else if (thr == 80)
        gtk_check_button_set_active (GTK_CHECK_BUTTON (f_lim_bal), TRUE);
      else if (thr == 60)
        gtk_check_button_set_active (GTK_CHECK_BUTTON (f_lim_life),  TRUE);
      else  /* unknown / unreadable: never fake an active feature */
        {
          gtk_check_button_set_active (GTK_CHECK_BUTTON (f_lim_full),  FALSE);
          gtk_check_button_set_active (GTK_CHECK_BUTTON (f_lim_bal),   FALSE);
          gtk_check_button_set_active (GTK_CHECK_BUTTON (f_lim_life),  FALSE);
        }
      gtk_label_set_text (GTK_LABEL (f_bat_limit),
                          thr >= 0
                          ? (g_snprintf (buf, sizeof buf, "%d%%", thr), buf)
                          : "—");
      gchar *polf = acc_dev_wmi_file ("charge_mode");
      int pol = polf ? acc_read_int (polf, -1) : -1;
      g_free (polf);
      gtk_label_set_text (GTK_LABEL (f_bat_pol),
                          pol == 0 ? acc_tr (ACC_T_POL_STD)
                          : pol == 1 ? acc_tr (ACC_T_POL_BAL)
                          : pol == 2 ? acc_tr (ACC_T_POL_MAX) : "—");
    }

  /* gpu page */
  if (f_gpu_name)
    {
      GpuInfo g;
      gpu_read (&g);
      gtk_label_set_text (GTK_LABEL (f_gpu_name),
                          g.ok && g.short_name[0] ? g.short_name : "—");

      if (g.util_pct >= 0)
        {
          g_snprintf (buf, sizeof buf, "%d%%", g.util_pct);
          gtk_label_set_text (GTK_LABEL (f_gpu_util), buf);
          gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (f_gpu_bar),
                                         g.util_pct / 100.0);
        }
      else
        {
          gtk_label_set_text (GTK_LABEL (f_gpu_util), "—");
          gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (f_gpu_bar), 0.0);
        }

      if (g.mem_total_gb > 0)
        {
          if (g.mem_used_gb >= 0)
            g_snprintf (buf, sizeof buf, "%.1f / %.1f GB",
                        g.mem_used_gb, g.mem_total_gb);
          else
            g_snprintf (buf, sizeof buf, "%.1f GB", g.mem_total_gb);
          gtk_label_set_text (GTK_LABEL (f_gpu_vram), buf);
        }
      else
        gtk_label_set_text (GTK_LABEL (f_gpu_vram), "—");

      gtk_label_set_text (GTK_LABEL (f_gpu_temp),
                          g.temp_c > 0
                          ? (g_snprintf (buf, sizeof buf, "%.0f°C",
                                         g.temp_c), buf)
                          : "—");
      gtk_label_set_text (GTK_LABEL (f_gpu_power),
                          g.power_w > 0
                          ? (g_snprintf (buf, sizeof buf, "%.0f W",
                                         g.power_w), buf)
                          : "—");
      gtk_label_set_text (GTK_LABEL (f_gpu_out),
                          g.display_active == 1 ? acc_tr (ACC_T_GPU_OUT_YES)
                          : g.display_active == 0 ? acc_tr (ACC_T_GPU_OUT_NO)
                          : "—");

      /* driver state is a fact (/proc/modules); the MUX value is a
       * separate firmware setting — never conflate the two */
      gboolean nv = acc_ctl_nvidia_loaded ();
      if (g.driver[0])
        {
          g_snprintf (buf, sizeof buf, "%s · %s", g.driver,
                      acc_tr (nv ? ACC_T_DRV_ON : ACC_T_DRV_OFF));
          gtk_label_set_text (GTK_LABEL (f_gpu_drv), buf);
        }
      else
        gtk_label_set_text (GTK_LABEL (f_gpu_drv),
                            acc_tr (nv ? ACC_T_DRV_ON : ACC_T_DRV_OFF));

      gchar *muxf = acc_dev_wmi_file ("gpu_mux_mode");
      int mux = muxf ? acc_read_int (muxf, -1) : -1;
      g_free (muxf);

      AsusdGpuState gs;
      gboolean has_gs = acc_asusd_gpu (&gs);

      AccGfxState gx;
      gboolean has_gx = acc_gfx_state (&gx);
      s_gfx_mode = has_gx ? gx.mode : ACC_GFX_UNKNOWN;
      s_gfx_pending = has_gx ? gx.pending : ACC_GFX_UNKNOWN;
      gboolean igpu = s_gfx_mode == ACC_GFX_INTEGRATED;

      /* iGPU only needs the daemon, its Integrated mode, and the panel on
       * the iGPU (MUX hybrid); while active it stays selectable */
      s_gpu_can_eco = has_gx && gx.can_integrated && (mux != 0 || igpu);
      if (f_gpu_eco_note)
        gtk_label_set_text (GTK_LABEL (f_gpu_eco_note),
                            acc_tr (!gx.known ? ACC_T_GFX_HINT
                                    : !gx.installed ? ACC_T_GFX_NEED
                                    : !has_gx ? ACC_T_GFX_START
                                    : !gx.can_integrated ? ACC_T_GPU_ECO_WHY
                                    : mux == 0 && !igpu ? ACC_T_GFX_MUX
                                    : ACC_T_GFX_HINT));

      gtk_label_set_text (GTK_LABEL (f_gpu_now),
                          igpu ? acc_tr (ACC_T_GPU_ECO)
                          : mux == 0 ? acc_tr (ACC_T_GPU_ULT)
                          : mux == 1 ? acc_tr (ACC_T_GPU_HYBRID)
                          : "—");

      /* asusd's PossibleValues narrows what the sysfs node allows */
      if (has_gs && (gs.allow_hybrid || gs.allow_ultimate))
        {
          s_gpu_can_hybrid = gs.allow_hybrid;
          s_gpu_can_ultimate = gs.allow_ultimate;
        }

      /* The radio always follows the LIVE firmware value: a queued change
       * is a request, not a state, and the note below spells it out. */
      if (igpu)
        gtk_check_button_set_active (GTK_CHECK_BUTTON (f_gpu_eco), TRUE);
      else if (mux == 0)
        gtk_check_button_set_active (GTK_CHECK_BUTTON (f_gpu_ult), TRUE);
      else if (mux == 1)
        gtk_check_button_set_active (GTK_CHECK_BUTTON (f_gpu_hybrid), TRUE);

      /* only re-enable while no apply job is in flight; the counter is the
       * single source of truth, so a lost/queued timeout cannot leave the
       * page stuck with a spinner and dead radios */
      gpu_radios_sensitive (s_gpu_jobs == 0);
      if (s_gpu_jobs == 0)
        w_busy (f_gpu_spin, FALSE);

      if (f_gpu_pill)
        {
          gtk_label_set_text (GTK_LABEL (f_gpu_pill),
                              mux >= 0 || igpu ? acc_tr (ACC_T_GPU_LIVE) : "");
          gtk_widget_set_visible (f_gpu_pill, mux >= 0 || igpu);
        }

      if (f_gpu_notice)
        {
          gboolean stale = acc_ctl_helper_stale ();
          gboolean pend = acc_dev_pending_reboot ();
          gboolean qpend = has_gs && gs.queued >= 0 && gs.queued != gs.current;
          gboolean gpend = has_gx && (gx.pending == ACC_GFX_INTEGRATED
                                      || gx.pending == ACC_GFX_HYBRID);
          const gchar *kind = NULL;
          gchar *head = NULL, *body = NULL;

          if (stale)
            {
              kind = "warn";
              head = g_strdup (acc_tr (ACC_T_HELPER_STALE));
            }
          else if (qpend)
            {
              /* say which mode is queued: the queue is a request, not a
               * live state, and the user must know what will be applied */
              head = g_strdup_printf ("%s · %s",
                                      acc_tr (gs.queued == 0 ? ACC_T_GPU_ULT
                                                             : ACC_T_GPU_HYBRID),
                                      acc_tr (ACC_T_GPU_NEXT));
              if (gs.shutdown_ready)
                {
                  kind = "info";
                  body = g_strdup_printf ("%s\n%s\n%s\n%s",
                                          acc_tr (ACC_T_GPU_STEP1),
                                          acc_tr (ACC_T_GPU_STEP2),
                                          acc_tr (ACC_T_GPU_STEP3),
                                          acc_tr (ACC_T_GPU_REVERT));
                }
              else
                {
                  kind = "warn";
                  body = g_strdup (acc_tr (ACC_T_GPU_NO_HOOK));
                }
            }
          else if (gpend)
            {
              /* supergfxd accepted a request that finishes at logout */
              kind = "info";
              head = g_strdup_printf ("%s · %s",
                                      acc_tr (gx.pending == ACC_GFX_INTEGRATED
                                              ? ACC_T_GPU_ECO
                                              : ACC_T_GPU_HYBRID),
                                      acc_tr (gx.action == ACC_GFX_ACT_REBOOT
                                              ? ACC_T_GPU_NEXT
                                              : ACC_T_GFX_AFTER_LOGOUT));
            }
          else if (pend)
            {
              kind = "info";
              head = g_strdup (acc_tr (ACC_T_GPU_PENDING));
            }

          gtk_widget_remove_css_class (f_gpu_notice, "info");
          gtk_widget_remove_css_class (f_gpu_notice, "warn");
          if (kind)
            gtk_widget_add_css_class (f_gpu_notice, kind);
          gtk_label_set_text (GTK_LABEL (f_gpu_notice_t), head ? head : "");
          gtk_label_set_text (GTK_LABEL (f_gpu_notice_b), body ? body : "");
          gtk_widget_set_visible (f_gpu_notice_b, body != NULL);
          gtk_widget_set_visible (f_gpu_cancel, qpend);
          gtk_widget_set_visible (f_gpu_notice, head != NULL);
          g_free (head);
          g_free (body);
        }
    }

  /* display page */
  if (f_dsp_res)
    {
      DispInfo d;
      display_read (&d);
      gtk_label_set_text (GTK_LABEL (f_dsp_res),
                          d.ok ? (g_snprintf (buf, sizeof buf,
                                              "%d × %d @ %.0f Hz",
                                              d.width, d.height, d.refresh_hz), buf)
                               : "—");
      if (d.backlight_max > 0 && d.backlight_now >= 0)
        {
          gdouble bl = d.backlight_now * 100.0 / d.backlight_max;
          /* same 3 s hands-off rule as the dimming slider below; the
           * Fn keys and GNOME's own slider move this one too */
          if (g_get_monotonic_time () / 1000 - s_bl_touch >= 3000)
            gtk_range_set_value (GTK_RANGE (f_bl_slider), bl);
          g_snprintf (buf, sizeof buf, "%.0f%%", bl);
          gtk_label_set_text (GTK_LABEL (f_bl_pct), buf);
        }
      gdouble sbp = acc_brightness_get ();
      if (sbp >= 0)
        {
          /* don't yank the slider while the user is dragging (3 s):
           * gtk_range_set_value() emits value-changed, which the guard
           * in dsp_cb would drop anyway, but the visual jump is ugly. */
          if (g_get_monotonic_time () / 1000 - s_dsp_touch >= 3000)
            gtk_range_set_value (GTK_RANGE (f_dsp_slider), sbp);
          g_snprintf (buf, sizeof buf, "%.0f%%", sbp);
          gtk_label_set_text (GTK_LABEL (f_dsp_pct), buf);
        }
    }

  /* perf page */
  if (f_fan_cpu)
    {
      HwInfo h;
      hwmon_read (&h);
      w_ring_val (f_perf_ring, h.t_cpu > 0 ? h.t_cpu / 100.0 : 0.0);
      if (f_perf_ring)
        acc_ring_set_big (f_perf_ring,
                          h.t_cpu > 0
                          ? (g_snprintf (buf, sizeof buf, "%.0f°C", h.t_cpu), buf)
                          : "—");
      gtk_label_set_text (GTK_LABEL (f_fan_cpu),
                          h.fan_cpu >= 0
                          ? (g_snprintf (buf, sizeof buf, "%d RPM", h.fan_cpu), buf)
                          : "—");
      gtk_label_set_text (GTK_LABEL (f_fan_gpu),
                          h.fan_gpu >= 0
                          ? (g_snprintf (buf, sizeof buf, "%d RPM", h.fan_gpu), buf)
                          : "—");
      gtk_label_set_text (GTK_LABEL (f_perf_temp),
                          h.t_board > 0
                          ? (g_snprintf (buf, sizeof buf, "%.0f°C", h.t_board), buf)
                          : "—");
    }

  /* the same live profile feeds the CPU-priority selector on the GPU page,
   * so sync captions and radios whichever page is currently built */
  prof_refresh ();

  /* keyboard backlight: honest level read from the LED class device; the
   * four level cards are disabled when this unit has no such LED */
  if (f_kbd_cur)
    {
      gchar *kf = acc_dev_kbd_led_file ();
      gint lvl = kf ? acc_read_int (kf, -1) : -1;
      g_free (kf);

      if (lvl >= 0)
        g_snprintf (buf, sizeof buf, "%d / %d", lvl, 3);
      gtk_label_set_text (GTK_LABEL (f_kbd_cur), lvl >= 0 ? buf : "—");
      for (int i = 0; i < 4; i++)
        {
          gtk_widget_set_sensitive (f_kbd[i], lvl >= 0);
          if (lvl == i)
            gtk_check_button_set_active (GTK_CHECK_BUTTON (f_kbd[i]), TRUE);
        }
    }

  /* system page */
  if (f_sys_os)
    {
      gchar *v = sysinfo_os ();
      gtk_label_set_text (GTK_LABEL (f_sys_os), v);
      g_free (v);
      v = sysinfo_cpu ();
      gtk_label_set_text (GTK_LABEL (f_sys_cpu), v);
      g_free (v);
      g_snprintf (buf, sizeof buf, "%.0f GB", sysinfo_ram_gb ());
      gtk_label_set_text (GTK_LABEL (f_sys_ram), buf);
      v = sysinfo_nvme_model ();
      gtk_label_set_text (GTK_LABEL (f_sys_ssd), v);
      g_free (v);
      v = dmi_bios_version ();
      gtk_label_set_text (GTK_LABEL (f_sys_bios), v ? v : "—");
      g_free (v);
      v = acc_read_trim ("/proc/sys/kernel/osrelease");
      gtk_label_set_text (GTK_LABEL (f_sys_kernel), v ? v : "—");
      g_free (v);
    }

  /* settings page */
  if (f_helper)
    gtk_label_set_text (GTK_LABEL (f_helper),
                        !acc_ctl_available () ? acc_tr (ACC_T_HELPER_NO)
                        : acc_ctl_helper_stale () ? acc_tr (ACC_T_HELPER_STALE)
                                                  : acc_tr (ACC_T_HELPER_OK));

  s_sync = FALSE;
}

void
acc_page_teardown (void)
{
  s_sync = FALSE;
  s_dsp_touch = 0;
  f_bat_ring = NULL; f_perf_ring = NULL;
  f_bat_status = f_bat_whnow = f_bat_whf = f_bat_cyc = NULL;
  f_bat_health = NULL;
  f_lim_full = f_lim_bal = f_lim_life = NULL;
  f_bat_limit = f_bat_pol = NULL;
  f_gpu_name = f_gpu_bar = f_gpu_util = f_gpu_temp = NULL;
  f_gpu_drv = f_gpu_now = f_gpu_vram = f_gpu_power = f_gpu_out = NULL;
  f_gpu_spin = NULL;
  f_gpu_notice = f_gpu_notice_t = f_gpu_notice_b = NULL;
  f_gpu_cancel = f_gpu_pill = NULL;
  s_gpu_jobs = 0;
  f_gpu_hybrid = f_gpu_ult = f_gpu_eco = NULL;
  f_gpu_eco_note = NULL;
  f_dsp_res = f_dsp_slider = f_dsp_pct = NULL;
  f_bl_slider = f_bl_pct = NULL;
  f_fan_cpu = f_fan_gpu = f_perf_temp = NULL;
  f_perf_q = f_perf_b = f_perf_p = NULL;
  f_perf_spin = f_perf_act = f_perf_ac = f_perf_bat = f_perf_note = NULL;
  f_cpu_q = f_cpu_b = f_cpu_p = NULL;
  f_cpu_spin = f_cpu_act = NULL;
  s_perf_jobs = 0;
  f_kbd_cur = NULL;
  for (int i = 0; i < 4; i++)
    f_kbd[i] = NULL;
  f_sys_os = f_sys_cpu = f_sys_ram = f_sys_ssd = f_sys_bios = f_sys_kernel = NULL;
  f_conn_wifi = f_conn_wifi_sub = f_conn_bt = NULL;
  f_helper = NULL;
  f_diag_res = NULL;
}
