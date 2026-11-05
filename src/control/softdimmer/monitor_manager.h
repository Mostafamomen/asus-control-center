/* control/softdimmer/monitor_manager.h - dynamic monitor discovery.
 *
 * Discovers connected monitors from the Linux DRM/KMS subsystem
 * (/sys/class/drm) so the engine works both standalone (ASUSCraft without
 * the GNOME Shell extension) and under GNOME Shell.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_SOFTDIMMER_MONITOR_MANAGER_H
#define ACC_SOFTDIMMER_MONITOR_MANAGER_H

#include "softdimmer_types.h"

G_BEGIN_DECLS

typedef struct AccSdMonitorManager AccSdMonitorManager;

/* Create a manager that polls /sys/class/drm for connected displays.
 * Callbacks (if non-NULL) are invoked from a GLib idle source when the
 * monitor set changes, so callers never touch GTK from a signal handler. */
AccSdMonitorManager *
acc_sd_monitor_manager_new (void);

/* Destroy the manager and stop polling. */
void
acc_sd_monitor_manager_free (AccSdMonitorManager *mgr);

/* Refresh the monitor list now (blocking, cheap). Returns the number of
 * connected monitors. Output array is owned by the manager; do not free. */
const AccSdMonitorState *
acc_sd_monitor_manager_refresh (AccSdMonitorManager *mgr, gint *out_count);

/* Return the current list (no sysfs read). May be NULL if never refreshed. */
const AccSdMonitorState *
acc_sd_monitor_manager_list (AccSdMonitorManager *mgr, gint *out_count);

/* Look up a monitor by index. Returns NULL when out of range. */
const AccSdMonitorState *
acc_sd_monitor_manager_get (AccSdMonitorManager *mgr, gint index);

/* Register / unregister a change callback. The callback receives the
 * manager and user data; it is invoked from a GLib main-context idle. */
typedef void (*AccSdMonitorChangedCb) (AccSdMonitorManager *mgr,
                                       gpointer             user_data);

guint
acc_sd_monitor_manager_on_changed (AccSdMonitorManager    *mgr,
                                   AccSdMonitorChangedCb   cb,
                                   gpointer                user_data);

void
acc_sd_monitor_manager_disconnect (AccSdMonitorManager *mgr,
                                   guint                handler_id);

G_END_DECLS

#endif /* ACC_SOFTDIMMER_MONITOR_MANAGER_H */
