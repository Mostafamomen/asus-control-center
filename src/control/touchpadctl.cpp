/* control/touchpadctl.cpp - the touchpad switch, through the desktop's own
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Only supported desktop settings are used. The xinput path that used to
 * live here is gone: xinput lists X11 devices only, so it did nothing on
 * a Wayland session, and it is not a program a Flatpak may run.
 *
 * Where the desktop exposes no usable setting, the control says so.
 */
#include "touchpadctl.h"
#include "../probe/desktop.h"
#include "agent.h"
#include "hostcmd.h"

#include <gio/gio.h>

#define GNOME_TP_SCHEMA "org.gnome.desktop.peripherals.touchpad"
#define GNOME_TP_KEY    "send-events"

/* The GNOME settings schema, if this machine has it. */
static GSettings *
gnome_settings (void)
{
  GSettingsSchemaSource *src = g_settings_schema_source_get_default ();
  if (!src)
    return NULL;
  GSettingsSchema *s = g_settings_schema_source_lookup (src, GNOME_TP_SCHEMA,
                                                        TRUE);
  if (!s)
    return NULL;
  g_settings_schema_unref (s);
  return g_settings_new (GNOME_TP_SCHEMA);
}

/* The desktop has to be one that actually honours the setting: the schema
 * may be installed on a machine that runs a different desktop. */
static gboolean
gnome_usable (void)
{
  if (!acc_desktop_is_gnome ())
    return FALSE;
  GSettings *st = gnome_settings ();
  if (!st)
    return FALSE;
  g_object_unref (st);
  return TRUE;
}

/* The switch belongs to the desktop session's dconf. A sandbox cannot be
 * trusted to reach the host's database, so the host agent does it. */

gboolean
acc_touchpad_available (void)
{
  if (acc_host_agent_ready ())
    {
      gchar *state = NULL, *err = NULL;
      if (acc_agent_touchpad_get (&state, &err))
        {
          gboolean ok = g_strcmp0 (state, "unsupported") != 0;
          g_free (state);
          g_free (err);
          return ok;
        }
      g_free (state);
      g_free (err);
    }
  return gnome_usable ();
}

const gchar *
acc_touchpad_backend (void)
{
  if (acc_host_agent_ready ())
    {
      gchar *state = NULL, *err = NULL;
      if (acc_agent_touchpad_get (&state, &err))
        {
          const gchar *r = g_strcmp0 (state, "unsupported") == 0 ? "" : "gnome";
          g_free (state);
          g_free (err);
          return r;
        }
      g_free (state);
      g_free (err);
    }
  return gnome_usable () ? "gnome" : "";
}

gboolean
acc_touchpad_get (void)
{
  if (acc_host_agent_ready ())
    {
      gchar *state = NULL, *err = NULL;
      if (acc_agent_touchpad_get (&state, &err))
        {
          gboolean on = g_strcmp0 (state, "enabled") == 0;
          g_free (state);
          g_free (err);
          return on;
        }
      g_free (state);
      g_free (err);
    }

  if (!gnome_usable ())
    return TRUE;    /* nothing to read: assume it is on */
  GSettings *st = gnome_settings ();
  if (!st)
    return TRUE;
  gchar *v = g_settings_get_string (st, GNOME_TP_KEY);
  g_object_unref (st);
  gboolean enabled = g_strcmp0 (v, "disabled") != 0;
  g_free (v);
  return enabled;
}

void
acc_touchpad_set (gboolean enabled)
{
  if (acc_host_agent_ready ())
    {
      gchar *err = NULL;
      gboolean ok = acc_agent_touchpad_set (enabled ? "enabled" : "disabled",
                                             &err);
      g_free (err);
      if (ok)
        return;
    }

  if (!gnome_usable ())
    return;
  GSettings *st = gnome_settings ();
  if (!st)
    return;
  g_settings_set_string (st, GNOME_TP_KEY, enabled ? "enabled" : "disabled");
  g_object_unref (st);
}
