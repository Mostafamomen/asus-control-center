/* ui/pcmanager.h - PC Manager page
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_PCMANAGER_H
#define ACC_PCMANAGER_H
#include "window.h"
G_BEGIN_DECLS
GtkWidget *acc_page_pcmanager (AccUI *u);
void       acc_pcmanager_teardown (void);
G_END_DECLS
#endif
