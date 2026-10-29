/* user/user.c - local account persistence + salted password hashing
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "user.h"

#include <glib/gstdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/random.h>
#include <unistd.h>

/* PBKDF2-HMAC-SHA256, OWASP 2023 work factor. Stored as
 *   hash = pbkdf2-sha256$<iterations>$<salt hex>$<key hex>
 * so the cost can be raised later without breaking old accounts. */
#define KDF_TAG     "pbkdf2-sha256"
#define KDF_ITER    600000
#define KDF_KEYLEN  32

/* wrong-password throttle: after LOCK_FREE misses each further miss doubles
 * the wait, from LOCK_BASE_S up to LOCK_MAX_S */
#define LOCK_FREE   5
#define LOCK_BASE_S 30
#define LOCK_MAX_S  (15 * 60)

static gchar   *s_name = NULL;
static gchar   *s_salt = NULL;   /* legacy accounts only */
static gchar   *s_hash = NULL;
static gboolean s_remember = TRUE;
/* "stay signed in": after a successful sign-in the app skips the login
 * page until Sign out, Reset account or the next reboot. */
static gboolean s_signed_in = FALSE;
static gchar   *s_boot = NULL;   /* boot id the sign-in belongs to */
static gint     s_failures = 0;
static gint64   s_locked_until = 0;  /* unix seconds */

static gboolean save_user (void);

static gchar *
pref_path (void)
{
  return g_build_filename (g_get_user_config_dir (),
                           "asus-control-center", "settings.conf", NULL);
}

/* changes on every boot, so a sign-in never outlives a restart */
static gchar *
current_boot_id (void)
{
  gchar *id = NULL;
  if (g_file_get_contents ("/proc/sys/kernel/random/boot_id", &id, NULL, NULL))
    g_strstrip (id);
  return id;
}

/* ------------------------------------------------------------- crypto */

/* compare without leaking the position of the first difference */
static gboolean
equal_ct (const gchar *a, const gchar *b)
{
  if (!a || !b)
    return FALSE;
  gsize la = strlen (a), lb = strlen (b);
  guint8 diff = la != lb;
  for (gsize i = 0; i < la && i < lb; i++)
    diff |= (guint8) (a[i] ^ b[i]);
  return diff == 0;
}

static gchar *
to_hex (const guint8 *buf, gsize n)
{
  gchar *out = (gchar *) g_malloc (n * 2 + 1);
  for (gsize i = 0; i < n; i++)
    g_snprintf (out + i * 2, 3, "%02x", buf[i]);
  return out;
}

/* RFC 8018 PBKDF2 with HMAC-SHA256; dklen <= 32 (one block) is all we need */
static void
pbkdf2_sha256 (const gchar *password, const guint8 *salt, gsize salt_len,
               guint iterations, guint8 out[KDF_KEYLEN])
{
  GHmac *base = g_hmac_new (G_CHECKSUM_SHA256, (const guchar *) password,
                            strlen (password));
  guint8 u[KDF_KEYLEN];
  gsize len = sizeof u;
  static const guint8 block1[4] = { 0, 0, 0, 1 };

  GHmac *h = g_hmac_copy (base);
  g_hmac_update (h, salt, salt_len);
  g_hmac_update (h, block1, sizeof block1);
  g_hmac_get_digest (h, u, &len);
  g_hmac_unref (h);
  memcpy (out, u, KDF_KEYLEN);

  for (guint i = 1; i < iterations; i++)
    {
      h = g_hmac_copy (base);
      g_hmac_update (h, u, sizeof u);
      len = sizeof u;
      g_hmac_get_digest (h, u, &len);
      g_hmac_unref (h);
      for (gsize k = 0; k < KDF_KEYLEN; k++)
        out[k] ^= u[k];
    }
  memset (u, 0, sizeof u);
  g_hmac_unref (base);
}

static gchar *
kdf_encode (const gchar *password, const gchar *salt_hex, guint iterations)
{
  guint8 key[KDF_KEYLEN];
  pbkdf2_sha256 (password, (const guint8 *) salt_hex, strlen (salt_hex),
                 iterations, key);
  gchar *hex = to_hex (key, sizeof key);
  memset (key, 0, sizeof key);
  gchar *enc = g_strdup_printf (KDF_TAG "$%u$%s$%s", iterations, salt_hex, hex);
  g_free (hex);
  return enc;
}

