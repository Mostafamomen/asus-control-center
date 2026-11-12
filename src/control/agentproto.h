/* control/agentproto.h - the host agent's D-Bus contract
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Shared by the app (the client) and the agent (the service) so the two
 * can never drift apart. The interface itself is documented in
 * docs/host-agent.md.
 *
 * The agent runs on the HOST, on the SESSION bus, as the ordinary user.
 * It owns no privilege of its own: every privileged operation is the
 * existing whitelist-only helper run through pkexec, so the polkit action
 * id and the helper's whitelist are unchanged.
 */
#ifndef ACC_AGENTPROTO_H
#define ACC_AGENTPROTO_H

#define ACC_AGENT_BUS_NAME "io.github.asuscontrolcenter.Agent"
#define ACC_AGENT_PATH     "/io/github/asuscontrolcenter/Agent"
#define ACC_AGENT_IFACE    "io.github.asuscontrolcenter.Agent"

/* the two settings that belong to the desktop session: the OLED dimming
 * value of the Soft Brightness Plus extension and the touchpad switch */
#define SBP_SCHEMA  "org.gnome.shell.extensions.soft-brightness-plus"
#define TP_SCHEMA   "org.gnome.desktop.peripherals.touchpad"
#define SBP_UUID    "soft-brightness-plus@joelkitching.com"
#define SBP_KEY     "current-brightness"
#define TP_KEY      "send-events"

/* the agent's own version, reported by GetInfo */
#define ACC_AGENT_VERSION  "1.0"

#endif
