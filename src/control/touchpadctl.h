/* control/touchpadctl.h - DE-aware touchpad enable/disable
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * On GNOME: drives org.gnome.desktop.peripherals.touchpad / send-events.
 * On KDE / other X11:  uses xinput to find the first touchpad device and
 *   toggles its "Device Enabled" property (libinput-based drivers).
 * On Wayland without GNOME: falls back to xinput when available.
 */
#ifndef ACC_TOUCHPADCTL_H
#define ACC_TOUCHPADCTL_H
#include <glib.h>

G_BEGIN_DECLS

/* Returns TRUE when the touchpad is enabled; FALSE when disabled or unknown. */
gboolean acc_touchpad_get (void);

/* Enables (TRUE) or disables (FALSE) the touchpad. */
void     acc_touchpad_set (gboolean enabled);

G_END_DECLS
#endif
