/* ui/sysmgr.cpp - System Tools hub (PC Manager-inspired)
 *
 * A tabbed hub with four tools, all SAFE BY DEFAULT and USER-LEVEL only:
 *   Health    live CPU/RAM/disk, failed user+system services (read-only),
 *             and plain recommendations.
 *   Storage   disk usage, largest folders in $HOME, and cleanable user
 *             caches with a preview shown BEFORE anything is removed.
 *   Processes the current user's processes, searchable/sortable, with a
 *             confirmed SIGTERM (never SIGKILL, never other users' PIDs).
 *   Startup   the user's own ~/.config/autostart entries, toggled through
 *             the freedesktop "Hidden" key. System autostart is read-only.
 *
 * Nothing here needs the privileged helper: every action is something the
 * signed-in user may already do. Destructive actions always confirm first.
 *
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
#include <glib/gstdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <initializer_list>
#include <vector>

/* ------------------------------------------------------------- state */
static GtkStack  *s_stack;
static GtkWidget *s_tabs[4];

/* health */
static GtkWidget *s_overall, *s_cpu_bar, *s_cpu_lbl, *s_mem_bar, *s_mem_lbl;
static GtkWidget *s_disk_bar, *s_disk_lbl, *s_failed, *s_reco;

/* storage */
static GtkWidget *s_du_bar, *s_du_lbl, *s_large_box, *s_cache_box;
static guint      s_store_timer;

/* processes */
static GtkWidget *s_proc_list, *s_proc_search;
static int        s_proc_sort;           /* 0 = CPU, 1 = memory */
static guint      s_proc_timer;
static gboolean   s_proc_busy;

/* startup */
static GtkWidget *s_startup_list;

/* ------------------------------------------------------------ helpers */
static gchar *
run_cmd (const gchar *const *argv)
{
  gchar *out = NULL;
  if (g_spawn_sync (NULL, (gchar **) argv, NULL,
                    (GSpawnFlags) (G_SPAWN_SEARCH_PATH
                                   | G_SPAWN_STDERR_TO_DEV_NULL),
                    NULL, NULL, &out, NULL, NULL, NULL))
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
set_lbl (GtkWidget *w, const gchar *fmt, ...)
{
  if (!w)
    return;
  va_list ap;
  va_start (ap, fmt);
  gchar *s = g_strdup_vprintf (fmt, ap);
  va_end (ap);
  gtk_label_set_text (GTK_LABEL (w), s);
  g_free (s);
}

static gchar *
human_size (guint64 bytes)
{
  const gchar *u[] = { "B", "KB", "MB", "GB", "TB" };
  gdouble v = (gdouble) bytes;
  gsize i = 0;
  while (v >= 1024.0 && i < G_N_ELEMENTS (u) - 1)
    {
      v /= 1024.0;
      i++;
    }
  return g_strdup_printf (i == 0 ? "%.0f %s" : "%.1f %s", v, u[i]);
}

static void
clear_rows (GtkWidget *listbox)
{
  GtkWidget *c;
  while ((c = gtk_widget_get_first_child (listbox)))
    gtk_list_box_remove (GTK_LIST_BOX (listbox), c);
}

/* a simple "name .... value" row for the metric/section lists */
static GtkWidget *
metric_row (const gchar *name, const gchar *value, GtkWidget **out_val)
{
  GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_margin_top (h, 2);
  gtk_widget_set_margin_bottom (h, 2);
  gtk_box_append (GTK_BOX (h), mc_label (name, "dim", 0));
  GtkWidget *v = mc_label (value ? value : "—", "val", 1);
  gtk_widget_set_hexpand (v, TRUE);
  gtk_box_append (GTK_BOX (h), v);
  if (out_val)
    *out_val = v;
  return h;
}

/* label + progress bar, value set live */
static GtkWidget *
gauge_row (const gchar *name, GtkWidget **out_bar, GtkWidget **out_val)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  GtkWidget *top = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_append (GTK_BOX (top), mc_label (name, "dim", 0));
  GtkWidget *v = mc_label ("—", "val", 1);
  gtk_widget_set_hexpand (v, TRUE);
  gtk_box_append (GTK_BOX (top), v);
  gtk_box_append (GTK_BOX (box), top);
  GtkWidget *bar = gtk_progress_bar_new ();
  gtk_box_append (GTK_BOX (box), bar);
  if (out_bar)
    *out_bar = bar;
  if (out_val)
    *out_val = v;
  return box;
}

/* ---------------------------------------------------------- tab shell */
static void
tab_cb (GtkToggleButton *b, gpointer name)
{
  if (gtk_toggle_button_get_active (b) && s_stack)
    gtk_stack_set_visible_child_name (s_stack, (const gchar *) name);
}

