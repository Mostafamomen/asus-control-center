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

/* ------------------------------------------------- Manage Apps --------- */
/* Lists Flatpak applications and GNOME Shell extensions, each with a safe,
 * user-scope action (remove / enable / disable). Every destructive action is
 * confirmed first. Nothing here uses root: a Flatpak system install that would
 * need pkexec simply reports that it must be removed with the system tool.
 * System RPM packages are shown read-only. */

#define MT(en, ar) (acc_i18n_lang () == ACC_LANG_AR ? (ar) : (en))

static GtkWidget *ma_flat_box, *ma_ext_box, *ma_status;
static guint      ma_gen;

typedef struct { guint gen; gchar *flat; gchar *ext_on; gchar *ext_off; } MaMsg;

typedef struct { gchar *id; int kind; } MaAction;  /* kind: 0 flat rm, 1 ext en, 2 ext dis */

static void ma_scan (void);

static void
ma_action_free (gpointer p)
{
  MaAction *a = (MaAction *) p;
  if (!a) return;
  g_free (a->id);
  g_free (a);
}

static MaAction *
ma_action_dup (const MaAction *a)
{
  MaAction *n = g_new0 (MaAction, 1);
  n->id = g_strdup (a->id);
  n->kind = a->kind;
  return n;
}

static void
ma_clear (GtkWidget *box)
{
  GtkWidget *c;
  while ((c = gtk_widget_get_first_child (box)))
    gtk_box_remove (GTK_BOX (box), c);
}

static void
ma_action_done (GObject *src, GAsyncResult *res, gpointer data)
{
  MaAction *a = (MaAction *) data;
  GError *err = NULL;
  GSubprocess *p = G_SUBPROCESS (src);
  gboolean ok = g_subprocess_wait_check_finish (p, res, &err);
  if (ok)
    acc_ui_toast (MT ("Done.", "تم."));
  else
    {
      acc_ui_toast (a->kind == 0
        ? MT ("Could not remove it. A system-wide app needs the system software tool.",
              "تعذّر الحذف. التطبيق المثبّت للنظام يحتاج أداة البرمجيات.")
        : MT ("The change did not apply.", "لم يُطبّق التغيير."));
      if (err) g_warning ("manage-apps: %s", err->message);
    }
  g_clear_error (&err);
  ma_action_free (a);
  ma_scan ();   /* refresh list */
}

static void
ma_run_action (MaAction *a)
{
  const gchar *argv_rm[]  = { "flatpak", "uninstall", "--user", "-y", a->id, NULL };
  const gchar *argv_en[]  = { "gnome-extensions", "enable", a->id, NULL };
  const gchar *argv_dis[] = { "gnome-extensions", "disable", a->id, NULL };
  const gchar *const *argv = a->kind == 0 ? argv_rm : a->kind == 1 ? argv_en : argv_dis;
  GError *err = NULL;
  GSubprocess *p = g_subprocess_newv (argv,
      (GSubprocessFlags) (G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE),
      &err);
  if (!p)
    {
      acc_ui_toast (MT ("Could not start the operation.", "تعذّر بدء العملية."));
      if (err) g_warning ("manage-apps: %s", err->message);
      g_clear_error (&err);
      ma_action_free (a);
      return;
    }
  g_subprocess_wait_check_async (p, NULL, ma_action_done, a);
  g_object_unref (p);
}

static void
ma_confirm_resp (AdwAlertDialog *d, const gchar *resp, gpointer data)
{
  (void) d;
  MaAction *a = (MaAction *) data;              /* a dup owned here */
  if (g_strcmp0 (resp, "ok") == 0)
    ma_run_action (a);
  else
    ma_action_free (a);
}

static void
ma_remove_cb (GtkButton *b, gpointer data)
{
  (void) data;
  MaAction *a = ma_action_dup ((MaAction *) g_object_get_data (G_OBJECT (b), "ma-act"));
  gchar *msg = g_strdup_printf (
    MT ("Remove %s? You can reinstall it later from the Software Center.",
        "إزالة %s؟ يمكنك إعادة تثبيته لاحقًا من مركز البرمجيات."), a->id);
  AdwDialog *d = ADW_DIALOG (adw_alert_dialog_new (MT ("Remove application", "إزالة التطبيق"), msg));
  g_free (msg);
  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (d),
    "cancel", MT ("Cancel", "إلغاء"), "ok", MT ("Remove", "إزالة"), NULL);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (d), "ok", ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (d), "cancel");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (d), "cancel");
  g_signal_connect (d, "response", G_CALLBACK (ma_confirm_resp), a);
  adw_dialog_present (d, acc_ui_root ());
}

