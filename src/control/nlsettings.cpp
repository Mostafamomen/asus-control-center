/* control/nlsettings.cpp - the GNOME night light settings
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "nlsettings.h"

#include <gio/gio.h>
#include <string.h>

#define NL_SCHEMA "org.gnome.settings-daemon.plugins.color"

/* The ONLY keys this code ever reads or writes. Everything else in the
 * user's colour settings is left exactly as it is. */
static const gchar *const nl_keys[] = {
  "night-light-enabled",
  "night-light-temperature",
  "night-light-schedule-automatic",
  "night-light-schedule-from",
  "night-light-schedule-to",
  "night-light-schedule",          /* older GNOME only */
  NULL
};

/* the ranges GNOME itself uses */
#define NL_TEMP_MIN 1700.0
#define NL_TEMP_MAX 4700.0
#define NL_HOURS_MAX 24.0
#define NL_MINUTES_MAX 1439

static GSettingsSchema *
nl_schema (void)
{
  GSettingsSchemaSource *src = g_settings_schema_source_get_default ();
  if (!src)
    return NULL;
  return g_settings_schema_source_lookup (src, NL_SCHEMA, TRUE);
}

static GSettings *
nl_settings (void)
{
  GSettingsSchema *s = nl_schema ();
  if (!s)
    return NULL;
  g_settings_schema_unref (s);
  return g_settings_new (NL_SCHEMA);
}

static gboolean
nl_has_key (const gchar *key)
{
  GSettingsSchema *s = nl_schema ();
  if (!s)
    return FALSE;
  gboolean found = g_settings_schema_has_key (s, key);
  g_settings_schema_unref (s);
  return found;
}

gboolean
acc_nl_available (void)
{
  GSettings *st = nl_settings ();
  if (!st)
    return FALSE;
  g_object_unref (st);
  return TRUE;
}

gchar *
acc_nl_read (void)
{
  GSettings *st = nl_settings ();
  if (!st)
    return NULL;

  GKeyFile *kf = g_key_file_new ();
  for (int i = 0; nl_keys[i]; i++)
    {
      if (!nl_has_key (nl_keys[i]))
        continue;
      GVariant *v = g_settings_get_value (st, nl_keys[i]);
      if (!v)
        continue;
      const GVariantType *t = g_variant_get_type (v);
      gchar *text = NULL;
      if (g_variant_type_equal (t, G_VARIANT_TYPE_BOOLEAN))
        text = g_strdup (g_variant_get_boolean (v) ? "true" : "false");
      else if (g_variant_type_equal (t, G_VARIANT_TYPE_UINT32))
        text = g_strdup_printf ("%u", g_variant_get_uint32 (v));
      else if (g_variant_type_equal (t, G_VARIANT_TYPE_INT32))
        text = g_strdup_printf ("%d", g_variant_get_int32 (v));
      else if (g_variant_type_equal (t, G_VARIANT_TYPE_DOUBLE))
        text = g_strdup_printf ("%g", g_variant_get_double (v));
      else if (g_variant_type_equal (t, G_VARIANT_TYPE_STRING))
        text = g_strdup (g_variant_get_string (v, NULL));
      if (text)
        {
          g_key_file_set_string (kf, "night-light", nl_keys[i], text);
          g_free (text);
        }
      g_variant_unref (v);
    }

  gchar *text = g_key_file_to_data (kf, NULL, NULL);
  g_key_file_free (kf);
  g_object_unref (st);
  return text;
}

gboolean
acc_nl_write (const gchar *key, const gchar *value, gchar **err)
{
  if (err)
    *err = NULL;

  if (!g_strv_contains ((const gchar *const *) nl_keys, key))
    {
      if (err)
        *err = g_strdup ("unknown night-light key");
      return FALSE;
    }
  if (!value || !*value)
    {
      if (err)
        *err = g_strdup ("the value is empty");
      return FALSE;
    }

  GSettings *st = nl_settings ();
  if (!st)
    {
      if (err)
        *err = g_strdup ("this desktop does not provide the GNOME night light");
      return FALSE;
    }
  if (!nl_has_key (key))
    {
      g_object_unref (st);
      if (err)
        *err = g_strdup_printf ("this desktop does not provide %s", key);
      return FALSE;
    }

  /* the type of the existing value decides how the new one is written, so
   * the value keeps the shape the desktop expects (minutes on old GNOME,
   * hours on new GNOME) */
  GVariant *cur = g_settings_get_value (st, key);
  if (!cur)
    {
      g_object_unref (st);
      if (err)
        *err = g_strdup ("could not read the current value");
      return FALSE;
    }
  const GVariantType *t = g_variant_get_type (cur);
  gboolean is_temp = g_strcmp0 (key, "night-light-temperature") == 0;

  if (g_variant_type_equal (t, G_VARIANT_TYPE_BOOLEAN))
    {
      if (g_strcmp0 (value, "true") != 0 && g_strcmp0 (value, "false") != 0)
        {
          g_variant_unref (cur);
          g_object_unref (st);
          if (err)
            *err = g_strdup ("the value must be true or false");
          return FALSE;
        }
      g_settings_set_boolean (st, key, g_strcmp0 (value, "true") == 0);
    }
  else if (g_variant_type_equal (t, G_VARIANT_TYPE_UINT32))
    {
      gchar *end = NULL;
      gint64 v = g_ascii_strtoll (value, &end, 10);
      gint64 lo = is_temp ? (gint64) NL_TEMP_MIN : 0;
      gint64 hi = is_temp ? (gint64) NL_TEMP_MAX : NL_MINUTES_MAX;
      if (!end || *end || end == value || v < lo || v > hi)
        {
          g_variant_unref (cur);
          g_object_unref (st);
          if (err)
            *err = g_strdup_printf ("the value must be a whole number between %"
                                    G_GINT64_FORMAT " and %" G_GINT64_FORMAT,
                                    lo, hi);
          return FALSE;
        }
      g_settings_set_uint (st, key, (guint) v);
    }
  else if (g_variant_type_equal (t, G_VARIANT_TYPE_DOUBLE))
    {
      gchar *end = NULL;
      gdouble v = g_ascii_strtod (value, &end);
      gdouble lo = is_temp ? NL_TEMP_MIN : 0.0;
      gdouble hi = is_temp ? NL_TEMP_MAX : NL_HOURS_MAX;
      if (!end || *end || end == value || v < lo || v > hi)
        {
          g_variant_unref (cur);
          g_object_unref (st);
          if (err)
            *err = g_strdup_printf ("the value must be a number between %g and %g",
                                    lo, hi);
          return FALSE;
        }
      g_settings_set_double (st, key, v);
    }
  else if (g_variant_type_equal (t, G_VARIANT_TYPE_STRING))
    {
      if (g_strcmp0 (value, "manual") != 0
          && g_strcmp0 (value, "night-only") != 0
          && g_strcmp0 (value, "sunset-to-sunrise") != 0)
        {
          g_variant_unref (cur);
          g_object_unref (st);
          if (err)
            *err = g_strdup ("unknown night-light schedule");
          return FALSE;
        }
      g_settings_set_string (st, key, value);
    }
  else
    {
      g_variant_unref (cur);
      g_object_unref (st);
      if (err)
        *err = g_strdup ("the value has a type this desktop does not accept");
      return FALSE;
    }

  g_variant_unref (cur);
  g_settings_sync ();
  g_object_unref (st);
  return TRUE;
}
