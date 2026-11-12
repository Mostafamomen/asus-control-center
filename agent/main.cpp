/* agent/main.cpp - the ASUSCraft host agent
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A thin, unprivileged, D-Bus-activated service that runs on the HOST, on
 * the session bus, as the ordinary user. It exists because a Flatpak has
 * no /usr/bin/pkexec, no gnome-extensions and no host utilities.
 *
 * It holds NO privilege of its own:
 *   - every privileged operation is the EXISTING whitelist-only helper
 *     (/usr/libexec/asus-control-center/acc-helper) run through pkexec,
 *     so the polkit action id io.github.asuscontrolcenter.helper and the
 *     helper's whitelist are unchanged;
 *   - everything else is a read-only host command built from the fixed
 *     table in src/control/commands.cpp - the same table the app uses.
 *
 * The agent lives OUTSIDE src/ on purpose: the app itself never owns a
 * bus name (see AGENTS.md).
 *
 * Usage:
 *   asuscraft-agent            run the service (D-Bus activated)
 *   asuscraft-agent --check    the pure-logic self test (no bus, no host)
 *   asuscraft-agent --version
 */
#include <gio/gio.h>
#include <glib-unix.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>

#include "agentproto.h"
#include "commands.h"
#include "nlsettings.h"
#include "sbp_local.h"

#define PKEXEC  "/usr/bin/pkexec"
#define HELPER  "/usr/libexec/asus-control-center/acc-helper"
#define TRUECMD "/usr/bin/true"

/* how long the agent stays alive after the last call */
#define IDLE_SECONDS 300
/* the longest a privileged call may wait for the polkit dialog */
#define HELPER_TIMEOUT 120

static const gchar *INTROSPECTION =
  "<node>"
  "  <interface name='" ACC_AGENT_IFACE "'>"
  "    <method name='GetInfo'>"
  "      <arg type='s' name='version' direction='out'/>"
  "      <arg type='s' name='capabilities' direction='out'/>"
  "      <arg type='s' name='desktop' direction='out'/>"
  "    </method>"
  "    <method name='RunCommand'>"
  "      <arg type='s' name='command_id' direction='in'/>"
  "      <arg type='s' name='argument' direction='in'/>"
  "      <arg type='i' name='exit_code' direction='out'/>"
  "      <arg type='s' name='stdout' direction='out'/>"
  "      <arg type='s' name='error' direction='out'/>"
  "    </method>"
  "    <method name='SetHardware'>"
  "      <arg type='s' name='action' direction='in'/>"
  "      <arg type='s' name='value' direction='in'/>"
  "      <arg type='i' name='exit_code' direction='out'/>"
  "      <arg type='s' name='error' direction='out'/>"
  "    </method>"
  "    <method name='CheckAdmin'>"
  "      <arg type='i' name='exit_code' direction='out'/>"
  "    </method>"
  "    <method name='SoftBrightness'>"
  "      <arg type='s' name='operation' direction='in'/>"
  "      <arg type='i' name='exit_code' direction='out'/>"
  "      <arg type='s' name='result' direction='out'/>"
  "      <arg type='s' name='error' direction='out'/>"
  "    </method>"
  "    <method name='NightLightGet'>"
  "      <arg type='s' name='settings' direction='out'/>"
  "    </method>"
  "    <method name='NightLightSet'>"
  "      <arg type='s' name='key' direction='in'/>"
  "      <arg type='s' name='value' direction='in'/>"
  "      <arg type='b' name='ok' direction='out'/>"
  "      <arg type='s' name='error' direction='out'/>"
  "    </method>"
  "    <method name='BrightnessGet'>"
  "      <arg type='s' name='value' direction='out'/>"
  "    </method>"
  "    <method name='BrightnessSet'>"
  "      <arg type='s' name='value' direction='in'/>"
  "      <arg type='b' name='ok' direction='out'/>"
  "      <arg type='s' name='error' direction='out'/>"
  "    </method>"
  "    <method name='TouchpadGet'>"
  "      <arg type='s' name='state' direction='out'/>"
  "    </method>"
  "    <method name='TouchpadSet'>"
  "      <arg type='s' name='state' direction='in'/>"
  "      <arg type='b' name='ok' direction='out'/>"
  "      <arg type='s' name='error' direction='out'/>"
  "    </method>"
  "  </interface>"
  "</node>";

