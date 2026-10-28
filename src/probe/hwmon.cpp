/* probe/hwmon.c - fans + temperatures (read-only)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "hwmon.h"
#include "probe.h"

static gchar *
find_hwmon_by_name (const gchar *want)
{
  GDir *d = g_dir_open ("/sys/class/hwmon", 0, NULL);
  if (!d)
    return NULL;
  const gchar *n;
  gchar *ret = NULL;
  while ((n = g_dir_read_name (d)) != NULL)
    {
      gchar *np = g_build_filename ("/sys/class/hwmon", n, "name", NULL);
      gchar *nm = acc_read_trim (np);
      g_free (np);
      if (nm && strcmp (nm, want) == 0)
        ret = g_build_filename ("/sys/class/hwmon", n, NULL);
      g_free (nm);
      if (ret)
        break;
    }
  g_dir_close (d);
  return ret;
}

gdouble
hwmon_max_temp_dir (const gchar *dir)
{
  if (!dir)
    return -1;
  GDir *d = g_dir_open (dir, 0, NULL);
  if (!d)
    return -1;
  gdouble best = -1;
  const gchar *n;
  while ((n = g_dir_read_name (d)) != NULL)
    {
      if (!g_str_has_prefix (n, "temp") || !g_str_has_suffix (n, "_input"))
        continue;
      gchar *p = g_build_filename (dir, n, NULL);
      gint mv = acc_read_int (p, -1);
      g_free (p);
      gdouble c = mv / 1000.0;
      if (c > best)
        best = c;
    }
  g_dir_close (d);
  return best;
}

static gint
fan_rpm (const gchar *hw, const gchar *key)
{
  if (!hw)
    return -1;
  gchar *p = g_build_filename (hw, g_strdup_printf ("%s_input", key), NULL);
  gint v = acc_read_int (p, -1);
  g_free (p);
  return v;
}

/* generic ACPI zone temperature: pick first thermal zone with type acpitz */
static gdouble
board_temp (void)
{
  GDir *d = g_dir_open ("/sys/class/thermal", 0, NULL);
  if (!d)
    return -1;
  gdouble ret = -1;
  const gchar *n;
  while ((n = g_dir_read_name (d)) != NULL)
    {
      if (!g_str_has_prefix (n, "thermal_zone"))
        continue;
      gchar *tp = g_build_filename ("/sys/class/thermal", n, "type", NULL);
      gchar *t = acc_read_trim (tp);
      g_free (tp);
      if (!t || strcmp (t, "acpitz") != 0)
        {
          g_free (t);
          continue;
        }
      g_free (t);
      gchar *vp = g_build_filename ("/sys/class/thermal", n, "temp", NULL);
      gdouble c = acc_read_int (vp, -1) / 1000.0;
      g_free (vp);
      if (c > 0)
        ret = c;
      break;
    }
  g_dir_close (d);
  return ret;
}

void
hwmon_read (HwInfo *o)
{
  memset (o, 0, sizeof *o);
  o->fan_cpu = o->fan_gpu = -1;
  o->t_cpu = o->t_nvme = o->t_board = -1;

  gchar *asus = find_hwmon_by_name ("asus");
  if (asus)
    o->fans_ok = TRUE;
  o->fan_cpu = fan_rpm (asus, "fan1");
  o->fan_gpu = fan_rpm (asus, "fan2");
  g_free (asus);

  gchar *ct = find_hwmon_by_name ("coretemp");
  o->t_cpu = hwmon_max_temp_dir (ct);
  g_free (ct);
  gchar *nv = find_hwmon_by_name ("nvme");
  o->t_nvme = hwmon_max_temp_dir (nv);
  g_free (nv);
  o->t_board = board_temp ();
}
