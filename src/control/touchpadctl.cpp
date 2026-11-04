/* control/touchpadctl.cpp - DE-aware touchpad enable/disable
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "touchpadctl.h"
#include "../probe/desktop.h"
#include <gio/gio.h>
#include <string.h>

#define GNOME_TP_SCHEMA "org.gnome.desktop.peripherals.touchpad"
#define GNOME_TP_KEY    "send-events"

/* ---- shared GSettings helper ---- */
static GSettings *
gs_open_safe (const gchar *schema)
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

/* ---- GNOME backend ---- */
static gboolean
gnome_get (void)
{
  GSettings *st = gs_open_safe (GNOME_TP_SCHEMA);
  if (!st)
    return TRUE; /* assume enabled if unknown */
  gchar *v = g_settings_get_string (st, GNOME_TP_KEY);
  g_object_unref (st);
  gboolean enabled = g_strcmp0 (v, "disabled") != 0;
  g_free (v);
  return enabled;
}

static void
gnome_set (gboolean enabled)
{
  GSettings *st = gs_open_safe (GNOME_TP_SCHEMA);
  if (!st)
    return;
  g_settings_set_string (st, GNOME_TP_KEY, enabled ? "enabled" : "disabled");
  g_object_unref (st);
}

/* ---- xinput backend (KDE / generic X11 or XWayland) ---- */
/* Finds the first xinput device whose name contains "touchpad" (case-
 * insensitive) and returns its integer id, or -1 if not found. */
static gint
xinput_touchpad_id (void)
{
  gchar *xinput = g_find_program_in_path ("xinput");
  if (!xinput)
    return -1;

  const gchar *argv[] = { xinput, "list", "--short", NULL };
  gchar *out = NULL;
  gboolean ok = g_spawn_sync (NULL, (gchar **) argv, NULL,
                               G_SPAWN_STDERR_TO_DEV_NULL,
                               NULL, NULL, &out, NULL, NULL, NULL);
  g_free (xinput);
  if (!ok || !out)
    {
      g_free (out);
      return -1;
    }

  gint id = -1;
  gchar **lines = g_strsplit (out, "\n", -1);
  g_free (out);
  for (gint i = 0; lines[i] && id < 0; i++)
    {
      gchar *lower = g_ascii_strdown (lines[i], -1);
      if (strstr (lower, "touchpad"))
        {
          /* line format: "↳ SynPS/2 Synaptics TouchPad          id=12  ..." */
          const gchar *p = strstr (lines[i], "id=");
          if (p)
            id = (gint) g_ascii_strtoll (p + 3, NULL, 10);
        }
      g_free (lower);
    }
  g_strfreev (lines);
  return id;
}

static gboolean
xinput_get (void)
{
  gchar *xinput = g_find_program_in_path ("xinput");
  if (!xinput)
    return TRUE;

  gint tp_id = xinput_touchpad_id ();
  if (tp_id < 0)
    {
      g_free (xinput);
      return TRUE;
    }

  gchar id_str[16];
  g_snprintf (id_str, sizeof id_str, "%d", tp_id);
  const gchar *argv[] = { xinput, "list-props", id_str, NULL };
  gchar *out = NULL;
  gboolean ok = g_spawn_sync (NULL, (gchar **) argv, NULL,
                               G_SPAWN_STDERR_TO_DEV_NULL,
                               NULL, NULL, &out, NULL, NULL, NULL);
  g_free (xinput);
  if (!ok || !out)
    {
      g_free (out);
      return TRUE;
    }

  /* "Device Enabled (139):   1" */
  gboolean enabled = TRUE;
  gchar **lines = g_strsplit (out, "\n", -1);
  g_free (out);
  for (gint i = 0; lines[i]; i++)
    {
      gchar *lower = g_ascii_strdown (lines[i], -1);
      if (strstr (lower, "device enabled"))
        {
          const gchar *colon = strrchr (lines[i], ':');
          if (colon)
            enabled = g_ascii_strtoll (colon + 1, NULL, 10) != 0;
        }
      g_free (lower);
    }
  g_strfreev (lines);
  return enabled;
}

static void
xinput_set (gboolean enabled)
{
  gchar *xinput = g_find_program_in_path ("xinput");
  if (!xinput)
    return;

  gint tp_id = xinput_touchpad_id ();
  if (tp_id < 0)
    {
      g_free (xinput);
      return;
    }

  gchar id_str[16];
  g_snprintf (id_str, sizeof id_str, "%d", tp_id);
  const gchar *argv[] = { xinput, enabled ? "enable" : "disable", id_str, NULL };
  g_spawn_async (NULL, (gchar **) argv, NULL,
                 (GSpawnFlags) (G_SPAWN_STDOUT_TO_DEV_NULL
                                | G_SPAWN_STDERR_TO_DEV_NULL),
                 NULL, NULL, NULL, NULL);
  g_free (xinput);
}

/* ---- public API ---- */
gboolean
acc_touchpad_get (void)
{
  if (acc_desktop_is_gnome ())
    return gnome_get ();
  return xinput_get ();
}

void
acc_touchpad_set (gboolean enabled)
{
  if (acc_desktop_is_gnome ())
    gnome_set (enabled);
  else
    xinput_set (enabled);
}
