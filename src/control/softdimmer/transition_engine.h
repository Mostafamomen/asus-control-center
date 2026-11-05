/* control/softdimmer/transition_engine.h - smooth brightness transitions.
 *
 * Animates a brightness value from a start to a target over a configurable
 * duration using an ease-out-quad curve. Driven by GLib timeouts so GTK and
 * D-Bus remain responsive.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_SOFTDIMMER_TRANSITION_ENGINE_H
#define ACC_SOFTDIMMER_TRANSITION_ENGINE_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct AccSdTransitionEngine AccSdTransitionEngine;

/* Callback fired on every frame with the current interpolated value. */
typedef void (*AccSdTransitionTickCb) (gdouble value, gpointer user_data);
/* Fired when the transition completes. */
typedef void (*AccSdTransitionDoneCb) (gpointer user_data);

/* Create an animator. duration_ms is the total transition length. */
AccSdTransitionEngine *
acc_sd_transition_engine_new (gint duration_ms);

/* Destroy the animator, stopping any in-flight transition. */
void
acc_sd_transition_engine_free (AccSdTransitionEngine *t);

/* Start a transition from start_value to target_value. Returns FALSE if a
 * transition is already in progress (callers should stop it first). */
gboolean
acc_sd_transition_engine_start (AccSdTransitionEngine   *t,
                                gdouble                  start_value,
                                gdouble                  target_value,
                                AccSdTransitionTickCb    tick,
                                AccSdTransitionDoneCb    done,
                                gpointer                 user_data);

/* Stop the current transition and hold at the current value. */
void
acc_sd_transition_engine_stop (AccSdTransitionEngine *t);

/* Query state. */
gboolean acc_sd_transition_engine_is_active (AccSdTransitionEngine *t);
void     acc_sd_transition_engine_set_duration (AccSdTransitionEngine *t,
                                                gint                  duration_ms);
gint     acc_sd_transition_engine_get_duration (AccSdTransitionEngine *t);

G_END_DECLS

#endif /* ACC_SOFTDIMMER_TRANSITION_ENGINE_H */
