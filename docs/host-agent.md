# The ASUSCraft host agent — D-Bus interface

**Component:** `agent/` (the `asuscraft-agent` package)
**Bus:** the **session** bus, as the ordinary user
**Name:** `io.github.asuscontrolcenter.Agent`
**Object:** `/io/github/asuscontrolcenter/Agent`
**Interface:** `io.github.asuscontrolcenter.Agent`
**Activation:** D-Bus activation (`/usr/share/dbus-1/services/io.github.asuscontrolcenter.Agent.service`)

---

## 1. Why it exists

A Flatpak cannot write to the laptop's firmware, cannot run `pkexec`,
cannot run `gnome-extensions` and cannot see the user's extension folder.
The agent runs on the host, in the user's session, and does exactly those
things for the sandboxed app.

**The agent holds no privilege of its own.** Every privileged change goes
through the *existing* whitelist-only helper
(`/usr/libexec/asus-control-center/acc-helper`) under the *existing* polkit
action (`io.github.asuscontrolcenter.helper`), so:

- the authentication prompt is the same one the native app has always shown;
- the list of allowed changes is unchanged;
- the helper itself is unchanged, byte for byte.

The agent is **optional**. The app works without it; the features that need
the host report that clearly instead of failing quietly.

## 2. The security model

| Property | Value |
|---|---|
| Runs as | the ordinary user (it refuses to start as root) |
| Bus | the session bus (never the system bus) |
| Privileges | none of its own |
| Privileged path | `pkexec` → `acc-helper` → the whitelisted sysfs write |
| Commands | a fixed table (`src/control/commands.cpp`); never an argv from the caller |
| Lifetime | exits 5 minutes after the last call; the bus restarts it on demand |

The agent is a **client of the app**, never the other way round: the app
(`src/`) still owns no bus name at all.

## 3. Methods

### `GetInfo() → (s version, s capabilities, s desktop)`

Who the agent is. `capabilities` is a comma-separated list; `desktop` is the
`XDG_CURRENT_DESKTOP` the agent sees.

```
$ gdbus call --session --dest io.github.asuscontrolcenter.Agent \
    --object-path /io/github/asuscontrolcenter/Agent \
    --method io.github.asuscontrolcenter.Agent.GetInfo
('1.0', 'hardware,commands,softbrightness,nightlight', 'GNOME')
```

### `RunCommand(s command_id, s argument) → (i exit_code, s stdout, s error)`

Runs one command from the fixed table on the host. The caller sends an **id**,
never an argv: the agent resolves the program and the arguments itself and
re-validates the argument. `exit_code` is `-2` when the agent does not know
the command (an older agent) — the caller then runs it locally instead.

The table (also in `src/control/commands.cpp`):

| id | program | argument | what it is for |
|---|---|---|---|
| `wifi-ssid` | `iw` | the interface name | the SSID |
| `service-active` | `systemctl` | `asusd` / `supergfxd` / `asus-shutdown` | is the service running |
| `package-updates` | `dnf` | — | pending package updates |
| `apt-updates` | `apt-get` | — | pending package updates (Debian) |
| `firmware-updates` | `fwupdmgr` | — | pending firmware updates |
| `flatpak-updates` | `flatpak` | — | pending Flatpak updates |
| `flatpak-list` | `flatpak` | — | the installed user applications |
| `flatpak-uninstall` | `flatpak` | the application id | remove a user application |
| `loaded-modules` | *(built in: `/proc/modules`)* | — | the loaded kernel modules |
| `bluetooth-adapters` | `bluetoothctl` | — | is there a Bluetooth adapter |
| `smart-health` | `smartctl` | a device from a fixed list | the SMART health |
| `nvidia-smi` | `nvidia-smi` | — | the NVIDIA telemetry |
| `journal-mux` | `journalctl` | the boot id | why a queued GPU switch failed |
| `gnome-extensions-enabled` | `gnome-extensions` | — | the enabled extensions |
| `gnome-extensions-disabled` | `gnome-extensions` | — | the disabled extensions |
| `gnome-extension-enable` | `gnome-extensions` | the extension uuid | enable an extension |
| `gnome-extension-disable` | `gnome-extensions` | the extension uuid | disable an extension |
| `nightlight-kde-get` | `kreadconfig5/6` | — | the KDE night colour state |
| `nightlight-kde-set` | `kwriteconfig5/6` | `true` / `false` | the KDE night colour |
| `nightlight-kde-reload` | `qdbus*` | `true` / `false` | ask KWin to reload |

(The two settings that belong to the session — the OLED dimming value and
the touchpad switch — are not commands: they have their own methods,
because they are GSettings writes, not programs.)

**The argument rules** (enforced in the agent, in the app and in the agent's
own self test):

