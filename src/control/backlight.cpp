/* control/backlight.cpp - hardware panel backlight (see backlight.h)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Security notes:
 *  - the device name comes from /sys/class/backlight and must be a plain
 *    file name; logind checks the session and the subsystem itself
 *  - one fixed D-Bus method on the system bus, no shell, no pkexec/sudo
 */
#include "backlight.h"
#include "../probe/probe.h"

#include <gio/gio.h>
#include <string.h>

#define BL_DIR      "/sys/class/backlight"
#define LOGIND      "org.freedesktop.login1"
#define SESSION     "/org/freedesktop/login1/session/auto"
#define SESSION_IF  "org.freedesktop.login1.Session"
#define MIN_PCT     1.0
#define MAX_PCT     100.0

static gboolean s_busy = FALSE;    /* a SetBrightness call is in flight */
static gint     s_next = -1;       /* raw value queued behind it        */

/* first backlight device, same choice as the display probe */
static gchar *
device (gint *max)
{
  GDir *d = g_dir_open (BL_DIR, 0, NULL);
  if (!d)
    return NULL;
  gchar *ret = NULL;
  const gchar *n;
  while (!ret && (n = g_dir_read_name (d)) != NULL)
    {
      if (strchr (n, '/') || n[0] == '.')
        continue;
      gchar *p = g_build_filename (BL_DIR, n, "max_brightness", NULL);
      gint m = acc_read_int (p, -1);
      g_free (p);
      if (m > 0)
        {
          ret = g_strdup (n);
          if (max)
            *max = m;
        }
    }
  g_dir_close (d);
  return ret;
}

gboolean
acc_backlight_available (void)
{
  gchar *n = device (NULL);
  g_free (n);
  return n != NULL;
}

gdouble
acc_backlight_get (void)
{
  gint max = 0;
  gchar *n = device (&max);
  if (!n)
    return -1;
  gchar *p = g_build_filename (BL_DIR, n, "brightness", NULL);
  gint now = acc_read_int (p, -1);
  g_free (p);
  g_free (n);
  return now < 0 ? -1 : now * 100.0 / max;
}

static void send_raw (gint raw);

static void
set_done (GObject *src, GAsyncResult *res, gpointer data)
{
  (void) data;
  GError *e = NULL;
  GVariant *r = g_dbus_connection_call_finish (G_DBUS_CONNECTION (src), res,
                                               &e);
  if (r)
    g_variant_unref (r);
  else
    {
      g_printerr ("backlight: %s\n", e->message);
      g_clear_error (&e);
    }

  s_busy = FALSE;
  if (s_next >= 0)
    {
      gint raw = s_next;
      s_next = -1;
      send_raw (raw);
    }
}

static void
send_raw (gint raw)
{
  gchar *n = device (NULL);
  GDBusConnection *bus = n ? g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL)
                           : NULL;
  if (!bus)
    {
      g_free (n);
      return;
    }
  s_busy = TRUE;
  g_dbus_connection_call (bus, LOGIND, SESSION, SESSION_IF, "SetBrightness",
                          g_variant_new ("(ssu)", "backlight", n,
                                         (guint32) raw),
                          NULL, G_DBUS_CALL_FLAGS_NONE, 3000, NULL,
                          set_done, NULL);
  g_object_unref (bus);
  g_free (n);
}

void
acc_backlight_set (gdouble pct)
{
  gint max = 0;
  gchar *n = device (&max);
  if (!n)
    return;
  g_free (n);
  if (pct < MIN_PCT)
    pct = MIN_PCT;
  if (pct > MAX_PCT)
    pct = MAX_PCT;
  gint raw = (gint) (pct * max / 100.0 + 0.5);
  if (raw < 1)
    raw = 1;

  /* slider drags fire many changes: keep only the latest while busy */
  if (s_busy)
    s_next = raw;
  else
    send_raw (raw);
}
