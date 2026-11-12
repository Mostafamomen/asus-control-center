/* ui/softcenter.cpp - Software Center (read-only)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "softcenter.h"
#include "theme.h"
#include "mcui.h"
#include "window.h"
#include "../i18n/i18n.h"
#include "../probe/desktop.h"
#include "../control/hostcmd.h"
#include "../control/portal.h"
#include "../probe/probe.h"
#include <adwaita.h>
#include <glib.h>
#include <string.h>
#include <stdarg.h>

static GtkWidget *sc_apps, *sc_updates, *sc_drivers, *sc_diag;

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

/* Run a host command and keep its first N lines. NULL when the command
 * could not run at all (no host agent and no program here). */
static gchar *
host_cmd (AccCommand id, const gchar *arg, int max_lines)
{
  gchar *out = NULL, *err = NULL;
  gint code = -1;
  AccHostHow how = ACC_HOST_UNAVAILABLE;
  if (!acc_host_run (id, arg, &out, &code, &how, &err))
    {
      g_free (out);
      g_free (err);
      return NULL;
    }
  g_free (err);
  if (!out)
    return NULL;

  /* keep the first N lines */
  int n = 0;
  gchar *p = out;
  while (p && *p && n < max_lines)
    {
      gchar *nl = strchr (p, '\n');
      if (!nl)
        break;
      *nl = '\0';
      p = nl + 1;
      n++;
    }
  if (p && *p && n >= max_lines)
    *p = '\0';
  return out;
}

static gpointer
scan_apps (gpointer data)
{
  (void) data;
  gchar *rpm = host_cmd (ACC_CMD_RPM_LIST, NULL, 40);
  gchar *flat = host_cmd (ACC_CMD_FLATPAK_LIST, NULL, 40);
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
  gchar *dnf = host_cmd (ACC_CMD_PACKAGE_UPDATES, NULL, 30);
  gchar *flat = host_cmd (ACC_CMD_FLATPAK_UPDATES, NULL, 30);
  gchar *fw = host_cmd (ACC_CMD_FIRMWARE_UPDATES, NULL, 30);
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
  /* the kernel release is readable straight from /proc */
  gchar *kernel = acc_read_trim ("/proc/sys/kernel/osrelease");
  gchar *nvidia = host_cmd (ACC_CMD_NVIDIA_SMI, NULL, 30);
  gchar *lspci = host_cmd (ACC_CMD_LSPCI, NULL, 40);
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
  /* the tools are on the host: ask the agent whether it can run them */
  static const AccCommand probe_ids[] = {
    ACC_CMD_PACKAGE_UPDATES, ACC_CMD_FLATPAK_UPDATES,
    ACC_CMD_FIRMWARE_UPDATES, ACC_CMD_RPM_LIST
  };
  static const gchar *probe_names[] = { "dnf", "flatpak", "fwupdmgr", "rpm" };
  GString *tools = g_string_new (NULL);
  for (guint i = 0; i < G_N_ELEMENTS (probe_ids); i++)
    {
      gchar *out = NULL, *err = NULL;
      gint code = -1;
      AccHostHow how = ACC_HOST_UNAVAILABLE;
      gboolean ran = acc_host_run (probe_ids[i], NULL, &out, &code, &how, &err);
      g_free (out);
      g_free (err);
      g_string_append_printf (tools, "%s: %s\n", probe_names[i],
                              ran ? "available" : "missing");
    }
  GString *s = g_string_new (NULL);
  g_string_append (s, "Software diagnostics:\\n");
  g_string_append (s, tools->str);
  g_string_free (tools, TRUE);
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

typedef struct { MaAction *act; gboolean ok; gchar *err; } MaJob;

static gboolean
ma_job_apply (gpointer data)
{
  MaJob *j = (MaJob *) data;
  if (j->ok)
    acc_ui_toast (MT ("Done.", "تم."));
  else
    {
      acc_ui_toast (j->act->kind == 0
        ? MT ("Could not remove it. A system-wide app needs the system software tool.",
              "تعذّر الحذف. التطبيق المثبّت للنظام يحتاج أداة البرمجيات.")
        : MT ("The change did not apply.", "لم يُطبّق التغيير."));
      if (j->err) g_warning ("manage-apps: %s", j->err);
    }
  g_free (j->err);
  ma_action_free (j->act);
  g_free (j);
  ma_scan ();   /* refresh list */
  return G_SOURCE_REMOVE;
}

/* The work happens on the host (the agent), so it runs off the main
 * thread: the UI must not freeze while a Flatpak is removed. */
static gpointer
ma_job_worker (gpointer data)
{
  MaJob *j = (MaJob *) data;
  AccHostHow how = ACC_HOST_UNAVAILABLE;
  gint code = -1;
  gchar *out = NULL, *err = NULL;
  gboolean ok = FALSE;

  if (j->act->kind == 0)
    ok = acc_host_run (ACC_CMD_FLATPAK_UNINSTALL, j->act->id, &out, &code, &how, &err);
  else if (j->act->kind == 1)
    ok = acc_host_run (ACC_CMD_GNOME_EXT_ENABLE, j->act->id, &out, &code, &how, &err);
  else
    ok = acc_host_run (ACC_CMD_GNOME_EXT_DISABLE, j->act->id, &out, &code, &how, &err);

  g_free (out);
  j->ok = ok;
  j->err = err;
  g_idle_add (ma_job_apply, j);
  return NULL;
}

static void
ma_run_action (MaAction *a)
{
  MaJob *j = g_new0 (MaJob, 1);
  j->act = a;
  g_thread_unref (g_thread_new ("manage-apps-action", ma_job_worker, j));
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
  if (m->gen == ma_gen && ma_flat_box)
    {
      ma_clear (ma_flat_box);
      if (ma_ext_box) ma_clear (ma_ext_box);
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

      /* Extensions — GNOME only */
      if (ma_ext_box)
        {
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
        }

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
  gchar *out = NULL, *err = NULL;
  gint code = -1;
  AccHostHow how = ACC_HOST_UNAVAILABLE;

  acc_host_run (ACC_CMD_FLATPAK_LIST, NULL, &out, &code, &how, &err);
  m->flat = out;
  g_free (err);
  err = NULL;

  if (acc_desktop_is_gnome ())
    {
      acc_host_run (ACC_CMD_GNOME_EXT_ENABLED, NULL, &out, &code, &how, &err);
      m->ext_on = out;
      out = NULL;
      g_free (err);
      err = NULL;
      acc_host_run (ACC_CMD_GNOME_EXT_DISABLED, NULL, &out, &code, &how, &err);
      m->ext_off = out;
      out = NULL;
      g_free (err);
    }
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
  /* the desktop's own software centre, through the portal: it works from
   * a Flatpak, where gnome-software cannot be started directly */
  gchar *perr = NULL;
  if (acc_portal_open_software (&perr))
    {
      g_free (perr);
      return;
    }
  g_free (perr);

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
    MT ("Remove Flatpak apps and manage desktop extensions. Removals are confirmed first and run only for your user — nothing here touches system files.",
        "أزل تطبيقات Flatpak وأدِر إضافات سطح المكتب. يُطلب تأكيد الحذف ويعمل لمستخدمك فقط دون المساس بملفات النظام."),
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

  if (acc_desktop_is_gnome ())
    {
      gtk_box_append (GTK_BOX (panel),
                      mc_group (MT ("GNOME extensions", "إضافات GNOME")));
      ma_ext_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
      gtk_box_append (GTK_BOX (panel), ma_ext_box);
    }
  else
    {
      ma_ext_box = NULL;
    }

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
