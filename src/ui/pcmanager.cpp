/* ui/pcmanager.cpp - PC Manager (read-only Stage 1)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "pcmanager.h"
#include "theme.h"
#include "mcui.h"
#include "window.h"
#include "../i18n/i18n.h"
#include "../probe/load.h"
#include "../probe/sysinfo.h"
#include <adwaita.h>
#include <glib.h>
#include <string.h>
#include <stdarg.h>

static GtkWidget *pm_overall, *pm_storage, *pm_network, *pm_quick;
static GtkWidget *pm_cache, *pm_browser;
static GtkWidget *pm_fw, *pm_selinux, *pm_ports, *pm_tools, *pm_secupd, *pm_latency;
static GtkWidget *pm_scan_result;
static gboolean scan_running = FALSE;

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

typedef struct { gchar *text; GtkWidget *target; } PmResult;

static gboolean
update_idle (gpointer data)
{
  PmResult *r = (PmResult *) data;
  if (r && r->target && r->text)
    set (r->target, "%s", r->text);
  g_free (r ? r->text : NULL);
  g_free (r);
  return G_SOURCE_REMOVE;
}

static gpointer
scan_network (gpointer data)
{
  (void) data;
  gchar *iface = run_cmd ((const gchar *[]) { "sh", "-c", "ip -o link show up | grep -v lo | head -1 | cut -d: -f2 | tr -d ' '", NULL });
  gchar *ip = run_cmd ((const gchar *[]) { "sh", "-c", "ip -4 -o addr show up | grep -v '127.0.0.1' | head -1 | awk '{print $4}'", NULL });
  gchar *gw = run_cmd ((const gchar *[]) { "sh", "-c", "ip route | awk '/default/ {print $3; exit}'", NULL });
  gchar *dns = run_cmd ((const gchar *[]) { "sh", "-c", "grep nameserver /etc/resolv.conf | head -2 | tr '\\n' ' '", NULL });
  gchar *ping = run_cmd ((const gchar *[]) { "sh", "-c", "ping -c 1 -W 2 1.1.1.1 >/dev/null 2>&1 && echo 'Internet: reachable' || echo 'Internet: unreachable'", NULL });
  gchar *dns_test = run_cmd ((const gchar *[]) { "sh", "-c", "getent hosts example.com >/dev/null 2>&1 && echo 'DNS: OK' || echo 'DNS: failed'", NULL });

  GString *s = g_string_new (NULL);
  g_string_append_printf (s, "Interface: %s\n", iface ? g_strstrip (iface) : "unknown");
  g_string_append_printf (s, "IP: %s\n", ip ? g_strstrip (ip) : "unknown");
  g_string_append_printf (s, "Gateway: %s\n", gw ? g_strstrip (gw) : "unknown");
  g_string_append_printf (s, "DNS: %s\n", dns ? g_strstrip (dns) : "unknown");
  if (ping) g_string_append (s, ping);
  g_string_append (s, "\\n");
  if (dns_test) g_string_append (s, dns_test);

  g_free (iface); g_free (ip); g_free (gw); g_free (dns); g_free (ping); g_free (dns_test);

  PmResult *r = g_new0 (PmResult, 1);
  r->text = g_strdup (s->str);
  r->target = pm_network;
  g_string_free (s, TRUE);
  g_idle_add (update_idle, r);
  return NULL;
}

static gpointer
scan_storage (gpointer data)
{
  (void) data;
  LoadInfo ld;
  acc_load_read (&ld);
  GString *s = g_string_new (NULL);
  if (ld.disk_total_gb > 0)
    g_string_append_printf (s, "Disk: %.0f%% used · %.0f / %.0f GB\\n", ld.disk_pct, ld.disk_used_gb, ld.disk_total_gb);
  else
    g_string_append (s, "Disk usage unavailable\\n");
  gchar *fs = run_cmd ((const gchar *[]) { "sh", "-c", "findmnt -no FSTYPE / 2>/dev/null || echo unknown", NULL });
  if (fs) { g_string_append_printf (s, "Filesystem: %s\\n", g_strstrip (fs)); g_free (fs); }
  gchar *nvme = run_cmd ((const gchar *[]) { "sh", "-c", "lsblk -d -o NAME,MODEL,SIZE 2>/dev/null | grep -i nvme | head -3", NULL });
  if (nvme && *nvme) { g_string_append (s, "NVMe:\\n"); g_string_append (s, nvme); }
  g_free (nvme);
  PmResult *r = g_new0 (PmResult, 1);
  r->text = g_strdup (s->str);
  r->target = pm_storage;
  g_string_free (s, TRUE);
  g_idle_add (update_idle, r);
  return NULL;
}

static void
quick_nav_cb (GtkButton *b, gpointer p)
{
  (void) b;
  acc_ui_nav_to ((const gchar *) p);
}

static gpointer
scan_firewall (gpointer data)
{
  (void) data;
  gchar *state = run_cmd ((const gchar *[]) { "sh", "-c", "command -v firewall-cmd >/dev/null && firewall-cmd --state 2>/dev/null || echo 'firewalld unavailable'", NULL });
  gchar *zone = run_cmd ((const gchar *[]) { "sh", "-c", "firewall-cmd --get-default-zone 2>/dev/null || echo 'unknown'", NULL });
  gchar *zones = run_cmd ((const gchar *[]) { "sh", "-c", "firewall-cmd --get-active-zones 2>/dev/null | head -20", NULL });
  GString *s = g_string_new (NULL);
  g_string_append_printf (s, "State: %s\nDefault zone: %s\n", state ? g_strstrip (state) : "unknown", zone ? g_strstrip (zone) : "unknown");
  if (zones && *zones) g_string_append (s, zones);
  g_free (state); g_free (zone); g_free (zones);
  PmResult *r = g_new0 (PmResult, 1);
  r->text = g_strdup (s->str); r->target = pm_fw;
  g_string_free (s, TRUE);
  g_idle_add (update_idle, r);
  return NULL;
}

static gpointer
scan_selinux (gpointer data)
{
  (void) data;
  gchar *mode = run_cmd ((const gchar *[]) { "sh", "-c", "command -v getenforce >/dev/null && getenforce 2>/dev/null || echo 'SELinux unavailable'", NULL });
  gchar *status = run_cmd ((const gchar *[]) { "sh", "-c", "command -v sestatus >/dev/null && sestatus 2>/dev/null | head -8 || echo 'sestatus unavailable'", NULL });
  GString *s = g_string_new (NULL);
  g_string_append_printf (s, "Mode: %s\n", mode ? g_strstrip (mode) : "unknown");
  if (status) g_string_append (s, status);
  g_free (mode); g_free (status);
  PmResult *r = g_new0 (PmResult, 1);
  r->text = g_strdup (s->str); r->target = pm_selinux;
  g_string_free (s, TRUE);
  g_idle_add (update_idle, r);
  return NULL;
}

static gpointer
scan_ports (gpointer data)
{
  (void) data;
  gchar *out = run_cmd ((const gchar *[]) { "sh", "-c", "ss -tuln 2>/dev/null | head -30", NULL });
  PmResult *r = g_new0 (PmResult, 1);
  r->text = out ? g_strdup (out) : g_strdup ("ss unavailable");
  r->target = pm_ports;
  g_free (out);
  g_idle_add (update_idle, r);
  return NULL;
}

static gpointer
scan_tools (gpointer data)
{
  (void) data;
  gchar *out = run_cmd ((const gchar *[]) { "sh", "-c",
    "for t in clamscan freshclam lynis rkhunter chkrootkit; do command -v $t >/dev/null && echo \\\"$t: installed\\\" || echo \\\"$t: missing\\\"; done", NULL });
  PmResult *r = g_new0 (PmResult, 1);
  r->text = out ? g_strdup (out) : g_strdup ("tool check unavailable");
  r->target = pm_tools;
  g_free (out);
  g_idle_add (update_idle, r);
  return NULL;
}

static gpointer
scan_secupd (gpointer data)
{
  (void) data;
  gchar *out = run_cmd ((const gchar *[]) { "sh", "-c",
    "command -v dnf >/dev/null && dnf check-update -q 2>/dev/null | head -10 || echo 'DNF unavailable'", NULL });
  PmResult *r = g_new0 (PmResult, 1);
  r->text = out ? g_strdup (out) : g_strdup ("unavailable");
  r->target = pm_secupd;
  g_free (out);
  g_idle_add (update_idle, r);
  return NULL;
}

static gpointer
scan_latency (gpointer data)
{
  (void) data;
  gchar *out = run_cmd ((const gchar *[]) { "sh", "-c", "ping -c 3 -W 2 1.1.1.1 2>/dev/null | tail -2", NULL });
  PmResult *r = g_new0 (PmResult, 1);
  r->text = out ? g_strdup (out) : g_strdup ("ping unavailable");
  r->target = pm_latency;
  g_free (out);
  g_idle_add (update_idle, r);
  return NULL;
}

static gpointer
run_scan_thread (gpointer data)
{
  gint idx = GPOINTER_TO_INT (data);
  const gchar *const *argv = NULL;
  if (idx == 0)
    argv = (const gchar *const[]) { "timeout", "120", "clamscan", "-r", "--infected", g_get_home_dir (), NULL };
  else if (idx == 1)
    argv = (const gchar *const[]) { "timeout", "300", "lynis", "audit", "system", "--quick", NULL };
  else if (idx == 2)
    argv = (const gchar *const[]) { "timeout", "300", "rkhunter", "--check", "--sk", NULL };
  else
    argv = (const gchar *const[]) { "timeout", "300", "chkrootkit", NULL };

  gchar *out = run_cmd (argv);
  if (pm_scan_result && GTK_IS_WIDGET (pm_scan_result))
    {
      gchar *txt = out ? g_strdup (out) : g_strdup ("Scan failed or tool unavailable");
      /* trim to last 40 lines */
      gchar **lines = g_strsplit (txt, "\n", -1);
      GString *trimmed = g_string_new (NULL);
      gint n = 0;
      while (lines[n]) n++;
      gint start = n > 40 ? n - 40 : 0;
      for (gint i = start; i < n; i++)
        if (lines[i])
          g_string_append_printf (trimmed, "%s\n", lines[i]);
      g_strfreev (lines);
      g_free (txt);
      txt = g_strdup (trimmed->str);
      g_string_free (trimmed, TRUE);
      g_idle_add ((GSourceFunc) (+[](gpointer p)->gboolean {
        if (pm_scan_result && GTK_IS_WIDGET (pm_scan_result))
          set (pm_scan_result, "%s", (const gchar *) p);
        g_free (p);
        return G_SOURCE_REMOVE;
      }), txt);
    }
  g_free (out);
  scan_running = FALSE;
  return NULL;
}

