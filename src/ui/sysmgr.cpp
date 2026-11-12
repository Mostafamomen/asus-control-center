/* ui/sysmgr.cpp - System Manager (read-only, PC Manager-inspired)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "sysmgr.h"
#include "theme.h"
#include "mcui.h"
#include "window.h"
#include "../i18n/i18n.h"
#include "../probe/load.h"
#include "../probe/battery.h"
#include <adwaita.h>
#include <glib.h>
#include <string.h>
#include <stdarg.h>
#include <initializer_list>
#include <vector>
#include <stdlib.h>
#include <sys/statvfs.h>
#include <glib/gstdio.h>

static GtkWidget *sm_health, *sm_storage, *sm_startup, *sm_proc, *sm_cleanup, *sm_updates;
static GtkWidget *sm_overall, *sm_cpu, *sm_ram;
static GtkWidget *sm_reco;
static guint sm_proc_timer = 0;
static gboolean sm_proc_busy = FALSE;


static gchar *
run_cmd (const gchar *const *argv)
{
  gchar *out = NULL;
  gint status = 0;
  if (g_spawn_sync (NULL, (gchar **) argv, NULL,
                    (GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL),
                    NULL, NULL, &out, NULL, &status, NULL))
    return out;
  g_free (out);
  return NULL;
}

static gchar *
run_cmd (std::initializer_list<const gchar *> args)
{
  std::vector<const gchar *> v (args);
  v.push_back (NULL);
  return run_cmd (v.data ());
}

static void
set (GtkWidget *w, const gchar *fmt, ...)
{
  if (!w) return;
  va_list ap;
  va_start (ap, fmt);
  gchar *s = g_strdup_vprintf (fmt, ap);
  va_end (ap);
  gtk_label_set_text (GTK_LABEL (w), s);
  g_free (s);
}

static gchar *
build_startup_text (void)
{
  GString *s = g_string_new (NULL);
  const gchar *dirs[] = { NULL, "/etc/xdg/autostart" };
  gchar *user_dir = g_build_filename (g_get_user_config_dir (), "autostart", NULL);
  dirs[0] = user_dir;

  for (int i = 0; i < 2; i++)
    {
      const gchar *d = dirs[i];
      if (!d) continue;
      GDir *gd = g_dir_open (d, 0, NULL);
      if (!gd) continue;
      gint count = 0;
      GString *names = g_string_new (NULL);
      const gchar *entry;
      while ((entry = g_dir_read_name (gd)))
        {
          if (!g_str_has_suffix (entry, ".desktop"))
            continue;
          if (names->len > 0 && names->len < 120)
            g_string_append (names, ", ");
          if (names->len < 120)
            {
              gchar *base = g_strndup (entry, strlen (entry) - 8);
              g_string_append (names, base);
              g_free (base);
            }
          count++;
        }
      g_dir_close (gd);
      if (i == 0)
        g_string_append_printf (s, "%s: %d", acc_tr (ACC_T_SM_USER), count);
      else
        g_string_append_printf (s, "  %s: %d", acc_tr (ACC_T_SM_SYSTEM), count);
      if (count > 0 && names->len > 0)
        g_string_append_printf (s, " (%s)", names->str);
      g_string_append (s, "\\n");
      g_string_free (names, TRUE);
    }
  g_free (user_dir);
  gchar *r = g_strdup (s->str);
  g_string_free (s, TRUE);
  return r;
}

static gboolean
update_proc_idle (gpointer data)
{
  gchar *out = (gchar *) data;
  if (sm_proc && out)
    set (sm_proc, "%s", g_strstrip (out));
  g_free (out);
  sm_proc_busy = FALSE;
  return G_SOURCE_REMOVE;
}

static gpointer
proc_scan_thread (gpointer data)
{
  (void) data;
  if (sm_proc_busy)
    return NULL;
  sm_proc_busy = TRUE;
  gchar *out = run_cmd ({ "sh", "-c",
    "ps -eo pid,pcpu,pmem,comm --sort=-pmem | head -8", NULL });
  g_idle_add (update_proc_idle, out);
  return NULL;
}

typedef struct { gchar *text; } ScanResult;

static gboolean
update_storage_idle (gpointer data)
{
  ScanResult *r = (ScanResult *) data;
  if (sm_storage && r && r->text)
    set (sm_storage, "%s", r->text);
  g_free (r ? r->text : NULL);
  g_free (r);
  return G_SOURCE_REMOVE;
}

static gpointer
storage_scan_thread (gpointer data)
{
  (void) data;
  LoadInfo ld;
  acc_load_read (&ld);
  gchar *cache = run_cmd ({ "sh", "-c",
    "du -sh ~/.cache 2>/dev/null | cut -f1", NULL });
  GString *s = g_string_new (NULL);
  if (ld.disk_total_gb > 0)
    g_string_append_printf (s, "Disk: %.0f%% used · %.0f / %.0f GB\\n",
                            ld.disk_pct, ld.disk_used_gb, ld.disk_total_gb);
  else
    g_string_append (s, "Disk usage unavailable\\n");
  if (cache && *cache)
    g_string_append_printf (s, "%s: %s", acc_tr (ACC_T_SM_CACHE), g_strstrip (cache));
  else
    g_string_append (s, acc_tr (ACC_T_SM_CACHE));
  g_free (cache);
  ScanResult *r = g_new0 (ScanResult, 1);
  r->text = g_strdup (s->str);
  g_string_free (s, TRUE);
  g_idle_add (update_storage_idle, r);
  return NULL;
}

static void
run_health (void)
{
  GString *s = g_string_new (NULL);
  gchar *failed = run_cmd ({ "sh", "-c",
    "systemctl --failed --no-pager 2>/dev/null | sed -n '1,8p'", NULL });
  if (failed && *failed)
    g_string_append_printf (s, "Failed services:\\n%s\\n", g_strstrip (failed));
  else
    g_string_append_printf (s, "%s\\n", acc_tr (ACC_T_SM_NO_FAILED));
  g_free (failed);

  gchar *tools = run_cmd ({ "sh", "-c",
    "command -v dnf >/dev/null && echo 'DNF: available' || echo 'DNF: unavailable'; "
    "command -v flatpak >/dev/null && echo 'Flatpak: available' || echo 'Flatpak: unavailable'; "
    "command -v fwupdmgr >/dev/null && echo 'fwupd: available' || echo 'fwupd: unavailable'", NULL });
  if (tools)
    {
      g_string_append (s, tools);
      g_free (tools);
    }
  LoadInfo ld;
  acc_load_read (&ld);
  if (ld.disk_pct > 90.0)
    g_string_append (s, "\\nStorage warning: disk usage high");
  if (sm_health)
    set (sm_health, "%s", s->str);

  /* Smart recommendations */
  GString *r = g_string_new (NULL);
  LoadInfo ld2;
  acc_load_read (&ld2);
  if (ld2.disk_pct > 90.0)
    g_string_append (r, "Low storage: review Storage Manager.\n");
  if (ld2.mem_pct > 90.0)
    g_string_append (r, "High RAM usage: inspect Process Manager.\n");
  gchar *failed_svc = run_cmd ({ "sh", "-c", "systemctl --failed --no-pager 2>/dev/null | grep -c failed", NULL });
  if (failed_svc && atoi (failed_svc) > 0)
    g_string_append (r, "Failed services detected: open System Diagnosis.\n");
  g_free (failed_svc);
  if (g_file_test ("/sys/class/power_supply/BAT0", G_FILE_TEST_IS_DIR))
    {
      BatInfo b; battery_read (&b);
      gdouble health = (b.wh_full > 0 && b.wh_design > 0) ? b.wh_full / b.wh_design * 100.0 : -1;
      if (health >= 0 && health < 80.0)
        g_string_append (r, "Battery health low: review Battery Care.\n");
    }
  if (r->len == 0)
    g_string_append (r, "No critical issues detected.\n");
  if (sm_reco)
    set (sm_reco, "%s", r->str);

  /* Health history (local, append and cap to 50 entries) */
  gchar *hist = g_build_filename (g_get_user_config_dir (), "asus-control-center", "health_history.txt", NULL);
  gchar *hist_dir = g_path_get_dirname (hist);
  g_mkdir_with_parents (hist_dir, 0700);
  g_free (hist_dir);
  GDateTime *now = g_date_time_new_now_local ();
  gchar *ts = g_date_time_format (now, "%Y-%m-%d %H:%M");
  g_date_time_unref (now);
  GString *entry = g_string_new (NULL);
  g_string_append_printf (entry, "%s | %s\n", ts, r->str);
  g_free (ts);

  gchar *old = NULL;
  gsize old_len = 0;
  if (g_file_get_contents (hist, &old, &old_len, NULL))
    {
      GString *combined = g_string_new (old);
      g_string_append (combined, entry->str);
      gchar **lines = g_strsplit (combined->str, "\n", -1);
      gint n = 0;
      while (lines[n]) n++;
      if (n > 50)
        {
          GString *trimmed = g_string_new (NULL);
          for (gint i = n - 50; i < n; i++)
            if (lines[i] && *lines[i])
              g_string_append_printf (trimmed, "%s\n", lines[i]);
          g_file_set_contents_full (hist, trimmed->str, -1,
                                    G_FILE_SET_CONTENTS_CONSISTENT, 0600, NULL);
          g_string_free (trimmed, TRUE);
        }
      else
        g_file_set_contents_full (hist, combined->str, -1,
                                  G_FILE_SET_CONTENTS_CONSISTENT, 0600, NULL);
      g_string_free (combined, TRUE);
      g_strfreev (lines);
      g_free (old);
    }
  else
    g_file_set_contents_full (hist, entry->str, -1,
                              G_FILE_SET_CONTENTS_CONSISTENT, 0600, NULL);
  g_string_free (entry, TRUE);
  g_free (hist);
  g_string_free (r, TRUE);
  g_string_free (s, TRUE);
}

