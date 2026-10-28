/* probe/dmi.h - model / vendor / BIOS (read-only)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_DMI_H
#define ACC_DMI_H
#include <glib.h>

G_BEGIN_DECLS

gchar *dmi_product_name (void);   /* cleaned: trailing _N6506CU removed */
gchar *dmi_product_full (void);
gchar *dmi_vendor (void);
gchar *dmi_bios_version (void);

G_END_DECLS
#endif
