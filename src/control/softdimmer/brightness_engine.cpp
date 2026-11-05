/* control/softdimmer/brightness_engine.cpp - core Soft Dimmer engine.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "brightness_engine.h"
#include "monitor_manager.h"
#include "../../probe/probe.h"

#include <gio/gio.h>
#include <glib.h>
#include <string.h>
#include <math.h>

#define DEFAULT_MIN_BRIGHTNESS 0.10
#define DEFAULT_TRANSITION_MS  250
#define DEFAULT_BRIGHTNESS     1.0

/* Clamp a brightness value into [min, 1.0]. */
static inline gdouble
clamp_brightness (gdouble value, gdouble min_brightness)
{
    if (value < min_brightness)
        value = min_brightness;
    if (value > 1.0)
        value = 1.0;
    return value;
}

struct AccSdBrightnessEngine {
    /* Configuration (protected by lock). */
    GMutex        lock;
    gboolean      enabled;
    AccSdBrightness global_brightness;
    gdouble       minimum_brightness;
    gint          transition_ms;

    /* Per-monitor state (index -> brightness). */
    GHashTable   *per_monitor;   /* gint pointer -> gdouble */

    /* Presets. */
    AccSdPreset  *presets;
    gint          n_presets;

    /* Monitor manager (borrowed, not owned). */
    AccSdMonitorManager *monitors;

    /* Transition state. */
    guint         transition_id;       /* 0 = none in flight */
    gint          transition_target_idx; /* -1 = all */
    gdouble       transition_start_val;
    gdouble       transition_target_val;
    guint64       transition_start_ms;
    gint          transition_duration_ms;

    /* Callbacks (GLib signal-slot style). */
    GPtrArray    *brightness_handlers; /* (cb, user_data) */
    GPtrArray    *enabled_handlers;
    GPtrArray    *status_handlers;

    AccSdStatus   last_status;
    gchar        *last_error;
};

/* ------------------------------------------------------------------ presets */
/* Factory presets. Not extreme: the dimmest is 0.30 (comfortable), not
 * near-black, to avoid OLED stress claims and unusable screens. */
static AccSdPreset factory_presets[] = {
    { (gchar *) "normal",  (gchar *) "Normal",   1.0,   NULL, TRUE },
    { (gchar *) "comfort", (gchar *) "Comfortable", 0.80, NULL, TRUE },
    { (gchar *) "night",   (gchar *) "Night",    0.50,   NULL, TRUE },
    { (gchar *) "reading", (gchar *) "Reading",  0.30,   NULL, TRUE },
};

static void
preset_free (AccSdPreset *p)
{
    if (!p)
        return;
    g_free (p->id);
    g_free (p->name);
    if (p->per_monitor)
    {
        g_hash_table_unref (p->per_monitor);
        p->per_monitor = NULL;
    }
}

static gboolean
preset_id_valid (const gchar *id)
{
    return id && *id && g_strcmp0 (id, "normal") != 0
        && g_strcmp0 (id, "comfort") != 0
        && g_strcmp0 (id, "night") != 0
        && g_strcmp0 (id, "reading") != 0;
}

/* ------------------------------------------------------------------ status */
static AccSdStatus
compute_status (AccSdBrightnessEngine *e)
{
    if (!e->enabled)
        return ACC_SD_STATUS_STOPPED;
    /* Check if any monitor has dimming below 1.0. */
    gboolean any_dim = FALSE;
    GHashTableIter iter;
    gpointer key, val;
    g_hash_table_iter_init (&iter, e->per_monitor);
    while (g_hash_table_iter_next (&iter, &key, &val))
    {
        if (*(gdouble *) val < 0.99)
        {
            any_dim = TRUE;
            break;
        }
    }
    if (!any_dim && e->global_brightness < 0.99)
        any_dim = TRUE;
    return any_dim ? ACC_SD_STATUS_ACTIVE : ACC_SD_STATUS_RUNNING;
}

