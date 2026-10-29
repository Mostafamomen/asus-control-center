/* control/gfx.cpp - supergfxctl client (see gfx.h)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * supergfxd answers slowly (or not at all) while it is busy switching, so
 * nothing here may run on the GTK main thread: the page reads a cached
 * snapshot that a worker thread refreshes, mode requests run in a GTask
 * thread, and every CLI call is capped with timeout(1).
 */
#include "gfx.h"
#include "../probe/probe.h"

#include <gio/gio.h>
#include <string.h>

#define GFX_CLI     "/usr/bin/supergfxctl"
#define TIMEOUT_BIN "/usr/bin/timeout"
#define CLI_SECS    "4"            /* one supergfxctl call, then SIGTERM */
#define CACHE_US    (3 * G_USEC_PER_SEC)

static GMutex      s_lock;
static AccGfxState s_cache;        /* guarded by s_lock                   */
static gint64      s_cache_at = 0; /* guarded by s_lock; 0 = stale         */
static gboolean    s_probing = FALSE;

/* Run supergfxctl with one fixed flag (and an optional mode name), capped
 * by timeout(1) when it exists. Returns stdout+stderr joined, or NULL when
 * it could not run at all; *ok is TRUE only for a zero exit status and
 * *timed_out for timeout's 124. Blocking: worker threads only. */
static gchar *
run_cli (const gchar *flag, const gchar *arg, gboolean *ok,
         gboolean *timed_out)
{
  *ok = FALSE;
  if (timed_out)
    *timed_out = FALSE;
  if (!acc_path_exists (GFX_CLI))
    return NULL;

  GError *e = NULL;
  gboolean capped = acc_path_exists (TIMEOUT_BIN);
  const gchar *argv_capped[] = { TIMEOUT_BIN, "-k", "1", CLI_SECS,
                                 GFX_CLI, flag, arg, NULL };
  const gchar *argv_plain[]  = { GFX_CLI, flag, arg, NULL };
  GSubprocess *p = g_subprocess_newv (capped ? argv_capped : argv_plain,
                                      (GSubprocessFlags)
                                      (G_SUBPROCESS_FLAGS_STDOUT_PIPE
                                       | G_SUBPROCESS_FLAGS_STDERR_MERGE),
                                      &e);
  if (!p)
    {
      g_clear_error (&e);
      return NULL;
    }

  gchar *out = NULL;
  if (!g_subprocess_communicate_utf8 (p, NULL, NULL, &out, NULL, &e))
    {
      g_clear_error (&e);
      g_object_unref (p);
      return NULL;
    }
  gint code = g_subprocess_get_if_exited (p)
              ? g_subprocess_get_exit_status (p) : -1;
  *ok = code == 0;
  if (timed_out)
    *timed_out = capped && (code == 124 || code == 137 || code < 0);
  g_object_unref (p);
  return out ? g_strstrip (out) : g_strdup ("");
}

static AccGfxMode
parse_mode (const gchar *s)
{
  if (!s || !*s)
    return ACC_GFX_UNKNOWN;
  gchar *l = g_ascii_strdown (s, -1);
  AccGfxMode m = ACC_GFX_UNKNOWN;
  if (strstr (l, "integrated"))
    m = ACC_GFX_INTEGRATED;
  else if (strstr (l, "hybrid"))
    m = ACC_GFX_HYBRID;
  else if (strstr (l, "vfio") || strstr (l, "egpu") || strstr (l, "mux")
           || strstr (l, "modeset"))
    m = ACC_GFX_OTHER;
  g_free (l);
  return m;
}

/* supergfxctl prints the action as prose; match the stable keywords */
static AccGfxAction
parse_action (const gchar *s)
{
  if (!s || !*s)
    return ACC_GFX_ACT_NONE;
  gchar *l = g_ascii_strdown (s, -1);
  AccGfxAction a = ACC_GFX_ACT_NONE;
  if (strstr (l, "logout") || strstr (l, "log out"))
    a = ACC_GFX_ACT_LOGOUT;
  else if (strstr (l, "reboot") || strstr (l, "restart"))
    a = ACC_GFX_ACT_REBOOT;
  else if (strstr (l, "integrated"))
    a = ACC_GFX_ACT_INTEGRATED_FIRST;
  g_free (l);
  return a;
}

/* One blocking probe of the daemon (worker thread or the CLI self-test).
 * Returns TRUE when supergfxd was too slow to answer (busy switching). */
static gboolean
probe (AccGfxState *out)
{
  memset (out, 0, sizeof *out);
  out->known = TRUE;
  out->installed = acc_path_exists (GFX_CLI);
  if (!out->installed)
    return FALSE;

  gboolean ok = FALSE, slow = FALSE;
  gchar *m = run_cli ("-g", NULL, &ok, &slow);
  if ((m && ok) || slow)
    {
      if (ok)
        {
          out->running = TRUE;
          out->mode = parse_mode (m);

          gchar *sup = run_cli ("-s", NULL, &ok, NULL);
          out->can_integrated = ok && sup
                                && strstr (sup, "ntegrated") != NULL;
          g_free (sup);
        }

      /* the pending request is still worth reading while it is busy */
      gchar *pm = run_cli ("-P", NULL, &ok, NULL);
      out->pending = ok ? parse_mode (pm) : ACC_GFX_UNKNOWN;
      g_free (pm);

      gchar *pa = run_cli ("-p", NULL, &ok, NULL);
      out->action = ok ? parse_action (pa) : ACC_GFX_ACT_NONE;
      g_free (pa);

      /* a "pending" equal to the live mode is not pending */
      if (out->pending == out->mode)
        out->pending = ACC_GFX_UNKNOWN;
    }
  g_free (m);
  return slow;
}