/* ------------------------------------------------------------ utilities */

static GMainLoop *s_loop = NULL;
static guint      s_idle_src = 0;

static gboolean
idle_expired (gpointer data)
{
  (void) data;
  s_idle_src = 0;
  g_main_loop_quit (s_loop);
  return G_SOURCE_REMOVE;
}

/* every call resets the idle timer; the agent then exits by itself */
static void
idle_touch (void)
{
  if (s_idle_src)
    g_source_remove (s_idle_src);
  s_idle_src = g_timeout_add_seconds (IDLE_SECONDS, idle_expired, NULL);
}

static const gchar *
current_desktop (void)
{
  const gchar *xdg = g_getenv ("XDG_CURRENT_DESKTOP");
  if (xdg && *xdg)
    return xdg;
  const gchar *ds = g_getenv ("DESKTOP_SESSION");
  if (ds && *ds)
    return ds;
  return "unknown";
}

/* the watchdog: kill a child that takes too long */
static gboolean
watchdog_cb (gpointer data)
{
  GSubprocess *p = (GSubprocess *) data;
  g_subprocess_force_exit (p);
  return G_SOURCE_REMOVE;
}

/* Run a fixed argv with a watchdog. No shell, ever. */
static gboolean
run_argv (const gchar *const *argv, gint timeout_secs,
          gchar **out, gchar **errout, gint *exit_code)
{
  if (out)
    *out = NULL;
  if (errout)
    *errout = NULL;
  if (exit_code)
    *exit_code = -1;

  GError *e = NULL;
  GSubprocess *p = g_subprocess_newv (argv,
                                      (GSubprocessFlags) (G_SUBPROCESS_FLAGS_STDOUT_PIPE
                                                          | G_SUBPROCESS_FLAGS_STDERR_PIPE),
                                      &e);
  if (!p)
    {
      g_clear_error (&e);
      return FALSE;
    }

  guint watchdog = 0;
  if (timeout_secs > 0)
    watchdog = g_timeout_add_seconds (timeout_secs, watchdog_cb, p);

  gchar *o = NULL, *er = NULL;
  gboolean ok = g_subprocess_communicate_utf8 (p, NULL, NULL, &o, &er, &e);
  if (watchdog)
    g_source_remove (watchdog);

  if (exit_code)
    *exit_code = ok ? g_subprocess_get_exit_status (p) : -1;

  g_clear_error (&e);
  g_object_unref (p);

  if (out)
    *out = o;
  else
    g_free (o);
  if (errout)
    *errout = er;
  else
    g_free (er);
  return ok;
}

/* ------------------------------------------------------- the night light */

/* The settings themselves live in src/control/nlsettings.cpp, which the
 * app also compiles: the two can never disagree about the night light. */

/* ------------------------------------------- the session's own settings */

/* The OLED dimming value and the touchpad switch belong to the desktop
 * session. A sandbox cannot be trusted to reach the host's dconf (the
 * shared-memory snapshot is not always there), so the agent does it. */

/* The Soft Brightness Plus schema lives in the user's own extension
 * folder (that is where the extension installs it), not in the system
 * schemas. The touchpad schema is a system one. */
static GSettings *
session_settings (const gchar *schema)
{
  GSettingsSchemaSource *src = g_settings_schema_source_get_default ();
  if (!src)
    return NULL;
  GSettingsSchema *s = g_settings_schema_source_lookup (src, schema, TRUE);
  if (s)
    {
      g_settings_schema_unref (s);
      return g_settings_new (schema);
    }

  if (g_strcmp0 (schema, SBP_SCHEMA) == 0)
    {
      gchar *dir = g_build_filename (g_get_user_data_dir (), "gnome-shell",
                                     "extensions", SBP_UUID, "schemas", NULL);
      GSettingsSchemaSource *own = g_settings_schema_source_new_from_directory
        (dir, src, FALSE, NULL);
      g_free (dir);
      if (own)
        {
          GSettingsSchema *os = g_settings_schema_source_lookup (own, schema,
                                                                  TRUE);
          if (os)
            {
              GSettings *st = g_settings_new_full (os, NULL, NULL);
              g_settings_schema_unref (os);
              g_settings_schema_source_unref (own);
              return st;
            }
          g_settings_schema_source_unref (own);
        }
    }
  return NULL;
}

