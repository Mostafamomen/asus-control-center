/* ui/sysdiag.h - System Diagnosis page
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_SYSDIAG_H
#define ACC_SYSDIAG_H
#include "window.h"
G_BEGIN_DECLS

GtkWidget *acc_sysdiag_page    (AccUI *u);
void       acc_sysdiag_teardown (void);

G_END_DECLS
#endif
