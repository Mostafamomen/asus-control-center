/* control/commands.h - the fixed command table shared by the app and the agent
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * One table, two users:
 *   - the app runs a command locally when no host agent is installed;
 *   - the host agent runs the same command on the host.
 *
 * Nothing here ever builds a shell string: the program comes from a fixed
 * candidate list, the arguments are fixed, and the one free argument is
 * validated against a pattern or a fixed list before it reaches argv.
 *
 * This file is compiled into BOTH binaries (the app and the agent), so the
 * two can never drift apart on what a command means.
 */
#ifndef ACC_COMMANDS_H
#define ACC_COMMANDS_H
#include <glib.h>

G_BEGIN_DECLS

typedef enum {
  ACC_CMD_WIFI_SSID = 0,      /* iw dev <iface> link            */
  ACC_CMD_SERVICE_ACTIVE,      /* systemctl is-active <service>  */
  ACC_CMD_SERVICE_ENABLED,     /* systemctl is-enabled <service> */
  ACC_CMD_PACKAGE_UPDATES,     /* dnf check-update              */
  ACC_CMD_APT_UPDATES,         /* apt-get -s upgrade            */
  ACC_CMD_RPM_LIST,            /* rpm -qa                       */
  ACC_CMD_LSPCI,               /* lspci                         */
  ACC_CMD_FIRMWARE_UPDATES,    /* fwupdmgr get-updates          */
  ACC_CMD_FLATPAK_UPDATES,     /* flatpak remote-ls --updates   */
  ACC_CMD_FLATPAK_LIST,        /* flatpak list --app            */
  ACC_CMD_FLATPAK_UNINSTALL,   /* flatpak uninstall --user <id> */
  ACC_CMD_LOADED_MODULES,      /* builtin: /proc/modules        */
  ACC_CMD_BLUETOOTH_ADAPTERS,  /* bluetoothctl list             */
  ACC_CMD_SMART_HEALTH,        /* smartctl -H <device>          */
  ACC_CMD_NVIDIA_SMI,          /* nvidia-smi --query-gpu=...    */
  ACC_CMD_JOURNAL_MUX,         /* journalctl -b <bootid> -g ... */
  ACC_CMD_GNOME_EXT_ENABLED,   /* gnome-extensions list --enabled  */
  ACC_CMD_GNOME_EXT_DISABLED,  /* gnome-extensions list --disabled */
  ACC_CMD_GNOME_EXT_ENABLE,    /* gnome-extensions enable <uuid>*/
  ACC_CMD_GNOME_EXT_DISABLE,   /* gnome-extensions disable <id> */
  ACC_CMD_NL_KDE_GET,          /* kreadconfig --file kwinrc ... */
  ACC_CMD_NL_KDE_SET,          /* kwriteconfig --file kwinrc... */
  ACC_CMD_NL_KDE_RELOAD,       /* qdbus org.kde.KWin ...        */
  ACC_CMD_COUNT
} AccCommand;

/* ---- the wire ids (stable strings, used by the D-Bus API) --------------- */
const gchar *acc_command_id       (AccCommand id);
gint         acc_command_from_id  (const gchar *id);

/* ---- the privileged helper's verbs -------------------------------------
 * The exact list data/acc-helper accepts. The app and the agent both
 * check against it before anything is executed; the helper checks it a
 * third time. The helper itself is NOT changed by this work. */
const gchar *const *acc_command_helper_actions (void);
gboolean acc_command_helper_action_ok  (const gchar *action);
gboolean acc_command_helper_value_ok   (const gchar *action, const gchar *value);

/* ---- arguments --------------------------------------------------------- */
gboolean acc_command_arg_ok (AccCommand id, const gchar *arg);

/* Build the fixed argv for a command. Returns NULL and sets *err when the
 * id is unknown, the program is missing or the argument is invalid. */
gchar **acc_command_argv (AccCommand id, const gchar *arg, gchar **err);

/* Run the command on this machine. Fixed argv, no shell, bounded by the
 * command's own timeout. */
gboolean acc_command_run_local (AccCommand id, const gchar *arg,
                                gchar **out, gint *exit_code, gchar **err);

G_END_DECLS
#endif
