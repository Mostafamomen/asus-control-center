/* probe/load.c - honest system load metrics (read-only)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "load.h"
#include "probe.h"

#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>

/* previous /proc/stat snapshot for the utilisation delta */
static gulong s_prev_busy = 0, s_prev_total = 0;
static gboolean s_have_prev = FALSE;

static void
read_proc_stat (gulong *busy, gulong *total)
{
  FILE *f = fopen ("/proc/stat", "r");
  if (!f)
    {
      *busy = *total = 0;
      return;
    }
  char cpu[8];
  gulong u, n, s, idle_res;
  if (fscanf (f, "%7s %lu %lu %lu %lu", cpu, &u, &n, &s, &idle_res) != 5)
    {
      *busy = *total = 0;
      fclose (f);
      return;
    }
  fclose (f);
  gulong idle = s + idle_res;
  gulong nonidle = u + n + s;
  *busy = nonidle;
  *total = nonidle + idle;
}

static gdouble
cpu_util_pct (void)
{
  gulong busy, total;
  read_proc_stat (&busy, &total);
  if (!total)
    return -1;
  if (!s_have_prev)
    {
      s_prev_busy = busy;
      s_prev_total = total;
      s_have_prev = TRUE;
      return -1;         /* first call: no baseline yet — honest "—" */
    }
  gulong db = busy - s_prev_busy;
  gulong dt = total - s_prev_total;
  s_prev_busy = busy;
  s_prev_total = total;
  if (!dt)
    return -1;
  gdouble pct = 100.0 * (gdouble) db / (gdouble) dt;
  if (pct < 0)
    pct = 0;
  if (pct > 100)
    pct = 100;
  return pct;
}

static void
mem_read (gdouble *pct, gdouble *total_gb)
{
  FILE *f = fopen ("/proc/meminfo", "r");
  if (!f)
    {
      *pct = *total_gb = -1;
      return;
    }
  char key[24];
  gulong v;
  gulong total = 0, avail = 0;
  gboolean got_total = FALSE, got_avail = FALSE;
  while (fscanf (f, "%23s %lu kB", key, &v) == 2)
    {
      if (!got_total && strcmp (key, "MemTotal:") == 0)
        {
          total = v;
          got_total = TRUE;
        }
      else if (!got_avail && strcmp (key, "MemAvailable:") == 0)
        {
          avail = v;
          got_avail = TRUE;
        }
      if (got_total && got_avail)
        break;
    }
  fclose (f);
  if (!got_total)
    {
      *pct = *total_gb = -1;
      return;
    }
  *total_gb = (gdouble) total / (gdouble) (1 << 20);   /* kB → GiB */
  *pct = got_avail && avail <= total
         ? 100.0 * (gdouble) (total - avail) / (gdouble) total
         : -1;
}

void
acc_load_read (LoadInfo *out)
{
  memset (out, 0, sizeof *out);
  out->cpu_pct = acc_path_exists ("/proc/stat") ? cpu_util_pct () : -1;

  gdouble mem_pct, mem_total;
  mem_read (&mem_pct, &mem_total);
  out->mem_pct = mem_pct;
  out->mem_total_gb = mem_total;

  struct statvfs v;
  if (statvfs ("/", &v) == 0 && v.f_blocks > 0 && v.f_frsize > 0)
    {
      gdouble one_gib = (gdouble) (1 << 30);
      gdouble tot = (gdouble) v.f_blocks * (gdouble) v.f_frsize;
      gdouble freeb = (gdouble) v.f_bavail * (gdouble) v.f_frsize;
      out->disk_total_gb = tot / one_gib;
      out->disk_used_gb = (tot - freeb) / one_gib;
      out->disk_pct = tot > 0 ? 100.0 * (tot - freeb) / tot : -1;
    }
  else
    {
      out->disk_used_gb = out->disk_total_gb = out->disk_pct = -1;
    }
}
