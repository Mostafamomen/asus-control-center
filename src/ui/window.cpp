/* ui/window.c - main window frame: header, sidebar, stack, refresh loop glue
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "window.h"
#include "home.h"
#include "pages.h"
#include "theme.h"
#include "mcui.h"
#include "login.h"
#include "../control/ctl.h"
#include "../i18n/i18n.h"
#include "../control/brightness.h"
#include "../probe/battery.h"
#include "../probe/dmi.h"
#include "../probe/hwmon.h"
#include "../probe/gpu.h"
#include "../probe/display.h"
#include "../probe/load.h"
#include "../probe/probe.h"
#include "../probe/devs.h"
#include "../probe/sysinfo.h"
#include "../user/user.h"

#include <adwaita.h>

#define APP_ID "io.github.asuscontrolcenter.AsusControlCenter"

/* name shown in the sidebar: the account created on the login page,
 * falling back to the OS user for odd cases (e.g. reset mid-session) */
static const gchar *
display_name (void)
{
  const gchar *n = acc_user_name ();
  if (n && *n)
    return n;
  const gchar *r = g_get_real_name ();
  if (r && *r)
    return r;
  return g_get_user_name ();
}

static AccUI *ui = NULL;
static GtkWidget *g_root = NULL;
static GtkWidget *g_toasts = NULL;
static GtkWidget *g_stack = NULL;
static GtkWidget *g_back = NULL;
static GtkListBox *g_nav = NULL;
static GtkApplication *g_app = NULL;
static GtkCssProvider *g_theme = NULL;

/* collapsible sidebar: closed = icon rail, open = icons + text */
#define SIDE_OPEN_W   250
#define SIDE_CLOSED_W  68
#define SIDE_MARK_PX   22
static GtkWidget *g_side = NULL;        /* the sidebar box              */
static GtkWidget *g_brandname = NULL;   /* app name beside the logo     */
static GtkWidget *g_brandbox  = NULL;   /* logo + app name row          */
static GtkWidget *g_brandmark = NULL;   /* the drawn app mark           */
static GtkWidget *g_uchip = NULL;       /* bottom user row              */
static GtkWidget *g_collapse_img = NULL;/* the toggle button's icon     */
static GPtrArray *g_navlabels = NULL;   /* nav texts to hide            */
static GPtrArray *g_navboxes = NULL;    /* nav rows to re-centre        */
static GPtrArray *g_navgroups = NULL;   /* section headings to hide     */
static gboolean   s_collapsed = FALSE;

/* [ui] sidebar_collapsed — read/written in the same settings.conf the
 * theme and language prefs use; every other key is preserved */
static gchar *
ui_pref_path (void)
{
  return g_build_filename (g_get_user_config_dir (),
                           "asus-control-center", "settings.conf", NULL);
}

static void
sidebar_load_pref (void)
{
  gchar *p = ui_pref_path ();
  GKeyFile *kf = g_key_file_new ();
  if (g_key_file_load_from_file (kf, p, G_KEY_FILE_NONE, NULL))
    s_collapsed = g_key_file_get_boolean (kf, "ui", "sidebar_collapsed", NULL);
  g_key_file_free (kf);
  g_free (p);
}

static void
sidebar_save_pref (gboolean collapsed)
{
  gchar *p = ui_pref_path ();
  GKeyFile *kf = g_key_file_new ();
  g_key_file_load_from_file (kf, p, G_KEY_FILE_NONE, NULL);
  g_key_file_set_boolean (kf, "ui", "sidebar_collapsed", collapsed);
  GError *e = NULL;
  if (!g_key_file_save_to_file (kf, p, &e))
    {
      g_printerr ("sidebar pref not saved: %s\n", e ? e->message : "?");
      g_clear_error (&e);
    }
  g_key_file_free (kf);
  g_free (p);
}

/* closed: icons only · open: icons + text */
static void
sidebar_apply (gboolean collapsed)
{
  s_collapsed = collapsed;

  if (g_side)
    gtk_widget_set_size_request (g_side,
                                 collapsed ? SIDE_CLOSED_W : SIDE_OPEN_W, -1);
  if (g_navlabels)
    for (guint i = 0; i < g_navlabels->len; i++)
      gtk_widget_set_visible ((GtkWidget *) g_ptr_array_index (g_navlabels, i),
                              !collapsed);
  if (g_navboxes)
    for (guint i = 0; i < g_navboxes->len; i++)
      gtk_widget_set_halign ((GtkWidget *) g_ptr_array_index (g_navboxes, i),
                             collapsed ? GTK_ALIGN_CENTER : GTK_ALIGN_FILL);
  if (g_navgroups)
    for (guint i = 0; i < g_navgroups->len; i++)
      gtk_widget_set_visible ((GtkWidget *) g_ptr_array_index (g_navgroups, i),
                              !collapsed);
  if (g_brandname)
    gtk_widget_set_visible (g_brandname, !collapsed);
  if (g_brandbox)
    gtk_widget_set_halign (g_brandbox,
                           collapsed ? GTK_ALIGN_CENTER : GTK_ALIGN_START);
  if (g_uchip)
    gtk_widget_set_visible (g_uchip, !collapsed);
  if (g_collapse_img)
    gtk_image_set_from_icon_name (GTK_IMAGE (g_collapse_img),
                                  "sidebar-show-symbolic");
  if (g_collapse_img && gtk_widget_get_parent (g_collapse_img))
    gtk_widget_set_tooltip_text
      (gtk_widget_get_parent (g_collapse_img),
       acc_tr (collapsed ? ACC_T_SB_EXPAND : ACC_T_SB_COLLAPSE));
}