static GtkWidget *
add_tab (GtkWidget *bar, GtkToggleButton *group, AccStr label,
         const gchar *page, gboolean first, int idx)
{
  GtkWidget *b = gtk_toggle_button_new_with_label (acc_tr (label));
  gtk_widget_add_css_class (b, "hubtab");
  if (group)
    gtk_toggle_button_set_group (GTK_TOGGLE_BUTTON (b), group);
  g_signal_connect (b, "toggled", G_CALLBACK (tab_cb), (gpointer) page);
  gtk_box_append (GTK_BOX (bar), b);
  if (first)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b), TRUE);
  s_tabs[idx] = b;
  return b;
}

/* =====================================================================
 * HEALTH
 * ===================================================================== */
static void
refresh_health (void)
{
  if (!s_overall)
    return;
  LoadInfo ld;
  acc_load_read (&ld);

  if (ld.cpu_pct >= 0)
    {
      gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (s_cpu_bar),
                                     ld.cpu_pct / 100.0);
      set_lbl (s_cpu_lbl, "%.0f%%", ld.cpu_pct);
    }
  if (ld.mem_pct >= 0)
    {
      gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (s_mem_bar),
                                     ld.mem_pct / 100.0);
      set_lbl (s_mem_lbl, "%.0f%% · %.1f GiB", ld.mem_pct,
               ld.mem_total_gb * ld.mem_pct / 100.0);
    }
  if (ld.disk_pct >= 0)
    {
      gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (s_disk_bar),
                                     ld.disk_pct / 100.0);
      set_lbl (s_disk_lbl, "%.0f%% · %.0f / %.0f GiB", ld.disk_pct,
               ld.disk_used_gb, ld.disk_total_gb);
    }

  /* failed services (read-only): user scope needs no privilege; the system
   * scope is a plain status read, never a modification */
  gchar *failed = run_cmd ({ "sh", "-c",
    "systemctl --failed --no-legend --no-pager 2>/dev/null | awk '{print $1}'; "
    "systemctl --user --failed --no-legend --no-pager 2>/dev/null | awk '{print $1}'",
    NULL });
  gboolean any_failed = failed && *g_strchomp (failed);
  set_lbl (s_failed, "%s", any_failed ? failed : acc_tr (ACC_T_ST_NO_FAILED));

  /* recommendations */
  GString *r = g_string_new (NULL);
  if (ld.disk_pct > 90.0)
    g_string_append_printf (r, "• %s\n", acc_tr (ACC_T_TAB_STORAGE));
  if (ld.mem_pct > 90.0)
    g_string_append_printf (r, "• %s\n", acc_tr (ACC_T_TAB_PROC));
  if (any_failed)
    g_string_append_printf (r, "• %s\n", acc_tr (ACC_T_ST_FAILED_SVC));
  gboolean healthy = (r->len == 0);
  if (healthy)
    g_string_append (r, acc_tr (ACC_T_ST_HEALTHY));
  set_lbl (s_reco, "%s", r->str);
  g_string_free (r, TRUE);
  g_free (failed);

  gtk_widget_remove_css_class (s_overall, "pill");
  gtk_widget_remove_css_class (s_overall, "on");
  gtk_widget_remove_css_class (s_overall, "soon");
  set_lbl (s_overall, "%s",
           acc_tr (healthy ? ACC_T_ST_HEALTHY : ACC_T_ST_ATTENTION));
  gtk_widget_add_css_class (s_overall, healthy ? "pill on" : "pill soon");
}

static void
health_refresh_cb (GtkButton *b, gpointer u)
{
  (void) b;
  (void) u;
  refresh_health ();
}

static GtkWidget *
build_health (void)
{
  GtkWidget *root = gtk_box_new (GTK_ORIENTATION_VERTICAL, 14);

  GtkWidget *grid = mc_grid (1, 2);

  /* overall + metrics */
  GtkWidget *body = NULL;
  GtkWidget *c = mc_card_titled ("emblem-ok-symbolic",
                                 acc_tr (ACC_T_ST_OVERALL), &body);
  s_overall = mc_label (acc_tr (ACC_T_ST_HEALTHY), "pill on", 0);
  gtk_widget_set_halign (s_overall, GTK_ALIGN_START);
  gtk_box_append (GTK_BOX (body), s_overall);
  gtk_box_append (GTK_BOX (body),
                  gauge_row (acc_tr (ACC_T_ST_CPU), &s_cpu_bar, &s_cpu_lbl));
  gtk_box_append (GTK_BOX (body),
                  gauge_row (acc_tr (ACC_T_ST_MEM), &s_mem_bar, &s_mem_lbl));
  gtk_box_append (GTK_BOX (body),
                  gauge_row (acc_tr (ACC_T_ST_DISKCAP), &s_disk_bar,
                             &s_disk_lbl));
  gtk_flow_box_append (GTK_FLOW_BOX (grid), c);

  /* services + recommendations */
  GtkWidget *body2 = NULL;
  GtkWidget *c2 = mc_card_titled ("dialog-information-symbolic",
                                  acc_tr (ACC_T_ST_RECO), &body2);
  s_reco = mc_label ("—", "val", 0);
  gtk_label_set_wrap (GTK_LABEL (s_reco), TRUE);
  gtk_box_append (GTK_BOX (body2), s_reco);
  gtk_box_append (GTK_BOX (body2),
                  gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));
  gtk_box_append (GTK_BOX (body2), mc_label (acc_tr (ACC_T_ST_FAILED_SVC),
                                             "dim", 0));
  s_failed = mc_label ("—", "val", 0);
  gtk_label_set_wrap (GTK_LABEL (s_failed), TRUE);
  gtk_box_append (GTK_BOX (body2), s_failed);
  gtk_flow_box_append (GTK_FLOW_BOX (grid), c2);

  gtk_box_append (GTK_BOX (root), grid);

  GtkWidget *refresh = gtk_button_new_with_label (acc_tr (ACC_T_ST_REFRESH));
  gtk_widget_add_css_class (refresh, "actionbtn");
  gtk_widget_set_halign (refresh, GTK_ALIGN_START);
  g_signal_connect (refresh, "clicked", G_CALLBACK (health_refresh_cb), NULL);
  gtk_box_append (GTK_BOX (root), refresh);

  return root;
}

