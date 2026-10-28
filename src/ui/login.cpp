/* ui/login.c - login page: username + password, remember username, logo
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * First launch → "Create your account" (name, password, confirm).
 * Later launches → "Welcome back" with the remembered username prefilled.
 * The account lives only in settings.conf (salted SHA-256, see user.c).
 */
#include "login.h"
#include "window.h"
#include "theme.h"
#include "../user/user.h"
#include "../i18n/i18n.h"

#include <adwaita.h>
#include <string.h>

typedef struct
{
  GtkApplication *app;
  GtkWidget *win;
  GtkWidget *user, *pass, *pass2;
  GtkWidget *pass2_box, *remember, *err, *reset;
  GtkWidget *headline, *subtitle;
  gboolean   create;
  gboolean   done;
} LoginUI;

static LoginUI *L = NULL;

/* ------------------------------------------------------------- helpers */
static GtkWidget *
label (const gchar *text, const gchar *cls, float xalign)
{
  GtkWidget *l = gtk_label_new (text);
  gtk_label_set_xalign (GTK_LABEL (l), xalign);
  if (cls)
    gtk_widget_add_css_class (l, cls);
  return l;
}

static const gchar *
u (GtkWidget *entry)
{
  return gtk_editable_get_text (GTK_EDITABLE (entry));
}

static void
set_error (const gchar *msg)
{
  gtk_label_set_text (GTK_LABEL (L->err), msg ? msg : "");
  gtk_widget_set_visible (L->err, msg != NULL && *msg != '\0');
  gtk_widget_remove_css_class (L->err, "loginok");
  gtk_widget_add_css_class (L->err, "loginerr");
}

static void
clear_error (void)
{
  gtk_widget_set_visible (L->err, FALSE);
}

static void
reveal_cb (GtkEntry *e, GtkEntryIconPosition pos, gpointer user)
{
  (void) user;
  gboolean hidden = !gtk_entry_get_visibility (e);
  gtk_entry_set_visibility (e, hidden);
  gtk_entry_set_icon_from_icon_name
    (e, pos, hidden ? "view-conceal-symbolic" : "view-reveal-symbolic");
}

static GtkWidget *
entry_new (const gchar *placeholder, gboolean password)
{
  GtkWidget *e = gtk_entry_new ();
  if (placeholder)
    gtk_entry_set_placeholder_text (GTK_ENTRY (e), placeholder);
  gtk_widget_add_css_class (e, "loginentry");
  gtk_widget_set_hexpand (e, TRUE);
  if (password)
    {
      gtk_entry_set_visibility (GTK_ENTRY (e), FALSE);
      gtk_entry_set_input_purpose (GTK_ENTRY (e), GTK_INPUT_PURPOSE_PASSWORD);
      gtk_entry_set_icon_from_icon_name (GTK_ENTRY (e),
                                         GTK_ENTRY_ICON_SECONDARY,
                                         "view-reveal-symbolic");
      g_signal_connect (e, "icon-press", G_CALLBACK (reveal_cb), NULL);
    }
  else
    {
      gtk_entry_set_input_purpose (GTK_ENTRY (e), GTK_INPUT_PURPOSE_NAME);
    }
  return e;
}

static GtkWidget *
field (const gchar *caption, GtkWidget *entry)
{
  GtkWidget *b = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  GtkWidget *l = label (caption, "loginlabel", 0.0f);
  gtk_box_append (GTK_BOX (b), l);
  gtk_box_append (GTK_BOX (b), entry);
  return b;
}

/* ------------------------------------------------------------- actions */
static void
on_success (void)
{
  if (L->done)
    return;
  L->done = TRUE;

  GtkApplication *app = L->app;
  GtkWidget *win = L->win;

  /* the login window is gone for good; build the dashboard through the
   * normal boot path so the whole app (sidebar, probes, refresh) exists */
  gtk_window_destroy (GTK_WINDOW (win));
  acc_ui_boot (app);
}

