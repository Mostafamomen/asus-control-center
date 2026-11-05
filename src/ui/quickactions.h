/* ui/quickactions.h - Quick Actions page
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_QUICKACTIONS_H
#define ACC_QUICKACTIONS_H
#include "window.h"
G_BEGIN_DECLS
GtkWidget *acc_page_quickactions (AccUI *u);
void       acc_quickactions_teardown (void);
G_END_DECLS
#endif
