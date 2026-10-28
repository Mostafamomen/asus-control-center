/* probe/gpu.cpp - GPU telemetry + vendor (read-only)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "gpu.h"
#include "probe.h"

#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SMI_PATH "/usr/bin/nvidia-smi"  /* absolute: no PATH hijack */
#define SMI_CACHE_US (2 * G_USEC_PER_SEC)

/* ------------------------------------------------------------ vendor ---- */
static AccGpuVendor s_vendor = ACC_GPU_UNKNOWN;
static gboolean s_vendor_done = FALSE;

static gchar *
pci_attr (const gchar *dev, const gchar *leaf)
{
  gchar *p = g_build_filename (dev, leaf, NULL);
  gchar *v = acc_read_trim (p);
  g_free (p);
  return v;
}

AccGpuVendor
gpu_vendor (void)
{
  if (s_vendor_done)
    return s_vendor;
  s_vendor_done = TRUE;

  GDir *d = g_dir_open ("/sys/bus/pci/devices", 0, NULL);
  if (!d)
    return s_vendor;

  gboolean nv = FALSE, amd = FALSE, intel = FALSE;
  const gchar *n;
  while ((n = g_dir_read_name (d)) != NULL)
    {
      gchar *dev = g_build_filename ("/sys/bus/pci/devices", n, NULL);
      gchar *cls = pci_attr (dev, "class");
      gchar *ven = pci_attr (dev, "vendor");
      g_free (dev);

      /* display controllers: 0x0300xx (VGA), 0x0302xx (3D), 0x0380xx */
      if (cls && g_str_has_prefix (cls, "0x03"))
        {
          if (g_strcmp0 (ven, "0x10de") == 0)
            nv = TRUE;
          else if (g_strcmp0 (ven, "0x1002") == 0)
            amd = TRUE;
          else if (g_strcmp0 (ven, "0x8086") == 0)
            intel = TRUE;
        }
      g_free (cls);
      g_free (ven);
    }
  g_dir_close (d);

  /* hybrid laptops: the dGPU is what the GPU card is about */
  s_vendor = nv ? ACC_GPU_NVIDIA
           : amd ? ACC_GPU_AMD
           : intel ? ACC_GPU_INTEL
           : ACC_GPU_UNKNOWN;
  return s_vendor;
}

const gchar *
gpu_vendor_label (void)
{
  switch (gpu_vendor ())
    {
    case ACC_GPU_NVIDIA: return "NVIDIA";
    case ACC_GPU_AMD:    return "AMD";
    case ACC_GPU_INTEL:  return "Intel";
    default:             return "";
    }
}

const gchar *
gpu_vendor_asset (void)
{
  switch (gpu_vendor ())
    {
    case ACC_GPU_NVIDIA: return "nvidia.png";
    case ACC_GPU_AMD:    return "amd.png";
    case ACC_GPU_INTEL:  return "intel.png";
    default:             return NULL;
    }
}

/* --------------------------------------------------------- nvidia-smi --- */

/* nvidia-smi prints "[N/A]" (or "N/A") for values it cannot read, e.g.
 * power draw while the dGPU is runtime-suspended. Treat that as unknown. */
static gboolean
smi_missing (const gchar *s)
{
  return !s || !*s || strcmp (s, "N/A") == 0 || strstr (s, "[N/A]") != NULL;
}

static gint
smi_int (const gchar *s, gint fallback)
{
  return smi_missing (s) ? fallback : (gint) g_ascii_strtoll (s, NULL, 10);
}

static gdouble
smi_double (const gchar *s, gdouble fallback)
{
  return smi_missing (s) ? fallback : g_ascii_strtod (s, NULL);
}

/* Runs the tool with a fixed argv (never a shell string) and returns its
 * first output line. NULL when the tool is absent, fails, or says nothing. */
static gchar *
smi_line (void)
{
  const gchar *argv[] = {
    SMI_PATH,
    "--query-gpu=utilization.gpu,temperature.gpu,memory.used,memory.total,"
    "power.draw,display_active,driver_version,name",
    "--format=csv,noheader,nounits",
    NULL
  };

  GError *e = NULL;
  GSubprocess *p = g_subprocess_newv
    (argv,
     (GSubprocessFlags) (G_SUBPROCESS_FLAGS_STDOUT_PIPE
                         | G_SUBPROCESS_FLAGS_STDERR_SILENCE),
     &e);
  if (!p)
    {
      g_clear_error (&e);
      return NULL;
    }

  gchar *out = NULL;
  gboolean ok = g_subprocess_communicate_utf8 (p, NULL, NULL,
                                               &out, NULL, &e);
  g_object_unref (p);
  if (!ok)
    {
      g_clear_error (&e);
      g_free (out);
      return NULL;
    }
  return out;
}

