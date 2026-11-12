/* main.c - application entry point
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <adwaita.h>
#include <locale.h>
#include <string.h>
#include <sys/utsname.h>

#include "i18n/i18n.h"
#include "ui/window.h"
#include "ui/home.h"
#include "ui/sysmgr.h"
#include "ui/login.h"
#include "user/user.h"
#include "control/backlight.h"
#include "control/brightness.h"
#include "control/asusd.h"
#include "control/gfx.h"
#include "control/muxreq.h"
#include "control/softdim.h"
#include "control/nightlight.h"
#include "control/touchpadctl.h"
#include "control/commands.h"
#include "control/hostcmd.h"
#include "probe/probe.h"
#include "probe/desktop.h"
#include "probe/devs.h"
#include "probe/battery.h"
#include "probe/dmi.h"
#include "probe/hwmon.h"
#include "probe/gpu.h"
#include "probe/display.h"
#include "probe/sysinfo.h"

#define APP_ID "io.github.asuscontrolcenter.AsusControlCenter"

static guint refresh_src = 0;

static gboolean
refresh_tick (gpointer user)
{
  (void) user;
  acc_ui_refresh ();
  return G_SOURCE_CONTINUE;
}

/* ----------------------------------------------------- spec report ---- */
/* `--specs` (also `--self-test`) prints the full hardware picture and
 * which controls this exact machine supports. Read-only: nothing is
 * written. The login page opens this in a terminal. */

static void
r_field (const gchar *key, const gchar *value)
{
  g_print ("  %-16s %s\n", key, (value && *value) ? value : "—");
}

static void
r_cap (gboolean ok, const gchar *label, const gchar *detail)
{
  g_print ("  %s %-26s %s\n", ok ? "OK  " : "--  ", label,
           (detail && *detail) ? detail : "");
}

static const gchar *
prof_name (AccProfile p)
{
  switch (p)
    {
    case ACC_PROF_QUIET:       return "quiet";
    case ACC_PROF_BALANCED:    return "balanced";
    case ACC_PROF_PERFORMANCE: return "performance";
    case ACC_PROF_LOW_POWER:   return "low-power";
    default:                   return NULL;
    }
}

static gint
cpu_threads (void)
{
  gchar *txt = NULL;
  gint n = 0;
  if (g_file_get_contents ("/proc/cpuinfo", &txt, NULL, NULL))
    {
      for (const gchar *p = txt; (p = strstr (p, "processor")); p += 9)
        n++;
      g_free (txt);
    }
  return n;
}

static gboolean
nvidia_loaded (void)
{
  gchar *mods = NULL;
  gboolean found = FALSE;
  if (g_file_get_contents ("/proc/modules", &mods, NULL, NULL))
    {
      found = g_str_has_prefix (mods, "nvidia ")
        || strstr (mods, "\nnvidia ") != NULL;
      g_free (mods);
    }
  return found;
}

