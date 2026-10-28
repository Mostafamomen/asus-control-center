/* probe/display.h - OLED panel capabilities from EDID (read-only)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_DISPLAY_H
#define ACC_DISPLAY_H
#include <glib.h>

G_BEGIN_DECLS

typedef struct
{
  gboolean ok;
  gint width;           /* px, from EDID detailed timing */
  gint height;
  gdouble refresh_hz;   /* computed from pixel clock + blanking */
  gint backlight_max;   /* hw backlight max, -1 unknown */
  gint backlight_now;
} DispInfo;

void display_read (DispInfo *out);

G_END_DECLS
#endif