static void
run_updates (void)
{
  gchar *out = run_cmd ({ "sh", "-c",
    "command -v dnf >/dev/null && echo 'DNF available' || echo 'DNF unavailable'; "
    "command -v flatpak >/dev/null && echo 'Flatpak available' || echo 'Flatpak unavailable'; "
    "command -v fwupdmgr >/dev/null && echo 'fwupd available' || echo 'fwupd unavailable'", NULL });
  if (out)
    {
      set (sm_updates, "%s", g_strstrip (out));
      g_free (out);
    }
  else
    set (sm_updates, "%s", acc_tr (ACC_T_SM_UNAVAILABLE));
}

static gboolean
proc_timer_cb (gpointer data)
{
  (void) data;
  if (!sm_proc || !GTK_IS_WIDGET (sm_proc))
    return G_SOURCE_REMOVE;
  /* Pause background work while System Manager is not the visible page. */
  if (!gtk_widget_get_visible (sm_proc))
    return G_SOURCE_CONTINUE;
  proc_scan_thread (NULL);
  return G_SOURCE_CONTINUE;
}

static void
quick_action_cb (GtkButton *b, gpointer p)
{
  (void) b;
  acc_ui_nav_to ((const gchar *) p);
}

/* ------------------------------------------------ Storage Manager ------ */
/* Windows-Storage-Sense-style breakdown of what is using disk space, with
 * safe, user-scope cleanup. Every clean is confirmed first and limited to a
 * fixed allow-list of per-user cache/trash directories — never system paths,
 * never a path from untrusted input, never your documents or Downloads. */

