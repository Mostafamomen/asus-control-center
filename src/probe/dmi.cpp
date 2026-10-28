/* probe/dmi.c - model / vendor / BIOS (read-only)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "dmi.h"
#include "probe.h"

static gchar *
rd (const gchar *leaf)
{
  gchar *p = g_build_filename ("/sys/class/dmi/id", leaf, NULL);
  gchar *v = acc_read_trim (p);
  g_free (p);
  return v;
}

gchar *
dmi_product_full (void)
{
  gchar *v = rd ("product_name");
  return v ? v : g_strdup ("—");
}

gchar *
dmi_product_name (void)
{
  gchar *s = dmi_product_full ();
  gchar *cut = strrchr (s, '_');
  if (cut && g_ascii_isalpha (cut[1]))
    *cut = '\0';
  return s;
}

gchar *
dmi_vendor (void)
{
  gchar *v = rd ("sys_vendor");
  return v ? v : g_strdup ("—");
}

gchar *
dmi_bios_version (void)
{
  gchar *a = rd ("bios_version");
  gchar *b = rd ("bios_date");
  gchar *out = g_strdup_printf ("%s (%s)", a ? a : "—", b ? b : "—");
  g_free (a);
  g_free (b);
  return out;
}
