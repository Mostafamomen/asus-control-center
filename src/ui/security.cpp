/* ui/security.cpp - Security Center
 *
 * A read-only overview of the system's security posture, plus manual,
 * opt-in security scans. SAFE BY DEFAULT:
 *   - Every check only READS system state (sysfs, systemctl/firewall-cmd
 *     status, getenforce, package-manager metadata, gsettings). Nothing is
 *     changed: no firewall rules, no SELinux mode, no Secure Boot keys, no
 *     kernel parameters, no packages.
 *   - A check never reports "Secure" when it could not run; it reports
 *     Unknown or Unsupported instead.
 *   - Scans (ClamAV/Lynis/rkhunter/chkrootkit/AIDE) run only when the user
 *     starts them, with a confirmation, a timeout and a cancel button; they
 *     never auto-install, quarantine or delete. (Scan runner: Stage C.)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "security.h"
#include "theme.h"
#include "mcui.h"
#include "window.h"
#include "../i18n/i18n.h"
#include "../control/ctl.h"
#include <adwaita.h>
#include <glib.h>
#include <string.h>
#include <initializer_list>
#include <vector>

/* ------------------------------------------------------------ states */
typedef enum { SEC_SECURE, SEC_WARN, SEC_CRIT, SEC_UNKNOWN, SEC_UNSUP } SecState;

/* a check's widgets: status pill + one-line summary + expandable detail */
typedef struct { GtkWidget *pill; GtkWidget *summary; GtkWidget *detail; } Check;

/* overview + per-tab checks */
static Check c_sb, c_fw, c_selinux, c_upd, c_kernel, c_services;      /* overview */
static Check c_fw_full, c_bk_sb, c_bk_kernel, c_nvidia, c_signing;   /* tabs */
static Check c_sources, c_outdated, c_flatpak;
static Check c_screenlock, c_autologin, c_netsvc, c_autostart;
static GtkWidget *s_lastcheck;

static GtkStack *s_stack;
static GtkWidget *s_tabs[6];

/* ----------------------------------------------------------- helpers */
static gchar *
run_cmd (const gchar *const *argv)
{
  gchar *out = NULL;
  if (g_spawn_sync (NULL, (gchar **) argv, NULL,
                    (GSpawnFlags) (G_SPAWN_SEARCH_PATH
                                   | G_SPAWN_STDERR_TO_DEV_NULL),
                    NULL, NULL, &out, NULL, NULL, NULL))
    return out;
  g_free (out);
  return NULL;
}

static gchar *
run_cmd (std::initializer_list<const gchar *> args)
{
  std::vector<const gchar *> v (args);
  v.push_back (NULL);
  return run_cmd (v.data ());
}

static gchar *
run_sh (const gchar *script)
{
  return run_cmd ({ "sh", "-c", script, NULL });
}

static gboolean
have (const gchar *cmd)
{
  gchar *p = g_find_program_in_path (cmd);
  gboolean ok = p != NULL;
  g_free (p);
  return ok;
}

/* trimmed first line of command output ("" on failure) */
static gchar *
first_line (gchar *s)
{
  if (!s)
    return g_strdup ("");
  gchar *nl = strchr (s, '\n');
  if (nl)
    *nl = '\0';
  gchar *r = g_strdup (g_strstrip (s));
  return r;
}

/* ------------------------------------------- results computed off-thread */
typedef struct {
  SecState sb;        gchar sb_d[256];
  SecState fw;        gchar fw_d[128];
  gchar fw_zones[256], fw_services[256], fw_ports[256], fw_startup[64];
  SecState selinux;   gchar selinux_d[128];
  SecState upd;       gchar upd_d[128];
  SecState kernel;    gchar kernel_d[128];
  SecState services;  gchar services_d[256];
  SecState nvidia;    gchar nvidia_d[128];
  SecState signing;   gchar signing_d[160];
  SecState sources;   gchar sources_d[160];
  SecState outdated;  gchar outdated_d[128];
  SecState flatpak;   gchar flatpak_d[128];
  SecState screenlock;gchar screenlock_d[160];
  SecState autologin; gchar autologin_d[160];
  SecState netsvc;    gchar netsvc_d[160];
  SecState autostart; gchar autostart_d[160];
} SecResults;

static void
cp (gchar *dst, gsize n, const gchar *src)
{
  g_strlcpy (dst, src ? src : "", n);
}

/* ---- individual checks (all read-only) ---- */
static void
check_secureboot (SecResults *r)
{
  if (!g_file_test ("/sys/firmware/efi", G_FILE_TEST_IS_DIR))
    {
      r->sb = SEC_UNSUP;
      cp (r->sb_d, sizeof r->sb_d, "Legacy BIOS boot — Secure Boot does not apply.");
      return;
    }
  if (have ("mokutil"))
    {
      gchar *o = run_cmd ({ "mokutil", "--sb-state", NULL });
      if (o && *o)
        {
          gchar *l = first_line (o);
          if (g_strstr_len (l, -1, "enabled"))
            { r->sb = SEC_SECURE; cp (r->sb_d, sizeof r->sb_d, "Secure Boot is enabled."); }
          else if (g_strstr_len (l, -1, "disabled"))
            { r->sb = SEC_WARN; cp (r->sb_d, sizeof r->sb_d, "Secure Boot is supported but disabled."); }
          else
            { r->sb = SEC_UNKNOWN; cp (r->sb_d, sizeof r->sb_d, l); }
          g_free (l);
          g_free (o);
          return;
        }
      g_free (o);
    }
  /* fall back to the EFI variable (read-only) */
  gchar *o = run_sh ("od -An -t u1 /sys/firmware/efi/efivars/SecureBoot-"
                     "8be4df61-93ca-11d2-aa0d-00e098032b8c 2>/dev/null "
                     "| awk '{print $NF}'");
  gchar *l = first_line (o);
  if (g_strcmp0 (l, "1") == 0)
    { r->sb = SEC_SECURE; cp (r->sb_d, sizeof r->sb_d, "Secure Boot is enabled."); }
  else if (g_strcmp0 (l, "0") == 0)
    { r->sb = SEC_WARN; cp (r->sb_d, sizeof r->sb_d, "Secure Boot is supported but disabled."); }
  else
    { r->sb = SEC_UNKNOWN; cp (r->sb_d, sizeof r->sb_d, "Could not read Secure Boot state (mokutil not installed)."); }
  g_free (l);
  g_free (o);
}