#define ST(en, ar) (acc_i18n_lang () == ACC_LANG_AR ? (ar) : (en))

enum { STC_CACHE = 0, STC_THUMBS, STC_TRASH, STC_FLATPAK, STC_DOWNLOADS, STC_COUNT };

static GtkWidget *st_bar, *st_bar_lbl, *st_val[STC_COUNT], *st_btn[STC_COUNT], *st_status;
static guint      st_gen;
static gboolean   st_busy;

/* Absolute path for a cleanable category, newly allocated, or NULL. */
static gchar *
st_path (int cat)
{
  const gchar *home = g_get_home_dir ();
  switch (cat)
    {
    case STC_CACHE:     return g_build_filename (home, ".cache", NULL);
    case STC_THUMBS:    return g_build_filename (home, ".cache", "thumbnails", NULL);
    case STC_TRASH:     return g_build_filename (home, ".local", "share", "Trash", NULL);
    case STC_DOWNLOADS: return g_strdup (g_get_user_special_dir (G_USER_DIRECTORY_DOWNLOAD)
                                         ?: g_build_filename (home, "Downloads", NULL));
    default:            return NULL;
    }
}

/* TRUE only for an absolute path strictly inside $HOME and at least one
 * component deep — the guard before any deletion. */
static gboolean
st_path_safe (const gchar *p)
{
  const gchar *home = g_get_home_dir ();
  if (!p || !home || !g_path_is_absolute (p)) return FALSE;
  if (strstr (p, "..")) return FALSE;
  gsize hl = strlen (home);
  if (strncmp (p, home, hl) != 0 || p[hl] != G_DIR_SEPARATOR) return FALSE;
  return strlen (p) > hl + 1;
}

/* Recursively delete `path` (file, dir, or symlink). Symlinks are unlinked,
 * never followed. Bounded to paths under $HOME by st_path_safe at entry. */
static void
st_rm (const gchar *path)
{
  if (g_file_test (path, G_FILE_TEST_IS_DIR) &&
      !g_file_test (path, G_FILE_TEST_IS_SYMLINK))
    {
      GDir *d = g_dir_open (path, 0, NULL);
      if (d)
        {
          const gchar *n;
          while ((n = g_dir_read_name (d)))
            {
              gchar *c = g_build_filename (path, n, NULL);
              st_rm (c);
              g_free (c);
            }
          g_dir_close (d);
        }
      g_rmdir (path);
    }
  else
    g_unlink (path);
}

