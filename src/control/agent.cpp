/* control/agent.cpp - the client for the ASUSCraft host agent
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The app is a CLIENT only: it never owns a bus name (AGENTS.md). It asks
 * the bus whether the host agent is there, and if it is, it calls it.
 */
#include "agent.h"
#include "agentproto.h"

#include <gio/gio.h>
#include <string.h>

/* how long a plain call may take */
#define AGENT_TIMEOUT_MS      8000
/* how long a privileged call may take (the polkit dialog waits for a
 * human, so this one is generous) */
#define AGENT_PRIV_TIMEOUT_MS 180000

static AccAgentState s_state = ACC_AGENT_ABSENT;
static gboolean      s_probed = FALSE;
static GDBusProxy  *s_proxy = NULL;
static gchar       *s_version = NULL;
static gchar       *s_caps = NULL;
static gchar       *s_desktop = NULL;
static gchar       *s_err = NULL;

gboolean
acc_agent_sandboxed (void)
{
  const gchar *id = g_getenv ("FLATPAK_ID");
  return id && *id;
}

const gchar *
acc_agent_version (void)
{
  return s_version ? s_version : "";
}

const gchar *
acc_agent_capabilities (void)
{
  return s_caps ? s_caps : "";
}

const gchar *
acc_agent_desktop (void)
{
  return s_desktop ? s_desktop : "";
}

const gchar *
acc_agent_last_error (void)
{
  return s_err ? s_err : "";
}

static void
set_error (const gchar *msg)
{
  g_free (s_err);
  s_err = g_strdup (msg ? msg : "");
}

/* Build the proxy. The default flags let the bus start the agent if it is
 * installed but not running yet (that is what D-Bus activation is for). */
static GDBusProxy *
build_proxy (void)
{
  if (s_proxy)
    return s_proxy;

  GError *e = NULL;
  s_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                           G_DBUS_PROXY_FLAGS_NONE,
                                           NULL,
                                           ACC_AGENT_BUS_NAME,
                                           ACC_AGENT_PATH,
                                           ACC_AGENT_IFACE,
                                           NULL, &e);
  if (!s_proxy)
    {
      set_error (e ? e->message : "the host agent is not installed");
      g_clear_error (&e);
      return NULL;
    }
  return s_proxy;
}

static void
drop_proxy (void)
{
  if (s_proxy)
    {
      g_object_unref (s_proxy);
      s_proxy = NULL;
    }
}

