/* control/softdimmer/monitor_manager.cpp - dynamic monitor discovery.
 *
 * Reads sys/class/drm/<card>/status and edid files to enumerate
 * connected monitors. Pure C++20 + GLib, no GNOME Shell dependency.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "monitor_manager.h"
#include "../../probe/probe.h"

#include <gio/gio.h>
#include <string.h>

struct AccSdMonitorManager {
    GMainContext *context;
    guint         poll_id;
    AccSdMonitorState *monitors;
    gint          n_monitors;
    GPtrArray    *handlers; /* (cb, user_data) pairs */
    gboolean      polling;
};

/* ------------------------------------------------------------------ edid */
static gchar *
edid_path_for (const gchar *drm_name)
{
    gchar *p = g_build_filename ("/sys/class/drm", drm_name, "edid", NULL);
    if (!acc_path_exists (p))
    {
        g_free (p);
        return NULL;
    }
    return p;
}

static gchar *
monitor_name_from_edid (const gchar *drm_name)
{
    gchar *p = edid_path_for (drm_name);
    if (!p)
        return g_strdup (drm_name);
    gchar *raw = NULL;
    gsize   len = 0;
    gchar  *name = NULL;
    if (g_file_get_contents (p, &raw, &len, NULL) && len >= 128)
    {
        /* EDID bytes 54..? hold the manufacturer name (3 packed chars)
         * and product code; we fall back to a generic name. For a
         * human-readable label we prefer the DRM connector name itself,
         * which on ASUS laptops is typically "eDP-1", "HDMI-1", etc. */
        name = g_strdup (drm_name);
    }
    else
    {
        name = g_strdup (drm_name);
    }
    g_free (raw);
    g_free (p);
    return name;
}

/* ------------------------------------------------------------------ refresh */
static gboolean
monitor_mgr_poll (AccSdMonitorManager *mgr)
{
    GDir *d = g_dir_open ("/sys/class/drm", 0, NULL);
    if (!d)
        return G_SOURCE_CONTINUE;

    /* Build the new list. */
    AccSdMonitorState *new_list = NULL;
    gint new_count = 0;
    gint cap = 0;

    const gchar *n;
    while ((n = g_dir_read_name (d)) != NULL)
    {
        /* Only real DRM connectors: card0-eDP-1, card0-HDMI-A-1, etc. */
        if (g_strstr_len (n, -1, "card") != n)
            continue;

        gchar *status_p = g_build_filename ("/sys/class/drm", n, "status", NULL);
        gchar *status = NULL;
        gboolean connected = FALSE;
        if (g_file_get_contents (status_p, &status, NULL, NULL))
        {
            g_strstrip (status);
            connected = (g_strcmp0 (status, "connected") == 0);
            g_free (status);
        }
        g_free (status_p);

        if (!connected)
            continue;

        gchar *name = monitor_name_from_edid (n);
        gint w = -1, h = -1;
        double refresh = 0.0;

        /* Try to read mode from /sys/class/drm/<n>/modes */
        gchar *modes_p = g_build_filename ("/sys/class/drm", n, "modes", NULL);
        gchar *modes = NULL;
        if (g_file_get_contents (modes_p, &modes, NULL, NULL))
        {
            /* First line is the preferred mode: "2880x1620@120.00" or "2880x1620" */
            gchar *nl = strchr (modes, '\n');
            gint   len = nl ? (gint)(nl - modes) : (gint)strlen (modes);
            gchar *first = g_strndup (modes, len);
            gchar *x = strchr (first, 'x');
            if (x)
            {
                *x = '\0';
                w = atoi (first);
                h = atoi (x + 1);
            }
            g_free (first);
            g_free (modes);
        }
        g_free (modes_p);

        if (new_count == cap)
        {
            cap = cap ? cap * 2 : 4;
            new_list = (AccSdMonitorState *) g_realloc
                (new_list, cap * sizeof (AccSdMonitorState));
        }
        AccSdMonitorState *ms = &new_list[new_count++];
        memset (ms, 0, sizeof *ms);
        ms->index = new_count - 1; /* will be renumbered below */
        ms->name = name;
        ms->width = w;
        ms->height = h;
        ms->refresh_hz = refresh;
        ms->connected = TRUE;
        ms->brightness = 1.0;
        ms->enabled = FALSE;
    }
    g_dir_close (d);

    /* Renumber indices 0..n-1. */
    for (gint i = 0; i < new_count; i++)
        new_list[i].index = i;

    /* Detect changes vs previous list. */
    gboolean changed = (new_count != mgr->n_monitors);
    if (!changed && mgr->n_monitors > 0)
    {
        for (gint i = 0; i < new_count && !changed; i++)
        {
            const AccSdMonitorState *a = &new_list[i];
            const AccSdMonitorState *b = &mgr->monitors[i];
            if (g_strcmp0 (a->name, b->name) != 0 ||
                a->width != b->width || a->height != b->height ||
                a->connected != b->connected)
                changed = TRUE;
        }
    }

    /* Free old list. */
    for (gint i = 0; i < mgr->n_monitors; i++)
        g_free (mgr->monitors[i].name);
    g_free (mgr->monitors);

    mgr->monitors = new_list;
    mgr->n_monitors = new_count;

    if (changed && mgr->handlers->len > 0)
    {
        /* Invoke handlers from an idle so GTK callers are safe. */
        g_idle_add ((GSourceFunc) [](gpointer data) -> gboolean {
            AccSdMonitorManager *m = (AccSdMonitorManager *) data;
            for (guint i = 0; i < m->handlers->len; i++)
            {
                GPtrArray *pair = (GPtrArray *) g_ptr_array_index (m->handlers, i);
                AccSdMonitorChangedCb cb = (AccSdMonitorChangedCb)
                    g_ptr_array_index (pair, 0);
                gpointer ud = g_ptr_array_index (pair, 1);
                cb (m, ud);
            }
            return G_SOURCE_REMOVE;
        }, mgr);
    }

    return G_SOURCE_CONTINUE;
}

