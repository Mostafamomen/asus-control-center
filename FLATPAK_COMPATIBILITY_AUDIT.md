# ASUSCraft — Flatpak compatibility audit

**Project:** `/home/mostafa/Documents/asus-control-center-1.2.2`
**Date:** 2026-11-11
**Phase:** audit only — **no file was modified, no manifest was created, nothing was installed or built**

---

## 1. Compatibility verdict

**ASUSCraft can be distributed as a Flatpak, but not as a single self-contained
Flatpak.** The GUI, the read-only hardware probes and the app's own
configuration work inside the sandbox; **every command the app runs and every
privileged write it performs needs a small host-side service**.

That split is not a compromise forced by the sandbox — it is already the
project's architecture. The app never writes to `/sys` itself: it asks
`/usr/libexec/asus-control-center/acc-helper`, a whitelist-only polkit helper,
and the AGENTS rule says *"The GUI never runs as root and never writes to
sysfs itself"*. A Flatpak simply makes the second half of that sentence
mandatory: `pkexec` is not in the runtime ✗, so the helper has to be invoked
from the host.

| Layer | Runs where | Verdict |
|---|---|---|
| The GTK4 / libadwaita GUI | inside the Flatpak | **works** |
| The read-only `/sys` and `/proc` probes | inside the Flatpak | **works** — verified below |
| The app's own configuration (`~/.config/asus-control-center`) | inside the Flatpak | **works** (the sandbox's own home) |
| The GSettings values (dconf) | host dconf, reached over the session bus | **works**, if the schemas are shipped in the Flatpak |
| The external commands (`iw`, `nvidia-smi`, `systemctl`, `fwupdmgr`, `xinput`, `bluetoothctl`, `dnf`, `journalctl`, `gnome-extensions`, `kwriteconfig5`) | **host** | **needs a host agent** |
| The privileged writes (charge limit, GPU MUX, charge mode, keyboard light) | **host**, through the existing polkit helper | **needs a host agent** |

### 1.1 The two empirical facts the design rests on

Both were measured on this machine, not assumed.

**A Flatpak sees the host's `/sys` and `/proc`, read-only, with no filesystem
permission at all.** Measured inside an installed Flatpak with no
`--filesystem` flag:

```
/proc/sys/kernel/random/boot_id               b2c56200-0008-4c54-a1f5-9fabee24462c
/proc/cpuinfo                                  processor : 0
/proc/modules                                  uinput 28672 0 - Live 0x0000000000000000
/sys/class/dmi/id/product_name                 ASUS Vivobook Pro 15 N6506CU_N6506CU
/sys/class/backlight/intel_backlight/max_brightness   400
/sys/devices/platform/asus-nb-wmi/gpu_mux_mode  1
/sys/class/power_supply                       AC0 BAT0 ucsi-source-psy-USBC000:001
/sys/class/firmware-attributes/asus-armoury/attributes  charge_mode gpu_mux_mode pending_reboot
/sys/class/hwmon                              hwmon0 hwmon1 hwmon2
/sys/class/rfkill                             rfkill0 rfkill1
/sys/class/drm                                card0 card0-eDP-2 card0-HDMI-A-1
/sys/class/net                                enp46s0 lo wlo1
/sys/class/thermal                            cooling_device0 cooling_device1 …
/sys/class/bluetooth                         hci0
/sys/bus/pci/devices                          0000:00:00.0 0000:00:01.0 …
/sys/class/leds                               asus::kbd_backlight enp46s0-0::lan …
/sys/class/block                              nvme0n1 nvme0n1p1 nvme0n1p2
```

**`/sys` is read-only inside the Flatpak** — exactly as it must be:

```
$ echo 0 > /sys/class/backlight/intel_backlight/brightness
/bin/sh: line 1: …/brightness: Read-only file system
```

So the *reads* need nothing, and the *writes* need the host helper — which is
the project's existing design ✓.

### 1.2 What the GNOME 51 runtime does and does not provide

Measured inside `org.gnome.Platform//51` (installed on this machine ✓):

