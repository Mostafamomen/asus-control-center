/* control/commands.cpp - the fixed command table (see commands.h)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "commands.h"

#include <string.h>
#include <sys/wait.h>

/* ------------------------------------------------------------------ table */

typedef struct {
  const gchar *id;
  const gchar *const *progs;   /* candidate programs, first match wins */
  const gchar *const *tpl;      /* fixed arguments; "@ARG" marks the slot */
  gint timeout_secs;            /* 0 = unbounded */
  const gchar *arg_kind;        /* NULL = the command takes no argument */
} AccCmdDef;

static const gchar *const p_iw[]        = { "iw", NULL };
static const gchar *const p_systemctl[] = { "systemctl", NULL };
static const gchar *const p_dnf[]       = { "dnf", NULL };
static const gchar *const p_apt[]      = { "apt-get", NULL };
static const gchar *const p_rpm[]      = { "rpm", NULL };
static const gchar *const p_lspci[]    = { "lspci", NULL };
static const gchar *const p_fwupd[]     = { "fwupdmgr", NULL };
static const gchar *const p_flatpak[]   = { "flatpak", NULL };
static const gchar *const p_bt[]        = { "bluetoothctl", NULL };
static const gchar *const p_smartctl[]  = { "smartctl", NULL };
static const gchar *const p_nvidia[]    = { "nvidia-smi", NULL };
static const gchar *const p_journal[]   = { "journalctl", NULL };
static const gchar *const p_gnext[]     = { "gnome-extensions", NULL };
static const gchar *const p_kread[]     = { "kreadconfig6", "kreadconfig5", NULL };
static const gchar *const p_kwrite[]    = { "kwriteconfig6", "kwriteconfig5", NULL };
static const gchar *const p_qdbus[]     = { "qdbus6", "qdbus", "qdbus-qt6", "qdbus-qt5", NULL };

static const gchar *const t_wifi[]    = { "dev", "@ARG", "link", NULL };
static const gchar *const t_service[] = { "is-active", "@ARG", NULL };
static const gchar *const t_service_en[] = { "is-enabled", "@ARG", NULL };
static const gchar *const t_dnf[]     = { "check-update", "-q", "--cacheonly", NULL };
static const gchar *const t_apt[]    = { "-s", "upgrade", NULL };
static const gchar *const t_rpm[]    = { "-qa", "--qf", "%{NAME} %{VERSION}-%{RELEASE}", NULL };
static const gchar *const t_lspci[]  = { "-nn", NULL };
static const gchar *const t_fwupd[]   = { "get-updates", "--no-unreported-check",
                                          "--no-metadata-check", "--json", NULL };
static const gchar *const t_flatpak[] = { "remote-ls", "--updates",
                                          "--columns=application", NULL };
static const gchar *const t_flatpak_list[] = { "list", "--app",
                                          "--columns=application,name,version,size", NULL };
static const gchar *const t_flatpak_rm[] = { "uninstall", "--user", "-y", "@ARG", NULL };
static const gchar *const t_bt[]      = { "list", NULL };
static const gchar *const t_smart[]   = { "-H", "@ARG", NULL };
static const gchar *const t_nvidia[]  = {
  "--query-gpu=utilization.gpu,temperature.gpu,memory.used,memory.total,"
  "power.draw,display_active,driver_version,name",
  "--format=csv,noheader,nounits", NULL
};
static const gchar *const t_journal[] = { "--no-pager", "-q", "-o", "cat", "-b", "@ARG",
                                          "-g", "gpu_mux_mode|GPU settings|current_value",
                                          NULL };
static const gchar *const t_gnext_on_list[]  = { "list", "--enabled", NULL };
static const gchar *const t_gnext_off_list[] = { "list", "--disabled", NULL };
static const gchar *const t_gnext_on[]  = { "enable", "@ARG", NULL };
static const gchar *const t_gnext_off[] = { "disable", "@ARG", NULL };
static const gchar *const t_kread[]   = { "--file", "kwinrc", "--group", "NightColor",
                                          "--key", "Active", NULL };
static const gchar *const t_kwrite[]  = { "--file", "kwinrc", "--group", "NightColor",
                                          "--key", "Active", "@ARG", NULL };
