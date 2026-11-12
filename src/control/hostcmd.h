/* control/hostcmd.h - one way to reach the host, with or without an agent
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Every command the app runs goes through acc_host_run(). When the host
 * agent is connected the command runs there (which is what a Flatpak
 * needs); otherwise it runs locally, exactly as before. A native install
 * without the agent therefore behaves precisely as it always did.
 */
#ifndef ACC_HOSTCMD_H
#define ACC_HOSTCMD_H
#include <glib.h>
#include "commands.h"

G_BEGIN_DECLS

typedef enum {
  ACC_HOST_LOCAL = 0,       /* ran on this machine                    */
  ACC_HOST_AGENT,           /* ran through the host agent             */
  ACC_HOST_UNAVAILABLE      /* neither could run it                   */
} AccHostHow;

/* TRUE when the host agent is connected and answering. */
gboolean acc_host_agent_ready (void);

/* Run a host command. Fixed argv, no shell, ever. */
gboolean acc_host_run (AccCommand id, const gchar *arg,
                       gchar **out, gint *exit_code,
                       AccHostHow *how, gchar **err);

/* The privileged path. It reaches the EXISTING whitelist-only helper
 * through pkexec, so the polkit action id and the whitelist are the same
 * whether the agent is there or not. */
gboolean acc_host_set_hardware (const gchar *action, const gchar *value,
                                gint *exit_code, AccHostHow *how,
                                gchar **err);

/* The admin probe the sign-in page uses (pkexec /usr/bin/true). */
gboolean acc_host_check_admin (gint *exit_code);

G_END_DECLS
#endif
