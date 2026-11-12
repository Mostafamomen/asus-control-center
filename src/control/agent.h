/* control/agent.h - the client for the ASUSCraft host agent
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The host agent (agent/, a separate package) runs on the machine that owns
 * the desktop session. A Flatpak cannot reach it directly: the app talks
 * to it over the session bus instead.
 *
 * The agent is OPTIONAL. Every call here has a local fallback, so the app
 * works exactly as before when no agent is installed.
 */
#ifndef ACC_AGENT_H
#define ACC_AGENT_H
#include <glib.h>
#include "commands.h"

G_BEGIN_DECLS

typedef enum {
  ACC_AGENT_ABSENT = 0,   /* no agent on the bus (not installed)        */
  ACC_AGENT_READY,        /* the name is owned, but it did not answer   */
  ACC_AGENT_CONNECTED     /* the agent answered                          */
} AccAgentState;

/* Probe the session bus once and remember the answer. */
AccAgentState acc_agent_state (void);

/* Forget the cached state so the next call probes again. */
void          acc_agent_reset (void);

const gchar  *acc_agent_version (void);
const gchar  *acc_agent_capabilities (void);
const gchar  *acc_agent_desktop (void);
const gchar  *acc_agent_last_error (void);

/* TRUE when the app runs inside a Flatpak sandbox. */
gboolean      acc_agent_sandboxed (void);

/* The calls. They all return FALSE when there is no agent; the caller
 * then uses the local path. */
gboolean acc_agent_run (AccCommand id, const gchar *arg,
                        gchar **out, gint *exit_code, gchar **err);
gboolean acc_agent_set_hardware (const gchar *action, const gchar *value,
                                 gint *exit_code, gchar **err);
gboolean acc_agent_check_admin (gint *exit_code);
gboolean acc_agent_softbrightness (const gchar *op, gchar **result, gchar **err);
gboolean acc_agent_nightlight_get (gchar **text, gchar **err);
gboolean acc_agent_nightlight_set (const gchar *key, const gchar *value,
                                   gchar **err);

/* The settings that belong to the desktop session: the OLED dimming
 * value and the touchpad switch. A sandbox cannot be trusted to reach
 * the host's dconf, so the agent does them. */
gboolean acc_agent_brightness_get (gchar **value, gchar **err);
gboolean acc_agent_brightness_set (const gchar *value, gchar **err);
gboolean acc_agent_touchpad_get (gchar **state, gchar **err);
gboolean acc_agent_touchpad_set (const gchar *state, gchar **err);

G_END_DECLS
#endif
