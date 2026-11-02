/* control/gfx.h - read-only dGPU power and Integrated-mode capability probe
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * "Integrated (Intel Arc) only" needs a firmware switch that cuts the NVIDIA
 * GPU's power (asus-armoury / asus-nb-wmi `dgpu_disable`). The N6506CU does
 * not expose one: its firmware only offers the display MUX. ASUSCraft
 * therefore never offers Integrated mode on such machines, and it does not
 * drive supergfxd or unbind/unload the NVIDIA driver itself.
 *
 * This module only reads: it reports whether the firmware switch exists,
 * whether supergfxd is running (it manages the same GPU as asusd), whether
 * supergfxd left its NVIDIA blacklist behind after being removed, and the
 * dGPU's runtime power state plus which of the user's own processes keep it
 * awake. Nothing here writes to the system.
 */
#ifndef ACC_GFX_H
#define ACC_GFX_H
#include <glib.h>

G_BEGIN_DECLS

typedef enum
{
  ACC_DGPU_PM_UNKNOWN = -1,
  ACC_DGPU_PM_SUSPENDED = 0,     /* runtime D3: almost no power          */
  ACC_DGPU_PM_ACTIVE = 1
} AccDgpuPm;

typedef struct
{
  gboolean  dgpu_present;        /* an NVIDIA display controller exists   */
  gboolean  has_dgpu_disable;    /* firmware exposes dgpu_disable         */
  gboolean  supergfxd_running;   /* org.supergfxctl.Daemon on system bus  */
  gboolean  supergfx_leftover;   /* its modprobe blacklist outlived it    */
  AccDgpuPm pm;                  /* dGPU runtime power state              */
  gchar     holders[160];        /* own processes with /dev/nvidia* open  */
} AccGfxState;

/* Fill *out. Cheap: the process scan behind `holders` is cached for 10 s.
 * Returns out->dgpu_present. */
gboolean acc_gfx_state (AccGfxState *out);

/* TRUE when Integrated mode could be offered at all. Always FALSE today:
 * a dgpu_disable attribute that ASUSCraft has not verified on the model
 * is reported, never used. */
gboolean acc_gfx_integrated_supported (const AccGfxState *st);

/* Pure helper for tests: TRUE when a modprobe.d text blacklists `nvidia`. */
gboolean acc_gfx_text_blacklists_nvidia (const gchar *text);

G_END_DECLS
#endif
