/* control/backlight.h - hardware panel backlight (user-level, via logind)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The normal backlight, on its own and separate from the OLED software
 * dimming in brightness.h. Reads /sys/class/backlight; writes go through
 * systemd-logind's Session.SetBrightness, which lets the user of the
 * active local session set the level without root, pkexec or udev rules.
 */
#ifndef ACC_BACKLIGHT_H
#define ACC_BACKLIGHT_H
#include <glib.h>

G_BEGIN_DECLS

gboolean acc_backlight_available (void);
gdouble  acc_backlight_get (void);        /* 0..100, -1 unavailable      */
/* Asynchronous, never blocks the UI; clamped 1..100 so the panel is never
 * switched fully off. The last request wins while one is in flight. */
void     acc_backlight_set (gdouble pct);

G_END_DECLS
#endif