| Present | Missing |
|---|---|
| `libgtk-4.so.1` ✓, `libadwaita-1.so.0` ✓ | `gnome-extensions` ✗ |
| `glib-compile-schemas` ✓ | `pkexec` ✗ |
| `xdg-open` ✓ | `xinput` ✗, `kwriteconfig5` ✗, `iw` ✗, `nvidia-smi` ✗ |
| `timeout` ✓ | `systemctl` ✗, `journalctl` ✗, `fwupdmgr` ✗, `dnf` ✗ |
| | `flatpak` ✗, `lsmod` ✗, `bluetoothctl` ✗ |

**Conclusion:** the GUI and the probes are fine; the *commands* are not. A host
agent is required — and it is required by the sandbox, not by a choice.

---

## 2. Dependency inventory

### 2.1 Build and runtime

| | |
|---|---|
| Language | C++20 (`-std=gnu++20`) |
| Libraries | `gtk4` (4.24.1 on the host), `libadwaita-1` (1.10.0 on the host) |
| Build system | a plain `Makefile` with one `g++` rule; no Meson, no CMake |
| Entry point | `src/main.cpp` → `g_application_run()` with `G_APPLICATION_NON_UNIQUE` |
| Application ID | `io.github.asuscontrolcenter.AsusControlCenter` (already the Flatpak-shaped id ✓) |
| Install paths | `/usr/bin/asus-control-center`, `/usr/libexec/asus-control-center/acc-helper`, `/usr/share/polkit-1/actions/…`, `/usr/share/asus-control-center/` (the themes, the images and the bundled Soft Brightness Plus) |
| Data | `src/ui/theme.css`, `src/ui/theme-dark.css`, `assets/*.png`, `data/soft-brightness-plus/` (a whole third-party extension) |

**Runtime choice:** `org.gnome.Platform//46` (GNOME 46 = GTK 4.18, libadwaita
1.6) is the oldest that satisfies `libadwaita ≥ 1.5`, which is what the DEB
build already targets. `//45` would also do; `//47`+ is safer for a
libadwaita that matches the host. The local machine already has
`org.gnome.Platform//50` and `//51` ✓, so a build against `//46` still installs
on it ✓ (Flatpak keeps the runtime in use).

### 2.2 The external commands the app runs (all of them)

| Command | From | Purpose | In the runtime? |
|---|---|---|---|
| `/usr/bin/journalctl`, `/usr/bin/timeout` | `src/control/muxreq.cpp:124` | read the MUX record from the journal | ✗ |
| `kwriteconfig5` / `kwriteconfig` | `src/control/nightlight.cpp:102,129` | read and write the KWin night-colour setting | ✗ |
| `/usr/bin/glib-compile-schemas` | `src/control/softdim.cpp:205` | compile the Soft Brightness Plus schema | ✓ |
| `/usr/bin/gnome-extensions` | `src/control/softdim.cpp:268` | enable Soft Brightness Plus | ✗ |
| `xinput` | `src/control/touchpadctl.cpp:61,108,155` | read and toggle the touchpad | ✗ |
| `/usr/bin/nvidia-smi` | `src/probe/gpu.cpp:124` | the NVIDIA telemetry | ✗ |
| `/usr/bin/pkexec` | `src/ui/login.cpp:237` | check that polkit works | ✗ |
| `sh -c "gnome-software || …"` | `src/ui/softcenter.cpp:413` | open the system software centre | ✗ (a portal can replace it ✓) |
| `systemctl is-active …` | `src/ui/sysdiag.cpp:1194`, `src/ui/systest.cpp:1247,1257` | is `asusd` / `supergfxd` running? | ✗ |
| `timeout 30 dnf check-update` | `src/ui/sysdiag.cpp:1265`, `src/ui/systest.cpp:1347` | are there updates? | ✗ |
| `fwupdmgr get-updates` | `src/ui/sysdiag.cpp:1284`, `src/ui/systest.cpp:1329` | firmware updates | ✗ |
| `flatpak remote-ls --updates` | `src/ui/sysdiag.cpp:1307` | Flatpak updates | ✗ |
| `xdg-open` | `src/ui/sysmgr.cpp:579` | open a URL | ✓ |
| `bluetoothctl list` | `src/ui/systest.cpp:1222` | is there a Bluetooth adapter? | ✗ |
| `lsmod` | `src/ui/systest.cpp:1267,1309` | which kernel modules are loaded | ✗ |
| `iw list` | `src/ui/pages.cpp:149` | the Wi-Fi SSID | ✗ |
| `pkexec /usr/libexec/asus-control-center/acc-helper` | `src/control/ctl.cpp:65` | **every privileged write** | ✗ |
| `sh`, `cat`, `ls` … | the helper itself | the whitelist helper | — (host) |

