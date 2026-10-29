/* control/softdim.cpp - install/enable the bundled Soft Brightness Plus
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Security notes:
 *  - writes only inside the user's own ~/.local/share/gnome-shell/extensions
 *  - copies a fixed list of files from the bundled folder, nothing else
 *  - runs /usr/bin/glib-compile-schemas and /usr/bin/gnome-extensions with
 *    fixed argv (no shell), never pkexec/sudo
 */
#include "softdim.h"
#include "../probe/probe.h"

#include <gio/gio.h>
#include <string.h>

#define SBP_UUID     "soft-brightness-plus@joelkitching.com"
#define SBP_SCHEMA   "org.gnome.shell.extensions.soft-brightness-plus"
#define SBP_XML      SBP_SCHEMA ".gschema.xml"
#define SHELL_SCHEMA "org.gnome.shell"
#define COMPILE_BIN  "/usr/bin/glib-compile-schemas"
#define EXT_BIN      "/usr/bin/gnome-extensions"

/* the upstream files the extension needs, relative to its folder */
static const gchar *const k_files[] = {
  "metadata.json", "extension.js", "prefs.js", "cursor.js", "logger.js",
  "utils.js", "LICENSE",
  "schemas/" SBP_XML,
  "dbus-interfaces/org.gnome.Mutter.DisplayConfig.xml",
  NULL
};

static gchar *
user_ext_dir (void)
{
  return g_build_filename (g_get_user_data_dir (), "gnome-shell",
                           "extensions", SBP_UUID, NULL);
}

/* data/soft-brightness-plus beside the binary (dev / portable), or the
 * packaged copy under /usr/share */
static gchar *
bundle_dir (void)
{
  gchar *exe = g_file_read_link ("/proc/self/exe", NULL);
  if (exe)
    {
      gchar *dir = g_path_get_dirname (exe);
      gchar *p = g_build_filename (dir, "data", "soft-brightness-plus", NULL);
      g_free (dir);
      g_free (exe);
      gchar *meta = g_build_filename (p, "metadata.json", NULL);
      gboolean ok = acc_path_exists (meta);
      g_free (meta);
      if (ok)
        return p;
      g_free (p);
    }
  const gchar *sys = "/usr/share/asus-control-center/soft-brightness-plus";
  gchar *meta = g_build_filename (sys, "metadata.json", NULL);
  gboolean ok = acc_path_exists (meta);
  g_free (meta);
  return ok ? g_strdup (sys) : NULL;
}

/* org.gnome.shell is absent outside GNOME: never let GSettings abort */
static GSettings *
shell_settings (void)
{
  GSettingsSchemaSource *src = g_settings_schema_source_get_default ();
  GSettingsSchema *s = src ? g_settings_schema_source_lookup
                               (src, SHELL_SCHEMA, TRUE) : NULL;
  if (!s)
    return NULL;
  g_settings_schema_unref (s);
  return g_settings_new (SHELL_SCHEMA);
}

static gboolean
installed (void)
{
  gchar *u = user_ext_dir ();
  gchar *um = g_build_filename (u, "metadata.json", NULL);
  gchar *sm = g_build_filename ("/usr/share/gnome-shell/extensions",
                                SBP_UUID, "metadata.json", NULL);
  gboolean ok = acc_path_exists (um) || acc_path_exists (sm);
  g_free (sm);
  g_free (um);
  g_free (u);
  return ok;
}

AccSoftDim
acc_softdim_status (void)
{
  if (!installed ())
    return ACC_SD_MISSING;

  GSettings *gs = shell_settings ();
  if (!gs)
    return ACC_SD_DISABLED;
  gchar **on = g_settings_get_strv (gs, "enabled-extensions");
  gboolean en = on && g_strv_contains ((const gchar *const *) on, SBP_UUID);
  gboolean blocked = g_settings_get_boolean (gs, "disable-user-extensions");
  g_strfreev (on);
  g_object_unref (gs);
  return en && !blocked ? ACC_SD_ON : ACC_SD_DISABLED;
}

gboolean
acc_softdim_bundled (void)
{
  gchar *b = bundle_dir ();
  g_free (b);
  return b != NULL;
}

static gboolean
run_fixed (const gchar *const *argv, gchar **err)
{
  GError *e = NULL;
  gint status = 0;
  gchar *errout = NULL;
  if (!g_spawn_sync (NULL, (gchar **) argv, NULL, G_SPAWN_STDOUT_TO_DEV_NULL,
                     NULL, NULL, NULL, &errout, &status, &e)
      || !g_spawn_check_wait_status (status, NULL))
    {
      if (err)
        *err = g_strdup (e ? e->message
                           : errout && *errout ? g_strstrip (errout)
                                               : argv[0]);
      g_clear_error (&e);
      g_free (errout);
      return FALSE;
    }
  g_free (errout);
  return TRUE;
}

