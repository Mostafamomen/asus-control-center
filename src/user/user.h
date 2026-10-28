/* user/user.h - the local account shown on the login page
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ASUS Control Center keeps a *local* account in
 * $XDG_CONFIG_HOME/asus-control-center/settings.conf:
 *
 *   [user]
 *   name = mostafa
 *   salt = 6f2c...
 *   hash = 9a41...        (SHA-256 of "<salt>:<password>")
 *   remember = true
 *
 * The password itself is never stored. This is a convenience gate for a
 * desktop panel, not a replacement for the operating system login.
 */
#ifndef ACC_USER_H
#define ACC_USER_H
#include <glib.h>

G_BEGIN_DECLS

/* read [user] from settings.conf into memory (call once at startup) */
void         acc_user_load (void);

/* TRUE when a name + hash are stored */
gboolean     acc_user_exists (void);

/* stored username, or "" when no account exists (never NULL) */
const gchar *acc_user_name (void);

/* remember the username on the sign-in form */
gboolean     acc_user_remember (void);
void         acc_user_set_remember (gboolean remember);

/* create/replace the local account; FALSE on empty input or I/O error */
gboolean     acc_user_create (const gchar *name, const gchar *password,
                              gboolean remember);

/* verify a sign-in attempt */
gboolean     acc_user_check (const gchar *name, const gchar *password);

/* drop the stored account (used by "reset local account") */
void         acc_user_forget (void);

/* "stay signed in": TRUE once a sign-in succeeded and no Sign out happened.
 * Lets the app go straight to the dashboard on later launches. */
gboolean     acc_user_signed_in (void);
void         acc_user_sign_out (void);

G_END_DECLS
#endif
