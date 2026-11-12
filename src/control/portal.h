/* control/portal.h - the XDG desktop portals ASUSCraft needs
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A Flatpak has no gnome-software and may not start one. The Software
 * portal is the supported way to ask the desktop to open its software
 * centre, and it needs exactly one narrowly scoped D-Bus permission:
 * --talk-name=org.freedesktop.portal.Desktop
 */
#ifndef ACC_PORTAL_H
#define ACC_PORTAL_H
#include <glib.h>

G_BEGIN_DECLS

/* Ask the desktop to open its software centre. Returns FALSE and sets *err
 * when there is no portal (for example in a native install, where the
 * software tool is started directly instead). */
gboolean acc_portal_open_software (gchar **err);

G_END_DECLS
#endif
