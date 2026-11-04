
/*
 * ui/theme.cpp - light/dark skin state, persistence, CSS path, Cairo colors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "theme.h"
#include "../probe/probe.h"

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>

static gboolean s_dark = FALSE;
static gboolean s_gray = FALSE;
static gboolean s_notify = TRUE;

static gchar *
pref_path (void)
{
  return g_build_filename (g_get_user_config_dir (),
                           "asus-control-center", "settings.conf", NULL);
}

void
acc_theme_load_pref (void)
{
  gchar *p = pref_path ();
  GKeyFile *kf = g_key_file_new ();

  if (g_key_file_load_from_file (kf, p, G_KEY_FILE_NONE, NULL))
    {
      s_dark = g_key_file_get_boolean (kf, "theme", "dark", NULL);
      s_gray = g_key_file_get_boolean (kf, "display", "gray", NULL);
      if (g_key_file_has_key (kf, "app", "notifications", NULL))
        s_notify = g_key_file_get_boolean (kf, "app", "notifications", NULL);
    }

  g_key_file_free (kf);
  g_free (p);
}

/* Write one boolean without dropping the other persisted keys. */
static void
save_key (const gchar *group, const gchar *key, gboolean v)
{
  gchar *dir = g_build_filename (g_get_user_config_dir (),
                                 "asus-control-center", NULL);

  /* The directory may not exist yet on a fresh profile. */
  g_mkdir_with_parents (dir, 0700);

  gchar *p = g_build_filename (dir, "settings.conf", NULL);
  GKeyFile *kf = g_key_file_new ();

  g_key_file_load_from_file (kf, p, G_KEY_FILE_NONE, NULL);
  g_key_file_set_boolean (kf, group, key, v);

  GError *e = NULL;
  if (!g_key_file_save_to_file (kf, p, &e))
    {
      g_printerr ("theme pref not saved: %s\n",
                  e ? e->message : "?");
      g_clear_error (&e);
    }

  g_key_file_free (kf);
  g_free (p);
  g_free (dir);
}

void
acc_theme_save_pref (gboolean dark)
{
  s_dark = dark;
  save_key ("theme", "dark", dark);
}

gboolean
acc_theme_gray_pref (void)
{
  return s_gray;
}

void
acc_theme_save_gray (gboolean gray)
{
  s_gray = gray;
  save_key ("display", "gray", gray);
}

gboolean
acc_pref_notifications (void)
{
  return s_notify;
}

void
acc_pref_set_notifications (gboolean on)
{
  s_notify = on;
  save_key ("app", "notifications", on);
}

static gchar *
autostart_path (void)
{
  return g_build_filename (g_get_user_config_dir (), "autostart",
                           "io.github.asuscontrolcenter.AsusControlCenter.desktop",
                           NULL);
}

gboolean
acc_autostart_enabled (void)
{
  gchar *p = autostart_path ();
  gboolean on = g_file_test (p, G_FILE_TEST_IS_REGULAR);
  g_free (p);
  return on;
}

gboolean
acc_autostart_set (gboolean on, gchar **err)
{
  gchar *p = autostart_path ();
  gboolean ok = TRUE;

  if (!on)
    {
      if (g_file_test (p, G_FILE_TEST_EXISTS))
        ok = g_remove (p) == 0;
      if (!ok && err)
        *err = g_strdup ("Could not remove the autostart entry.");
      g_free (p);
      return ok;
    }

  gchar *dir = g_path_get_dirname (p);
  g_mkdir_with_parents (dir, 0700);
  g_free (dir);

  gchar *exe = g_file_read_link ("/proc/self/exe", NULL);
  if (!exe)
    {
      if (err)
        *err = g_strdup ("Could not find the ASUSCraft executable.");
      g_free (p);
      return FALSE;
    }

  gchar *quoted = g_shell_quote (exe);
  gchar *body = g_strdup_printf (
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=ASUSCraft\n"
    "Comment=Intelligent ASUS Laptop Management\n"
    "Exec=%s\n"
    "Icon=io.github.asuscontrolcenter.AsusControlCenter\n"
    "Terminal=false\n"
    "X-GNOME-Autostart-enabled=true\n"
    "X-KDE-autostart-enabled=true\n",
    quoted);
  GError *e = NULL;
  ok = g_file_set_contents (p, body, -1, &e);
  if (!ok && err)
    *err = g_strdup (e ? e->message : "Could not write the autostart entry.");
  g_clear_error (&e);
  g_free (body);
  g_free (quoted);
  g_free (exe);
  g_free (p);
  return ok;
}

gboolean
acc_theme_is_dark (void)
{
  return s_dark;
}

gchar *
acc_theme_css_path (void)
{
  gchar *exe = g_file_read_link ("/proc/self/exe", NULL);
  if (!exe)
    return NULL;

  gchar *dir = g_path_get_dirname (exe);
  const gchar *name = s_dark ? "theme-dark.css" : "theme.css";

  /* Development tree: <project>/src/ui/<name>. */
  gchar *css = g_build_filename (dir, "src", "ui", name, NULL);

  /* Portable installation: stylesheet beside the executable. */
  if (!acc_path_exists (css))
    {
      g_free (css);
      css = g_build_filename (dir, name, NULL);
    }

  /* Fedora RPM installation. */
  if (!acc_path_exists (css))
    {
      g_free (css);
      css = g_build_filename ("/usr/share/asus-control-center",
                              name, NULL);
    }

  if (!acc_path_exists (css))
    {
      g_free (css);
      css = NULL;
    }

  g_free (dir);
  g_free (exe);
  return css;
}

gchar *
acc_asset_path (const gchar *name)
{
  if (!name || !*name)
    return NULL;

  gchar *exe = g_file_read_link ("/proc/self/exe", NULL);
  if (!exe)
    return NULL;

  gchar *dir = g_path_get_dirname (exe);

  /* Development or portable installation. */
  gchar *p = g_build_filename (dir, "assets", name, NULL);

  if (!acc_path_exists (p))
    {
      g_free (p);
      p = g_build_filename (dir, name, NULL);
    }

  /* Fedora RPM installation. */
  if (!acc_path_exists (p))
    {
      g_free (p);
      p = g_build_filename ("/usr/share/asus-control-center",
                            name, NULL);
    }

  if (!acc_path_exists (p))
    {
      g_free (p);
      p = NULL;
    }

  g_free (dir);
  g_free (exe);
  return p;
}

const gchar *
acc_theme_col_accent (void)
{
  /* ASUSCraft teal/cyan identity; the dark skin uses a brighter tint. */
  return s_dark ? "#22D3EE" : "#0891B2";
}

const gchar *
acc_theme_col_warn (void)
{
  return s_dark ? "#FCB040" : "#F59E0B";
}
