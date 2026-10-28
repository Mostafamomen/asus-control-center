/* ui/ring.h - cairo doughnut progress widget
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_RING_H
#define ACC_RING_H
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define ACC_TYPE_RING acc_ring_get_type ()
G_DECLARE_FINAL_TYPE (AccRing, acc_ring, ACC, RING, GtkDrawingArea)

GtkWidget *acc_ring_new (const gchar *arc_color);
void       acc_ring_set_value (AccRing *r, gdouble v);   /* 0..1 */
void       acc_ring_set_big (AccRing *r, const gchar *text);
void       acc_ring_set_small (AccRing *r, const gchar *text);
gdouble    acc_ring_get_value (AccRing *r);
gdouble    acc_ring_get_max (const AccRing *r);           /* preview helper */

G_END_DECLS
#endif