/* the Soft Brightness Plus dimming value, as a percentage */
static gchar *
brightness_read (void)
{
  GSettings *st = session_settings (SBP_SCHEMA);
  if (!st)
    return NULL;
  gdouble v = g_settings_get_double (st, SBP_KEY) * 100.0;
  g_object_unref (st);
  return g_strdup_printf ("%.0f", v);
}

static gboolean
brightness_write (const gchar *value, gchar **err)
{
  gchar *end = NULL;
  gdouble pct = g_ascii_strtod (value, &end);
  if (!end || *end || end == value || pct < 10.0 || pct > 100.0)
    {
      if (err)
        *err = g_strdup ("the value must be a percentage between 10 and 100");
      return FALSE;
    }
  GSettings *st = session_settings (SBP_SCHEMA);
  if (!st)
    {
      if (err)
        *err = g_strdup ("the Soft Brightness Plus extension is not installed");
      return FALSE;
    }
  g_settings_set_double (st, SBP_KEY, pct / 100.0);
  g_settings_sync ();
  g_object_unref (st);
  return TRUE;
}

/* the touchpad switch: "enabled", "disabled" or "unsupported" */
static gchar *
touchpad_read (void)
{
  GSettings *st = session_settings (TP_SCHEMA);
  if (!st)
    return g_strdup ("unsupported");
  gchar *v = g_settings_get_string (st, TP_KEY);
  gboolean on = g_strcmp0 (v, "disabled") != 0;
  g_free (v);
  g_object_unref (st);
  return g_strdup (on ? "enabled" : "disabled");
}

static gboolean
touchpad_write (const gchar *state, gchar **err)
{
  if (g_strcmp0 (state, "enabled") != 0 && g_strcmp0 (state, "disabled") != 0)
    {
      if (err)
        *err = g_strdup ("the state must be enabled or disabled");
      return FALSE;
    }
  GSettings *st = session_settings (TP_SCHEMA);
  if (!st)
    {
      if (err)
        *err = g_strdup ("this desktop does not expose a touchpad switch");
      return FALSE;
    }
  g_settings_set_string (st, TP_KEY, state);
  g_settings_sync ();
  g_object_unref (st);
  return TRUE;
}

/* --------------------------------------------------- the privileged path */

/* The helper is the ONLY privileged program. Its whitelist and the polkit
 * action id are unchanged. */
static gboolean
run_helper (const gchar *action, const gchar *value,
            gint *exit_code, gchar **err)
{
  if (err)
    *err = NULL;

  if (!acc_command_helper_action_ok (action)
      || !acc_command_helper_value_ok (action, value))
    {
      if (err)
        *err = g_strdup ("the action or the value is not allowed");
      if (exit_code)
        *exit_code = 2;
      return FALSE;
    }

  if (!g_file_test (PKEXEC, G_FILE_TEST_EXISTS)
      || !g_file_test (HELPER, G_FILE_TEST_EXISTS))
    {
      if (err)
        *err = g_strdup ("the ASUSCraft helper is not installed on this host");
      if (exit_code)
        *exit_code = 1;
      return FALSE;
    }

  const gchar *argv[] = { PKEXEC, HELPER, action, value, NULL };
  gchar *out = NULL, *er = NULL;
  gint code = -1;
  gboolean ok = run_argv (argv, HELPER_TIMEOUT, &out, &er, &code);

  if (exit_code)
    *exit_code = ok ? code : -1;

  if (!ok)
    {
      if (err)
        *err = g_strdup (er && *er ? g_strstrip (er) : "could not run the helper");
    }
  else if (code != 0 && err)
    {
      gchar *m = g_strdup (er && *er ? g_strstrip (er) : "");
      if (!*m)
        {
          g_free (m);
          m = g_strdup_printf ("the helper refused the request (exit %d)", code);
        }
      *err = m;
    }

  g_free (out);
  g_free (er);
  return ok && code == 0;
}