/* ------------------------------------------------------------------ public API */
AccSdMonitorManager *
acc_sd_monitor_manager_new (void)
{
    AccSdMonitorManager *mgr = g_new0 (AccSdMonitorManager, 1);
    mgr->context = g_main_context_default ();
    mgr->handlers = g_ptr_array_new ();
    /* Poll every 2 seconds — cheap sysfs reads, no aggressive polling. */
    mgr->poll_id = g_timeout_add_seconds (2,
                                          (GSourceFunc) monitor_mgr_poll,
                                          mgr);
    /* Initial read. */
    monitor_mgr_poll (mgr);
    return mgr;
}

void
acc_sd_monitor_manager_free (AccSdMonitorManager *mgr)
{
    if (!mgr)
        return;
    if (mgr->poll_id)
        g_source_remove (mgr->poll_id);
    for (gint i = 0; i < mgr->n_monitors; i++)
        g_free (mgr->monitors[i].name);
    g_free (mgr->monitors);
    g_ptr_array_unref (mgr->handlers);
    g_free (mgr);
}

const AccSdMonitorState *
acc_sd_monitor_manager_refresh (AccSdMonitorManager *mgr, gint *out_count)
{
    monitor_mgr_poll (mgr);
    if (out_count)
        *out_count = mgr->n_monitors;
    return mgr->monitors;
}

const AccSdMonitorState *
acc_sd_monitor_manager_list (AccSdMonitorManager *mgr, gint *out_count)
{
    if (out_count)
        *out_count = mgr->n_monitors;
    return mgr->monitors;
}

const AccSdMonitorState *
acc_sd_monitor_manager_get (AccSdMonitorManager *mgr, gint index)
{
    if (index < 0 || index >= mgr->n_monitors)
        return NULL;
    return &mgr->monitors[index];
}

guint
acc_sd_monitor_manager_on_changed (AccSdMonitorManager    *mgr,
                                   AccSdMonitorChangedCb   cb,
                                   gpointer                user_data)
{
    GPtrArray *pair = g_ptr_array_new ();
    g_ptr_array_add (pair, (gpointer) cb);
    g_ptr_array_add (pair, user_data);
    g_ptr_array_add (mgr->handlers, pair);
    return mgr->handlers->len - 1;
}

void
acc_sd_monitor_manager_disconnect (AccSdMonitorManager *mgr,
                                   guint                handler_id)
{
    if (handler_id < mgr->handlers->len)
    {
        GPtrArray *pair = (GPtrArray *) g_ptr_array_index
            (mgr->handlers, handler_id);
        g_ptr_array_unref (pair);
        g_ptr_array_remove_index (mgr->handlers, handler_id);
    }
}