static const gchar *const t_qdbus[]   = { "org.kde.KWin", "/ColorCorrect", "setEnabled",
                                          "@ARG", NULL };

static const AccCmdDef k_cmds[] = {
  { "wifi-ssid",             p_iw,        t_wifi,         0, "iface"   },
  { "service-active",        p_systemctl, t_service,      0, "service" },
  { "service-enabled",       p_systemctl, t_service_en,   0, "service" },
  { "package-updates",       p_dnf,       t_dnf,         30, NULL       },
  { "apt-updates",           p_apt,       t_apt,         30, NULL       },
  { "rpm-list",              p_rpm,       t_rpm,          0, NULL       },
  { "lspci",                 p_lspci,     t_lspci,        0, NULL       },
  { "firmware-updates",      p_fwupd,     t_fwupd,       30, NULL       },
  { "flatpak-updates",       p_flatpak,   t_flatpak,     30, NULL       },
  { "flatpak-list",          p_flatpak,   t_flatpak_list, 30, NULL       },
  { "flatpak-uninstall",     p_flatpak,   t_flatpak_rm,  30, "appid"   },
  { "loaded-modules",        NULL,        NULL,           0, NULL       },
  { "bluetooth-adapters",    p_bt,        t_bt,           0, NULL       },
  { "smart-health",          p_smartctl,  t_smart,        0, "device"  },
  { "nvidia-smi",            p_nvidia,    t_nvidia,       0, NULL       },
  { "journal-mux",           p_journal,   t_journal,      0, "bootid"  },
  { "gnome-extensions-enabled",  p_gnext, t_gnext_on_list,  0, NULL       },
  { "gnome-extensions-disabled", p_gnext, t_gnext_off_list, 0, NULL       },
  { "gnome-extension-enable",  p_gnext,   t_gnext_on,     0, "uuid"    },
  { "gnome-extension-disable", p_gnext,   t_gnext_off,    0, "uuid"    },
  { "nightlight-kde-get",    p_kread,     t_kread,        0, NULL       },
  { "nightlight-kde-set",    p_kwrite,    t_kwrite,       0, "bool"    },
  { "nightlight-kde-reload", p_qdbus,     t_qdbus,        0, "bool"    },
};

/* The network-backed tools are bounded with /usr/bin/timeout, exactly the
 * way the app has always bounded them. The shorter commands are not: they
 * answer immediately and the agent applies its own watchdog on top. */
#define ACC_CMD_TIMEOUT_PREFIX_FROM 30

/* ------------------------------------------------------- helper whitelist */

/* The verbs data/acc-helper accepts. Unchanged: the helper is the final
 * gate and these are the same names it has always used. */
static const gchar *const k_helper_actions[] = {
  "set-perf", "set-charge", "set-charge-mode",
  "set-gpu-mux", "set-kbd", NULL
};

const gchar *const *
acc_command_helper_actions (void)
{
  return k_helper_actions;
}

gboolean
acc_command_helper_action_ok (const gchar *action)
{
  if (!action || !*action)
    return FALSE;
  for (int i = 0; k_helper_actions[i]; i++)
    if (g_strcmp0 (k_helper_actions[i], action) == 0)
      return TRUE;
  return FALSE;
}

/* The values the helper accepts, per verb. These mirror the case arms in
 * data/acc-helper exactly. */
gboolean
acc_command_helper_value_ok (const gchar *action, const gchar *value)
{
  if (!action || !value || !*value)
    return FALSE;

  if (g_strcmp0 (action, "set-perf") == 0)
    return g_strcmp0 (value, "quiet") == 0
        || g_strcmp0 (value, "balanced") == 0
        || g_strcmp0 (value, "performance") == 0;

  if (g_strcmp0 (action, "set-charge") == 0)
    return g_strcmp0 (value, "60") == 0
        || g_strcmp0 (value, "80") == 0
        || g_strcmp0 (value, "100") == 0;

  if (g_strcmp0 (action, "set-charge-mode") == 0)
    return g_strcmp0 (value, "0") == 0
        || g_strcmp0 (value, "1") == 0
        || g_strcmp0 (value, "2") == 0;

  if (g_strcmp0 (action, "set-gpu-mux") == 0)
    return g_strcmp0 (value, "0") == 0 || g_strcmp0 (value, "1") == 0;

  if (g_strcmp0 (action, "set-kbd") == 0)
    return g_strcmp0 (value, "0") == 0
        || g_strcmp0 (value, "1") == 0
        || g_strcmp0 (value, "2") == 0
        || g_strcmp0 (value, "3") == 0;

  return FALSE;
}

