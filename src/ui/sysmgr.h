/* ui/sysmgr.h - System Manager page (PC Manager-inspired, read-only)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_SYSMGR_H
#define ACC_SYSMGR_H
#include "window.h"
G_BEGIN_DECLS
GtkWidget *acc_page_sysmgr (AccUI *u);
void       acc_sysmgr_teardown (void);
gboolean   acc_sysmgr_selftest (void);
G_END_DECLS
#endif
