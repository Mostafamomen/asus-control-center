/* probe/gpu.h - GPU telemetry + vendor (read-only)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NVIDIA  → nvidia-smi (fixed argv, no shell, 2 s cache)
 * AMD     → amdgpu sysfs (gpu_busy_percent, mem_info_vram_*, hwmon temp)
 * Intel   → name only (iGPU has no load/temp interface we can trust)
 *
 * The vendor is read from PCI class/vendor ids, never from a model list, so
 * the dashboard badge follows whatever GPU the machine actually has.
 *
 * Fields that the tool cannot report are left at their "unknown" value
 * (-1 / "") and the UI prints an em dash. A 0 % utilisation or an idle
 * display output never means "the dGPU is disabled".
 */
#ifndef ACC_GPU_H
#define ACC_GPU_H
#include <glib.h>

G_BEGIN_DECLS

typedef struct
{
  gboolean ok;
  gchar short_name[64];
  gchar driver[32];        /* NVIDIA driver version, "" when unknown     */
  gint util_pct;           /* -1 unknown                                  */
  gdouble temp_c;          /* -1 unknown                                  */
  gdouble mem_used_gb;     /* -1 unknown                                  */
  gdouble mem_total_gb;    /* -1 unknown                                  */
  gdouble power_w;         /* -1 unknown / not available                  */
  gint display_active;     /* 1 = driving a display, 0 = not, -1 unknown  */
} GpuInfo;

typedef enum
{
  ACC_GPU_UNKNOWN = 0,
  ACC_GPU_NVIDIA,
  ACC_GPU_AMD,
  ACC_GPU_INTEL
} AccGpuVendor;

/* One cached snapshot (2 s) so the 2 s UI refresh does not spawn
 * nvidia-smi several times per tick. */
void gpu_read (GpuInfo *out);

/* discrete GPU first, then whatever display controller exists */
AccGpuVendor gpu_vendor (void);

/* "NVIDIA" / "AMD" / "Intel" / "" */
const gchar *gpu_vendor_label (void);

/* bundled badge image name: "nvidia.png", "amd.png", "intel.png", or NULL */
const gchar *gpu_vendor_asset (void);

G_END_DECLS
#endif