static void
check_firewall (SecResults *r)
{
  cp (r->fw_zones, sizeof r->fw_zones, "—");
  cp (r->fw_services, sizeof r->fw_services, "—");
  cp (r->fw_ports, sizeof r->fw_ports, "—");
  cp (r->fw_startup, sizeof r->fw_startup, "—");

  if (!have ("firewall-cmd") && !g_file_test ("/usr/sbin/firewalld", G_FILE_TEST_EXISTS))
    {
      r->fw = SEC_UNSUP;
      cp (r->fw_d, sizeof r->fw_d, "firewalld is not installed.");
      return;
    }
  gchar *active = run_cmd ({ "systemctl", "is-active", "firewalld", NULL });
  gchar *al = first_line (active);
  gboolean running = g_strcmp0 (al, "active") == 0;
  g_free (al); g_free (active);

  gchar *en = run_cmd ({ "systemctl", "is-enabled", "firewalld", NULL });
  gchar *enl = first_line (en);
  cp (r->fw_startup, sizeof r->fw_startup, enl && *enl ? enl : "—");
  g_free (enl); g_free (en);

  if (!running)
    {
      r->fw = SEC_CRIT;
      cp (r->fw_d, sizeof r->fw_d, "firewalld is installed but not running.");
      return;
    }
  r->fw = SEC_SECURE;
  cp (r->fw_d, sizeof r->fw_d, "firewalld is active.");

  if (have ("firewall-cmd"))
    {
      gchar *z = run_sh ("firewall-cmd --get-active-zones 2>/dev/null | awk 'NF && $1!~/:/{printf \"%s \",$1}'");
      if (z && *g_strchomp (z)) cp (r->fw_zones, sizeof r->fw_zones, g_strstrip (z));
      g_free (z);
      gchar *s = run_sh ("firewall-cmd --list-services 2>/dev/null");
      gchar *sl = first_line (s);
      if (*sl) cp (r->fw_services, sizeof r->fw_services, sl);
      g_free (sl); g_free (s);
      gchar *p = run_sh ("firewall-cmd --list-ports 2>/dev/null");
      gchar *pl = first_line (p);
      cp (r->fw_ports, sizeof r->fw_ports, *pl ? pl : "none");
      g_free (pl); g_free (p);
    }
}

static void
check_selinux (SecResults *r)
{
  if (have ("getenforce"))
    {
      gchar *o = run_cmd ({ "getenforce", NULL });
      gchar *l = first_line (o);
      if (g_ascii_strcasecmp (l, "Enforcing") == 0)
        { r->selinux = SEC_SECURE; cp (r->selinux_d, sizeof r->selinux_d, "SELinux is enforcing."); }
      else if (g_ascii_strcasecmp (l, "Permissive") == 0)
        { r->selinux = SEC_WARN; cp (r->selinux_d, sizeof r->selinux_d, "SELinux is permissive (logging only)."); }
      else if (g_ascii_strcasecmp (l, "Disabled") == 0)
        { r->selinux = SEC_WARN; cp (r->selinux_d, sizeof r->selinux_d, "SELinux is disabled."); }
      else
        { r->selinux = SEC_UNKNOWN; cp (r->selinux_d, sizeof r->selinux_d, l); }
      g_free (l); g_free (o);
      return;
    }
  /* no SELinux (e.g. Ubuntu/AppArmor systems) */
  if (have ("aa-status") || g_file_test ("/sys/kernel/security/apparmor", G_FILE_TEST_IS_DIR))
    { r->selinux = SEC_UNSUP; cp (r->selinux_d, sizeof r->selinux_d, "SELinux not in use; this system uses AppArmor."); }
  else
    { r->selinux = SEC_UNSUP; cp (r->selinux_d, sizeof r->selinux_d, "SELinux tools are not installed."); }
}

static void
check_updates (SecResults *r)
{
  if (have ("dnf"))
    {
      gchar *o = run_sh ("dnf -q updateinfo list --security 2>/dev/null | grep -c .");
      gchar *l = first_line (o);
      int n = atoi (l);
      g_free (l); g_free (o);
      if (n > 0)
        { r->upd = SEC_WARN; g_snprintf (r->upd_d, sizeof r->upd_d, "%d security update(s) available.", n); }
      else
        { r->upd = SEC_SECURE; cp (r->upd_d, sizeof r->upd_d, "No pending security updates."); }
      return;
    }
  if (have ("apt"))
    {
      gchar *o = run_sh ("apt list --upgradable 2>/dev/null | grep -ic security");
      gchar *l = first_line (o);
      int n = atoi (l);
      g_free (l); g_free (o);
      if (n > 0)
        { r->upd = SEC_WARN; g_snprintf (r->upd_d, sizeof r->upd_d, "%d security update(s) available.", n); }
      else
        { r->upd = SEC_SECURE; cp (r->upd_d, sizeof r->upd_d, "No pending security updates detected."); }
      return;
    }
  r->upd = SEC_UNKNOWN;
  cp (r->upd_d, sizeof r->upd_d, "No supported package manager found.");
}

static void
check_kernel (SecResults *r)
{
  gchar *o = run_cmd ({ "uname", "-r", NULL });
  gchar *l = first_line (o);
  g_snprintf (r->kernel_d, sizeof r->kernel_d, "Running kernel %s.", *l ? l : "?");
  r->kernel = *l ? SEC_SECURE : SEC_UNKNOWN;
  /* a newer kernel installed than the one running = reboot recommended */
  if (have ("rpm"))
    {
      gchar *latest = run_sh ("rpm -q --last kernel-core 2>/dev/null | head -1 | sed 's/^kernel-core-//; s/ .*//'");
      gchar *ll = first_line (latest);
      if (*ll && !g_str_has_prefix (ll, l) && !strstr (l, ll))
        {
          r->kernel = SEC_WARN;
          g_snprintf (r->kernel_d, sizeof r->kernel_d,
                      "Running %s; %s is installed — reboot to apply.", l, ll);
        }
      g_free (ll); g_free (latest);
    }
  g_free (l); g_free (o);
}

static void
check_services (SecResults *r)
{
  GString *s = g_string_new (NULL);
  const gchar *svc[] = { "firewalld", "auditd", "fail2ban", "apparmor" };
  int on = 0;
  for (gsize i = 0; i < G_N_ELEMENTS (svc); i++)
    {
      gchar *argv_out = run_cmd ({ "systemctl", "is-active", svc[i], NULL });
      gchar *l = first_line (argv_out);
      if (g_strcmp0 (l, "active") == 0)
        { g_string_append_printf (s, "%s ", svc[i]); on++; }
      g_free (l); g_free (argv_out);
    }
  if (on > 0)
    { r->services = SEC_SECURE; g_snprintf (r->services_d, sizeof r->services_d, "Active: %s", g_strstrip (s->str)); }
  else
    { r->services = SEC_WARN; cp (r->services_d, sizeof r->services_d, "No common security services are active."); }
  g_string_free (s, TRUE);
}

