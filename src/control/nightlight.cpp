/* control/nightlight.cpp - DE-aware night light toggle
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "nightlight.h"
#include "../probe/desktop.h"
#include <gio/gio.h>
#include <string.h>

#define GNOME_COLOR_SCHEMA "org.gnome.settings-daemon.plugins.color"
#define GNOME_NL_KEY       "night-light-enabled"

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
  GSettings *st = gs_open_safe (GNOME_COLOR_SCHEMA);
  if (!st)
    return FALSE;
  gboolean v = g_settings_get_boolean (st, GNOME_NL_KEY);
  g_object_unref (st);
  return v;
}

static void
gnome_set (gboolean on)
{
  GSettings *st = gs_open_safe (GNOME_COLOR_SCHEMA);
  if (!st)
    return;
  g_settings_set_boolean (st, GNOME_NL_KEY, on);
  g_object_unref (st);
}

static gboolean
gnome_available (void)
{
  GSettingsSchemaSource *src = g_settings_schema_source_get_default ();
  if (!src)
    return FALSE;
  GSettingsSchema *s = g_settings_schema_source_lookup (src, GNOME_COLOR_SCHEMA, TRUE);
  if (!s)
    return FALSE;
  g_settings_schema_unref (s);
  return TRUE;
}

/* ---- KDE backend ---- */
/* KDE stores night color in ~/.config/kwinrc [NightColor] Active=true/false.
 * We write that via kwriteconfig5/kwriteconfig6 (tries both) and then ask
 * KWin to reload its configuration via DBus so the change takes effect
 * without a restart. */

static gchar *
find_kwriteconfig (void)
{
  static const gchar *bins[] = { "kwriteconfig6", "kwriteconfig5", NULL };
  for (gint i = 0; bins[i]; i++)
    {
      gchar *p = g_find_program_in_path (bins[i]);
      if (p)
        return p;
    }
  return NULL;
}

static gboolean
kde_available (void)
{
  if (!acc_desktop_is_kde ())
    return FALSE;
  gchar *p = find_kwriteconfig ();
  gboolean ok = (p != NULL);
  g_free (p);
  return ok;
}

static gboolean
kde_get (void)
{
  /* Read kwinrc directly — kreadconfig5 output parsing is simpler. */
  static const gchar *readers[] = { "kreadconfig6", "kreadconfig5", NULL };
  for (gint i = 0; readers[i]; i++)
    {
      gchar *bin = g_find_program_in_path (readers[i]);
      if (!bin)
        continue;
      const gchar *argv[] = { bin, "--file", "kwinrc",
                               "--group", "NightColor",
                               "--key", "Active", NULL };
      gchar *out = NULL;
      gboolean ok = g_spawn_sync (NULL, (gchar **) argv, NULL,
                                  G_SPAWN_STDERR_TO_DEV_NULL,
                                  NULL, NULL, &out, NULL, NULL, NULL);
      g_free (bin);
      if (ok && out)
        {
          gboolean val = g_ascii_strcasecmp (g_strstrip (out), "true") == 0;
          g_free (out);
          return val;
        }
      g_free (out);
      break;
    }
  return FALSE;
}

static void
kde_set (gboolean on)
{
  gchar *kwrite = find_kwriteconfig ();
  if (!kwrite)
    return;

  const gchar *argv[] = { kwrite, "--file", "kwinrc",
                           "--group", "NightColor",
                           "--key", "Active",
                           on ? "true" : "false", NULL };
  g_spawn_sync (NULL, (gchar **) argv, NULL,
                (GSpawnFlags) (G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL),
                NULL, NULL, NULL, NULL, NULL, NULL);
  g_free (kwrite);

  /* Ask KWin to reload configuration so the change applies immediately. */
  static const gchar *qdbus[] = { "qdbus6", "qdbus", "qdbus-qt6", "qdbus-qt5", NULL };
  for (gint i = 0; qdbus[i]; i++)
    {
      gchar *qbin = g_find_program_in_path (qdbus[i]);
      if (!qbin)
        continue;
      const gchar *qa[] = { qbin, "org.kde.KWin",
                             "/ColorCorrect", "setEnabled",
                             on ? "true" : "false", NULL };
      g_spawn_async (NULL, (gchar **) qa, NULL,
                     (GSpawnFlags) (G_SPAWN_STDOUT_TO_DEV_NULL
                                    | G_SPAWN_STDERR_TO_DEV_NULL),
                     NULL, NULL, NULL, NULL);
      g_free (qbin);
      break;
    }
}

/* ---- public API ---- */
gboolean
acc_nightlight_available (void)
{
  if (acc_desktop_is_gnome ())
    return gnome_available ();
  if (acc_desktop_is_kde ())
    return kde_available ();
  return FALSE;
}

gboolean
acc_nightlight_get (void)
{
  if (acc_desktop_is_gnome ())
    return gnome_get ();
  if (acc_desktop_is_kde ())
    return kde_get ();
  return FALSE;
}

void
acc_nightlight_set (gboolean on)
{
  if (acc_desktop_is_gnome ())
    gnome_set (on);
  else if (acc_desktop_is_kde ())
    kde_set (on);
}
