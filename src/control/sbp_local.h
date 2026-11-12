/* control/sbp_local.h - the local Soft Brightness Plus management
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The bundled GNOME Shell extension (data/soft-brightness-plus/) has to be
 * copied into the USER's own extension folder and enabled there. That
 * folder belongs to the desktop session, so this code only works on the
 * machine that owns the session:
 *
 *   - in a native install it runs inside the app;
 *   - in a Flatpak it runs inside the host agent.
 *
 * The split keeps one implementation of the file list, the schema compile
 * and the enabled-extensions bookkeeping in a single place.
 */
#ifndef ACC_SBP_LOCAL_H
#define ACC_SBP_LOCAL_H
#include <glib.h>

G_BEGIN_DECLS

typedef enum
{
  ACC_SD_MISSING = 0,      /* not installed for this user or system-wide */
  ACC_SD_DISABLED,         /* installed but not in enabled-extensions    */
  ACC_SD_ON                /* installed and enabled                      */
} AccSoftDim;

/* Where the bundled copy lives on this machine. The app uses the package
 * path; the host agent uses its own package path (the two packages do not
 * share files). */
#ifndef ACC_SBP_SYSTEM_DIR
#define ACC_SBP_SYSTEM_DIR "/usr/share/asus-control-center/soft-brightness-plus"
#endif

AccSoftDim acc_sbp_local_status (void);

/* TRUE when the bundled copy can be found on this machine */
gboolean   acc_sbp_local_bundled (void);

/* Copy the bundled extension into ~/.local/share/gnome-shell/extensions,
 * compile its schema, point it at the built-in panel and enable it. */
gboolean   acc_sbp_local_install (gchar **err);

/* Add it to enabled-extensions (and ask the running shell to enable it). */
gboolean   acc_sbp_local_enable (gchar **err);

G_END_DECLS
#endif
