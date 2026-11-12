/* control/nightlight.cpp - the night light, on GNOME and on KDE
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GNOME: org.gnome.settings-daemon.plugins.color. The app uses the schema
 * directly when the machine has it, and asks the host agent when it does
 * not (a Flatpak has no GNOME colour schema).
 *
 * KDE: the NightColor group of kwinrc, written with kwriteconfig and
 * applied with a KWin reload. Those programs live on the host, so they
 * run through the host command layer - which means the night light keeps
 * working inside a Flatpak as well.
 *
 * Only the night-light keys are ever touched. Nothing else in the user's
 * colour or window settings is written.
 */
#include "nightlight.h"
#include "../probe/desktop.h"
#include "agent.h"
#include "commands.h"
#include "hostcmd.h"
#include "nlsettings.h"

#include <gio/gio.h>
#include <string.h>

#define NL_KEY_ENABLED "night-light-enabled"
#define NL_KEY_TEMP    "night-light-temperature"
#define NL_KEY_AUTO    "night-light-schedule-automatic"
#define NL_KEY_FROM    "night-light-schedule-from"
#define NL_KEY_TO      "night-light-schedule-to"

/* ------------------------------------------------------------- the backend */

/* Which desktop owns the night light. The environment is asked first; the
 * GNOME colour schema is the tie-breaker. */
static gboolean
use_kde (void)
{
  if (acc_desktop_is_kde ())
    return TRUE;
  if (acc_desktop_is_gnome ())
    return FALSE;
  return FALSE;
}

/* ----------------------------------------------------------- the GNOME path */

/* Read the GNOME settings: from the local schema when there is one, from
 * the host agent otherwise. */
static gchar *
gnome_read_text (void)
{
  if (acc_nl_available ())
    return acc_nl_read ();

  gchar *text = NULL;
  gchar *err = NULL;
  if (acc_agent_nightlight_get (&text, &err))
    return text;
  g_free (text);
  g_free (err);
  return NULL;
}

static gboolean
gnome_write (const gchar *key, const gchar *value, gchar **err)
{
  if (acc_nl_available ())
    return acc_nl_write (key, value, err);

  gchar *aerr = NULL;
  gboolean ok = acc_agent_nightlight_set (key, value, &aerr);
  if (!ok && err)
    /* the caller owns its own copy: handing it aerr and freeing aerr
     * would leave a dangling pointer behind */
    *err = aerr ? g_strdup (aerr)
                : g_strdup ("the host agent did not answer");
  g_free (aerr);
  return ok;
}

/* ------------------------------------------------------------- the KDE path */

static gboolean
kde_get (void)
{
  gchar *out = NULL, *err = NULL;
  gint code = -1;
  AccHostHow how = ACC_HOST_UNAVAILABLE;
  gboolean ok = acc_host_run (ACC_CMD_NL_KDE_GET, NULL, &out, &code,
                              &how, &err);
  g_free (err);
  if (!ok || !out)
    {
      g_free (out);
      return FALSE;
    }
  gboolean val = g_ascii_strcasecmp (g_strstrip (out), "true") == 0;
  g_free (out);
  return val;
}

static void
kde_set (gboolean on)
{
  const gchar *v = on ? "true" : "false";
  gchar *out = NULL, *err = NULL;
  gint code = -1;
  AccHostHow how = ACC_HOST_UNAVAILABLE;

  acc_host_run (ACC_CMD_NL_KDE_SET, v, &out, &code, &how, &err);
  g_free (out);
  g_free (err);

  /* ask KWin to reload its configuration so the change applies at once */
  acc_host_run (ACC_CMD_NL_KDE_RELOAD, v, &out, &code, &how, &err);
  g_free (out);
  g_free (err);
}

/* ---------------------------------------------------------------- the API */