/* Empty the contents of `dir` but keep `dir` itself. */
static void
st_empty (const gchar *dir)
{
  if (!st_path_safe (dir) || !g_file_test (dir, G_FILE_TEST_IS_DIR))
    return;
  GDir *d = g_dir_open (dir, 0, NULL);
  if (!d) return;
  const gchar *n;
  while ((n = g_dir_read_name (d)))
    {
      gchar *c = g_build_filename (dir, n, NULL);
      st_rm (c);
      g_free (c);
    }
  g_dir_close (d);
}

static void st_scan (void);

typedef struct { guint gen; gint64 sizes[STC_COUNT]; gdouble disk_total, disk_free; } StMsg;

static gchar *
st_human (gint64 b)
{
  if (b < 0) return g_strdup ("—");
  return g_format_size ((guint64) b);
}

static gboolean
st_apply (gpointer data)
{
  StMsg *m = (StMsg *) data;
  if (m->gen == st_gen)
    {
      for (int i = 0; i < STC_COUNT; i++)
        if (st_val[i])
          {
            gchar *h;
            if (i == STC_FLATPAK)
              h = m->sizes[i] > 0
                  ? g_strdup_printf (ST ("%ld unused", "%ld غير مستخدم"), (long) m->sizes[i])
                  : g_strdup (ST ("None", "لا شيء"));
            else
              h = st_human (m->sizes[i]);
            gtk_label_set_text (GTK_LABEL (st_val[i]), h);
            g_free (h);
            if (st_btn[i])
              gtk_widget_set_sensitive (st_btn[i], m->sizes[i] > 0);
          }
      if (st_bar && m->disk_total > 0)
        {
          gdouble used = (m->disk_total - m->disk_free) / m->disk_total;
          gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (st_bar), used);
          if (st_bar_lbl)
            {
              gchar *s = g_strdup_printf (
                ST ("%.0f GB free of %.0f GB", "%.0f جيجابايت متاحة من %.0f"),
                m->disk_free, m->disk_total);
              gtk_label_set_text (GTK_LABEL (st_bar_lbl), s);
              g_free (s);
            }
        }
      if (st_status) gtk_label_set_text (GTK_LABEL (st_status), "");
      st_busy = FALSE;
    }
  g_free (m);
  return G_SOURCE_REMOVE;
}

static gint64
st_du (const gchar *dir)
{
  if (!dir || !g_file_test (dir, G_FILE_TEST_EXISTS)) return 0;
  gchar *o = run_cmd ({ "du", "-sb", "--", dir, NULL });
  gint64 v = o ? g_ascii_strtoll (o, NULL, 10) : -1;
  g_free (o);
  return v;
}

static gpointer
st_worker (gpointer data)
{
  guint gen = GPOINTER_TO_UINT (data);
  StMsg *m = g_new0 (StMsg, 1);
  m->gen = gen;
  for (int i = 0; i < STC_COUNT; i++)
    {
      gchar *p = st_path (i);
      /* cache figure excludes thumbnails so the two rows don't double-count */
      if (i == STC_CACHE)
        {
          gint64 all = st_du (p);
          gchar *th = st_path (STC_THUMBS);
          gint64 t = st_du (th);
          m->sizes[i] = (all < 0) ? -1 : (all - (t > 0 ? t : 0));
          g_free (th);
        }
      else if (i == STC_FLATPAK)
        {
          gchar *o = run_cmd ({ "sh", "-c",
            "flatpak list --runtime --columns=application 2>/dev/null >/dev/null; "
            "flatpak uninstall --unused --dry-run 2>/dev/null | grep -cE '^[[:space:]]*[0-9]+\\.' ", NULL });
          m->sizes[i] = o ? atoll (o) : 0;   /* count of unused runtimes, not bytes */
          g_free (o);
        }
      else
        m->sizes[i] = st_du (p);
      g_free (p);
    }
  struct statvfs sv;
  if (statvfs (g_get_home_dir (), &sv) == 0 && sv.f_blocks)
    {
      m->disk_total = (gdouble) sv.f_blocks * sv.f_frsize / 1e9;
      m->disk_free  = (gdouble) sv.f_bavail * sv.f_frsize / 1e9;
    }
  g_idle_add (st_apply, m);
  return NULL;
}

static void
st_scan (void)
{
  if (st_busy) return;
  st_busy = TRUE;
  st_gen++;
  if (st_status) gtk_label_set_text (GTK_LABEL (st_status), ST ("Measuring…", "جارٍ القياس…"));
  g_thread_unref (g_thread_new ("storage-mgr", st_worker, GUINT_TO_POINTER (st_gen)));
}

