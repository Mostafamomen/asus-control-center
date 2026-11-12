# ASUSCraft 1.2.5

*Battery, performance, GPU and display controls for ASUS laptops — now also
as a Flatpak, with a separate host agent for the hardware the sandbox cannot
reach.*

---

## Highlights

* **A Flatpak build** — the whole interface, the read-only hardware readings
  and the app's own settings, in a sandbox that asks for six named D-Bus
  services and nothing else.
* **A separate host agent** (`asuscraft-agent`) — a thin, unprivileged,
  D-Bus-activated service that lets the sandboxed app reach the laptop's
  firmware and your desktop session. It holds **no** privilege of its own.
* **GNOME 51 and KDE Plasma 6.8** — the night light and the touchpad now use
  the desktop's own supported settings.
* **The polkit path is unchanged** — the same helper, the same action id,
  the same whitelist.

---

## The architecture in one picture

```
┌─────────────────── the Flatpak (io.github.asuscontrolcenter.asuscraft) ──┐
│  the interface · the read-only /sys and /proc probes · your settings     │
│  no root · no host filesystem · no device access                         │
└───────────────────────────────┬──────────────────────────────────────────┘
                                │  the session bus
                                ▼
┌─────────────────── the host (asuscraft-agent, its own package) ──────────┐
│  the privileged writes → pkexec → acc-helper → the firmware             │
│  the host commands   → systemctl, dnf, fwupdmgr, iw, nvidia-smi, …      │
│  the desktop session → the Soft Brightness Plus extension, the night    │
│                         light, the touchpad switch                      │
└─────────────────────────────────────────────────────────────────────────┘
```

**The agent is optional.** The Flatpak is fully usable without it — the
controls that need the machine itself simply say so, with the reason.

---

## What is new in 1.2.5

### The Flatpak and the host agent

* The Flatpak manifest lives at
  `flatpak/io.github.asuscontrolcenter.asuscraft.yml` ✓; it asks for **four
  session-bus names, two system-bus names and one directory** (`xdg-run/dconf` ✓)
  — **no `--filesystem=host`**, **no `--device=all`**, **no unrestricted
  system bus** ✓.
* The host agent is a **separate package** (`asuscraft-agent` ✓). It is
  activated by the session bus on demand ✓, runs as your own user ✓,
  refuses to run as root ✓, and exits five minutes after its last call ✓.
* It runs the commands from a **fixed table** ✓ (`src/control/commands.cpp` ✗
  — the app and the agent share it ✓, so they can never disagree ✗). Every
  argument is validated against a pattern or a fixed list ✓ — a command id
  is never an arbitrary command line ✓.

### The night light (GNOME)

* The night light now **reads and writes** the GNOME colour schema ✓:
  on/off ✓, the colour temperature (1700–4700 K ✓) and the schedule ✓.
* It writes **one key at a time** ✓ — the rest of your colour settings are
  never touched ✓.
* The KDE path (`kwinrc` + a KWin reload ✓) is preserved ✓.
* The desktop is detected automatically ✓.

### The touchpad

* The touchpad now uses the desktop's **own** setting ✓ (the GNOME
  periphls schema ✓).
* The old `xinput` path is **gone** ✓ — it only ever saw X11 devices ✗, so
  it did nothing on a Wayland session ✗.
* Where the desktop exposes no usable switch ✗, the control says so ✓
  instead of pretending to control the hardware ✓.

### The Soft Dimmer removal

* The **Soft Dimmer software-dimming integration is removed** ✗ — the
  software dimming engine, its D-Bus service, its page and the bundled
  GNOME extension ✓ are gone ✗.
* **Soft Brightness Plus is kept** ✓ — the hardware OLED dimming below the
  panel's lowest backlight level ✓, bundled and installed by the app ✓ (or
  by the host agent ✓ in the Flatpak build ✓).
* The dimming engine's own D-Bus service is **not** reintroduced ✓.

### Two crash fixes

* A double free in the System Diagnosis SMART check aborted the application
  when it started ✗ — fixed ✓.
* A double free in the night light's error path aborted the Flatpak build
  when the agent refused a write ✗ — fixed ✓.

### The version policy

* The application version is now **1.2.5 everywhere** ✓: the Makefile ✓,
  both RPM specifications ✓, `debian/changelog` ✓ and the AppStream metadata
  ✓. (Earlier releases carried `10.2` in the packages while the Makefile
  said something else ✗ — that inconsistency is gone ✓.)
