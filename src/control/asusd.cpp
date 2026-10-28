/* control/asusd.cpp - asusd (asusctl) D-Bus client
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * No shell, no command line: a fixed system-bus connection, fixed object
 * paths and property names, and each write is verified by reading the
 * property back. When asusd is absent every function returns FALSE and the
 * caller falls back to the polkit helper.
 */
#include "asusd.h"

#include <gio/gio.h>
#include <string.h>

#define ASUSD_NAME      "xyz.ljones.Asusd"
#define ASUSD_PLATFORM  "/xyz/ljones"
#define IFACE_PLATFORM  "xyz.ljones.Platform"
#define ASUSD_MUX_PATH  "/xyz/ljones/asus_armoury/gpu_mux_mode"
#define IFACE_ARMOURY   "xyz.ljones.AsusArmoury"

#define IFACE_PROPS     "org.freedesktop.DBus.Properties"
#define NAME_DBUS       "org.freedesktop.DBus"
#define PATH_DBUS       "/org/freedesktop/DBus"
#define IFACE_DBUS      "org.freedesktop.DBus"

#define NAME_SYSTEMD    "org.freedesktop.systemd1"
#define PATH_SYSTEMD    "/org/freedesktop/systemd1"
#define IFACE_MANAGER   "org.freedesktop.systemd1.Manager"
#define IFACE_UNIT      "org.freedesktop.systemd1.Unit"

#define ASUS_SHUTDOWN   "asus-shutdown.service"
#define CALL_TIMEOUT_MS 2500

/* The system bus connection is a shared, process-wide singleton. Keeping
 * one reference is intentional; it is released by the process exit. */
static GDBusConnection *s_bus = NULL;

static GDBusConnection *
bus (void)
{
  if (!s_bus)
    s_bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL);
  return s_bus;
}

static GVariant *
call (const gchar *name, const gchar *path, const gchar *iface,
      const gchar *method, GVariant *params, const GVariantType *reply,
      GError **err)
{
  GDBusConnection *c = bus ();
  if (!c)
    {
      g_set_error_literal (err, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "system bus unavailable");
      return NULL;
    }
  return g_dbus_connection_call_sync (c, name, path, iface, method, params,
                                      reply, G_DBUS_CALL_FLAGS_NONE,
                                      CALL_TIMEOUT_MS, NULL, err);
}

/* a{sv} dictionary of one interface, or NULL */
static GVariant *
get_all (const gchar *path, const gchar *iface, GError **err)
{
  GVariant *r = call (ASUSD_NAME, path, IFACE_PROPS, "GetAll",
                      g_variant_new ("(s)", iface),
                      G_VARIANT_TYPE ("(a{sv})"), err);
  if (!r)
    return NULL;
  GVariant *d = g_variant_get_child_value (r, 0);
  g_variant_unref (r);
  return d;
}

static gboolean
set_prop (const gchar *path, const gchar *iface, const gchar *prop,
          GVariant *value, GError **err)
{
  GVariant *r = call (ASUSD_NAME, path, IFACE_PROPS, "Set",
                      g_variant_new ("(ssv)", iface, prop, value),
                      NULL, err);
  if (!r)
    return FALSE;
  g_variant_unref (r);
  return TRUE;
}

/* asusd mixes signed and unsigned properties; accept either */
static gboolean
dict_num (GVariant *dict, const gchar *key, gint *out)
{
  GVariant *v = g_variant_lookup_value (dict, key, NULL);
  if (!v)
    return FALSE;

  gboolean ok = TRUE;
  if (g_variant_is_of_type (v, G_VARIANT_TYPE_UINT32))
    *out = (gint) g_variant_get_uint32 (v);
  else if (g_variant_is_of_type (v, G_VARIANT_TYPE_INT32))
    *out = g_variant_get_int32 (v);
  else if (g_variant_is_of_type (v, G_VARIANT_TYPE_UINT16))
    *out = (gint) g_variant_get_uint16 (v);
  else if (g_variant_is_of_type (v, G_VARIANT_TYPE_UINT64))
    *out = (gint) g_variant_get_uint64 (v);
  else if (g_variant_is_of_type (v, G_VARIANT_TYPE_BYTE))
    *out = (gint) g_variant_get_byte (v);
  else
    ok = FALSE;

  g_variant_unref (v);
  return ok;
}

static gboolean
dict_bool (GVariant *dict, const gchar *key, gboolean *out)
{
  GVariant *v = g_variant_lookup_value (dict, key, G_VARIANT_TYPE_BOOLEAN);
  if (!v)
    return FALSE;
  *out = g_variant_get_boolean (v);
  g_variant_unref (v);
  return TRUE;
}

