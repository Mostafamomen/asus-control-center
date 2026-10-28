/* probe/display.c - OLED panel capabilities from EDID (read-only)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The preferred detailed timing of the eDP panel gives exact 2880x1620@120
 * values without hardcoding: w = b56 | (b58 hi nibble <<8), refresh computed
 * from pixel clock / ((hactive+hblank)*(vactive+vblank)).
 */
#include "display.h"
#include "probe.h"

static gchar *
edid_path (void)
{
  GDir *d = g_dir_open ("/sys/class/drm", 0, NULL);
  if (!d)
    return NULL;
  gchar *ret = NULL;
  const gchar *n;
  while ((n = g_dir_read_name (d)) != NULL)
    {
      /* internal panel names vary: card0-eDP-1, eDP-1, card1-eDP-2 … */
      if (g_strstr_len (n, -1, "eDP") != NULL)
        {
          gchar *p = g_build_filename ("/sys/class/drm", n, "edid", NULL);
          if (acc_path_exists (p))
            ret = p;
          else
            g_free (p);
          if (ret)
            break;
        }
    }
  g_dir_close (d);
  return ret;
}

void
display_read (DispInfo *o)
{
  memset (o, 0, sizeof *o);
  o->ok = FALSE;
  o->refresh_hz = o->width = o->height = 0;
  o->backlight_max = o->backlight_now = -1;

  /* hardware backlight level (read-only) */
  {
    GDir *d = g_dir_open ("/sys/class/backlight", 0, NULL);
    if (d)
      {
        const gchar *n;
        while ((n = g_dir_read_name (d)) != NULL)
          {
            gchar *p = g_build_filename ("/sys/class/backlight", n,
                                         "max_brightness", NULL);
            if (acc_path_exists (p))
              {
                gchar *cp = g_build_filename ("/sys/class/backlight", n,
                                              "brightness", NULL);
                o->backlight_max = acc_read_int (p, -1);
                o->backlight_now = acc_read_int (cp, -1);
                g_free (cp);
                g_free (p);
                break;
              }
            g_free (p);
          }
        g_dir_close (d);
      }
  }

  gchar *p = edid_path ();
  if (!p)
    return;
  gchar *raw = NULL;
  gsize len = 0;
  if (!g_file_get_contents (p, &raw, &len, NULL) || len < 120)
    {
      g_free (raw);
      g_free (p);
      return;
    }
  g_free (p);

  const guchar *e = (const guchar *) raw;
  gint hact = e[56] | ((e[58] & 0xF0) << 4);
  gint hblank = e[57] | ((e[58] & 0x0F) << 8);
  gint vact = e[59] | ((e[61] & 0xF0) << 4);
  gint vblank = e[60] | ((e[61] & 0x0F) << 8);
  gint pixclk_10khz = e[54] | (e[55] << 8);

  if (hact > 0 && vact > 0 && pixclk_10khz > 0)
    {
      gdouble hhz = hact + hblank;
      gdouble vhz = vact + vblank;
      if (hhz > 0 && vhz > 0)
        {
          o->ok = TRUE;
          o->width = hact;
          o->height = vact;
          o->refresh_hz = (pixclk_10khz * 10000.0) / (hhz * vhz);
        }
    }
  g_free (raw);
}