static gpointer
probe_thread (gpointer data)
{
  (void) data;
  AccGfxState st;
  gboolean slow = probe (&st);

  g_mutex_lock (&s_lock);
  /* a busy daemon is not a missing one: keep the last good mode and
   * capabilities, take the fresh pending request */
  if (slow && s_cache.running)
    {
      s_cache.pending = st.pending != s_cache.mode ? st.pending
                                                   : ACC_GFX_UNKNOWN;
      s_cache.action = st.action;
    }
  else
    s_cache = st;
  s_cache_at = g_get_monotonic_time ();
  s_probing = FALSE;
  g_mutex_unlock (&s_lock);
  return NULL;
}

gboolean
acc_gfx_state (AccGfxState *out)
{
  if (!out)
    return FALSE;

  gboolean start = FALSE;
  g_mutex_lock (&s_lock);
  *out = s_cache;
  if (!s_probing
      && (!s_cache_at || g_get_monotonic_time () - s_cache_at >= CACHE_US))
    start = s_probing = TRUE;
  g_mutex_unlock (&s_lock);

  if (start)
    {
      GThread *t = g_thread_try_new ("acc-gfx-probe", probe_thread, NULL,
                                     NULL);
      if (t)
        g_thread_unref (t);
      else
        {
          g_mutex_lock (&s_lock);
          s_probing = FALSE;
          g_mutex_unlock (&s_lock);
        }
    }
  return out->running;
}

gboolean
acc_gfx_state_sync (AccGfxState *out)
{
  if (!out)
    return FALSE;
  probe (out);
  return out->running;
}

/* ---- mode request (GTask worker) ------------------------------------ */

typedef struct
{
  AccGfxMode   mode;
  AccGfxAction action;
  gchar       *err;
} SetJob;

static void
set_job_free (gpointer p)
{
  SetJob *j = (SetJob *) p;
  g_free (j->err);
  g_free (j);
}

static void
set_thread (GTask *task, gpointer src, gpointer data, GCancellable *c)
{
  (void) src;
  (void) c;
  SetJob *j = (SetJob *) data;
  const gchar *name = j->mode == ACC_GFX_INTEGRATED ? "Integrated" : "Hybrid";

  gboolean ok = FALSE, slow = FALSE;
  gchar *msg = run_cli ("-m", name, &ok, &slow);

  /* supergfxctl may sit waiting on the daemon while it prepares the
   * switch. If it timed out, trust the daemon's pending mode instead. */
  if (!ok && slow)
    {
      gboolean pok = FALSE;
      gchar *pm = run_cli ("-P", NULL, &pok, NULL);
      gchar *gm = run_cli ("-g", NULL, &ok, NULL);
      ok = (pok && parse_mode (pm) == j->mode)
           || (ok && parse_mode (gm) == j->mode);
      g_free (pm);
      g_free (gm);
      if (ok && !msg)
        msg = g_strdup ("logout");
    }

  if (ok)
    {
      gboolean pok = FALSE;
      gchar *pa = run_cli ("-p", NULL, &pok, NULL);
      j->action = parse_action (pok && pa && *pa ? pa : msg);
      g_free (pa);
    }
  else
    j->err = msg && *msg ? g_strdup (msg)
                         : g_strdup (slow ? "supergfxd did not answer"
                                          : "supergfxctl is not available");
  g_free (msg);

  g_mutex_lock (&s_lock);
  s_cache_at = 0;                     /* next read must be fresh */
  g_mutex_unlock (&s_lock);

  g_task_return_boolean (task, ok);
}

void
acc_gfx_set_mode_async (AccGfxMode mode, GAsyncReadyCallback cb,
                        gpointer user)
{
  GTask *t = g_task_new (NULL, NULL, cb, user);
  SetJob *j = g_new0 (SetJob, 1);
  j->mode = mode == ACC_GFX_INTEGRATED ? ACC_GFX_INTEGRATED : ACC_GFX_HYBRID;
  g_task_set_task_data (t, j, set_job_free);
  g_task_run_in_thread (t, set_thread);
  g_object_unref (t);
}

gboolean
acc_gfx_set_mode_finish (GAsyncResult *res, AccGfxAction *action,
                         gchar **err)
{
  GTask *t = G_TASK (res);
  SetJob *j = (SetJob *) g_task_get_task_data (t);
  gboolean ok = g_task_propagate_boolean (t, NULL);
  if (action)
    *action = j->action;
  if (err)
    *err = ok ? NULL : g_strdup (j->err);
  return ok;
}
