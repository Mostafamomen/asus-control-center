/* ui/quickactions.cpp - Quick Actions page
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "quickactions.h"
#include "theme.h"
#include "mcui.h"
#include "window.h"
#include "../i18n/i18n.h"
#include <adwaita.h>
#include <glib.h>

static void
qa_cb (GtkButton *b, gpointer p)
{
  (void) b;
  acc_ui_nav_to ((const gchar *) p);
}

GtkWidget *
acc_page_quickactions (AccUI *u)
{
  (void) u;
  GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 18);

  GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_add_css_class (h, "pagehead");
  GtkWidget *col = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
  gtk_widget_set_hexpand (col, TRUE);
  gtk_widget_set_valign (col, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (col), mc_label ("ASUSCraft", "eyebrow", 0));
  gtk_box_append (GTK_BOX (col), mc_label ("Quick Actions", "pagetitle", 0));
  GtkWidget *sub = mc_label ("Shortcuts to existing ASUSCraft tools.", "pagesub", 0);
  gtk_label_set_wrap (GTK_LABEL (sub), TRUE);
  gtk_label_set_max_width_chars (GTK_LABEL (sub), 80);
  gtk_box_append (GTK_BOX (col), sub);
  gtk_box_append (GTK_BOX (h), col);
  gtk_box_append (GTK_BOX (page), h);

  const char *acts[][2] = {
    { "Open Storage Manager", "pcmanager" },
    { "Open Process Manager", "sysmgr" },
    { "Open Startup Manager", "sysmgr" },
    { "Open Software Center", "softcenter" },
    { "Open Updates Center", "support" },
    { "Open Hardware Diagnostics", "diagnosis" },
    { "Open Battery Care", "battery" },
  };
  for (int i = 0; i < 7; i++)
    {
      GtkWidget *b = gtk_button_new_with_label (acts[i][0]);
      gtk_widget_add_css_class (b, "actionbtn");
      gtk_widget_set_halign (b, GTK_ALIGN_START);
      g_signal_connect (b, "clicked", G_CALLBACK (qa_cb), (gpointer) acts[i][1]);
      gtk_box_append (GTK_BOX (page), b);
    }

  return mc_page (page);
}

void
acc_quickactions_teardown (void)
{
}