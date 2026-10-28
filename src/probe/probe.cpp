/* probe/probe.c - shared read-only sysfs helpers
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "probe.h"
#include <unistd.h>

gchar *
acc_read_trim (const gchar *path)
{
  gchar *c = NULL;
  if (!g_file_get_contents (path, &c, NULL, NULL))
    return NULL;
  return g_strstrip (c);
}

gint
acc_read_int (const gchar *path, gint fallback)
{
  gchar *s = acc_read_trim (path);
  if (!s)
    return fallback;
  gint v = (gint) strtoll (s, NULL, 10);
  g_free (s);
  return v;
}

gboolean
acc_path_exists (const gchar *path)
{
  return access (path, F_OK) == 0;
}