### 2.3 The files the app reads or writes

| Path | Read/write | Purpose |
|---|---|---|
| `/sys/class/power_supply/*` | read | the battery, the AC adapter |
| `/sys/class/backlight/*` | read | the panel minimum (for the hardware backlight) |
| `/sys/class/firmware-attributes/asus-armoury/attributes/*` | read | the charge mode and the GPU MUX |
| `/sys/devices/platform/asus-nb-wmi/gpu_mux_mode` | read | the live GPU mode |
| `/sys/firmware/acpi/platform_profile*` | read | the platform profile |
| `/sys/class/hwmon/*`, `/sys/class/thermal/*` | read | the temperatures |
| `/sys/class/drm/*`, `/sys/class/net/*`, `/sys/class/block/*`, `/sys/class/bluetooth/*`, `/sys/class/rfkill/*`, `/sys/class/leds/*`, `/sys/bus/pci/devices/*`, `/sys/class/dmi/id/*` | read | the hardware inventory |
| `/proc/cpuinfo`, `/proc/meminfo`, `/proc/stat`, `/proc/loadavg`, `/proc/modules`, `/proc/self/exe`, `/proc/sys/kernel/osrelease`, `/proc/sys/kernel/random/boot_id`, `/proc/bus/input/devices`, `/proc/driver/nvidia/version` | read | the telemetry and the boot identity |
| `/etc/os-release`, `/etc/modprobe.d/supergfxd.conf`, `/etc/xdg/autostart` | read | the OS, the driver blacklist, the autostart entries |
| `~/.local/share/gnome-shell/extensions/soft-brightness-plus@…` | **write** | installing Soft Brightness Plus |
| `~/.local/share/glib-2.0/schemas` | **write** | compiling that schema |
| `~/.config/asus-control-center/*` | read/write | the local account, the language, the MUX record, the history files, the autostart entry |
| `~/.config/autostart/*` | **write** | the autostart entry |
| `/usr/share/asus-control-center/` | read | the bundled Soft Brightness Plus, the themes |
| `/usr/libexec/asus-control-center/acc-helper` | execute | the privileged helper |

**Every `/sys` and `/proc` path the app uses is visible read-only inside the
sandbox ✓** (§1.1). The writes are all in the user's home ✗ — inside the
sandbox that is the *sandbox's* home ✗, not the host's ✗ — plus the two
host-level operations (the GNOME extension directory and `gnome-extensions`).

---

## 3. The D-Bus inventory

| Bus | Name | Interface / path | Used for | In a Flatpak |
|---|---|---|---|---|
| system | `org.freedesktop.login1` | `/org/freedesktop/login1/session/auto` | `SetBrightness` — the panel brightness | `--system-talk-name=org.freedesktop.login1` ✓ |
| system | `xyz.ljones.asus_armoury` | `/xyz/ljones/asus_armoury/gpu_mux_mode` | the GPU MUX | `--system-talk-name=xyz.ljones.asus_armoury` ✓ |
| system | `org.freedesktop.systemd1` | `…/Manager`, `…/Unit` | is `asusd` running? | ✗ needs the host agent |
| session | `org.gnome.shell` | — | enabling Soft Brightness Plus | ✗ needs the host agent |
| session | `org.gnome.settings-daemon.plugins.color` | — | the night-colour temperature | ✗ (or GSettings ✓) |
| session | `org.kde.KWin` | — | the KWin night-light script | ✗ needs the host agent |
| session | `org.freedesktop.DBus` | — | the bus itself | `--talk-name=org.freedesktop.DBus` ✓ |

**No bus is used in a way that requires `--system-talk-name=org.freedesktop.DBus`
(unrestricted system bus) ✓** — every system-bus call names a specific
well-known name ✓, which is the Flatpak-correct way ✓.

