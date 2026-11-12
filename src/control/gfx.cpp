/* control/gfx.cpp - read-only dGPU power probe (see gfx.h)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "gfx.h"
#include "hostcmd.h"
#include "../probe/probe.h"
#include "../probe/devs.h"

#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SUPERGFX_NAME     "org.supergfxctl.Daemon"
#define SUPERGFX_CLI      "/usr/bin/supergfxctl"
#define SUPERGFX_MODPROBE "/etc/modprobe.d/supergfxd.conf"
#define HOLDERS_US        (10 * G_USEC_PER_SEC)

/* sysfs directory of the first NVIDIA display controller, or NULL */
static gchar *
nvidia_pci_dir (void)
{
  GDir *d = g_dir_open ("/sys/bus/pci/devices", 0, NULL);
  if (!d)
    return NULL;
  gchar *found = NULL;
  const gchar *n;
  while (!found && (n = g_dir_read_name (d)))
    {
      gchar *base = g_build_filename ("/sys/bus/pci/devices", n, NULL);
      gchar *vp = g_build_filename (base, "vendor", NULL);
      gchar *cp = g_build_filename (base, "class", NULL);
      gchar *v = acc_read_trim (vp);
      gchar *c = acc_read_trim (cp);
      /* class 0x03xxxx = display controller */
      if (g_strcmp0 (v, "0x10de") == 0 && c && g_str_has_prefix (c, "0x03"))
        found = g_strdup (base);
      g_free (v);
      g_free (c);
      g_free (vp);
      g_free (cp);
      g_free (base);
    }
  g_dir_close (d);
  return found;
}

static gboolean
supergfxd_owned (void)
{
  /* A sandbox must not be given the supergfxctl bus name: the agent
   * answers the same question with systemctl. */
  if (acc_host_agent_ready ())
    {
      gchar *out = NULL, *err = NULL;
      gint code = -1;
      AccHostHow how = ACC_HOST_UNAVAILABLE;
      if (acc_host_run (ACC_CMD_SERVICE_ACTIVE, "supergfxd", &out, &code,
                        &how, &err))
        {
          gboolean active = g_strcmp0 (g_strstrip (out), "active") == 0;
          g_free (out);
          g_free (err);
          return active;
        }
      g_free (out);
      g_free (err);
      return FALSE;
    }

  GDBusConnection *bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL);
  if (!bus)
    return FALSE;
  GVariant *r = g_dbus_connection_call_sync
    (bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
     "org.freedesktop.DBus", "NameHasOwner",
     g_variant_new ("(s)", SUPERGFX_NAME), G_VARIANT_TYPE ("(b)"),
     G_DBUS_CALL_FLAGS_NONE, 800, NULL, NULL);
  gboolean owned = FALSE;
  if (r)
    {
      g_variant_get (r, "(b)", &owned);
      g_variant_unref (r);
    }
  g_object_unref (bus);
  return owned;
}

gboolean
acc_gfx_text_blacklists_nvidia (const gchar *text)
{
  if (!text)
    return FALSE;
  gboolean hit = FALSE;
  gchar **lines = g_strsplit (text, "\n", -1);
  for (gint i = 0; lines[i] && !hit; i++)
    {
      gchar **w = g_strsplit_set (g_strstrip (lines[i]), " \t", -1);
      gint n = 0;
      gchar *word[2] = { NULL, NULL };
      for (gint k = 0; w[k] && n < 2; k++)
        if (*w[k])
          word[n++] = w[k];
      hit = n == 2 && g_strcmp0 (word[0], "blacklist") == 0
            && g_strcmp0 (word[1], "nvidia") == 0;
      g_strfreev (w);
    }
  g_strfreev (lines);
  return hit;
}

/* Comma list of this user's processes holding /dev/nvidia<N> open (the
 * nodes that keep the dGPU out of runtime D3). Other users' fds are not
 * readable, which is fine: the list is a hint, never a decision input. */