static void
check_nvidia (SecResults *r)
{
  if (acc_ctl_nvidia_loaded ())
    { r->nvidia = SEC_SECURE; cp (r->nvidia_d, sizeof r->nvidia_d, "NVIDIA kernel module is loaded."); }
  else
    { r->nvidia = SEC_UNKNOWN; cp (r->nvidia_d, sizeof r->nvidia_d, "NVIDIA kernel module is not loaded."); }
}

static void
check_signing (SecResults *r)
{
  gchar *o = run_sh ("cat /sys/module/module/parameters/sig_enforce 2>/dev/null");
  gchar *l = first_line (o);
  if (g_strcmp0 (l, "Y") == 0)
    { r->signing = SEC_SECURE; cp (r->signing_d, sizeof r->signing_d, "Kernel enforces module signatures."); }
  else if (g_strcmp0 (l, "N") == 0)
    { r->signing = SEC_WARN; cp (r->signing_d, sizeof r->signing_d, "Kernel does not enforce module signatures."); }
  else
    { r->signing = SEC_UNKNOWN; cp (r->signing_d, sizeof r->signing_d, "Module-signature enforcement is not reported."); }
  g_free (l); g_free (o);
}

static void
check_sources (SecResults *r)
{
  if (have ("dnf"))
    {
      gchar *o = run_sh ("dnf -q repolist --enabled 2>/dev/null | tail -n +2 | grep -c .");
      gchar *l = first_line (o);
      int n = atoi (l);
      g_free (l); g_free (o);
      r->sources = SEC_SECURE;
      g_snprintf (r->sources_d, sizeof r->sources_d, "%d enabled DNF repositories.", n);
    }
  else if (have ("apt"))
    {
      gchar *o = run_sh ("ls /etc/apt/sources.list /etc/apt/sources.list.d/*.list /etc/apt/sources.list.d/*.sources 2>/dev/null | wc -l");
      gchar *l = first_line (o);
      r->sources = SEC_SECURE;
      g_snprintf (r->sources_d, sizeof r->sources_d, "%s APT source file(s).", l);
      g_free (l); g_free (o);
    }
  else
    { r->sources = SEC_UNKNOWN; cp (r->sources_d, sizeof r->sources_d, "No supported package manager found."); }
}

static void
check_outdated (SecResults *r)
{
  if (have ("dnf"))
    {
      gchar *o = run_sh ("dnf -q check-update 2>/dev/null | grep -c .");
      gchar *l = first_line (o);
      int n = atoi (l);
      g_free (l); g_free (o);
      r->outdated = n > 0 ? SEC_WARN : SEC_SECURE;
      g_snprintf (r->outdated_d, sizeof r->outdated_d, "%d updatable package(s).", n);
    }
  else if (have ("apt"))
    {
      gchar *o = run_sh ("apt list --upgradable 2>/dev/null | tail -n +2 | grep -c .");
      gchar *l = first_line (o);
      int n = atoi (l);
      g_free (l); g_free (o);
      r->outdated = n > 0 ? SEC_WARN : SEC_SECURE;
      g_snprintf (r->outdated_d, sizeof r->outdated_d, "%d updatable package(s).", n);
    }
  else
    { r->outdated = SEC_UNKNOWN; cp (r->outdated_d, sizeof r->outdated_d, "No supported package manager found."); }
}

static void
check_flatpak (SecResults *r)
{
  if (!have ("flatpak"))
    { r->flatpak = SEC_UNSUP; cp (r->flatpak_d, sizeof r->flatpak_d, "Flatpak is not installed."); return; }
  gchar *o = run_sh ("flatpak remote-ls --updates 2>/dev/null | grep -c .");
  gchar *l = first_line (o);
  int n = atoi (l);
  g_free (l); g_free (o);
  r->flatpak = n > 0 ? SEC_WARN : SEC_SECURE;
  g_snprintf (r->flatpak_d, sizeof r->flatpak_d, "%d Flatpak update(s) available.", n);
}

/* read a GSettings boolean safely (schema may be absent) */
static gboolean
gs_bool (const gchar *schema, const gchar *key, gboolean *found)
{
  if (found) *found = FALSE;
  GSettingsSchemaSource *src = g_settings_schema_source_get_default ();
  if (!src) return FALSE;
  GSettingsSchema *sch = g_settings_schema_source_lookup (src, schema, TRUE);
  if (!sch) return FALSE;
  if (!g_settings_schema_has_key (sch, key)) { g_settings_schema_unref (sch); return FALSE; }
  GSettings *st = g_settings_new_full (sch, NULL, NULL);
  g_settings_schema_unref (sch);
  gboolean v = g_settings_get_boolean (st, key);
  g_object_unref (st);
  if (found) *found = TRUE;
  return v;
}

static void
check_screenlock (SecResults *r)
{
  gboolean found = FALSE;
  gboolean locked = gs_bool ("org.gnome.desktop.screensaver", "lock-enabled", &found);
  if (!found)
    { r->screenlock = SEC_UNKNOWN; cp (r->screenlock_d, sizeof r->screenlock_d, "Screen-lock setting not readable on this desktop."); return; }
  if (locked)
    { r->screenlock = SEC_SECURE; cp (r->screenlock_d, sizeof r->screenlock_d, "Screen lock is enabled."); }
  else
    { r->screenlock = SEC_WARN; cp (r->screenlock_d, sizeof r->screenlock_d, "Screen lock is disabled."); }
}

static void
check_autologin (SecResults *r)
{
  /* GDM/SDDM config files are world-readable; we only read them */
  gchar *o = run_sh ("grep -hiE '^(AutomaticLoginEnable|AutoLogin)' "
                     "/etc/gdm/custom.conf /etc/gdm3/custom.conf "
                     "/etc/sddm.conf /etc/sddm.conf.d/*.conf 2>/dev/null | tr -d ' '");
  gchar *l = first_line (o);
  gchar *low = g_ascii_strdown (l, -1);
  if (!*l)
    { r->autologin = SEC_SECURE; cp (r->autologin_d, sizeof r->autologin_d, "Automatic login is not configured."); }
  else if (strstr (low, "true") || strstr (low, "=1") || (strstr (low, "autologin") && !strstr (low, "false")))
    { r->autologin = SEC_WARN; cp (r->autologin_d, sizeof r->autologin_d, "Automatic login appears to be enabled."); }
  else
    { r->autologin = SEC_SECURE; cp (r->autologin_d, sizeof r->autologin_d, "Automatic login is disabled."); }
  g_free (low); g_free (l); g_free (o);
}

