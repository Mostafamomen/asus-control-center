/* control/nightlight.h - the night light, on GNOME and on KDE
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The night light is a desktop feature. On GNOME it is the
 * org.gnome.settings-daemon.plugins.color schema; on KDE Plasma it is the
 * NightColor group of kwinrc. The desktop is detected automatically.
 *
 * Where the app cannot reach the schema itself (a Flatpak has no GNOME
 * colour schema), the host agent does it instead.
 */
#ifndef ACC_NIGHTLIGHT_H
#define ACC_NIGHTLIGHT_H
#include <glib.h>

G_BEGIN_DECLS

typedef struct {
  gboolean available;          /* the desktop provides a night light     */
  gboolean enabled;            /* is it on right now?                    */
  gboolean has_temperature;    /* the colour temperature can be changed  */
  guint    temperature;        /* in Kelvin                             */
  gboolean has_schedule;       /* a manual schedule can be changed       */
  gboolean schedule_automatic; /* sunrise / sunset instead of a schedule */
  gdouble schedule_from;       /* hours from midnight                    */
  gdouble schedule_to;         /* hours from midnight                    */
} AccNightLight;

/* Read everything the desktop tells us about the night light. */
AccNightLight acc_nightlight_read (void);

/* Turn the night light on or off. */
gboolean acc_nightlight_write_enabled (gboolean on);

/* Change the colour temperature (Kelvin). GNOME only. */
gboolean acc_nightlight_write_temperature (guint kelvin);

/* Change the manual schedule. GNOME only. */
gboolean acc_nightlight_write_schedule (gboolean automatic,
                                        gdouble from, gdouble to);

/* The simple API the older call sites use. */
gboolean acc_nightlight_get (void);
void     acc_nightlight_set (gboolean on);
gboolean acc_nightlight_available (void);

G_END_DECLS
#endif
