/* control/softdimmer/softdimmer_service.cpp - D-Bus service for Soft Dimmer.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "softdimmer_service.h"
#include "brightness_engine.h"

#include <gio/gio.h>
#include <string.h>

static const gchar *introspection_xml =
"<node>"
"  <interface name='" ACC_SD_DBUS_INTERFACE "'>"
"    <method name='GetBrightness'>"
"      <arg name='monitor_index' type='i' direction='in'/>"
"      <arg name='brightness' type='d' direction='out'/>"
"    </method>"
"    <method name='SetBrightness'>"
"      <arg name='monitor_index' type='i' direction='in'/>"
"      <arg name='brightness' type='d' direction='in'/>"
"    </method>"
"    <method name='GetEnabled'>"
"      <arg name='enabled' type='b' direction='out'/>"
"    </method>"
"    <method name='SetEnabled'>"
"      <arg name='enabled' type='b' direction='in'/>"
"    </method>"
"    <method name='GetMinimumBrightness'>"
"      <arg name='min' type='d' direction='out'/>"
"    </method>"
"    <method name='SetMinimumBrightness'>"
"      <arg name='min' type='d' direction='in'/>"
"    </method>"
"    <method name='GetTransitionMs'>"
"      <arg name='ms' type='i' direction='out'/>"
"    </method>"
"    <method name='SetTransitionMs'>"
"      <arg name='ms' type='i' direction='in'/>"
"    </method>"
"    <method name='GetStatus'>"
"      <arg name='status' type='s' direction='out'/>"
"      <arg name='enabled' type='b' direction='out'/>"
"      <arg name='brightness' type='d' direction='out'/>"
"      <arg name='monitor_count' type='i' direction='out'/>"
"    </method>"
"    <signal name='BrightnessChanged'>"
"      <arg name='monitor_index' type='i'/>"
"      <arg name='brightness' type='d'/>"
"    </signal>"
"    <signal name='EnabledChanged'>"
"      <arg name='enabled' type='b'/>"
"    </signal>"
"    <signal name='StatusChanged'>"
"      <arg name='status' type='s'/>"
"    </signal>"
"  </interface>"
"</node>";

struct AccSdSoftDimmerService {
    AccSdBrightnessEngine *engine; /* borrowed */
    guint                  name_id;
    GDBusConnection       *connection;
    guint                  registration_id;
};

static GDBusInterfaceVTable vtable;

/* ------------------------------------------------------------------ helpers */
static const char *
status_to_string (AccSdStatus st)
{
    switch (st)
    {
    case ACC_SD_STATUS_STOPPED:  return "stopped";
    case ACC_SD_STATUS_RUNNING:  return "running";
    case ACC_SD_STATUS_ACTIVE:   return "active";
    case ACC_SD_STATUS_ERROR:    return "error";
    default:                     return "unknown";
    }
}

/* ------------------------------------------------------------------ method calls */
static void
on_method_call (GDBusConnection        *connection,
                const gchar            *sender,
                const gchar            *object_path,
                const gchar            *interface_name,
                const gchar            *method_name,
                GVariant               *parameters,
                GDBusMethodInvocation  *invocation,
                gpointer                user_data)
{
    (void) connection;
    (void) sender;
    (void) object_path;
    (void) interface_name;
    AccSdSoftDimmerService *svc = (AccSdSoftDimmerService *) user_data;
    AccSdBrightnessEngine *e = svc->engine;

    if (g_strcmp0 (method_name, "GetBrightness") == 0)
    {
        gint idx;
        g_variant_get (parameters, "(i)", &idx);
        gdouble b = acc_sd_engine_get_brightness (e, idx);
        g_dbus_method_invocation_return_value (invocation,
            g_variant_new ("(d)", b));
        return;
    }
    if (g_strcmp0 (method_name, "SetBrightness") == 0)
    {
        gint idx;
        gdouble b;
        g_variant_get (parameters, "(id)", &idx, &b);
        /* Validate. */
        if (idx < -1)
        {
            g_dbus_method_invocation_return_error (invocation,
                G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                "monitor_index must be >= -1");
            return;
        }
        gdouble minb = acc_sd_engine_get_minimum_brightness (e);
        if (b < minb || b > 1.0)
        {
            g_dbus_method_invocation_return_error (invocation,
                G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                "brightness must be in [min, 1.0]");
            return;
        }
        acc_sd_engine_set_brightness (e, idx, b);
        g_dbus_method_invocation_return_value (invocation, NULL);
        return;
    }
    if (g_strcmp0 (method_name, "GetEnabled") == 0)
    {
        gboolean en = acc_sd_engine_get_enabled (e);
        g_dbus_method_invocation_return_value (invocation,
            g_variant_new ("(b)", en));
        return;
    }
    if (g_strcmp0 (method_name, "SetEnabled") == 0)
    {
        gboolean en;
        g_variant_get (parameters, "(b)", &en);
        acc_sd_engine_set_enabled (e, en);
        g_dbus_method_invocation_return_value (invocation, NULL);
        return;
    }
    if (g_strcmp0 (method_name, "GetMinimumBrightness") == 0)
    {
        gdouble minb = acc_sd_engine_get_minimum_brightness (e);
        g_dbus_method_invocation_return_value (invocation,
            g_variant_new ("(d)", minb));
        return;
    }
    if (g_strcmp0 (method_name, "SetMinimumBrightness") == 0)
    {
        gdouble minb;
        g_variant_get (parameters, "(d)", &minb);
        acc_sd_engine_set_minimum_brightness (e, minb);
        g_dbus_method_invocation_return_value (invocation, NULL);
        return;
    }
    if (g_strcmp0 (method_name, "GetTransitionMs") == 0)
    {
        gint ms = acc_sd_engine_get_transition_ms (e);
        g_dbus_method_invocation_return_value (invocation,
            g_variant_new ("(i)", ms));
        return;
    }
    if (g_strcmp0 (method_name, "SetTransitionMs") == 0)
    {
        gint ms;
        g_variant_get (parameters, "(i)", &ms);
        acc_sd_engine_set_transition_ms (e, ms);
        g_dbus_method_invocation_return_value (invocation, NULL);
        return;
    }
    if (g_strcmp0 (method_name, "GetStatus") == 0)
    {
        gboolean en = acc_sd_engine_get_enabled (e);
        gdouble b = acc_sd_engine_get_brightness (e, -1);
        g_dbus_method_invocation_return_value (invocation,
            g_variant_new ("(sbdi)", status_to_string (ACC_SD_STATUS_RUNNING),
                           en, b, 0));
        return;
    }

    /* Unknown method. */
    g_dbus_method_invocation_return_error (invocation,
        G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
        "Unknown method: %s", method_name);
}

