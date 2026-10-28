/* user/user.c - local account persistence + salted password hashing
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "user.h"

#include <glib/gstdio.h>
#include <string.h>

static gchar   *s_name = NULL;
static gchar   *s_salt = NULL;
static gchar   *s_hash = NULL;
static gboolean s_remember = TRUE;
/* "stay signed in": after a successful sign-in the app skips the login
 * page on the next launch. Cleared by Sign out / Reset account. */
static gboolean s_signed_in = FALSE;

static gboolean save_user (void);

static gchar *
pref_path (void)
{
  return g_build_filename (g_get_user_config_dir (),
                           "asus-control-center", "settings.conf", NULL);
}

void
acc_user_load (void)
{
  g_free (s_name);
  g_free (s_salt);
  g_free (s_hash);
  s_name = s_salt = s_hash = NULL;
  s_remember = TRUE;
  s_signed_in = FALSE;

  gchar *p = pref_path ();
  GKeyFile *kf = g_key_file_new ();
  if (g_key_file_load_from_file (kf, p, G_KEY_FILE_NONE, NULL))
    {
      s_name = g_key_file_get_string (kf, "user", "name", NULL);
      s_salt = g_key_file_get_string (kf, "user", "salt", NULL);
      s_hash = g_key_file_get_string (kf, "user", "hash", NULL);
      s_remember = g_key_file_get_boolean (kf, "user", "remember", NULL);
      /* Make existing installs honour "don't ask again": a stored account
       * with no signed_in key (created before this key existed) counts as
       * signed in. An explicit Sign out writes signed_in=false, which is
       * then respected on every later launch. */
      GError *e = NULL;
      s_signed_in = g_key_file_get_boolean (kf, "user", "signed_in", &e);
      if (e)
        {
          s_signed_in = TRUE;
          g_clear_error (&e);
          if (s_name && *s_name && s_hash && *s_hash)
            save_user ();      /* write the migrated value once */
        }
    }
  g_key_file_free (kf);
  g_free (p);
}

/* write only the [user] keys; theme/display/ui groups survive untouched */
static gboolean
save_user (void)
{
  gchar *dir = g_build_filename (g_get_user_config_dir (),
                                 "asus-control-center", NULL);
  g_mkdir_with_parents (dir, 0700);
  gchar *p = g_build_filename (dir, "settings.conf", NULL);
  GKeyFile *kf = g_key_file_new ();
  g_key_file_load_from_file (kf, p, G_KEY_FILE_NONE, NULL);

  g_key_file_set_string (kf, "user", "name", s_name ? s_name : "");
  g_key_file_set_string (kf, "user", "salt", s_salt ? s_salt : "");
  g_key_file_set_string (kf, "user", "hash", s_hash ? s_hash : "");
  g_key_file_set_boolean (kf, "user", "remember", s_remember);
  g_key_file_set_boolean (kf, "user", "signed_in", s_signed_in);

  GError *e = NULL;
  gboolean ok = g_key_file_save_to_file (kf, p, &e);
  if (!ok)
    {
      g_printerr ("user pref not saved: %s\n", e ? e->message : "?");
      g_clear_error (&e);
    }
  else
    {
      /* the file holds a password hash: keep it private */
      g_chmod (p, 0600);
    }
  g_key_file_free (kf);
  g_free (p);
  g_free (dir);
  return ok;
}

gboolean
acc_user_exists (void)
{
  return s_name && *s_name && s_hash && *s_hash;
}

const gchar *
acc_user_name (void)
{
  return s_name ? s_name : "";
}

gboolean
acc_user_remember (void)
{
  return s_remember;
}

void
acc_user_set_remember (gboolean remember)
{
  s_remember = remember;
  if (acc_user_exists ())
    save_user ();
}

/* SHA-256("<salt>:<password>") as lowercase hex */
static gchar *
hash_password (const gchar *salt, const gchar *password)
{
  gchar *buf = g_strdup_printf ("%s:%s", salt, password);
  gchar *hex = g_compute_checksum_for_string (G_CHECKSUM_SHA256, buf, -1);
  /* do not leave the cleartext copy lying in the heap */
  memset (buf, 0, strlen (buf));
  g_free (buf);
  return hex;
}

static gchar *
new_salt (void)
{
  /* g_uuid_string_random() reads from the OS CSPRNG; strip the dashes so
   * the salt is a compact 32-char hex string */
  gchar *u = g_uuid_string_random ();
  gchar *out = (gchar *) g_malloc (strlen (u) + 1);
  gchar *d = out;
  for (const gchar *s = u; *s; s++)
    if (*s != '-')
      *d++ = *s;
  *d = '\0';
  g_free (u);
  return out;
}

gboolean
acc_user_create (const gchar *name, const gchar *password, gboolean remember)
{
  if (!name || !*name || !password || !*password)
    return FALSE;

  gchar *salt = new_salt ();
  gchar *hash = hash_password (salt, password);

  g_free (s_name);
  g_free (s_salt);
  g_free (s_hash);
  s_name = g_strdup (name);
  s_salt = salt;
  s_hash = hash;
  s_remember = remember;
  s_signed_in = TRUE;       /* creating the account signs you in */

  return save_user ();
}

gboolean
acc_user_check (const gchar *name, const gchar *password)
{
  if (!acc_user_exists () || !name || !password)
    return FALSE;
  if (g_strcmp0 (name, s_name) != 0)
    return FALSE;

  gchar *hash = hash_password (s_salt, password);
  gboolean ok = g_strcmp0 (hash, s_hash) == 0;
  g_free (hash);
  if (ok)
    {
      /* stay signed in until an explicit Sign out */
      s_signed_in = TRUE;
      save_user ();
    }
  return ok;
}

gboolean
acc_user_signed_in (void)
{
  return acc_user_exists () && s_signed_in;
}

void
acc_user_sign_out (void)
{
  s_signed_in = FALSE;
  if (acc_user_exists ())
    save_user ();
}

void
acc_user_forget (void)
{
  g_free (s_name);
  g_free (s_salt);
  g_free (s_hash);
  s_name = s_salt = s_hash = NULL;
  s_remember = TRUE;
  s_signed_in = FALSE;

  gchar *p = pref_path ();
  GKeyFile *kf = g_key_file_new ();
  if (g_key_file_load_from_file (kf, p, G_KEY_FILE_NONE, NULL))
    {
      g_key_file_remove_group (kf, "user", NULL);
      GError *e = NULL;
      if (!g_key_file_save_to_file (kf, p, &e))
        g_clear_error (&e);
    }
  g_key_file_free (kf);
  g_free (p);
}
