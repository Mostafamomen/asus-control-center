/* ui/theme.c - light/dark skin state, persistence, css path, cairo colors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The GUI always ships two stylesheets next to the source tree (dev) or
 * the binary (installed): `theme.css` (MyASUS light) and `theme-dark.css`.
 * window.c picks the matching provider at boot; this module only owns the
 * *choice* and the mode-dependent color constants for cairo widgets.
 */
#include "theme.h"
#include "../probe/probe.h"

#include <gio/gio.h>

static gboolean s_dark = FALSE;
static gboolean s_gray = FALSE;

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
    }
  g_key_file_free (kf);
  g_free (p);
}

/* write one boolean without dropping the other persisted keys */
static void
save_key (const gchar *group, const gchar *key, gboolean v)
{
  gchar *dir = g_build_filename (g_get_user_config_dir (),
                                 "asus-control-center", NULL);
  /* the directory may not exist yet on a fresh profile */
  g_mkdir_with_parents (dir, 0700);
  gchar *p = g_build_filename (dir, "settings.conf", NULL);
  GKeyFile *kf = g_key_file_new ();
  g_key_file_load_from_file (kf, p, G_KEY_FILE_NONE, NULL);
  g_key_file_set_boolean (kf, group, key, v);
  GError *e = NULL;
  if (!g_key_file_save_to_file (kf, p, &e))
    {
      g_printerr ("theme pref not saved: %s\n", e ? e->message : "?");
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
  /* dev tree: <project>/asus-control-center + <project>/src/ui/<name> */
  gchar *css = g_build_filename (dir, "src", "ui", name, NULL);
  if (!acc_path_exists (css))
    {
      /* binary installed next to the stylesheet */
      g_free (css);
      css = g_build_filename (dir, name, NULL);
    }
  if (!acc_path_exists (css))
    {
      /* FHS install: /usr/local/bin + /usr/local/share/asus-control-center */
      g_free (css);
      css = g_build_filename (dir, "..", "share", "asus-control-center",
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
  if (!name)
    return NULL;
  gchar *exe = g_file_read_link ("/proc/self/exe", NULL);
  if (!exe)
    return NULL;
  gchar *dir = g_path_get_dirname (exe);
  /* dev tree first, then next to the binary, then the FHS install */
  gchar *p = g_build_filename (dir, "assets", name, NULL);
  if (!acc_path_exists (p))
    {
      g_free (p);
      p = g_build_filename (dir, name, NULL);
    }
  if (!acc_path_exists (p))
    {
      g_free (p);
      p = g_build_filename (dir, "..", "share", "asus-control-center",
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
  /* one palette in both skins: the arc is a filled shape, so the MyASUS
   * blue stays readable on #F5F7FA and on #202939 alike */
  return "#006CE1";
}

const gchar *
acc_theme_col_warn (void)
{
  return "#F59E0B";
}
