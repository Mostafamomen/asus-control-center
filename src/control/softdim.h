/* control/softdim.h - install/enable the bundled Soft Brightness Plus
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * OLED dimming below the panel minimum is done by the GNOME Shell extension
 * Soft Brightness Plus (GPL-3.0-or-later), shipped unmodified in
 * data/soft-brightness-plus/. This module puts it in the user's own
 * extension folder and turns it on. User level only: no root, no shell;
 * the only programs run are glib-compile-schemas and gnome-extensions with
 * fixed arguments. brightness.h keeps driving the slider afterwards.
 */
#ifndef ACC_SOFTDIM_H
#define ACC_SOFTDIM_H
#include <glib.h>

G_BEGIN_DECLS

typedef enum
{
  ACC_SD_MISSING = 0,      /* not installed for this user or system-wide */
  ACC_SD_DISABLED,         /* installed but not in enabled-extensions    */
  ACC_SD_ON                /* installed and enabled                      */
} AccSoftDim;

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
