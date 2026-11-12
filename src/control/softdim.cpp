/* control/softdim.cpp - install/enable the bundled Soft Brightness Plus
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The extension belongs to the desktop session, so the work is done by
 * whoever can reach that session: this app in a native install, the host
 * agent in a Flatpak. The app only decides which one to ask.
 */
#include "softdim.h"
#include "agent.h"
#include "hostcmd.h"
#include "sbp_local.h"

#include <gio/gio.h>
#include <string.h>

AccSoftDim
acc_softdim_status (void)
{
  if (acc_host_agent_ready ())
    {
      gchar *res = NULL, *err = NULL;
      if (acc_agent_softbrightness ("status", &res, &err))
        {
          AccSoftDim r = g_strcmp0 (res, "on") == 0 ? ACC_SD_ON
                       : g_strcmp0 (res, "disabled") == 0 ? ACC_SD_DISABLED
                       : ACC_SD_MISSING;
          g_free (res);
          g_free (err);
          return r;
        }
      g_free (res);
      g_free (err);
    }
  return acc_sbp_local_status ();
}

gboolean
acc_softdim_bundled (void)
{
  return acc_sbp_local_bundled ();
}

/* Ask the agent to do the work; fall back to doing it here. */
static gboolean
via_agent (const gchar *op, gchar **err)
{
  if (!acc_host_agent_ready ())
    return FALSE;

  gchar *res = NULL, *aerr = NULL;
  if (acc_agent_softbrightness (op, &res, &aerr))
    {
      g_free (res);
      g_free (aerr);
      return TRUE;
    }
  if (err)
    *err = aerr ? aerr : g_strdup ("the host agent refused the request");
  else
    g_free (aerr);
  g_free (res);
  return FALSE;
}

gboolean
acc_softdim_install (gchar **err)
{
  if (err)
    *err = NULL;

  if (acc_host_agent_ready ())
    {
      gchar *aerr = NULL;
      if (via_agent ("install", &aerr))
        return TRUE;
      if (err)
        *err = aerr;
      else
        g_free (aerr);
      return FALSE;
    }

  /* no agent: the extension can only be installed by a native install */
  if (!acc_sbp_local_bundled ())
    {
      if (err)
        *err = g_strdup ("bundled extension not found");
      return FALSE;
    }
  return acc_sbp_local_install (err);
}

gboolean
acc_softdim_enable (gchar **err)
{
  if (err)
    *err = NULL;

  if (acc_host_agent_ready ())
    {
      gchar *aerr = NULL;
      if (via_agent ("enable", &aerr))
        return TRUE;
      if (err)
        *err = aerr;
      else
        g_free (aerr);
      return FALSE;
    }

  return acc_sbp_local_enable (err);
}
