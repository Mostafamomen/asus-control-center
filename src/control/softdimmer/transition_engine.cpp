/* control/softdimmer/transition_engine.cpp - smooth brightness transitions.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "transition_engine.h"

#include <glib.h>

struct AccSdTransitionEngine {
    gint         duration_ms;
    gdouble      start_value;
    gdouble      target_value;
    gdouble      current_value;
    guint64      start_ms;
    guint        timer_id; /* 0 = idle */

    AccSdTransitionTickCb tick_cb;
    AccSdTransitionDoneCb done_cb;
    gpointer   user_data;
};

static gdouble
ease_out_quad (gdouble t)
{
    /* t in [0,1]. Returns ease-out-quad: fast start, decelerating end. */
    return 1.0 - (1.0 - t) * (1.0 - t);
}

static gboolean
transition_tick (gpointer data)
{
    AccSdTransitionEngine *t = (AccSdTransitionEngine *) data;
    guint64 now = g_get_monotonic_time () / 1000; /* ms */
    gint elapsed = (gint) (now - t->start_ms);
    gint dur = t->duration_ms > 0 ? t->duration_ms : 250;
    gdouble ft = (gdouble) elapsed / (gdouble) dur;
    if (ft > 1.0)
        ft = 1.0;

    gdouble eased = ease_out_quad (ft);
    t->current_value = t->start_value
        + (t->target_value - t->start_value) * eased;

    if (t->tick_cb)
        t->tick_cb (t->current_value, t->user_data);

    if (ft >= 1.0)
    {
        t->timer_id = 0;
        if (t->done_cb)
            t->done_cb (t->user_data);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

AccSdTransitionEngine *
acc_sd_transition_engine_new (gint duration_ms)
{
    AccSdTransitionEngine *t = g_new0 (AccSdTransitionEngine, 1);
    t->duration_ms = duration_ms > 0 ? duration_ms : 250;
    t->start_value = 1.0;
    t->target_value = 1.0;
    t->current_value = 1.0;
    t->timer_id = 0;
    return t;
}

void
acc_sd_transition_engine_free (AccSdTransitionEngine *t)
{
    if (!t)
        return;
    if (t->timer_id)
        g_source_remove (t->timer_id);
    g_free (t);
}

gboolean
acc_sd_transition_engine_start (AccSdTransitionEngine   *t,
                                gdouble                  start_value,
                                gdouble                  target_value,
                                AccSdTransitionTickCb    tick,
                                AccSdTransitionDoneCb    done,
                                gpointer                 user_data)
{
    if (t->timer_id)
        return FALSE; /* already in progress */

    t->start_value = start_value;
    t->target_value = target_value;
    t->current_value = start_value;
    t->start_ms = g_get_monotonic_time () / 1000;
    t->tick_cb = tick;
    t->done_cb = done;
    t->user_data = user_data;

    /* ~60 fps. */
    t->timer_id = g_timeout_add (16, transition_tick, t);
    return TRUE;
}

void
acc_sd_transition_engine_stop (AccSdTransitionEngine *t)
{
    if (t->timer_id)
    {
        g_source_remove (t->timer_id);
        t->timer_id = 0;
    }
    if (t->done_cb)
        t->done_cb (t->user_data);
}

gboolean
acc_sd_transition_engine_is_active (AccSdTransitionEngine *t)
{
    return t->timer_id != 0;
}

void
acc_sd_transition_engine_set_duration (AccSdTransitionEngine *t,
                                       gint                  duration_ms)
{
    t->duration_ms = duration_ms > 0 ? duration_ms : 250;
}

gint
acc_sd_transition_engine_get_duration (AccSdTransitionEngine *t)
{
    return t->duration_ms;
}
