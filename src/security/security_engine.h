/* security/security_engine.h - read-only security checks, no GTK dependency
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Every check is argv-only (no shell), read-only, and returns NOT AVAILABLE
 * when its tool/data is missing. Nothing here modifies the system.
 */
#ifndef ACC_SECURITY_ENGINE_H
#define ACC_SECURITY_ENGINE_H
#include <glib.h>
G_BEGIN_DECLS
typedef enum { SEC_PASS, SEC_WARNING, SEC_CRITICAL, SEC_NOT_AVAILABLE,
               SEC_SKIPPED, SEC_ERROR, SEC_CANCELLED } SecStatus;
typedef struct {
  gchar *id, *name, *category, *detail, *evidence, *recommendation;
  SecStatus status; gint severity; /* 0 info 1 warn 2 crit */
  gboolean needs_admin, reversible;
} SecResult;
typedef struct { SecResult *items; guint len; gint pass,warn,crit,na,skip,err; } SecReport;
const gchar *sec_status_name (SecStatus s);
SecReport *sec_run_all (gint *cancelled_atomic); /* g_atomic_int_* storage; NULL = never cancel */
void sec_report_free (SecReport *r);
gchar *sec_report_to_json (const SecReport *r);
gchar *sec_report_to_html (const SecReport *r);
gchar *sec_report_to_text (const SecReport *r);
void sec_history_append (const SecReport *r);
GPtrArray *sec_history_entries (void); /* strings */
G_END_DECLS
#endif