/* TRUE when password matches a stored pbkdf2 string */
static gboolean
kdf_verify (const gchar *password, const gchar *stored)
{
  gchar **f = g_strsplit (stored, "$", 0);
  gboolean ok = FALSE;
  if (g_strv_length (f) == 4 && g_strcmp0 (f[0], KDF_TAG) == 0)
    {
      guint64 it = g_ascii_strtoull (f[1], NULL, 10);
      if (it >= 1 && it <= 10000000 && *f[2])
        {
          gchar *enc = kdf_encode (password, f[2], (guint) it);
          ok = equal_ct (enc, stored);
          g_free (enc);
        }
    }
  g_strfreev (f);
  return ok;
}

/* pre-2026-10 accounts: SHA-256("<salt>:<password>") as lowercase hex */
static gboolean
legacy_verify (const gchar *password)
{
  if (!s_salt || !*s_salt)
    return FALSE;
  gchar *buf = g_strdup_printf ("%s:%s", s_salt, password);
  gchar *hex = g_compute_checksum_for_string (G_CHECKSUM_SHA256, buf, -1);
  memset (buf, 0, strlen (buf));
  g_free (buf);
  gboolean ok = equal_ct (hex, s_hash);
  g_free (hex);
  return ok;
}

static gchar *
new_salt (void)
{
  /* 128 random bits straight from the kernel CSPRNG */
  guint8 b[16];
  if (getrandom (b, sizeof b, 0) != (ssize_t) sizeof b)
    {
      /* never expected; g_uuid_string_random() also reads the kernel pool */
      gchar *u = g_uuid_string_random ();
      gchar *sum = g_compute_checksum_for_string (G_CHECKSUM_MD5, u, -1);
      g_free (u);
      return sum;
    }
  return to_hex (b, sizeof b);
}

/* ------------------------------------------------------------ storage */

