/* probe/sysinfo.h - OS / RAM / storage / CPU strings (read-only)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_SYSINFO_H
#define ACC_SYSINFO_H
#include <glib.h>

G_BEGIN_DECLS

gchar  *sysinfo_os (void);        /* PRETTY_NAME from os-release */
gdouble sysinfo_ram_gb (void);    /* /proc/meminfo */
gchar  *sysinfo_nvme_model (void);
gchar  *sysinfo_cpu (void);       /* first "model name" line */

G_END_DECLS
#endif