static void
check_netsvc (SecResults *r)
{
  if (!have ("ss"))
    { r->netsvc = SEC_UNKNOWN; cp (r->netsvc_d, sizeof r->netsvc_d, "ss is not available to list listening sockets."); return; }
  /* count sockets listening on non-loopback addresses */
  gchar *o = run_sh ("ss -tulnH 2>/dev/null | awk '{print $5}' "
                     "| grep -vE '127\\.0\\.0\\.1|\\[::1\\]|::1' | grep -c .");
  gchar *l = first_line (o);
  int n = atoi (l);
  g_free (l); g_free (o);
  if (n == 0)
    { r->netsvc = SEC_SECURE; cp (r->netsvc_d, sizeof r->netsvc_d, "No services listening on external interfaces."); }
  else
    { r->netsvc = SEC_WARN; g_snprintf (r->netsvc_d, sizeof r->netsvc_d, "%d service(s) listening on non-loopback addresses.", n); }
}

static void
check_autostart (SecResults *r)
{
  gchar *dir = g_build_filename (g_get_user_config_dir (), "autostart", NULL);
  guint n = 0;
  GDir *d = g_dir_open (dir, 0, NULL);
  if (d)
    {
      const gchar *e;
      while ((e = g_dir_read_name (d)))
        if (g_str_has_suffix (e, ".desktop")) n++;
      g_dir_close (d);
    }
  g_free (dir);
  r->autostart = SEC_SECURE;
  g_snprintf (r->autostart_d, sizeof r->autostart_d, "%u user autostart entr%s. Review them in System Tools.", n, n == 1 ? "y" : "ies");
}

/* --------------------------------------------------- apply to widgets */
static void
sec_apply (Check *c, SecState st, const gchar *detail)
{
  if (!c || !c->pill)
    return;
  AccStr label; const gchar *tone;
  switch (st)
    {
    case SEC_SECURE: label = ACC_T_SEC_SECURE;   tone = "on";   break;
    case SEC_WARN:   label = ACC_T_SEC_WARNING;  tone = "soon"; break;
    case SEC_CRIT:   label = ACC_T_SEC_CRITICAL; tone = "bad";  break;
    case SEC_UNSUP:  label = ACC_T_SEC_UNSUP;    tone = "off";  break;
    default:         label = ACC_T_SEC_UNKNOWN;  tone = "off";  break;
    }
  gtk_label_set_text (GTK_LABEL (c->pill), acc_tr (label));
  for (const char *k : { "on", "soon", "bad", "off" })
    gtk_widget_remove_css_class (c->pill, k);
  gtk_widget_add_css_class (c->pill, tone);
  if (c->summary && detail)
    gtk_label_set_text (GTK_LABEL (c->summary), detail);
  if (c->detail && detail)
    gtk_label_set_text (GTK_LABEL (c->detail), detail);
}

static gboolean
results_apply (gpointer data)
{
  SecResults *r = (SecResults *) data;
  sec_apply (&c_sb, r->sb, r->sb_d);
  sec_apply (&c_fw, r->fw, r->fw_d);
  sec_apply (&c_selinux, r->selinux, r->selinux_d);
  sec_apply (&c_upd, r->upd, r->upd_d);
  sec_apply (&c_kernel, r->kernel, r->kernel_d);
  sec_apply (&c_services, r->services, r->services_d);

  /* firewall tab detail rows */
  sec_apply (&c_fw_full, r->fw, r->fw_d);
  if (c_fw_full.detail)
    {
      gchar *t = g_strdup_printf ("%s\nZones: %s\nServices: %s\nPorts: %s\nStart at boot: %s",
                                  r->fw_d, r->fw_zones, r->fw_services, r->fw_ports, r->fw_startup);
      gtk_label_set_text (GTK_LABEL (c_fw_full.detail), t);
      g_free (t);
    }
  sec_apply (&c_bk_sb, r->sb, r->sb_d);
  sec_apply (&c_bk_kernel, r->kernel, r->kernel_d);
  sec_apply (&c_nvidia, r->nvidia, r->nvidia_d);
  sec_apply (&c_signing, r->signing, r->signing_d);
  sec_apply (&c_sources, r->sources, r->sources_d);
  sec_apply (&c_outdated, r->outdated, r->outdated_d);
  sec_apply (&c_flatpak, r->flatpak, r->flatpak_d);
  sec_apply (&c_screenlock, r->screenlock, r->screenlock_d);
  sec_apply (&c_autologin, r->autologin, r->autologin_d);
  sec_apply (&c_netsvc, r->netsvc, r->netsvc_d);
  sec_apply (&c_autostart, r->autostart, r->autostart_d);

  if (s_lastcheck)
    {
      GDateTime *now = g_date_time_new_now_local ();
      gchar *ts = g_date_time_format (now, "%H:%M");
      gchar *t = g_strdup_printf ("%s %s", acc_tr (ACC_T_SEC_LASTCHECK), ts);
      gtk_label_set_text (GTK_LABEL (s_lastcheck), t);
      g_free (t); g_free (ts); g_date_time_unref (now);
    }
  g_free (r);
  return G_SOURCE_REMOVE;
}

static gpointer
scan_thread (gpointer data)
{
  (void) data;
  SecResults *r = g_new0 (SecResults, 1);
  check_secureboot (r);
  check_firewall (r);
  check_selinux (r);
  check_updates (r);
  check_kernel (r);
  check_services (r);
  check_nvidia (r);
  check_signing (r);
  check_sources (r);
  check_outdated (r);
  check_flatpak (r);
  check_screenlock (r);
  check_autologin (r);
  check_netsvc (r);
  check_autostart (r);
  g_idle_add (results_apply, r);
  return NULL;
}

static void
run_all_checks (void)
{
  if (!c_sb.pill)
    return;
  g_thread_new ("sec-checks", scan_thread, NULL);
}

static void
recheck_cb (GtkButton *b, gpointer u)
{
  (void) b; (void) u;
  run_all_checks ();
}

/* ------------------------------------------------------ UI builders */
/* a compact check card: badge + title header, status pill, summary, and a
 * collapsible "Details" expander */
static GtkWidget *
check_card (const gchar *icon, AccStr title, Check *out)
{
  GtkWidget *body = NULL;
  GtkWidget *c = mc_card_titled (icon, acc_tr (title), &body);

  GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  out->summary = mc_label ("…", "dim", 0);
  gtk_label_set_wrap (GTK_LABEL (out->summary), TRUE);
  gtk_widget_set_hexpand (out->summary, TRUE);
  gtk_box_append (GTK_BOX (row), out->summary);
  out->pill = mc_label (acc_tr (ACC_T_SEC_UNKNOWN), "pill off", 1);
  gtk_widget_set_valign (out->pill, GTK_ALIGN_START);
  gtk_box_append (GTK_BOX (row), out->pill);
  gtk_box_append (GTK_BOX (body), row);
  out->detail = NULL;
  return c;
}