static void
ma_toggle_cb (GtkButton *b, gpointer data)
{
  (void) data;                                  /* enable/disable: reversible, no prompt */
  ma_run_action (ma_action_dup ((MaAction *) g_object_get_data (G_OBJECT (b), "ma-act")));
}

static GtkWidget *
ma_row (const gchar *title, const gchar *meta,
        const gchar *btn_label, const gchar *css, GCallback cb, MaAction *a)
{
  GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *col = gtk_box_new (GTK_ORIENTATION_VERTICAL, 1);
  gtk_widget_set_hexpand (col, TRUE);
  gtk_box_append (GTK_BOX (col), mc_label (title, "val", 0));
  if (meta && *meta)
    gtk_box_append (GTK_BOX (col), mc_label (meta, "dim", 0));
  gtk_box_append (GTK_BOX (row), col);
  GtkWidget *btn = gtk_button_new_with_label (btn_label);
  gtk_widget_add_css_class (btn, css);
  gtk_widget_set_valign (btn, GTK_ALIGN_CENTER);
  /* button owns `a`; the click handler operates on a dup, so a refresh that
   * destroys the row frees `a` cleanly whether or not it was ever clicked. */
  g_object_set_data_full (G_OBJECT (btn), "ma-act", a, ma_action_free);
  g_signal_connect (btn, "clicked", cb, NULL);
  gtk_box_append (GTK_BOX (row), btn);
  return row;
}

static gboolean
ma_apply (gpointer data)
{
  MaMsg *m = (MaMsg *) data;
  if (m->gen == ma_gen && ma_flat_box && ma_ext_box)
    {
      ma_clear (ma_flat_box);
      ma_clear (ma_ext_box);
      int nflat = 0, next = 0;

      /* Flatpak: tab-separated application<TAB>name<TAB>version<TAB>size */
      if (m->flat && *m->flat)
        {
          gchar **lines = g_strsplit (m->flat, "\n", -1);
          for (gchar **p = lines; *p; p++)
            {
              if (!**p) continue;
              gchar **f = g_strsplit (*p, "\t", 4);
              guint n = g_strv_length (f);
              if (n >= 1 && f[0][0])
                {
                  const gchar *id = f[0];
                  const gchar *name = n >= 2 && f[1][0] ? f[1] : id;
                  gchar *meta = g_strdup_printf ("%s · %s%s%s", id,
                      n >= 3 ? f[2] : "",
                      n >= 4 && f[3][0] ? " · " : "",
                      n >= 4 ? f[3] : "");
                  MaAction *a = g_new0 (MaAction, 1);
                  a->id = g_strdup (id); a->kind = 0;
                  gtk_box_append (GTK_BOX (ma_flat_box),
                    ma_row (name, meta, MT ("Remove", "إزالة"), "actionbtn",
                            G_CALLBACK (ma_remove_cb), a));
                  g_free (meta);
                  nflat++;
                }
              g_strfreev (f);
            }
          g_strfreev (lines);
        }
      if (!nflat)
        gtk_box_append (GTK_BOX (ma_flat_box),
          mc_label (MT ("No user Flatpak applications, or Flatpak is unavailable.",
                        "لا توجد تطبيقات Flatpak للمستخدم أو أن Flatpak غير متاح."), "dim", 0));

      /* Extensions */
      for (int pass = 0; pass < 2; pass++)
        {
          const gchar *src = pass == 0 ? m->ext_on : m->ext_off;
          if (!src) continue;
          gchar **lines = g_strsplit (src, "\n", -1);
          for (gchar **p = lines; *p; p++)
            {
              gchar *uuid = g_strstrip (*p);
              if (!*uuid) continue;
              MaAction *a = g_new0 (MaAction, 1);
              a->id = g_strdup (uuid); a->kind = pass == 0 ? 2 : 1;
              gtk_box_append (GTK_BOX (ma_ext_box),
                ma_row (uuid, pass == 0 ? MT ("Enabled", "مفعّلة") : MT ("Disabled", "معطّلة"),
                        pass == 0 ? MT ("Disable", "تعطيل") : MT ("Enable", "تفعيل"),
                        "actionbtn", G_CALLBACK (ma_toggle_cb), a));
              next++;
            }
          g_strfreev (lines);
        }
      if (!next)
        gtk_box_append (GTK_BOX (ma_ext_box),
          mc_label (MT ("No GNOME Shell extensions found.", "لا توجد إضافات لـ GNOME Shell."), "dim", 0));

      if (ma_status)
        gtk_label_set_text (GTK_LABEL (ma_status), "");
    }
  g_free (m->flat); g_free (m->ext_on); g_free (m->ext_off); g_free (m);
  return G_SOURCE_REMOVE;
}

