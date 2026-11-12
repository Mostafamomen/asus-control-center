/* ui/hubs.h - ASUSCraft section hubs (Customization, Hardware, Diagnosis)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_HUBS_H
#define ACC_HUBS_H
#include "window.h"

G_BEGIN_DECLS

GtkWidget *acc_page_customization (AccUI *u);
GtkWidget *acc_page_hardware      (AccUI *u);
GtkWidget *acc_page_diagnosis     (AccUI *u);
GtkWidget *acc_page_updates       (AccUI *u);

/* Periodic live refresh for the hardware/diagnosis pages. */
void       acc_hubs_refresh (void);
void       acc_hubs_teardown (void);

/* Home/shortcut buttons still use the old page ids; map them to the new
 * section and select the matching tab. */
const gchar *acc_hubs_parent (const gchar *page);
gboolean     acc_hubs_select (const gchar *page);

G_END_DECLS
#endif