/* =====================================================================
 * STORAGE
 * ===================================================================== */
typedef struct { gchar *du_text; gchar *large; } StoreScan;

static gboolean
store_apply (gpointer data)
{
  StoreScan *s = (StoreScan *) data;
  if (s_large_box && s->large)
    {
      clear_rows (s_large_box);
      gchar **lines = g_strsplit (s->large, "\n", -1);
      for (int i = 0; lines[i] && *lines[i]; i++)
        {
          /* "<size>\t<path>" from du */
          gchar **kv = g_strsplit (lines[i], "\t", 2);
          if (kv[0] && kv[1])
            {
              gchar *base = g_path_get_basename (g_strstrip (kv[1]));
              gtk_list_box_append (GTK_LIST_BOX (s_large_box),
                                   metric_row (base, g_strstrip (kv[0]), NULL));
              g_free (base);
            }
          g_strfreev (kv);
        }
      g_strfreev (lines);
    }
  g_free (s->du_text);
  g_free (s->large);
  g_free (s);
  return G_SOURCE_REMOVE;
}

static gpointer
store_scan (gpointer data)
{
  (void) data;
  /* largest top-level folders in $HOME (read-only; one level deep) */
  gchar *large = run_cmd ({ "sh", "-c",
    "du -x -h -d1 \"$HOME\" 2>/dev/null | sort -rh | sed -n '2,7p'", NULL });
  StoreScan *s = g_new0 (StoreScan, 1);
  s->large = large;
  g_idle_add (store_apply, s);
  return NULL;
}

/* list the cleanable user caches with live sizes; returns total bytes */
static guint64
cache_targets (GPtrArray *paths_out)
{
  const gchar *cands[] = {
    g_get_user_cache_dir (),
    NULL, /* trash files */
    NULL, /* thumbnails */
  };
  gchar *trash = g_build_filename (g_get_user_data_dir (), "Trash", "files",
                                   NULL);
  gchar *thumbs = g_build_filename (g_get_user_cache_dir (), "thumbnails",
                                    NULL);
  cands[1] = trash;
  cands[2] = thumbs;

  guint64 total = 0;
  for (gsize i = 0; i < G_N_ELEMENTS (cands); i++)
    {
      if (!cands[i] || !g_file_test (cands[i], G_FILE_TEST_IS_DIR))
        continue;
      const gchar *argv[] = { "du", "-sb", "--", cands[i], NULL };
      gchar *o = run_cmd (argv);
      guint64 b = o ? g_ascii_strtoull (o, NULL, 10) : 0;
      g_free (o);
      total += b;
      if (paths_out)
        g_ptr_array_add (paths_out, g_strdup (cands[i]));
    }
  g_free (trash);
  g_free (thumbs);
  return total;
}

static void
refresh_storage (void)
{
  LoadInfo ld;
  acc_load_read (&ld);
  if (s_du_bar && ld.disk_pct >= 0)
    {
      gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (s_du_bar),
                                     ld.disk_pct / 100.0);
      set_lbl (s_du_lbl, "%.0f%% · %.0f / %.0f GiB", ld.disk_pct,
               ld.disk_used_gb, ld.disk_total_gb);
    }
  /* refresh the cleanable-cache figures (du -sb per target -> human size) */
  if (s_cache_box)
    {
      clear_rows (s_cache_box);
      GPtrArray *paths = g_ptr_array_new_with_free_func (g_free);
      cache_targets (paths);
      for (guint i = 0; i < paths->len; i++)
        {
          const gchar *p = (const gchar *) g_ptr_array_index (paths, i);
          const gchar *argv[] = { "du", "-sb", "--", p, NULL };
          gchar *o = run_cmd (argv);
          guint64 b = o ? g_ascii_strtoull (o, NULL, 10) : 0;
          gchar *sz = human_size (b);
          gchar *base = g_path_get_basename (p);
          gtk_list_box_append (GTK_LIST_BOX (s_cache_box),
                               metric_row (base, sz, NULL));
          g_free (base);
          g_free (sz);
          g_free (o);
        }
      g_ptr_array_unref (paths);
    }
  g_thread_new ("store-scan", store_scan, NULL);
}