static void
emit_status (AccSdBrightnessEngine *e)
{
    AccSdStatus st = compute_status (e);
    if (st == e->last_status)
        return;
    e->last_status = st;
    for (guint i = 0; i < e->status_handlers->len; i++)
    {
        GPtrArray *pair = (GPtrArray *) g_ptr_array_index
            (e->status_handlers, i);
        AccSdStatusChangedCb cb = (AccSdStatusChangedCb) g_ptr_array_index (pair, 0);
        gpointer ud = g_ptr_array_index (pair, 1);
        cb (e, st, ud);
    }
}

/* ------------------------------------------------------------------ transitions */
static gboolean
transition_tick (gpointer data)
{
    AccSdBrightnessEngine *e = (AccSdBrightnessEngine *) data;
    guint64 now = g_get_monotonic_time () / 1000; /* ms */
    gint elapsed = (gint) (now - e->transition_start_ms);
    gint dur = e->transition_duration_ms > 0 ? e->transition_duration_ms
                                             : DEFAULT_TRANSITION_MS;
    gdouble t = (gdouble) elapsed / (gdouble) dur;
    if (t > 1.0)
        t = 1.0;

    /* Ease-out-quad. */
    gdouble eased = 1.0 - (1.0 - t) * (1.0 - t);
    gdouble cur = e->transition_start_val
        + (e->transition_target_val - e->transition_start_val) * eased;

    g_mutex_lock (&e->lock);
    if (e->transition_target_idx == -1)
    {
        e->global_brightness = clamp_brightness (cur, e->minimum_brightness);
        GHashTableIter iter;
        gpointer key, val;
        g_hash_table_iter_init (&iter, e->per_monitor);
        while (g_hash_table_iter_next (&iter, &key, &val))
        {
            gdouble *v = (gdouble *) val;
            *v = clamp_brightness (cur, e->minimum_brightness);
        }
    }
    else
    {
        gpointer existing = g_hash_table_lookup (e->per_monitor,
                                                 &e->transition_target_idx);
        if (existing)
        {
            *(gdouble *) existing = clamp_brightness (cur, e->minimum_brightness);
        }
    }
    g_mutex_unlock (&e->lock);

    /* Emit brightness-changed for affected monitors. */
    for (guint i = 0; i < e->brightness_handlers->len; i++)
    {
        GPtrArray *pair = (GPtrArray *) g_ptr_array_index
            (e->brightness_handlers, i);
        void (*cb)(gint, gdouble, gpointer) =
            (void (*)(gint, gdouble, gpointer)) g_ptr_array_index (pair, 0);
        gpointer ud = g_ptr_array_index (pair, 1);
        if (e->transition_target_idx == -1)
        {
            /* Emit for all known monitors. We need the monitor manager. */
            gint n = 0;
            const AccSdMonitorState *list = NULL;
            if (e->monitors)
                list = acc_sd_monitor_manager_list (e->monitors, &n);
            for (gint mi = 0; mi < n; mi++)
                cb (list[mi].index, e->global_brightness, ud);
        }
        else
        {
            cb (e->transition_target_idx, cur, ud);
        }
    }

    if (t >= 1.0)
    {
        e->transition_id = 0;
        emit_status (e);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void
start_transition (AccSdBrightnessEngine *e, gint monitor_idx,
                  gdouble target)
{
    if (e->transition_id)
        g_source_remove (e->transition_id);

    g_mutex_lock (&e->lock);
    gdouble start;
    if (monitor_idx == -1)
        start = e->global_brightness;
    else
    {
        gpointer v = g_hash_table_lookup (e->per_monitor, &monitor_idx);
        start = v ? *(gdouble *) v : 1.0;
    }
    e->transition_target_idx = monitor_idx;
    e->transition_start_val = start;
    e->transition_target_val = clamp_brightness (target, e->minimum_brightness);
    e->transition_start_ms = g_get_monotonic_time () / 1000;
    e->transition_duration_ms = e->transition_ms;
    g_mutex_unlock (&e->lock);

    e->transition_id = g_timeout_add (16, transition_tick, e); /* ~60fps */
}

/* ------------------------------------------------------------------ public API */
AccSdBrightnessEngine *
acc_sd_brightness_engine_new (AccSdMonitorManager *monitors)
{
    AccSdBrightnessEngine *e = g_new0 (AccSdBrightnessEngine, 1);
    g_mutex_init (&e->lock);
    e->enabled = FALSE;
    e->global_brightness = DEFAULT_BRIGHTNESS;
    e->minimum_brightness = DEFAULT_MIN_BRIGHTNESS;
    e->transition_ms = DEFAULT_TRANSITION_MS;
    e->per_monitor = g_hash_table_new (g_int_hash, g_int_equal);
    e->monitors = monitors;
    e->transition_id = 0;
    e->last_status = ACC_SD_STATUS_STOPPED;
    e->last_error = NULL;

    e->brightness_handlers = g_ptr_array_new ();
    e->enabled_handlers = g_ptr_array_new ();
    e->status_handlers = g_ptr_array_new ();

    /* Copy factory presets. */
    e->n_presets = (gint) G_N_ELEMENTS (factory_presets);
    e->presets = g_new0 (AccSdPreset, e->n_presets);
    for (gint i = 0; i < e->n_presets; i++)
    {
        e->presets[i].id = g_strdup (factory_presets[i].id);
        e->presets[i].name = g_strdup (factory_presets[i].name);
        e->presets[i].brightness = factory_presets[i].brightness;
        e->presets[i].per_monitor = NULL;
        e->presets[i].builtin = TRUE;
    }

    /* Initialize per-monitor brightness for connected monitors. */
    if (monitors)
    {
        gint n = 0;
        const AccSdMonitorState *list =
            acc_sd_monitor_manager_list (monitors, &n);
        for (gint i = 0; i < n; i++)
        {
            gint *idx = g_new0 (gint, 1);
            *idx = list[i].index;
            gdouble *val = g_new0 (gdouble, 1);
            *val = 1.0;
            g_hash_table_insert (e->per_monitor, idx, val);
        }
    }

    return e;
}

void
acc_sd_brightness_engine_free (AccSdBrightnessEngine *e)
{
    if (!e)
        return;
    if (e->transition_id)
        g_source_remove (e->transition_id);
    g_mutex_clear (&e->lock);
    g_hash_table_unref (e->per_monitor);
    for (gint i = 0; i < e->n_presets; i++)
        preset_free (&e->presets[i]);
    g_free (e->presets);
    g_free (e->last_error);
    g_ptr_array_unref (e->brightness_handlers);
    g_ptr_array_unref (e->enabled_handlers);
    g_ptr_array_unref (e->status_handlers);
    g_free (e);
}

/* ── Global state ──────────────────────────────────────────────── */
gboolean
acc_sd_engine_get_enabled (AccSdBrightnessEngine *e)
{
    gboolean v;
    g_mutex_lock (&e->lock);
    v = e->enabled;
    g_mutex_unlock (&e->lock);
    return v;
}

void
acc_sd_engine_set_enabled (AccSdBrightnessEngine *e, gboolean enabled)
{
    g_mutex_lock (&e->lock);
    if (e->enabled == enabled)
    {
        g_mutex_unlock (&e->lock);
        return;
    }
    e->enabled = enabled;
    g_mutex_unlock (&e->lock);

    /* When disabling, set everything to full brightness. */
    if (!enabled)
    {
        gint n = 0;
        const AccSdMonitorState *list = NULL;
        if (e->monitors)
            list = acc_sd_monitor_manager_list (e->monitors, &n);
        if (n > 0)
        {
            for (gint i = 0; i < n; i++)
                acc_sd_engine_set_brightness (e, list[i].index, 1.0);
        }
        else
        {
            acc_sd_engine_set_brightness (e, -1, 1.0);
        }
    }

    for (guint i = 0; i < e->enabled_handlers->len; i++)
    {
        GPtrArray *pair = (GPtrArray *) g_ptr_array_index
            (e->enabled_handlers, i);
        AccSdEnabledChangedCb cb = (AccSdEnabledChangedCb) g_ptr_array_index (pair, 0);
        gpointer ud = g_ptr_array_index (pair, 1);
        cb (e, enabled, ud);
    }
    emit_status (e);
}

AccSdBrightness
acc_sd_engine_get_brightness (AccSdBrightnessEngine *e, gint monitor_index)
{
    gdouble v;
    g_mutex_lock (&e->lock);
    if (monitor_index == -1)
        v = e->global_brightness;
    else
    {
        gpointer pv = g_hash_table_lookup (e->per_monitor, &monitor_index);
        v = pv ? *(gdouble *) pv : 1.0;
    }
    g_mutex_unlock (&e->lock);
    return v;
}

void
acc_sd_engine_set_brightness (AccSdBrightnessEngine *e,
                              gint                   monitor_index,
                              AccSdBrightness        brightness)
{
    brightness = clamp_brightness (brightness, e->minimum_brightness);

    g_mutex_lock (&e->lock);
    if (monitor_index == -1)
    {
        if (fabs (e->global_brightness - brightness) < 1e-6)
        {
            g_mutex_unlock (&e->lock);
            return;
        }
        e->global_brightness = brightness;
        GHashTableIter iter;
        gpointer key, val;
        g_hash_table_iter_init (&iter, e->per_monitor);
        while (g_hash_table_iter_next (&iter, &key, &val))
            *(gdouble *) val = brightness;
    }
    else
    {
        gpointer pv = g_hash_table_lookup (e->per_monitor, &monitor_index);
        gdouble cur = pv ? *(gdouble *) pv : 1.0;
        if (fabs (cur - brightness) < 1e-6)
        {
            g_mutex_unlock (&e->lock);
            return;
        }
        if (!pv)
        {
            gint *idx = g_new0 (gint, 1);
            *idx = monitor_index;
            g_hash_table_insert (e->per_monitor, idx, NULL);
            pv = g_hash_table_lookup (e->per_monitor, idx);
        }
        *(gdouble *) pv = brightness;
    }
    g_mutex_unlock (&e->lock);

    /* Smooth transition. */
    start_transition (e, monitor_index, brightness);

    for (guint i = 0; i < e->brightness_handlers->len; i++)
    {
        GPtrArray *pair = (GPtrArray *) g_ptr_array_index
            (e->brightness_handlers, i);
        void (*cb)(gint, gdouble, gpointer) =
            (void (*)(gint, gdouble, gpointer)) g_ptr_array_index (pair, 0);
        gpointer ud = g_ptr_array_index (pair, 1);
        cb (monitor_index, brightness, ud);
    }
    emit_status (e);
}

/* ── Limits ────────────────────────────────────────────────────── */
gdouble
acc_sd_engine_get_minimum_brightness (AccSdBrightnessEngine *e)
{
    gdouble v;
    g_mutex_lock (&e->lock);
    v = e->minimum_brightness;
    g_mutex_unlock (&e->lock);
    return v;
}

void
acc_sd_engine_set_minimum_brightness (AccSdBrightnessEngine *e,
                                      gdouble                min)
{
    if (min < 0.05)
        min = 0.05;
    if (min > 0.30)
        min = 0.30;
    g_mutex_lock (&e->lock);
    e->minimum_brightness = min;
    g_mutex_unlock (&e->lock);
}

/* ── Presets ───────────────────────────────────────────────────── */
const AccSdPreset *
acc_sd_engine_get_presets (AccSdBrightnessEngine *e, gint *out_count)
{
    if (out_count)
        *out_count = e->n_presets;
    return e->presets;
}

gboolean
acc_sd_engine_apply_preset (AccSdBrightnessEngine *engine,
                            const gchar           *preset_id)
{
    if (!preset_id || !*preset_id)
        return FALSE;
    for (gint i = 0; i < engine->n_presets; i++)
    {
        if (g_strcmp0 (engine->presets[i].id, preset_id) == 0)
        {
            acc_sd_engine_set_brightness (engine, -1,
                                          engine->presets[i].brightness);
            /* Apply per-monitor overrides. */
            if (engine->presets[i].per_monitor)
            {
                GHashTableIter iter;
                gpointer key, val;
                g_hash_table_iter_init (&iter, engine->presets[i].per_monitor);
                while (g_hash_table_iter_next (&iter, &key, &val))
                {
                    gint idx = *(gint *) key;
                    gdouble b = *(gdouble *) val;
                    acc_sd_engine_set_brightness (engine, idx, b);
                }
            }
            return TRUE;
        }
    }
    return FALSE;
}

gboolean
acc_sd_engine_add_preset (AccSdBrightnessEngine *engine,
                          const AccSdPreset     *preset)
{
    if (!preset || !preset_id_valid (preset->id))
        return FALSE;
    /* Replace existing by id. */
    for (gint i = 0; i < engine->n_presets; i++)
    {
        if (g_strcmp0 (engine->presets[i].id, preset->id) == 0)
        {
            preset_free (&engine->presets[i]);
            engine->presets[i] = *preset; /* shallow copy; caller retains */
            return TRUE;
        }
    }
    /* Append. */
    AccSdPreset *np = g_renew (AccSdPreset, engine->presets,
                               engine->n_presets + 1);
    np[engine->n_presets] = *preset;
    engine->presets = np;
    engine->n_presets++;
    return TRUE;
}

/* ── Transitions ───────────────────────────────────────────────── */
gint
acc_sd_engine_get_transition_ms (AccSdBrightnessEngine *e)
{
    gint v;
    g_mutex_lock (&e->lock);
    v = e->transition_ms;
    g_mutex_unlock (&e->lock);
    return v;
}

void
acc_sd_engine_set_transition_ms (AccSdBrightnessEngine *e, gint ms)
{
    if (ms < 0)
        ms = 0;
    g_mutex_lock (&e->lock);
    e->transition_ms = ms;
    g_mutex_unlock (&e->lock);
}

/* ── Status ────────────────────────────────────────────────────── */
AccSdEngineStatus
acc_sd_engine_get_status (AccSdBrightnessEngine *e)
{
    AccSdEngineStatus s;
    memset (&s, 0, sizeof s);
    s.status = compute_status (e);
    s.enabled = e->enabled;
    s.global_brightness = e->global_brightness;

    gint n = 0;
    if (e->monitors)
        (void) acc_sd_monitor_manager_list (e->monitors, &n);
    s.monitor_count = n;

    s.last_error = e->last_error ? g_strdup (e->last_error) : NULL;
    s.extension_available = FALSE; /* set by D-Bus layer */
    s.dbus_available = FALSE;
    return s;
}

/* ── Callbacks ─────────────────────────────────────────────────── */
static guint
add_handler (GPtrArray *array, gpointer cb, gpointer user_data)
{
    GPtrArray *pair = g_ptr_array_new ();
    g_ptr_array_add (pair, cb);
    g_ptr_array_add (pair, user_data);
    g_ptr_array_add (array, pair);
    return array->len - 1;
}

guint
acc_sd_engine_on_brightness_changed (AccSdBrightnessEngine *engine,
                                     void                  (*cb)(gint index,
                                                                   gdouble brightness,
                                                                   gpointer user_data),
                                     gpointer              user_data)
{
    return add_handler (engine->brightness_handlers, (gpointer) cb, user_data);
}

guint
acc_sd_engine_on_enabled_changed (AccSdBrightnessEngine *engine,
                                  AccSdEnabledChangedCb  cb,
                                  gpointer               user_data)
{
    return add_handler (engine->enabled_handlers, (gpointer) cb, user_data);
}

guint
acc_sd_engine_on_status_changed (AccSdBrightnessEngine *engine,
                                 AccSdStatusChangedCb   cb,
                                 gpointer               user_data)
{
    return add_handler (engine->status_handlers, (gpointer) cb, user_data);
}

void
acc_sd_engine_disconnect (AccSdBrightnessEngine *engine,
                          guint                  handler_id)
{
    /* Determine which array. */
    GPtrArray *target = NULL;
    if (handler_id < engine->brightness_handlers->len)
        target = engine->brightness_handlers;
    else if ((handler_id -= engine->brightness_handlers->len)
             < engine->enabled_handlers->len)
        target = engine->enabled_handlers;
    else
    {
        handler_id -= engine->enabled_handlers->len;
        target = engine->status_handlers;
    }
    if (target && handler_id < target->len)
    {
        GPtrArray *pair = (GPtrArray *) g_ptr_array_index (target, handler_id);
        g_ptr_array_unref (pair);
        g_ptr_array_remove_index (target, handler_id);
    }
}
