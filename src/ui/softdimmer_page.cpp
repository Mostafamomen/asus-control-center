/* ui/softdimmer_page.cpp - native Soft Dimmer page inside ASUSCraft.
 *
 * Hardware → Display → Soft Dimmer.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "softdimmer_page.h"
#include "hubs.h"
#include "mcui.h"
#include "theme.h"
#include "../i18n/i18n.h"
#include "../control/softdimmer/brightness_engine.h"
#include "../control/softdimmer/monitor_manager.h"
#include "../control/softdimmer/softdimmer_service.h"
#include <adwaita.h>
#include <string.h>

/* ---------------------------------------------------------------- state */
typedef struct {
    AccSdBrightnessEngine *engine;
    AccSdMonitorManager   *monitors;
    AccSdSoftDimmerService *service;

    /* UI widgets (main card). */
    GtkWidget *enable_switch;
    GtkWidget *brightness_slider;
    GtkWidget *brightness_pct;
    GtkWidget *status_label;

    /* Presets. */
    GtkWidget *presets_box;

    /* Monitors. */
    GtkWidget *monitors_box;
    guint      monitor_changed_handler;

    /* Advanced. */
    GtkWidget *transition_switch;
    GtkWidget *transition_scale;
    GtkWidget *restore_switch;
    GtkWidget *autostart_switch;
    GtkWidget *fullscreen_switch;

    /* Diagnostics. */
    GtkWidget *diag_engine_status;
    GtkWidget *diag_extension_status;
    GtkWidget *diag_dbus_status;
    GtkWidget *diag_monitor_status;
    GtkWidget *diag_error;

    guint brightness_handler;
    guint enabled_handler;
    guint status_handler;
    gboolean syncing;
} SdPage;

static SdPage *g_sd = NULL;

/* ---------------------------------------------------------------- helpers */
static gdouble
clamp01 (gdouble v)
{
    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;
    return v;
}

static void
sd_set_slider_no_notify (GtkRange *range, gdouble value)
{
    gtk_range_set_value (range, value);
}

/* ── brightness slider: normalized 0..100 mapped to [min, 1.0] ── */
static void
sd_brightness_slider_cb (GtkRange *range, gpointer data)
{
    if (!g_sd || g_sd->syncing)
        return;
    SdPage *p = (SdPage *) data;
    gdouble minb = acc_sd_engine_get_minimum_brightness (p->engine);
    gdouble raw = gtk_range_get_value (range) / 100.0;           /* 0..1 */
    gdouble brightness = minb + raw * (1.0 - minb);             /* [min, 1.0] */
    acc_sd_engine_set_brightness (p->engine, -1, brightness);
}

static void
sd_enable_switch_cb (GtkSwitch *sw, GParamSpec *ps, gpointer data)
{
    (void) ps;
    if (!g_sd || g_sd->syncing)
        return;
    SdPage *p = (SdPage *) data;
    acc_sd_engine_set_enabled (p->engine, gtk_switch_get_active (sw));
}

/* ── refresh UI from engine state ── */
static void
sd_refresh_ui (SdPage *p)
{
    if (!p || !p->engine)
        return;
    g_sd->syncing = TRUE;

    gboolean enabled = acc_sd_engine_get_enabled (p->engine);
    gtk_switch_set_active (GTK_SWITCH (p->enable_switch), enabled);

    gdouble minb = acc_sd_engine_get_minimum_brightness (p->engine);
    gdouble bright = acc_sd_engine_get_brightness (p->engine, -1);
    gdouble slider = (bright - minb) / (1.0 - minb);
    sd_set_slider_no_notify (GTK_RANGE (p->brightness_slider), clamp01 (slider) * 100.0);

    gchar *pct = g_strdup_printf ("%.0f%%", bright * 100.0);
    gtk_label_set_text (GTK_LABEL (p->brightness_pct), pct);
    g_free (pct);

    gchar *status = g_strdup_printf (enabled ? "Active — %.0f%%" : "Disabled",
                                     bright * 100.0);
    gtk_label_set_text (GTK_LABEL (p->status_label), status);
    g_free (status);

    g_sd->syncing = FALSE;
}