static void
self_test (void)
{
  gchar *s;
  struct utsname un;
  uname (&un);

  acc_brightness_init (NULL, NULL);   /* for the OLED dim availability line */

  g_print ("==========================================================\n");
  g_print ("  ASUSCraft — laptop report (read-only)\n");
  g_print ("==========================================================\n");

  /* ---------------- device ---------------- */
  g_print ("\nDEVICE\n");
  s = dmi_product_name ();
  r_field ("Model", s);
  g_free (s);
  s = dmi_vendor ();
  r_field ("Vendor", s);
  g_free (s);
  s = acc_read_trim ("/sys/class/dmi/id/product_serial");
  r_field ("Serial", s);
  g_free (s);
  s = dmi_bios_version ();
  r_field ("BIOS", s);
  g_free (s);
  s = acc_read_trim ("/sys/class/dmi/id/board_name");
  r_field ("Board", s);
  g_free (s);

  /* ---------------- processor / memory ---------------- */
  g_print ("\nPROCESSOR\n");
  s = sysinfo_cpu ();
  r_field ("CPU", s);
  g_free (s);
  g_print ("  %-16s %d\n", "Threads", cpu_threads ());
  r_field ("Architecture", un.machine);

  g_print ("\nMEMORY & STORAGE\n");
  gchar *ram = g_strdup_printf ("%.1f GB", sysinfo_ram_gb ());
  r_field ("RAM", ram);
  g_free (ram);
  s = sysinfo_nvme_model ();
  r_field ("NVMe", s);
  g_free (s);
  {
    guint64 total = 0, freeb = 0;
    GFile *root = g_file_new_for_path ("/");
    GFileInfo *fi = g_file_query_filesystem_info
      (root, G_FILE_ATTRIBUTE_FILESYSTEM_SIZE ","
       G_FILE_ATTRIBUTE_FILESYSTEM_FREE, NULL, NULL);
    if (fi)
      {
        total = g_file_info_get_attribute_uint64
          (fi, G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);
        freeb = g_file_info_get_attribute_uint64
          (fi, G_FILE_ATTRIBUTE_FILESYSTEM_FREE);
        g_object_unref (fi);
      }
    g_object_unref (root);
    if (total)
      {
        gchar *disk = g_strdup_printf ("%.0f / %.0f GB",
                                       (total - freeb) / 1e9, total / 1e9);
        r_field ("Disk (/)", disk);
        g_free (disk);
      }
    else
      r_field ("Disk (/)", NULL);
  }

  /* ---------------- graphics ---------------- */
  g_print ("\nGRAPHICS\n");
  GpuInfo g;
  gpu_read (&g);
  r_field ("GPU vendor", gpu_vendor_label ());
  if (g.ok)
    {
      r_field ("GPU", g.short_name);
      gchar *mem = g_strdup_printf ("%.0f GB total (%.1f GB in use)",
                                    g.mem_total_gb, g.mem_used_gb);
      r_field ("Video memory", mem);
      g_free (mem);
    }
  else
    {
      r_field ("GPU", NULL);
      r_field ("Video memory", NULL);
    }
  r_field ("NVIDIA module", nvidia_loaded () ? "loaded" : "not loaded");
  s = acc_dev_wmi_file ("gpu_mux_mode");
  if (s)
    {
      gchar *v = acc_read_trim (s);
      g_free (s);
      s = v;
    }
  r_field ("MUX mode", s ? (g_strcmp0 (s, "0") == 0 ? "0 (ultimate/dGPU)"
                             : g_strcmp0 (s, "1") == 0 ? "1 (hybrid)"
                             : s) : NULL);
  g_free (s);
  /* which interface the value came from — kernel 6.17+ moved it to
   * asus-armoury, older kernels only have the platform file */
  {
    gchar *a = acc_dev_armoury_file ("gpu_mux_mode");
    r_field ("MUX interface", a ? "asus-armoury (firmware-attributes)"
                                : "asus-nb-wmi (legacy sysfs)");
    g_free (a);
  }
  r_field ("Restart pending", acc_dev_pending_reboot () ? "yes" : "no");

  /* ---------------- display ---------------- */
  g_print ("\nDISPLAY\n");
  DispInfo d;
  display_read (&d);
  if (d.ok)
    {
      s = g_strdup_printf ("%d × %d @ %.0f Hz", d.width, d.height,
                           d.refresh_hz);
      r_field ("Panel", s);
      g_free (s);
    }
  else
    r_field ("Panel", NULL);
  if (d.backlight_max > 0)
    {
      s = g_strdup_printf ("%d / %d", d.backlight_now, d.backlight_max);
      r_field ("Backlight", s);
      g_free (s);
    }
  else
    r_field ("Backlight", NULL);

  /* ---------------- battery ---------------- */
  g_print ("\nBATTERY\n");
  BatInfo b;
  battery_read (&b);
  if (b.ok)
    {
      s = g_strdup_printf ("%d%% (%s)", b.capacity, b.status);
      r_field ("Charge", s);
      g_free (s);
      s = g_strdup_printf ("%.1f Wh design / %.1f Wh full",
                           b.wh_design, b.wh_full);
      r_field ("Capacity", s);
      g_free (s);
      s = b.wh_design > 0
        ? g_strdup_printf ("%.0f%%", b.wh_full / b.wh_design * 100.0) : NULL;
      r_field ("Health", s);
      g_free (s);
      s = b.cycle_count >= 0 ? g_strdup_printf ("%d", b.cycle_count) : NULL;
      r_field ("Cycles", s);
      g_free (s);
      s = b.threshold > 0 ? g_strdup_printf ("%d%%", b.threshold) : NULL;
      r_field ("Charge limit", s);
      g_free (s);
    }
  else
    {
      r_field ("Charge", NULL);
      r_field ("Capacity", NULL);
      r_field ("Health", NULL);
      r_field ("Cycles", NULL);
      r_field ("Charge limit", NULL);
    }

  /* ---------------- system ---------------- */
  g_print ("\nSYSTEM\n");
  s = sysinfo_os ();
  r_field ("OS", s);
  g_free (s);
  r_field ("Kernel", un.release);

  /* ---------------- asusd service ---------------- */
  g_print ("\nASUSD SERVICE\n");
  r_field ("Service", acc_asusd_available () ? "running" : "not running");
  {
    AsusdProfileState ps;
    if (acc_asusd_profiles (&ps))
      {
        r_field ("Profile (live)", prof_name (ps.active));
        r_field ("Profile on AC", prof_name (ps.on_ac));
        r_field ("Profile on bat", prof_name (ps.on_bat));
        r_field ("Auto AC/battery", (ps.change_on_ac && ps.change_on_bat)
                                   ? "yes" : "no");
      }
    else
      {
        r_field ("Profile (live)", NULL);
        r_field ("Profile on AC", NULL);
        r_field ("Profile on bat", NULL);
        r_field ("Auto AC/battery", NULL);
      }
  }
  {
    AsusdGpuState gs;
    if (acc_asusd_gpu (&gs))
      {
        r_field ("MUX (asusd)", gs.current < 0 ? NULL
                  : gs.current == 0 ? "0 (ultimate/dGPU)" : "1 (hybrid)");
        r_field ("MUX queued", gs.queued < 0 ? "nothing"
                  : gs.queued == 0 ? "0 (ultimate/dGPU)" : "1 (hybrid)");
        r_field ("asus-shutdown", !gs.shutdown_known ? NULL
                  : gs.shutdown_ready ? "ready" : "disabled");
      }
    else
      {
        r_field ("MUX (asusd)", NULL);
        r_field ("MUX queued", NULL);
        r_field ("asus-shutdown", NULL);
      }
  }

  /* ---------------- capabilities ---------------- */
  g_print ("\nSUPPORTED CONTROLS\n");

  {
    gchar *ch = acc_read_trim
      ("/sys/firmware/acpi/platform_profile_choices");
    r_cap (ch != NULL, "Performance profiles",
           ch ? ch : "platform_profile not exposed");
    g_free (ch);
  }
  {
    gchar *f = acc_dev_charge_limit_file ();
    r_cap (f != NULL, "Charge limit",
           f ? "60 / 80 / 100 % via polkit helper" : "not exposed");
    g_free (f);
  }
  {
    gchar *f = acc_dev_wmi_file ("charge_mode");
    r_cap (f != NULL, "Charging policy",
           f ? "standard / balanced / max lifespan" : "not exposed");
    g_free (f);
  }
  {
    gchar *f = acc_dev_wmi_file ("gpu_mux_mode");
    r_cap (f != NULL, "GPU MUX switch",
           f ? "ultimate / hybrid (applies on reboot)" : "not exposed");
    g_free (f);
  }
  {
    gchar *f = acc_dev_wmi_file ("dgpu_disable");
    r_cap (f != NULL, "dGPU disable",
           f ? "eDP/ACPI flag present" : "not exposed");
    g_free (f);
  }
  {
    AccGfxState gx;
    acc_gfx_state (&gx);
    r_cap (acc_gfx_integrated_supported (&gx), "Integrated (iGPU only)",
           gx.has_dgpu_disable ? "dgpu_disable present, not verified on this model"
                               : "no firmware dGPU switch (dgpu_disable)");
    r_cap (gx.pm == ACC_DGPU_PM_SUSPENDED, "dGPU runtime power",
           gx.pm == ACC_DGPU_PM_SUSPENDED ? "suspended (D3)"
           : gx.pm == ACC_DGPU_PM_ACTIVE ? (gx.holders[0] ? gx.holders : "active")
           : "unknown");
    if (gx.supergfxd_running)
      r_cap (FALSE, "supergfxd", "running (can conflict with asusd)");
    if (gx.supergfx_leftover)
      r_cap (FALSE, "supergfxd leftover",
             "/etc/modprobe.d/supergfxd.conf blacklists nvidia");
  }
  {
    gchar *kbd = acc_dev_kbd_led_file ();
    gchar *v = kbd ? acc_read_trim (kbd) : NULL;
    r_cap (kbd != NULL, "Keyboard backlight",
           kbd ? "levels 0–3" : "not exposed");
    g_free (v);
    g_free (kbd);
  }
  {
    HwInfo h;
    hwmon_read (&h);
    r_cap (h.fans_ok, "Fan sensors",
           h.fans_ok ? "RPM monitoring only (no manual control)" : "not exposed");
    gchar *t = h.t_cpu >= 0 ? g_strdup_printf ("CPU %.1f °C, board %.1f °C",
                                               h.t_cpu, h.t_board) : NULL;
    r_cap (t != NULL, "Temperature sensors", t ? t : "not exposed");
    g_free (t);
  }
  {
    gdouble bl = acc_backlight_get ();
    gchar *d2 = bl >= 0
      ? g_strdup_printf ("current %.0f%%, set through logind", bl)
      : g_strdup ("no /sys/class/backlight device");
    r_cap (bl >= 0, "Backlight", d2);
    g_free (d2);
  }
  {
    AccSoftDim sd = acc_softdim_status ();
    gboolean ok = acc_brightness_available () && sd == ACC_SD_ON;
    gchar *d2 = ok
      ? g_strdup_printf ("current %.0f%%, min 10%%", acc_brightness_get ())
      : sd == ACC_SD_DISABLED
      ? g_strdup ("Soft Brightness Plus installed but not enabled")
      : g_strdup (acc_softdim_bundled ()
                  ? "not installed (bundled copy ready: Display page)"
                  : "Soft Brightness Plus extension not installed");
    r_cap (ok, "Software dimming", d2);
    g_free (d2);
  }
  {
    gboolean nl = acc_nightlight_available ();
    r_cap (nl, "Night light / Night color",
           nl ? "backend available" : "no supported backend on this DE");
  }
  {
    /* the helper lives on the host: a sandbox reaches it through the
     * agent, and says so instead of claiming it is missing */
    gboolean here = g_file_test ("/usr/libexec/asus-control-center/acc-helper",
                                  G_FILE_TEST_IS_EXECUTABLE);
    gboolean via_agent = !here && acc_host_agent_ready ();
    r_cap (here || via_agent, "Polkit helper (writes)",
           here ? "/usr/libexec/asus-control-center/acc-helper installed"
                : via_agent ? "on the host, through the host agent"
                            : "run ./install.sh with sudo");
  }

  r_cap (acc_user_selftest (), "Sign-in password hashing",
         acc_user_selftest () ? "PBKDF2-HMAC-SHA256 test vector OK"
                              : "PBKDF2 test vector FAILED");

  g_print ("\nNotes\n");
  g_print ("  • Values marked — could not be read on this machine.\n");
  g_print ("  • Writes go through asusd or the polkit helper (no root here).\n");
  g_print ("  • Nothing in this report writes to the hardware.\n");
}

