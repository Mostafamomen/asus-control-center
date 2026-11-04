/* ui/systest.h - System Testing page (interactive hardware tests)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_SYSTEST_H
#define ACC_SYSTEST_H
#include "window.h"

G_BEGIN_DECLS

GtkWidget *acc_systest_page     (AccUI *u);
void       acc_systest_teardown (void);

G_END_DECLS
#endif
