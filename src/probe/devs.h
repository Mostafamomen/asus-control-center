/* probe/devs.h - portable device discovery (read-only)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Nothing here assumes a specific SKU. Like MyASUS (one app in front of the
 * ASUS System Control Interface driver, feature set depending on the model),
 * this app talks to whatever the kernel exposes on *this* machine:
 *
 *   battery    → /sys/class/power_supply/<name> with type=Battery (BAT0, BAT1…)
 *   charge cap → charge_control_end_threshold OR charge_stop_threshold
 *   ASUS WMI   → asus-nb-wmi / asus-wmi / any asus* platform dir
 *   kbd LED    → any LED named *kbd_backlight*
 *
 * Every function returns NULL / "" when the kernel does not expose the
 * device; callers must treat that as "feature unavailable", never guess.
 */
#ifndef ACC_DEVS_H
#define ACC_DEVS_H
#include <glib.h>

G_BEGIN_DECLS

/* battery device directory, e.g. "/sys/class/power_supply/BAT0" */
const gchar *acc_dev_battery_dir (void);

/* battery device name, e.g. "BAT0"; "" when absent */
const gchar *acc_dev_battery_name (void);

/* "<battery>/<leaf>" or NULL when no battery exists (caller frees) */
gchar       *acc_dev_battery_file (const gchar *leaf);

/* charge-limit file (end_threshold or stop_threshold) or NULL (free it) */
gchar       *acc_dev_charge_limit_file (void);

/* asus-armoury firmware attribute (kernel >= 6.17). Newer kernels moved the
 * writable ASUS settings here and left the old platform files read-only or
 * deprecated, which is why writes must prefer this path. NULL when absent. */
gchar       *acc_dev_armoury_file (const gchar *attr);

/* TRUE when asus-armoury says the last firmware change still needs a reboot */
gboolean     acc_dev_pending_reboot (void);

/* TRUE when a Mains adapter is online. ASUS firmware commonly refuses the
 * GPU MUX switch on battery, so failures can tell the user to plug in. */
gboolean     acc_dev_on_ac (void);

/* ASUS WMI platform directory, e.g. "/sys/devices/platform/asus-nb-wmi" */
const gchar *acc_dev_wmi_dir (void);

/* "<wmi>/<leaf>" or NULL when no ASUS WMI interface exists (free it) */
gchar       *acc_dev_wmi_file (const gchar *leaf);

/* keyboard backlight brightness file or NULL (free it) */
gchar       *acc_dev_kbd_led_file (void);

G_END_DECLS
#endif