/* the admin probe the sign-in page uses (pkexec /usr/bin/true) */
static gint
check_admin (void)
{
  if (!g_file_test (PKEXEC, G_FILE_TEST_EXISTS))
    return -1;
  const gchar *argv[] = { PKEXEC, TRUECMD, NULL };
  gchar *out = NULL, *er = NULL;
  gint code = -1;
  run_argv (argv, HELPER_TIMEOUT, &out, &er, &code);
  g_free (out);
  g_free (er);
  return code;
}

/* ---------------------------------------------------------- the workers */

typedef enum {
  JOB_RUN = 0,
  JOB_SET,
  JOB_ADMIN,
  JOB_SBP,
  JOB_NL_SET
} JobKind;

typedef struct {
  GDBusMethodInvocation *inv;
  JobKind kind;
  gchar *a;
  gchar *b;
} Job;

static void
job_free (Job *j)
{
  if (!j)
    return;
  g_free (j->a);
  g_free (j->b);
  g_free (j);
}

static gpointer
job_worker (gpointer data)
{
  Job *j = (Job *) data;
  idle_touch ();

  switch (j->kind)
    {
    case JOB_RUN:
      {
        gint idx = acc_command_from_id (j->a);
        if (idx < 0)
          {
            g_dbus_method_invocation_return_value
              (j->inv, g_variant_new ("(iss)", -2, "", "unknown command"));
            break;
          }
        gchar *out = NULL, *err = NULL;
        gint code = -1;
        gboolean ok = acc_command_run_local ((AccCommand) idx, j->b,
                                            &out, &code, &err);
        if (!ok)
          {
            g_dbus_method_invocation_return_value
              (j->inv, g_variant_new ("(iss)", code, out ? out : "",
                                      err ? err : "the command failed"));
          }
        else
          {
            g_dbus_method_invocation_return_value
              (j->inv, g_variant_new ("(iss)", code, out ? out : "", ""));
          }
        g_free (out);
        g_free (err);
        break;
      }

    case JOB_SET:
      {
        gint code = -1;
        gchar *err = NULL;
        gboolean ok = run_helper (j->a, j->b, &code, &err);
        g_dbus_method_invocation_return_value
          (j->inv, g_variant_new ("(is)", code, err ? err : ""));
        (void) ok;
        g_free (err);
        break;
      }

    case JOB_ADMIN:
      {
        gint code = check_admin ();
        g_dbus_method_invocation_return_value
          (j->inv, g_variant_new ("(i)", code));
        break;
      }

    case JOB_SBP:
      {
        const gchar *op = j->a ? j->a : "";
        if (g_strcmp0 (op, "status") == 0)
          {
            AccSoftDim st = acc_sbp_local_status ();
            const gchar *name = st == ACC_SD_ON ? "on"
                              : st == ACC_SD_DISABLED ? "disabled" : "missing";
            g_dbus_method_invocation_return_value
              (j->inv, g_variant_new ("(iss)", 0, name, ""));
          }
        else if (g_strcmp0 (op, "install") == 0)
          {
            gchar *err = NULL;
            gboolean ok = acc_sbp_local_install (&err);
            g_dbus_method_invocation_return_value
              (j->inv, g_variant_new ("(iss)", ok ? 0 : 1, "",
                                      err ? err : ""));
            g_free (err);
          }
        else if (g_strcmp0 (op, "enable") == 0)
          {
            gchar *err = NULL;
            gboolean ok = acc_sbp_local_enable (&err);
            g_dbus_method_invocation_return_value
              (j->inv, g_variant_new ("(iss)", ok ? 0 : 1, "",
                                      err ? err : ""));
            g_free (err);
          }
        else
          {
            g_dbus_method_invocation_return_value
              (j->inv, g_variant_new ("(iss)", -2, "", "unknown operation"));
          }
        break;
      }

    case JOB_NL_SET:
      {
        gchar *err = NULL;
        gboolean ok = acc_nl_write (j->a, j->b, &err);
        g_dbus_method_invocation_return_value
          (j->inv, g_variant_new ("(bs)", ok, err ? err : ""));
        g_free (err);
        break;
      }
    }

  job_free (j);
  return NULL;
}