AccAgentState
acc_agent_state (void)
{
  if (s_probed)
    return s_state;

  s_probed = TRUE;
  s_state = ACC_AGENT_ABSENT;

  GError *e = NULL;
  GDBusConnection *bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &e);
  if (!bus)
    {
      set_error (e ? e->message : "no session bus");
      g_clear_error (&e);
      return s_state;
    }

  /* 1. is it already running? */
  GVariant *r = g_dbus_connection_call_sync (bus, "org.freedesktop.DBus",
                                              "/org/freedesktop/DBus",
                                              "org.freedesktop.DBus",
                                              "NameHasOwner",
                                              g_variant_new ("(s)",
                                                             ACC_AGENT_BUS_NAME),
                                              G_VARIANT_TYPE ("(b)"),
                                              G_DBUS_CALL_FLAGS_NONE,
                                              2000, NULL, &e);
  gboolean owned = FALSE;
  if (r)
    {
      g_variant_get (r, "(b)", &owned);
      g_variant_unref (r);
    }
  g_clear_error (&e);

  /* 2. if not, ask the bus to activate it (the agent is activatable) */
  if (!owned)
    {
      e = NULL;
      r = g_dbus_connection_call_sync (bus, "org.freedesktop.DBus",
                                       "/org/freedesktop/DBus",
                                       "org.freedesktop.DBus",
                                       "StartServiceByName",
                                       g_variant_new ("(su)",
                                                      ACC_AGENT_BUS_NAME, 0),
                                       G_VARIANT_TYPE ("(u)"),
                                       G_DBUS_CALL_FLAGS_NONE,
                                       5000, NULL, &e);
      if (r)
        {
          guint32 reply = 0;
          g_variant_get (r, "(u)", &reply);
          g_variant_unref (r);
          owned = (reply == 1 || reply == 2);   /* running | started */
        }
      else
        {
          set_error (e ? e->message
                       : "the ASUSCraft host agent is not installed");
          g_clear_error (&e);
        }
    }
  g_object_unref (bus);

  if (!owned)
    {
      s_state = ACC_AGENT_ABSENT;
      return s_state;
    }

  /* 3. it is there: ask it who it is */
  GDBusProxy *proxy = build_proxy ();
  if (!proxy)
    {
      s_state = ACC_AGENT_ABSENT;
      return s_state;
    }
  if (!g_dbus_proxy_get_name_owner (proxy))
    {
      drop_proxy ();
      s_state = ACC_AGENT_ABSENT;
      return s_state;
    }

  e = NULL;
  GVariant *info = g_dbus_proxy_call_sync (proxy, "GetInfo", NULL,
                                           G_DBUS_CALL_FLAGS_NONE,
                                           AGENT_TIMEOUT_MS, NULL, &e);
  if (!info)
    {
      set_error (e ? e->message : "the host agent did not answer");
      g_clear_error (&e);
      s_state = ACC_AGENT_READY;
      return s_state;
    }

  const gchar *v = NULL, *c = NULL, *d = NULL;
  g_variant_get (info, "(&s&s&s)", &v, &c, &d);
  g_free (s_version);
  g_free (s_caps);
  g_free (s_desktop);
  s_version = g_strdup (v ? v : "");
  s_caps = g_strdup (c ? c : "");
  s_desktop = g_strdup (d ? d : "");
  g_variant_unref (info);

  set_error ("");
  s_state = ACC_AGENT_CONNECTED;
  return s_state;
}

void
acc_agent_reset (void)
{
  drop_proxy ();
  s_probed = FALSE;
  s_state = ACC_AGENT_ABSENT;
}

/* ------------------------------------------------------------- the calls */

gboolean
acc_agent_run (AccCommand id, const gchar *arg,
               gchar **out, gint *exit_code, gchar **err)
{
  if (out)
    *out = NULL;
  if (exit_code)
    *exit_code = -1;
  if (err)
    *err = NULL;

  if (acc_agent_state () != ACC_AGENT_CONNECTED)
    return FALSE;

  GDBusProxy *proxy = build_proxy ();
  if (!proxy)
    return FALSE;

  GError *e = NULL;
  GVariant *r = g_dbus_proxy_call_sync (proxy, "RunCommand",
                                        g_variant_new ("(ss)",
                                                       acc_command_id (id),
                                                       arg ? arg : ""),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        AGENT_TIMEOUT_MS, NULL, &e);
  if (!r)
    {
      if (err)
        *err = g_strdup (e ? e->message : "the host agent did not answer");
      g_clear_error (&e);
      return FALSE;
    }

  gint code = -1;
  const gchar *o = NULL, *er = NULL;
  g_variant_get (r, "(i&s&s)", &code, &o, &er);
  if (out)
    *out = g_strdup (o ? o : "");
  if (exit_code)
    *exit_code = code;
  if (err)
    *err = g_strdup (er ? er : "");
  g_variant_unref (r);

  /* -2 means the agent does not know this command (an older agent): the
   * caller may still run it locally. */
  if (code == -2)
    {
      if (out)
        *out = NULL;
      return FALSE;
    }
  return TRUE;
}

gboolean
acc_agent_set_hardware (const gchar *action, const gchar *value,
                        gint *exit_code, gchar **err)
{
  if (exit_code)
    *exit_code = -1;
  if (err)
    *err = NULL;

  if (acc_agent_state () != ACC_AGENT_CONNECTED)
    return FALSE;

  GDBusProxy *proxy = build_proxy ();
  if (!proxy)
    return FALSE;

  GError *e = NULL;
  GVariant *r = g_dbus_proxy_call_sync (proxy, "SetHardware",
                                        g_variant_new ("(ss)", action, value),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        AGENT_PRIV_TIMEOUT_MS, NULL, &e);
  if (!r)
    {
      if (err)
        *err = g_strdup (e ? e->message : "the host agent did not answer");
      g_clear_error (&e);
      return FALSE;
    }

  gint code = -1;
  const gchar *er = NULL;
  g_variant_get (r, "(i&s)", &code, &er);
  if (exit_code)
    *exit_code = code;
  if (err)
    *err = g_strdup (er ? er : "");
  g_variant_unref (r);
  return TRUE;
}

