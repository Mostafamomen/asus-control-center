/* ui/softdimmer_page.h - native Soft Dimmer page inside ASUSCraft.
 *
 * Hardware → Display → Soft Dimmer. All controls talk to the C++
 * Soft Dimmer engine (src/control/softdimmer/brightness_engine.*);
 * the GNOME Shell overlay and Quick Settings stay in sync via D-Bus.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_UI_SOFTDIMMER_PAGE_H
#define ACC_UI_SOFTDIMMER_PAGE_H

#include "window.h"
#include "../control/softdimmer/brightness_engine.h"
#include "../control/softdimmer/monitor_manager.h"
#include "../control/softdimmer/softdimmer_service.h"

/* Application-owned Soft Dimmer engine instances (created in main.cpp).
 * The Soft Dimmer page borrows them; it does not own them. */
extern AccSdBrightnessEngine *g_sd_engine;
extern AccSdMonitorManager   *g_sd_monitors;
extern AccSdSoftDimmerService *g_sd_service;

G_BEGIN_DECLS

GtkWidget *acc_page_softdimmer (AccUI *u);

G_END_DECLS

#endif /* ACC_UI_SOFTDIMMER_PAGE_H */
