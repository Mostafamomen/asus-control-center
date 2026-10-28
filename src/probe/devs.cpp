/* probe/devs.c - portable device discovery (read-only)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "devs.h"
#include "probe.h"

#include <stdlib.h>

/* new-style ASUS firmware attributes (kernel >= 6.17) */
#define ARMOURY "/sys/class/firmware-attributes/asus-armoury/attributes"

gchar *
acc_dev_armoury_file (const gchar *attr)
{
  if (!attr || !*attr)
    return NULL;
  gchar *p = g_build_filename (ARMOURY, attr, "current_value", NULL);
  if (acc_path_exists (p))
    return p;
  g_free (p);
  return NULL;
}

gboolean
acc_dev_on_ac (void)
{
  GDir *d = g_dir_open ("/sys/class/power_supply", 0, NULL);
  if (!d)
    return FALSE;
  gboolean on_ac = FALSE;
  const gchar *name;
  while ((name = g_dir_read_name (d)))
    {
      gchar *tf = g_build_filename ("/sys/class/power_supply", name, "type", NULL);
      gchar *type = acc_read_trim (tf);
      g_free (tf);
      gboolean is_mains = g_strcmp0 (type, "Mains") == 0;
      g_free (type);
      if (!is_mains)
        continue;
      gchar *of = g_build_filename ("/sys/class/power_supply", name, "online", NULL);
      on_ac = acc_read_int (of, 0) == 1;
      g_free (of);
      break;
    }
  g_dir_close (d);
  return on_ac;
}

gboolean
acc_dev_pending_reboot (void)
{
  gchar *p = g_build_filename (ARMOURY, "pending_reboot", NULL);
  gchar *v = acc_read_trim (p);
  g_free (p);
  gboolean pending = v && *v && atoi (v) != 0;
  g_free (v);
  return pending;
}

static gchar *s_bat = NULL;      /* cached battery directory   */
static gboolean s_bat_done = FALSE;
static gchar *s_wmi = NULL;      /* cached ASUS WMI directory  */
static gboolean s_wmi_done = FALSE;
static gchar *s_kbd = NULL;      /* cached kbd LED file        */
static gboolean s_kbd_done = FALSE;

/* first directory under base whose "type" file says Battery and that has a
 * readable capacity — same selection the EC makes on 1- and 2-battery units */
static gchar *
find_battery (void)
{
  GDir *d = g_dir_open ("/sys/class/power_supply", 0, NULL);
  if (!d)
    return NULL;

  gchar *best = NULL;
  const gchar *n;
  while ((n = g_dir_read_name (d)) != NULL)
    {
      gchar *dir = g_build_filename ("/sys/class/power_supply", n, NULL);
      gchar *tp = g_build_filename (dir, "type", NULL);
      gchar *t = acc_read_trim (tp);
      gchar *cp = g_build_filename (dir, "capacity", NULL);
      gboolean is_bat = g_strcmp0 (t, "Battery") == 0
                        && acc_path_exists (cp);

      g_free (tp);
      g_free (t);
      g_free (cp);

      if (is_bat)
        {
          if (g_strcmp0 (n, "BAT0") == 0)
            {
              /* the conventional name wins, but keep scanning is over */
              g_free (best);
              best = dir;
              break;
            }
          if (!best)
            best = dir;
          else
            g_free (dir);
        }
      else
        g_free (dir);
    }
  g_dir_close (d);
  return best;
}

const gchar *
acc_dev_battery_dir (void)
{
  if (!s_bat_done)
    {
      s_bat = find_battery ();
      s_bat_done = TRUE;
    }
  return s_bat;
}

const gchar *
acc_dev_battery_name (void)
{
  const gchar *d = acc_dev_battery_dir ();
  return d ? strrchr (d, '/') + 1 : "";
}

gchar *
acc_dev_battery_file (const gchar *leaf)
{
  const gchar *d = acc_dev_battery_dir ();
  if (!d || !leaf)
    return NULL;
  return g_build_filename (d, leaf, NULL);
}

gchar *
acc_dev_charge_limit_file (void)
{
  /* kernels/drivers disagree on the name: try both before giving up */
  static const gchar *names[] = {
    "charge_control_end_threshold",
    "charge_stop_threshold",
  };
  for (gsize i = 0; i < G_N_ELEMENTS (names); i++)
    {
      gchar *p = acc_dev_battery_file (names[i]);
      if (p && acc_path_exists (p))
        return p;
      g_free (p);
    }
  return NULL;
}

/* anywhere in /sys/devices/platform that looks like the ASUS WMI node */
static gchar *
find_wmi (void)
{
  static const gchar *known[] = {
    "/sys/devices/platform/asus-nb-wmi",
    "/sys/devices/platform/asus-wmi",
  };
  static const gchar *probe_files[] = {
    "gpu_mux_mode", "dgpu_disable", "charge_mode",
    "throttle_thermal_policy", "panel_overdrive",
  };

  for (gsize i = 0; i < G_N_ELEMENTS (known); i++)
    {
      if (g_file_test (known[i], G_FILE_TEST_IS_DIR))
        return g_strdup (known[i]);
    }

  GDir *d = g_dir_open ("/sys/devices/platform", 0, NULL);
  if (!d)
    return NULL;

  gchar *ret = NULL;
  const gchar *n;
  while ((n = g_dir_read_name (d)) != NULL)
    {
      if (!g_str_has_prefix (n, "asus"))
        continue;
      gchar *dir = g_build_filename ("/sys/devices/platform", n, NULL);
      for (gsize i = 0; i < G_N_ELEMENTS (probe_files); i++)
        {
          gchar *p = g_build_filename (dir, probe_files[i], NULL);
          if (acc_path_exists (p))
            {
              ret = dir;
              g_free (p);
              break;
            }
          g_free (p);
        }
      if (ret)
        break;
      g_free (dir);
    }
  g_dir_close (d);
  return ret;
}

const gchar *
acc_dev_wmi_dir (void)
{
  if (!s_wmi_done)
    {
      s_wmi = find_wmi ();
      s_wmi_done = TRUE;
    }
  return s_wmi;
}

gchar *
acc_dev_wmi_file (const gchar *leaf)
{
  /* Kernel 6.17+ moved these attributes to asus-armoury. The old platform
   * files are still there on some models but are read-only (charge_mode is
   * r--r--r-- here) or a deprecated alias, so writes silently did nothing.
   * Always take the armoury copy when it exists. */
  gchar *arm = acc_dev_armoury_file (leaf);
  if (arm)
    return arm;

  const gchar *d = acc_dev_wmi_dir ();
  if (!d || !leaf)
    return NULL;
  gchar *p = g_build_filename (d, leaf, NULL);
  if (!acc_path_exists (p))
    {
      g_free (p);
      return NULL;
    }
  return p;
}

gchar *
acc_dev_kbd_led_file (void)
{
  if (s_kbd_done)
    return s_kbd ? g_strdup (s_kbd) : NULL;
  s_kbd_done = TRUE;

  GDir *d = g_dir_open ("/sys/class/leds", 0, NULL);
  if (d)
    {
      const gchar *n;
      while ((n = g_dir_read_name (d)) != NULL)
        {
          if (!g_strstr_len (n, -1, "kbd_backlight"))
            continue;
          gchar *p = g_build_filename ("/sys/class/leds", n, "brightness",
                                       NULL);
          if (acc_path_exists (p))
            {
              s_kbd = p;
              break;
            }
          g_free (p);
        }
      g_dir_close (d);
    }
  return s_kbd ? g_strdup (s_kbd) : NULL;
}