gboolean
acc_agent_check_admin (gint *exit_code)
{
  if (exit_code)
    *exit_code = -1;

  if (acc_agent_state () != ACC_AGENT_CONNECTED)
    return FALSE;

  GDBusProxy *proxy = build_proxy ();
  if (!proxy)
    return FALSE;

  GError *e = NULL;
  GVariant *r = g_dbus_proxy_call_sync (proxy, "CheckAdmin", NULL,
                                        G_DBUS_CALL_FLAGS_NONE,
                                        AGENT_PRIV_TIMEOUT_MS, NULL, &e);
  if (!r)
    {
      g_clear_error (&e);
      return FALSE;
    }
  gint code = -1;
  g_variant_get (r, "(i)", &code);
  g_variant_unref (r);
  if (exit_code)
    *exit_code = code;
  return TRUE;
}

gboolean
acc_agent_softbrightness (const gchar *op, gchar **result, gchar **err)
{
  if (result)
    *result = NULL;
  if (err)
    *err = NULL;

  if (acc_agent_state () != ACC_AGENT_CONNECTED)
    return FALSE;

  GDBusProxy *proxy = build_proxy ();
  if (!proxy)
    return FALSE;

  GError *e = NULL;
  GVariant *r = g_dbus_proxy_call_sync (proxy, "SoftBrightness",
                                        g_variant_new ("(s)", op),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        AGENT_TIMEOUT_MS, NULL, &e);
  if (!r)
    {
      if (err)
        *err = g_strdup (e ? e->message : "the host agent did not answer");
      g_clear_error (&e);
      return FALSE;
    }

  gint code = -1;
  const gchar *res = NULL, *er = NULL;
  g_variant_get (r, "(i&s&s)", &code, &res, &er);
  if (result)
    *result = g_strdup (res ? res : "");
  if (err)
    *err = g_strdup (er ? er : "");
  g_variant_unref (r);
  return code == 0;
}

gboolean
acc_agent_nightlight_get (gchar **text, gchar **err)
{
  if (text)
    *text = NULL;
  if (err)
    *err = NULL;

  if (acc_agent_state () != ACC_AGENT_CONNECTED)
    return FALSE;

  GDBusProxy *proxy = build_proxy ();
  if (!proxy)
    return FALSE;

  GError *e = NULL;
  GVariant *r = g_dbus_proxy_call_sync (proxy, "NightLightGet", NULL,
                                        G_DBUS_CALL_FLAGS_NONE,
                                        AGENT_TIMEOUT_MS, NULL, &e);
  if (!r)
    {
      if (err)
        *err = g_strdup (e ? e->message : "the host agent did not answer");
      g_clear_error (&e);
      return FALSE;
    }
  const gchar *t = NULL;
  g_variant_get (r, "(&s)", &t);
  if (text)
    *text = g_strdup (t ? t : "");
  g_variant_unref (r);
  return TRUE;
}

gboolean
acc_agent_nightlight_set (const gchar *key, const gchar *value, gchar **err)
{
  if (err)
    *err = NULL;

  if (acc_agent_state () != ACC_AGENT_CONNECTED)
    return FALSE;

  GDBusProxy *proxy = build_proxy ();
  if (!proxy)
    return FALSE;

  GError *e = NULL;
  GVariant *r = g_dbus_proxy_call_sync (proxy, "NightLightSet",
                                        g_variant_new ("(ss)", key, value),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        AGENT_TIMEOUT_MS, NULL, &e);
  if (!r)
    {
      if (err)
        *err = g_strdup (e ? e->message : "the host agent did not answer");
      g_clear_error (&e);
      return FALSE;
    }
  gboolean ok = FALSE;
  const gchar *er = NULL;
  g_variant_get (r, "(b&s)", &ok, &er);
  if (err)
    *err = g_strdup (er ? er : "");
  g_variant_unref (r);
  return ok;
}

