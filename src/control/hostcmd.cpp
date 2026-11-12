/* control/hostcmd.cpp - one way to reach the host, with or without an agent
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "hostcmd.h"
#include "agent.h"

#include <gio/gio.h>
#include <string.h>
#include <sys/wait.h>

#define PKEXEC  "/usr/bin/pkexec"
#define HELPER  "/usr/libexec/asus-control-center/acc-helper"
#define TRUECMD "/usr/bin/true"

gboolean
acc_host_agent_ready (void)
{
  return acc_agent_state () == ACC_AGENT_CONNECTED;
}

/* --------------------------------------------------------- the read path */

gboolean
acc_host_run (AccCommand id, const gchar *arg,
              gchar **out, gint *exit_code, AccHostHow *how, gchar **err)
{
  if (out)
    *out = NULL;
  if (exit_code)
    *exit_code = -1;
  if (err)
    *err = NULL;
  if (how)
    *how = ACC_HOST_UNAVAILABLE;

  /* 1. through the agent, when there is one */
  if (acc_host_agent_ready ())
    {
      gchar *aout = NULL, *aerr = NULL;
      gint acode = -1;
      if (acc_agent_run (id, arg, &aout, &acode, &aerr))
        {
          /* A negative exit code means the agent could not run the
           * command at all (an argument it refuses, a program that is
           * not installed, an agent that is too old for this command).
           * A command that ran and failed has its own exit code (0-255)
           * and its own output, and that answer is passed through. */
          if (acode >= 0)
            {
              if (out)
                *out = aout;
              else
                g_free (aout);
              if (exit_code)
                *exit_code = acode;
              if (err)
                *err = aerr;
              else
                g_free (aerr);
              if (how)
                *how = ACC_HOST_AGENT;
              return TRUE;
            }
          g_free (aout);
          g_free (aerr);
        }
      /* fall through and run it here instead */
    }

  /* 2. locally */
  gchar *lout = NULL, *lerr = NULL;
  gint lcode = -1;
  gboolean ok = acc_command_run_local (id, arg, &lout, &lcode, &lerr);
  if (out)
    *out = lout;
  else
    g_free (lout);
  if (exit_code)
    *exit_code = lcode;
  if (err)
    *err = lerr;
  else
    g_free (lerr);
  if (how)
    *how = ok ? ACC_HOST_LOCAL : ACC_HOST_UNAVAILABLE;
  return ok;
}

/* ------------------------------------------------------ the privileged path */

/* The local privileged call: exactly what the app has always done. */
static gboolean
helper_local (const gchar *action, const gchar *value,
              gint *exit_code, gchar **err)
{
  if (!g_file_test (PKEXEC, G_FILE_TEST_EXISTS)
      || !g_file_test (HELPER, G_FILE_TEST_EXISTS))
    {
      if (err)
        *err = g_strdup ("helper not installed");
      if (exit_code)
        *exit_code = -1;
      return FALSE;
    }

  const gchar *argv[] = { PKEXEC, HELPER, action, value, NULL };
  GError *e = NULL;
  GSubprocessLauncher *l = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);
  GSubprocess *p = g_subprocess_launcher_spawnv (l, argv, &e);
  g_object_unref (l);

  if (!p)
    {
      if (exit_code)
        *exit_code = -1;
      if (err)
        *err = g_strdup (e ? e->message : "spawn failed");
      g_clear_error (&e);
      return FALSE;
    }

  gboolean ok = g_subprocess_wait_check (p, NULL, &e);
  /* remember how it ended: helper exit codes carry the reason
   * (3 = modules in use), pkexec returns 127 when auth is dismissed */
  gint code = g_subprocess_get_exit_status (p) & 0xff;
  if (exit_code)
    *exit_code = code;
  if (!ok && err)
    *err = g_strdup (e ? e->message
                       : "authorization denied or action failed");
  g_clear_error (&e);
  g_object_unref (p);
  return ok;
}

gboolean
acc_host_set_hardware (const gchar *action, const gchar *value,
                       gint *exit_code, AccHostHow *how, gchar **err)
{
  if (exit_code)
    *exit_code = -1;
  if (err)
    *err = NULL;
  if (how)
    *how = ACC_HOST_UNAVAILABLE;

  /* the whitelist is checked here, on the host side and in the helper */
  if (!acc_command_helper_action_ok (action)
      || !acc_command_helper_value_ok (action, value))
    {
      if (err)
        *err = g_strdup ("invalid action or argument");
      if (exit_code)
        *exit_code = 2;
      return FALSE;
    }

  /* 1. through the agent, when there is one */
  if (acc_host_agent_ready ())
    {
      gint code = -1;
      gchar *aerr = NULL;
      if (acc_agent_set_hardware (action, value, &code, &aerr))
        {
          if (exit_code)
            *exit_code = code;
          if (err)
            *err = aerr;
          else
            g_free (aerr);
          if (how)
            *how = ACC_HOST_AGENT;
          return code == 0;
        }
      g_free (aerr);
    }

  /* 2. locally */
  gboolean ok = helper_local (action, value, exit_code, err);
  if (how)
    *how = ok ? ACC_HOST_LOCAL : ACC_HOST_UNAVAILABLE;
  return ok;
}

/* --------------------------------------------------------- the admin probe */

gboolean
acc_host_check_admin (gint *exit_code)
{
  if (exit_code)
    *exit_code = -1;

  if (acc_host_agent_ready ())
    {
      gint code = -1;
      if (acc_agent_check_admin (&code))
        {
          if (exit_code)
            *exit_code = code;
          return TRUE;
        }
    }

  if (!g_file_test (PKEXEC, G_FILE_TEST_EXISTS))
    return FALSE;

  const gchar *argv[] = { PKEXEC, TRUECMD, NULL };
  GError *e = NULL;
  GSubprocess *p = g_subprocess_newv (argv, G_SUBPROCESS_FLAGS_NONE, &e);
  if (!p)
    {
      g_clear_error (&e);
      return FALSE;
    }
  gboolean ok = g_subprocess_wait_check (p, NULL, NULL);
  if (exit_code)
    *exit_code = g_subprocess_get_exit_status (p) & 0xff;
  g_object_unref (p);
  return ok;
}