static void
read_nvidia (GpuInfo *o)
{
  gchar *raw = smi_line ();
  if (!raw)
    return;

  gchar *end = raw + strlen (raw);
  while (end > raw && (end[-1] == '\n' || end[-1] == '\r'))
    *--end = 0;
  if (!*raw)
    {
      g_free (raw);
      return;
    }

  /* name is the last column and is the only free-text one, so numeric
   * fields are fixed from the left and the name keeps any commas */
  gchar **f = g_strsplit (raw, ", ", -1);
  gsize n = g_strv_length (f);
  if (n < 8)
    {
      g_strfreev (f);
      g_free (raw);
      return;
    }

  o->util_pct   = smi_int    (f[0], -1);
  o->temp_c     = smi_double (f[1], -1);
  gint used_mib = smi_int    (f[2], -1);
  gint tot_mib  = smi_int    (f[3], -1);
  o->power_w    = smi_double (f[4], -1);

  if (smi_missing (f[5]))
    o->display_active = -1;
  else
    o->display_active = g_ascii_strcasecmp (f[5], "Enabled") == 0 ? 1
                      : g_ascii_strcasecmp (f[5], "Disabled") == 0 ? 0
                      : -1;

  if (!smi_missing (f[6]))
    g_strlcpy (o->driver, f[6], sizeof o->driver);

  GString *name = g_string_new (f[7]);
  for (gsize i = 8; i < n; i++)
    g_string_append_printf (name, ", %s", f[i]);
  gchar *nm_buf = g_string_free (name, FALSE);
  gchar *nm = nm_buf;
  g_strstrip (nm);
  if (*nm == '"')
    {
      nm++;
      for (gchar *c = nm; *c; c++)
        if (*c == '"')
          {
            *c = 0;
            break;
          }
    }

  if (tot_mib > 0)
    {
      o->ok = TRUE;
      g_strlcpy (o->short_name, nm, sizeof o->short_name);
      o->mem_total_gb = tot_mib / 1024.0;
      o->mem_used_gb = used_mib >= 0 ? used_mib / 1024.0 : -1;
    }

  g_free (nm_buf);
  g_strfreev (f);
  g_free (raw);
}

/* ---------------------------------------------------------------- AMD --- */
/* find <card>/device for the first AMD display controller, or NULL */
static gchar *
amd_device_dir (void)
{
  GDir *d = g_dir_open ("/sys/class/drm", 0, NULL);
  if (!d)
    return NULL;

  gchar *ret = NULL;
  const gchar *n;
  while ((n = g_dir_read_name (d)) != NULL)
    {
      if (!g_str_has_prefix (n, "card") || strchr (n, '-'))
        continue;
      gchar *dev = g_build_filename ("/sys/class/drm", n, "device", NULL);
      gchar *ven = pci_attr (dev, "vendor");
      if (g_strcmp0 (ven, "0x1002") == 0)
        ret = dev;
      g_free (ven);
      if (ret)
        break;
      g_free (dev);
    }
  g_dir_close (d);
  return ret;
}

static gdouble
read_temp_mc (const gchar *dev)
{
  GDir *d = g_dir_open (dev, 0, NULL);
  gdouble t = -1;
  if (!d)
    return t;
  const gchar *n;
  while ((n = g_dir_read_name (d)) != NULL)
    {
      if (!g_str_has_prefix (n, "hwmon"))
        continue;
      gchar *p = g_build_filename (dev, n, "temp1_input", NULL);
      int mc = acc_read_int (p, -1);
      g_free (p);
      if (mc > 0)
        {
          t = mc / 1000.0;
          break;
        }
    }
  g_dir_close (d);
  return t;
}

static void
read_amd (GpuInfo *o)
{
  gchar *dev = amd_device_dir ();
  if (!dev)
    return;

  gchar *p = g_build_filename (dev, "gpu_busy_percent", NULL);
  o->util_pct = acc_read_int (p, -1);
  g_free (p);

  gint64 used = 0, total = 0;
  p = g_build_filename (dev, "mem_info_vram_used", NULL);
  {
    gchar *v = acc_read_trim (p);
    if (v)
      used = g_ascii_strtoll (v, NULL, 10);
    g_free (v);
  }
  g_free (p);
  p = g_build_filename (dev, "mem_info_vram_total", NULL);
  {
    gchar *v = acc_read_trim (p);
    if (v)
      total = g_ascii_strtoll (v, NULL, 10);
    g_free (v);
  }
  g_free (p);

  if (total > 0)
    {
      o->mem_used_gb = used / 1073741824.0;
      o->mem_total_gb = total / 1073741824.0;
    }
  o->temp_c = read_temp_mc (dev);

  if (o->util_pct >= 0 || o->mem_total_gb > 0 || o->temp_c > 0)
    {
      o->ok = TRUE;
      g_strlcpy (o->short_name, "AMD Radeon Graphics",
                 sizeof o->short_name);
    }
  g_free (dev);
}

/* --------------------------------------------------------------- main --- */
static void
gpu_read_uncached (GpuInfo *o)
{
  memset (o, 0, sizeof *o);
  o->ok = FALSE;
  o->util_pct = -1;
  o->temp_c = -1;
  o->mem_used_gb = -1;
  o->mem_total_gb = -1;
  o->power_w = -1;
  o->display_active = -1;

  switch (gpu_vendor ())
    {
    case ACC_GPU_NVIDIA:
      read_nvidia (o);
      if (!o->ok)
        {
          /* driver loaded but tool missing/busy: still name the hardware */
          g_strlcpy (o->short_name, "NVIDIA GeForce",
                     sizeof o->short_name);
        }
      break;
    case ACC_GPU_AMD:
      read_amd (o);
      break;
    case ACC_GPU_INTEL:
      o->ok = TRUE;
      g_strlcpy (o->short_name, "Intel Graphics", sizeof o->short_name);
      break;
    default:
      break;
    }
}

void
gpu_read (GpuInfo *o)
{
  static GpuInfo cache;
  static gint64  stamp = 0;

  if (!o)
    return;

  gint64 now = g_get_monotonic_time ();
  if (stamp != 0 && now - stamp < SMI_CACHE_US)
    {
      *o = cache;
      return;
    }

  gpu_read_uncached (&cache);
  stamp = now;
  *o = cache;
}