/* asusd wire enum: Balanced=0, Performance=1, Quiet=2, LowPower=3 */
static AccProfile
profile_from_wire (gint v)
{
  switch (v)
    {
    case 0: return ACC_PROF_BALANCED;
    case 1: return ACC_PROF_PERFORMANCE;
    case 2: return ACC_PROF_QUIET;
    case 3: return ACC_PROF_LOW_POWER;
    default: return ACC_PROF_UNKNOWN;
    }
}

static gint
profile_to_wire (AccProfile p)
{
  switch (p)
    {
    case ACC_PROF_BALANCED:    return 0;
    case ACC_PROF_PERFORMANCE: return 1;
    case ACC_PROF_QUIET:       return 2;
    case ACC_PROF_LOW_POWER:   return 3;
    default:                   return -1;
    }
}

const gchar *
acc_profile_key (AccProfile p)
{
  switch (p)
    {
    case ACC_PROF_QUIET:       return "quiet";
    case ACC_PROF_BALANCED:    return "balanced";
    case ACC_PROF_PERFORMANCE: return "performance";
    default:                   return NULL;
    }
}

static void
read_choices (GVariant *dict, AsusdProfileState *o)
{
  GVariant *v = g_variant_lookup_value (dict, "PlatformProfileChoices",
                                        G_VARIANT_TYPE ("au"));
  if (!v)
    return;

  o->has_choices = TRUE;
  gsize n = g_variant_n_children (v);
  for (gsize i = 0; i < n; i++)
    {
      guint32 w = 0;
      g_variant_get_child (v, i, "u", &w);
      switch (profile_from_wire ((gint) w))
        {
        case ACC_PROF_QUIET:       o->choice_quiet = TRUE;       break;
        case ACC_PROF_BALANCED:    o->choice_balanced = TRUE;    break;
        case ACC_PROF_PERFORMANCE: o->choice_performance = TRUE; break;
        case ACC_PROF_LOW_POWER:   o->choice_low_power = TRUE;   break;
        default: break;
        }
    }
  g_variant_unref (v);
}

gboolean
acc_asusd_available (void)
{
  GError *e = NULL;
  GVariant *r = call (NAME_DBUS, PATH_DBUS, IFACE_DBUS, "NameHasOwner",
                      g_variant_new ("(s)", ASUSD_NAME),
                      G_VARIANT_TYPE ("(b)"), &e);
  if (!r)
    {
      g_clear_error (&e);
      return FALSE;
    }
  gboolean owned = FALSE;
  g_variant_get (r, "(b)", &owned);
  g_variant_unref (r);
  return owned;
}

gboolean
acc_asusd_profiles (AsusdProfileState *out)
{
  if (!out)
    return FALSE;

  memset (out, 0, sizeof *out);
  out->active = out->on_ac = out->on_bat = ACC_PROF_UNKNOWN;

  GError *e = NULL;
  GVariant *d = get_all (ASUSD_PLATFORM, IFACE_PLATFORM, &e);
  if (!d)
    {
      g_clear_error (&e);
      return FALSE;
    }

  out->available = TRUE;
  gint v = 0;
  if (dict_num (d, "PlatformProfile", &v))
    out->active = profile_from_wire (v);
  if (dict_num (d, "PlatformProfileOnAc", &v))
    out->on_ac = profile_from_wire (v);
  if (dict_num (d, "PlatformProfileOnBattery", &v))
    out->on_bat = profile_from_wire (v);
  dict_bool (d, "ChangePlatformProfileOnAc", &out->change_on_ac);
  dict_bool (d, "ChangePlatformProfileOnBattery", &out->change_on_bat);
  read_choices (d, out);

  g_variant_unref (d);
  return TRUE;
}

gboolean
acc_asusd_set_profile (AccProfile p, gchar **err)
{
  if (err)
    *err = NULL;

  gint wire = profile_to_wire (p);
  if (wire < 0)
    {
      if (err)
        *err = g_strdup ("unsupported profile");
      return FALSE;
    }

  GError *e = NULL;
  if (!set_prop (ASUSD_PLATFORM, IFACE_PLATFORM, "PlatformProfile",
                 g_variant_new_uint32 ((guint32) wire), &e))
    {
      if (err)
        *err = g_strdup (e && e->message ? e->message
                                         : "asusd rejected the profile");
      g_clear_error (&e);
      return FALSE;
    }

  /* Never claim success on a request: asusd applies the write on its own
   * task, so poll briefly (up to ~300 ms) and insist on the read-back. */
  for (int i = 0; i < 6; i++)
    {
      AsusdProfileState st;
      if (acc_asusd_profiles (&st) && st.active == p)
        return TRUE;
      g_usleep (50 * 1000);
    }

  if (err)
    *err = g_strdup ("profile did not change");
  return FALSE;
}

