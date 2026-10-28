/* control/brightness.c - Soft Brightness Plus GSettings bridge (user-level)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Security notes:
 *  - reads the schema from the user-owned extension dir only
 *  - writes only the whitelisted key "current-brightness"
 *  - value clamped to [10,100] before storing
 *  - never invokes pkexec/sudo; pure dconf (user database)
 */
#include "brightness.h"
#include "../probe/probe.h"
#include <gio/gio.h>

#define SBP_UUID   "soft-brightness-plus@joelkitching.com"
#define SBP_SCHEMA "org.gnome.shell.extensions.soft-brightness-plus"
#define KEY_CUR    "current-brightness"
#define MIN_PCT    10.0
#define MAX_PCT    100.0

static GSettings *g_settings_obj = NULL;
static GSettingsSchemaSource *g_source = NULL;
static GSettingsSchema *g_schema = NULL;

gboolean
acc_brightness_available (void)
{
  return g_settings_obj != NULL;
}

gdouble
acc_brightness_get (void)
{
  if (!acc_brightness_available ())
    return -1;
  return g_settings_get_double (g_settings_obj, KEY_CUR) * 100.0;
}

void
acc_brightness_set (gdouble pct)
{
  if (!acc_brightness_available ())
    return;
  if (pct < MIN_PCT)
    pct = MIN_PCT;
  if (pct > MAX_PCT)
    pct = MAX_PCT;
  g_settings_set_double (g_settings_obj, KEY_CUR, pct / 100.0);
}

gboolean
acc_brightness_init (GCallback onchange, gpointer user)
{
  gchar *dir = g_build_filename (g_get_home_dir (), ".local/share",
                                 "gnome-shell", "extensions", SBP_UUID,
                                 "schemas", NULL);
  if (!acc_path_exists (dir))
    {
      g_free (dir);
      return FALSE;
    }

  GError *err = NULL;
  g_source = g_settings_schema_source_new_from_directory (dir,
                                                          g_settings_schema_source_get_default (),
                                                          FALSE, &err);
  g_free (dir);
  if (!g_source || err)
    {
      g_clear_error (&err);
      return FALSE;
    }

  g_schema = g_settings_schema_source_lookup (g_source, SBP_SCHEMA, TRUE);
  if (!g_schema)
    return FALSE;

  g_settings_obj = g_settings_new_full (g_schema, NULL, NULL);
  if (onchange)
    g_signal_connect (g_settings_obj, "changed::" KEY_CUR, onchange, user);
  return acc_brightness_available ();
}

void
acc_brightness_shutdown (void)
{
  g_clear_object (&g_settings_obj);
  g_clear_pointer (&g_schema, g_settings_schema_unref);
  g_clear_pointer (&g_source, g_settings_schema_source_unref);
}
