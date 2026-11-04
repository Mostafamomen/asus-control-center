/* ui/softcenter.cpp - Software Center (read-only, card-based)
 *
 * A read-only overview of installed software, available updates, graphics
 * drivers and the package tools present on the system. It never installs,
 * removes or updates anything — package changes stay with the user's own
 * package manager. The native package manager is detected (dnf/rpm on
 * Fedora, apt/dpkg on Debian/Ubuntu/Mint) so the readout is correct on
 * every supported distro, with Flatpak and fwupd shown when available.
 *
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
static GtkWidget *sc_apps_sub, *sc_updates_sub;

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
run_sh (const gchar *script)
{
  const gchar *argv[] = { "sh", "-c", script, NULL };
  return run_cmd (argv);
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

static guint
count_lines (const gchar *s)
{
  if (!s || !*s)
    return 0;
  guint n = 0;
  for (const gchar *p = s; *p; p++)
    if (*p == '\n')
      n++;
  if (s[strlen (s) - 1] != '\n')
    n++;
  return n;
}

typedef struct { gchar *text; guint count; GtkWidget *target; GtkWidget *sub; } ScResult;

static gboolean
update_idle (gpointer data)
{
  ScResult *r = (ScResult *) data;
  if (r->target && r->text)
    set_lbl (r->target, "%s", r->text);
  if (r->sub)
    set_lbl (r->sub, "%u", r->count);
  g_free (r->text);
  g_free (r);
  return G_SOURCE_REMOVE;
}

static void
post (GtkWidget *target, GtkWidget *sub, gchar *text, guint count)
{
  ScResult *r = g_new0 (ScResult, 1);
  r->text = text;
  r->count = count;
  r->target = target;
  r->sub = sub;
  g_idle_add (update_idle, r);
}

static gpointer
scan_apps (gpointer data)
{
  (void) data;
  /* native packages first (dnf/rpm, else apt/dpkg), then Flatpak apps */
  gchar *native = run_sh (
    "if command -v rpm >/dev/null; then "
    "  rpm -qa --qf '%{NAME} %{VERSION}\\n' 2>/dev/null | sort; "
    "elif command -v dpkg-query >/dev/null; then "
    "  dpkg-query -W -f='${Package} ${Version}\\n' 2>/dev/null | sort; "
    "fi");
  gchar *flat = run_sh (
    "command -v flatpak >/dev/null && "
    "flatpak list --app --columns=application,version 2>/dev/null");
  guint n = count_lines (native) + count_lines (flat);

  GString *s = g_string_new (NULL);
  if (native && *native)
    g_string_append (s, native);
  if (flat && *g_strchomp (flat ? flat : (gchar *) ""))
    {
      if (s->len)
        g_string_append_c (s, '\n');
      g_string_append (s, "— Flatpak —\n");
      g_string_append (s, flat);
    }
  if (!s->len)
    g_string_append (s, acc_tr (ACC_T_UNAVAILABLE));
  g_free (native);
  g_free (flat);
  post (sc_apps, sc_apps_sub, g_string_free (s, FALSE), n);
  return NULL;
}

static gpointer
scan_updates (gpointer data)
{
  (void) data;
  gchar *upd = run_sh (
    "if command -v dnf >/dev/null; then "
    "  dnf -q check-update 2>/dev/null | awk 'NF>=3{print $1\" \"$2}'; "
    "elif command -v apt >/dev/null; then "
    "  apt list --upgradable 2>/dev/null | sed '1d' | cut -d/ -f1; "
    "fi");
  gchar *flat = run_sh (
    "command -v flatpak >/dev/null && "
    "flatpak remote-ls --updates --columns=application 2>/dev/null");
  gchar *fw = run_sh (
    "command -v fwupdmgr >/dev/null && "
    "fwupdmgr get-updates 2>/dev/null | sed -n '2,20p'");
  guint n = count_lines (upd) + count_lines (flat);

  GString *s = g_string_new (NULL);
  if (upd && *g_strchomp (upd))
    g_string_append (s, upd);
  if (flat && *g_strchomp (flat))
    {
      if (s->len) g_string_append_c (s, '\n');
      g_string_append (s, "— Flatpak —\n");
      g_string_append (s, flat);
    }
  if (fw && *g_strchomp (fw))
    {
      if (s->len) g_string_append_c (s, '\n');
      g_string_append (s, "— Firmware —\n");
      g_string_append (s, fw);
    }
  if (!s->len)
    g_string_append (s, acc_tr (ACC_T_ST_NO_FAILED));  /* "No failed" ~ none */
  g_free (upd);
  g_free (flat);
  g_free (fw);
  post (sc_updates, sc_updates_sub, g_string_free (s, FALSE), n);
  return NULL;
}

