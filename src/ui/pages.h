/* ui/pages.h - the seven feature pages (battery, gpu, display, perf,
 * system, apps, settings) — all fully EN/Arabic.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_PAGES_H
#define ACC_PAGES_H
#include "window.h"

G_BEGIN_DECLS

GtkWidget *acc_page_bat      (AccUI *u);
GtkWidget *acc_page_gpu      (AccUI *u);
GtkWidget *acc_page_disp     (AccUI *u);
GtkWidget *acc_page_perf     (AccUI *u);
GtkWidget *acc_page_sys      (AccUI *u);
GtkWidget *acc_page_settings (AccUI *u);
GtkWidget *acc_page_support  (AccUI *u);

/* pushes current sysfs state into the page widgets (called from refresh) */
void acc_page_refresh (AccUI *u);

/* called from acc_ui_destroy so static widget refs don't dangle */
void acc_page_teardown (void);

G_END_DECLS
#endif
