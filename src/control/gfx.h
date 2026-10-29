/* control/gfx.h - supergfxctl client (iGPU-only / Integrated mode)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This laptop's firmware has no dgpu_disable attribute, so "integrated
 * only" cannot be a firmware write. supergfxd (asus-linux) does it the
 * supported way instead: it blocks the NVIDIA modules and removes the dGPU
 * from the PCI bus, and the switch completes at the next logout.
 *
 * Fixed argv (/usr/bin/supergfxctl, capped by timeout(1)), no shell, never
 * on the GTK main thread. supergfxd runs as root and
 * its D-Bus policy lets the desktop user change the mode, so no polkit
 * helper is involved. Every function returns FALSE when supergfxctl or the
 * daemon is missing, and the UI then explains how to get it.
 */
#ifndef ACC_GFX_H
#define ACC_GFX_H
#include <gio/gio.h>

G_BEGIN_DECLS

typedef enum
{
  ACC_GFX_UNKNOWN = 0,
  ACC_GFX_HYBRID,
  ACC_GFX_INTEGRATED,
  ACC_GFX_OTHER            /* Vfio, AsusEgpu, AsusMuxDgpu, NvidiaNoModeset */
} AccGfxMode;

typedef enum
{
  ACC_GFX_ACT_NONE = 0,
  ACC_GFX_ACT_LOGOUT,
  ACC_GFX_ACT_REBOOT,
  ACC_GFX_ACT_INTEGRATED_FIRST   /* must pass through Integrated first */
} AccGfxAction;

typedef struct
{
  gboolean     known;            /* a probe has finished at least once    */
  gboolean     installed;        /* /usr/bin/supergfxctl exists           */
  gboolean     running;          /* supergfxd answered                    */
  AccGfxMode   mode;             /* live mode                             */
  gboolean     can_integrated;   /* "Integrated" in the supported list    */
  AccGfxMode   pending;          /* requested mode waiting for an action  */
  AccGfxAction action;           /* what the user must do to finish       */
} AccGfxState;

/* Non-blocking: returns the last snapshot (known = FALSE before the first
 * probe) and starts a background probe when it is older than 3 s, so the
 * 2 s page refresh never waits on the daemon. */
gboolean acc_gfx_state (AccGfxState *out);

/* Blocking probe, for the command-line self-test only. */
gboolean acc_gfx_state_sync (AccGfxState *out);

/* Request HYBRID or INTEGRATED in a worker thread; cb runs on the main
 * thread. _finish gives the action that completes the switch, or the
 * daemon's own message in *err (free it). */
void     acc_gfx_set_mode_async  (AccGfxMode mode, GAsyncReadyCallback cb,
                                  gpointer user);
gboolean acc_gfx_set_mode_finish (GAsyncResult *res, AccGfxAction *action,
                                  gchar **err);

G_END_DECLS
#endif
