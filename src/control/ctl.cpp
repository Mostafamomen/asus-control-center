/* control/ctl.c - privileged write layer via polkit (M2)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ctl.h"
#include "commands.h"
#include "hostcmd.h"
#include "../probe/probe.h"
#include <gio/gio.h>
#include <string.h>

#define PKEXEC "/usr/bin/pkexec"
#define HELPER "/usr/libexec/asus-control-center/acc-helper"

static int s_last_exit = 0;   /* exit status of the last helper run */

gboolean
acc_ctl_available (void)
{
  /* the helper itself, here ... */
  if (acc_path_exists (PKEXEC) && acc_path_exists (HELPER))
    return TRUE;
  /* ... or on the host, through the agent (a sandbox cannot see
   * /usr/libexec) */
  return acc_host_agent_ready ();
}

gboolean
acc_ctl_helper_stale (void)
{
  gchar *txt = NULL;
  if (!g_file_get_contents (HELPER, &txt, NULL, NULL) || !txt)
    return FALSE;
  /* helper v2 introduced the asus-armoury path + read-back verification;
   * v3 confirms MUX writes by pending_reboot (exit 5 = unconfirmed) */
  gboolean stale = strstr (txt, "armoury_file") == NULL
                   || strstr (txt, "pending_reboot") == NULL;
  g_free (txt);
  return stale;
}

gboolean
acc_ctl_run (const gchar *action, const gchar *arg, gchar **err)
{
  if (err)
    *err = NULL;

  /* Validate action and value against the helper's whitelist before
   * touching anything privileged. The same list lives in
   * src/control/commands.cpp, so the app, the agent and the helper all
   * agree on what is allowed. */
  if (!acc_command_helper_action_ok (action)
      || !arg || !*arg
      || !acc_command_helper_value_ok (action, arg))
    {
      if (err)
        *err = g_strdup ("invalid action or argument");
      return FALSE;
    }

  /* The helper has to be reachable: either on this machine, or on the host
   * the agent serves (a Flatpak cannot see /usr/libexec itself). */
  if (!acc_ctl_available () && !acc_host_agent_ready ())
    {
      if (err)
        *err = g_strdup ("helper not installed");
      return FALSE;
    }

  gint code = -1;
  AccHostHow how = ACC_HOST_UNAVAILABLE;
  gboolean ok = acc_host_set_hardware (action, arg, &code, &how, err);
  /* remember how it ended: helper exit codes carry the reason
   * (3 = modules in use), pkexec returns 127 when auth is dismissed */
  s_last_exit = code;
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