/* point the dimming at the internal (OLED) panel; leave the rest at the
 * extension's own defaults */
static void
prefer_builtin (const gchar *schemas_dir)
{
  GSettingsSchemaSource *src = g_settings_schema_source_new_from_directory
    (schemas_dir, g_settings_schema_source_get_default (), FALSE, NULL);
  if (!src)
    return;
  GSettingsSchema *s = g_settings_schema_source_lookup (src, SBP_SCHEMA,
                                                        FALSE);
  if (s)
    {
      GSettings *st = g_settings_new_full (s, NULL, NULL);
      g_settings_set_string (st, "monitors", "built-in");
      g_settings_sync ();
      g_object_unref (st);
      g_settings_schema_unref (s);
    }
  g_settings_schema_source_unref (src);
}

gboolean
acc_softdim_install (gchar **err)
{
  if (err)
    *err = NULL;

  gchar *from = bundle_dir ();
  if (!from)
    {
      if (err)
        *err = g_strdup ("bundled extension not found");
      return FALSE;
    }

  gchar *to = user_ext_dir ();
  gboolean ok = TRUE;
  for (guint i = 0; ok && k_files[i]; i++)
    {
      gchar *src = g_build_filename (from, k_files[i], NULL);
      gchar *dst = g_build_filename (to, k_files[i], NULL);
      gchar *dst_dir = g_path_get_dirname (dst);
      g_mkdir_with_parents (dst_dir, 0755);

      GError *e = NULL;
      GFile *fs = g_file_new_for_path (src);
      GFile *fd = g_file_new_for_path (dst);
      if (!g_file_copy (fs, fd, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &e))
        {
          ok = FALSE;
          if (err)
            *err = g_strdup (e ? e->message : k_files[i]);
        }
      g_clear_error (&e);
      g_object_unref (fd);
      g_object_unref (fs);
      g_free (dst_dir);
      g_free (dst);
      g_free (src);
    }

  if (ok)
    {
      gchar *schemas = g_build_filename (to, "schemas", NULL);
      const gchar *argv[] = { COMPILE_BIN, schemas, NULL };
      ok = acc_path_exists (COMPILE_BIN) && run_fixed (argv, err);
      if (!ok && err && !*err)
        *err = g_strdup ("glib-compile-schemas not found");
      if (ok)
        prefer_builtin (schemas);
      g_free (schemas);
    }

  g_free (to);
  g_free (from);
  return ok && acc_softdim_enable (err);
}

gboolean
acc_softdim_enable (gchar **err)
{
  if (err && *err)
    g_clear_pointer (err, g_free);

  GSettings *gs = shell_settings ();
  if (!gs)
    {
      if (err)
        *err = g_strdup ("GNOME Shell settings not found");
      return FALSE;
    }

  /* enabled-extensions += UUID, disabled-extensions -= UUID */
  gchar **on = g_settings_get_strv (gs, "enabled-extensions");
  if (!g_strv_contains ((const gchar *const *) on, SBP_UUID))
    {
      GStrvBuilder *b = g_strv_builder_new ();
      g_strv_builder_addv (b, (const char **) on);
      g_strv_builder_add (b, SBP_UUID);
      gchar **nv = g_strv_builder_end (b);
      g_settings_set_strv (gs, "enabled-extensions",
                           (const gchar *const *) nv);
      g_strfreev (nv);
      g_strv_builder_unref (b);
    }
  g_strfreev (on);

  gchar **off = g_settings_get_strv (gs, "disabled-extensions");
  if (g_strv_contains ((const gchar *const *) off, SBP_UUID))
    {
      GStrvBuilder *b = g_strv_builder_new ();
      for (guint i = 0; off[i]; i++)
        if (strcmp (off[i], SBP_UUID) != 0)
          g_strv_builder_add (b, off[i]);
      gchar **nv = g_strv_builder_end (b);
      g_settings_set_strv (gs, "disabled-extensions",
                           (const gchar *const *) nv);
      g_strfreev (nv);
      g_strv_builder_unref (b);
    }
  g_strfreev (off);
  g_settings_sync ();
  g_object_unref (gs);

  /* a shell that already knows the extension enables it right away; a
   * fresh install is picked up at the next login, so ignore the result */
  if (acc_path_exists (EXT_BIN))
    {
      const gchar *argv[] = { EXT_BIN, "enable", SBP_UUID, NULL };
      g_spawn_async (NULL, (gchar **) argv, NULL,
                     (GSpawnFlags) (G_SPAWN_STDOUT_TO_DEV_NULL
                                    | G_SPAWN_STDERR_TO_DEV_NULL),
                     NULL, NULL, NULL, NULL);
    }
  return TRUE;
}
