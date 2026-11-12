/* control/touchpadctl.h - the touchpad switch, through the desktop's own
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The touchpad is switched through the DESKTOP's supported setting - the
 * GNOME periphls touchpad schema. The old xinput path is gone: xinput
 * only ever saw X11 devices, so it did nothing on a Wayland session, and
 * it is not something a Flatpak may run.
 *
 * Where the desktop provides no such setting, the control reports that it
 * is unsupported instead of pretending to work.
 */
#ifndef ACC_TOUCHPADCTL_H
#define ACC_TOUCHPADCTL_H
#include <glib.h>

G_BEGIN_DECLS

/* TRUE when the running desktop provides a setting ASUSCraft can use. */
gboolean     acc_touchpad_available (void);

/* Which backend is in use: "gnome" or "" when there is none. */
const gchar *acc_touchpad_backend (void);

/* The switch state. Meaningful only when acc_touchpad_available() is
 * TRUE; otherwise the value is TRUE (the safe assumption). */
gboolean     acc_touchpad_get (void);
void         acc_touchpad_set (gboolean enabled);

G_END_DECLS
#endif