static void
submit_cb (GtkButton *b, gpointer user)
{
  (void) b;
  (void) user;

  const gchar *name = u (L->user);
  const gchar *pass = u (L->pass);
  gchar *trimmed = g_strstrip (g_strdup (name));
  gboolean remember = gtk_check_button_get_active
    (GTK_CHECK_BUTTON (L->remember));

  if (!*trimmed || !*pass)
    {
      set_error (acc_tr (ACC_T_LOGIN_ERR_EMPTY));
      g_free (trimmed);
      return;
    }

  if (L->create)
    {
      const gchar *pass2 = u (L->pass2);
      if (acc_user_exists ())
        {
          set_error (acc_tr (ACC_T_LOGIN_ERR_EXISTS));
          g_free (trimmed);
          return;
        }
      if (strlen (pass) < 4)
        {
          set_error (acc_tr (ACC_T_LOGIN_ERR_SHORT));
          g_free (trimmed);
          return;
        }
      if (g_strcmp0 (pass, pass2) != 0)
        {
          set_error (acc_tr (ACC_T_LOGIN_ERR_MATCH));
          g_free (trimmed);
          return;
        }
      if (!acc_user_create (trimmed, pass, remember))
        {
          set_error (acc_tr (ACC_T_WRERR));
          g_free (trimmed);
          return;
        }
    }
  else
    {
      if (!acc_user_check (trimmed, pass))
        {
          set_error (acc_tr (ACC_T_LOGIN_ERR_WRONG));
          gtk_editable_set_text (GTK_EDITABLE (L->pass), "");
          g_free (trimmed);
          return;
        }
      acc_user_set_remember (remember);
    }

  g_free (trimmed);
  on_success ();
}

static void
reset_cb (GtkButton *b, gpointer user)
{
  (void) b;
  (void) user;
  acc_user_forget ();
  /* rebuild in create mode with an empty form */
  GtkApplication *app = L->app;
  L->done = TRUE;                 /* do not boot anything */
  gtk_window_destroy (GTK_WINDOW (L->win));
  acc_login_show (app);
}

/* keep whatever the user already typed when switching EN/ع */
static void
rebuild_cb (GtkButton *b, gpointer user)
{
  (void) b;
  (void) user;
  gchar *keep = g_strdup (u (L->user));
  GtkApplication *app = L->app;

  L->done = TRUE;
  gtk_window_destroy (GTK_WINDOW (L->win));

  acc_i18n_toggle ();
  gtk_widget_set_default_direction (acc_i18n_dir ());

  acc_login_show (app);
  if (L && keep && *keep && L->user)
    gtk_editable_set_text (GTK_EDITABLE (L->user), keep);
  g_free (keep);
}

/* light/dark switch on the login page: flips the saved skin and rebuilds
 * the page in place, keeping whatever username was typed. */
static void
theme_cb (GtkButton *b, gpointer user)
{
  (void) b;
  (void) user;
  gchar *keep = L->user ? g_strdup (u (L->user)) : NULL;
  GtkApplication *app = L->app;

  L->done = TRUE;
  gtk_window_destroy (GTK_WINDOW (L->win));

  acc_theme_save_pref (!acc_theme_is_dark ());

  acc_login_show (app);
  if (L && keep && *keep && L->user)
    gtk_editable_set_text (GTK_EDITABLE (L->user), keep);
  g_free (keep);
}

/* Open a terminal running `cmd`. Every terminal gets the same
 * program + flags; the command string is a fixed template built from
 * the absolute executable path, never user input. */
static gboolean
spawn_in_terminal (const gchar *cmd)
{
  static const struct
  {
    const gchar *prog;
    const gchar *pre[3];
  } terms[] = {
    { "ptyxis",             { "--new-window", "--", NULL } },
    { "kgx",                { "--", NULL, NULL } },
    { "gnome-terminal",     { "--", NULL, NULL } },
    { "konsole",            { "-e", NULL, NULL } },
    { "xfce4-terminal",     { "-x", NULL, NULL } },
    { "x-terminal-emulator",{ "-e", NULL, NULL } },
    { "xterm",              { "-e", NULL, NULL } },
  };

  for (gsize i = 0; i < G_N_ELEMENTS (terms); i++)
    {
      gchar *path = g_find_program_in_path (terms[i].prog);
      if (!path)
        continue;

      const gchar *argv[10];
      gint n = 0;
      argv[n++] = path;
      for (gint k = 0; terms[i].pre[k] && n < 7; k++)
        argv[n++] = terms[i].pre[k];
      argv[n++] = "bash";
      argv[n++] = "-c";
      argv[n++] = cmd;
      argv[n] = NULL;

      GError *e = NULL;
      GSubprocessLauncher *l = g_subprocess_launcher_new
        (G_SUBPROCESS_FLAGS_NONE);
      GSubprocess *p = g_subprocess_launcher_spawnv (l, argv, &e);
      g_object_unref (l);
      g_free (path);
      if (p)
        {
          g_object_unref (p);
          return TRUE;
        }
      g_clear_error (&e);
    }
  return FALSE;
}