static gpointer
scan_drivers (gpointer data)
{
  (void) data;
  gchar *out = run_sh (
    "echo \"Kernel: $(uname -r)\"; "
    "echo; echo 'Graphics controllers:'; "
    "lspci 2>/dev/null | grep -iE 'vga|3d|display' | sed 's/^/  /'; "
    "if command -v nvidia-smi >/dev/null; then echo; echo 'NVIDIA:'; "
    "  nvidia-smi --query-gpu=name,driver_version,memory.total "
    "  --format=csv,noheader 2>/dev/null | sed 's/^/  /'; fi");
  post (sc_drivers, NULL, out ? out : g_strdup (acc_tr (ACC_T_UNAVAILABLE)), 0);
  return NULL;
}

static gpointer
scan_tools (gpointer data)
{
  (void) data;
  gchar *out = run_sh (
    "for t in dnf apt rpm dpkg flatpak fwupdmgr snap; do "
    "  if command -v $t >/dev/null; then echo \"$t: available\"; "
    "  else echo \"$t: not installed\"; fi; done");
  post (sc_diag, NULL, out ? out : g_strdup (acc_tr (ACC_T_UNAVAILABLE)), 0);
  return NULL;
}

/* a card whose body is a scrolled, selectable read-out */
static GtkWidget *
readout_card (const gchar *icon, const gchar *title, const gchar *subkey,
              GtkWidget **out_label, GtkWidget **out_sub)
{
  GtkWidget *body = NULL;
  GtkWidget *c = mc_card_titled (icon, title, &body);
  gtk_widget_set_vexpand (c, TRUE);

  if (subkey && out_sub)
    {
      GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
      GtkWidget *n = mc_label ("0", "statval", 0);
      gtk_widget_add_css_class (n, "countbig");
      gtk_box_append (GTK_BOX (h), n);
      gtk_box_append (GTK_BOX (h), mc_label (subkey, "statcap", 0));
      gtk_box_append (GTK_BOX (body), h);
      *out_sub = n;
    }

  GtkWidget *scroll = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand (scroll, TRUE);
  gtk_widget_set_size_request (scroll, -1, 150);
  GtkWidget *lbl = mc_label (acc_tr (ACC_T_ST_SCANNING), "readout", 0);
  gtk_label_set_wrap (GTK_LABEL (lbl), TRUE);
  gtk_label_set_selectable (GTK_LABEL (lbl), TRUE);
  gtk_label_set_yalign (GTK_LABEL (lbl), 0.0);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), lbl);
  gtk_box_append (GTK_BOX (body), scroll);
  if (out_label)
    *out_label = lbl;
  return c;
}

GtkWidget *
acc_page_softcenter (AccUI *u)
{
  (void) u;
  GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_add_css_class (h, "pagehead");
  GtkWidget *col = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
  gtk_widget_set_hexpand (col, TRUE);
  gtk_widget_set_valign (col, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (col), mc_label ("ASUSCraft", "eyebrow", 0));
  gtk_box_append (GTK_BOX (col), mc_label (acc_tr (ACC_T_NAV_SOFTWARE),
                                           "pagetitle", 0));
  GtkWidget *sub = mc_label (acc_tr (ACC_T_ST_PROTECTED), "pagesub", 0);
  gtk_label_set_wrap (GTK_LABEL (sub), TRUE);
  gtk_box_append (GTK_BOX (col), sub);
  gtk_box_append (GTK_BOX (h), col);
  gtk_box_append (GTK_BOX (page), h);

  GtkWidget *grid = mc_grid (1, 2);
  gtk_flow_box_append (GTK_FLOW_BOX (grid),
                       readout_card ("system-software-install-symbolic",
                                     acc_tr (ACC_T_SW_INSTALLED),
                                     acc_tr (ACC_T_SW_INSTALLED),
                                     &sc_apps, &sc_apps_sub));
  gtk_flow_box_append (GTK_FLOW_BOX (grid),
                       readout_card ("software-update-available-symbolic",
                                     acc_tr (ACC_T_SW_UPDATES),
                                     acc_tr (ACC_T_SW_UPDATES),
                                     &sc_updates, &sc_updates_sub));
  gtk_flow_box_append (GTK_FLOW_BOX (grid),
                       readout_card ("video-display-symbolic",
                                     acc_tr (ACC_T_HW_GPU), NULL,
                                     &sc_drivers, NULL));
  gtk_flow_box_append (GTK_FLOW_BOX (grid),
                       readout_card ("applications-utilities-symbolic",
                                     acc_tr (ACC_T_NAV_TOOLS), NULL,
                                     &sc_diag, NULL));
  gtk_box_append (GTK_BOX (page), grid);

  g_thread_new ("sc-apps", scan_apps, NULL);
  g_thread_new ("sc-updates", scan_updates, NULL);
  g_thread_new ("sc-drivers", scan_drivers, NULL);
  g_thread_new ("sc-tools", scan_tools, NULL);

  return mc_page (page);
}

void
acc_softcenter_teardown (void)
{
  sc_apps = sc_updates = sc_drivers = sc_diag = NULL;
  sc_apps_sub = sc_updates_sub = NULL;
}