gboolean
acc_agent_brightness_get (gchar **value, gchar **err)
{
  if (value)
    *value = NULL;
  if (err)
    *err = NULL;

  if (acc_agent_state () != ACC_AGENT_CONNECTED)
    return FALSE;

  GDBusProxy *proxy = build_proxy ();
  if (!proxy)
    return FALSE;

  GError *e = NULL;
  GVariant *r = g_dbus_proxy_call_sync (proxy, "BrightnessGet", NULL,
                                        G_DBUS_CALL_FLAGS_NONE,
                                        AGENT_TIMEOUT_MS, NULL, &e);
  if (!r)
    {
      if (err)
        *err = g_strdup (e ? e->message : "the host agent did not answer");
      g_clear_error (&e);
      return FALSE;
    }
  const gchar *v = NULL;
  g_variant_get (r, "(&s)", &v);
  if (value)
    *value = g_strdup (v ? v : "");
  g_variant_unref (r);
  return TRUE;
}

gboolean
acc_agent_brightness_set (const gchar *value, gchar **err)
{
  if (err)
    *err = NULL;

  if (acc_agent_state () != ACC_AGENT_CONNECTED)
    return FALSE;

  GDBusProxy *proxy = build_proxy ();
  if (!proxy)
    return FALSE;

  GError *e = NULL;
  GVariant *r = g_dbus_proxy_call_sync (proxy, "BrightnessSet",
                                        g_variant_new ("(s)", value),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        AGENT_TIMEOUT_MS, NULL, &e);
  if (!r)
    {
      if (err)
        *err = g_strdup (e ? e->message : "the host agent did not answer");
      g_clear_error (&e);
      return FALSE;
    }
  gboolean ok = FALSE;
  const gchar *er = NULL;
  g_variant_get (r, "(b&s)", &ok, &er);
  if (err)
    *err = g_strdup (er ? er : "");
  g_variant_unref (r);
  return ok;
}

gboolean
acc_agent_touchpad_get (gchar **state, gchar **err)
{
  if (state)
    *state = NULL;
  if (err)
    *err = NULL;

  if (acc_agent_state () != ACC_AGENT_CONNECTED)
    return FALSE;

  GDBusProxy *proxy = build_proxy ();
  if (!proxy)
    return FALSE;

  GError *e = NULL;
  GVariant *r = g_dbus_proxy_call_sync (proxy, "TouchpadGet", NULL,
                                        G_DBUS_CALL_FLAGS_NONE,
                                        AGENT_TIMEOUT_MS, NULL, &e);
  if (!r)
    {
      if (err)
        *err = g_strdup (e ? e->message : "the host agent did not answer");
      g_clear_error (&e);
      return FALSE;
    }
  const gchar *v = NULL;
  g_variant_get (r, "(&s)", &v);
  if (state)
    *state = g_strdup (v ? v : "unsupported");
  g_variant_unref (r);
  return TRUE;
}

gboolean
acc_agent_touchpad_set (const gchar *state, gchar **err)
{
  if (err)
    *err = NULL;

  if (acc_agent_state () != ACC_AGENT_CONNECTED)
    return FALSE;

  GDBusProxy *proxy = build_proxy ();
  if (!proxy)
    return FALSE;

  GError *e = NULL;
  GVariant *r = g_dbus_proxy_call_sync (proxy, "TouchpadSet",
                                        g_variant_new ("(s)", state),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        AGENT_TIMEOUT_MS, NULL, &e);
  if (!r)
    {
      if (err)
        *err = g_strdup (e ? e->message : "the host agent did not answer");
      g_clear_error (&e);
      return FALSE;
    }
  gboolean ok = FALSE;
  const gchar *er = NULL;
  g_variant_get (r, "(b&s)", &ok, &er);
  if (err)
    *err = g_strdup (er ? er : "");
  g_variant_unref (r);
  return ok;
}