* The host agent's **package** version follows the release ✓; its **D-Bus
  interface** version (1.0 ✓) stays independent ✓ — it describes the
  protocol ✗, not the release ✓.

---

## Installation

### The Flatpak (the recommended way on Fedora)

```sh
flatpak install --user ./io.github.asuscontrolcenter.asuscraft_1.2.5.flatpak
```

or, from the source tree:

```sh
flatpak-builder --user --install build \
    flatpak/io.github.asuscontrolcenter.asuscraft.yml
```

### The host agent (optional, recommended)

```sh
sudo dnf install ./asuscraft-agent-1.2.5-1.fc45.x86_64.rpm
```

It installs `/usr/libexec/asuscraft-agent/asuscraft-agent` ✓ and the D-Bus
activation file ✓. It needs no configuration ✓ and starts on demand ✓.

### The native packages

```sh
sudo dnf install ./asus-control-center-1.2.5-1.fc45.x86_64.rpm
# or, on Debian/Ubuntu:
sudo apt install ./asus-control-center_1.2.5_amd64.deb
```

The native package installs the whitelist-only helper and the polkit policy ✓.

### Verifying the download

```sh
sha256sum -c SHA256SUMS.txt
```

---

## What still needs the host

| | |
|---|---|
| the charge limit, the charging policy, the performance profile, the keyboard light, the GPU mode | the polkit helper on the host ✓ — through the agent in the Flatpak ✓ |
| the host readings (the service states, the updates, the Wi-Fi SSID, the SMART health, the NVIDIA telemetry) | the host's tools ✓ — through the agent ✓ |
| installing and enabling Soft Brightness Plus | the desktop session ✓ — through the agent ✓ |
| the GNOME night light | the colour schema ✓ — through the agent ✓ |

**Without the agent** the Flatpak still shows every reading, the brightness,
the eye-care mode, the touchpad, the diagnostics and the local account ✓ —
and it says exactly which control needs the host ✓.

---

## Known limitations

* **The KDE night-light path has not been tested** ✗ — it was written for
  Plasma 6.8 ✓ and the commands are in the shared table ✓, but the release
  machine runs GNOME ✗ and the KDE path was **not executed** ✗.
* **The camera and microphone tests are limited by the sandbox** ✗ — they
  need `/dev/video0` and `/dev/snd` ✗, which a Flatpak deliberately does not
  get ✓.
* **In the Flatpak the performance profile cannot be read** ✗ — `/sys/firmware`
  is not visible in a sandbox ✗. The app says so ✓ ("platform_profile not
  exposed") ✓ and the profile can still be *changed* ✓ through the helper ✓.
* In the Flatpak the "disk (/)" row reports the sandbox's own filesystem ✓.
* A sandbox's dconf does not always reach the host's database ✓ — the
  manifest asks for the one runtime directory that fixes it ✓, and the two
  settings that must reach the host (the OLED dimming value ✓ and the
  touchpad switch ✓) are done by the **host agent** ✓.
* The packages are not signed ✓ — `SHA256SUMS.txt` is published with them ✓.

---

## Build and test results

| | |
|---|---|
| the application | builds with **0 warnings** ✓ (`-Wall -Wextra -Werror` ✓) |
| the logic gate | `./asus-control-center --check` — **31 assertions, 0 failures** ✓ |
| the read-only report | `./asus-control-center --self-test` — exit 0 ✓ |
| the host agent | builds with **0 warnings** ✓; `./agent/asuscraft-agent --check` — **53 assertions, 0 failures** ✓ |
| the Flatpak | builds ✓, installs ✓, and its gates pass **inside the sandbox** ✓ |
| the DEB | builds on Ubuntu 24.04 ✓, installs ✓, and its self-test passes ✓ |
| the polkit path | verified end to end ✓ — the privileged write succeeds ✓ and a value outside the whitelist is refused ✓ |
| the sandbox's permissions | verified against the built Flatpak's own metadata ✓ |

Every detail, with the exact commands and their outputs, is in
`RELEASE_READINESS_1.2.5.md` ✓ and `FINAL_INTEGRATION_AUDIT_1.2.5.md` ✓.

---

## Thanks

ASUSCraft is a community project ✗. It is not affiliated with or endorsed
by ASUS ✗. The bundled **Soft Brightness Plus** extension is
GPL-3.0-or-later ✓ and ships unmodified ✓.
