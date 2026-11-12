/* control/portal.cpp - the XDG desktop portals ASUSCraft needs
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "portal.h"

#include <gio/gio.h>

#define PORTAL_BUS   "org.freedesktop.portal.Desktop"
#define PORTAL_PATH  "/org/freedesktop/portal/desktop"
#define PORTAL_IFACE "org.freedesktop.portal.Software"

gboolean
acc_portal_open_software (gchar **err)
{
  if (err)
    *err = NULL;

  GError *e = NULL;
  GDBusConnection *bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &e);
  if (!bus)
    {
      if (err)
        *err = g_strdup (e ? e->message : "no session bus");
      g_clear_error (&e);
      return FALSE;
    }

  /* 1. ask for the sources the portal should offer */
  GVariantBuilder opts;
  g_variant_builder_init (&opts, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&opts, "{sv}", "source_type", g_variant_new_string ("app"));
  g_variant_builder_add (&opts, "{sv}", "app_ids",
                         g_variant_new_strv (NULL, 0));

  GVariant *r = g_dbus_connection_call_sync (bus, PORTAL_BUS, PORTAL_PATH,
                                              PORTAL_IFACE, "SelectSources",
                                              g_variant_new ("(a{sv})", &opts),
                                              G_VARIANT_TYPE ("(o)"),
                                              G_DBUS_CALL_FLAGS_NONE, 10000,
                                              NULL, &e);
  if (!r)
    {
      if (err)
        *err = g_strdup (e ? e->message : "the software portal did not answer");
      g_clear_error (&e);
      g_object_unref (bus);
      return FALSE;
    }

  const gchar *handle = NULL;
  g_variant_get (r, "(&o)", &handle);

  /* 2. ask the portal to open the software centre with those sources */
  GVariantBuilder empty;
  g_variant_builder_init (&empty, G_VARIANT_TYPE_VARDICT);
  GVariant *r2 = g_dbus_connection_call_sync (bus, PORTAL_BUS, handle,
                                               PORTAL_IFACE, "Software",
                                               g_variant_new ("(a{sv})", &empty),
                                               NULL, G_DBUS_CALL_FLAGS_NONE,
                                               10000, NULL, &e);
  if (!r2)
    {
      if (err)
        *err = g_strdup (e ? e->message : "the software portal refused");
      g_clear_error (&e);
    }
  else
    g_variant_unref (r2);

  g_variant_unref (r);
  g_object_unref (bus);
  return r2 != NULL;
}