- an interface name: letters, digits, `_`, `-`, `.`, `:` — at most 32, and
  never starting with `-`;
- a service: one of the three names above;
- a device: one of `/dev/nvme0`, `/dev/nvme0n1`, `/dev/sda`, `/dev/vda`;
- a boot id: hexadecimal and dashes only;
- an extension uuid: `name@domain` with no shell characters;
- an application id: letters, digits, `.`, `_`, `-`, at most 64;
- a boolean: exactly `true` or `false`.

### `SetHardware(s action, s value) → (i exit_code, s error)`

The privileged path. The action and the value are checked against the
helper's whitelist in the agent **and** in the helper itself.

| action | allowed values |
|---|---|
| `set-perf` | `quiet`, `balanced`, `performance` |
| `set-charge` | `60`, `80`, `100` |
| `set-charge-mode` | `0`, `1`, `2` |
| `set-gpu-mux` | `0`, `1` |
| `set-kbd` | `0`, `1`, `2`, `3` |

The exit code is the helper's own: `0` ok, `2` a value that is not allowed,
`3` the modules are in use, `4` the firmware refused it, `5` the MUX write
was not confirmed, `127` the authorization was dismissed.

### `CheckAdmin() → (i exit_code)`

The same probe the sign-in page has always used (`pkexec /usr/bin/true`),
so that resetting the local account still asks for the administrator
password.

### `SoftBrightness(s operation) → (i exit_code, s result, s error)`

Manages the bundled **Soft Brightness Plus** GNOME Shell extension.

| operation | result | what it does |
|---|---|---|
| `status` | `on` / `disabled` / `missing` | is the extension installed and enabled |
| `install` | — | copies the bundled extension into `~/.local/share/gnome-shell/extensions`, compiles its schema, points it at the built-in panel |
| `enable` | — | adds the uuid to `enabled-extensions` and asks the running shell to enable it |

### `NightLightGet() → (s settings)`

The GNOME night light, as a key file:

```
[night-light]
night-light-enabled=false
night-light-temperature=4445
night-light-schedule-automatic=true
night-light-schedule-from=8
night-light-schedule-to=6
```

Only the keys the desktop actually has are returned. The value's shape
follows the schema: minutes on the older GNOME, hours on the newer one.

### `NightLightSet(s key, s value) → (b ok, s error)`

Changes one night-light key. The key must be one of the six night-light
keys, and the value must fit the key's own type and range. **No other
setting in the user's colour schema is ever written.**

| key | value |
|---|---|
| `night-light-enabled` | `true` / `false` |
| `night-light-temperature` | 1700 – 4700 (Kelvin) |
| `night-light-schedule-automatic` | `true` / `false` |
| `night-light-schedule-from` / `-to` | 0 – 24 (hours) or 0 – 1439 (minutes) |
| `night-light-schedule` | `manual` / `night-only` / `sunset-to-sunrise` |

### `BrightnessGet() → (s value)` and `BrightnessSet(s value) → (b ok, s error)`

The OLED dimming value of the Soft Brightness Plus extension, as a
percentage. The value lives in the desktop session's dconf, and a sandbox
cannot be trusted to reach the host's database, so the agent reads and
writes it there. The value is clamped to **10–100 %** and refused outside
that range.

### `TouchpadGet() → (s state)` and `TouchpadSet(s state) → (b ok, s error)`

The touchpad switch: `enabled`, `disabled` or `unsupported`. The agent
writes the host's `org.gnome.desktop.peripherals.touchpad` `send-events`
key. A desktop with no such setting answers `unsupported`, and the app
says so instead of pretending to work.

## 4. Detection

The app finds the agent like this:

1. `org.freedesktop.DBus.NameHasOwner` — is it already running?
2. if not, `org.freedesktop.DBus.StartServiceByName` — can the bus start it?
   (this is the D-Bus activation path)
3. if the name is owned, `GetInfo` — does it really answer?

The three states are shown in the app: **Connected** (with the agent's
version), **Not answering**, **Not installed**. The Settings page has a
*Check again* button that repeats the probe.

## 5. The shared table

`src/control/commands.cpp` is compiled into **both** binaries. The app uses
it for its local fallback, the agent uses it to run the commands. The two
can therefore never disagree about what a command means or which arguments
it accepts.

The agent's own self test (`asuscraft-agent --check`) walks the whole table:
every id, every argument rule, and the argv that each command builds.

## 6. Errors

| where | what |
|---|---|
| the agent is not installed | the app uses its local path and says the feature is unavailable |
| the agent does not know a command | the app runs it locally instead |
| the helper is not installed on the host | `SetHardware` returns exit `1` and the message "the ASUSCraft helper is not installed on this host" |
| the authorization was dismissed | `SetHardware` returns exit `127` |
| the polkit dialog is never answered | the call is cut off after 120 seconds |
