/* ui/specs.h - "Laptop specifications" dialog
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_SPECS_H
#define ACC_SPECS_H
#include <gtk/gtk.h>

G_BEGIN_DECLS

/* Modal summary of the real hardware probes (read-only, no fake values).
 * parent may be NULL. */
void acc_specs_show (GtkWindow *parent);

/* A ready-made button that opens the dialog for the window it lives in. */
GtkWidget *acc_specs_button (const gchar *label, const gchar *css_class);

G_END_DECLS
#endif
