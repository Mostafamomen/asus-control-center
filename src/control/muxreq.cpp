/* control/muxreq.cpp - GPU MUX request record (see muxreq.h)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "muxreq.h"
#include "hostcmd.h"
#include "../probe/probe.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

#define BOOT_ID_FILE "/proc/sys/kernel/random/boot_id"

static gchar *
req_path (void)
{
  return g_build_filename (g_get_user_config_dir (), "asus-control-center",
                           "gpu-request.ini", NULL);
}

static gchar *
this_boot (void)
{
  return acc_read_trim (BOOT_ID_FILE);
}

void
acc_muxreq_record (gint mode, gboolean queued)
{
  if (mode != 0 && mode != 1)
    return;
  gchar *boot = this_boot ();
  if (!boot)
    return;

  GKeyFile *kf = g_key_file_new ();
  g_key_file_set_integer (kf, "request", "mode", mode);
  g_key_file_set_boolean (kf, "request", "queued", queued);
  g_key_file_set_string (kf, "request", "boot_id", boot);
  g_key_file_set_int64 (kf, "request", "time", g_get_real_time () / G_USEC_PER_SEC);

  gchar *p = req_path ();
  gchar *dir = g_path_get_dirname (p);
  g_mkdir_with_parents (dir, 0700);
  GError *e = NULL;
  if (!g_key_file_save_to_file (kf, p, &e))
    {
      g_printerr ("gpu request not saved: %s\n", e->message);
      g_clear_error (&e);
    }
  g_free (dir);
  g_free (p);
  g_free (boot);
  g_key_file_free (kf);
}

void
acc_muxreq_clear (void)
{
  gchar *p = req_path ();
  g_unlink (p);
  g_free (p);
}

AccMuxReqState
acc_muxreq_classify (gint req_mode, const gchar *req_boot,
                     const gchar *now_boot, gint live_mode)
{
  if ((req_mode != 0 && req_mode != 1) || !req_boot || !*req_boot)
    return ACC_MUXREQ_NONE;
  if (now_boot && g_strcmp0 (req_boot, now_boot) == 0)
    return ACC_MUXREQ_PENDING;
  if (live_mode != 0 && live_mode != 1)
    return ACC_MUXREQ_NONE;          /* cannot tell: say nothing */
  return live_mode == req_mode ? ACC_MUXREQ_VERIFIED : ACC_MUXREQ_FAILED;
}

gchar *
acc_muxreq_reason_from_log (const gchar *log)
{
  if (!log)
    return NULL;
  /* the asusd line names the value and the errno; the kernel line is the
   * firmware's own answer; the asus-shutdown line is the fallback */
  static const gchar *keys[] = {
    "Could not apply queued GPU attribute",
    "Failed to set current_value",
    "Failed to apply deferred GPU settings",
    NULL
  };
  gchar **lines = g_strsplit (log, "\n", -1);
  gchar *hit = NULL;
  for (gint k = 0; keys[k] && !hit; k++)
    for (gint i = 0; lines[i]; i++)
      if (strstr (lines[i], keys[k]))
        {
          g_free (hit);
          hit = g_strdup (g_strstrip (lines[i]));   /* keep the last one */
        }
  g_strfreev (lines);
  return hit;
}

/* The request boot's journal lines about the MUX. The journal lives on the
 * host, so the read goes through the host command layer: the desktop user
 * can read the system journal on Fedora (wheel). */
static gchar *
journal_reason (const gchar *boot)
{
  /* boot ids are 36 chars of [0-9a-f-]; anything else is not passed on */
  for (const gchar *c = boot; *c; c++)
    if (!g_ascii_isxdigit (*c) && *c != '-')
      return NULL;

  gchar *out = NULL, *err = NULL;
  gint code = -1;
  AccHostHow how = ACC_HOST_UNAVAILABLE;
  if (!acc_host_run (ACC_CMD_JOURNAL_MUX, boot, &out, &code, &how, &err))
    {
      g_free (out);
      g_free (err);
      return NULL;
    }
  g_free (err);
  gchar *r = acc_muxreq_reason_from_log (out);
  g_free (out);
  return r;
}

void
acc_muxreq_check (gint live_mode, AccMuxReq *out)
{
  static gchar   *s_reason = NULL;
  static gboolean s_reason_done = FALSE;

  memset (out, 0, sizeof *out);
  out->mode = -1;

  gchar *p = req_path ();
  GKeyFile *kf = g_key_file_new ();
  gboolean ok = g_key_file_load_from_file (kf, p, G_KEY_FILE_NONE, NULL);
  g_free (p);
  if (!ok)
    {
      g_key_file_free (kf);
      return;
    }

  GError *e = NULL;
  gint mode = g_key_file_get_integer (kf, "request", "mode", &e);
  if (e)
    {
      g_clear_error (&e);
      mode = -1;
    }
  gchar *boot = g_key_file_get_string (kf, "request", "boot_id", NULL);
  out->queued = g_key_file_get_boolean (kf, "request", "queued", NULL);
  g_key_file_free (kf);

  gchar *now = this_boot ();
  out->state = acc_muxreq_classify (mode, boot, now, live_mode);
  out->mode = mode;
  if (boot)
    g_strlcpy (out->boot_id, boot, sizeof out->boot_id);

  if (out->state == ACC_MUXREQ_FAILED && boot)
    {
      if (!s_reason_done)
        {
          s_reason = journal_reason (boot);
          s_reason_done = TRUE;
        }
      out->reason = g_strdup (s_reason);
    }
  g_free (now);
  g_free (boot);
}
