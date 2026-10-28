/* control/ctl.c - privileged write layer via polkit (M2)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ctl.h"
#include "../probe/probe.h"
#include <gio/gio.h>
#include <string.h>

#define PKEXEC "/usr/bin/pkexec"
#define HELPER "/usr/local/bin/acc-helper"

static int s_last_exit = 0;   /* exit status of the last helper run */

gboolean
acc_ctl_available (void)
{
  return acc_path_exists (PKEXEC) && acc_path_exists (HELPER);
}

gboolean
acc_ctl_helper_stale (void)
{
  gchar *txt = NULL;
  if (!g_file_get_contents (HELPER, &txt, NULL, NULL) || !txt)
    return FALSE;
  /* helper v2 introduced the asus-armoury path + read-back verification */
  gboolean stale = strstr (txt, "armoury_file") == NULL;
  g_free (txt);
  return stale;
}

gboolean
acc_ctl_run (const gchar *action, const gchar *arg, gchar **err)
{
  if (err)
    *err = NULL;
  if (!acc_ctl_available ())
    {
      if (err)
        *err = g_strdup ("helper not installed");
      return FALSE;
    }

  const gchar *argv[] = { PKEXEC, HELPER, action, arg, NULL };
  GError *e = NULL;
  GSubprocessLauncher *l = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);
  GSubprocess *p = g_subprocess_launcher_spawnv (l, argv, &e);
  g_object_unref (l);

  if (!p)
    {
      if (err)
        *err = g_strdup (e ? e->message : "spawn failed");
      g_clear_error (&e);
      return FALSE;
    }

  gboolean ok = g_subprocess_wait_check (p, NULL, &e) ;
  /* remember how it ended: helper exit codes carry the reason
   * (3 = modules in use), pkexec returns 127 when auth is dismissed */
  s_last_exit = g_subprocess_get_exit_status (p) & 0xff;
  if (!ok && err)
    *err = g_strdup (e ? e->message
                       : "authorization denied or action failed");
  g_clear_error (&e);
  g_object_unref (p);
  return ok;
}

int
acc_ctl_last_exit (void)
{
  return s_last_exit;
}

gboolean
acc_ctl_set_perf (const gchar *profile)
{
  if (!profile || !*profile)
    return FALSE;
  g_auto (GStrv) allowed = g_strsplit ("quiet,balanced,performance", ",", 0);
  for (int i = 0; allowed[i]; i++)
    if (g_strcmp0 (allowed[i], profile) == 0)
      return acc_ctl_run ("set-perf", profile, NULL);
  return FALSE;   /* not a whitelisted value */
}

gboolean
acc_ctl_set_charge (int pct)
{
  gchar v[8];
  if (pct != 60 && pct != 80 && pct != 100)
    return FALSE;
  g_snprintf (v, sizeof v, "%d", pct);
  return acc_ctl_run ("set-charge", v, NULL);
}

gboolean
acc_ctl_set_charge_mode (int mode)
{
  gchar v[8];
  if (mode < 0 || mode > 2)
    return FALSE;
  g_snprintf (v, sizeof v, "%d", mode);
  return acc_ctl_run ("set-charge-mode", v, NULL);
}

gboolean
acc_ctl_set_gpu_mux (int mode)
{
  gchar v[8];
  if (mode != 0 && mode != 1)
    return FALSE;
  g_snprintf (v, sizeof v, "%d", mode);
  return acc_ctl_run ("set-gpu-mux", v, NULL);
}

gboolean
acc_ctl_set_kbd (int level)
{
  gchar v[8];
  if (level < 0 || level > 3)
    return FALSE;
  g_snprintf (v, sizeof v, "%d", level);
  return acc_ctl_run ("set-kbd", v, NULL);
}

gboolean
acc_ctl_nvidia_loaded (void)
{
  /* /proc/modules is world-readable; look for the " nvidia " driver line */
  gchar *c = NULL;
  if (!g_file_get_contents ("/proc/modules", &c, NULL, NULL))
    return FALSE;
  gchar **lines = g_strsplit (c, "\n", -1);
  g_free (c);
  gboolean found = FALSE;
  for (gint i = 0; lines && lines[i]; i++)
    if (g_str_has_prefix (lines[i], "nvidia "))
      {
        found = TRUE;
        break;
      }
  g_strfreev (lines);
  return found;
}