static void
scan_button_cb (GtkButton *b, gpointer p)
{
  (void) b;
  if (scan_running)
    return;
  GtkAlertDialog *dlg = gtk_alert_dialog_new ("%s", "Run this security scan?");
  const gchar *btns[] = { "Run", "Cancel", NULL };
  gtk_alert_dialog_set_buttons (dlg, btns);
  gtk_alert_dialog_set_cancel_button (dlg, 1);
  gtk_alert_dialog_set_default_button (dlg, 1);
  gtk_alert_dialog_choose (dlg, GTK_WINDOW (acc_ui_root ()), NULL,
    (GAsyncReadyCallback) (+[](GObject *src, GAsyncResult *res, gpointer data) {
      GError *e = NULL;
      int choice = gtk_alert_dialog_choose_finish (GTK_ALERT_DIALOG (src), res, &e);
      if (e || choice != 0) { g_clear_error (&e); return; }
      scan_running = TRUE;
      g_thread_new ("sec-scan", run_scan_thread, data);
    }), p);
  g_object_unref (dlg);
}


static gpointer
scan_cache (gpointer data)
{
  (void) data;
  gchar *cache = run_cmd ((const gchar *[]) { "sh", "-c", "du -sh ~/.cache 2>/dev/null | cut -f1", NULL });
  gchar *thumb = run_cmd ((const gchar *[]) { "sh", "-c", "du -sh ~/.cache/thumbnails 2>/dev/null | cut -f1", NULL });
  gchar *tmp = run_cmd ((const gchar *[]) { "sh", "-c", "du -sh /tmp 2>/dev/null | cut -f1", NULL });
  GString *s = g_string_new (NULL);
  g_string_append_printf (s, "User cache: %s\n", cache ? g_strstrip (cache) : "unavailable");
  g_string_append_printf (s, "Thumbnails: %s\n", thumb ? g_strstrip (thumb) : "unavailable");
  g_string_append_printf (s, "Temp (/tmp): %s\n", tmp ? g_strstrip (tmp) : "unavailable");
  g_free (cache); g_free (thumb); g_free (tmp);
  PmResult *r = g_new0 (PmResult, 1);
  r->text = g_strdup (s->str); r->target = pm_cache;
  g_string_free (s, TRUE);
  g_idle_add (update_idle, r);
  return NULL;
}