/* ── engine callbacks ── */
static void
sd_on_brightness_changed (gint index, gdouble brightness, gpointer user_data)
{
    (void) index;
    (void) brightness;
    SdPage *p = (SdPage *) user_data;
    sd_refresh_ui (p);
}

static void
sd_on_enabled_changed (AccSdBrightnessEngine *engine, gboolean enabled,
                       gpointer user_data)
{
    (void) engine; (void) enabled;
    SdPage *p = (SdPage *) user_data;
    sd_refresh_ui (p);
}

static void
sd_on_status_changed (AccSdBrightnessEngine *engine, AccSdStatus status,
                      gpointer user_data)
{
    (void) engine; (void) status;
    SdPage *p = (SdPage *) user_data;
    sd_refresh_ui (p);
}

/* ── monitor card factory ── */
static GtkWidget *
sd_monitor_card (const AccSdMonitorState *m, SdPage *p)
{
    GtkWidget *card = mc_card ();
    gtk_widget_set_hexpand (card, TRUE);
    gtk_widget_set_size_request (card, 220, -1);

    GtkWidget *head = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *name = gtk_label_new (m->name ? m->name : "Unknown");
    gtk_widget_add_css_class (name, "cardtitle");
    gtk_label_set_xalign (GTK_LABEL (name), 0.0);
    gtk_label_set_ellipsize (GTK_LABEL (name), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars (GTK_LABEL (name), 20);
    gtk_box_append (GTK_BOX (head), name);

    GtkWidget *sub = gtk_label_new (NULL);
    gchar *sub_txt = g_strdup_printf ("%d × %d%s",
                                      m->width > 0 ? m->width : 0,
                                      m->height > 0 ? m->height : 0,
                                      m->refresh_hz > 0
                                        ? g_strdup_printf (" @ %.0f Hz", m->refresh_hz)
                                        : "");
    g_free (sub_txt);
    sub_txt = g_strdup_printf ("%d × %d%s",
                               m->width > 0 ? m->width : 0,
                               m->height > 0 ? m->height : 0,
                               m->refresh_hz > 0 ? "" : "");
    if (m->refresh_hz > 0)
    {
        g_free (sub_txt);
        sub_txt = g_strdup_printf ("%d × %d @ %.0f Hz", m->width, m->height,
                                   m->refresh_hz);
    }
    gtk_label_set_text (GTK_LABEL (sub), sub_txt);
    g_free (sub_txt);
    gtk_widget_add_css_class (sub, "foot");
    gtk_label_set_xalign (GTK_LABEL (sub), 0.0);
    gtk_box_append (GTK_BOX (head), sub);
    gtk_box_append (GTK_BOX (card), head);

    /* Enable/disable switch for this monitor. */
    GtkWidget *sw_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *sw_lab = gtk_label_new ("Dimming");
    gtk_widget_add_css_class (sw_lab, "dim");
    gtk_box_append (GTK_BOX (sw_row), sw_lab);
    GtkWidget *sw = gtk_switch_new ();
    gtk_switch_set_active (GTK_SWITCH (sw), m->enabled);
    gtk_widget_set_valign (sw, GTK_ALIGN_CENTER);
    gtk_box_append (GTK_BOX (sw_row), sw);
    gtk_box_append (GTK_BOX (card), sw_row);

    /* Slider. */
    GtkWidget *scale_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL,
                                                 0.0, 100.0, 1.0);
    gtk_scale_set_draw_value (GTK_SCALE (scale), FALSE);
    gtk_widget_set_hexpand (scale, TRUE);
    gdouble minb = acc_sd_engine_get_minimum_brightness (p->engine);
    gdouble cur = acc_sd_engine_get_brightness (p->engine, m->index);
    gdouble scur = (cur - minb) / (1.0 - minb) * 100.0;
    gtk_range_set_value (GTK_RANGE (scale), clamp01 (scur));
    gtk_box_append (GTK_BOX (scale_row), scale);
    GtkWidget *scale_pct = gtk_label_new ("—");
    gtk_widget_add_css_class (scale_pct, "val");
    gtk_box_append (GTK_BOX (scale_row), scale_pct);
    gtk_box_append (GTK_BOX (card), scale_row);

    g_signal_connect (scale, "value-changed", G_CALLBACK (+[](GtkRange *r,
        gpointer d) {
        SdPage *pp = (SdPage *) d;
        /* We need the monitor index — stored as object data. */
        gint idx = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (r),
                                                          "sd-monitor-idx"));
        gdouble minb2 = acc_sd_engine_get_minimum_brightness (pp->engine);
        gdouble raw = gtk_range_get_value (r) / 100.0;
        gdouble brightness = minb2 + raw * (1.0 - minb2);
        acc_sd_engine_set_brightness (pp->engine, idx, brightness);
    }), p);
    g_object_set_data (G_OBJECT (scale), "sd-monitor-idx",
                         GINT_TO_POINTER (m->index));

    g_signal_connect (scale, "value-changed", G_CALLBACK (+[](GtkRange *r,
        gpointer d) {
        SdPage *pp = (SdPage *) d;
        gdouble minb2 = acc_sd_engine_get_minimum_brightness (pp->engine);
        gdouble raw = gtk_range_get_value (r) / 100.0;
        gdouble brightness = minb2 + raw * (1.0 - minb2);
        gchar *txt = g_strdup_printf ("%.0f%%", brightness * 100.0);
        GtkWidget *lbl = (GtkWidget *) g_object_get_data (G_OBJECT (r),
                                                            "sd-monitor-pct-widget");
        gtk_label_set_text (GTK_LABEL (lbl), txt);
        g_free (txt);
    }), p);
    g_object_set_data (G_OBJECT (scale_pct), "sd-monitor-pct-widget",
                         scale_pct);

    gtk_box_append (GTK_BOX (card), scale_row);
    return card;
}