AccNightLight
acc_nightlight_read (void)
{
  AccNightLight nl;
  memset (&nl, 0, sizeof nl);

  if (use_kde ())
    {
      nl.available = TRUE;
      nl.enabled = kde_get ();
      return nl;
    }

  gchar *text = gnome_read_text ();
  if (!text)
    return nl;                 /* available stays FALSE */

  nl.available = TRUE;

  GKeyFile *kf = g_key_file_new ();
  if (g_key_file_load_from_data (kf, text, -1, G_KEY_FILE_NONE, NULL))
    {
      gchar *v = g_key_file_get_string (kf, "night-light", NL_KEY_ENABLED, NULL);
      if (v)
        {
          nl.enabled = g_strcmp0 (v, "true") == 0;
          g_free (v);
        }
      v = g_key_file_get_string (kf, "night-light", NL_KEY_TEMP, NULL);
      if (v)
        {
          nl.has_temperature = TRUE;
          nl.temperature = (guint) g_ascii_strtoll (v, NULL, 10);
          g_free (v);
        }
      v = g_key_file_get_string (kf, "night-light", NL_KEY_AUTO, NULL);
      if (v)
        {
          nl.has_schedule = TRUE;
          nl.schedule_automatic = g_strcmp0 (v, "true") == 0;
          g_free (v);
        }
      else if (g_key_file_has_key (kf, "night-light", "night-light-schedule", NULL))
        {
          nl.has_schedule = TRUE;
          nl.schedule_automatic = FALSE;
        }
      v = g_key_file_get_string (kf, "night-light", NL_KEY_FROM, NULL);
      if (v)
        {
          nl.schedule_from = g_ascii_strtod (v, NULL);
          g_free (v);
        }
      v = g_key_file_get_string (kf, "night-light", NL_KEY_TO, NULL);
      if (v)
        {
          nl.schedule_to = g_ascii_strtod (v, NULL);
          g_free (v);
        }
    }
  g_key_file_free (kf);
  g_free (text);
  return nl;
}

gboolean
acc_nightlight_write_enabled (gboolean on)
{
  if (use_kde ())
    {
      kde_set (on);
      return TRUE;
    }
  gchar *err = NULL;
  gboolean ok = gnome_write (NL_KEY_ENABLED, on ? "true" : "false", &err);
  g_free (err);
  return ok;
}

gboolean
acc_nightlight_write_temperature (guint kelvin)
{
  if (use_kde ())
    return FALSE;             /* KDE has no colour temperature setting */
  if (kelvin < 1700 || kelvin > 4700)
    return FALSE;
  gchar *v = g_strdup_printf ("%u", kelvin);
  gchar *err = NULL;
  gboolean ok = gnome_write (NL_KEY_TEMP, v, &err);
  g_free (err);
  g_free (v);
  return ok;
}

gboolean
acc_nightlight_write_schedule (gboolean automatic, gdouble from, gdouble to)
{
  if (use_kde ())
    return FALSE;
  gchar *err = NULL;
  gboolean ok = TRUE;

  if (automatic)
    {
      ok = gnome_write (NL_KEY_AUTO, "true", &err);
    }
  else
    {
      ok = gnome_write (NL_KEY_AUTO, "false", &err);
      if (ok)
        {
          gchar *f = g_strdup_printf ("%g", from);
          gchar *t = g_strdup_printf ("%g", to);
          ok = gnome_write (NL_KEY_FROM, f, &err)
               && gnome_write (NL_KEY_TO, t, &err);
          g_free (f);
          g_free (t);
        }
    }
  g_free (err);
  return ok;
}

/* --------------------------------------------------------- the older API */

gboolean
acc_nightlight_available (void)
{
  AccNightLight nl = acc_nightlight_read ();
  return nl.available;
}

gboolean
acc_nightlight_get (void)
{
  AccNightLight nl = acc_nightlight_read ();
  return nl.available ? nl.enabled : FALSE;
}

void
acc_nightlight_set (gboolean on)
{
  acc_nightlight_write_enabled (on);
}
