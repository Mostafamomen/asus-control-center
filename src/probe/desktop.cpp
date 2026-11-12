/* probe/desktop.cpp - desktop environment detection
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Checks $XDG_CURRENT_DESKTOP and $DESKTOP_SESSION, which are set by the
 * display manager before the session starts and are reliable on all major
 * DEs.  Results are cached after the first call (the variables never change
 * during a session).
 */
#include "desktop.h"
#include <glib.h>
#include <string.h>

static int s_gnome = -1;  /* -1 = not yet checked */
static int s_kde   = -1;

static gboolean
contains_token (const gchar *haystack, const gchar *needle)
{
  if (!haystack || !needle)
    return FALSE;
  /* $XDG_CURRENT_DESKTOP can be a colon-separated list, e.g. "ubuntu:GNOME" */
  gchar **parts = g_strsplit (haystack, ":", -1);
  gboolean found = FALSE;
  for (gint i = 0; parts[i] && !found; i++)
    found = g_ascii_strcasecmp (g_strstrip (parts[i]), needle) == 0;
  g_strfreev (parts);
  return found;
}

gboolean
acc_desktop_is_gnome (void)
{
  if (s_gnome >= 0)
    return (gboolean) s_gnome;

  const gchar *xdg = g_getenv ("XDG_CURRENT_DESKTOP");
  const gchar *ds  = g_getenv ("DESKTOP_SESSION");

  s_gnome = (contains_token (xdg, "GNOME") ||
             contains_token (xdg, "Unity") ||
             (ds && (g_str_has_prefix (ds, "gnome") ||
                     g_str_has_prefix (ds, "ubuntu")))) ? 1 : 0;
  return (gboolean) s_gnome;
}

gboolean
acc_desktop_is_kde (void)
{
  if (s_kde >= 0)
    return (gboolean) s_kde;

  const gchar *xdg = g_getenv ("XDG_CURRENT_DESKTOP");
  const gchar *ds  = g_getenv ("DESKTOP_SESSION");

  s_kde = (contains_token (xdg, "KDE") ||
           (ds && (g_str_has_prefix (ds, "plasma") ||
                   g_ascii_strcasecmp (ds, "kde") == 0))) ? 1 : 0;
  return (gboolean) s_kde;
}