static void
start_job (GDBusMethodInvocation *inv, JobKind kind,
           const gchar *a, const gchar *b)
{
  Job *j = g_new0 (Job, 1);
  j->inv = inv;
  j->kind = kind;
  j->a = a ? g_strdup (a) : NULL;
  j->b = b ? g_strdup (b) : NULL;
  idle_touch ();
  g_thread_unref (g_thread_new ("acc-agent-job", job_worker, j));
}

/* ------------------------------------------------------ the D-Bus object */

static void
handle_method (GDBusConnection *conn, const gchar *sender,
               const gchar *object_path, const gchar *interface_name,
               const gchar *method_name, GVariant *parameters,
               GDBusMethodInvocation *inv, gpointer user_data)
{
  (void) conn; (void) sender; (void) object_path;
  (void) interface_name; (void) user_data;

  idle_touch ();

  if (g_strcmp0 (method_name, "GetInfo") == 0)
    {
      g_dbus_method_invocation_return_value
        (inv, g_variant_new ("(sss)", ACC_AGENT_VERSION,
                             "hardware,commands,softbrightness,nightlight",
                             current_desktop ()));
      return;
    }

  if (g_strcmp0 (method_name, "RunCommand") == 0)
    {
      const gchar *id = NULL, *arg = NULL;
      g_variant_get (parameters, "(&s&s)", &id, &arg);
      start_job (inv, JOB_RUN, id, arg);
      return;
    }

  if (g_strcmp0 (method_name, "SetHardware") == 0)
    {
      const gchar *action = NULL, *value = NULL;
      g_variant_get (parameters, "(&s&s)", &action, &value);
      start_job (inv, JOB_SET, action, value);
      return;
    }

  if (g_strcmp0 (method_name, "CheckAdmin") == 0)
    {
      start_job (inv, JOB_ADMIN, NULL, NULL);
      return;
    }

  if (g_strcmp0 (method_name, "SoftBrightness") == 0)
    {
      const gchar *op = NULL;
      g_variant_get (parameters, "(&s)", &op);
      start_job (inv, JOB_SBP, op, NULL);
      return;
    }

  if (g_strcmp0 (method_name, "NightLightGet") == 0)
    {
      gchar *text = acc_nl_read ();
      if (!text)
        {
          g_dbus_method_invocation_return_error
            (inv, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED,
             "this desktop does not provide the GNOME night light");
          return;
        }
      g_dbus_method_invocation_return_value (inv, g_variant_new ("(s)", text));
      g_free (text);
      return;
    }

  if (g_strcmp0 (method_name, "NightLightSet") == 0)
    {
      const gchar *key = NULL, *value = NULL;
      g_variant_get (parameters, "(&s&s)", &key, &value);
      start_job (inv, JOB_NL_SET, key, value);
      return;
    }

  if (g_strcmp0 (method_name, "BrightnessGet") == 0)
    {
      gchar *v = brightness_read ();
      g_dbus_method_invocation_return_value (inv, g_variant_new ("(s)",
                                              v ? v : ""));
      g_free (v);
      return;
    }

  if (g_strcmp0 (method_name, "BrightnessSet") == 0)
    {
      const gchar *value = NULL;
      g_variant_get (parameters, "(&s)", &value);
      gchar *err = NULL;
      gboolean ok = brightness_write (value, &err);
      g_dbus_method_invocation_return_value
        (inv, g_variant_new ("(bs)", ok, err ? err : ""));
      g_free (err);
      return;
    }

  if (g_strcmp0 (method_name, "TouchpadGet") == 0)
    {
      gchar *v = touchpad_read ();
      g_dbus_method_invocation_return_value (inv, g_variant_new ("(s)",
                                              v ? v : "unsupported"));
      g_free (v);
      return;
    }

  if (g_strcmp0 (method_name, "TouchpadSet") == 0)
    {
      const gchar *state = NULL;
      g_variant_get (parameters, "(&s)", &state);
      gchar *err = NULL;
      gboolean ok = touchpad_write (state, &err);
      g_dbus_method_invocation_return_value
        (inv, g_variant_new ("(bs)", ok, err ? err : ""));
      g_free (err);
      return;
    }

  g_dbus_method_invocation_return_error
    (inv, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "unknown method");
}

