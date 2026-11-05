/* control/softdimmer/brightness_engine.h - core Soft Dimmer engine.
 *
 * Manages normalized software dimming values (0.05..1.0), per-monitor
 * state, presets, and transitions. Thread-safe: all public methods grab
 * the internal mutex. GLib main-loop callbacks are used for transitions so
 * GTK and D-Bus stay responsive.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_SOFTDIMMER_BRIGHTNESS_ENGINE_H
#define ACC_SOFTDIMMER_BRIGHTNESS_ENGINE_H

#include "softdimmer_types.h"
#include "monitor_manager.h"

G_BEGIN_DECLS

typedef struct AccSdBrightnessEngine AccSdBrightnessEngine;

/* Create the engine with a monitor manager (may be NULL). */
AccSdBrightnessEngine *
acc_sd_brightness_engine_new (AccSdMonitorManager *monitors);

/* Destroy the engine. Stops any in-flight transition. */
void
acc_sd_brightness_engine_free (AccSdBrightnessEngine *engine);

/* ── Global state ──────────────────────────────────────────────── */

gboolean acc_sd_engine_get_enabled   (AccSdBrightnessEngine *engine);
void     acc_sd_engine_set_enabled   (AccSdBrightnessEngine *engine,
                                      gboolean              enabled);

AccSdBrightness acc_sd_engine_get_brightness (AccSdBrightnessEngine *engine,
                                              gint                   monitor_index);
/* monitor_index == -1 returns the global value. */

void     acc_sd_engine_set_brightness (AccSdBrightnessEngine *engine,
                                       gint                   monitor_index,
                                       AccSdBrightness        brightness);
/* monitor_index == -1 applies to all connected monitors. Clamped to
 * [minimum_brightness, 1.0]. Emits brightness-changed via the signal
 * callback if set. */

/* ── Limits ────────────────────────────────────────────────────── */
gdouble acc_sd_engine_get_minimum_brightness (AccSdBrightnessEngine *engine);
void    acc_sd_engine_set_minimum_brightness (AccSdBrightnessEngine *engine,
                                              gdouble                min);

/* ── Presets ───────────────────────────────────────────────────── */
typedef void (*AccSdPresetsChangedCb) (AccSdBrightnessEngine *engine,
                                       gpointer               user_data);

const AccSdPreset *
acc_sd_engine_get_presets (AccSdBrightnessEngine *engine, gint *out_count);

gboolean acc_sd_engine_apply_preset (AccSdBrightnessEngine *engine,
                                     const gchar           *preset_id);

/* Register a user preset (overwrites by id). Returns FALSE on invalid id. */
gboolean acc_sd_engine_add_preset (AccSdBrightnessEngine *engine,
                                   const AccSdPreset     *preset);

/* ── Transitions ───────────────────────────────────────────────── */
gint     acc_sd_engine_get_transition_ms (AccSdBrightnessEngine *engine);
void     acc_sd_engine_set_transition_ms (AccSdBrightnessEngine *engine,
                                          gint                   ms);

/* ── Status ────────────────────────────────────────────────────── */
AccSdEngineStatus
acc_sd_engine_get_status (AccSdBrightnessEngine *engine);

/* ── Callbacks ─────────────────────────────────────────────────── */
/* brightness_changed(monitor_index, brightness) — invoked from the GLib
 * main loop whenever a value changes. user_data is opaque. */
guint
acc_sd_engine_on_brightness_changed (AccSdBrightnessEngine *engine,
                                     void                  (*cb)(gint index,
                                                                   gdouble brightness,
                                                                   gpointer user_data),
                                     gpointer              user_data);

typedef void (*AccSdEnabledChangedCb) (AccSdBrightnessEngine *engine,
                                       gboolean              enabled,
                                       gpointer              user_data);

guint
acc_sd_engine_on_enabled_changed (AccSdBrightnessEngine *engine,
                                  AccSdEnabledChangedCb  cb,
                                  gpointer               user_data);

typedef void (*AccSdStatusChangedCb) (AccSdBrightnessEngine *engine,
                                      AccSdStatus            status,
                                      gpointer               user_data);

guint
acc_sd_engine_on_status_changed (AccSdBrightnessEngine *engine,
                                 AccSdStatusChangedCb   cb,
                                 gpointer               user_data);

void
acc_sd_engine_disconnect (AccSdBrightnessEngine *engine,
                          guint                  handler_id);

G_END_DECLS

#endif /* ACC_SOFTDIMMER_BRIGHTNESS_ENGINE_H */