/* ------------------------------------------------------------------ export / unexport */
static void
on_bus_acquired (GDBusConnection *connection, const gchar *name,
                 gpointer user_data)
{
    (void) name;
    AccSdSoftDimmerService *svc = (AccSdSoftDimmerService *) user_data;
    GError *error = NULL;

    static const gchar *introspection = introspection_xml;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml (introspection, &error);
    if (!info)
    {
        g_clear_error (&error);
        return;
    }

    memset (&vtable, 0, sizeof (vtable));
    vtable.method_call = on_method_call;

    svc->connection = g_object_ref (connection);
    svc->registration_id = g_dbus_connection_register_object
        (connection, ACC_SD_DBUS_OBJECT_PATH,
         info->interfaces[0], &vtable, svc, NULL, &error);
    g_clear_error (&error);
    g_dbus_node_info_unref (info);
}

static void
on_name_acquired (GDBusConnection *connection, const gchar *name,
                  gpointer user_data)
{
    /* The name is ours; on_bus_acquired already registered the object. */
    (void) connection; (void) name; (void) user_data;
}

static void
on_name_lost (GDBusConnection *connection, const gchar *name,
              gpointer user_data)
{
    AccSdSoftDimmerService *svc = (AccSdSoftDimmerService *) user_data;
    (void) connection;
    svc->name_id = 0;
    svc->connection = NULL;
    g_printerr ("soft-dimmer: lost D-Bus name %s\n", name);
}

AccSdSoftDimmerService *
acc_sd_service_new (AccSdBrightnessEngine *engine)
{
    AccSdSoftDimmerService *svc = g_new0 (AccSdSoftDimmerService, 1);
    svc->engine = engine;
    svc->name_id = 0;
    svc->connection = NULL;
    svc->registration_id = 0;

    svc->name_id = g_bus_own_name (G_BUS_TYPE_SESSION,
        ACC_SD_DBUS_SERVICE,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired,
        on_name_acquired,
        on_name_lost,
        svc,
        NULL);
    return svc;
}

void
acc_sd_service_free (AccSdSoftDimmerService *svc)
{
    if (!svc)
        return;
    if (svc->registration_id && svc->connection)
        g_dbus_connection_unregister_object (svc->connection,
                                             svc->registration_id);
    if (svc->name_id)
        g_bus_unown_name (svc->name_id);
    if (svc->connection)
        g_object_unref (svc->connection);
    g_free (svc);
}

gboolean
acc_sd_service_is_exported (AccSdSoftDimmerService *svc)
{
    return svc->name_id != 0 && svc->connection != NULL;
}

void
acc_sd_service_notify_brightness (AccSdSoftDimmerService *svc,
                                  gint                    monitor_index,
                                  gdouble                 brightness)
{
    if (!svc->connection)
        return;
    GVariant *params = g_variant_new ("(id)", monitor_index, brightness);
    g_dbus_connection_emit_signal (svc->connection, NULL,
        ACC_SD_DBUS_OBJECT_PATH,
        ACC_SD_DBUS_INTERFACE,
        "BrightnessChanged",
        params,
        NULL);
    g_variant_unref (params);
}

void
acc_sd_service_notify_enabled (AccSdSoftDimmerService *svc,
                               gboolean                enabled)
{
    if (!svc->connection)
        return;
    GVariant *params = g_variant_new ("(b)", enabled);
    g_dbus_connection_emit_signal (svc->connection, NULL,
        ACC_SD_DBUS_OBJECT_PATH,
        ACC_SD_DBUS_INTERFACE,
        "EnabledChanged",
        params,
        NULL);
    g_variant_unref (params);
}

void
acc_sd_service_notify_status (AccSdSoftDimmerService *svc)
{
    if (!svc->connection)
        return;
    GVariant *params = g_variant_new ("(s)", status_to_string (ACC_SD_STATUS_RUNNING));
    g_dbus_connection_emit_signal (svc->connection, NULL,
        ACC_SD_DBUS_OBJECT_PATH,
        ACC_SD_DBUS_INTERFACE,
        "StatusChanged",
        params,
        NULL);
    g_variant_unref (params);
}
