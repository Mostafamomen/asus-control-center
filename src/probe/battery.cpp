/* probe/battery.c - read-only battery telemetry
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "battery.h"
#include "probe.h"
#include "devs.h"

static gdouble
f (gchar *dir, const gchar *name)
{
  gchar *p = g_build_filename (dir, name, NULL);
  gchar *s = acc_read_trim (p);
  g_free (p);
  if (!s)
    return -1.0;
  gdouble v = g_ascii_strtod (s, NULL);
  g_free (s);
  return v;
}

void
battery_read (BatInfo *o)
{
  memset (o, 0, sizeof *o);
  o->ok = FALSE;
  o->cycle_count = o->capacity = -1;
  o->wh_now = o->wh_full = o->wh_design = -1.0;
  o->power_watts_x10 = -1;

  gchar *d = g_strdup (acc_dev_battery_dir ());
  if (!d)
    return;
  o->ok = TRUE;

  o->capacity = (gint) f (d, "capacity");
  o->wh_now = f (d, "energy_now") / 1e6;
  o->wh_full = f (d, "energy_full") / 1e6;
  o->wh_design = f (d, "energy_full_design") / 1e6;

  /* Some ECs expose charge (µAh) + voltage instead of energy (µWh).
   * Convert with the live voltage: Wh = µAh * µV / 1e12. */
  if (o->wh_now < 0)
    {
      gdouble charge = f (d, "charge_now");
      gdouble volt = f (d, "voltage_now");
      if (charge >= 0 && volt > 0)
        o->wh_now = charge * volt / 1e12;
    }
  if (o->wh_full < 0)
    {
      gdouble charge = f (d, "charge_full");
      gdouble volt = f (d, "voltage_now");
      if (charge >= 0 && volt > 0)
        o->wh_full = charge * volt / 1e12;
    }
  if (o->wh_design < 0)
    {
      gdouble charge = f (d, "charge_full_design");
      gdouble volt = f (d, "voltage_min_design");
      if (charge >= 0 && volt > 0)
        o->wh_design = charge * volt / 1e12;
    }

  o->cycle_count = (gint) f (d, "cycle_count");
  gdouble pw = f (d, "power_now");
  if (pw > 0)
    o->power_watts_x10 = (gint) (pw / 1e8);

  /* the file name differs between kernel generations: both are accepted */
  gchar *tp = acc_dev_charge_limit_file ();
  o->has_threshold = tp != NULL;
  o->threshold = 0;
  if (tp)
    {
      o->threshold = acc_read_int (tp, 0);
      g_free (tp);
    }

  gchar *sp = g_build_filename (d, "status", NULL);
  gchar *st = acc_read_trim (sp);
  if (st)
    {
      strncpy (o->status, st, sizeof o->status - 1);
      o->status[sizeof o->status - 1] = '\0';
    }
  g_free (st);
  g_free (sp);
  g_free (d);
}