/* a check card that also exposes a details expander (firewall tab) */
static GtkWidget *
check_card_detailed (const gchar *icon, AccStr title, Check *out)
{
  GtkWidget *c = check_card (icon, title, out);
  GtkWidget *exp = gtk_expander_new (acc_tr (ACC_T_SEC_DETAILS));
  out->detail = mc_label ("…", "dim", 0);
  gtk_label_set_wrap (GTK_LABEL (out->detail), TRUE);
  gtk_label_set_selectable (GTK_LABEL (out->detail), TRUE);
  gtk_widget_set_margin_top (out->detail, 6);
  gtk_expander_set_child (GTK_EXPANDER (exp), out->detail);
  gtk_box_append (GTK_BOX (c), exp);
  return c;
}

static GtkWidget *
tab_overview (void)
{
  GtkWidget *grid = mc_grid (1, 2);
  gtk_flow_box_append (GTK_FLOW_BOX (grid), check_card ("application-x-firmware-symbolic", ACC_T_SEC_SECUREBOOT, &c_sb));
  gtk_flow_box_append (GTK_FLOW_BOX (grid), check_card ("network-wired-symbolic", ACC_T_SEC_FIREWALL, &c_fw));
  gtk_flow_box_append (GTK_FLOW_BOX (grid), check_card ("security-high-symbolic", ACC_T_SEC_SELINUX, &c_selinux));
  gtk_flow_box_append (GTK_FLOW_BOX (grid), check_card ("software-update-available-symbolic", ACC_T_SEC_UPDATES, &c_upd));
  gtk_flow_box_append (GTK_FLOW_BOX (grid), check_card ("application-x-executable-symbolic", ACC_T_SEC_KERNEL, &c_kernel));
  gtk_flow_box_append (GTK_FLOW_BOX (grid), check_card ("emblem-system-symbolic", ACC_T_SEC_SERVICES, &c_services));
  return grid;
}

static GtkWidget *
tab_firewall (void)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 14);
  gtk_box_append (GTK_BOX (box), check_card_detailed ("network-wired-symbolic", ACC_T_SEC_FIREWALL, &c_fw_full));
  GtkWidget *link = gtk_link_button_new_with_label (
      "https://docs.fedoraproject.org/en-US/quick-docs/firewalld/",
      acc_tr (ACC_T_SEC_FW_CONFIG));
  gtk_widget_set_halign (link, GTK_ALIGN_START);
  gtk_box_append (GTK_BOX (box), link);
  return box;
}

static GtkWidget *
tab_boot (void)
{
  GtkWidget *grid = mc_grid (1, 2);
  gtk_flow_box_append (GTK_FLOW_BOX (grid), check_card ("application-x-firmware-symbolic", ACC_T_SEC_SECUREBOOT, &c_bk_sb));
  gtk_flow_box_append (GTK_FLOW_BOX (grid), check_card ("application-x-executable-symbolic", ACC_T_SEC_KERNEL, &c_bk_kernel));
  gtk_flow_box_append (GTK_FLOW_BOX (grid), check_card ("video-display-symbolic", ACC_T_SEC_NVIDIA, &c_nvidia));
  gtk_flow_box_append (GTK_FLOW_BOX (grid), check_card ("channel-secure-symbolic", ACC_T_SEC_SIGNING, &c_signing));
  return grid;
}

static GtkWidget *
tab_apps (void)
{
  GtkWidget *grid = mc_grid (1, 2);
  gtk_flow_box_append (GTK_FLOW_BOX (grid), check_card ("system-software-install-symbolic", ACC_T_SEC_SOURCES, &c_sources));
  gtk_flow_box_append (GTK_FLOW_BOX (grid), check_card ("software-update-available-symbolic", ACC_T_SEC_OUTDATED, &c_outdated));
  gtk_flow_box_append (GTK_FLOW_BOX (grid), check_card ("application-x-addon-symbolic", ACC_T_SEC_FLATPAK, &c_flatpak));
  return grid;
}

static GtkWidget *
tab_privacy (void)
{
  GtkWidget *grid = mc_grid (1, 2);
  gtk_flow_box_append (GTK_FLOW_BOX (grid), check_card ("system-lock-screen-symbolic", ACC_T_SEC_SCREENLOCK, &c_screenlock));
  gtk_flow_box_append (GTK_FLOW_BOX (grid), check_card ("avatar-default-symbolic", ACC_T_SEC_AUTOLOGIN, &c_autologin));
  gtk_flow_box_append (GTK_FLOW_BOX (grid), check_card ("network-transmit-receive-symbolic", ACC_T_SEC_NETSVC, &c_netsvc));
  gtk_flow_box_append (GTK_FLOW_BOX (grid), check_card ("system-run-symbolic", ACC_T_SEC_AUTOSTART, &c_autostart));
  return grid;
}

/* Tools tab + manual scan runner (implemented at the end of this file). */
GtkWidget *acc_security_tools_tab (void);
void       acc_security_tools_teardown (void);

/* -------------------------------------------------------- tab shell */
static void
tab_cb (GtkToggleButton *b, gpointer name)
{
  if (gtk_toggle_button_get_active (b) && s_stack)
    gtk_stack_set_visible_child_name (s_stack, (const gchar *) name);
}

static void
add_tab (GtkWidget *bar, GtkToggleButton *grp, AccStr label, const gchar *page,
         gboolean first, int idx)
{
  GtkWidget *b = gtk_toggle_button_new_with_label (acc_tr (label));
  gtk_widget_add_css_class (b, "hubtab");
  if (grp) gtk_toggle_button_set_group (GTK_TOGGLE_BUTTON (b), grp);
  g_signal_connect (b, "toggled", G_CALLBACK (tab_cb), (gpointer) page);
  gtk_box_append (GTK_BOX (bar), b);
  if (first) gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b), TRUE);
  s_tabs[idx] = b;
}

