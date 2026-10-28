/* ui/home.h - dashboard page builder (the MyASUS-style home screen)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_HOME_H
#define ACC_HOME_H
#include <gtk/gtk.h>
#include "window.h"

G_BEGIN_DECLS

/* Builds the dashboard into widgets and wires their refs into @ui.
 * The returned widget must be added to a GtkStack afterwards. */
GtkWidget *acc_home_build (AccUI *ui);

G_END_DECLS
#endif
