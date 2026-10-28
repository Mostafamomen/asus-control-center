/* ui/window.h - main window state + lifecycle
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_WINDOW_H
#define ACC_WINDOW_H
#include <gtk/gtk.h>
#include "ring.h"
#include "spark.h"

G_BEGIN_DECLS

typedef struct
{
  /* hero */
  GtkWidget *hero_bat;    /* "Battery: 71% · Charging" chip */
  gint64     oled_touch;  /* ms of last user brightness drag (0=none) */
  GtkWidget *bat_small;   /* status caption under the battery ring    */
  GtkWidget *fan_small;   /* caption under the temperature ring       */

  /* MyASUS-style stat tiles (top band of the dashboard) */
  GtkWidget *tile_bat;      /* battery %                               */
  GtkWidget *tile_cpu;      /* CPU load %                              */
  GtkWidget *tile_cputemp;  /* CPU °C shown next to the load           */
  GtkWidget *tile_mem;      /* memory used %                           */
  GtkWidget *tile_disk;     /* disk used %                             */
  GtkWidget *tile_disksub;  /* "used / total" subtitle                 */
  GtkWidget *lap_bat;       /* battery % drawn on the laptop mock      */

  /* battery */
  AccRing *ring_bat;
  GtkWidget *bat_wh_now;
  GtkWidget *bat_wh_full;
  GtkWidget *bat_cycles;
  GtkWidget *bat_limit_scale;
  GtkWidget *bat_limit_val;
  GtkWidget *bat_switch;

  /* gpu */
  GtkWidget *gpu_name;
  GtkWidget *gpu_vram;
  GtkWidget *gpu_pill;
  GtkWidget *gpu_bar;
  GtkWidget *gpu_util;
  GtkWidget *gpu_temp;

  /* oled */
  GtkWidget *oled_mode;   /* refresh-rate value label            */
  GtkWidget *oled_scale;  /* brightness slider (live)            */
  GtkWidget *oled_pct;

  /* system */
  GtkWidget *sys_os;
  GtkWidget *sys_ram;
  GtkWidget *sys_ssd;
  GtkWidget *sys_temp;

  /* fans */
  AccRing *ring_fan;
  GtkWidget *fan_cpu;
  GtkWidget *fan_gpu;
  AccSpark *spark;

  gboolean syncing;
} AccUI;

/* module-private casts (window.c / home.c only) */
#define ACC_UI_CAST(x) ((AccUI *) (x))

void        acc_ui_boot (GtkApplication *app);   /* creates window */
void        acc_ui_apply_theme (void);           /* load skin CSS now  */
void        acc_ui_refresh (void);               /* read probes -> widgets */
void        acc_ui_destroy (void);               /* close window (lang swap) */
void        acc_ui_toast (const gchar *msg);     /* transient notice */
void        acc_ui_nav_to (const gchar *page);   /* change stack page */
GtkWidget  *acc_ui_root (void);
const gchar *acc_status_tr (const gchar *hw_status); /* Charging->شحن … */
void        acc_ui_lang_toggle (void);               /* EN ع switch   */
void        acc_ui_theme_toggle (void);              /* light/dark swap */

G_END_DECLS
#endif