/* ── rebuild monitor cards ── */
static void
sd_rebuild_monitors (SdPage *p)
{
    if (!p->monitors_box)
        return;
    /* GTK4: remove all children. */
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child (p->monitors_box)) != NULL)
        gtk_box_remove (GTK_BOX (p->monitors_box), child);

    gint n = 0;
    const AccSdMonitorState *list =
        acc_sd_monitor_manager_list (p->monitors, &n);
    if (n == 0)
    {
        GtkWidget *none = gtk_label_new ("No monitors detected");
        gtk_widget_add_css_class (none, "foot");
        gtk_box_append (GTK_BOX (p->monitors_box), none);
        return;
    }
    for (gint i = 0; i < n; i++)
    {
        GtkWidget *card = sd_monitor_card (&list[i], p);
        gtk_box_append (GTK_BOX (p->monitors_box), card);
    }
}

/* ── presets ── */
static void
sd_preset_clicked (GtkButton *btn, gpointer data)
{
    SdPage *p = (SdPage *) data;
    const gchar *id = (const gchar *) g_object_get_data (G_OBJECT (btn),
                                                           "sd-preset-id");
    acc_sd_engine_apply_preset (p->engine, id);
    sd_refresh_ui (p);
}

static void
sd_rebuild_presets (SdPage *p)
{
    if (!p->presets_box)
        return;
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child (p->presets_box)) != NULL)
        gtk_box_remove (GTK_BOX (p->presets_box), child);

    gint n = 0;
    const AccSdPreset *presets = acc_sd_engine_get_presets (p->engine, &n);
    for (gint i = 0; i < n; i++)
    {
        GtkWidget *btn = gtk_button_new_with_label (presets[i].name);
        gtk_widget_add_css_class (btn, "optcard");
        gtk_widget_set_hexpand (btn, TRUE);
        gtk_widget_set_halign (btn, GTK_ALIGN_FILL);
        g_object_set_data (G_OBJECT (btn), "sd-preset-id",
                             (gpointer) presets[i].id);
        g_signal_connect (btn, "clicked", G_CALLBACK (sd_preset_clicked), p);
        gtk_box_append (GTK_BOX (p->presets_box), btn);
    }
}

