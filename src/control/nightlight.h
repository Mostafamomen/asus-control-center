/* control/nightlight.h - DE-aware night light (blue-light filter) toggle
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * On GNOME: drives org.gnome.settings-daemon.plugins.color / night-light-enabled.
 * On KDE:   writes kwinrc [NightColor] Active and signals KWin via DBus.
 * Falls back gracefully when neither is available.
 */
#ifndef ACC_NIGHTLIGHT_H
#define ACC_NIGHTLIGHT_H
#include <glib.h>

G_BEGIN_DECLS

/* Returns TRUE when night-light is currently on; FALSE when off or unavailable. */
gboolean acc_nightlight_get (void);

/* Turns night-light on (TRUE) or off (FALSE).  No-ops silently when
 * the current DE provides no supported night-light backend. */
void     acc_nightlight_set (gboolean on);

/* TRUE when a supported backend is available on this DE. */
gboolean acc_nightlight_available (void);

G_END_DECLS
#endif
