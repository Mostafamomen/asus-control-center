/* probe/battery.h - read-only battery telemetry
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_BATTERY_H
#define ACC_BATTERY_H
#include <glib.h>

G_BEGIN_DECLS

typedef struct
{
  gboolean ok;
  gint capacity;        /* % */
  gdouble wh_now;       /* energy_now / 1e6  */
  gdouble wh_full;      /* energy_full / 1e6 */
  gdouble wh_design;    /* design / 1e6      */
  gint cycle_count;     /* -1 unknown        */
  gint power_watts_x10; /* power_now uW /1e8 (1 decimal) */
  gboolean has_threshold;      /* charge_control_end_threshold exists */
  gint threshold;              /* current limit % (0 if unsupported) */
  gchar status[32];
} BatInfo;

void battery_read (BatInfo *out);

G_END_DECLS
#endif