static void
term_specs_cb (GtkButton *b, gpointer user)
{
  (void) b;
  (void) user;

  gchar *exe = g_file_read_link ("/proc/self/exe", NULL);
  if (!exe)
    {
      set_error (acc_tr (ACC_T_LOGIN_ERR_TERM));
      return;
    }

  gchar *q = g_shell_quote (exe);
  gchar *cmd = g_strdup_printf
    ("%s --specs; echo; printf 'Press Enter to close...'; read -r _", q);

  if (!spawn_in_terminal (cmd))
    {
      gchar *msg = g_strdup_printf ("%s\n%s --specs",
                                    acc_tr (ACC_T_LOGIN_ERR_TERM), exe);
      set_error (msg);
      g_free (msg);
    }

  g_free (cmd);
  g_free (q);
  g_free (exe);
}

/* --------------------------------------------------------------- build */
static GtkWidget *
brand_panel (void)
{
  GtkWidget *side = gtk_box_new (GTK_ORIENTATION_VERTICAL, 14);
  gtk_widget_add_css_class (side, "loginbrand");
  gtk_widget_set_size_request (side, 360, -1);
  gtk_widget_set_hexpand (side, FALSE);
  gtk_widget_set_valign (side, GTK_ALIGN_FILL);

  GtkWidget *inner = gtk_box_new (GTK_ORIENTATION_VERTICAL, 14);
  gtk_widget_set_valign (inner, GTK_ALIGN_CENTER);
  gtk_widget_set_halign (inner, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_start (inner, 40);
  gtk_widget_set_margin_end (inner, 40);

  /* original app logo (assets/logo.png, drawn in logo.svg) */
  {
    gchar *lp = acc_asset_path ("logo.png");
    GtkWidget *pic;
    if (lp)
      {
        pic = gtk_picture_new ();
        gtk_picture_set_filename (GTK_PICTURE (pic), lp);
        gtk_picture_set_content_fit (GTK_PICTURE (pic),
                                     GTK_CONTENT_FIT_CONTAIN);
        gtk_picture_set_can_shrink (GTK_PICTURE (pic), TRUE);
      }
    else
      {
        pic = gtk_image_new_from_icon_name ("computer-symbolic");
        gtk_image_set_pixel_size (GTK_IMAGE (pic), 96);
      }
    g_free (lp);
    gtk_widget_set_size_request (pic, 116, 116);
    gtk_widget_set_halign (pic, GTK_ALIGN_CENTER);
    gtk_box_append (GTK_BOX (inner), pic);
  }

  gtk_box_append (GTK_BOX (inner),
                  label ("ASUS Control Center", "loginbrandtitle", 0.5f));
  {
    GtkWidget *s = label (acc_tr (ACC_T_LOGIN_BRAND_SUB),
                          "loginbrandsub", 0.5f);
    gtk_label_set_justify (GTK_LABEL (s), GTK_JUSTIFY_CENTER);
    gtk_label_set_wrap (GTK_LABEL (s), TRUE);
    gtk_label_set_max_width_chars (GTK_LABEL (s), 30);
    gtk_box_append (GTK_BOX (inner), s);
  }
  {
    GtkWidget *n = label (acc_tr (ACC_T_LOGIN_LOCAL), "loginbrandnote", 0.5f);
    gtk_label_set_justify (GTK_LABEL (n), GTK_JUSTIFY_CENTER);
    gtk_label_set_wrap (GTK_LABEL (n), TRUE);
    gtk_label_set_max_width_chars (GTK_LABEL (n), 30);
    gtk_widget_set_margin_top (n, 18);
    gtk_box_append (GTK_BOX (inner), n);
  }

  gtk_box_append (GTK_BOX (side), inner);
  return side;
}

/* one complete login form; create = first run */
static void
build_window (GtkApplication *app, gboolean create)
{
  L->app = app;
  L->create = create;
  L->done = FALSE;

  L->win = GTK_WIDGET (adw_application_window_new (app));
  gtk_window_set_title (GTK_WINDOW (L->win), "ASUS Control Center");
  gtk_window_set_default_size (GTK_WINDOW (L->win), 940, 700);

  /* slim header bar: window controls + light/dark + language, no duplicate title */
  GtkWidget *tv = adw_toolbar_view_new ();
  GtkWidget *hb = gtk_header_bar_new ();
  {
    GtkWidget *t = gtk_label_new ("ASUS Control Center");
    gtk_widget_add_css_class (t, "hbtitle");
    gtk_header_bar_set_title_widget (GTK_HEADER_BAR (hb), t);

    GtkWidget *lng = gtk_button_new_with_label
      (acc_i18n_lang () == ACC_LANG_EN ? "ع" : "EN");
    gtk_widget_add_css_class (lng, "langpill");
    gtk_widget_set_tooltip_text (lng, acc_tr (ACC_T_LANG));
    g_signal_connect (lng, "clicked", G_CALLBACK (rebuild_cb), NULL);
    gtk_header_bar_pack_end (GTK_HEADER_BAR (hb), lng);

    /* light/dark toggle — same meaning as the one in the main window */
    GtkWidget *tbtn = gtk_button_new ();
    {
      GtkWidget *ti = gtk_image_new_from_icon_name
        (acc_theme_is_dark () ? "weather-clear-symbolic"
                              : "weather-clear-night-symbolic");
      gtk_widget_set_valign (ti, GTK_ALIGN_CENTER);
      gtk_button_set_child (GTK_BUTTON (tbtn), ti);
    }
    gtk_widget_add_css_class (tbtn, "flat");
    gtk_widget_set_tooltip_text (tbtn, acc_tr (ACC_T_TT_THEME));
    g_signal_connect (tbtn, "clicked", G_CALLBACK (theme_cb), NULL);
    gtk_header_bar_pack_end (GTK_HEADER_BAR (hb), tbtn);
  }
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (tv), hb);

  GtkWidget *root = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class (root, "loginroot");
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (tv), root);
  adw_application_window_set_content (ADW_APPLICATION_WINDOW (L->win), tv);

  gtk_box_append (GTK_BOX (root), brand_panel ());

  /* ---------------- form column ---------------- */
  GtkWidget *right = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand (right, TRUE);
  gtk_widget_set_valign (right, GTK_ALIGN_FILL);
  gtk_widget_set_halign (right, GTK_ALIGN_FILL);

  GtkWidget *centre = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand (centre, TRUE);
  gtk_widget_set_vexpand (centre, TRUE);
  gtk_widget_set_valign (centre, GTK_ALIGN_CENTER);
  gtk_widget_set_halign (centre, GTK_ALIGN_CENTER);

  GtkWidget *card = gtk_box_new (GTK_ORIENTATION_VERTICAL, 14);
  gtk_widget_add_css_class (card, "logincard");
  gtk_widget_set_size_request (card, 380, -1);
  gtk_widget_set_margin_start (card, 24);
  gtk_widget_set_margin_end (card, 24);

  L->headline = label (acc_tr (create ? ACC_T_LOGIN_TITLE_UP
                                      : ACC_T_LOGIN_TITLE_IN),
                       "loginhead", 0.0f);
  L->subtitle = label (acc_tr (create ? ACC_T_LOGIN_SUB_UP
                                      : ACC_T_LOGIN_SUB_IN),
                       "loginsub", 0.0f);
  gtk_label_set_wrap (GTK_LABEL (L->subtitle), TRUE);
  gtk_box_append (GTK_BOX (card), L->headline);
  gtk_box_append (GTK_BOX (card), L->subtitle);

  L->user = entry_new (NULL, FALSE);
  if (!create && acc_user_exists () && acc_user_remember ())
    gtk_editable_set_text (GTK_EDITABLE (L->user), acc_user_name ());
  g_signal_connect (L->user, "changed", G_CALLBACK (clear_error), NULL);
  gtk_box_append (GTK_BOX (card),
                  field (acc_tr (ACC_T_LOGIN_USER), L->user));

  L->pass = entry_new (NULL, TRUE);
  gtk_entry_set_activates_default (GTK_ENTRY (L->pass), FALSE);
  gtk_box_append (GTK_BOX (card),
                  field (acc_tr (ACC_T_LOGIN_PASS), L->pass));

  L->pass2 = entry_new (NULL, TRUE);
  L->pass2_box = field (acc_tr (ACC_T_LOGIN_CONFIRM), L->pass2);
  gtk_widget_set_visible (L->pass2_box, create);
  gtk_box_append (GTK_BOX (card), L->pass2_box);

  L->remember = gtk_check_button_new_with_label
    (acc_tr (ACC_T_LOGIN_REMEMBER));
  gtk_widget_add_css_class (L->remember, "logincheck");
  gtk_check_button_set_active
    (GTK_CHECK_BUTTON (L->remember),
     create ? TRUE : acc_user_remember ());
  gtk_box_append (GTK_BOX (card), L->remember);

  L->err = label ("", "loginerr", 0.0f);
  gtk_label_set_wrap (GTK_LABEL (L->err), TRUE);
  gtk_widget_set_visible (L->err, FALSE);
  gtk_box_append (GTK_BOX (card), L->err);

  GtkWidget *go = gtk_button_new_with_label
    (acc_tr (create ? ACC_T_LOGIN_CREATE : ACC_T_LOGIN_SIGNIN));
  gtk_widget_add_css_class (go, "primarybtn");
  gtk_widget_add_css_class (go, "loginbtn");
  gtk_widget_set_hexpand (go, TRUE);
  g_signal_connect (go, "clicked", G_CALLBACK (submit_cb), NULL);
  gtk_box_append (GTK_BOX (card), go);

  {
    /* once signed in, later launches skip this page until Sign out */
    GtkWidget *stay = label (acc_tr (ACC_T_LOGOUT_SUB), "loginhint", 0.5f);
    gtk_label_set_wrap (GTK_LABEL (stay), TRUE);
    gtk_label_set_justify (GTK_LABEL (stay), GTK_JUSTIFY_CENTER);
    gtk_label_set_max_width_chars (GTK_LABEL (stay), 36);
    gtk_box_append (GTK_BOX (card), stay);
  }

  /* sign-in only: a way out when the password is forgotten. The account
   * is local, so resetting it simply removes the stored record. */
  L->reset = gtk_button_new_with_label (acc_tr (ACC_T_LOGIN_RESET));
  gtk_widget_add_css_class (L->reset, "loginlink");
  gtk_widget_set_halign (L->reset, GTK_ALIGN_CENTER);
  gtk_widget_set_visible (L->reset, !create);
  g_signal_connect (L->reset, "clicked", G_CALLBACK (reset_cb), NULL);
  gtk_box_append (GTK_BOX (card), L->reset);

  /* Full hardware + capability report in a terminal: works before an
   * account exists, and never writes anything. */
  {
    GtkWidget *sp = gtk_button_new_with_label (acc_tr (ACC_T_SPECS));
    gtk_widget_add_css_class (sp, "actionbtn");
    gtk_widget_set_margin_top (sp, 6);
    g_signal_connect (sp, "clicked", G_CALLBACK (term_specs_cb), NULL);
    gtk_box_append (GTK_BOX (card), sp);

    GtkWidget *hint = label (acc_tr (ACC_T_SPECS_TERM), "loginhint", 0.5f);
    gtk_label_set_wrap (GTK_LABEL (hint), TRUE);
    gtk_label_set_justify (GTK_LABEL (hint), GTK_JUSTIFY_CENTER);
    gtk_label_set_max_width_chars (GTK_LABEL (hint), 34);
    gtk_box_append (GTK_BOX (card), hint);
  }

  /* Enter anywhere in the form submits */
  g_signal_connect (L->pass, "activate", G_CALLBACK (submit_cb), NULL);
  g_signal_connect (L->pass2, "activate", G_CALLBACK (submit_cb), NULL);
  g_signal_connect (L->user, "activate", G_CALLBACK (submit_cb), NULL);

  gtk_box_append (GTK_BOX (centre), card);
  gtk_box_append (GTK_BOX (right), centre);
  gtk_box_append (GTK_BOX (root), right);

  gtk_window_present (GTK_WINDOW (L->win));
  gtk_widget_grab_focus (L->user);
}

void
acc_login_show (GtkApplication *app)
{
  if (!L)
    L = g_new0 (LoginUI, 1);

  /* skin first: the login page is the first thing the user sees */
  acc_ui_apply_theme ();
  gtk_widget_set_default_direction (acc_i18n_dir ());

  build_window (app, !acc_user_exists ());
}
