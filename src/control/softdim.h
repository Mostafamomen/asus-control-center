/* control/softdim.h - install/enable the bundled Soft Brightness Plus
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * OLED dimming below the panel minimum is done by the GNOME Shell extension
 * Soft Brightness Plus (GPL-3.0-or-later), shipped unmodified in
 * data/soft-brightness-plus/.
 *
 * The extension has to be installed into the USER's own extension folder,
 * which belongs to the desktop session:
 *
 *   - a native install does it here;
 *   - a Flatpak cannot see that folder, so the host agent does it.
 *
 * brightness.h keeps driving the slider afterwards: the dimming VALUE is a
 * GSettings key, and GSettings works across the sandbox boundary.
 */
#ifndef ACC_SOFTDIM_H
#define ACC_SOFTDIM_H
#include <glib.h>
#include "sbp_local.h"     /* AccSoftDim */

G_BEGIN_DECLS

AccSoftDim acc_softdim_status (void);

/* TRUE when the bundled copy can be found next to the app */
gboolean   acc_softdim_bundled (void);

/* Copy the bundled extension to ~/.local/share/gnome-shell/extensions,
 * compile its schema, point it at the built-in panel and enable it.
 * GNOME Shell loads a newly installed extension at the next login. */
gboolean   acc_softdim_install (gchar **err);

/* Add it to enabled-extensions (and ask the running shell to enable it). */
gboolean   acc_softdim_enable (gchar **err);

G_END_DECLS
#endif
