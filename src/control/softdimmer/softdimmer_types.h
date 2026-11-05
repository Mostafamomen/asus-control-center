/* control/softdimmer/softdimmer_types.h - shared types for the Soft Dimmer
 * engine integrated into ASUSCraft.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_SOFTDIMMER_TYPES_H
#define ACC_SOFTDIMMER_TYPES_H

#include <glib.h>
#include <stdint.h>

G_BEGIN_DECLS

/* Normalized software dimming value: 0.05 (very dim) to 1.0 (no dimming).
 * This is distinct from physical hardware backlight brightness. */
typedef gdouble AccSdBrightness; /* [0.05, 1.0] */

/* Per-monitor runtime state. */
typedef struct {
    gint    index;          /* 0-based monitor index */
    gchar  *name;           /* human-readable monitor name (EDID or DRM) */
    gint    width;          /* px, -1 unknown */
    gint    height;         /* px, -1 unknown */
    gdouble refresh_hz;     /* 0 unknown */
    gboolean connected;     /* currently connected */
    AccSdBrightness brightness; /* current software dimming value */
    gboolean  enabled;      /* dimming active on this monitor */
} AccSdMonitorState;

/* A named brightness preset. brightness applies to all monitors;
 * per_monitor overrides specific monitors (index -> value). */
typedef struct {
    gchar  *id;             /* stable preset id (e.g. "normal", "night") */
    gchar  *name;           /* user-facing name */
    AccSdBrightness brightness; /* global brightness */
    GHashTable *per_monitor;    /* gint index -> gdouble brightness, or NULL */
    gboolean builtin;       /* TRUE for factory presets, FALSE for user */
} AccSdPreset;

/* Engine status reported to the UI and over D-Bus. */
typedef enum {
    ACC_SD_STATUS_STOPPED = 0,   /* engine not running */
    ACC_SD_STATUS_RUNNING,       /* engine running, no overlays yet */
    ACC_SD_STATUS_ACTIVE,        /* dimming active on >=1 monitor */
    ACC_SD_STATUS_ERROR,         /* engine error, see last_error */
} AccSdStatus;

typedef struct {
    AccSdStatus status;
    gchar      *last_error;
    gboolean    extension_available; /* GNOME Shell extension reachable */
    gboolean    dbus_available;      /* D-Bus peer reachable */
    gint        monitor_count;
    gdouble     global_brightness;
    gboolean    enabled;
} AccSdEngineStatus;

G_END_DECLS

#endif /* ACC_SOFTDIMMER_TYPES_H */