static void
scan_holders (gchar *out, gsize len)
{
  out[0] = '\0';
  GDir *proc = g_dir_open ("/proc", 0, NULL);
  if (!proc)
    return;
  GString *s = g_string_new (NULL);
  const gchar *pid;
  while ((pid = g_dir_read_name (proc)))
    {
      /* skip non-pids and ASUSCraft itself (its NVML probe opens nvidia0) */
      if (!g_ascii_isdigit (pid[0]) || atoi (pid) == (int) getpid ())
        continue;
      gchar *fdp = g_build_filename ("/proc", pid, "fd", NULL);
      GDir *fds = g_dir_open (fdp, 0, NULL);
      gboolean holds = FALSE;
      const gchar *fd;
      while (fds && !holds && (fd = g_dir_read_name (fds)))
        {
          gchar *lp = g_build_filename (fdp, fd, NULL);
          gchar *t = g_file_read_link (lp, NULL);
          /* /dev/nvidia0, /dev/nvidia1 … (not nvidiactl / nvidia-uvm) */
          holds = t && g_str_has_prefix (t, "/dev/nvidia")
                  && g_ascii_isdigit (t[strlen ("/dev/nvidia")]);
          g_free (t);
          g_free (lp);
        }
      if (fds)
        g_dir_close (fds);
      g_free (fdp);
      if (!holds)
        continue;

      gchar *cp = g_build_filename ("/proc", pid, "comm", NULL);
      gchar *comm = acc_read_trim (cp);
      g_free (cp);
      if (comm && *comm && !strstr (s->str, comm))
        {
          if (s->len)
            g_string_append (s, ", ");
          g_string_append (s, comm);
        }
      g_free (comm);
    }
  g_dir_close (proc);
  g_strlcpy (out, s->str, len);
  g_string_free (s, TRUE);
}

gboolean
acc_gfx_state (AccGfxState *out)
{
  static gchar  s_holders[160];
  static gint64 s_holders_at = 0;

  if (!out)
    return FALSE;
  memset (out, 0, sizeof *out);
  out->pm = ACC_DGPU_PM_UNKNOWN;

  gchar *dd = acc_dev_wmi_file ("dgpu_disable");
  out->has_dgpu_disable = dd != NULL;
  g_free (dd);

  out->supergfxd_running = supergfxd_owned ();
  if (!acc_path_exists (SUPERGFX_CLI))
    {
      gchar *txt = NULL;
      if (g_file_get_contents (SUPERGFX_MODPROBE, &txt, NULL, NULL))
        out->supergfx_leftover = acc_gfx_text_blacklists_nvidia (txt);
      g_free (txt);
    }

  gchar *pci = nvidia_pci_dir ();
  out->dgpu_present = pci != NULL;
  if (pci)
    {
      gchar *rp = g_build_filename (pci, "power", "runtime_status", NULL);
      gchar *rs = acc_read_trim (rp);
      if (g_strcmp0 (rs, "suspended") == 0)
        out->pm = ACC_DGPU_PM_SUSPENDED;
      else if (g_strcmp0 (rs, "active") == 0)
        out->pm = ACC_DGPU_PM_ACTIVE;
      g_free (rs);
      g_free (rp);
      g_free (pci);
    }

  if (out->pm == ACC_DGPU_PM_ACTIVE)
    {
      gint64 now = g_get_monotonic_time ();
      if (!s_holders_at || now - s_holders_at >= HOLDERS_US)
        {
          scan_holders (s_holders, sizeof s_holders);
          s_holders_at = now;
        }
      g_strlcpy (out->holders, s_holders, sizeof out->holders);
    }
  return out->dgpu_present;
}

gboolean
acc_gfx_integrated_supported (const AccGfxState *st)
{
  /* No model has been verified yet; see docs/gpu-mux.md, "Integrated". */
  (void) st;
  return FALSE;
}