/* ---- cleaning (worker; confirmed before it is ever called) ---- */

static gpointer
st_clean_worker (gpointer data)
{
  int cat = GPOINTER_TO_INT (data);
  if (cat == STC_FLATPAK)
    {
      gchar *o = run_cmd ({ "flatpak", "uninstall", "--unused", "-y", NULL });
      g_free (o);
    }
  else if (cat == STC_TRASH)
    {
      gchar *files = g_build_filename (g_get_home_dir (), ".local", "share", "Trash", "files", NULL);
      gchar *info  = g_build_filename (g_get_home_dir (), ".local", "share", "Trash", "info", NULL);
      st_empty (files); st_empty (info);
      g_free (files); g_free (info);
    }
  else
    {
      gchar *p = st_path (cat);
      st_empty (p);
      g_free (p);
    }
  g_idle_add ((GSourceFunc) +[] (gpointer) -> gboolean {
      acc_ui_toast (ST ("Cleanup finished.", "انتهى التنظيف."));
      st_scan ();
      return G_SOURCE_REMOVE;
    }, NULL);
  return NULL;
}

static void
st_clean_resp (AdwAlertDialog *d, const gchar *resp, gpointer data)
{
  (void) d;
  if (g_strcmp0 (resp, "ok") == 0)
    {
      if (st_status) gtk_label_set_text (GTK_LABEL (st_status), ST ("Cleaning…", "جارٍ التنظيف…"));
      g_thread_unref (g_thread_new ("storage-clean", st_clean_worker, data));
    }
}

static void
st_clean_cb (GtkButton *b, gpointer data)
{
  (void) b;
  int cat = GPOINTER_TO_INT (data);
  const gchar *body =
    cat == STC_TRASH    ? ST ("Permanently delete everything in the Trash? This cannot be undone.",
                              "حذف كل ما في سلة المهملات نهائيًا؟ لا يمكن التراجع.") :
    cat == STC_FLATPAK  ? ST ("Remove Flatpak runtimes no app is using? They reinstall automatically when needed.",
                              "إزالة أطقم تشغيل Flatpak غير المستخدمة؟ تُعاد تلقائيًا عند الحاجة.") :
    cat == STC_THUMBS   ? ST ("Clear the thumbnail cache? Thumbnails will be regenerated as needed.",
                              "مسح ذاكرة الصور المصغّرة؟ ستُنشأ من جديد عند الحاجة.") :
                          ST ("Clear the application cache? Apps rebuild it as needed; some may start slower once.",
                              "مسح ذاكرة التطبيقات المؤقتة؟ تُعاد عند الحاجة وقد يبطؤ بدء بعضها مرة واحدة.");
  AdwDialog *d = ADW_DIALOG (adw_alert_dialog_new (ST ("Confirm cleanup", "تأكيد التنظيف"), body));
  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (d),
    "cancel", ST ("Cancel", "إلغاء"), "ok", ST ("Clean", "تنظيف"), NULL);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (d), "ok",
    cat == STC_TRASH ? ADW_RESPONSE_DESTRUCTIVE : ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (d), "cancel");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (d), "cancel");
  g_signal_connect (d, "response", G_CALLBACK (st_clean_resp), data);
  adw_dialog_present (d, acc_ui_root ());
}

static void
st_open_downloads_cb (GtkButton *b, gpointer d)
{
  (void) b; (void) d;
  gchar *p = st_path (STC_DOWNLOADS);
  gchar *uri = g_strdup_printf ("file://%s", p);
  const gchar *argv[] = { "xdg-open", uri, NULL };
  GSubprocess *pr = g_subprocess_newv (argv,
      (GSubprocessFlags) (G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE), NULL);
  if (pr) g_object_unref (pr);
  else acc_ui_toast (ST ("This folder cannot be opened from here.",
                         "لا يمكن فتح هذا المجلد من هنا."));
  g_free (uri); g_free (p);
}

static void
st_refresh_cb (GtkButton *b, gpointer d) { (void) b; (void) d; st_scan (); }

