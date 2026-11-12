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
#include "softdim.h"
#include "hostcmd.h"
#include "agent.h"
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
/* the default schema source belongs to GLib: it must never be unreffed */
static gboolean g_source_ours = FALSE;

gboolean
acc_brightness_available (void)
{
  return g_settings_obj != NULL;
}

gdouble
acc_brightness_get (void)
{
  /* The value lives in the desktop session's dconf. A sandbox cannot be
   * trusted to reach the host's database (the shared-memory snapshot is
   * not always reachable), so the host agent reads it there. */
  if (acc_host_agent_ready ())
    {
      gchar *v = NULL, *err = NULL;
      if (acc_agent_brightness_get (&v, &err))
        {
          gdouble pct = v ? g_ascii_strtod (v, NULL) : -1.0;
          g_free (v);
          g_free (err);
          return pct;
        }
      g_free (v);
      g_free (err);
    }

  if (!acc_brightness_available ())
    return -1;
  return g_settings_get_double (g_settings_obj, KEY_CUR) * 100.0;
}

void
acc_brightness_set (gdouble pct)
{
  if (pct < MIN_PCT)
    pct = MIN_PCT;
  if (pct > MAX_PCT)
    pct = MAX_PCT;

  if (acc_host_agent_ready ())
    {
      gchar *v = g_strdup_printf ("%.0f", pct);
      gchar *err = NULL;
      gboolean ok = acc_agent_brightness_set (v, &err);
      g_free (err);
      g_free (v);
      if (ok)
        return;
    }

  if (!acc_brightness_available ())
    return;
  g_settings_set_double (g_settings_obj, KEY_CUR, pct / 100.0);
}

gboolean
acc_brightness_init (GCallback onchange, gpointer user)
{
  /* The schema is looked up in two places:
   *   - the installed schemas (a Flatpak ships the schema, so the value
   *     reaches the host's dconf even though the extension itself lives
   *     on the host);
   *   - the user's own extension folder (a native install, where the
   *     schema only exists there).
   */
  g_source = g_settings_schema_source_get_default ();
  g_source_ours = FALSE;
  if (g_source)
    g_schema = g_settings_schema_source_lookup (g_source, SBP_SCHEMA, TRUE);

  if (!g_schema)
    {
      gchar *dir = g_build_filename (g_get_home_dir (), ".local/share",
                                     "gnome-shell", "extensions", SBP_UUID,
                                     "schemas", NULL);
      if (acc_path_exists (dir))
        {
          GError *err = NULL;
          g_source = g_settings_schema_source_new_from_directory
            (dir, g_settings_schema_source_get_default (), FALSE, &err);
          if (g_source && !err)
            {
              g_schema = g_settings_schema_source_lookup (g_source, SBP_SCHEMA,
                                                          TRUE);
              g_source_ours = TRUE;
            }
          else
            g_source = NULL;
          g_clear_error (&err);
        }
      g_free (dir);
    }

  if (!g_schema)
    return FALSE;

  /* The extension itself belongs to the host session: without it there
   * is nothing to dim (the agent knows whether it is installed). */
  if (acc_softdim_status () == ACC_SD_MISSING)
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
  if (g_source_ours)
    g_clear_pointer (&g_source, g_settings_schema_source_unref);
  else
    g_source = NULL;
}
