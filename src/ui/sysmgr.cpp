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
  gchar *out = run_cmd ((const gchar *[]) { "sh", "-c",
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
  gchar *cache = run_cmd ((const gchar *[]) { "sh", "-c",
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
  gchar *failed = run_cmd ((const gchar *[]) { "sh", "-c",
    "systemctl --failed --no-pager 2>/dev/null | sed -n '1,8p'", NULL });
  if (failed && *failed)
    g_string_append_printf (s, "Failed services:\\n%s\\n", g_strstrip (failed));
  else
    g_string_append_printf (s, "%s\\n", acc_tr (ACC_T_SM_NO_FAILED));
  g_free (failed);

  gchar *tools = run_cmd ((const gchar *[]) { "sh", "-c",
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
  gchar *failed_svc = run_cmd ((const gchar *[]) { "sh", "-c", "systemctl --failed --no-pager 2>/dev/null | grep -c failed", NULL });
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
  gchar *out = run_cmd ((const gchar *[]) { "sh", "-c",
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
}
