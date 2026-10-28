/* ui/login.h - first-run / sign-in gate shown before the dashboard
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_LOGIN_H
#define ACC_LOGIN_H
#include <gtk/gtk.h>

G_BEGIN_DECLS

/* Show the login window. On success it calls acc_ui_boot() itself, so the
 * caller only needs to start the refresh timer. */
void acc_login_show (GtkApplication *app);

G_END_DECLS
#endif
