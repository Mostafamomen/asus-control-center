/* control/muxreq.h - remember a GPU MUX request and check it after reboot
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A MUX write only takes effect on the next boot, so "the switch worked"
 * can only be said after a reboot. When a request is accepted (by the
 * firmware directly, or queued in asusd) the app records the mode and the
 * boot it was made in. On a later boot it compares the live firmware value
 * with the request: equal means verified, different means the switch did
 * not happen, and the previous boot's journal is searched for the reason.
 *
 * File: $XDG_CONFIG_HOME/asus-control-center/gpu-request.ini (user-owned).
 */
#ifndef ACC_MUXREQ_H
#define ACC_MUXREQ_H
#include <glib.h>

G_BEGIN_DECLS

typedef enum
{
  ACC_MUXREQ_NONE = 0,      /* nothing recorded                          */
  ACC_MUXREQ_PENDING,       /* recorded this boot, needs a restart        */
  ACC_MUXREQ_VERIFIED,      /* a later boot runs the requested mode       */
  ACC_MUXREQ_FAILED         /* a later boot still runs the old mode       */
} AccMuxReqState;

typedef struct
{
  AccMuxReqState state;
  gint     mode;            /* requested MUX value, 0 or 1                */
  gboolean queued;          /* went through the asusd shutdown queue      */
  gchar    boot_id[40];     /* boot the request was made in               */
  gchar   *reason;          /* FAILED only: journal line, or NULL         */
} AccMuxReq;

/* Save a request made in this boot. */
void     acc_muxreq_record (gint mode, gboolean queued);

/* Forget the request (after a verified/failed result was shown, or after
 * the user cancelled it). */
void     acc_muxreq_clear (void);

/* Read the record and classify it against the live MUX value. *out->reason
 * must be freed with g_free. Reads the journal only for FAILED, once per
 * process. */
void     acc_muxreq_check (gint live_mode, AccMuxReq *out);

/* ---- pure helpers (unit-tested by --check) ---------------------------- */

AccMuxReqState acc_muxreq_classify (gint req_mode, const gchar *req_boot,
                                    const gchar *this_boot, gint live_mode);

/* The most telling MUX failure line in a journal excerpt, or NULL. */
gchar   *acc_muxreq_reason_from_log (const gchar *log);

G_END_DECLS
#endif