/* ------------------------------------------------------------------- ids */

const gchar *
acc_command_id (AccCommand id)
{
  if ((guint) id >= G_N_ELEMENTS (k_cmds))
    return NULL;
  return k_cmds[id].id;
}

gint
acc_command_from_id (const gchar *id)
{
  if (!id)
    return -1;
  for (guint i = 0; i < G_N_ELEMENTS (k_cmds); i++)
    if (g_strcmp0 (k_cmds[i].id, id) == 0)
      return (gint) i;
  return -1;
}

/* --------------------------------------------------------- argument rules */

static gboolean
has_safe_chars (const gchar *s, gsize max_len)
{
  if (!s || !*s || strlen (s) > max_len)
    return FALSE;
  if (s[0] == '-')
    return FALSE;               /* never let an argument look like a flag */
  for (const gchar *c = s; *c; c++)
    if (!g_ascii_isalnum (*c) && *c != '_' && *c != '-' && *c != '.'
        && *c != ':' && *c != '@')
      return FALSE;
  return TRUE;
}

static gboolean
is_hexish (const gchar *s, gsize max_len)
{
  if (!s || !*s || strlen (s) > max_len)
    return FALSE;
  for (const gchar *c = s; *c; c++)
    if (!g_ascii_isxdigit (*c) && *c != '-')
      return FALSE;
  return TRUE;
}

static gboolean
in_list (const gchar *s, const gchar *const *list)
{
  if (!s)
    return FALSE;
  for (int i = 0; list[i]; i++)
    if (g_strcmp0 (list[i], s) == 0)
      return TRUE;
  return FALSE;
}

/* the services the app is allowed to ask about */
static const gchar *const k_services[] = {
  "asusd", "supergfxd", "asus-shutdown", NULL
};

/* the block devices the app is allowed to ask SMART about */
static const gchar *const k_devices[] = {
  "/dev/nvme0", "/dev/nvme0n1", "/dev/sda", "/dev/vda", NULL
};

gboolean
acc_command_arg_ok (AccCommand id, const gchar *arg)
{
  if ((guint) id >= G_N_ELEMENTS (k_cmds))
    return FALSE;

  const gchar *kind = k_cmds[id].arg_kind;
  if (!kind)
    return arg == NULL || !*arg;   /* a command with no slot takes no value */

  if (!arg || !*arg)
    return FALSE;

  if (g_strcmp0 (kind, "iface") == 0)
    return has_safe_chars (arg, 32);

  if (g_strcmp0 (kind, "service") == 0)
    return in_list (arg, k_services);

  if (g_strcmp0 (kind, "device") == 0)
    return in_list (arg, k_devices);

  if (g_strcmp0 (kind, "bootid") == 0)
    return is_hexish (arg, 64);

  if (g_strcmp0 (kind, "uuid") == 0)
    return has_safe_chars (arg, 128) && strchr (arg, '@') != NULL;

  if (g_strcmp0 (kind, "appid") == 0)
    return has_safe_chars (arg, 64);

  if (g_strcmp0 (kind, "bool") == 0)
    return g_strcmp0 (arg, "true") == 0 || g_strcmp0 (arg, "false") == 0;

  return FALSE;
}

/* --------------------------------------------------------------- argv */

/* Resolve the program from its fixed candidate list. The first one that
 * exists wins; the absolute path is used so there is no PATH hijack. */
static gchar *
resolve_program (const gchar *const *progs)
{
  for (int i = 0; progs && progs[i]; i++)
    {
      gchar *p = g_find_program_in_path (progs[i]);
      if (p)
        return p;
    }
  return NULL;
}