The GSettings the app touches: `org.gnome.shell`,
`org.gnome.shell.extensions.soft-brightness-plus`,
`org.gnome.desktop.peripherals.touchpad`,
`org.gnome.settings-daemon.plugins.color` — plus the app's own
`org.gnome.shell.extensions.*`-style schemas. **GSettings works across the
sandbox boundary** (the values live in the host's dconf, reached over the
session bus) ✓, **provided the schema is installed inside the Flatpak** ✓ —
`org.gnome.shell.extensions.soft-brightness-plus` is not in the GNOME runtime
✗ and would have to be shipped with the app ✓.

---

## 4. The Polkit and privileged operations

There is exactly **one** polkit action:

```xml
<action id="io.github.asuscontrolcenter.helper">
  <description>Run the ASUSCraft hardware helper</description>
  <message>ASUSCraft wants to change a hardware setting (charge limit, performance mode, GPU mode, keyboard light)</message>
  <defaults>
    <allow_any>auth_admin</allow_any>
    <allow_inactive>auth_admin</allow_inactive>
    <allow_active>auth_admin_keep</allow_active>
  </defaults>
  <annotate key="org.freedesktop.policykit.exec.path">/usr/libexec/asus-control-center/acc-helper</annotate>
</action>
```

`acc-helper` is a whitelist-only bash helper: it validates the action and the
value against hard-coded lists, discovers the device paths read-only, and
writes only to the whitelisted files. It is reached through
`pkexec /usr/libexec/asus-control-center/acc-helper <action> [value]`
(`src/control/ctl.cpp:65`).

**Inside a Flatpak this cannot work as it stands** ✗: `pkexec` is not in the
runtime ✗, and a Flatpak may not execute a host binary ✗.

The Flatpak-correct replacement is the **D-Bus activated host agent**
(§6.2): the app calls the agent over the session bus, the agent calls
`pkexec`/the helper on the host ✓, and the polkit prompt still appears with
the same action id ✓.

---

## 5. The ASUS hardware integrations

