/* ui/softcenter.cpp - Software Center (read-only)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "softcenter.h"
#include "theme.h"
#include "mcui.h"
#include "window.h"
#include "../i18n/i18n.h"
#include <adwaita.h>
#include <glib.h>
#include <string.h>
#include <stdarg.h>
#include <initializer_list>
#include <vector>

static GtkWidget *sc_apps, *sc_updates, *sc_drivers, *sc_diag;

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

typedef struct { gchar *text; GtkWidget *target; } ScResult;

static gboolean
update_idle (gpointer data)
{
  ScResult *r = (ScResult *) data;
  if (r && r->target && r->text)
    set (r->target, "%s", r->text);
  g_free (r ? r->text : NULL);
  g_free (r);
  return G_SOURCE_REMOVE;
}

static gpointer
scan_apps (gpointer data)
{
  (void) data;
  gchar *rpm = run_cmd ({ "sh", "-c",
    "rpm -qa --qf '%{NAME} %{VERSION}-%{RELEASE}\\n' 2>/dev/null | head -40", NULL });
  gchar *flat = run_cmd ({ "sh", "-c",
    "flatpak list --app --columns=application,version 2>/dev/null | head -40", NULL });
  GString *s = g_string_new (NULL);
  g_string_append (s, "RPM packages (first 40):\\n");
  if (rpm && *rpm) g_string_append (s, rpm); else g_string_append (s, "none/unavailable\\n");
  g_string_append (s, "\\nFlatpak apps (first 40):\\n");
  if (flat && *flat) g_string_append (s, flat); else g_string_append (s, "none/unavailable\\n");
  g_free (rpm); g_free (flat);
  ScResult *r = g_new0 (ScResult, 1);
  r->text = g_strdup (s->str);
  r->target = sc_apps;
  g_string_free (s, TRUE);
  g_idle_add (update_idle, r);
  return NULL;
}

static gpointer
scan_updates (gpointer data)
{
  (void) data;
  gchar *dnf = run_cmd ({ "sh", "-c",
    "command -v dnf >/dev/null && dnf check-update -q 2>/dev/null | head -30 || echo 'DNF unavailable'", NULL });
  gchar *flat = run_cmd ({ "sh", "-c",
    "command -v flatpak >/dev/null && flatpak remote-ls --updates 2>/dev/null | head -30 || echo 'Flatpak unavailable'", NULL });
  gchar *fw = run_cmd ({ "sh", "-c",
    "command -v fwupdmgr >/dev/null && fwupdmgr get-updates 2>/dev/null | head -30 || echo 'fwupd unavailable'", NULL });
  GString *s = g_string_new (NULL);
  g_string_append (s, "DNF updates:\\n");
  if (dnf) { g_string_append (s, dnf); g_free (dnf); }
  g_string_append (s, "\\nFlatpak updates:\\n");
  if (flat) { g_string_append (s, flat); g_free (flat); }
  g_string_append (s, "\\nFirmware updates:\\n");
  if (fw) { g_string_append (s, fw); g_free (fw); }
  ScResult *r = g_new0 (ScResult, 1);
  r->text = g_strdup (s->str);
  r->target = sc_updates;
  g_string_free (s, TRUE);
  g_idle_add (update_idle, r);
  return NULL;
}

static gpointer
scan_drivers (gpointer data)
{
  (void) data;
  gchar *kernel = run_cmd ({ "uname", "-r", NULL });
  gchar *nvidia = run_cmd ({ "sh", "-c",
    "nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv,noheader 2>/dev/null || echo 'NVIDIA unavailable'", NULL });
  gchar *lspci = run_cmd ({ "sh", "-c",
    "lspci | grep -iE 'vga|3d|display' 2>/dev/null", NULL });
  GString *s = g_string_new (NULL);
  g_string_append_printf (s, "Kernel: %s\\n", kernel ? g_strstrip (kernel) : "unknown");
  g_string_append (s, "Graphics controllers:\\n");
  if (lspci) g_string_append (s, lspci);
  g_string_append (s, "\\nNVIDIA:\\n");
  if (nvidia) g_string_append (s, nvidia);
  g_free (kernel); g_free (nvidia); g_free (lspci);
  ScResult *r = g_new0 (ScResult, 1);
  r->text = g_strdup (s->str);
  r->target = sc_drivers;
  g_string_free (s, TRUE);
  g_idle_add (update_idle, r);
  return NULL;
}

static gpointer
scan_diag (gpointer data)
{
  (void) data;
  gchar *tools = run_cmd ({ "sh", "-c",
    "for t in dnf flatpak fwupdmgr rpm; do command -v $t >/dev/null && echo \"$t: available\" || echo \"$t: missing\"; done", NULL });
  GString *s = g_string_new (NULL);
  g_string_append (s, "Software diagnostics:\\n");
  if (tools) g_string_append (s, tools);
  g_free (tools);
  ScResult *r = g_new0 (ScResult, 1);
  r->text = g_strdup (s->str);
  r->target = sc_diag;
  g_string_free (s, TRUE);
  g_idle_add (update_idle, r);
  return NULL;
}

GtkWidget *
acc_page_softcenter (AccUI *u)
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
    gtk_box_append (GTK_BOX (col), mc_label ("Software Center", "pagetitle", 0));
    GtkWidget *sub = mc_label ("Read-only software, update, driver and diagnostics overview.", "pagesub", 0);
    gtk_label_set_wrap (GTK_LABEL (sub), TRUE);
    gtk_label_set_max_width_chars (GTK_LABEL (sub), 80);
    gtk_box_append (GTK_BOX (col), sub);
    gtk_box_append (GTK_BOX (h), col);
    gtk_box_append (GTK_BOX (page), h);
  }

  const char *titles[] = { "Installed Applications", "Application Updates", "Drivers & Hardware Software", "Software Diagnostics" };
  GtkWidget **targets[] = { &sc_apps, &sc_updates, &sc_drivers, &sc_diag };
  for (int i = 0; i < 4; i++)
    {
      GtkWidget *c = mc_card ();
      gtk_box_append (GTK_BOX (c), mc_title (titles[i]));
      GtkWidget *l = mc_label ("Loading…", "val", 0);
      gtk_label_set_wrap (GTK_LABEL (l), TRUE);
      gtk_box_append (GTK_BOX (c), l);
      *targets[i] = l;
      gtk_box_append (GTK_BOX (page), c);
    }

  g_thread_new ("sc-apps", scan_apps, NULL);
  g_thread_new ("sc-updates", scan_updates, NULL);
  g_thread_new ("sc-drivers", scan_drivers, NULL);
  g_thread_new ("sc-diag", scan_diag, NULL);

  return mc_page (page);
}

void
acc_softcenter_teardown (void)
{
  sc_apps = sc_updates = sc_drivers = sc_diag = NULL;
}