gchar **
acc_command_argv (AccCommand id, const gchar *arg, gchar **err)
{
  if (err)
    *err = NULL;

  if ((guint) id >= G_N_ELEMENTS (k_cmds))
    {
      if (err)
        *err = g_strdup ("unknown command");
      return NULL;
    }

  const AccCmdDef *d = &k_cmds[id];

  if (!d->progs)
    {
      if (err)
        *err = g_strdup ("command has no program");
      return NULL;
    }

  if (!acc_command_arg_ok (id, arg))
    {
      if (err)
        *err = g_strdup_printf ("invalid argument for %s", d->id);
      return NULL;
    }

  gchar *prog = resolve_program (d->progs);
  if (!prog)
    {
      if (err)
        *err = g_strdup_printf ("%s is not installed", d->progs[0]);
      return NULL;
    }

  GPtrArray *v = g_ptr_array_new_with_free_func (g_free);

  /* bound the network-backed tools the same way the app always has */
  if (d->timeout_secs >= ACC_CMD_TIMEOUT_PREFIX_FROM
      && g_file_test ("/usr/bin/timeout", G_FILE_TEST_EXISTS))
    {
      g_ptr_array_add (v, g_strdup ("/usr/bin/timeout"));
      g_ptr_array_add (v, g_strdup_printf ("%d", d->timeout_secs));
    }

  g_ptr_array_add (v, prog);

  /* the journal wants the boot id without its dashes */
  gchar *argval = NULL;
  if (id == ACC_CMD_JOURNAL_MUX && arg)
    {
      argval = g_strdup (arg);
      gchar *w = argval;
      for (const gchar *c = arg; *c; c++)
        if (*c != '-')
          *w++ = *c;
      *w = '\0';
    }

  for (int i = 0; d->tpl && d->tpl[i]; i++)
    {
      if (g_strcmp0 (d->tpl[i], "@ARG") == 0)
        g_ptr_array_add (v, g_strdup (argval ? argval : (arg ? arg : "")));
      else
        g_ptr_array_add (v, g_strdup (d->tpl[i]));
    }
  g_free (argval);

  g_ptr_array_add (v, NULL);
  return (gchar **) g_ptr_array_free (g_steal_pointer (&v), FALSE);
}

/* --------------------------------------------------------------- running */

gboolean
acc_command_run_local (AccCommand id, const gchar *arg,
                       gchar **out, gint *exit_code, gchar **err)
{
  if (out)
    *out = NULL;
  if (exit_code)
    *exit_code = -1;
  if (err)
    *err = NULL;

  if ((guint) id >= G_N_ELEMENTS (k_cmds))
    {
      if (err)
        *err = g_strdup ("unknown command");
      return FALSE;
    }

  /* the loaded modules are readable without any program at all */
  if (id == ACC_CMD_LOADED_MODULES)
    {
      gchar *txt = NULL;
      if (!g_file_get_contents ("/proc/modules", &txt, NULL, NULL))
        {
          if (err)
            *err = g_strdup ("could not read /proc/modules");
          return FALSE;
        }
      if (out)
        *out = txt;
      else
        g_free (txt);
      if (exit_code)
        *exit_code = 0;
      return TRUE;
    }

  gchar *aerr = NULL;
  gchar **argv = acc_command_argv (id, arg, &aerr);
  if (!argv)
    {
      if (err)
        *err = aerr ? aerr : g_strdup ("could not build the command");
      else
        g_free (aerr);
      return FALSE;
    }

  GError *e = NULL;
  gint status = 0;
  gchar *o = NULL;
  gboolean ok = g_spawn_sync (NULL, argv, NULL, (GSpawnFlags) 0,
                              NULL, NULL, &o, NULL, &status, &e);
  g_strfreev (argv);

  if (!ok)
    {
      if (err)
        *err = g_strdup (e ? e->message : "could not run the command");
      g_clear_error (&e);
      g_free (o);
      return FALSE;
    }
  g_clear_error (&e);

  if (exit_code)
    *exit_code = WIFEXITED (status) ? WEXITSTATUS (status) : -1;
  if (out)
    *out = o;
  else
    g_free (o);
  return TRUE;
}