static GtkWidget *
st_row (int cat, const gchar *title, const gchar *desc)
{
  GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *col = gtk_box_new (GTK_ORIENTATION_VERTICAL, 1);
  gtk_widget_set_hexpand (col, TRUE);
  gtk_box_append (GTK_BOX (col), mc_label (title, "val", 0));
  GtkWidget *dl = mc_label (desc, "dim", 0);
  gtk_label_set_wrap (GTK_LABEL (dl), TRUE);
  gtk_box_append (GTK_BOX (col), dl);
  gtk_box_append (GTK_BOX (row), col);

  st_val[cat] = mc_label ("…", "val", 1);
  gtk_widget_set_valign (st_val[cat], GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (row), st_val[cat]);

  if (cat == STC_DOWNLOADS)
    {
      GtkWidget *o = gtk_button_new_with_label (ST ("Open", "فتح"));
      gtk_widget_add_css_class (o, "actionbtn");
      gtk_widget_set_valign (o, GTK_ALIGN_CENTER);
      g_signal_connect (o, "clicked", G_CALLBACK (st_open_downloads_cb), NULL);
      gtk_box_append (GTK_BOX (row), o);
      st_btn[cat] = NULL;
    }
  else
    {
      st_btn[cat] = gtk_button_new_with_label (
        cat == STC_TRASH ? ST ("Empty", "إفراغ") : ST ("Clean", "تنظيف"));
      gtk_widget_add_css_class (st_btn[cat], "actionbtn");
      gtk_widget_set_valign (st_btn[cat], GTK_ALIGN_CENTER);
      g_signal_connect (st_btn[cat], "clicked", G_CALLBACK (st_clean_cb), GINT_TO_POINTER (cat));
      gtk_box_append (GTK_BOX (row), st_btn[cat]);
    }
  return row;
}

static GtkWidget *
st_section (void)
{
  GtkWidget *panel = mc_panel (ST ("Storage Manager", "مدير التخزين"));
  GtkWidget *sub = mc_label (
    ST ("See what is using space and free some safely. Cleanup only touches your own cache and Trash — never system files, documents or Downloads — and every action is confirmed first.",
        "شاهد ما يستهلك المساحة وحرّر بعضها بأمان. التنظيف يمسّ ذاكرتك المؤقتة وسلة المهملات فقط — دون ملفات النظام أو مستنداتك أو التنزيلات — ويُطلب تأكيد كل إجراء."),
    "pagesub", 0);
  gtk_label_set_wrap (GTK_LABEL (sub), TRUE);
  gtk_box_append (GTK_BOX (panel), sub);

  st_bar = gtk_progress_bar_new ();
  gtk_box_append (GTK_BOX (panel), st_bar);
  st_bar_lbl = mc_label ("…", "dim", 0);
  gtk_box_append (GTK_BOX (panel), st_bar_lbl);

  GtkWidget *bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *rb = gtk_button_new_with_label (ST ("Refresh", "تحديث"));
  gtk_widget_add_css_class (rb, "actionbtn");
  g_signal_connect (rb, "clicked", G_CALLBACK (st_refresh_cb), NULL);
  gtk_box_append (GTK_BOX (bar), rb);
  st_status = mc_label ("", "dim", 0);
  gtk_widget_set_valign (st_status, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (bar), st_status);
  gtk_box_append (GTK_BOX (panel), bar);

  gtk_box_append (GTK_BOX (panel), st_row (STC_CACHE,
    ST ("Application cache", "ذاكرة التطبيقات المؤقتة"),
    ST ("Temporary files apps can rebuild.", "ملفات مؤقتة يمكن للتطبيقات إعادة بنائها.")));
  gtk_box_append (GTK_BOX (panel), st_row (STC_THUMBS,
    ST ("Thumbnails", "الصور المصغّرة"),
    ST ("Cached previews of your images and videos.", "معاينات مخزّنة لصورك ومقاطعك.")));
  gtk_box_append (GTK_BOX (panel), st_row (STC_TRASH,
    ST ("Trash", "سلة المهملات"),
    ST ("Files you deleted but have not emptied yet.", "ملفات حذفتها ولم تُفرغها بعد.")));
  gtk_box_append (GTK_BOX (panel), st_row (STC_FLATPAK,
    ST ("Unused Flatpak runtimes", "أطقم تشغيل Flatpak غير المستخدمة"),
    ST ("Runtimes no installed app needs.", "أطقم لا يحتاجها أي تطبيق مثبّت.")));
  gtk_box_append (GTK_BOX (panel), st_row (STC_DOWNLOADS,
    ST ("Downloads folder", "مجلد التنزيلات"),
    ST ("Shown for review only — never cleaned automatically.", "يُعرض للمراجعة فقط ولا يُنظَّف تلقائيًا.")));

  st_scan ();
  return panel;
}

static void
st_teardown (void)
{
  st_gen++;
  st_busy = FALSE;
  st_bar = st_bar_lbl = st_status = NULL;
  for (int i = 0; i < STC_COUNT; i++) st_val[i] = st_btn[i] = NULL;
}

#undef ST