/* TRUE when asus-shutdown.service is enabled or currently active, i.e.
 * when asusd's queued GPU value will actually be applied at shutdown. */
static gboolean
shutdown_handler_ready (void)
{
  GError *e = NULL;
  gboolean ready = FALSE;

  GVariant *r = call (NAME_SYSTEMD, PATH_SYSTEMD, IFACE_MANAGER,
                      "GetUnitFileState", g_variant_new ("(s)", ASUS_SHUTDOWN),
                      G_VARIANT_TYPE ("(s)"), &e);
  if (r)
    {
      const gchar *st = NULL;
      g_variant_get (r, "(&s)", &st);
      ready = g_strcmp0 (st, "enabled") == 0
              || g_strcmp0 (st, "enabled-runtime") == 0;
      g_variant_unref (r);
    }
  g_clear_error (&e);
  if (ready)
    return TRUE;

  /* enabled is the persistent answer; also accept "started with --now" */
  r = call (NAME_SYSTEMD, PATH_SYSTEMD, IFACE_MANAGER, "GetUnit",
            g_variant_new ("(s)", ASUS_SHUTDOWN),
            G_VARIANT_TYPE ("(o)"), &e);
  if (!r)
    {
      g_clear_error (&e);
      return FALSE;
    }

  const gchar *obj = NULL;
  g_variant_get (r, "(&o)", &obj);
  gchar *path = g_strdup (obj);
  g_variant_unref (r);

  GVariant *v = call (NAME_SYSTEMD, path, IFACE_PROPS, "Get",
                      g_variant_new ("(ss)", IFACE_UNIT, "ActiveState"),
                      G_VARIANT_TYPE ("(v)"), &e);
  g_free (path);
  if (v)
    {
      GVariant *inner = NULL;
      g_variant_get (v, "(v)", &inner);
      const gchar *st = g_variant_get_string (inner, NULL);
      ready = g_strcmp0 (st, "active") == 0;
      g_variant_unref (inner);
      g_variant_unref (v);
    }
  g_clear_error (&e);
  return ready;
}

static void
read_possible_mux (GVariant *dict, AsusdGpuState *o)
{
  GVariant *v = g_variant_lookup_value (dict, "PossibleValues",
                                        G_VARIANT_TYPE ("ai"));
  if (!v)
    return;
  gsize n = g_variant_n_children (v);
  for (gsize i = 0; i < n; i++)
    {
      gint32 w = -1;
      g_variant_get_child (v, i, "i", &w);
      if (w == 0)
        o->allow_ultimate = TRUE;
      else if (w == 1)
        o->allow_hybrid = TRUE;
    }
  g_variant_unref (v);
}

gboolean
acc_asusd_gpu (AsusdGpuState *out)
{
  if (!out)
    return FALSE;

  memset (out, 0, sizeof *out);
  out->current = -1;
  out->queued = -1;

  GError *e = NULL;
  GVariant *d = get_all (ASUSD_MUX_PATH, IFACE_ARMOURY, &e);
  if (!d)
    {
      g_clear_error (&e);
      return FALSE;
    }

  out->available = TRUE;
  dict_num (d, "CurrentValue", &out->current);
  dict_num (d, "QueuedGpuValue", &out->queued);
  read_possible_mux (d, out);
  g_variant_unref (d);

  out->shutdown_ready = shutdown_handler_ready ();
  return TRUE;
}

gboolean
acc_asusd_set_gpu_mux (gint mode, gchar **err)
{
  if (err)
    *err = NULL;

  if (mode != 0 && mode != 1)
    {
      if (err)
        *err = g_strdup ("unsupported MUX value");
      return FALSE;
    }

  GError *e = NULL;
  if (!set_prop (ASUSD_MUX_PATH, IFACE_ARMOURY, "CurrentValue",
                 g_variant_new_int32 (mode), &e))
    {
      if (err)
        *err = g_strdup (e && e->message ? e->message
                                         : "asusd rejected the MUX value");
      g_clear_error (&e);
      return FALSE;
    }

  /* asusd acknowledges the queue, not the write: confirm the queue only */
  for (int i = 0; i < 6; i++)
    {
      AsusdGpuState st;
      if (acc_asusd_gpu (&st) && st.queued == mode)
        return TRUE;
      g_usleep (50 * 1000);
    }

  if (err)
    *err = g_strdup ("asusd did not keep the queued value");
  return FALSE;
}
