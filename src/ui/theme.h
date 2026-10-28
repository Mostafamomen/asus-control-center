/* ui/theme.h - light/dark MyASUS-style skin switch (persisted per user)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ACC_UI_THEME_H
#define ACC_UI_THEME_H
#include <glib.h>

G_BEGIN_DECLS

/* current mode: FALSE = light MyASUS skin, TRUE = dark skin */
gboolean    acc_theme_is_dark (void);

/* read the persisted preference into the cached state (called once) */
void        acc_theme_load_pref (void);

/* persist the current mode to $XDG_CONFIG_HOME/asus-control-center */
void        acc_theme_save_pref (gboolean dark);

/* "E-Reading" grayscale mode (offered in Display settings), persisted */
gboolean    acc_theme_gray_pref (void);
void        acc_theme_save_gray (gboolean gray);

/* absolute path of the stylesheet matching the current mode */
gchar      *acc_theme_css_path (void);

/* absolute path of a bundled asset (e.g. "laptop.png"), or NULL */
gchar      *acc_asset_path (const gchar *name);

/* graph/cairo colors for the active mode (do not free) */
const gchar *acc_theme_col_accent (void);   /* battery ring, spark       */
const gchar *acc_theme_col_warn   (void);   /* temperature ring          */

G_END_DECLS
#endif
