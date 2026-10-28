/* probe/sysinfo.c - OS / RAM / storage / CPU strings (read-only)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "sysinfo.h"
#include "probe.h"
#include <stdio.h>

gchar *
sysinfo_os (void)
{
  /* /etc/os-release has no INI group, so GKeyFile can't read it whole;
   * grep the PRETTY_NAME line instead (read-only, no spawns). */
  gchar *c = NULL;
  if (!g_file_get_contents ("/etc/os-release", &c, NULL, NULL))
    return g_strdup ("—");
  gchar *ret = g_strdup ("—");
  gchar **lines = g_strsplit (c, "\n", -1);
  for (gint i = 0; lines && lines[i]; i++)
    if (g_str_has_prefix (lines[i], "PRETTY_NAME="))
      {
        g_free (ret);
        ret = g_strdup (lines[i] + strlen ("PRETTY_NAME="));
        g_strstrip (ret);
        break;
      }
  g_strfreev (lines);
  g_free (c);
  return ret;
}

gdouble
sysinfo_ram_gb (void)
{
  FILE *f = fopen ("/proc/meminfo", "r");
  if (!f)
    return 0;
  long kb = 0;
  char line[128];
  while (fgets (line, sizeof line, f))
    if (g_str_has_prefix (line, "MemTotal"))
      {
        sscanf (line, "MemTotal: %ld kB", &kb);
        break;
      }
  fclose (f);
  return kb / 1048576.0;
}

gchar *
sysinfo_nvme_model (void)
{
  GDir *d = g_dir_open ("/sys/class/block", 0, NULL);
  if (!d)
    return g_strdup ("—");
  const gchar *n;
  gchar *ret = NULL;
  while ((n = g_dir_read_name (d)) != NULL)
    {
      /* whole nvme0n1 devices only: no 'p' partitions, no nvme0n1 suffix hit */
      if (!g_str_has_prefix (n, "nvme") || strchr (n + 4, 'p') != NULL)
        continue;
      gchar *p = g_build_filename ("/sys/class/block", n, "device", "model",
                                   NULL);
      gchar *v = acc_read_trim (p);
      g_free (p);
      if (v && *v)
        {
          g_strstrip (v);
          ret = v;
          break;
        }
      g_free (v);
    }
  g_dir_close (d);
  return ret ? ret : g_strdup ("—");
}

gchar *
sysinfo_cpu (void)
{
  FILE *f = fopen ("/proc/cpuinfo", "r");
  if (!f)
    return g_strdup ("—");
  char line[384];
  gchar *ret = NULL;
  while (fgets (line, sizeof line, f))
    if (g_str_has_prefix (line, "model name"))
      {
        const gchar *c = strchr (line, ':');
        ret = g_strdup (c ? c + 2 : "—");
        g_strstrip (ret);
        break;
      }
  fclose (f);
  return ret ? ret : g_strdup ("—");
}
