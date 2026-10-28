# ACC Security Report — M3 Update (asusd + verified writes)

Date: 2026-10-27 · Scope: `src/**`, `data/acc-helper`, polkit policy,
the asusd D-Bus client (`src/control/asusd.cpp`).
M1 read-only audit verdict unchanged and still valid: fixed paths,
constant argv, bounded parsing, no injection surface in any probe.
This page covers what changed when M2 controls went live.

## Privilege model (unchanged in spirit: least privilege)

| Layer | What runs there |
|---|---|
| GUI (`asus-control-center`) | Always the normal user. Probes are read-only (sysfs/EDID/`nvidia-smi`, plus `/proc/stat`, `/proc/meminfo`, `statvfs("/")`). The login account is stored locally in `~/.config/asus-control-center/settings.conf` as a salted SHA-256 hash (mode 0600), never plaintext. One user-level dconf write (OLED dim key, clamped 10–100 %). Launches: quick apps and GNOME Software, each a **compile-time-constant absolute path** spawned as `path + NULL` argv (no shell). The Warranty card opens a `https://www.asus.com/…` URI in the default browser (`GtkLinkButton`, no shell). |
| `pkexec /usr/local/bin/acc-helper <action> <value>` | Fallback write path, only when **you** click a hardware control and asusd is not running. One action + one validated value per invocation; nothing else. |
| `xyz.ljones.Asusd` (system bus) | Used when the ASUS service from `asusctl` is installed and running. The service is root-owned and already does the firmware writes; the GUI only asks it for fixed properties on fixed object paths. Its D-Bus policy (`/usr/share/dbus-1/system.d/asusd.conf`) allows group `users`, so no polkit prompt appears for this path. asusd enforces its own limits. |
| Nothing else | No `sudo`, no setuid, no polkit rule beyond the one action; the GUI never runs as root and never writes to sysfs itself. |

## The helper is whitelist-only

`data/acc-helper` (installed 0755 at `/usr/local/bin/acc-helper`) is a bash
case-statement: **every action and every accepted value is a hard-coded
literal**. A wrong value exits 2 without touching anything. Accepted today:

| Action | Accepted values | Target |
|---|---|---|
| `set-charge` | `60` / `80` / `100` | `BAT0/charge_control_end_threshold` |
| `set-charge-mode` | `0` / `1` / `2` | `asus-nb-wmi/charge_mode` |
| `set-perf` | `quiet` / `balanced` / `performance` | `acpi/platform_profile` |
| `set-gpu-mux` | `0` / `1` | `asus-armoury/gpu_mux_mode`, legacy `asus-nb-wmi` as fallback |
| `set-kbd` | `0`–`3` | keyboard LED level |

No variables reach a shell command line; nothing is concatenated into
commands; no eval. Exit codes report *why* an action failed: `0` ok,
`2` disallowed value, `3` reserved (was: NVIDIA modules busy), `4`
firmware refused the value. The GUI maps these to exact translated
toasts via `acc_ctl_last_exit()` (`127` = polkit prompt dismissed).
The old `set-gpu-nvidia` verb (unload/reload the NVIDIA driver) was
removed: unloading a driver is not a GPU mode, and no UI path uses it
any more. The app only *reads* `/proc/modules` to show "Loaded".

## Client-side defense in depth (`src/control/ctl.cpp`)

Before ever calling the helper, the UI layer rejects out-of-range values
(charge ≠ {60,80,100}, mode out of range, kbd ∉ 0–3 → no-op). Arguments
travel as an **argv array** (`GSubprocessLauncher.spawnv`), so even a
hypothetical shell metacharacter could not be interpreted.

## polkit action

`data/io.github.asuscontrolcenter.AsusControlCenter.policy` authorizes
`io.github.asuscontrolcenter.AsusControlCenter.*` for the **active local
session** only (auth_admin = your password, first change per session).
It grants nothing else — no other user, no other machine, no inactive
sessions. Never weakened during redesign.

## Safety rules honored by the code

1. **No writes on refresh.** The 2 s tick only pushes data into widgets.
   Switch/radio/slider handlers guard `syncing` state during programmatic
   updates so a refresh cannot reproduce the user's last action.
2. **Slider drag-guards.** After a user drag, refresh skips rewriting
   that slider for 3 s (`oled_touch` on Home, `s_dsp_touch` on Display) —
   prevents accidental re-sends of the setting.
3. **MUX never automatic.** Startup and refresh only *read*
   `gpu_mux_mode` (and asusd's queue). Switching requires an explicit
   click; Ultimate additionally asks for confirmation because it rewires
   the internal panel after a restart.
4. **Honest states.** Unreadable/unknown hardware values show "—" and
   clear any control state rather than displaying a plausible fake.
   Driver-loaded state and MUX mode are displayed separately.
5. **Failing probes** never overwrite the last valid reading with a
   misleading zero; unavailable is shown as "—".

## Build hygiene

`make` builds everything with `-Wall -Wextra` → **0 warnings** (GCC 16).
- Owned strings are freed (`dispose` implementations added for the Cairo
widgets); leaked `g_strdup_printf` temporaries replaced with stack
buffers in `ctl.cpp`.

## Helper exit codes

| code | meaning |
|---|---|
| 0 | value written and read back unchanged |
| 1 | action or device not available (no such attribute) |
| 2 | value outside the whitelist |
| 3 | NVIDIA modules still held by running processes |
| 4 | **firmware refused the value** (attribute exists but the EC rejected it) |

Code 4 matters on models that expose `gpu_mux_mode` without a real MUX: the app
reports the refusal instead of pretending the switch happened.

## GPU MUX: request, queue, verify

`gpu_mux_mode` is a **firmware switch for which GPU drives the panel**, not a
way to disable the dGPU. On the reference N6506CU the EC refuses an in-session
write (`0x0` from WMI → `-EIO`), so asusd deliberately *queues* the value and
`asus-shutdown.service` applies it during shutdown after the NVIDIA stack goes
idle. The app therefore:

- writes through the helper first (same value MyASUS writes) and accepts the
  change only when the helper read the new value back;
- if the firmware refuses **and** the machine is on AC, asks asusd to queue the
  value and then confirms `QueuedGpuValue` — never claiming the live mode
  changed;
- never queues on battery (the EC refuses it there too);
- reads `asus-shutdown.service` state and warns that the queue is lost if the
  unit is disabled;
- keeps the radio on the **live** firmware value and shows the queued value in
  a separate notice.

Telemetry is read-only: `nvidia-smi` runs with a fixed argv through
`GSubprocess` (no shell, no `popen`), and the result is cached for 2 s.
0 % utilisation or `display_active: Disabled` is shown as such and never
interpreted as "dGPU disabled". A missing `dgpu_disable` attribute is reported
as unavailable — the app does not invent one.
