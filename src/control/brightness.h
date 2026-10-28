/* control/brightness.h - Soft Brightness Plus GSettings bridge (user-level)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Read/write ONLY the installed GNOME extension's schema. No root involved:
 * this is the user's own dconf database. Only the whitelisted key
 * "current-brightness" is ever written.
 */
#ifndef ACC_BRIGHTNESS_H
#define ACC_BRIGHTNESS_H
#include <gio/gio.h>

G_BEGIN_DECLS

gboolean acc_brightness_init (GCallback onchange, gpointer user);
void     acc_brightness_shutdown (void);
gboolean acc_brightness_available (void);
gdouble  acc_brightness_get (void);        /* 0..100, -1 unavailable */
void     acc_brightness_set (gdouble pct); /* clamped 10..100 */

G_END_DECLS
#endif