GtkWidget *
acc_page_sysmgr (AccUI *u)
{
  (void) u;
  GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 18);

  {
    GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
    gtk_widget_add_css_class (h, "pagehead");
    GtkWidget *col = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand (col, TRUE);
    gtk_widget_set_valign (col, GTK_ALIGN_CENTER);
    gtk_box_append (GTK_BOX (col), mc_label ("ASUSCraft", "eyebrow", 0));
    gtk_box_append (GTK_BOX (col), mc_label (acc_tr (ACC_T_SYSMGR), "pagetitle", 0));
    GtkWidget *sub = mc_label (acc_tr (ACC_T_SYSMGR_SUB), "pagesub", 0);
    gtk_label_set_wrap (GTK_LABEL (sub), TRUE);
    gtk_label_set_max_width_chars (GTK_LABEL (sub), 80);
    gtk_box_append (GTK_BOX (col), sub);
    gtk_box_append (GTK_BOX (h), col);
    gtk_box_append (GTK_BOX (page), h);
  }

  {
    GtkWidget *c = mc_card ();
    gtk_box_append (GTK_BOX (c), mc_title ("Overall System Health"));
    sm_overall = mc_label ("Checking…", "val", 0);
    gtk_label_set_wrap (GTK_LABEL (sm_overall), TRUE);
    gtk_box_append (GTK_BOX (c), sm_overall);
    gtk_box_append (GTK_BOX (page), c);
  }

  {
    GtkWidget *c = mc_card ();
    gtk_box_append (GTK_BOX (c), mc_title ("CPU & Memory"));
    sm_cpu = mc_label ("—", "val", 0);
    gtk_label_set_wrap (GTK_LABEL (sm_cpu), TRUE);
    gtk_box_append (GTK_BOX (c), sm_cpu);
    sm_ram = mc_label ("—", "val", 0);
    gtk_label_set_wrap (GTK_LABEL (sm_ram), TRUE);
    gtk_box_append (GTK_BOX (c), sm_ram);
    gtk_box_append (GTK_BOX (page), c);
  }

  {
    GtkWidget *c = mc_card ();
    gtk_box_append (GTK_BOX (c), mc_title ("Smart Recommendations"));
    sm_reco = mc_label ("Run Health Check to generate recommendations.", "val", 0);
    gtk_label_set_wrap (GTK_LABEL (sm_reco), TRUE);
    gtk_box_append (GTK_BOX (c), sm_reco);
    gtk_box_append (GTK_BOX (page), c);
  }

  {
    GtkWidget *c = mc_card ();
    gtk_box_append (GTK_BOX (c), mc_title (acc_tr (ACC_T_SM_HEALTH)));
    sm_health = mc_label (acc_tr (ACC_T_SM_LOADING), "val", 0);
    gtk_label_set_wrap (GTK_LABEL (sm_health), TRUE);
    gtk_box_append (GTK_BOX (c), sm_health);
    gtk_box_append (GTK_BOX (page), c);
  }

  {
    GtkWidget *c = mc_card ();
    gtk_box_append (GTK_BOX (c), mc_title (acc_tr (ACC_T_SM_STORAGE)));
    sm_storage = mc_label (acc_tr (ACC_T_SM_LOADING), "val", 0);
    gtk_label_set_wrap (GTK_LABEL (sm_storage), TRUE);
    gtk_box_append (GTK_BOX (c), sm_storage);
    gtk_box_append (GTK_BOX (page), c);
  }

  {
    GtkWidget *c = mc_card ();
    gtk_box_append (GTK_BOX (c), mc_title (acc_tr (ACC_T_SM_STARTUP)));
    sm_startup = mc_label (acc_tr (ACC_T_SM_LOADING), "val", 0);
    gtk_label_set_wrap (GTK_LABEL (sm_startup), TRUE);
    gtk_box_append (GTK_BOX (c), sm_startup);
    gtk_box_append (GTK_BOX (page), c);
  }

  {
    GtkWidget *c = mc_card ();
    gtk_box_append (GTK_BOX (c), mc_title (acc_tr (ACC_T_SM_PROC)));
    sm_proc = mc_label (acc_tr (ACC_T_SM_LOADING), "val", 0);
    gtk_label_set_wrap (GTK_LABEL (sm_proc), TRUE);
    gtk_box_append (GTK_BOX (c), sm_proc);
    gtk_box_append (GTK_BOX (page), c);
  }

  {
    GtkWidget *c = mc_card ();
    gtk_box_append (GTK_BOX (c), mc_title (acc_tr (ACC_T_SM_CLEANUP)));
    sm_cleanup = mc_label (acc_tr (ACC_T_SM_CACHE), "val", 0);
    gtk_label_set_wrap (GTK_LABEL (sm_cleanup), TRUE);
    gtk_box_append (GTK_BOX (c), sm_cleanup);
    gtk_box_append (GTK_BOX (page), c);
  }

  {
    GtkWidget *c = mc_card ();
    gtk_box_append (GTK_BOX (c), mc_title (acc_tr (ACC_T_SM_UPDATES)));
    sm_updates = mc_label (acc_tr (ACC_T_SM_LOADING), "val", 0);
    gtk_label_set_wrap (GTK_LABEL (sm_updates), TRUE);
    gtk_box_append (GTK_BOX (c), sm_updates);
    gtk_box_append (GTK_BOX (page), c);
  }

  /* Quick Actions */
  {
    GtkWidget *c = mc_card ();
    gtk_box_append (GTK_BOX (c), mc_title ("Quick Actions"));
    struct { const char *label; const char *page; } acts[] = {
      { "Open Storage Manager", "sysmgr" },
      { "Open Process Manager", "sysmgr" },
      { "Open Startup Manager", "sysmgr" },
      { "Open Software Center", "softcenter" },
      { "Open Updates Center", "support" },
      { "Open Hardware Diagnostics", "diagnosis" },
      { "Open Battery Care", "customization" },
    };
    for (int i = 0; i < 7; i++)
      {
        GtkWidget *b = gtk_button_new_with_label (acts[i].label);
        gtk_widget_add_css_class (b, "actionbtn");
        gtk_widget_set_halign (b, GTK_ALIGN_START);
        g_signal_connect (b, "clicked", G_CALLBACK (quick_action_cb), (gpointer) acts[i].page);
        gtk_box_append (GTK_BOX (c), b);
      }
    gtk_box_append (GTK_BOX (page), c);
  }

  /* initial data */
  gchar *st = build_startup_text ();
  if (st) { set (sm_startup, "%s", st); g_free (st); }

  run_health ();
  {
    LoadInfo ld;
    acc_load_read (&ld);
    if (ld.cpu_pct >= 0)
      set (sm_cpu, "CPU load: %.0f%%", ld.cpu_pct);
    else
      set (sm_cpu, "CPU load: unavailable");
    if (ld.mem_pct >= 0)
      set (sm_ram, "RAM: %.0f%% used · %.1f / %.1f GiB", ld.mem_pct, ld.mem_total_gb * ld.mem_pct / 100.0, ld.mem_total_gb);
    else
      set (sm_ram, "RAM: unavailable");
    /* overall health heuristic */
    gboolean warn = ld.disk_pct > 90.0 || ld.mem_pct > 90.0;
    set (sm_overall, warn ? "Needs Attention" : "Healthy");
  }
  gtk_box_append (GTK_BOX (page), st_section ());

  run_updates ();
  g_thread_new ("storage-scan", storage_scan_thread, NULL);
  g_thread_new ("proc-scan", proc_scan_thread, NULL);
  sm_proc_timer = g_timeout_add_seconds (2, proc_timer_cb, NULL);

  return mc_page (page);
}

