/* ui/spark.h - minimal temperature trend line widget
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_SPARK_H
#define ACC_SPARK_H
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define ACC_TYPE_SPARK acc_spark_get_type ()
G_DECLARE_FINAL_TYPE (AccSpark, acc_spark, ACC, SPARK, GtkDrawingArea)

GtkWidget *acc_spark_new (const gchar *line_color);
void       acc_spark_push (AccSpark *s, gdouble v);

G_END_DECLS
#endif
