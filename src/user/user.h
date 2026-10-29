/* user/user.h - the local account shown on the login page
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ASUS Control Center keeps a *local* account in
 * $XDG_CONFIG_HOME/asus-control-center/settings.conf:
 *
 *   [user]
 *   name = mostafa
 *   hash = pbkdf2-sha256$600000$<salt>$<key>   (PBKDF2-HMAC-SHA256)
 *   remember = true
 *   signed_in = true
 *   signed_in_boot = <kernel boot id>
 *   failures = 0
 *   locked_until = 0
 *
 * The password itself is never stored. Old SHA-256 accounts are upgraded
 * on their next successful sign-in. A sign-in lasts until Sign out or the
 * next reboot. This is a convenience gate for a desktop panel, not a
 * replacement for the operating system login: hardware writes are guarded
 * separately by Polkit.
 */
#ifndef ACC_USER_H
#define ACC_USER_H
#include <glib.h>

G_BEGIN_DECLS

#define ACC_USER_MIN_PASS 8   /* shortest accepted password */

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

/* verify a sign-in attempt; always FALSE while locked out */
gboolean     acc_user_check (const gchar *name, const gchar *password);

/* seconds left in a wrong-password lockout, 0 when sign-in is allowed */
gint         acc_user_lock_remaining (void);

/* TRUE when the PBKDF2 code matches a published test vector */
gboolean     acc_user_selftest (void);

/* drop the stored account (used by "reset local account") */
void         acc_user_forget (void);

/* "stay signed in": TRUE once a sign-in succeeded in this boot and no Sign
 * out happened. Lets the app go straight to the dashboard on later launches. */
gboolean     acc_user_signed_in (void);
void         acc_user_sign_out (void);

G_END_DECLS
#endif
