/* control/nlsettings.h - the GNOME night light settings
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The night light is a desktop feature, not a hardware one: on GNOME it
 * lives in org.gnome.settings-daemon.plugins.color. This module is the
 * only place that reads or writes that schema.
 *
 * It is compiled into BOTH the app and the host agent, so the two never
 * disagree about what the night light is set to.
 *
 * Only the six keys listed in nlsettings.cpp are ever touched. Every other
 * setting in the user's colour schema is left exactly as it was.
 */
#ifndef ACC_NLSETTINGS_H
#define ACC_NLSETTINGS_H
#include <glib.h>

G_BEGIN_DECLS

/* TRUE when this machine has the GNOME colour schema. */
gboolean acc_nl_available (void);

/* Read the night light settings into a keyfile text:
 *   [night-light]
 *   night-light-enabled=true
 *   night-light-temperature=3000
 *   ...
 * Returns NULL when the schema is not there. */
gchar *acc_nl_read (void);

/* Write one key. The value has to fit the key's own type and range.
 * Returns FALSE and sets *err when the key or the value is refused. */
gboolean acc_nl_write (const gchar *key, const gchar *value, gchar **err);

G_END_DECLS
#endif