GtkWidget *
acc_page_security (AccUI *u)
{
  (void) u;
  GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  /* header with a re-check button + last-checked time */
  GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_add_css_class (h, "pagehead");
  GtkWidget *col = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
  gtk_widget_set_hexpand (col, TRUE);
  gtk_widget_set_valign (col, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (col), mc_label ("ASUSCraft", "eyebrow", 0));
  gtk_box_append (GTK_BOX (col), mc_label (acc_tr (ACC_T_NAV_SECURITY), "pagetitle", 0));
  GtkWidget *sub = mc_label (acc_tr (ACC_T_SEC_SUB), "pagesub", 0);
  gtk_label_set_wrap (GTK_LABEL (sub), TRUE);
  gtk_box_append (GTK_BOX (col), sub);
  gtk_box_append (GTK_BOX (h), col);
  GtkWidget *rc = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_valign (rc, GTK_ALIGN_CENTER);
  GtkWidget *rbtn = gtk_button_new_with_label (acc_tr (ACC_T_SEC_RECHECK));
  gtk_widget_add_css_class (rbtn, "actionbtn");
  g_signal_connect (rbtn, "clicked", G_CALLBACK (recheck_cb), NULL);
  gtk_box_append (GTK_BOX (rc), rbtn);
  s_lastcheck = mc_label ("", "foot", 1);
  gtk_box_append (GTK_BOX (rc), s_lastcheck);
  gtk_box_append (GTK_BOX (h), rc);
  gtk_box_append (GTK_BOX (page), h);

  /* tab bar */
  GtkWidget *bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class (bar, "hubbar");
  gtk_widget_set_halign (bar, GTK_ALIGN_START);
  gtk_widget_set_margin_start (bar, 22);
  gtk_widget_set_margin_end (bar, 22);
  gtk_widget_set_margin_bottom (bar, 10);
  gtk_box_append (GTK_BOX (page), bar);

  GtkWidget *stack = gtk_stack_new ();
  s_stack = GTK_STACK (stack);
  gtk_stack_set_transition_type (GTK_STACK (stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  gtk_widget_set_vexpand (stack, TRUE);
  gtk_stack_add_named (GTK_STACK (stack), tab_overview (), "overview");
  gtk_stack_add_named (GTK_STACK (stack), tab_firewall (), "firewall");
  gtk_stack_add_named (GTK_STACK (stack), tab_boot (), "boot");
  gtk_stack_add_named (GTK_STACK (stack), tab_apps (), "apps");
  gtk_stack_add_named (GTK_STACK (stack), tab_privacy (), "privacy");
  gtk_stack_add_named (GTK_STACK (stack), acc_security_tools_tab (), "tools");
  gtk_box_append (GTK_BOX (page), stack);

  GtkToggleButton *g0;
  add_tab (bar, NULL, ACC_T_SEC_TAB_OVERVIEW, "overview", TRUE, 0);
  g0 = GTK_TOGGLE_BUTTON (s_tabs[0]);
  add_tab (bar, g0, ACC_T_SEC_TAB_FIREWALL, "firewall", FALSE, 1);
  add_tab (bar, g0, ACC_T_SEC_TAB_BOOT, "boot", FALSE, 2);
  add_tab (bar, g0, ACC_T_SEC_TAB_APPS, "apps", FALSE, 3);
  add_tab (bar, g0, ACC_T_SEC_TAB_PRIVACY, "privacy", FALSE, 4);
  add_tab (bar, g0, ACC_T_SEC_TAB_TOOLS, "tools", FALSE, 5);

  run_all_checks ();
  return mc_page (page);
}

void
acc_security_teardown (void)
{
  s_stack = NULL;
  s_lastcheck = NULL;
  for (guint i = 0; i < G_N_ELEMENTS (s_tabs); i++)
    s_tabs[i] = NULL;
  Check *all[] = { &c_sb, &c_fw, &c_selinux, &c_upd, &c_kernel, &c_services,
                   &c_fw_full, &c_bk_sb, &c_bk_kernel, &c_nvidia, &c_signing,
                   &c_sources, &c_outdated, &c_flatpak,
                   &c_screenlock, &c_autologin, &c_netsvc, &c_autostart };
  for (guint i = 0; i < G_N_ELEMENTS (all); i++)
    { all[i]->pill = all[i]->summary = all[i]->detail = NULL; }
  acc_security_tools_teardown ();
}

/* =====================================================================
 * Tools tab + manual security scans (Stage C)
 *
 * Scans are opt-in: nothing runs until the user confirms. Each scan uses an
 * argument array (never a shell string), runs asynchronously with a timeout
 * and a Cancel button, captures the exit code and output, and is recorded in
 * a short local history. A scan is read-only: it never installs tools,
 * quarantines, or deletes files, and a scan that fails to run is reported as
 * a failure — never as "clean".
 * ===================================================================== */

typedef struct {
  const gchar *id;       /* program on PATH          */
  const gchar *label;    /* display name             */
  const gchar *const *argv;  /* scan command (read-only) */
  int timeout_s;
} ScanDef;

static const gchar *ARGV_CLAMAV[]     = { "clamscan", "-r", "-i", "--stdout", NULL, NULL }; /* [4]=$HOME */
static const gchar *ARGV_LYNIS[]      = { "lynis", "audit", "system", "--quick", "--no-colors", NULL };
static const gchar *ARGV_RKHUNTER[]   = { "rkhunter", "--check", "--sk", "--nocolors", "--rwo", NULL };
static const gchar *ARGV_CHKROOTKIT[] = { "chkrootkit", "-q", NULL };
static const gchar *ARGV_AIDE[]       = { "aide", "--check", NULL };

static ScanDef s_scans[] = {
  { "clamscan",    "ClamAV",     ARGV_CLAMAV,     3600 },
  { "lynis",       "Lynis",      ARGV_LYNIS,      900  },
  { "rkhunter",    "rkhunter",   ARGV_RKHUNTER,   900  },
  { "chkrootkit",  "chkrootkit", ARGV_CHKROOTKIT, 600  },
  { "aide",        "AIDE",       ARGV_AIDE,       1800 },
};

static GtkWidget *s_tool_rows[G_N_ELEMENTS (s_scans)];  /* run buttons */
static GtkWidget *s_scan_status, *s_scan_spinner, *s_scan_cancel, *s_scan_out;
static GtkWidget *s_hist_box;
static GSubprocess   *s_proc;
static GCancellable  *s_cancel;
static guint          s_scan_timeout_id;
static gboolean       s_scan_running;

static gchar *
hist_path (void)
{
  return g_build_filename (g_get_user_config_dir (), "asus-control-center",
                           "scan_history.txt", NULL);
}

static void
hist_reload (void)
{
  if (!s_hist_box)
    return;
  GtkWidget *c;
  while ((c = gtk_widget_get_first_child (s_hist_box)))
    gtk_box_remove (GTK_BOX (s_hist_box), c);

  gchar *p = hist_path ();
  gchar *data = NULL;
  gboolean any = FALSE;
  if (g_file_get_contents (p, &data, NULL, NULL) && data && *data)
    {
      gchar **lines = g_strsplit (g_strchomp (data), "\n", -1);
      /* newest last in file -> show reversed (newest first) */
      int n = 0; while (lines[n]) n++;
      for (int i = n - 1; i >= 0; i--)
        if (*lines[i])
          { gtk_box_append (GTK_BOX (s_hist_box), mc_label (lines[i], "dim", 0)); any = TRUE; }
      g_strfreev (lines);
    }
  g_free (data);
  g_free (p);
  if (!any)
    gtk_box_append (GTK_BOX (s_hist_box), mc_label (acc_tr (ACC_T_SEC_NOHIST), "dim", 0));
}

static void
hist_append (const gchar *tool, const gchar *result)
{
  gchar *p = hist_path ();
  gchar *dir = g_path_get_dirname (p);
  g_mkdir_with_parents (dir, 0700);
  g_free (dir);
  GDateTime *now = g_date_time_new_now_local ();
  gchar *ts = g_date_time_format (now, "%Y-%m-%d %H:%M");
  g_date_time_unref (now);

  gchar *old = NULL;
  g_file_get_contents (p, &old, NULL, NULL);
  GString *s = g_string_new (old ? old : "");
  g_free (old);
  if (s->len && s->str[s->len - 1] != '\n')
    g_string_append_c (s, '\n');
  g_string_append_printf (s, "%s · %s · %s\n", ts, tool, result);
  g_free (ts);

  /* trim to the retention limit (keep newest) */
  gchar **lines = g_strsplit (g_strchomp (s->str), "\n", -1);
  int n = 0; while (lines[n]) n++;
  int keep = acc_pref_scan_keep ();
  GString *out = g_string_new (NULL);
  for (int i = (n > keep ? n - keep : 0); i < n; i++)
    if (*lines[i]) g_string_append_printf (out, "%s\n", lines[i]);
  g_strfreev (lines);
  g_file_set_contents_full (p, out->str, -1, G_FILE_SET_CONTENTS_CONSISTENT, 0600, NULL);
  g_string_free (out, TRUE);
  g_string_free (s, TRUE);
  g_free (p);
  hist_reload ();
}

static void
scan_set_buttons_sensitive (gboolean on)
{
  for (gsize i = 0; i < G_N_ELEMENTS (s_scans); i++)
    if (s_tool_rows[i])
      gtk_widget_set_sensitive (s_tool_rows[i], on);
}

static void
scan_cleanup (void)
{
  if (s_scan_timeout_id) { g_source_remove (s_scan_timeout_id); s_scan_timeout_id = 0; }
  g_clear_object (&s_proc);
  g_clear_object (&s_cancel);
  s_scan_running = FALSE;
  if (s_scan_spinner) { gtk_spinner_stop (GTK_SPINNER (s_scan_spinner)); gtk_widget_set_visible (s_scan_spinner, FALSE); }
  if (s_scan_cancel) gtk_widget_set_visible (s_scan_cancel, FALSE);
  scan_set_buttons_sensitive (TRUE);
}

static void
scan_done (GObject *src, GAsyncResult *res, gpointer user)
{
  gchar *tool = (gchar *) user;
  GError *e = NULL;
  gchar *out = NULL;
  gboolean ok = g_subprocess_communicate_utf8_finish (G_SUBPROCESS (src), res,
                                                      &out, NULL, &e);
  gboolean cancelled = s_cancel && g_cancellable_is_cancelled (s_cancel);
  gint status = s_proc ? g_subprocess_get_exit_status (s_proc) : -1;

  const gchar *result;
  if (cancelled)
    { result = acc_tr (ACC_T_SEC_SCAN_CANCELLED); }
  else if (!ok)
    { result = acc_tr (ACC_T_SEC_SCAN_FAIL); }
  else if (status == 0)
    { result = acc_tr (ACC_T_SEC_SCAN_DONE); }    /* no findings / clean exit */
  else
    { result = acc_tr (ACC_T_SEC_SCAN_DONE); }    /* exit!=0 often means findings */

  if (s_scan_status)
    {
      gchar *msg = cancelled ? g_strdup (result)
                   : !ok ? g_strdup_printf ("%s: %s", result, e ? e->message : "?")
                   : g_strdup_printf ("%s — exit %d", result, status);
      gtk_label_set_text (GTK_LABEL (s_scan_status), msg);
      g_free (msg);
    }
  if (s_scan_out && out)
    {
      /* show the tail of the output, not the whole thing */
      gsize len = strlen (out);
      const gchar *tail = len > 4000 ? out + len - 4000 : out;
      gtk_label_set_text (GTK_LABEL (s_scan_out), tail);
    }

  gchar *hres = cancelled ? g_strdup (acc_tr (ACC_T_SEC_SCAN_CANCELLED))
                : !ok ? g_strdup (acc_tr (ACC_T_SEC_SCAN_FAIL))
                : g_strdup_printf ("%s (exit %d)", acc_tr (ACC_T_SEC_SCAN_DONE), status);
  hist_append (tool, hres);
  g_free (hres);

  g_clear_error (&e);
  g_free (out);
  g_free (tool);
  scan_cleanup ();
}

static gboolean
scan_timeout_cb (gpointer u)
{
  (void) u;
  s_scan_timeout_id = 0;
  if (s_proc) g_subprocess_force_exit (s_proc);
  if (s_cancel) g_cancellable_cancel (s_cancel);
  return G_SOURCE_REMOVE;
}

static void
scan_cancel_cb (GtkButton *b, gpointer u)
{
  (void) b; (void) u;
  if (s_proc) g_subprocess_force_exit (s_proc);
  if (s_cancel) g_cancellable_cancel (s_cancel);
}

static void
scan_start (const ScanDef *def)
{
  if (s_scan_running)
    return;

  /* build argv; ClamAV scans the user's home (read-only) */
  std::vector<const gchar *> argv;
  for (int i = 0; def->argv[i]; i++)
    argv.push_back (def->argv[i]);
  if (g_strcmp0 (def->id, "clamscan") == 0)
    argv.push_back (g_get_home_dir ());
  argv.push_back (NULL);

  GError *e = NULL;
  s_cancel = g_cancellable_new ();
  s_proc = g_subprocess_newv (argv.data (),
                              (GSubprocessFlags) (G_SUBPROCESS_FLAGS_STDOUT_PIPE
                                                  | G_SUBPROCESS_FLAGS_STDERR_MERGE),
                              &e);
  if (!s_proc)
    {
      if (s_scan_status)
        {
          gchar *m = g_strdup_printf ("%s: %s", acc_tr (ACC_T_SEC_SCAN_FAIL),
                                      e ? e->message : "?");
          gtk_label_set_text (GTK_LABEL (s_scan_status), m);
          g_free (m);
        }
      g_clear_error (&e);
      g_clear_object (&s_cancel);
      return;
    }

  s_scan_running = TRUE;
  scan_set_buttons_sensitive (FALSE);
  if (s_scan_spinner) { gtk_widget_set_visible (s_scan_spinner, TRUE); gtk_spinner_start (GTK_SPINNER (s_scan_spinner)); }
  if (s_scan_cancel) gtk_widget_set_visible (s_scan_cancel, TRUE);
  if (s_scan_status)
    { gchar *m = g_strdup_printf ("%s %s", def->label, acc_tr (ACC_T_SEC_SCANNING));
      gtk_label_set_text (GTK_LABEL (s_scan_status), m); g_free (m); }
  if (s_scan_out) gtk_label_set_text (GTK_LABEL (s_scan_out), "");

  s_scan_timeout_id = g_timeout_add_seconds (def->timeout_s, scan_timeout_cb, NULL);
  g_subprocess_communicate_utf8_async (s_proc, NULL, s_cancel, scan_done,
                                       g_strdup (def->label));
}

typedef struct { const ScanDef *def; } ScanLaunch;

static void
scan_confirm_response (AdwAlertDialog *dlg, const gchar *resp, gpointer user)
{
  (void) dlg;
  ScanLaunch *sl = (ScanLaunch *) user;
  if (g_strcmp0 (resp, "ok") == 0)
    scan_start (sl->def);
  g_free (sl);
}

static void
scan_clicked (GtkButton *b, gpointer user)
{
  (void) b;
  const ScanDef *def = (const ScanDef *) user;
  gchar *body = g_strdup_printf (acc_tr (ACC_T_SEC_SCAN_Q), def->label);
  AdwAlertDialog *dlg = ADW_ALERT_DIALOG (adw_alert_dialog_new (def->label, body));
  adw_alert_dialog_add_responses (dlg, "cancel", acc_tr (ACC_T_ST_CANCEL),
                                  "ok", acc_tr (ACC_T_SEC_SCAN), NULL);
  adw_alert_dialog_set_default_response (dlg, "cancel");
  ScanLaunch *sl = g_new0 (ScanLaunch, 1);
  sl->def = def;
  g_signal_connect (dlg, "response", G_CALLBACK (scan_confirm_response), sl);
  adw_dialog_present (ADW_DIALOG (dlg), acc_ui_root ());
  g_free (body);
}

GtkWidget *
acc_security_tools_tab (void)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 14);

  /* detected tools + run buttons */
  GtkWidget *body = NULL;
  GtkWidget *card = mc_card_titled ("security-high-symbolic", acc_tr (ACC_T_SEC_TAB_TOOLS), &body);
  GtkWidget *hint = mc_label (acc_tr (ACC_T_SEC_TOOLS_HINT), "foot", 0);
  gtk_label_set_wrap (GTK_LABEL (hint), TRUE);
  gtk_box_append (GTK_BOX (body), hint);

  for (gsize i = 0; i < G_N_ELEMENTS (s_scans); i++)
    {
      gboolean inst = have (s_scans[i].id);
      GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
      gtk_widget_set_margin_top (row, 4);
      gtk_box_append (GTK_BOX (row), mc_label (s_scans[i].label, "val", 0));
      GtkWidget *pill = mc_label (acc_tr (inst ? ACC_T_SEC_INSTALLED : ACC_T_SEC_NOTINST),
                                  inst ? "pill on" : "pill off", 0);
      gtk_widget_set_valign (pill, GTK_ALIGN_CENTER);
      gtk_box_append (GTK_BOX (row), pill);
      GtkWidget *sp = mc_label (NULL, NULL, 0);
      gtk_widget_set_hexpand (sp, TRUE);
      gtk_box_append (GTK_BOX (row), sp);
      if (inst)
        {
          GtkWidget *run = gtk_button_new_with_label (acc_tr (ACC_T_SEC_SCAN));
          gtk_widget_add_css_class (run, "actionbtn");
          g_signal_connect (run, "clicked", G_CALLBACK (scan_clicked),
                            (gpointer) &s_scans[i]);
          gtk_box_append (GTK_BOX (row), run);
          s_tool_rows[i] = run;
        }
      else
        s_tool_rows[i] = NULL;
      gtk_box_append (GTK_BOX (body), row);
    }

  /* live scan status */
  GtkWidget *statusrow = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_margin_top (statusrow, 6);
  s_scan_spinner = gtk_spinner_new ();
  gtk_widget_set_visible (s_scan_spinner, FALSE);
  gtk_box_append (GTK_BOX (statusrow), s_scan_spinner);
  s_scan_status = mc_label ("", "dim", 0);
  gtk_widget_set_hexpand (s_scan_status, TRUE);
  gtk_label_set_wrap (GTK_LABEL (s_scan_status), TRUE);
  gtk_box_append (GTK_BOX (statusrow), s_scan_status);
  s_scan_cancel = gtk_button_new_with_label (acc_tr (ACC_T_ST_CANCEL));
  gtk_widget_add_css_class (s_scan_cancel, "actionbtn");
  gtk_widget_set_visible (s_scan_cancel, FALSE);
  g_signal_connect (s_scan_cancel, "clicked", G_CALLBACK (scan_cancel_cb), NULL);
  gtk_box_append (GTK_BOX (statusrow), s_scan_cancel);
  gtk_box_append (GTK_BOX (body), statusrow);

  GtkWidget *exp = gtk_expander_new (acc_tr (ACC_T_SEC_DETAILS));
  s_scan_out = mc_label ("", "readout", 0);
  gtk_label_set_wrap (GTK_LABEL (s_scan_out), TRUE);
  gtk_label_set_selectable (GTK_LABEL (s_scan_out), TRUE);
  gtk_expander_set_child (GTK_EXPANDER (exp), s_scan_out);
  gtk_box_append (GTK_BOX (body), exp);

  gtk_box_append (GTK_BOX (box), card);

  /* scan history */
  GtkWidget *hbody = NULL;
  GtkWidget *hcard = mc_card_titled ("document-open-recent-symbolic", acc_tr (ACC_T_SEC_HISTORY), &hbody);
  s_hist_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  gtk_box_append (GTK_BOX (hbody), s_hist_box);
  gtk_box_append (GTK_BOX (box), hcard);
  hist_reload ();

  return box;
}

void
acc_security_tools_teardown (void)
{
  if (s_scan_running)
    {
      if (s_proc) g_subprocess_force_exit (s_proc);
      if (s_cancel) g_cancellable_cancel (s_cancel);
    }
  if (s_scan_timeout_id) { g_source_remove (s_scan_timeout_id); s_scan_timeout_id = 0; }
  g_clear_object (&s_proc);
  g_clear_object (&s_cancel);
  s_scan_running = FALSE;
  for (gsize i = 0; i < G_N_ELEMENTS (s_scans); i++)
    s_tool_rows[i] = NULL;
  s_scan_status = s_scan_spinner = s_scan_cancel = s_scan_out = NULL;
  s_hist_box = NULL;
}