/* ── diagnostics refresh ── */
static void
sd_refresh_diagnostics (SdPage *p)
{
    if (!p)
        return;
    AccSdEngineStatus st;
    memset (&st, 0, sizeof st);
    /* We read directly from the engine for accurate values. */
    gboolean enabled = acc_sd_engine_get_enabled (p->engine);
    gdouble bright = acc_sd_engine_get_brightness (p->engine, -1);
    gint n = 0;
    acc_sd_monitor_manager_list (p->monitors, &n);

    gtk_label_set_text (GTK_LABEL (p->diag_engine_status),
                        enabled ? "Running — dimming active"
                                : "Running — disabled");
    gtk_label_set_text (GTK_LABEL (p->diag_extension_status),
                        "Not detected (start GNOME Shell extension)");
    gtk_label_set_text (GTK_LABEL (p->diag_dbus_status),
                        acc_sd_service_is_exported (p->service)
                          ? "Exported — io.github.asuscraft.SoftDimmer"
                          : "Not exported");
    gtk_label_set_text (GTK_LABEL (p->diag_monitor_status),
                        n > 0 ? g_strdup_printf ("%d monitor(s) detected", n)
                              : g_strdup ("No monitors detected"));
    gtk_label_set_text (GTK_LABEL (p->diag_error),
                        bright >= 0.0 && bright < 0.99
                          ? g_strdup_printf ("Software dimming at %.0f%%",
                                             bright * 100.0)
                          : g_strdup ("No errors"));
}