static void
clear_dir_contents (const gchar *dir)
{
  GDir *d = g_dir_open (dir, 0, NULL);
  if (!d)
    return;
  const gchar *n;
  while ((n = g_dir_read_name (d)))
    {
      gchar *p = g_build_filename (dir, n, NULL);
      /* argv form (no shell): the path is a computed cache entry, never
       * user text, and "--" stops option parsing */
      const gchar *argv[] = { "rm", "-rf", "--", p, NULL };
      g_spawn_sync (NULL, (gchar **) argv, NULL,
                    (GSpawnFlags) (G_SPAWN_SEARCH_PATH
                                   | G_SPAWN_STDOUT_TO_DEV_NULL
                                   | G_SPAWN_STDERR_TO_DEV_NULL),
                    NULL, NULL, NULL, NULL, NULL, NULL);
      g_free (p);
    }
  g_dir_close (d);
}

static void
cleanup_response (AdwAlertDialog *dlg, const gchar *resp, gpointer u)
{
  (void) dlg;
  (void) u;
  if (g_strcmp0 (resp, "ok") != 0)
    return;
  GPtrArray *paths = g_ptr_array_new_with_free_func (g_free);
  guint64 before = cache_targets (paths);
  for (guint i = 0; i < paths->len; i++)
    clear_dir_contents ((const gchar *) g_ptr_array_index (paths, i));
  g_ptr_array_unref (paths);
  gchar *h = human_size (before);
  gchar *msg = g_strdup_printf ("%s: %s", acc_tr (ACC_T_ST_FREED), h);
  acc_ui_toast (msg);
  g_free (msg);
  g_free (h);
  refresh_storage ();
}

static void
cleanup_clicked (GtkButton *b, gpointer u)
{
  (void) b;
  (void) u;
  GPtrArray *paths = g_ptr_array_new_with_free_func (g_free);
  guint64 total = cache_targets (paths);
  g_ptr_array_unref (paths);
  if (total == 0)
    {
      acc_ui_toast (acc_tr (ACC_T_ST_NOTHING));
      return;
    }
  gchar *h = human_size (total);
  gchar *body = g_strdup_printf ("%s\n(%s)", acc_tr (ACC_T_ST_CLEAN_Q), h);
  AdwAlertDialog *dlg = ADW_ALERT_DIALOG (
      adw_alert_dialog_new (acc_tr (ACC_T_ST_CLEAN), body));
  adw_alert_dialog_add_responses (dlg, "cancel", acc_tr (ACC_T_ST_CANCEL),
                                  "ok", acc_tr (ACC_T_ST_CLEAN), NULL);
  adw_alert_dialog_set_response_appearance (dlg, "ok",
                                            ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_default_response (dlg, "cancel");
  g_signal_connect (dlg, "response", G_CALLBACK (cleanup_response), NULL);
  adw_dialog_present (ADW_DIALOG (dlg), acc_ui_root ());
  g_free (body);
  g_free (h);
}

static GtkWidget *
build_storage (void)
{
  GtkWidget *root = gtk_box_new (GTK_ORIENTATION_VERTICAL, 14);
  GtkWidget *grid = mc_grid (1, 2);

  /* disk usage + largest folders */
  GtkWidget *b1 = NULL;
  GtkWidget *c1 = mc_card_titled ("drive-harddisk-symbolic",
                                  acc_tr (ACC_T_ST_DISK), &b1);
  gtk_box_append (GTK_BOX (b1),
                  gauge_row (acc_tr (ACC_T_ST_DISKCAP), &s_du_bar, &s_du_lbl));
  gtk_box_append (GTK_BOX (b1), mc_label (acc_tr (ACC_T_ST_LARGE), "dim", 0));
  s_large_box = gtk_list_box_new ();
  gtk_widget_add_css_class (s_large_box, "plainlist");
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (s_large_box),
                                   GTK_SELECTION_NONE);
  gtk_box_append (GTK_BOX (b1), s_large_box);
  gtk_flow_box_append (GTK_FLOW_BOX (grid), c1);

  /* cleanable caches + preview/clear */
  GtkWidget *b2 = NULL;
  GtkWidget *c2 = mc_card_titled ("user-trash-symbolic",
                                  acc_tr (ACC_T_ST_CACHES), &b2);
  s_cache_box = gtk_list_box_new ();
  gtk_widget_add_css_class (s_cache_box, "plainlist");
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (s_cache_box),
                                   GTK_SELECTION_NONE);
  gtk_box_append (GTK_BOX (b2), s_cache_box);
  GtkWidget *clean = gtk_button_new_with_label (acc_tr (ACC_T_ST_CLEAN));
  gtk_widget_add_css_class (clean, "primarybtn");
  gtk_widget_set_halign (clean, GTK_ALIGN_START);
  g_signal_connect (clean, "clicked", G_CALLBACK (cleanup_clicked), NULL);
  gtk_box_append (GTK_BOX (b2), clean);
  gtk_flow_box_append (GTK_FLOW_BOX (grid), c2);

  gtk_box_append (GTK_BOX (root), grid);
  return root;
}

