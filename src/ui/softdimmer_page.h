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

G_BEGIN_DECLS

GtkWidget *acc_page_softdimmer (AccUI *u);

G_END_DECLS

#endif /* ACC_UI_SOFTDIMMER_PAGE_H */