/* ---------------------------------------------------------------- main */
static void
on_activate (GtkApplication *app, gpointer user)
{
  (void) user;
  gtk_widget_set_default_direction (acc_i18n_dir ());

  /* user-level GSettings bridge for the OLED flicker-free dim slider */
  acc_brightness_init (NULL, NULL);

  /* the local account decides between "create account", "sign in" and
   * straight-to-dashboard when the previous session is still signed in */
  acc_user_load ();

  if (g_getenv ("ACC_DEBUG"))
    g_printerr ("account: exists=%d signed_in=%d\n",
                acc_user_exists (), acc_user_signed_in ());

  if (acc_user_signed_in ())
    acc_ui_boot (app);          /* no login gate on later launches */
  else
    acc_login_show (app);       /* it calls acc_ui_boot() on success */

  /* dashboard refresh loop: cheap read-only sysfs/GSettings probes.
   * It no-ops until the dashboard window exists. */
  if (!refresh_src)
    refresh_src = g_timeout_add_seconds (2, refresh_tick, NULL);
}

static void
on_shutdown (GtkApplication *app, gpointer user)
{
  (void) app;
  (void) user;
  if (refresh_src)
    {
      g_source_remove (refresh_src);
      refresh_src = 0;
    }
  acc_brightness_shutdown ();
}