gboolean
acc_sysmgr_selftest (void)
{
  gchar *dir = g_mkdtemp (g_strdup ("/tmp/asuscraft-sysmgr-XXXXXX"));
  if (!dir)
    return FALSE;
  gchar *hist = g_build_filename (dir, "health_history.txt", NULL);
  gboolean ok = TRUE;

  /* empty history */
  if (!g_file_set_contents (hist, "", -1, NULL))
    ok = FALSE;
  gchar *contents = NULL;
  gsize len = 0;
  if (!g_file_get_contents (hist, &contents, &len, NULL) || len != 0)
    ok = FALSE;
  g_free (contents);

  /* append */
  if (ok)
    {
      g_file_set_contents (hist, "entry1\n", -1, NULL);
      gchar *old = NULL;
      gsize old_len = 0;
      if (g_file_get_contents (hist, &old, &old_len, NULL))
        {
          GString *c = g_string_new (old);
          g_string_append (c, "entry2\n");
          g_file_set_contents (hist, c->str, -1, NULL);
          g_string_free (c, TRUE);
          g_free (old);
        }
      if (!g_file_get_contents (hist, &contents, &len, NULL) ||
          strstr (contents, "entry1") == NULL ||
          strstr (contents, "entry2") == NULL)
        ok = FALSE;
      g_free (contents);
    }

  g_free (hist);
  rmdir (dir);
  g_free (dir);
  return ok;
}

void
acc_sysmgr_teardown (void)
{
  if (sm_proc_timer)
    {
      g_source_remove (sm_proc_timer);
      sm_proc_timer = 0;
    }
  sm_health = sm_storage = sm_startup = sm_proc = sm_cleanup = sm_updates = NULL;
  sm_overall = sm_cpu = sm_ram = sm_reco = NULL;
  st_teardown ();
}