/* =====================================================================
 * PROCESSES
 * ===================================================================== */
typedef struct { int pid; gdouble cpu; gdouble mem; gchar *comm; } ProcRow;
typedef struct { GPtrArray *rows; } ProcScan;

static void
proc_free (gpointer p)
{
  ProcRow *r = (ProcRow *) p;
  g_free (r->comm);
  g_free (r);
}

static void
end_response (AdwAlertDialog *dlg, const gchar *resp, gpointer data)
{
  (void) dlg;
  int pid = GPOINTER_TO_INT (data);
  if (g_strcmp0 (resp, "ok") == 0 && pid > 1)
    {
      /* SIGTERM only, and only our own processes (ps already filtered by
       * uid); the kernel refuses anything we do not own */
      if (kill ((pid_t) pid, SIGTERM) == 0)
        acc_ui_toast (acc_tr (ACC_T_ST_ENDED));
    }
}

static void
end_clicked (GtkButton *b, gpointer data)
{
  int pid = GPOINTER_TO_INT (data);
  const gchar *comm = (const gchar *) g_object_get_data (G_OBJECT (b), "comm");
  gchar *body = g_strdup_printf (acc_tr (ACC_T_ST_END_Q),
                                 comm ? comm : "?", pid);
  AdwAlertDialog *dlg = ADW_ALERT_DIALOG (
      adw_alert_dialog_new (acc_tr (ACC_T_ST_END), body));
  adw_alert_dialog_add_responses (dlg, "cancel", acc_tr (ACC_T_ST_CANCEL),
                                  "ok", acc_tr (ACC_T_ST_END), NULL);
  adw_alert_dialog_set_response_appearance (dlg, "ok",
                                            ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_default_response (dlg, "cancel");
  g_signal_connect (dlg, "response", G_CALLBACK (end_response), data);
  adw_dialog_present (ADW_DIALOG (dlg), acc_ui_root ());
  g_free (body);
}

static gint
proc_cmp_cpu (gconstpointer a, gconstpointer b)
{
  const ProcRow *x = *(const ProcRow **) a;
  const ProcRow *y = *(const ProcRow **) b;
  return (y->cpu > x->cpu) - (y->cpu < x->cpu);
}

static gint
proc_cmp_mem (gconstpointer a, gconstpointer b)
{
  const ProcRow *x = *(const ProcRow **) a;
  const ProcRow *y = *(const ProcRow **) b;
  return (y->mem > x->mem) - (y->mem < x->mem);
}

static gboolean
proc_apply (gpointer data)
{
  ProcScan *s = (ProcScan *) data;
  if (!s_proc_list)
    {
      g_ptr_array_unref (s->rows);
      g_free (s);
      s_proc_busy = FALSE;
      return G_SOURCE_REMOVE;
    }
  g_ptr_array_sort (s->rows, s_proc_sort == 0 ? proc_cmp_cpu : proc_cmp_mem);

  const gchar *q = s_proc_search
                     ? gtk_editable_get_text (GTK_EDITABLE (s_proc_search))
                     : "";
  gchar *ql = g_utf8_strdown (q ? q : "", -1);

  clear_rows (s_proc_list);
  guint shown = 0;
  for (guint i = 0; i < s->rows->len && shown < 60; i++)
    {
      ProcRow *r = (ProcRow *) g_ptr_array_index (s->rows, i);
      if (*ql)
        {
          gchar *cl = g_utf8_strdown (r->comm, -1);
          gboolean hit = strstr (cl, ql) != NULL;
          g_free (cl);
          if (!hit)
            continue;
        }
      GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
      gtk_widget_set_margin_top (row, 4);
      gtk_widget_set_margin_bottom (row, 4);
      gtk_widget_set_margin_start (row, 6);
      gtk_widget_set_margin_end (row, 6);
      GtkWidget *name = mc_label (r->comm, "val", 0);
      gtk_widget_set_hexpand (name, TRUE);
      gtk_label_set_ellipsize (GTK_LABEL (name), PANGO_ELLIPSIZE_END);
      gtk_box_append (GTK_BOX (row), name);
      gchar *cpu = g_strdup_printf ("%.1f%%", r->cpu);
      gchar *mem = g_strdup_printf ("%.1f%%", r->mem);
      GtkWidget *lc = mc_label (cpu, "dim", 1);
      GtkWidget *lm = mc_label (mem, "dim", 1);
      gtk_widget_set_size_request (lc, 56, -1);
      gtk_widget_set_size_request (lm, 56, -1);
      gtk_box_append (GTK_BOX (row), lc);
      gtk_box_append (GTK_BOX (row), lm);
      g_free (cpu);
      g_free (mem);
      GtkWidget *end = gtk_button_new_from_icon_name
          ("window-close-symbolic");
      gtk_widget_add_css_class (end, "flat");
      gtk_widget_set_tooltip_text (end, acc_tr (ACC_T_ST_END));
      g_object_set_data_full (G_OBJECT (end), "comm", g_strdup (r->comm),
                              g_free);
      g_signal_connect (end, "clicked", G_CALLBACK (end_clicked),
                        GINT_TO_POINTER (r->pid));
      gtk_box_append (GTK_BOX (row), end);
      gtk_list_box_append (GTK_LIST_BOX (s_proc_list), row);
      shown++;
    }
  g_free (ql);
  g_ptr_array_unref (s->rows);
  g_free (s);
  s_proc_busy = FALSE;
  return G_SOURCE_REMOVE;
}

static gpointer
proc_scan (gpointer data)
{
  (void) data;
  gchar uid[16];
  g_snprintf (uid, sizeof uid, "%u", (unsigned) getuid ());
  /* own processes only */
  const gchar *argv[] = { "ps", "-u", uid, "-o",
                          "pid=,pcpu=,pmem=,comm=", NULL };
  gchar *out = run_cmd (argv);
  GPtrArray *rows = g_ptr_array_new_with_free_func (proc_free);
  if (out)
    {
      gchar **lines = g_strsplit (out, "\n", -1);
      for (int i = 0; lines[i]; i++)
        {
          if (!*g_strstrip (lines[i]))
            continue;
          /* columns are whitespace-padded; sscanf skips runs of spaces,
           * and %[^\n] captures a comm that may itself contain spaces */
          long pid = 0;
          double cpu = 0, mem = 0;
          char comm[256] = { 0 };
          if (sscanf (lines[i], "%ld %lf %lf %255[^\n]",
                      &pid, &cpu, &mem, comm) == 4 && pid > 0)
            {
              ProcRow *r = g_new0 (ProcRow, 1);
              r->pid = (int) pid;
              r->cpu = cpu;
              r->mem = mem;
              r->comm = g_strdup (g_strstrip (comm));
              g_ptr_array_add (rows, r);
            }
        }
      g_strfreev (lines);
      g_free (out);
    }
  ProcScan *s = g_new0 (ProcScan, 1);
  s->rows = rows;
  g_idle_add (proc_apply, s);
  return NULL;
}

static void
proc_kick (void)
{
  if (s_proc_busy)
    return;
  s_proc_busy = TRUE;
  g_thread_new ("proc-scan", proc_scan, NULL);
}

static gboolean
proc_timer_cb (gpointer u)
{
  (void) u;
  if (!s_proc_list || !GTK_IS_WIDGET (s_proc_list))
    return G_SOURCE_REMOVE;
  if (gtk_widget_get_mapped (s_proc_list))
    proc_kick ();
  return G_SOURCE_CONTINUE;
}

static void
proc_search_cb (GtkEditable *e, gpointer u)
{
  (void) e;
  (void) u;
  proc_kick ();
}

static void
proc_sort_cb (GtkToggleButton *b, gpointer which)
{
  if (gtk_toggle_button_get_active (b))
    {
      s_proc_sort = GPOINTER_TO_INT (which);
      proc_kick ();
    }
}

static GtkWidget *
build_proc (void)
{
  GtkWidget *root = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);

  GtkWidget *tools = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  s_proc_search = gtk_search_entry_new ();
  gtk_search_entry_set_placeholder_text (GTK_SEARCH_ENTRY (s_proc_search),
                                         acc_tr (ACC_T_ST_SEARCH));
  gtk_widget_set_hexpand (s_proc_search, TRUE);
  g_signal_connect (s_proc_search, "search-changed",
                    G_CALLBACK (proc_search_cb), NULL);
  gtk_box_append (GTK_BOX (tools), s_proc_search);
  GtkWidget *sc = gtk_toggle_button_new_with_label (acc_tr (ACC_T_ST_SORTCPU));
  gtk_widget_add_css_class (sc, "hubtab");
  GtkWidget *sm = gtk_toggle_button_new_with_label (acc_tr (ACC_T_ST_SORTMEM));
  gtk_widget_add_css_class (sm, "hubtab");
  gtk_toggle_button_set_group (GTK_TOGGLE_BUTTON (sm), GTK_TOGGLE_BUTTON (sc));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (sc), TRUE);
  g_signal_connect (sc, "toggled", G_CALLBACK (proc_sort_cb),
                    GINT_TO_POINTER (0));
  g_signal_connect (sm, "toggled", G_CALLBACK (proc_sort_cb),
                    GINT_TO_POINTER (1));
  gtk_box_append (GTK_BOX (tools), sc);
  gtk_box_append (GTK_BOX (tools), sm);
  gtk_box_append (GTK_BOX (root), tools);

  /* column header */
  GtkWidget *hdr = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_margin_start (hdr, 6);
  gtk_widget_set_margin_end (hdr, 6);
  GtkWidget *hn = mc_label (acc_tr (ACC_T_ST_COL_PROC), "overline", 0);
  gtk_widget_set_hexpand (hn, TRUE);
  gtk_box_append (GTK_BOX (hdr), hn);
  GtkWidget *hc = mc_label (acc_tr (ACC_T_ST_COL_CPU), "overline", 1);
  GtkWidget *hm = mc_label (acc_tr (ACC_T_ST_COL_MEM), "overline", 1);
  gtk_widget_set_size_request (hc, 56, -1);
  gtk_widget_set_size_request (hm, 56, -1);
  gtk_box_append (GTK_BOX (hdr), hc);
  gtk_box_append (GTK_BOX (hdr), hm);
  gtk_box_append (GTK_BOX (hdr), mc_label ("   ", NULL, 1));
  gtk_box_append (GTK_BOX (root), hdr);

  GtkWidget *card = mc_card ();
  gtk_widget_set_vexpand (card, TRUE);
  GtkWidget *scroll = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand (scroll, TRUE);
  s_proc_list = gtk_list_box_new ();
  gtk_widget_add_css_class (s_proc_list, "plainlist");
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (s_proc_list),
                                   GTK_SELECTION_NONE);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), s_proc_list);
  gtk_box_append (GTK_BOX (card), scroll);
  gtk_box_append (GTK_BOX (root), card);

  return root;
}

