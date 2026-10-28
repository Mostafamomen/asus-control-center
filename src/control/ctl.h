/* control/ctl.h - privileged write layer via polkit (M2)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Every user-approved move (perf mode battery limit, gpu mux) goes through
 * `pkexec /usr/libexec/asus-control-center/acc-helper <action> <arg>`:
 *   - pkexec is the absolute /usr/bin/pkexec
 *   - the helper is whitelisted (fixed paths + fixed allowed values)
 *   - both are shipped in data/ and installed by install.sh
 * There is no shell between the app and the helper.
 */
#ifndef ACC_CTL_H
#define ACC_CTL_H
#include <glib.h>

G_BEGIN_DECLS

gboolean acc_ctl_available (void);   /* helper installed + pkexec present */

/* TRUE when the installed helper predates the asus-armoury write path.
 * Kernel >= 6.17 needs helper v2; an old helper writes to a read-only
 * legacy node and fails with EIO, so the UI must say "run ./install.sh". */
gboolean acc_ctl_helper_stale (void);

/* action: one of the helper's whitelisted verbs (set-perf, set-charge, ...)
 * arg:    the validated value to pass through
 * err:    optional returns a human-readable failure reason */
gboolean acc_ctl_run (const gchar *action, const gchar *arg, gchar **err);

/* convenience wrappers */
gboolean acc_ctl_set_perf        (const gchar *profile);
gboolean acc_ctl_set_charge      (int pct);
gboolean acc_ctl_set_charge_mode (int mode);   /* 0/1/2 */
gboolean acc_ctl_set_gpu_mux     (int mode);   /* 1 hybrid, 0 dGPU only */
gboolean acc_ctl_set_kbd         (int level);  /* 0..3 */

/* read-only: is the NVIDIA driver loaded right now? Driver state is NOT a
 * GPU mode and never used as one (the MUX is a separate firmware setting). */
gboolean acc_ctl_nvidia_loaded (void);

/* exit status of the last helper/pkexec run:
 * 0 ok · 3 modules in use · 127 auth dismissed · other generic failure */
int acc_ctl_last_exit (void);

G_END_DECLS
#endif
