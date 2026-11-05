/* control/softdimmer/softdimmer_service.h - D-Bus service for Soft Dimmer.
 *
 * Exports io.github.asuscraft.SoftDimmer on the session bus so the minimal
 * GNOME Shell adapter (and any other client) can read/write dimming state
 * without depending on ASUSCraft's internal C++ API.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_SOFTDIMMER_SERVICE_H
#define ACC_SOFTDIMMER_SERVICE_H

#include "brightness_engine.h"

G_BEGIN_DECLS

/* D-Bus well-known name, object path, and interface. */
#define ACC_SD_DBUS_SERVICE    "io.github.asuscraft.SoftDimmer"
#define ACC_SD_DBUS_OBJECT_PATH "/io/github/asuscraft/SoftDimmer"
#define ACC_SD_DBUS_INTERFACE  "io.github.asuscraft.SoftDimmer"

typedef struct AccSdSoftDimmerService AccSdSoftDimmerService;

/* Create and export the D-Bus service. Returns NULL if the bus name cannot
 * be acquired (e.g. another instance already owns it). */
AccSdSoftDimmerService *
acc_sd_service_new (AccSdBrightnessEngine *engine);

/* Unexport and release the bus name. */
void
acc_sd_service_free (AccSdSoftDimmerService *service);

/* Returns TRUE if the service is currently exported on the bus. */
gboolean
acc_sd_service_is_exported (AccSdSoftDimmerService *service);

/* Notify connected D-Bus clients that brightness changed. Call this after
 * any engine mutation that originated outside D-Bus (e.g. from the UI). */
void
acc_sd_service_notify_brightness (AccSdSoftDimmerService *svc,
                                  gint                    monitor_index,
                                  gdouble                 brightness);

void
acc_sd_service_notify_enabled (AccSdSoftDimmerService *svc,
                               gboolean                enabled);

void
acc_sd_service_notify_status (AccSdSoftDimmerService *svc);

G_END_DECLS

#endif /* ACC_SOFTDIMMER_SERVICE_H */