static const GDBusInterfaceVTable s_vtable = {
  handle_method, NULL, NULL, { 0 }
};

/* ------------------------------------------------------------- the check */

static int s_fail = 0;

static void
expect (gboolean ok, const gchar *what)
{
  g_print ("  %s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok)
    s_fail++;
}

static int
self_check (void)
{
  g_print ("ASUSCraft host agent — self test\n");

  g_print ("the helper whitelist\n");
  expect (acc_command_helper_action_ok ("set-perf"), "set-perf is allowed");
  expect (acc_command_helper_action_ok ("set-kbd"), "set-kbd is allowed");
  expect (!acc_command_helper_action_ok ("rm"), "an unknown action is refused");
  expect (!acc_command_helper_action_ok (NULL), "a NULL action is refused");
  expect (acc_command_helper_value_ok ("set-perf", "quiet"),
          "quiet is an allowed profile");
  expect (!acc_command_helper_value_ok ("set-perf", "turbo"),
          "turbo is not an allowed profile");
  expect (acc_command_helper_value_ok ("set-charge", "80"),
          "80 is an allowed charge limit");
  expect (!acc_command_helper_value_ok ("set-charge", "55"),
          "55 is not an allowed charge limit");
  expect (!acc_command_helper_value_ok ("set-kbd", "9"),
          "9 is not an allowed keyboard level");
  expect (!acc_command_helper_value_ok ("set-gpu-mux", "2"),
          "2 is not an allowed MUX mode");

  g_print ("the command ids\n");
  for (gint i = 0; i < ACC_CMD_COUNT; i++)
    {
      const gchar *id = acc_command_id ((AccCommand) i);
      gboolean ok = id && acc_command_from_id (id) == i;
      expect (ok, id ? id : "(null id)");
    }
  expect (acc_command_from_id ("no-such-command") == -1,
          "an unknown id is refused");
  expect (acc_command_from_id (NULL) == -1, "a NULL id is refused");

  g_print ("the argument rules\n");
  expect (acc_command_arg_ok (ACC_CMD_WIFI_SSID, "wlo1"), "wlo1 is a valid interface");
  expect (!acc_command_arg_ok (ACC_CMD_WIFI_SSID, "-rfkill"),
          "an argument that looks like a flag is refused");
  expect (!acc_command_arg_ok (ACC_CMD_WIFI_SSID, "wlo1; rm -rf /"),
          "an argument with a shell metacharacter is refused");
  expect (acc_command_arg_ok (ACC_CMD_SERVICE_ACTIVE, "asusd"),
          "asusd is an allowed service");
  expect (!acc_command_arg_ok (ACC_CMD_SERVICE_ACTIVE, "sshd"),
          "sshd is not an allowed service");
  expect (acc_command_arg_ok (ACC_CMD_SMART_HEALTH, "/dev/nvme0n1"),
          "a known block device is allowed");
  expect (!acc_command_arg_ok (ACC_CMD_SMART_HEALTH, "/dev/sdb"),
          "an unknown block device is refused");
  expect (acc_command_arg_ok (ACC_CMD_JOURNAL_MUX, "b2c5620000084c54a1f59fabee24462c"),
          "a boot id is allowed");
  expect (!acc_command_arg_ok (ACC_CMD_JOURNAL_MUX, "abc; reboot"),
          "a boot id with a metacharacter is refused");
  expect (acc_command_arg_ok (ACC_CMD_GNOME_EXT_ENABLE,
                              "soft-brightness-plus@joelkitching.com"),
          "an extension uuid is allowed");
  expect (!acc_command_arg_ok (ACC_CMD_GNOME_EXT_ENABLE, "not-a-uuid"),
          "a string that is not a uuid is refused");
  expect (acc_command_arg_ok (ACC_CMD_NL_KDE_SET, "true"),
          "true is an allowed night-light value");
  expect (!acc_command_arg_ok (ACC_CMD_NL_KDE_SET, "yes"),
          "yes is not an allowed night-light value");
  expect (!acc_command_arg_ok (ACC_CMD_PACKAGE_UPDATES, "anything"),
          "a command without an argument slot refuses one");

  g_print ("the argv builder\n");
  {
    gchar *err = NULL;
    gchar **argv = acc_command_argv (ACC_CMD_SERVICE_ACTIVE, "asusd", &err);
    if (argv)
      {
        gboolean fixed = g_str_has_suffix (argv[0], "systemctl")
                         && g_strcmp0 (argv[1], "is-active") == 0
                         && g_strcmp0 (argv[2], "asusd") == 0
                         && argv[3] == NULL;
        expect (fixed, "the service command is a fixed argv");
      }
    else
      expect (g_strcmp0 (err, "systemctl is not installed") == 0,
              "a missing program is reported, not guessed");
    g_strfreev (argv);
    g_free (err);
  }
  {
    gchar *err = NULL;
    gchar **argv = acc_command_argv (ACC_CMD_SERVICE_ACTIVE, "sshd", &err);
    expect (argv == NULL, "an invalid argument builds no argv at all");
    g_strfreev (argv);
    g_free (err);
  }
  {
    gchar *err = NULL;
    gchar **argv = acc_command_argv ((AccCommand) 9999, NULL, &err);
    expect (argv == NULL, "an unknown command builds no argv at all");
    g_strfreev (argv);
    g_free (err);
  }

  g_print ("the loaded modules (no program needed)\n");
  {
    gchar *out = NULL, *err = NULL;
    gint code = -1;
    gboolean ok = acc_command_run_local (ACC_CMD_LOADED_MODULES, NULL,
                                        &out, &code, &err);
    expect (ok && code == 0, "/proc/modules is readable");
    g_free (out);
    g_free (err);
  }

  g_print ("\n%d failure(s)\n", s_fail);
  return s_fail;
}

/* ------------------------------------------------------------------ main */

int
main (int argc, char **argv)
{
  if (argc > 1 && strcmp (argv[1], "--version") == 0)
    {
      g_print ("asuscraft-agent %s\n", ACC_AGENT_VERSION);
      return 0;
    }

  if (argc > 1 && strcmp (argv[1], "--check") == 0)
    return self_check () ? 1 : 0;

  /* the agent is a session service: it must not be run as root */
  if (geteuid () == 0)
    {
      g_printerr ("asuscraft-agent must not run as root\n");
      return 1;
    }

  GDBusConnection *bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
  if (!bus)
    {
      g_printerr ("asuscraft-agent: no session bus\n");
      return 1;
    }

  GError *e = NULL;
  GDBusNodeInfo *node = g_dbus_node_info_new_for_xml (INTROSPECTION, &e);
  if (!node)
    {
      g_printerr ("asuscraft-agent: %s\n", e ? e->message : "bad introspection");
      g_clear_error (&e);
      return 1;
    }

  guint reg = g_dbus_connection_register_object (bus, ACC_AGENT_PATH,
                                                 node->interfaces[0],
                                                 &s_vtable, NULL, NULL, &e);
  if (reg == 0)
    {
      g_printerr ("asuscraft-agent: could not register: %s\n",
                  e ? e->message : "?");
      g_clear_error (&e);
      return 1;
    }

  g_bus_own_name_on_connection (bus, ACC_AGENT_BUS_NAME,
                                G_BUS_NAME_OWNER_FLAGS_REPLACE, NULL, NULL,
                                NULL, NULL);

  s_loop = g_main_loop_new (NULL, FALSE);
  s_idle_src = g_timeout_add_seconds (IDLE_SECONDS, idle_expired, NULL);
  g_main_loop_run (s_loop);

  g_source_remove (s_idle_src);
  g_dbus_connection_unregister_object (bus, reg);
  g_dbus_node_info_unref (node);
  g_object_unref (bus);
  return 0;
}