/* `--check`: assertion tests for the pure logic behind hardware decisions.
 * No hardware is touched; returns the number of failures. */
static int s_fail = 0;

static void
expect (gboolean ok, const gchar *what)
{
  g_print ("  %s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok)
    s_fail++;
}

static int
unit_checks (void)
{
  const gchar *b1 = "11111111-2222-3333-4444-555555555555";
  const gchar *b2 = "aaaaaaaa-2222-3333-4444-555555555555";

  g_print ("MUX request lifecycle\n");
  expect (acc_muxreq_classify (0, b1, b1, 1) == ACC_MUXREQ_PENDING,
          "same boot is pending, even though the live value differs");
  expect (acc_muxreq_classify (0, b1, b2, 0) == ACC_MUXREQ_VERIFIED,
          "later boot running the requested mode is verified");
  expect (acc_muxreq_classify (0, b1, b2, 1) == ACC_MUXREQ_FAILED,
          "later boot still in the old mode is failed");
  expect (acc_muxreq_classify (1, b1, b2, -1) == ACC_MUXREQ_NONE,
          "unknown live value claims nothing");
  expect (acc_muxreq_classify (2, b1, b2, 1) == ACC_MUXREQ_NONE,
          "out-of-range request is ignored");
  expect (acc_muxreq_classify (1, NULL, b2, 1) == ACC_MUXREQ_NONE,
          "record without a boot id is ignored");

  g_print ("MUX failure reason from the journal\n");
  const gchar *log =
    "[INFO  asus_shutdown] Applying deferred GPU attribute gpu_mux_mode = 0\n"
    "asus_armoury: Failed to set current_value: (result): 0x0\n"
    "[ERROR asusd::asus_armoury] Could not apply queued GPU attribute "
    "gpu_mux_mode = 0: Io(Os { code: 5 })\n";
  gchar *r = acc_muxreq_reason_from_log (log);
  expect (r && g_str_has_prefix (r, "[ERROR asusd::asus_armoury] Could not"),
          "asusd's line wins over the kernel line");
  g_free (r);
  r = acc_muxreq_reason_from_log ("asus_armoury: Failed to set current_value: (result): 0x0\n");
  expect (r && strstr (r, "Failed to set current_value"),
          "kernel line is used when asusd said nothing");
  g_free (r);
  r = acc_muxreq_reason_from_log ("gpu_mux_mode changed\n");
  expect (r == NULL, "no failure line gives no reason");
  g_free (r);
  expect (acc_muxreq_reason_from_log (NULL) == NULL, "NULL log is safe");

  g_print ("supergfxd leftover detection\n");
  expect (acc_gfx_text_blacklists_nvidia ("# Automatically generated by supergfxd\n"
                                          "blacklist nouveau\nblacklist nvidia_drm\n"
                                          "blacklist nvidia\n"),
          "supergfxd's Integrated file blacklists nvidia");
  expect (!acc_gfx_text_blacklists_nvidia ("blacklist nouveau\noptions nvidia-drm modeset=1\n"),
          "nouveau-only blacklist is not an nvidia blacklist");
  expect (!acc_gfx_text_blacklists_nvidia ("blacklist nvidia_drm\n"),
          "nvidia_drm alone does not count");
  expect (acc_gfx_text_blacklists_nvidia ("  blacklist\tnvidia  \n"),
          "whitespace variants are recognised");
  expect (!acc_gfx_text_blacklists_nvidia (NULL), "NULL text is safe");

  g_print ("Integrated mode is never offered unverified\n");
  AccGfxState st;
  memset (&st, 0, sizeof st);
  st.has_dgpu_disable = TRUE;
  expect (!acc_gfx_integrated_supported (&st),
          "an unverified dgpu_disable attribute is not used");

  g_print ("the host command table\n");
  expect (acc_command_from_id ("service-active") == ACC_CMD_SERVICE_ACTIVE,
          "a command id maps back to its command");
  expect (acc_command_from_id ("no-such-command") == -1,
          "an unknown command id is refused");
  expect (acc_command_from_id (NULL) == -1, "a NULL command id is refused");
  {
    gboolean distinct = TRUE;
    for (gint i = 0; i < ACC_CMD_COUNT && distinct; i++)
      for (gint j = i + 1; j < ACC_CMD_COUNT; j++)
        if (g_strcmp0 (acc_command_id ((AccCommand) i),
                       acc_command_id ((AccCommand) j)) == 0)
          distinct = FALSE;
    expect (distinct, "every command has its own id");
  }
  expect (acc_command_arg_ok (ACC_CMD_SERVICE_ACTIVE, "asusd"),
          "asusd is an allowed service");
  expect (!acc_command_arg_ok (ACC_CMD_SERVICE_ACTIVE, "sshd"),
          "another service is refused");
  expect (!acc_command_arg_ok (ACC_CMD_WIFI_SSID, "-rfkill"),
          "an argument that looks like a flag is refused");
  expect (!acc_command_arg_ok (ACC_CMD_SMART_HEALTH, "/dev/sdb"),
          "an unknown block device is refused");
  expect (!acc_command_arg_ok (ACC_CMD_GNOME_EXT_ENABLE, "not-a-uuid"),
          "a string that is not an extension uuid is refused");

  g_print ("the helper whitelist\n");
  expect (acc_command_helper_action_ok ("set-perf"),
          "set-perf is one of the helper's verbs");
  expect (!acc_command_helper_action_ok ("set-anything"),
          "an unknown verb is refused");
  expect (acc_command_helper_value_ok ("set-charge", "80"),
          "80 is an allowed charge limit");
  expect (!acc_command_helper_value_ok ("set-charge", "55"),
          "55 is not an allowed charge limit");
  expect (!acc_command_helper_value_ok ("set-kbd", "9"),
          "9 is not an allowed keyboard level");

  g_print ("the touchpad backend\n");
  expect (acc_touchpad_available ()
          || acc_touchpad_backend ()[0] == '\0',
          "the touchpad reports a backend or reports none");

  g_print ("\n%d failure(s)\n", s_fail);
  return s_fail;
}

int
main (int argc, char **argv)
{
  /* The environment's locale decides how the console output is encoded.
   * GTK does this when the interface starts; the read-only reports have
   * to do it themselves, or their dashes come out as question marks. */
  setlocale (LC_ALL, "");

  if (argc > 1 && (strcmp (argv[1], "--version") == 0
                   || strcmp (argv[1], "-v") == 0))
    {
      g_print ("asuscraft %s\n", ACC_VERSION);
      return 0;
    }

  if (argc > 1 && strcmp (argv[1], "--check") == 0)
    return unit_checks () ? 1 : 0;

  if (argc > 1 && (strcmp (argv[1], "--self-test") == 0
                   || strcmp (argv[1], "--specs") == 0))
    {
      self_test ();
      acc_brightness_shutdown ();
      /* non-zero so `make test` fails when the sign-in crypto or System Manager self-test is broken */
      return (acc_user_selftest () && acc_sysmgr_selftest ()) ? 0 : 1;
    }

  /* arabic is the default language of the project */
#ifdef ACC_LANG_AR
  acc_i18n_set (ACC_LANG_AR);
#endif
  /* ...unless the user picked EN/ع in an earlier session */
  acc_i18n_load_pref ();

  AdwApplication *app = ADW_APPLICATION
    (g_object_new (ADW_TYPE_APPLICATION,
                   "application-id", APP_ID,
                   "flags", G_APPLICATION_NON_UNIQUE,
                   NULL));

  g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);
  g_signal_connect (app, "shutdown", G_CALLBACK (on_shutdown), NULL);

  int status = g_application_run (G_APPLICATION (app), argc, argv);
  g_object_unref (app);
  return status;
}