| Integration | Mechanism | Flatpak impact |
|---|---|---|
| **Charge limit / charge mode** | `acc-helper` → `/sys/class/firmware-attributes/asus-armoury/attributes/charge_mode` | **host agent** ✗ |
| **GPU MUX (hybrid / ultimate)** | `acc-helper` → `…/gpu_mux_mode`; the *deferred* switch is applied by the host's `asus-shutdown.service` at shutdown | **host agent** ✗ (the service is already on the host ✓) |
| **The MUX request record** | `~/.config/asus-control-center/mux-request.conf` + the boot id from `/proc/sys/kernel/random/boot_id` + the journal | the record can live in the sandbox ✓, but the **journal** (`journalctl`) is host-only ✗ |
| **The panel brightness** | `org.freedesktop.login1` `SetBrightness` | `--system-talk-name=org.freedesktop.login1` ✓ |
| **The keyboard backlight** | `acc-helper` → `/sys/class/leds/asus::kbd_backlight` | **host agent** ✗ |
| **The touchpad** | `xinput` + `org.gnome.desktop.peripherals.touchpad` | the GSettings part ✓, the `xinput` part ✗ (and `xinput` is X11-only ✗ — it is already broken on this Wayland machine ✓) |
| **The charge policy** | `acc-helper` | **host agent** ✗ |
| **The platform profile (quiet / balanced / boost)** | `acc-helper` → `/sys/firmware/acpi/platform_profile` | **host agent** ✗ |
| **The NVIDIA telemetry** | `nvidia-smi` | **host agent** ✗ |
| **The fan / temperature sensors** | `/sys/class/hwmon`, `/sys/class/thermal` | ✓ works in the sandbox |
| **Night Light** | `kwriteconfig5` on `kwinrc`, or the `org.kde.KWin` D-Bus call | **host agent** ✗ |
| **Eye-care / E-Reading** | `org.gnome.settings-daemon.plugins.color` (a GSettings schema ✓) and the app's own greyscale rendering ✓ | ✓ works (dconf over the session bus) |
| **The local account** | `~/.config/asus-control-center/settings.conf`, PBKDF2 ✓ | ✓ works (the sandbox's own home) |

---

## 6. Soft Brightness Plus

SBP is a **third-party GNOME Shell extension** that ASUSCraft bundles
(`data/soft-brightness-plus/`, GPL-3.0-or-later) and installs into the
user's own extension directory.

What the app does with it (`src/control/softdim.cpp`):

1. copies a fixed file list into
   `~/.local/share/gnome-shell/extensions/soft-brightness-plus@joelkitching.com/`;
2. runs `glib-compile-schemas` on it (✓ the tool **is** in the runtime);
3. writes the extension's own `monitors` GSettings key;
4. adds the UUID to `org.gnome.shell enabled-extensions`;
5. runs `gnome-extensions enable <uuid>` (✗ **not in the runtime**).

**In a Flatpak, steps 1 and 4 write into the *sandbox's* home ✗, not the
host's** ✗, and step 5 needs the host's `gnome-extensions` ✗. The SBP
extension would be installed in the sandbox where GNOME Shell cannot see it ✗ —
the whole mechanism is meaningless there.

**Consequence:** the Soft Brightness Plus integration **cannot live inside the
Flatpak** ✗. It has to move to the host agent ✓ (the agent installs and
enables it; the Flatpak then only talks to the resulting extension over the
session bus ✓, which is already how the brightness is set today —
`org.gnome.shell.extensions.soft-brightness-plus` ✓).

Note that this is the *same* conclusion the removal audit reached for a
different reason: the feature is hardware OLED dimming, and the mechanism that
reaches the hardware is the host's ✗.

---

## 7. The feature-by-feature compatibility table

| Feature | Inside the Flatpak | Needs |
|---|---|---|
| The whole GTK4 / libadwaita UI, both skins | ✅ | — |
| The dashboard, the pages, the rings, the graphs | ✅ | — |
| The local account, the password hashing, the language | ✅ | — |
| The battery, the charger, the health history | ✅ | `/sys` reads ✓ |
| The CPU / RAM / disk / temperatures / load | ✅ | `/proc` and `/sys` reads ✓ |
| The hardware inventory (DMI, PCI, USB, DRM, block, net, leds) | ✅ | `/sys` reads ✓ |
| The resolution and the refresh rate | ✅ | — |
| The panel brightness (the hardware backlight) | ✅ | `--system-talk-name=org.freedesktop.login1` ✓ |
| Eye-care / E-Reading | ✅ | dconf ✓ |
| The Soft Brightness Plus dimming (the hardware OLED dimming) | ❌ | the **host agent** (it installs and enables the extension) |
| The charge limit, the charge policy | ❌ | the **host agent** → `acc-helper` → polkit |
| The GPU MUX switch | ❌ | the **host agent** → `acc-helper` → polkit; the record's journal check needs the host too |
| The keyboard backlight | ❌ | the **host agent** → `acc-helper` → polkit |
| The platform profile (quiet / balanced / boost) | ❌ | the **host agent** → `acc-helper` → polkit |
| Night Light (KDE) | ❌ | the **host agent** |
| The touchpad toggle | ⚠️ | the GSettings part works ✓; the `xinput` part needs the host ✗ (and is X11-only ✗ — already non-functional on this Wayland host) |
| The NVIDIA telemetry | ❌ | the **host agent** |
| The Wi-Fi SSID | ❌ | the **host agent** (`iw`) |
| The Bluetooth adapter test | ❌ | the **host agent** (`bluetoothctl`) |
| The loaded kernel modules | ❌ | the **host agent** (`lsmod`, or `/proc/modules` ✓ which *does* work ✓) |
| The service state (`asusd`, `supergfxd`) | ❌ | the **host agent** (`systemctl`) |
| The update and firmware checks (`dnf`, `fwupdmgr`, `flatpak`) | ❌ | the **host agent** |
| The software-centre button | ⚠️ | replaceable with the **Software portal** ✓ |
| Opening a URL | ✅ | `xdg-open` ✓ (or the OpenURI portal ✓) |
| The diagnostics and the interactive tests | ⚠️ | the read-only parts ✓; the command-based parts need the host agent ✓ |

---

## 8. The operations a portal can replace

| Today | Portal |
|---|---|
| `sh -c "gnome-software || plasma-discover || flatpak run org.gnome.Software"` | **`org.freedesktop.portal.Software`** (`SelectSources` → `Software`) ✓ — this is the canonical replacement ✓ |
| `xdg-open <uri>` | **`org.freedesktop.portal.OpenURI`** ✓ (though `xdg-open` works ✓ already) |
| — | **`org.freedesktop.portal.Device`** for the GPU ✓ (a *better* source for the NVIDIA telemetry than `nvidia-smi` ✓, though it needs the NVIDIA VA-API driver ✗) |

The portals cover **one** button. Everything else needs a host agent ✗.

---

## 9. The operations that need a separately installed host service

A single **host agent** covers all of them:

| The agent provides | It replaces |
|---|---|
| `acc-helper` invocation with the polkit prompt | `pkexec` ✗ |
| `gnome-extensions enable/disable` + the `org.gnome.shell` GSettings | the SBP installation ✗ |
| `journalctl` (the MUX record) | the journal read ✗ |
| `systemctl is-active` | the service checks ✗ |
| `iw`, `nvidia-smi`, `xinput`, `kwriteconfig5`, `bluetoothctl`, `fwupdmgr`, `dnf`, `flatpak remote-ls`, `lsmod` | the command-based probes ✗ |

The agent must be a **system package** (installed by the RPM/DEB, or by the
Flatpak's own install hook) ✗ — it cannot live inside the Flatpak ✗ (it needs
`/usr/bin/pkexec` ✗ and the host's `/sys` write access ✗).

---

## 10. Security risks

| # | Risk | Assessment | Mitigation |
|---|---|---|---|
| S1 | `--device=all` to reach the hardware | **not needed** ✓ — `/sys` is readable in the sandbox ✓ (measured ✓) and the writes go through the helper ✗ | do **not** add `--device=all` ✓ |
| S2 | `--filesystem=host` to reach `/sys` | **not needed** ✓ — `/sys` and `/proc` are visible read-only ✓ (measured ✓) | do **not** add `--filesystem=host` ✓ |
| S3 | `--system-talk-name=org.freedesktop.DBus` (unrestricted system bus) | **not needed** ✓ — only `org.freedesktop.login1` and `xyz.ljones.asus_armoury` are used ✓ | two specific `--system-talk-name` entries ✓ |
| S4 | A Flatpak that runs as root | impossible ✓ — Flatpak has no root ✓ | keep the polkit prompt in the *host* agent ✓ |
| S5 | The agent becomes a privilege-escalation surface | the agent must keep the whitelist ✓ | the agent only ever runs the **existing** `acc-helper` with its existing whitelist ✓ and the existing polkit action id ✓ |
| S6 | Writing to the host's home from the sandbox | the sandbox's home is separate ✓ | the agent does the writes ✓; the Flatpak keeps its own configuration ✓ |
| S7 | Silently disabling a protection to make a feature work | — | **nothing may be weakened**: no `--device=all`, no `--filesystem=host`, no unrestricted bus ✓. The features that need the host get the host agent ✓, or they are reported as unavailable ✓ |

**The security posture is good as it stands** ✓: the app never writes to
`/sys` itself ✓, it never runs as root ✓, and its only privileged path is a
whitelist-only helper behind polkit ✓. A Flatpak **strengthens** that
posture ✓ (the sandbox physically cannot write to `/sys` ✓ — measured ✓).

---

## 11. The proposed architecture

```
┌─────────────────────────── the Flatpak ────────────────────────────┐
│  asus-control-center (GTK4 / libadwaita)                          │
│    • the whole UI, both skins                                     │
│    • the read-only probes:  /sys, /proc   (no permission needed) │
│    • its own configuration:  ~/.config/asus-control-center        │
│    • GSettings reads/writes (dconf over the session bus)           │
│    • the session bus →  io.github.asuscontrolcenter.Agent          │
└────────────────────────────────┬───────────────────────────────────┘
                                 │  session bus (D-Bus, unprivileged)
                                 ▼
┌───────────────────────── the host (a system package) ─────────────┐
│  asuscraft-agent (D-Bus activated, session bus)                   │
│    • privileged commands →  pkexec acc-helper <action> <value>    │
│      (the existing polkit action id, the existing whitelist)      │
│    • gnome-extensions enable/disable + org.gnome.shell GSettings   │
│    • journalctl, systemctl, iw, nvidia-smi, xinput, kwriteconfig5│
│      fwupdmgr, dnf, flatpak remote-ls, lsmod, bluetoothctl         │
│    • installs Soft Brightness Plus into the host's extension dir   │
└───────────────────────────────────────────────────────────────────┘
```

**The agent is a thin, D-Bus-activated, unprivileged service on the host**
✓. It holds **no** privileges of its own ✓: every privileged operation goes
through the *existing* `acc-helper` and the *existing* polkit action ✓, so the
authentication prompt and the whitelist are unchanged ✓. The agent only needs
the ability to run `pkexec` (which any user may do ✓ — polkit decides ✓) and
to read the host's system information ✓.

**The Flatpak side needs no privilege at all** ✓ except the two specific
system-bus names ✓ and the session bus ✓.

---

## 12. The proposed manifest structure

A sketch for the review — **not** a manifest file ✓ (none was created ✓).

```yaml
app-id: io.github.asuscontrolcenter.AsusControlCenter
runtime: org.gnome.Platform          # //46 or newer
sdk: org.gnome.Sdk                  # //46 or newer
command: asus-control-center

finish-args:
  # the two system-bus names the app really uses — nothing unrestricted
  - --system-talk-name=org.freedesktop.login1
  - --system-talk-name=xyz.ljones.asus_armoury
  # the host agent, on the session bus
  - --talk-name=io.github.asuscontrolcenter.Agent
  # the Soft Brightness Plus extension, over the session bus
  - --talk-name=org.gnome.shell.extensions.soft-brightness-plus
  # the settings the app reads
  - --talk-name=ca.desrt.dconf
  # NO --device=all, NO --filesystem=host, NO --system-talk-name=org.freedesktop.DBus
```

Modules:

| Module | Contents |
|---|---|
| `asuscraft` | the binary, the desktop file, the icons, the AppStream metainfo, the man page, `theme.css`, `theme-dark.css`, `assets/*.png` |
| `softdim` | the bundled Soft Brightness Plus tree **and** its GSettings schema — the schema has to be inside the Flatpak so that the GSettings writes land in the host's dconf ✓ (the values are shared through dconf ✓ even though the schema is the Flatpak's own ✓) |
| `agent` | the host-side agent, built from `data/acc-agent/` (a new small service) — **installed as a system package**, not as a Flatpak module ✓ |

The desktop entry's `Exec=@BIN@/asus-control-center` becomes `Exec=asus-control-center` ✓ (the binary is on the PATH inside the sandbox ✓), and the metainfo's `<launchable type="desktop-id">` stays ✓.

---

## 13. The implementation phases

| Phase | Work | Risk |
|---|---|---|
| **0** | decide the agent's interface (the D-Bus API) and where it is packaged | low |
| **1** | write the host agent: the D-Bus service, the command wrappers, the `acc-helper` invocation | medium — it is the trust boundary |
| **2** | make the app talk to the agent instead of spawning the commands — a thin "host command" abstraction behind the existing `g_spawn_*` call sites ✓ | medium |
| **3** | move the Soft Brightness Plus installation and the `gnome-extensions` calls into the agent ✓ | medium |
| **4** | the manifest, the build files, the metainfo and the desktop entry | low |
| **5** | the tests: the sandbox behaviour, the agent's unit tests, the end-to-end checks | medium |
| **6** | the packaging: the RPM/DEB gains the agent; the Flatpak is built and tested | medium |

The order matters ✓: the app must keep working **without** the agent ✓ (a
native install ✓) — so the agent is an *alternative* backend ✗, not a
replacement ✗, until the Flatpak is the only distribution ✓.

---

## 14. The testing strategy

| Layer | How it is tested |
|---|---|
| the pure logic | `./asus-control-center --check` ✓ (unchanged ✓) |
| the hardware probes | run inside the sandbox and compare the readings with a native run ✓ — the same numbers must come out ✓ |
| the agent | its own unit tests for the command mapping ✓ and the whitelist ✓ |
| the privilege path | the polkit prompt must still appear, with the same action id ✓, and the helper's whitelist must still reject a bad value ✓ |
| the sandbox | a build with `--filesystem=host` **absent** ✓ and `--device=all` **absent** ✓ — a test that fails if either appears ✓ |
| the end-to-end | install the Flatpak, run every page, and check that each feature either works ✓ or says clearly that it needs the host ✓ |
| the regression | the existing `--self-test` ✓ plus the display rows ✓ |

---

## 15. The open questions for the approver

1. **The agent's packaging.** It has to be a **system package** ✓. On Fedora
   that means the RPM ✓ (and the Flatpak would depend on it ✓ — a
   Flatpak depending on a host package is legal ✓ but it means the Flatpak is
   *not* self-contained ✓). Is that acceptable ✓, or should the Flatpak ship
   only the features that need no host ✓ and the agent be a separate
   download ✓?
2. **The touchpad.** The `xinput` path is X11-only ✗ and already does not
   work on this Wayland machine ✓. Should the agent keep it ✗, or should the
   feature drop the `xinput` path and use only the GSettings ✓?
3. **Night Light.** The KDE path (`kwriteconfig5` ✗) is used today ✓. In a
   Flatpak the GNOME equivalent (the `org.gnome.settings-daemon.plugins.color`
   GSettings ✓) would be the portable path ✓ — should the agent also expose
   the GNOME night-light toggle ✓?

---

**Audit complete. No file was modified, no manifest was created, nothing was
installed or built. Waiting for approval before any implementation.**


---

## 16. Addendum — what the implementation measured (2026-11-11)

The audit above was written before any code existed. The implementation
found four things it did not cover. They are recorded here so the audit and
the implementation do not drift apart.

### 16.1 The Flatpak id must be lower case

A Flatpak application id may not contain capitals. The native id
(`io.github.asuscontrolcenter.AsusControlCenter`) is what the polkit
action, the desktop file, the icon and the dconf path already use, and it
was **not** changed. The Flatpak therefore has its own id,
`io.github.asuscontrolcenter.asuscraft`, with its own desktop file, icon
and AppStream metainfo. Both are shipped side by side.

### 16.2 `/sys/firmware` is not in the sandbox

`/sys/class`, `/sys/devices` and `/sys/bus` are visible read-only inside a
Flatpak; **`/sys/firmware` is not**. The performance profile
(`/sys/firmware/acpi/platform_profile`) therefore cannot be *read* in the
sandbox. It can still be *changed*, through the helper on the host.

### 16.3 A sandbox's dconf does not reliably reach the host

dconf keeps a shared-memory snapshot in `$XDG_RUNTIME_DIR/dconf`. A
sandbox normally cannot see it, and the client then falls back to an empty
private database: the settings would be written into the sandbox and never
reach the desktop. A second, unrelated Flatpak on the same machine
behaved the same way, so this is a property of the platform setup, not of
this app.

Consequences:

- the manifest asks for `--filesystem=xdg-run/dconf` (one directory inside
  the runtime dir, owned by the user);
- and the two settings that *must* reach the host — the OLED dimming
  value and the touchpad switch — are done by the **host agent**, which
  writes them in the user's own session.

### 16.4 The agent needs four methods more than the audit listed

Besides the commands and the hardware writes, the agent also provides
`BrightnessGet`/`BrightnessSet` and `TouchpadGet`/`TouchpadSet`: the two
settings that belong to the desktop session (see 16.3).

### 16.5 The manifest's permissions, as built

```
[Context]
filesystems=xdg-run/dconf;

[Session Bus Policy]
io.github.asuscontrolcenter.Agent=talk
org.freedesktop.portal.Desktop=talk
ca.desrt.dconf=talk
org.gnome.shell.extensions.soft-brightness-plus=talk

[System Bus Policy]
org.freedesktop.login1=talk
xyz.ljones.Asusd=talk
```

Note `xyz.ljones.Asusd`: the audit wrote `xyz.ljones.asus_armoury`, which
is the *object* on the bus, not the service name. The implementation uses
the name the code actually connects to.

`org.freedesktop.systemd1` is deliberately **not** granted: the systemd
bus can start units, and a sandbox has no business doing that. The two
questions the app asked it (is `asus-shutdown` enabled, is `supergfxd`
running) are asked of the agent instead, which answers them with
`systemctl`.