static gpointer
scan_browser (gpointer data)
{
  (void) data;
  GString *s = g_string_new (NULL);
  const gchar *browsers[] = { "firefox", "chromium", "google-chrome", "brave-browser", NULL };
  for (int i = 0; browsers[i]; i++)
    {
      gchar *path = g_strdup_printf ("%s/.config/%s", g_get_home_dir (), browsers[i]);
      if (g_file_test (path, G_FILE_TEST_IS_DIR))
        {
          gchar *cmd = g_strdup_printf ("du -sh %s 2>/dev/null | cut -f1", path);
          gchar *size = run_cmd ((const gchar *[]) { "sh", "-c", cmd, NULL });
          g_free (cmd);
          g_string_append_printf (s, "%s: %s\n", browsers[i], size ? g_strstrip (size) : "unknown");
          g_free (size);
        }
      g_free (path);
    }
  if (s->len == 0)
    g_string_append (s, "No supported browsers detected.\n");
  g_string_append (s, "\nPrivacy note: only local profile sizes are inspected.\nBrowsing history, passwords, and form data are never read.");
  PmResult *r = g_new0 (PmResult, 1);
  r->text = g_strdup (s->str); r->target = pm_browser;
  g_string_free (s, TRUE);
  g_idle_add (update_idle, r);
  return NULL;
}