void
acc_user_load (void)
{
  g_free (s_name);
  g_free (s_salt);
  g_free (s_hash);
  g_free (s_boot);
  s_name = s_salt = s_hash = s_boot = NULL;
  s_remember = TRUE;
  s_signed_in = FALSE;
  s_failures = 0;
  s_locked_until = 0;

  gchar *p = pref_path ();
  GKeyFile *kf = g_key_file_new ();
  if (g_key_file_load_from_file (kf, p, G_KEY_FILE_NONE, NULL))
    {
      GError *e = NULL;
      s_name = g_key_file_get_string (kf, "user", "name", NULL);
      s_salt = g_key_file_get_string (kf, "user", "salt", NULL);
      s_hash = g_key_file_get_string (kf, "user", "hash", NULL);
      s_remember = g_key_file_get_boolean (kf, "user", "remember", &e);
      if (e)
        {
          s_remember = TRUE;
          g_clear_error (&e);
        }
      s_boot = g_key_file_get_string (kf, "user", "signed_in_boot", NULL);
      s_failures = g_key_file_get_integer (kf, "user", "failures", NULL);
      s_locked_until = g_key_file_get_int64 (kf, "user", "locked_until", NULL);

      /* A missing or malformed key means signed OUT. A sign-in is only
       * honoured for the boot it happened in. */
      gboolean flag = g_key_file_get_boolean (kf, "user", "signed_in", NULL);
      gchar *boot = current_boot_id ();
      s_signed_in = flag && boot && s_boot && g_strcmp0 (boot, s_boot) == 0;
      g_free (boot);
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
  g_chmod (dir, 0700);
  gchar *p = g_build_filename (dir, "settings.conf", NULL);
  GKeyFile *kf = g_key_file_new ();
  g_key_file_load_from_file (kf, p, G_KEY_FILE_KEEP_COMMENTS, NULL);

  g_key_file_set_string (kf, "user", "name", s_name ? s_name : "");
  if (s_salt && *s_salt)
    g_key_file_set_string (kf, "user", "salt", s_salt);
  else
    g_key_file_remove_key (kf, "user", "salt", NULL);
  g_key_file_set_string (kf, "user", "hash", s_hash ? s_hash : "");
  g_key_file_set_boolean (kf, "user", "remember", s_remember);
  g_key_file_set_boolean (kf, "user", "signed_in", s_signed_in);
  g_key_file_set_string (kf, "user", "signed_in_boot",
                         s_signed_in && s_boot ? s_boot : "");
  g_key_file_set_integer (kf, "user", "failures", s_failures);
  g_key_file_set_int64 (kf, "user", "locked_until", s_locked_until);

  /* write to a private temp file, then rename: the hash is never on disk
   * with loose permissions, not even for a moment */
  gsize len = 0;
  gchar *data = g_key_file_to_data (kf, &len, NULL);
  gchar *tmp = g_strconcat (p, ".tmp", NULL);
  gboolean ok = FALSE;
  int fd = g_open (tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
  if (fd >= 0)
    {
      ok = write (fd, data, len) == (gssize) len;
      ok = (fsync (fd) == 0) && ok;
      g_close (fd, NULL);
      ok = ok && g_rename (tmp, p) == 0;
      if (!ok)
        g_unlink (tmp);
    }
  if (!ok)
    g_printerr ("user pref not saved: %s\n", g_strerror (errno));

  g_free (tmp);
  g_free (data);
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

static void
mark_signed_in (void)
{
  s_signed_in = TRUE;
  g_free (s_boot);
  s_boot = current_boot_id ();
}

gboolean
acc_user_create (const gchar *name, const gchar *password, gboolean remember)
{
  if (!name || !*name || !password || strlen (password) < ACC_USER_MIN_PASS)
    return FALSE;

  gchar *salt = new_salt ();
  gchar *hash = kdf_encode (password, salt, KDF_ITER);
  g_free (salt);

  g_free (s_name);
  g_free (s_salt);
  g_free (s_hash);
  s_name = g_strdup (name);
  s_salt = NULL;
  s_hash = hash;
  s_remember = remember;
  s_failures = 0;
  s_locked_until = 0;
  mark_signed_in ();        /* creating the account signs you in */

  return save_user ();
}

gint
acc_user_lock_remaining (void)
{
  gint64 now = g_get_real_time () / G_USEC_PER_SEC;
  /* a lock far in the future means the clock or the file was tampered
   * with: cap it instead of locking the user out forever */
  if (s_locked_until > now + LOCK_MAX_S)
    s_locked_until = now + LOCK_MAX_S;
  return s_locked_until > now ? (gint) (s_locked_until - now) : 0;
}

static void
note_failure (void)
{
  s_failures++;
  if (s_failures >= LOCK_FREE)
    {
      gint shift = MIN (s_failures - LOCK_FREE, 10);
      gint64 wait = MIN ((gint64) LOCK_BASE_S << shift, (gint64) LOCK_MAX_S);
      s_locked_until = g_get_real_time () / G_USEC_PER_SEC + wait;
    }
  save_user ();
}

gboolean
acc_user_check (const gchar *name, const gchar *password)
{
  if (!acc_user_exists () || !name || !password)
    return FALSE;
  if (acc_user_lock_remaining () > 0)
    return FALSE;

  /* always run the KDF, even for a wrong name, so timing says nothing
   * about which part was wrong */
  gboolean ok;
  if (g_str_has_prefix (s_hash, KDF_TAG "$"))
    ok = kdf_verify (password, s_hash);
  else
    {
      ok = legacy_verify (password);
      if (ok)
        {
          /* upgrade the old fast hash in place */
          gchar *salt = new_salt ();
          g_free (s_hash);
          s_hash = kdf_encode (password, salt, KDF_ITER);
          g_free (salt);
          g_free (s_salt);
          s_salt = NULL;
        }
    }
  ok = ok && g_strcmp0 (name, s_name) == 0;

  if (!ok)
    {
      note_failure ();
      return FALSE;
    }

  s_failures = 0;
  s_locked_until = 0;
  mark_signed_in ();        /* stay signed in until Sign out or reboot */
  save_user ();
  return TRUE;
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
  g_free (s_boot);
  s_name = s_salt = s_hash = s_boot = NULL;
  s_remember = TRUE;
  s_signed_in = FALSE;
  s_failures = 0;
  s_locked_until = 0;

  gchar *p = pref_path ();
  GKeyFile *kf = g_key_file_new ();
  if (g_key_file_load_from_file (kf, p, G_KEY_FILE_KEEP_COMMENTS, NULL))
    {
      g_key_file_remove_group (kf, "user", NULL);
      GError *e = NULL;
      if (!g_key_file_save_to_file (kf, p, &e))
        g_clear_error (&e);
    }
  g_key_file_free (kf);
  g_free (p);
}

gboolean
acc_user_selftest (void)
{
  /* RFC 7914 §11 / common PBKDF2-HMAC-SHA256 vector */
  static const gchar want[] =
    "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a";
  guint8 key[KDF_KEYLEN];
  pbkdf2_sha256 ("password", (const guint8 *) "salt", 4, 4096, key);
  gchar *hex = to_hex (key, sizeof key);
  gboolean ok = g_strcmp0 (hex, want) == 0;
  g_free (hex);
  return ok;
}