static void
sidebar_toggle_cb (GtkButton *b, gpointer user)
{
  (void) b;
  (void) user;
  sidebar_apply (!s_collapsed);
  sidebar_save_pref (s_collapsed);
}

static void
about_cb (GtkButton *b, gpointer user)
{
  (void) b;
  (void) user;
  AdwAboutDialog *d =
    ADW_ABOUT_DIALOG (adw_about_dialog_new ());
  adw_about_dialog_set_application_name (d, "ASUS Control Center");
  adw_about_dialog_set_version (d, "0.3");
  adw_about_dialog_set_comments (d, acc_tr (ACC_T_DESC));
  adw_about_dialog_set_license_type (d, GTK_LICENSE_GPL_3_0);
  adw_about_dialog_set_application_icon (d, APP_ID);
  adw_dialog_present (ADW_DIALOG (d), g_root);
}

static void
refresh_cb (GtkButton *b, gpointer user)
{
  (void) b;
  (void) user;
  acc_ui_refresh ();
}

/* Sign out: forget the "stay signed in" flag, close the dashboard and show
 * the login page again. The local account (name + hash) is kept. */
static void
signout_cb (GtkButton *b, gpointer user)
{
  (void) b;
  (void) user;
  acc_user_sign_out ();
  acc_ui_destroy ();
  acc_login_show (g_app);
}

static void
theme_cb (GtkButton *b, gpointer user)
{
  (void) b;
  (void) user;
  acc_ui_theme_toggle ();
}

void
acc_ui_nav_to (const gchar *page)
{
  if (!page)
    return;
  if (g_stack)
    gtk_stack_set_visible_child_name (GTK_STACK (g_stack), page);
  /* keep the sidebar selection in sync when a page is opened from a
   * dashboard/feature button instead of the nav list */
  if (g_nav)
    {
      for (int i = 0; ; i++)
        {
          GtkListBoxRow *row = gtk_list_box_get_row_at_index (g_nav, i);
          if (!row)
            break;
          const gchar *p = (const gchar *) g_object_get_data (G_OBJECT (row), "page");
          if (g_strcmp0 (p, page) == 0)
            {
              gtk_list_box_select_row (g_nav, row);
              break;
            }
        }
    }
  /* the back arrow is only useful away from the dashboard */
  if (g_back)
    gtk_widget_set_visible (g_back, g_strcmp0 (page, "home") != 0);
}

static void
back_cb (GtkButton *b, gpointer user)
{
  (void) b;
  (void) user;
  acc_ui_nav_to ("home");
}

/* Load the stylesheet matching the saved light/dark preference from the
 * source tree (dev) or next to the binary (installed). Replaces any
 * provider applied by a previous boot (theme/language rebuilds). */
static void
acc_theme_load (void)
{
  if (g_theme)
    {
      gtk_style_context_remove_provider_for_display
        (gdk_display_get_default (), GTK_STYLE_PROVIDER (g_theme));
      g_object_unref (g_theme);
      g_theme = NULL;
    }
  gchar *css = acc_theme_css_path ();
  if (css && acc_path_exists (css))
    {
      g_theme = g_object_ref_sink (gtk_css_provider_new ());
      gtk_css_provider_load_from_path (g_theme, css);
      /* USER priority, not APPLICATION: libadwaita installs its own
       * stylesheet above APPLICATION, so our MyASUS rules used to lose
       * the cascade (grey buttons, default cards) even though the file
       * parsed fine. */
      gtk_style_context_add_provider_for_display
        (gdk_display_get_default (), GTK_STYLE_PROVIDER (g_theme),
         GTK_STYLE_PROVIDER_PRIORITY_USER);
    }
  g_free (css);
}

void
acc_ui_toast (const gchar *msg)
{
  if (!g_toasts || !msg)
    return;
  AdwToast *t = adw_toast_new (msg);
  adw_toast_set_timeout (t, 3);
  adw_toast_overlay_add_toast (ADW_TOAST_OVERLAY (g_toasts), t);
}

const gchar *
acc_status_tr (const gchar *hw_status)
{
  if (!hw_status || !*hw_status)
    return "";
  if (strcmp (hw_status, "Charging") == 0)
    return acc_tr (ACC_T_ST_CHG);
  if (strcmp (hw_status, "Discharging") == 0)
    return acc_tr (ACC_T_ST_DIS);
  if (strcmp (hw_status, "Full") == 0)
    return acc_tr (ACC_T_ST_FULL);
  return hw_status;
}

static GtkWidget *
lbl_x (const gchar *text, const gchar *cls, int xalign, gboolean wrap)
{
  GtkWidget *l = gtk_label_new (text);
  gtk_label_set_xalign (GTK_LABEL (l), xalign);
  gtk_label_set_wrap (GTK_LABEL (l), wrap);
  if (cls)
    gtk_widget_add_css_class (l, cls);
  return l;
}