/* =====================================================================
 * STARTUP
 * ===================================================================== */
static gchar *
autostart_dir (void)
{
  return g_build_filename (g_get_user_config_dir (), "autostart", NULL);
}

static void
startup_toggle (GtkSwitch *sw, GParamSpec *ps, gpointer path)
{
  (void) ps;
  const gchar *file = (const gchar *) path;
  gboolean enabled = gtk_switch_get_active (sw);
  GKeyFile *kf = g_key_file_new ();
  if (g_key_file_load_from_file (kf, file,
                                 (GKeyFileFlags) (G_KEY_FILE_KEEP_COMMENTS
                                                  | G_KEY_FILE_KEEP_TRANSLATIONS),
                                 NULL))
    {
      /* freedesktop: Hidden=true means "do not autostart" */
      g_key_file_set_boolean (kf, "Desktop Entry", "Hidden", !enabled);
      g_key_file_save_to_file (kf, file, NULL);
    }
  g_key_file_free (kf);
}

static void
rebuild_startup (void)
{
  if (!s_startup_list)
    return;
  clear_rows (s_startup_list);

  gchar *dir = autostart_dir ();
  GDir *d = g_dir_open (dir, 0, NULL);
  guint count = 0;
  if (d)
    {
      const gchar *n;
      while ((n = g_dir_read_name (d)))
        {
          if (!g_str_has_suffix (n, ".desktop"))
            continue;
          gchar *path = g_build_filename (dir, n, NULL);
          GKeyFile *kf = g_key_file_new ();
          gchar *name = NULL;
          gboolean hidden = FALSE;
          if (g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, NULL))
            {
              name = g_key_file_get_locale_string (kf, "Desktop Entry",
                                                   "Name", NULL, NULL);
              hidden = g_key_file_get_boolean (kf, "Desktop Entry", "Hidden",
                                               NULL);
            }
          g_key_file_free (kf);

          GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
          gtk_widget_set_margin_top (row, 6);
          gtk_widget_set_margin_bottom (row, 6);
          gtk_widget_set_margin_start (row, 6);
          gtk_widget_set_margin_end (row, 6);
          GtkWidget *col = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
          gtk_widget_set_hexpand (col, TRUE);
          gtk_box_append (GTK_BOX (col),
                          mc_label (name && *name ? name : n, "val", 0));
          gtk_box_append (GTK_BOX (col),
                          mc_label (acc_tr (hidden ? ACC_T_ST_DISABLED
                                                   : ACC_T_ST_ENABLED),
                                    "dim", 0));
          gtk_box_append (GTK_BOX (row), col);
          GtkWidget *sw = gtk_switch_new ();
          gtk_switch_set_active (GTK_SWITCH (sw), !hidden);
          gtk_widget_set_valign (sw, GTK_ALIGN_CENTER);
          g_object_set_data_full (G_OBJECT (sw), "path", path, g_free);
          g_signal_connect (sw, "notify::active",
                            G_CALLBACK (startup_toggle), path);
          gtk_box_append (GTK_BOX (row), sw);
          gtk_list_box_append (GTK_LIST_BOX (s_startup_list), row);
          g_free (name);
          count++;
        }
      g_dir_close (d);
    }
  g_free (dir);

  if (count == 0)
    gtk_list_box_append (GTK_LIST_BOX (s_startup_list),
                         mc_label (acc_tr (ACC_T_ST_START_NONE), "dim", 0));
}

