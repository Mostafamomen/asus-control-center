/* probe/load.h - honest system load metrics (CPU / memory / disk)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Everything here is real and read-only:
 *  - CPU util: /proc/stat delta between two calls (first call returns -1)
 *  - Memory:   /proc/meminfo MemTotal / MemAvailable
 *  - Disk:     statvfs("/") used/total in GiB
 */
#ifndef ACC_PROBE_LOAD_H
#define ACC_PROBE_LOAD_H
#include <glib.h>

G_BEGIN_DECLS

typedef struct {
  gdouble cpu_pct;       /* 0..100, or -1 until the second call   */
  gdouble mem_pct;       /* 0..100, or -1 if unreadable            */
  gdouble mem_total_gb;
  gdouble disk_used_gb;  /* -1 if statvfs fails                    */
  gdouble disk_total_gb; /* -1 if statvfs fails                    */
  gdouble disk_pct;      /* 0..100 (used/total), or -1             */
} LoadInfo;

void acc_load_read (LoadInfo *out);

G_END_DECLS
#endif