static GtkWidget *
mk_nav_row (const gchar *icon, AccStr txt, const gchar *page)
{
  GtkWidget *row = gtk_list_box_row_new ();
  GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_margin_top (h, 7);
  gtk_widget_set_margin_bottom (h, 7);
  gtk_widget_set_margin_start (h, 12);
  gtk_widget_set_margin_end (h, 12);
  GtkWidget *img = gtk_image_new_from_icon_name (icon);
  gtk_image_set_pixel_size (GTK_IMAGE (img), 18);
  gtk_widget_set_valign (img, GTK_ALIGN_CENTER);
  GtkWidget *lab = gtk_label_new (acc_tr (txt));
  gtk_label_set_xalign (GTK_LABEL (lab), 0.0);
  gtk_widget_set_hexpand (lab, TRUE);
  gtk_widget_set_valign (lab, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (h), img);
  gtk_box_append (GTK_BOX (h), lab);
  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), h);
  /* the icon stays as a tooltip label when the sidebar is collapsed */
  gtk_widget_set_tooltip_text (row, acc_tr (txt));
  if (g_navlabels)
    {
      g_ptr_array_add (g_navlabels, lab);
      g_ptr_array_add (g_navboxes, h);
    }
  g_object_set_data_full (G_OBJECT (row), "page", g_strdup (page), g_free);
  return row;
}

static GtkWidget *
mk_nav_group (AccStr txt)
/* non-selectable uppercase section label above a group of nav rows */
{
  GtkWidget *row = gtk_list_box_row_new ();
  gtk_list_box_row_set_selectable (GTK_LIST_BOX_ROW (row), FALSE);
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
  gtk_widget_add_css_class (row, "navgrouprow");
  GtkWidget *lab = gtk_label_new (acc_tr (txt));
  gtk_label_set_xalign (GTK_LABEL (lab), 0.0);
  gtk_widget_add_css_class (lab, "navgroup");
  gtk_widget_set_margin_top (lab, 10);
  gtk_widget_set_margin_bottom (lab, 2);
  gtk_widget_set_margin_start (lab, 14);
  gtk_widget_set_margin_end (lab, 14);
  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), lab);
  if (g_navgroups)
    g_ptr_array_add (g_navgroups, row);
  return row;
}

static void
on_nav (GtkListBox *l2, GtkListBoxRow *row, gpointer stack)
{
  (void) l2;
  if (!row)
    return;
  const gchar *p = (const gchar *) g_object_get_data (G_OBJECT (row), "page");
  if (p)
    {
      gtk_stack_set_visible_child_name (GTK_STACK (stack), p);
      if (g_back)
        gtk_widget_set_visible (g_back, g_strcmp0 (p, "home") != 0);
    }
}

static void
on_lang_clicked (GtkButton *b2, gpointer user)
{
  (void) b2;
  (void) user;
  acc_ui_lang_toggle ();
}

void
acc_ui_lang_toggle (void)
{
  acc_i18n_toggle ();
  acc_ui_destroy ();
  gtk_widget_set_default_direction (acc_i18n_dir ());
  acc_ui_boot (g_app);
}

void
acc_ui_theme_toggle (void)
{
  acc_theme_save_pref (!acc_theme_is_dark ());
  acc_ui_destroy ();
  acc_ui_boot (g_app);
}

void
acc_ui_apply_theme (void)
{
  /* Read the persisted skin BEFORE forcing the libadwaita color scheme.
   * On a cold start s_dark was still FALSE here, so a saved dark theme
   * used to open with white header bar / sidebar / buttons. */
  acc_theme_load_pref ();

  /* libadwaita widgets (header bar, toasts, scrollbars, checkbox
   * indicators, dialogs) follow the system color scheme; force the
   * scheme matching the saved skin so they match on any desktop. */
  adw_style_manager_set_color_scheme
    (adw_style_manager_get_default (),
     acc_theme_is_dark () ? ADW_COLOR_SCHEME_FORCE_DARK
                          : ADW_COLOR_SCHEME_FORCE_LIGHT);

  /* apply the stylesheet matching the saved light/dark preference */
  acc_theme_load ();
}