GtkWidget *
acc_page_pcmanager (AccUI *u)
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
    gtk_box_append (GTK_BOX (col), mc_label (acc_tr (ACC_T_PC_MANAGER), "pagetitle", 0));
    GtkWidget *sub = mc_label (acc_tr (ACC_T_PC_SUB), "pagesub", 0);
    gtk_label_set_wrap (GTK_LABEL (sub), TRUE);
    gtk_label_set_max_width_chars (GTK_LABEL (sub), 80);
    gtk_box_append (GTK_BOX (col), sub);
    gtk_box_append (GTK_BOX (h), col);
    gtk_box_append (GTK_BOX (page), h);
  }

  {
    GtkWidget *c = mc_card ();
    gtk_box_append (GTK_BOX (c), mc_title (acc_tr (ACC_T_PC_OVERALL)));
    pm_overall = mc_label ("Loading…", "val", 0);
    gtk_label_set_wrap (GTK_LABEL (pm_overall), TRUE);
    gtk_box_append (GTK_BOX (c), pm_overall);
    gtk_box_append (GTK_BOX (page), c);
  }

  {
    GtkWidget *c = mc_card ();
    gtk_box_append (GTK_BOX (c), mc_title (acc_tr (ACC_T_PC_STORAGE)));
    pm_storage = mc_label ("Loading…", "val", 0);
    gtk_label_set_wrap (GTK_LABEL (pm_storage), TRUE);
    gtk_box_append (GTK_BOX (c), pm_storage);
    gtk_box_append (GTK_BOX (page), c);
  }

  {
    GtkWidget *c = mc_card ();
    gtk_box_append (GTK_BOX (c), mc_title (acc_tr (ACC_T_PC_NETWORK)));
    pm_network = mc_label ("Loading…", "val", 0);
    gtk_label_set_wrap (GTK_LABEL (pm_network), TRUE);
    gtk_box_append (GTK_BOX (c), pm_network);
    gtk_box_append (GTK_BOX (page), c);
  }

  {
    GtkWidget *c = mc_card ();
    gtk_box_append (GTK_BOX (c), mc_title (acc_tr (ACC_T_PC_SECURITY)));
    pm_fw = mc_label ("Loading…", "val", 0);
    gtk_label_set_wrap (GTK_LABEL (pm_fw), TRUE);
    gtk_box_append (GTK_BOX (c), pm_fw);
    pm_selinux = mc_label ("Loading…", "val", 0);
    gtk_label_set_wrap (GTK_LABEL (pm_selinux), TRUE);
    gtk_box_append (GTK_BOX (c), pm_selinux);
    pm_ports = mc_label ("Loading…", "val", 0);
    gtk_label_set_wrap (GTK_LABEL (pm_ports), TRUE);
    gtk_box_append (GTK_BOX (c), pm_ports);
    pm_tools = mc_label ("Loading…", "val", 0);
    gtk_label_set_wrap (GTK_LABEL (pm_tools), TRUE);
    gtk_box_append (GTK_BOX (c), pm_tools);
    pm_secupd = mc_label ("Loading…", "val", 0);
    gtk_label_set_wrap (GTK_LABEL (pm_secupd), TRUE);
    gtk_box_append (GTK_BOX (c), pm_secupd);
    pm_latency = mc_label ("Loading…", "val", 0);
    gtk_label_set_wrap (GTK_LABEL (pm_latency), TRUE);
    gtk_box_append (GTK_BOX (c), pm_latency);
    gtk_box_append (GTK_BOX (page), c);
  }

  {
    GtkWidget *c = mc_card ();
    gtk_box_append (GTK_BOX (c), mc_title (acc_tr (ACC_T_PC_CACHE)));
    pm_cache = mc_label ("Loading…", "val", 0);
    gtk_label_set_wrap (GTK_LABEL (pm_cache), TRUE);
    gtk_box_append (GTK_BOX (c), pm_cache);
    gtk_box_append (GTK_BOX (page), c);
  }

  {
    GtkWidget *c = mc_card ();
    gtk_box_append (GTK_BOX (c), mc_title (acc_tr (ACC_T_PC_BROWSER)));
    pm_browser = mc_label ("Loading…", "val", 0);
    gtk_label_set_wrap (GTK_LABEL (pm_browser), TRUE);
    gtk_box_append (GTK_BOX (c), pm_browser);
    gtk_box_append (GTK_BOX (page), c);
  }

  {
    GtkWidget *c = mc_card ();
    gtk_box_append (GTK_BOX (c), mc_title (acc_tr (ACC_T_PC_SEC_SCANS)));
    pm_scan_result = mc_label ("No scan run yet.", "val", 0);
    gtk_label_set_wrap (GTK_LABEL (pm_scan_result), TRUE);
    gtk_box_append (GTK_BOX (c), pm_scan_result);

    const char *labels[] = { "Scan with ClamAV", "Run Lynis audit", "Run rkhunter", "Run chkrootkit" };
    for (int i = 0; i < 4; i++)
      {
        GtkWidget *b = gtk_button_new_with_label (labels[i]);
        gtk_widget_add_css_class (b, "actionbtn");
        gtk_widget_set_halign (b, GTK_ALIGN_START);
        g_signal_connect (b, "clicked", G_CALLBACK (scan_button_cb), GINT_TO_POINTER (i));
        gtk_box_append (GTK_BOX (c), b);
      }
    gtk_box_append (GTK_BOX (page), c);
  }

  {
    GtkWidget *c = mc_card ();
    gtk_box_append (GTK_BOX (c), mc_title (acc_tr (ACC_T_PC_QUICK)));
    const char *acts[][2] = {
      { "Open Hardware", "hardware" },
      { "Open System Diagnosis", "diagnosis" },
      { "Open System Manager", "sysmgr" },
      { "Open Software Center", "softcenter" },
      { "Open Updates & Support", "support" },
    };
    for (int i = 0; i < 5; i++)
      {
        GtkWidget *b = gtk_button_new_with_label (acts[i][0]);
        gtk_widget_add_css_class (b, "actionbtn");
        gtk_widget_set_halign (b, GTK_ALIGN_START);
        g_signal_connect (b, "clicked", G_CALLBACK (quick_nav_cb), (gpointer) acts[i][1]);
        gtk_box_append (GTK_BOX (c), b);
      }
    gtk_box_append (GTK_BOX (page), c);
  }

  /* initial async scans */
  g_thread_new ("pm-storage", scan_storage, NULL);
  g_thread_new ("pm-network", scan_network, NULL);
  g_thread_new ("pm-fw", scan_firewall, NULL);
  g_thread_new ("pm-selinux", scan_selinux, NULL);
  g_thread_new ("pm-ports", scan_ports, NULL);
  g_thread_new ("pm-tools", scan_tools, NULL);
  g_thread_new ("pm-secupd", scan_secupd, NULL);
  g_thread_new ("pm-latency", scan_latency, NULL);
  g_thread_new ("pm-cache", scan_cache, NULL);
  g_thread_new ("pm-browser", scan_browser, NULL);

  /* overall status */
  {
    LoadInfo ld;
    acc_load_read (&ld);
    if (ld.disk_pct > 90.0 || ld.mem_pct > 90.0)
      set (pm_overall, "Status: Needs Attention\\nDisk: %.0f%% · RAM: %.0f%%", ld.disk_pct, ld.mem_pct);
    else
      set (pm_overall, "Status: Healthy\\nDisk: %.0f%% · RAM: %.0f%%", ld.disk_pct, ld.mem_pct);
  }

  return mc_page (page);
}

void
acc_pcmanager_teardown (void)
{
  pm_overall = pm_storage = pm_network = pm_quick = NULL;
  pm_fw = pm_selinux = pm_ports = pm_tools = pm_secupd = pm_latency = NULL;
  pm_scan_result = NULL;
}