/* ================================================================ page */
GtkWidget *
acc_page_softdimmer (AccUI *u)
{
    (void) u;
    if (g_sd)
        return NULL; /* already built */

    SdPage *p = g_new0 (SdPage, 1);
    g_sd = p;

    /* Use the application-owned engine/monitors/service (created in main.cpp). */
    p->engine = g_sd_engine;
    p->monitors = g_sd_monitors;
    p->service = g_sd_service;

    /* Engine callbacks (D-Bus forwarding already connected in main.cpp;
     * these just refresh the UI). */
    p->brightness_handler =
        acc_sd_engine_on_brightness_changed (p->engine,
            sd_on_brightness_changed, p);
    p->enabled_handler =
        acc_sd_engine_on_enabled_changed (p->engine, sd_on_enabled_changed, p);
    p->status_handler =
        acc_sd_engine_on_status_changed (p->engine, sd_on_status_changed, p);

    GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 14);

    /* ── Header ── */
    GtkWidget *head = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
    GtkWidget *eye = mc_label ("Soft Dimmer", "eyebrow", 0);
    GtkWidget *title = mc_label (acc_tr (ACC_T_SOFTDIM), "pagetitle", 0);
    GtkWidget *sub = mc_label (acc_tr (ACC_T_SOFTDIM_SUB), "pagesub", 0);
    gtk_label_set_wrap (GTK_LABEL (sub), TRUE);
    gtk_label_set_max_width_chars (GTK_LABEL (sub), 80);
    gtk_box_append (GTK_BOX (head), eye);
    gtk_box_append (GTK_BOX (head), title);
    gtk_box_append (GTK_BOX (head), sub);
    gtk_box_append (GTK_BOX (page), head);

    /* ── A. Main brightness card ── */
    {
        GtkWidget *c = mc_card ();
        GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
        {
            GtkWidget *im = gtk_image_new_from_icon_name
                ("display-brightness-symbolic");
            gtk_image_set_pixel_size (GTK_IMAGE (im), 40);
            gtk_widget_add_css_class (im, "cardicon");
            gtk_box_append (GTK_BOX (h), im);
        }
        {
            GtkWidget *cd = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
            gtk_box_append (GTK_BOX (cd),
                mc_title (acc_tr (ACC_T_SOFTDIM)));
            GtkWidget *csub = mc_label (acc_tr (ACC_T_SOFTDIM_SUB), "foot", 0);
            gtk_label_set_wrap (GTK_LABEL (csub), TRUE);
            gtk_label_set_max_width_chars (GTK_LABEL (csub), 30);
            gtk_box_append (GTK_BOX (cd), csub);
            gtk_box_append (GTK_BOX (h), cd);
        }
        {
            GtkWidget *ctl = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
            gtk_widget_set_hexpand (ctl, TRUE);

            /* Enable/disable switch. */
            GtkWidget *sw_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
            GtkWidget *sw_lab = gtk_label_new (acc_tr (ACC_T_SD_ENABLE));
            gtk_widget_add_css_class (sw_lab, "dim");
            gtk_box_append (GTK_BOX (sw_row), sw_lab);
            p->enable_switch = gtk_switch_new ();
            gtk_switch_set_active (GTK_SWITCH (p->enable_switch), FALSE);
            gtk_widget_set_valign (p->enable_switch, GTK_ALIGN_CENTER);
            g_signal_connect (p->enable_switch, "notify::active",
                              G_CALLBACK (sd_enable_switch_cb), p);
            gtk_box_append (GTK_BOX (sw_row), p->enable_switch);
            gtk_box_append (GTK_BOX (ctl), sw_row);

            /* Large slider. */
            GtkWidget *sl_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
            p->brightness_slider = gtk_scale_new_with_range
                (GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0);
            gtk_scale_set_draw_value (GTK_SCALE (p->brightness_slider), FALSE);
            gtk_widget_set_hexpand (p->brightness_slider, TRUE);
            gtk_range_set_value (GTK_RANGE (p->brightness_slider), 100.0);
            g_signal_connect (p->brightness_slider, "value-changed",
                              G_CALLBACK (sd_brightness_slider_cb), p);
            gtk_box_append (GTK_BOX (sl_row), p->brightness_slider);
            p->brightness_pct = gtk_label_new ("100%");
            gtk_widget_add_css_class (p->brightness_pct, "val");
            gtk_box_append (GTK_BOX (sl_row), p->brightness_pct);
            gtk_box_append (GTK_BOX (ctl), sl_row);

            /* Status label + reset. */
            GtkWidget *st_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
            p->status_label = gtk_label_new ("Disabled");
            gtk_widget_add_css_class (p->status_label, "foot");
            gtk_box_append (GTK_BOX (st_row), p->status_label);
            GtkWidget *fill = gtk_label_new (NULL);
            gtk_widget_set_hexpand (fill, TRUE);
            gtk_box_append (GTK_BOX (st_row), fill);
            GtkWidget *reset_btn = gtk_button_new_with_label (acc_tr (ACC_T_SD_RESET));
            gtk_widget_add_css_class (reset_btn, "actionbtn");
            g_signal_connect (reset_btn, "clicked", G_CALLBACK (+[](GtkButton *,
                gpointer d) {
                SdPage *pp = (SdPage *) d;
                acc_sd_engine_set_brightness (pp->engine, -1, 1.0);
                acc_sd_engine_set_enabled (pp->engine, FALSE);
                sd_refresh_ui (pp);
            }), p);
            gtk_box_append (GTK_BOX (st_row), reset_btn);
            gtk_box_append (GTK_BOX (ctl), st_row);

            gtk_box_append (GTK_BOX (h), ctl);
        }
        gtk_box_append (GTK_BOX (c), h);
        gtk_box_append (GTK_BOX (page), c);
    }

    /* ── B. Presets card ── */
    {
        GtkWidget *c = mc_card ();
        GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
        {
            GtkWidget *im = gtk_image_new_from_icon_name ("emblem-favourite-symbolic");
            gtk_image_set_pixel_size (GTK_IMAGE (im), 40);
            gtk_widget_add_css_class (im, "cardicon");
            gtk_box_append (GTK_BOX (h), im);
        }
        {
            GtkWidget *cd = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
            gtk_box_append (GTK_BOX (cd), mc_title (acc_tr (ACC_T_SD_PRESETS)));
            GtkWidget *csub = mc_label (acc_tr (ACC_T_SD_PRESETS_SUB), "foot", 0);
            gtk_label_set_wrap (GTK_LABEL (csub), TRUE);
            gtk_label_set_max_width_chars (GTK_LABEL (csub), 30);
            gtk_box_append (GTK_BOX (cd), csub);
            gtk_box_append (GTK_BOX (h), cd);
        }
        {
            GtkWidget *ctl = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
            gtk_widget_set_hexpand (ctl, TRUE);
            p->presets_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
            gtk_widget_set_hexpand (p->presets_box, TRUE);
            gtk_box_append (GTK_BOX (ctl), p->presets_box);
            sd_rebuild_presets (p);
            gtk_box_append (GTK_BOX (h), ctl);
        }
        gtk_box_append (GTK_BOX (c), h);
        gtk_box_append (GTK_BOX (page), c);
    }

    /* ── C. Monitors card ── */
    {
        GtkWidget *c = mc_card ();
        GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
        {
            GtkWidget *im = gtk_image_new_from_icon_name ("video-display-symbolic");
            gtk_image_set_pixel_size (GTK_IMAGE (im), 40);
            gtk_widget_add_css_class (im, "cardicon");
            gtk_box_append (GTK_BOX (h), im);
        }
        {
            GtkWidget *cd = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
            gtk_box_append (GTK_BOX (cd), mc_title (acc_tr (ACC_T_SD_MONITORS)));
            GtkWidget *csub = mc_label (acc_tr (ACC_T_SD_MONITORS_SUB), "foot", 0);
            gtk_label_set_wrap (GTK_LABEL (csub), TRUE);
            gtk_label_set_max_width_chars (GTK_LABEL (csub), 30);
            gtk_box_append (GTK_BOX (cd), csub);
            gtk_box_append (GTK_BOX (h), cd);
        }
        {
            GtkWidget *ctl = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
            gtk_widget_set_hexpand (ctl, TRUE);
            GtkWidget *sync_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
            GtkWidget *sync_lab = gtk_label_new (acc_tr (ACC_T_SD_SYNC_MONITORS));
            gtk_widget_add_css_class (sync_lab, "dim");
            gtk_box_append (GTK_BOX (sync_row), sync_lab);
            GtkWidget *fill = gtk_label_new (NULL);
            gtk_widget_set_hexpand (fill, TRUE);
            gtk_box_append (GTK_BOX (sync_row), fill);
            gtk_box_append (GTK_BOX (ctl), sync_row);
            p->monitors_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
            gtk_widget_set_hexpand (p->monitors_box, TRUE);
            gtk_box_append (GTK_BOX (ctl), p->monitors_box);
            sd_rebuild_monitors (p);
            gtk_box_append (GTK_BOX (h), ctl);
        }
        gtk_box_append (GTK_BOX (c), h);
        gtk_box_append (GTK_BOX (page), c);
    }

    /* ── D. Advanced settings card ── */
    {
        GtkWidget *c = mc_card ();
        GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
        {
            GtkWidget *im = gtk_image_new_from_icon_name ("preferences-system-symbolic");
            gtk_image_set_pixel_size (GTK_IMAGE (im), 40);
            gtk_widget_add_css_class (im, "cardicon");
            gtk_box_append (GTK_BOX (h), im);
        }
        {
            GtkWidget *cd = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
            gtk_box_append (GTK_BOX (cd), mc_title (acc_tr (ACC_T_SD_ADVANCED)));
            GtkWidget *csub = mc_label (acc_tr (ACC_T_SD_ADVANCED_SUB), "foot", 0);
            gtk_label_set_wrap (GTK_LABEL (csub), TRUE);
            gtk_label_set_max_width_chars (GTK_LABEL (csub), 30);
            gtk_box_append (GTK_BOX (cd), csub);
            gtk_box_append (GTK_BOX (h), cd);
        }
        {
            GtkWidget *ctl = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
            gtk_widget_set_hexpand (ctl, TRUE);

            p->transition_switch = gtk_switch_new ();
            gtk_switch_set_active (GTK_SWITCH (p->transition_switch), TRUE);
            GtkWidget *ts_row = mc_feature ("emblem-ok-symbolic",
                "Smooth transitions", "Fade brightness changes instead of jumping.",
                NULL);
            GtkWidget *sw_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
            gtk_box_append (GTK_BOX (sw_box), p->transition_switch);
            /* mc_feature's out_ctl is NULL so we append manually below */
            (void) ts_row;

            p->transition_scale = gtk_scale_new_with_range
                (GTK_ORIENTATION_HORIZONTAL, 100, 2000, 100);
            gtk_scale_set_draw_value (GTK_SCALE (p->transition_scale), FALSE);
            gtk_widget_set_hexpand (p->transition_scale, TRUE);
            gtk_range_set_value (GTK_RANGE (p->transition_scale),
                                 (gdouble) acc_sd_engine_get_transition_ms (p->engine));

            p->restore_switch = gtk_switch_new ();
            gtk_switch_set_active (GTK_SWITCH (p->restore_switch), TRUE);

            p->autostart_switch = gtk_switch_new ();
            gtk_switch_set_active (GTK_SWITCH (p->autostart_switch), FALSE);

            p->fullscreen_switch = gtk_switch_new ();
            gtk_switch_set_active (GTK_SWITCH (p->fullscreen_switch), TRUE);

            /* rows: label + control */
            {
                GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
                GtkWidget *im2 = gtk_image_new_from_icon_name ("emblem-ok-symbolic");
                gtk_image_set_pixel_size (GTK_IMAGE (im2), 20);
                gtk_widget_add_css_class (im2, "cardicon");
                gtk_box_append (GTK_BOX (row), im2);
                GtkWidget *txt = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
                gtk_box_append (GTK_BOX (txt), gtk_label_new (acc_tr (ACC_T_SD_TRANSITIONS)));
                GtkWidget *sub2 = gtk_label_new (acc_tr (ACC_T_SD_TRANSITIONS_SUB));
                gtk_widget_add_css_class (sub2, "foot");
                gtk_label_set_wrap (GTK_LABEL (sub2), TRUE);
                gtk_box_append (GTK_BOX (txt), sub2);
                gtk_box_append (GTK_BOX (row), txt);
                GtkWidget *ctl2 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
                gtk_box_append (GTK_BOX (ctl2), p->transition_switch);
                gtk_box_append (GTK_BOX (row), ctl2);
                gtk_box_append (GTK_BOX (ctl), row);
            }
            {
                GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
                GtkWidget *im2 = gtk_image_new_from_icon_name ("time-symbolic");
                gtk_image_set_pixel_size (GTK_IMAGE (im2), 20);
                gtk_widget_add_css_class (im2, "cardicon");
                gtk_box_append (GTK_BOX (row), im2);
                GtkWidget *txt = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
                gtk_box_append (GTK_BOX (txt), gtk_label_new (acc_tr (ACC_T_SD_TRANSITION_DURATION)));
                GtkWidget *sub2 = gtk_label_new (acc_tr (ACC_T_SD_TRANSITION_DURATION_SUB));
                gtk_widget_add_css_class (sub2, "foot");
                gtk_label_set_wrap (GTK_LABEL (sub2), TRUE);
                gtk_box_append (GTK_BOX (txt), sub2);
                gtk_box_append (GTK_BOX (row), txt);
                GtkWidget *ctl2 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
                gtk_box_append (GTK_BOX (ctl2), p->transition_scale);
                gtk_box_append (GTK_BOX (row), ctl2);
                gtk_box_append (GTK_BOX (ctl), row);
            }
            {
                GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
                GtkWidget *im2 = gtk_image_new_from_icon_name ("system-restart-symbolic");
                gtk_image_set_pixel_size (GTK_IMAGE (im2), 20);
                gtk_widget_add_css_class (im2, "cardicon");
                gtk_box_append (GTK_BOX (row), im2);
                GtkWidget *txt = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
                gtk_box_append (GTK_BOX (txt), gtk_label_new (acc_tr (ACC_T_SD_RESTORE)));
                GtkWidget *sub2 = gtk_label_new (acc_tr (ACC_T_SD_RESTORE_SUB));
                gtk_widget_add_css_class (sub2, "foot");
                gtk_label_set_wrap (GTK_LABEL (sub2), TRUE);
                gtk_box_append (GTK_BOX (txt), sub2);
                gtk_box_append (GTK_BOX (row), txt);
                GtkWidget *ctl2 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
                gtk_box_append (GTK_BOX (ctl2), p->restore_switch);
                gtk_box_append (GTK_BOX (row), ctl2);
                gtk_box_append (GTK_BOX (ctl), row);
            }
            {
                GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
                GtkWidget *im2 = gtk_image_new_from_icon_name ("system-run-symbolic");
                gtk_image_set_pixel_size (GTK_IMAGE (im2), 20);
                gtk_widget_add_css_class (im2, "cardicon");
                gtk_box_append (GTK_BOX (row), im2);
                GtkWidget *txt = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
                gtk_box_append (GTK_BOX (txt), gtk_label_new (acc_tr (ACC_T_SD_AUTOSTART)));
                GtkWidget *sub2 = gtk_label_new (acc_tr (ACC_T_SD_AUTOSTART_SUB));
                gtk_widget_add_css_class (sub2, "foot");
                gtk_label_set_wrap (GTK_LABEL (sub2), TRUE);
                gtk_box_append (GTK_BOX (txt), sub2);
                gtk_box_append (GTK_BOX (row), txt);
                GtkWidget *ctl2 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
                gtk_box_append (GTK_BOX (ctl2), p->autostart_switch);
                gtk_box_append (GTK_BOX (row), ctl2);
                gtk_box_append (GTK_BOX (ctl), row);
            }
            {
                GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
                GtkWidget *im2 = gtk_image_new_from_icon_name ("object-flip-symbolic");
                gtk_image_set_pixel_size (GTK_IMAGE (im2), 20);
                gtk_widget_add_css_class (im2, "cardicon");
                gtk_box_append (GTK_BOX (row), im2);
                GtkWidget *txt = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
                gtk_box_append (GTK_BOX (txt), gtk_label_new (acc_tr (ACC_T_SD_FULLSCREEN)));
                GtkWidget *sub2 = gtk_label_new (acc_tr (ACC_T_SD_FULLSCREEN_SUB));
                gtk_widget_add_css_class (sub2, "foot");
                gtk_label_set_wrap (GTK_LABEL (sub2), TRUE);
                gtk_box_append (GTK_BOX (txt), sub2);
                gtk_box_append (GTK_BOX (row), txt);
                GtkWidget *ctl2 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
                gtk_box_append (GTK_BOX (ctl2), p->fullscreen_switch);
                gtk_box_append (GTK_BOX (row), ctl2);
                gtk_box_append (GTK_BOX (ctl), row);
            }

            gtk_box_append (GTK_BOX (h), ctl);
        }
        gtk_box_append (GTK_BOX (c), h);
        gtk_box_append (GTK_BOX (page), c);
    }

    /* ── E. Diagnostics card ── */
    {
        GtkWidget *c = mc_card ();
        GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
        {
            GtkWidget *im = gtk_image_new_from_icon_name ("dialog-information-symbolic");
            gtk_image_set_pixel_size (GTK_IMAGE (im), 40);
            gtk_widget_add_css_class (im, "cardicon");
            gtk_box_append (GTK_BOX (h), im);
        }
        {
            GtkWidget *cd = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
            gtk_box_append (GTK_BOX (cd), mc_title (acc_tr (ACC_T_SD_DIAGNOSTICS)));
            GtkWidget *csub = mc_label (acc_tr (ACC_T_SD_DIAGNOSTICS_SUB), "foot", 0);
            gtk_label_set_wrap (GTK_LABEL (csub), TRUE);
            gtk_label_set_max_width_chars (GTK_LABEL (csub), 30);
            gtk_box_append (GTK_BOX (cd), csub);
            gtk_box_append (GTK_BOX (h), cd);
        }
        {
            GtkWidget *ctl = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
            gtk_widget_set_hexpand (ctl, TRUE);

            auto add_diag_row = [&](const gchar *label, GtkWidget **out) {
                GtkWidget *row = mc_row (label, out);
                gtk_box_append (GTK_BOX (ctl), row);
            };

            add_diag_row (acc_tr (ACC_T_SD_DIAG_ENGINE), &p->diag_engine_status);
            add_diag_row (acc_tr (ACC_T_SD_DIAG_EXTENSION), &p->diag_extension_status);
            add_diag_row (acc_tr (ACC_T_SD_DIAG_DBUS), &p->diag_dbus_status);
            add_diag_row (acc_tr (ACC_T_SD_DIAG_MONITORS), &p->diag_monitor_status);
            add_diag_row (acc_tr (ACC_T_SD_DIAG_ERROR), &p->diag_error);

            gtk_box_append (GTK_BOX (h), ctl);
        }
        gtk_box_append (GTK_BOX (c), h);
        gtk_box_append (GTK_BOX (page), c);
    }

    /* Initial UI sync. */
    sd_refresh_ui (p);
    sd_refresh_diagnostics (p);

    /* Refresh diagnostics periodically (2s) — cheap read-only. */
    guint diag_id = g_timeout_add_seconds (2, [](gpointer data) -> gboolean {
        SdPage *pp = (SdPage *) data;
        sd_refresh_diagnostics (pp);
        /* Also rebuild monitors if the set changed. */
        sd_rebuild_monitors (pp);
        return G_SOURCE_CONTINUE;
    }, p);
    (void) diag_id;

    return mc_page (page);
}
