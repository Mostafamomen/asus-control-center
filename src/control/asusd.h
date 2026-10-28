/* control/asusd.h - asusd (asusctl) D-Bus client
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * asusd is the privileged ASUS notebook service (part of asusctl). It owns
 * the firmware writes and enforces its own policy, so talking to it is the
 * supported path on machines where it is installed:
 *
 *   xyz.ljones.Asusd
 *     /xyz/ljones                 xyz.ljones.Platform
 *         PlatformProfile (u, rw)        0 balanced · 1 performance · 2 quiet
 *         PlatformProfileChoices (au)
 *         PlatformProfileOnAc / PlatformProfileOnBattery (u, rw)
 *         ChangePlatformProfileOnAc / ...OnBattery (b, rw)
 *     /xyz/ljones/asus_armoury/gpu_mux_mode   xyz.ljones.AsusArmoury
 *         CurrentValue (i, rw)     1 = hybrid/Optimus · 0 = dGPU only
 *         QueuedGpuValue (i)       -1 = nothing queued
 *
 * GPU notes: asusd deliberately does NOT write the MUX immediately. It
 * queues the value and asus-shutdown.service applies it during shutdown
 * after the NVIDIA stack goes idle and its modules are unloaded — the only
 * sequence the EC accepts in-session. A queued value is therefore not a
 * live change, and it is lost unless asus-shutdown.service is enabled.
 * Every setter here re-reads the service and reports failure unless the
 * service confirms the new value; nothing is ever assumed.
 */
#ifndef ACC_ASUSD_H
#define ACC_ASUSD_H
#include <glib.h>

G_BEGIN_DECLS

/* Logical profile names. The numeric values are OUR order (matching the
 * AC/battery names in asusd's own config), not asusd's wire enum. */
typedef enum
{
  ACC_PROF_UNKNOWN     = -1,
  ACC_PROF_QUIET       = 0,
  ACC_PROF_BALANCED    = 1,
  ACC_PROF_PERFORMANCE = 2,
  ACC_PROF_LOW_POWER   = 3
} AccProfile;

typedef struct
{
  gboolean   available;        /* asusd is on the system bus              */
  AccProfile active;           /* what the firmware runs right now        */
  AccProfile on_ac;            /* asusd's configured AC profile           */
  AccProfile on_bat;           /* asusd's configured battery profile      */
  gboolean   change_on_ac;     /* asusd applies on_ac automatically       */
  gboolean   change_on_bat;
  gboolean   has_choices;      /* PlatformProfileChoices was readable     */
  gboolean   choice_quiet, choice_balanced, choice_performance;
  gboolean   choice_low_power;
} AsusdProfileState;

typedef struct
{
  gboolean available;          /* asusd + the armoury object are present   */
  gint     current;            /* live MUX value, -1 when unknown          */
  gint     queued;             /* asusd queue, -1 when nothing is queued   */
  gboolean allow_hybrid, allow_ultimate;
  gboolean shutdown_ready;     /* asus-shutdown.service enabled or active  */
} AsusdGpuState;

/* TRUE when the asusd name is currently owned on the system bus. */
gboolean acc_asusd_available (void);

/* Snapshot of the platform-profile interface. FALSE when asusd is not
 * reachable; the struct is still zeroed with UNKNOWN values. */
gboolean acc_asusd_profiles (AsusdProfileState *out);

/* Set the ACTIVE profile. Verified by reading the property back. */
gboolean acc_asusd_set_profile (AccProfile p, gchar **err);

/* Snapshot of the GPU MUX interface (read-only, no writes). */
gboolean acc_asusd_gpu (AsusdGpuState *out);

/* Queue a MUX change through asusd (0 = ultimate, 1 = hybrid).
 * Verified against QueuedGpuValue; the change applies at shutdown. */
gboolean acc_asusd_set_gpu_mux (gint mode, gchar **err);

/* "quiet" / "balanced" / "performance" for the sysfs fallback, or NULL */
const gchar *acc_profile_key (AccProfile p);

G_END_DECLS
#endif
