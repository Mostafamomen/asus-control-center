/* probe/probe.h - shared read-only sysfs helpers
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_PROBE_H
#define ACC_PROBE_H
#include <glib.h>

G_BEGIN_DECLS

gchar *acc_read_trim (const gchar *path);
gint acc_read_int (const gchar *path, gint fallback);
gboolean acc_path_exists (const gchar *path);

G_END_DECLS
#endif
