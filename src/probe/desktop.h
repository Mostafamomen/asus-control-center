/* probe/desktop.h - desktop environment detection
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_DESKTOP_H
#define ACC_DESKTOP_H
#include <glib.h>

G_BEGIN_DECLS

/* Returns TRUE when running under GNOME (or a GNOME-compatible shell). */
gboolean acc_desktop_is_gnome (void);

/* Returns TRUE when running under KDE Plasma. */
gboolean acc_desktop_is_kde (void);

G_END_DECLS
#endif
