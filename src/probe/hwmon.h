/* probe/hwmon.h - fans + temperatures (read-only)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_HWMON_H
#define ACC_HWMON_H
#include <glib.h>

G_BEGIN_DECLS

typedef struct
{
  gboolean fans_ok;
  gint fan_cpu;      /* RPM, -1 unknown */
  gint fan_gpu;
  gdouble t_cpu;     /* °C, -1 unknown  */
  gdouble t_nvme;
  gdouble t_board;   /* generic ACPI zone */
} HwInfo;

void hwmon_read (HwInfo *out);
gdouble hwmon_max_temp_dir (const gchar *hwmon_dir);

G_END_DECLS
#endif