static GtkWidget *
build_startup (void)
{
  GtkWidget *root = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);

  GtkWidget *b = NULL;
  GtkWidget *c = mc_card_titled ("system-run-symbolic",
                                 acc_tr (ACC_T_ST_STARTAPPS), &b);
  s_startup_list = gtk_list_box_new ();
  gtk_widget_add_css_class (s_startup_list, "plainlist");
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (s_startup_list),
                                   GTK_SELECTION_NONE);
  gtk_box_append (GTK_BOX (b), s_startup_list);
  GtkWidget *impact = mc_label (acc_tr (ACC_T_ST_START_IMPACT), "foot", 0);
  gtk_label_set_wrap (GTK_LABEL (impact), TRUE);
  gtk_box_append (GTK_BOX (b), impact);
  gtk_box_append (GTK_BOX (root), c);

  GtkWidget *note = mc_label (acc_tr (ACC_T_ST_PROTECTED), "foot", 0);
  gtk_label_set_wrap (GTK_LABEL (note), TRUE);
  gtk_box_append (GTK_BOX (root), note);

  rebuild_startup ();
  return root;
}

/* =====================================================================
 * page
 * ===================================================================== */
GtkWidget *
acc_page_sysmgr (AccUI *u)
{
  (void) u;
  GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  /* header */
  GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_add_css_class (h, "pagehead");
  GtkWidget *col = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
  gtk_widget_set_hexpand (col, TRUE);
  gtk_widget_set_valign (col, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (col), mc_label ("ASUSCraft", "eyebrow", 0));
  gtk_box_append (GTK_BOX (col), mc_label (acc_tr (ACC_T_NAV_TOOLS),
                                           "pagetitle", 0));
  GtkWidget *sub = mc_label (acc_tr (ACC_T_ST_SUB), "pagesub", 0);
  gtk_label_set_wrap (GTK_LABEL (sub), TRUE);
  gtk_box_append (GTK_BOX (col), sub);
  gtk_box_append (GTK_BOX (h), col);
  gtk_box_append (GTK_BOX (page), h);

  /* tab bar */
  GtkWidget *bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class (bar, "hubbar");
  gtk_widget_set_halign (bar, GTK_ALIGN_START);
  gtk_widget_set_margin_start (bar, 22);
  gtk_widget_set_margin_end (bar, 22);
  gtk_widget_set_margin_bottom (bar, 10);
  gtk_box_append (GTK_BOX (page), bar);

  GtkWidget *stack = gtk_stack_new ();
  s_stack = GTK_STACK (stack);
  gtk_stack_set_transition_type (GTK_STACK (stack),
                                 GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  gtk_widget_set_vexpand (stack, TRUE);
  gtk_stack_add_named (GTK_STACK (stack), build_health (), "health");
  gtk_stack_add_named (GTK_STACK (stack), build_storage (), "storage");
  gtk_stack_add_named (GTK_STACK (stack), build_proc (), "proc");
  gtk_stack_add_named (GTK_STACK (stack), build_startup (), "startup");
  gtk_box_append (GTK_BOX (page), stack);

  GtkToggleButton *g0;
  add_tab (bar, NULL, ACC_T_TAB_HEALTH, "health", TRUE, 0);
  g0 = GTK_TOGGLE_BUTTON (s_tabs[0]);
  add_tab (bar, g0, ACC_T_TAB_STORAGE, "storage", FALSE, 1);
  add_tab (bar, g0, ACC_T_TAB_PROC, "proc", FALSE, 2);
  add_tab (bar, g0, ACC_T_TAB_STARTUP, "startup", FALSE, 3);

  /* initial data */
  refresh_health ();
  refresh_storage ();
  proc_kick ();
  s_proc_timer = g_timeout_add_seconds (2, proc_timer_cb, NULL);
  s_store_timer = g_timeout_add_seconds (15, [] (gpointer) -> gboolean {
      if (!s_du_bar || !GTK_IS_WIDGET (s_du_bar))
        return G_SOURCE_REMOVE;
      if (gtk_widget_get_mapped (s_du_bar))
        refresh_health ();
      return G_SOURCE_CONTINUE;
    }, NULL);

  return mc_page (page);
}

gboolean
acc_sysmgr_selftest (void)
{
  /* human_size formats the byte thresholds correctly */
  gboolean ok = TRUE;
  struct { guint64 b; const gchar *want; } cases[] = {
    { 0, "0 B" }, { 1023, "1023 B" }, { 1024, "1.0 KB" },
    { 1048576, "1.0 MB" }, { 1073741824, "1.0 GB" },
  };
  for (gsize i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      gchar *s = human_size (cases[i].b);
      if (g_strcmp0 (s, cases[i].want) != 0)
        ok = FALSE;
      g_free (s);
    }
  return ok;
}

void
acc_sysmgr_teardown (void)
{
  if (s_proc_timer)
    {
      g_source_remove (s_proc_timer);
      s_proc_timer = 0;
    }
  if (s_store_timer)
    {
      g_source_remove (s_store_timer);
      s_store_timer = 0;
    }
  s_stack = NULL;
  for (guint i = 0; i < G_N_ELEMENTS (s_tabs); i++)
    s_tabs[i] = NULL;
  s_overall = s_cpu_bar = s_cpu_lbl = s_mem_bar = s_mem_lbl = NULL;
  s_disk_bar = s_disk_lbl = s_failed = s_reco = NULL;
  s_du_bar = s_du_lbl = s_large_box = s_cache_box = NULL;
  s_proc_list = s_proc_search = NULL;
  s_startup_list = NULL;
  s_proc_busy = FALSE;
}
