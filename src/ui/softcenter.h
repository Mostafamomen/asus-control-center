/* ui/softcenter.h - Software Center page (read-only)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_SOFTCENTER_H
#define ACC_SOFTCENTER_H
#include "window.h"
G_BEGIN_DECLS
GtkWidget *acc_page_softcenter (AccUI *u);
void       acc_softcenter_teardown (void);
G_END_DECLS
#endif