static gpointer
ma_worker (gpointer data)
{
  guint gen = GPOINTER_TO_UINT (data);
  MaMsg *m = g_new0 (MaMsg, 1);
  m->gen = gen;
  m->flat = run_cmd ({ "flatpak", "list", "--app",
                       "--columns=application,name,version,size", NULL });
  m->ext_on  = run_cmd ({ "gnome-extensions", "list", "--enabled", NULL });
  m->ext_off = run_cmd ({ "gnome-extensions", "list", "--disabled", NULL });
  g_idle_add (ma_apply, m);
  return NULL;
}

static void
ma_scan (void)
{
  ma_gen++;
  if (ma_status)
    gtk_label_set_text (GTK_LABEL (ma_status), MT ("Loading…", "جارٍ التحميل…"));
  g_thread_unref (g_thread_new ("manage-apps", ma_worker, GUINT_TO_POINTER (ma_gen)));
}

static void
ma_refresh_cb (GtkButton *b, gpointer d) { (void) b; (void) d; ma_scan (); }

static void
ma_systool_cb (GtkButton *b, gpointer d)
{
  (void) b; (void) d;
  const gchar *argv[] = { "sh", "-c",
    "gnome-software || plasma-discover || flatpak run org.gnome.Software &", NULL };
  GError *err = NULL;
  GSubprocess *p = g_subprocess_newv (argv,
      (GSubprocessFlags) (G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE), &err);
  if (p) g_object_unref (p);
  else { acc_ui_toast (MT ("No system software tool found.", "لا توجد أداة برمجيات للنظام.")); g_clear_error (&err); }
}

static GtkWidget *
ma_section (void)
{
  GtkWidget *panel = mc_panel (MT ("Manage Apps", "إدارة التطبيقات"));
  GtkWidget *sub = mc_label (
    MT ("Remove Flatpak apps and turn GNOME extensions on or off. Removals are confirmed first and run only for your user — nothing here touches system files.",
        "أزل تطبيقات Flatpak وفعّل أو عطّل إضافات GNOME. يُطلب تأكيد الحذف ويعمل لمستخدمك فقط دون المساس بملفات النظام."),
    "pagesub", 0);
  gtk_label_set_wrap (GTK_LABEL (sub), TRUE);
  gtk_box_append (GTK_BOX (panel), sub);

  GtkWidget *bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *rb = gtk_button_new_with_label (MT ("Refresh", "تحديث"));
  gtk_widget_add_css_class (rb, "actionbtn");
  g_signal_connect (rb, "clicked", G_CALLBACK (ma_refresh_cb), NULL);
  gtk_box_append (GTK_BOX (bar), rb);
  GtkWidget *sysb = gtk_button_new_with_label (MT ("System software tool…", "أداة برمجيات النظام…"));
  gtk_widget_add_css_class (sysb, "actionbtn");
  g_signal_connect (sysb, "clicked", G_CALLBACK (ma_systool_cb), NULL);
  gtk_box_append (GTK_BOX (bar), sysb);
  ma_status = mc_label ("", "dim", 0);
  gtk_widget_set_valign (ma_status, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (bar), ma_status);
  gtk_box_append (GTK_BOX (panel), bar);

  gtk_box_append (GTK_BOX (panel), mc_group (MT ("Flatpak applications", "تطبيقات Flatpak")));
  ma_flat_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_box_append (GTK_BOX (panel), ma_flat_box);

  gtk_box_append (GTK_BOX (panel), mc_group (MT ("GNOME extensions", "إضافات GNOME")));
  ma_ext_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_box_append (GTK_BOX (panel), ma_ext_box);

  ma_scan ();
  return panel;
}

#undef MT

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

  gtk_box_append (GTK_BOX (page), ma_section ());

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
  ma_gen++;
  ma_flat_box = ma_ext_box = ma_status = NULL;
}
