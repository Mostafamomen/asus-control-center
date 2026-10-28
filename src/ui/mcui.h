/* ui/mcui.h - MyASUS-style UI kit (reusable component library)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A small, self-contained widget library that reproduces the MyASUS
 * desktop look with plain GTK4 + libadwaita:
 *
 *   mc_page()      scrolled page body with the MyASUS margins
 *   mc_panel()     white card with the blue leading accent ("accentcard")
 *   mc_card()      plain white card
 *   mc_group()     uppercase, letter-spaced section label
 *   mc_badge()     rounded blue-tinted icon badge
 *   mc_feature()   three-column block: icon | title + description | controls
 *   mc_row()       label ......... value (read-only telemetry line)
 *   mc_switch()    libadwaita switch
 *   mc_primary()   filled MyASUS-blue call-to-action button
 *   mc_link()      filled call-to-action that opens a URL
 *   mc_pill()      small rounded pill button (language etc.)
 *   mc_grayscale   wrapper that can render its whole subtree in grayscale
 *
 * Every widget uses CSS classes defined in BOTH theme.css and theme-dark.css,
 * so the kit is theme-agnostic: build with the kit, get both skins free.
 */
#ifndef ACC_MCUI_H
#define ACC_MCUI_H
#include <gtk/gtk.h>

G_BEGIN_DECLS

/* text ------------------------------------------------------------------ */
GtkWidget *mc_label  (const gchar *text, const gchar *cls, int xalign);
GtkWidget *mc_group  (const gchar *text);            /* uppercase micro-head */
GtkWidget *mc_title  (const gchar *text);            /* card heading         */

/* containers ------------------------------------------------------------ */
GtkWidget *mc_card   (void);                         /* plain white card     */
GtkWidget *mc_panel  (const gchar *title);           /* accent card + header */
GtkWidget *mc_page   (GtkWidget *child);             /* scrollable page body */

/* MyASUS three-column setting block: icon | text | controls ------------- */
GtkWidget *mc_feature (const gchar *icon, const gchar *title,
                       const gchar *desc, GtkWidget **out_ctl);

/* small pieces ---------------------------------------------------------- */
GtkWidget *mc_badge  (const gchar *icon, int pixel); /* blue icon badge      */
GtkWidget *mc_row    (const gchar *label, GtkWidget **out_value);
GtkWidget *mc_switch (gboolean active);
GtkWidget *mc_primary (const gchar *label);
GtkWidget *mc_link   (const gchar *url, const gchar *label);
GtkWidget *mc_pill   (const gchar *label);
GtkWidget *mc_spacer (void);                         /* hexpand filler label */

/* app mark: rounded tile + dial, drawn with cairo so the pixel size is
 * exact at any size and HiDPI scale factor ------------------------------ */
GtkWidget *mc_brand_mark_new (int px);

/* grayscale ("E-Reading") wrapper --------------------------------------- */
GtkWidget *mc_gray_new     (GtkWidget *child);
void       mc_gray_set     (gboolean on);            /* global, redraws all  */
gboolean   mc_gray_get     (void);

G_END_DECLS
#endif