void
acc_ui_boot (GtkApplication *app)
{
  g_app = app;

  /* the window was rebuilt (theme / language switch): start with empty
   * registries so no stale widget pointers survive */
  if (g_navlabels)
    {
      g_ptr_array_set_size (g_navlabels, 0);
      g_ptr_array_set_size (g_navboxes, 0);
      g_ptr_array_set_size (g_navgroups, 0);
    }
  else
    {
      g_navlabels = g_ptr_array_new ();
      g_navboxes = g_ptr_array_new ();
      g_navgroups = g_ptr_array_new ();
    }

  /* login page and main window share one skin */
  acc_ui_apply_theme ();

  g_root = GTK_WIDGET (adw_application_window_new (app));
  gtk_window_set_title (GTK_WINDOW (g_root), "ASUS Control Center");
  gtk_window_set_default_size (GTK_WINDOW (g_root), 1180, 760);

  ui = g_new0 (AccUI, 1);

  /* toast overlay wraps everything */
  g_toasts = adw_toast_overlay_new ();

  /* toolbar with header: app name + laptop model centered, actions right */
  GtkWidget *tv = adw_toolbar_view_new ();
  GtkWidget *bar = gtk_header_bar_new ();

  /* back arrow — always returns to the dashboard; hidden while on it */
  g_back = gtk_button_new ();
  {
    GtkWidget *bi = gtk_image_new_from_icon_name ("go-previous-symbolic");
    gtk_widget_set_valign (bi, GTK_ALIGN_CENTER);
    gtk_button_set_child (GTK_BUTTON (g_back), bi);
  }
  gtk_widget_add_css_class (g_back, "flat");
  gtk_widget_set_tooltip_text (g_back, acc_tr (ACC_T_BACK));
  g_signal_connect (g_back, "clicked", G_CALLBACK (back_cb), NULL);
  gtk_header_bar_pack_start (GTK_HEADER_BAR (bar), g_back);
  gtk_widget_set_visible (g_back, FALSE);

  /* open/close the sidebar: closed keeps the icons, open adds the text */
  GtkWidget *cbtn = gtk_button_new ();
  {
    GtkWidget *ci = gtk_image_new_from_icon_name ("sidebar-show-symbolic");
    gtk_widget_set_valign (ci, GTK_ALIGN_CENTER);
    gtk_button_set_child (GTK_BUTTON (cbtn), ci);
    g_collapse_img = ci;
  }
  gtk_widget_add_css_class (cbtn, "flat");
  gtk_widget_set_tooltip_text (cbtn, acc_tr (ACC_T_SB_COLLAPSE));
  g_signal_connect (cbtn, "clicked", G_CALLBACK (sidebar_toggle_cb), NULL);
  gtk_header_bar_pack_start (GTK_HEADER_BAR (bar), cbtn);

  /* the header carries the laptop model only: the app name lives in the
   * sidebar next to the logo, so it is not repeated at the top */
  GtkWidget *hbcol = gtk_box_new (GTK_ORIENTATION_VERTICAL, 1);
  gtk_widget_set_halign (hbcol, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (hbcol, GTK_ALIGN_CENTER);
  {
    gchar *m = dmi_product_name ();
    GtkWidget *msub = lbl_x (m, "hbsub", 0, FALSE);
    gtk_label_set_ellipsize (GTK_LABEL (msub), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars (GTK_LABEL (msub), 40);
    gtk_box_append (GTK_BOX (hbcol), msub);
    g_free (m);
  }
  gtk_header_bar_set_title_widget (GTK_HEADER_BAR (bar), hbcol);

  GtkWidget *langbtn = gtk_button_new_with_label ("EN / ع");
  gtk_widget_add_css_class (langbtn, "langpill");
  g_signal_connect (langbtn, "clicked", G_CALLBACK (on_lang_clicked), NULL);
  gtk_header_bar_pack_end (GTK_HEADER_BAR (bar), langbtn);

  GtkWidget *rbtn = gtk_button_new ();
  {
    GtkWidget *ri = gtk_image_new_from_icon_name
      ("view-refresh-symbolic");
    gtk_widget_set_valign (ri, GTK_ALIGN_CENTER);
    gtk_button_set_child (GTK_BUTTON (rbtn), ri);
  }
  gtk_widget_add_css_class (rbtn, "flat");
  gtk_widget_set_tooltip_text (rbtn, acc_tr (ACC_T_TT_REFRESH));
  g_signal_connect (rbtn, "clicked", G_CALLBACK (refresh_cb), NULL);
  gtk_header_bar_pack_end (GTK_HEADER_BAR (bar), rbtn);

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
  gtk_header_bar_pack_end (GTK_HEADER_BAR (bar), tbtn);

  GtkWidget *mbtn = gtk_menu_button_new ();
  gtk_widget_add_css_class (mbtn, "flat");
  {
    GtkWidget *mi = gtk_image_new_from_icon_name ("open-menu-symbolic");
    gtk_widget_set_valign (mi, GTK_ALIGN_CENTER);
    gtk_menu_button_set_child (GTK_MENU_BUTTON (mbtn), mi);
  }
  gtk_widget_set_tooltip_text (mbtn, acc_tr (ACC_T_TT_MENU));
  {
    GtkWidget *pop = gtk_popover_new ();
    GtkWidget *pbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top (pbox, 6);
    gtk_widget_set_margin_bottom (pbox, 6);
    gtk_widget_set_margin_start (pbox, 6);
    gtk_widget_set_margin_end (pbox, 6);
    GtkWidget *ab = gtk_button_new ();
    gtk_widget_add_css_class (ab, "flat");
    GtkWidget *ah = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    {
      GtkWidget *hi = gtk_image_new_from_icon_name ("help-about-symbolic");
      gtk_widget_set_valign (hi, GTK_ALIGN_CENTER);
      gtk_box_append (GTK_BOX (ah), hi);
    }
    gtk_box_append (GTK_BOX (ah), lbl_x (acc_tr (ACC_T_ABOUT), NULL, 0,
                                         FALSE));
    gtk_button_set_child (GTK_BUTTON (ab), ah);
    g_signal_connect (ab, "clicked", G_CALLBACK (about_cb), NULL);
    gtk_box_append (GTK_BOX (pbox), ab);

    /* sign out: returns to the login page and stops auto sign-in */
    GtkWidget *sb = gtk_button_new ();
    gtk_widget_add_css_class (sb, "flat");
    GtkWidget *sh = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    {
      GtkWidget *si = gtk_image_new_from_icon_name ("system-log-out-symbolic");
      gtk_widget_set_valign (si, GTK_ALIGN_CENTER);
      gtk_box_append (GTK_BOX (sh), si);
    }
    gtk_box_append (GTK_BOX (sh),
                    lbl_x (acc_tr (ACC_T_LOGOUT), NULL, 0, FALSE));
    gtk_button_set_child (GTK_BUTTON (sb), sh);
    gtk_widget_set_tooltip_text (sb, acc_tr (ACC_T_LOGOUT_SUB));
    g_signal_connect (sb, "clicked", G_CALLBACK (signout_cb), NULL);
    gtk_box_append (GTK_BOX (pbox), sb);

    gtk_popover_set_child (GTK_POPOVER (pop), pbox);
    gtk_menu_button_set_popover (GTK_MENU_BUTTON (mbtn), pop);
  }
  gtk_header_bar_pack_end (GTK_HEADER_BAR (bar), mbtn);

  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (tv), bar);

  GtkWidget *root_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class (root_box, "accroot");

  GtkWidget *body = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_vexpand (body, TRUE);

  /* ---------------- sidebar ---------------- */
  GtkWidget *side = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class (side, "acside");
  g_side = side;
  gtk_widget_set_size_request (side, SIDE_OPEN_W, -1);

  /* sidebar brand block, MyASUS style: logo mark with the app name beside
   * it, top-left of the navigation rail (no personal greeting) */
  GtkWidget *greet = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
  g_brandbox = greet;
  gtk_widget_set_halign (greet, GTK_ALIGN_START);
  gtk_widget_set_margin_top (greet, 10);
  gtk_widget_set_margin_bottom (greet, 14);
  gtk_widget_set_margin_start (greet, 20);
  gtk_widget_set_margin_end (greet, 20);
  {
    /* app mark: the dial, drawn by the UI kit at an exact pixel size
     * (a bitmap GtkPicture kept its 256 px natural size and ignored the
     * request — this one cannot) */
    GtkWidget *mark = mc_brand_mark_new (SIDE_MARK_PX);
    g_brandmark = mark;
    gtk_box_append (GTK_BOX (greet), mark);
  }
  {
    GtkWidget *name = lbl_x ("ASUS Control Center", "sidename", 0, FALSE);
    g_brandname = name;
    gtk_label_set_xalign (GTK_LABEL (name), 0.0f);
    gtk_label_set_ellipsize (GTK_LABEL (name), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars (GTK_LABEL (name), 22);
    gtk_widget_set_valign (name, GTK_ALIGN_CENTER);
    gtk_box_append (GTK_BOX (greet), name);
  }
  gtk_box_append (GTK_BOX (side), greet);

  GtkListBox *nav = GTK_LIST_BOX (gtk_list_box_new ());
  g_nav = nav;
  gtk_widget_add_css_class (GTK_WIDGET (nav), "acnav");
  gtk_list_box_set_selection_mode (nav, GTK_SELECTION_SINGLE);
  /* stable page identifiers (used by nav rows and the dashboard ">" buttons) */
  struct { const gchar *icon; AccStr txt; const gchar *page; } navs[] = {
    { "view-grid-symbolic",             ACC_T_NAV_HOME, "home"        },
    { "battery-symbolic",               ACC_T_NAV_BAT,  "battery"     },
    { "video-display-symbolic",         ACC_T_NAV_GPU,  "gpu"         },
    { "display-brightness-symbolic",    ACC_T_NAV_DISP, "display"     },
    { "applications-system-symbolic",   ACC_T_NAV_FANS, "performance" },
    { "org.gnome.Settings-symbolic",    ACC_T_NAV_SYS,  "system"      },
    { "view-grid-symbolic",             ACC_T_NAV_APPS, "apps"        },
    { "emblem-system-symbolic",         ACC_T_NAV_SET,  "settings"    },
    { "help-about-symbolic",            ACC_T_SUPPORT,  "support"     },
  };
  enum { NAV_DEV_END = 6 };   /* rows 0..6 belong to the "Devices" group */
  gtk_list_box_append (nav, mk_nav_group (ACC_T_SEC_DEV));
  for (guint i = 0; i <= NAV_DEV_END; i++)
    gtk_list_box_append (nav, mk_nav_row (navs[i].icon, navs[i].txt,
                                          navs[i].page));
  gtk_list_box_append (nav, mk_nav_group (ACC_T_SEC_PREF));
  for (guint i = NAV_DEV_END + 1; i < G_N_ELEMENTS (navs); i++)
    gtk_list_box_append (nav, mk_nav_row (navs[i].icon, navs[i].txt,
                                          navs[i].page));

  GtkWidget *stack = gtk_stack_new ();
  g_stack = stack;
  gtk_stack_set_transition_type (GTK_STACK (stack),
                                 GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  gtk_widget_set_hexpand (stack, TRUE);
  /* pages sit on a raised panel with a rounded corner (MyASUS 4 frame) */
  gtk_widget_add_css_class (stack, "pagepanel");
  gtk_widget_set_overflow (stack, GTK_OVERFLOW_HIDDEN);
  gtk_stack_add_named (GTK_STACK (stack), acc_home_build (ui), "home");
  gtk_stack_add_named (GTK_STACK (stack), acc_page_bat (ui),  "battery");
  gtk_stack_add_named (GTK_STACK (stack), acc_page_gpu (ui),  "gpu");
  gtk_stack_add_named (GTK_STACK (stack), acc_page_disp (ui), "display");
  gtk_stack_add_named (GTK_STACK (stack), acc_page_perf (ui), "performance");
  gtk_stack_add_named (GTK_STACK (stack), acc_page_sys (ui),  "system");
  gtk_stack_add_named (GTK_STACK (stack), acc_page_apps (ui), "apps");
  gtk_stack_add_named (GTK_STACK (stack), acc_page_settings (ui), "settings");
  gtk_stack_add_named (GTK_STACK (stack), acc_page_support (ui), "support");

  /* restore the collapsed/expanded state from the last session */
  sidebar_load_pref ();

  g_signal_connect (nav, "row-selected", G_CALLBACK (on_nav), stack);
  /* first *selectable* row is now index 1 (index 0 is the "Device settings" label) */
  GtkListBoxRow *first = gtk_list_box_get_row_at_index (nav, 1);
  if (first)
    gtk_list_box_select_row (nav, first);

  GtkWidget *navscroll = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (navscroll),
                                 GTK_WIDGET (nav));
  gtk_widget_set_vexpand (navscroll, TRUE);
  gtk_box_append (GTK_BOX (side), navscroll);

  /* user chip (display-only; power menu arrives later) */
  GtkWidget *uchip = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  g_uchip = uchip;
  gtk_widget_add_css_class (uchip, "userchip");
  gtk_widget_set_margin_start (uchip, 12);
  gtk_widget_set_margin_end (uchip, 12);
  gtk_widget_set_margin_bottom (uchip, 12);
  GtkWidget *uav = gtk_image_new_from_icon_name ("avatar-default-symbolic");
  gtk_image_set_pixel_size (GTK_IMAGE (uav), 26);
  gtk_widget_add_css_class (uav, "avring");
  gtk_widget_set_valign (uav, GTK_ALIGN_CENTER);
  GtkWidget *ulab = gtk_label_new (display_name ());
  gtk_widget_set_hexpand (ulab, TRUE);
  gtk_box_append (GTK_BOX (uchip), uav);
  gtk_box_append (GTK_BOX (uchip), ulab);
  GtkWidget *pwr = gtk_image_new_from_icon_name ("system-shutdown-symbolic");
  gtk_widget_set_valign (pwr, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (uchip), pwr);
  gtk_box_append (GTK_BOX (side), uchip);

  GtkWidget *sep = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
  gtk_widget_add_css_class (sep, "sidesep");
  gtk_box_append (GTK_BOX (body), side);
  gtk_box_append (GTK_BOX (body), sep);
  gtk_box_append (GTK_BOX (body), stack);

  /* footer bar — "ASUS · IN SEARCH OF INCREDIBLE" */
  GtkWidget *footbar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_add_css_class (footbar, "footbar");
  {
    GtkWidget *aw = lbl_x ("ASUS", "asusword", 0, FALSE);
    GtkWidget *sl = lbl_x ("IN SEARCH OF INCREDIBLE", "slogan", 0, FALSE);
    GtkWidget *fill = lbl_x (NULL, NULL, 0, FALSE);
    gtk_widget_set_hexpand (fill, TRUE);
    GtkWidget *lf = lbl_x (acc_tr (ACC_T_FOOT), "leaf", 1, FALSE);
    gtk_box_append (GTK_BOX (footbar), aw);
    gtk_box_append (GTK_BOX (footbar), sl);
    gtk_box_append (GTK_BOX (footbar), fill);
    gtk_box_append (GTK_BOX (footbar), lf);
  }

  gtk_box_append (GTK_BOX (root_box), tv);
  gtk_box_append (GTK_BOX (root_box), footbar);
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (tv), body);
  adw_toast_overlay_set_child (ADW_TOAST_OVERLAY (g_toasts), root_box);
  /* AdwApplicationWindow requires its own content setter:
   * gtk_window_set_child() on it triggers a libadwaita fatal assertion.
   * The whole window sits inside the MyASUS grayscale ("E-Reading")
   * wrapper so the Display page can switch the UI to black & white. */
  mc_gray_set (acc_theme_gray_pref ());
  adw_application_window_set_content (ADW_APPLICATION_WINDOW (g_root),
                                      mc_gray_new (g_toasts));

  gtk_window_present (GTK_WINDOW (g_root));

  /* every widget exists now: apply the saved sidebar state once */
  sidebar_apply (s_collapsed);
  if (g_getenv ("ACC_DEBUG"))
    g_printerr ("sidebar: collapsed=%d width=%d\n",
                s_collapsed, s_collapsed ? SIDE_CLOSED_W : SIDE_OPEN_W);

  acc_ui_refresh ();
}

GtkWidget *
acc_ui_root (void)
{
  return g_root;
}

void
acc_ui_destroy (void)
{
  acc_page_teardown ();
  g_side = NULL;
  g_brandname = NULL;
  g_brandbox = NULL;
  g_brandmark = NULL;
  g_uchip = NULL;
  g_collapse_img = NULL;
  g_stack = NULL;
  if (g_root)
    gtk_window_destroy (GTK_WINDOW (g_root));
  g_root = NULL;
  g_toasts = NULL;
  g_free (ui);
  ui = NULL;
}

void
acc_ui_refresh (void)
{
  if (!ui || !g_root)
    return;

  if (g_getenv ("ACC_DEBUG"))
    {
      static gboolean shown = FALSE;
      /* the first refresh runs before the first layout pass, so wait for a
       * real allocation instead of reporting 0x0 */
      if (!shown && g_brandmark && gtk_widget_get_width (g_brandmark) > 0)
        {
          shown = TRUE;
          g_printerr ("brand mark: request=%d allocated=%dx%d\n",
                      SIDE_MARK_PX,
                      gtk_widget_get_width (g_brandmark),
                      gtk_widget_get_height (g_brandmark));
        }
    }
  gchar buf[96];
  ui->syncing = TRUE;

  LoadInfo ld;
  acc_load_read (&ld);

  BatInfo b;
  battery_read (&b);
  acc_ring_set_value (ui->ring_bat, b.capacity > 0 ? b.capacity / 100.0
                                                   : 0.0);
  g_snprintf (buf, sizeof buf, "%d%%", b.capacity >= 0 ? b.capacity : 0);
  acc_ring_set_big (ui->ring_bat, buf);
  if (ui->bat_small)
    gtk_label_set_text (GTK_LABEL (ui->bat_small), acc_status_tr (b.status));
  if (ui->hero_bat)
    gtk_label_set_text (GTK_LABEL (ui->hero_bat),
                        b.capacity >= 0
                        ? (g_snprintf (buf, sizeof buf, "%s %d%% · %s",
                                       acc_tr (ACC_T_CARD_BATTERY),
                                       b.capacity, acc_status_tr (b.status)),
                           buf)
                        : "—");

  g_snprintf (buf, sizeof buf, "%.1f Wh", b.wh_now);
  gtk_label_set_text (GTK_LABEL (ui->bat_wh_now), b.wh_now > 0 ? buf : "—");
  g_snprintf (buf, sizeof buf, "%.0f Wh", b.wh_design);
  gtk_label_set_text (GTK_LABEL (ui->bat_wh_full), b.wh_design > 0 ? buf : "—");
  g_snprintf (buf, sizeof buf, "%d", b.cycle_count);
  gtk_label_set_text (GTK_LABEL (ui->bat_cycles), b.cycle_count >= 0 ? buf : "—");
  g_snprintf (buf, sizeof buf, "%d%%", b.threshold);
  gtk_label_set_text (GTK_LABEL (ui->bat_limit_val), b.threshold ? buf : "—");
  if (b.threshold)
    gtk_range_set_value (GTK_RANGE (ui->bat_limit_scale), b.threshold);
  /* recommended-limit toggle: on = an 80/60 health limit is active */
  gtk_switch_set_active (GTK_SWITCH (ui->bat_switch),
                         b.threshold > 0 && b.threshold < 100);

  HwInfo h;
  hwmon_read (&h);
  if (h.t_cpu > 0)
    acc_spark_push (ui->spark, h.t_cpu);   /* never record 0° spikes */
  gdouble tv = h.t_cpu > 0 ? h.t_cpu / 100.0 : 0.0;
  acc_ring_set_value (ui->ring_fan, tv);
  g_snprintf (buf, sizeof buf, "%.0f°C", h.t_cpu > 0 ? h.t_cpu : 0);
  acc_ring_set_big (ui->ring_fan, h.t_cpu > 0 ? buf : "—");
  if (ui->fan_small)
    gtk_label_set_text (GTK_LABEL (ui->fan_small), acc_tr (ACC_T_SYS_TEMP));
  g_snprintf (buf, sizeof buf, "%d RPM", h.fan_cpu);
  gtk_label_set_text (GTK_LABEL (ui->fan_cpu), h.fan_cpu >= 0 ? buf : "—");
  g_snprintf (buf, sizeof buf, "%d RPM", h.fan_gpu);
  gtk_label_set_text (GTK_LABEL (ui->fan_gpu), h.fan_gpu >= 0 ? buf : "—");

  /* MyASUS-style stat tiles (honest: "—" when a metric is unreadable) */
  if (ui->tile_bat)
    {
      if (b.capacity >= 0)
        {
          g_snprintf (buf, sizeof buf, "%d%%", b.capacity);
          gtk_label_set_text (GTK_LABEL (ui->tile_bat), buf);
        }
      else
        gtk_label_set_text (GTK_LABEL (ui->tile_bat), "—");
    }
  if (ui->lap_bat)   /* same real value, drawn on the laptop mock */
    {
      if (b.capacity >= 0)
        {
          g_snprintf (buf, sizeof buf, "%d%%", b.capacity);
          gtk_label_set_text (GTK_LABEL (ui->lap_bat), buf);
        }
      else
        gtk_label_set_text (GTK_LABEL (ui->lap_bat), "—");
    }
  if (ui->tile_cpu)
    {
      if (ld.cpu_pct >= 0)
        {
          g_snprintf (buf, sizeof buf, "%.0f%%", ld.cpu_pct);
          gtk_label_set_text (GTK_LABEL (ui->tile_cpu), buf);
        }
      else
        gtk_label_set_text (GTK_LABEL (ui->tile_cpu), "—");
    }
  if (ui->tile_cputemp)
    {
      if (h.t_cpu > 0)
        {
          g_snprintf (buf, sizeof buf, "%.0f°C", h.t_cpu);
          gtk_label_set_text (GTK_LABEL (ui->tile_cputemp), buf);
        }
      else
        gtk_label_set_text (GTK_LABEL (ui->tile_cputemp), "—");
    }
  if (ui->tile_mem)
    {
      if (ld.mem_pct >= 0)
        {
          g_snprintf (buf, sizeof buf, "%.0f%%", ld.mem_pct);
          gtk_label_set_text (GTK_LABEL (ui->tile_mem), buf);
        }
      else
        gtk_label_set_text (GTK_LABEL (ui->tile_mem), "—");
    }
  if (ui->tile_disk)
    {
      if (ld.disk_pct >= 90.0)
        gtk_widget_add_css_class (ui->tile_disk, "tilehot");
      else
        gtk_widget_remove_css_class (ui->tile_disk, "tilehot");
      if (ld.disk_pct >= 0)
        {
          g_snprintf (buf, sizeof buf, "%.0f%%", ld.disk_pct);
          gtk_label_set_text (GTK_LABEL (ui->tile_disk), buf);
        }
      else
        gtk_label_set_text (GTK_LABEL (ui->tile_disk), "—");
    }
  if (ui->tile_disksub)
    {
      if (ld.disk_pct >= 0)
        {
          g_snprintf (buf, sizeof buf, "%.0f / %.0f GB",
                      ld.disk_used_gb, ld.disk_total_gb);
          gtk_label_set_text (GTK_LABEL (ui->tile_disksub), buf);
        }
      else
        gtk_label_set_text (GTK_LABEL (ui->tile_disksub), "—");
    }

  GpuInfo g;
  gpu_read (&g);
  gtk_label_set_text (GTK_LABEL (ui->gpu_name), g.ok ? g.short_name : "—");
  gtk_label_set_text (GTK_LABEL (ui->gpu_vram), g.ok
                      ? (g_snprintf (buf, sizeof buf, "%.0f GB VRAM",
                                     g.mem_total_gb), buf)
                      : "—");
  {
    gchar *muxf = acc_dev_wmi_file ("gpu_mux_mode");
    int mux = muxf ? acc_read_int (muxf, -1) : -1;
    g_free (muxf);
    gboolean nv = acc_ctl_nvidia_loaded ();
    gtk_widget_remove_css_class (ui->gpu_pill, "on");
    gtk_widget_remove_css_class (ui->gpu_pill, "pill");
    gtk_widget_remove_css_class (ui->gpu_pill, "soon");
    if (!nv)
      {
        gtk_label_set_text (GTK_LABEL (ui->gpu_pill), acc_tr (ACC_T_PILL_I));
        gtk_widget_add_css_class (ui->gpu_pill, "pill");
      }
    else if (mux == 0)
      {
        gtk_label_set_text (GTK_LABEL (ui->gpu_pill), acc_tr (ACC_T_PILL_U));
        gtk_widget_add_css_class (ui->gpu_pill, "pill");
      }
    else
      {
        gtk_label_set_text (GTK_LABEL (ui->gpu_pill), acc_tr (ACC_T_PILL_L));
        gtk_widget_add_css_class (ui->gpu_pill, "pill on");
      }
  }
  gtk_label_set_text (GTK_LABEL (ui->gpu_temp), g.temp_c > 0
                      ? (g_snprintf (buf, sizeof buf, "%.0f°C", g.temp_c), buf)
                      : "—");
  gtk_label_set_text (GTK_LABEL (ui->gpu_util), g.util_pct >= 0
                      ? (g_snprintf (buf, sizeof buf, "%d%%", g.util_pct), buf)
                      : "—");
  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (ui->gpu_bar),
                                 g.util_pct >= 0 ? g.util_pct / 100.0 : 0.0);

  DispInfo d;
  display_read (&d);
  gtk_label_set_text (GTK_LABEL (ui->oled_mode), d.ok
                      ? (g_snprintf (buf, sizeof buf, "%.0f Hz",
                                     d.refresh_hz), buf)
                      : "—");

  gdouble sbp = acc_brightness_get ();
  if (sbp >= 0)
    {
      /* don't yank the slider while the user is dragging (3 s window) */
      gint64 idle = g_get_monotonic_time () / 1000 - ui->oled_touch;
      if (idle >= 3000)
        gtk_range_set_value (GTK_RANGE (ui->oled_scale), sbp);
      g_snprintf (buf, sizeof buf, "%.0f%%", sbp);
      gtk_label_set_text (GTK_LABEL (ui->oled_pct), buf);
    }

  acc_page_refresh (ui);

  /* system page strings (static, cheap) */
  gchar *s = sysinfo_os ();
  gtk_label_set_text (GTK_LABEL (ui->sys_os), s);
  g_free (s);
  s = g_strdup_printf ("%.0f GB", sysinfo_ram_gb ());
  gtk_label_set_text (GTK_LABEL (ui->sys_ram), s);
  g_free (s);
  s = sysinfo_nvme_model ();
  gtk_label_set_text (GTK_LABEL (ui->sys_ssd), s);
  g_free (s);
  gtk_label_set_text (GTK_LABEL (ui->sys_temp),
                      h.t_cpu > 0 ? (g_snprintf (buf, sizeof buf,
                                                 "%.0f°C", h.t_cpu), buf)
                                  : "—");

  ui->syncing = FALSE;
}
