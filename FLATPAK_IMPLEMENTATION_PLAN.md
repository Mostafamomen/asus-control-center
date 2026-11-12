# ASUSCraft — Flatpak + Host Agent implementation plan

**Project:** `/home/mostafa/Documents/asus-control-center-1.2.2`
**Date:** 2026-11-11
**Basis:** `FLATPAK_COMPATIBILITY_AUDIT.md` (approved) + the three approved decisions
**Backup:** `~/.local/share/soft-dimmer/backups/20261111T110048Z-asuscraft-flatpak-agent/`
(40 files, `MANIFEST.sha256` verified)

---

## 0. The approved decisions, restated as engineering rules

| Decision | Rule |
|---|---|
| **1 — Packaging** | Two independent components: the Flatpak GUI and a **separately installed Fedora RPM** host agent. The Flatpak stays usable without the agent. No RPM installation from inside the Flatpak. Reliable agent detection, connection status and clear feature-availability messages. The native RPM keeps working. |
| **2 — Touchpad** | The `xinput` path is **removed**. Use supported desktop settings where available. Detect unsupported operations and say so. Never claim hardware control the DE does not provide. |
| **3 — Night Light** | Add GNOME support through `org.gnome.settings-daemon.plugins.color` (read **and** change). Keep the KDE path where practical. Detect the DE automatically. Never overwrite unrelated user preferences. |

**Invariants that must not change**

- `data/acc-helper` — unchanged, byte for byte.
- The polkit action id `io.github.asuscontrolcenter.helper` — unchanged.
- The polkit policy file — unchanged.
- The helper's whitelist — unchanged (no new verb, no new value).
- No `--device=all`, no `--filesystem=host`, no unrestricted system bus.
- No root inside the Flatpak. No `sudo`/`pkexec`/`setuid` in `src/`.
- The app keeps working with no agent installed.
- Soft Dimmer stays removed.

---

## 1. Measured facts this plan rests on

All measured on this machine, not assumed.

| Fact | Consequence |
|---|---|
| A Flatpak sees the host `/sys` and `/proc` read-only with **no** filesystem permission | The read-only probes need no permission at all |
| `/sys` is read-only inside the sandbox | Every write must go through the host |
| The GNOME 51 runtime has **no** `pkexec`, `gnome-extensions`, `xinput`, `kwriteconfig5`, `nvidia-smi`, `iw`, `systemctl`, `journalctl`, `fwupdmgr`, `dnf`, `flatpak`, `lsmod`, `bluetoothctl` | The commands must run on the host |
| The GNOME 51 runtime **has** `org.gnome.desktop.peripherals` (touchpad) | The touchpad GSettings works in the sandbox |
| The GNOME 51 runtime has **no** `org.gnome.shell`, **no** `org.gnome.settings-daemon.plugins.color`, **no** `org.gnome.shell.extensions.soft-brightness-plus` | Night Light and Soft Brightness Plus need the host (or a shipped schema) |
| `data/soft-brightness-plus/schemas/*.gschema.xml` ships with the project | The Flatpak can install that schema itself so the dimming **value** still reaches the host's dconf |
| `pkexec` triggers the desktop's graphical polkit agent | The host agent can raise the same prompt as today |

**New finding that the audit did not cover:** the Flatpak app-id must be
lower-case, but the project's id (`io.github.asuscontrolcenter.AsusControlCenter`)
contains capitals. The native id, the desktop file name, the icon name, the
dconf path and the polkit action id must all keep working, so **the Flatpak
uses its own lower-case id** `io.github.asuscontrolcenter.asuscraft` with its
own desktop file, icon and metainfo. The native id is untouched.

---

## 2. Component map

```
ASUSCraft Flatpak (io.github.asuscontrolcenter.asuscraft)   [src/, unchanged layout]
  ├─ the GUI, both skins                    → works
  ├─ the read-only probes (/sys, /proc)     → works, no permission
  ├─ its own configuration                  → works (the sandbox's home)
  ├─ GSettings: touchpad                    → works (schema in the runtime)
  ├─ GSettings: the SBP dimming value       → works (schema shipped by us)
  └─ session bus → io.github.asuscontrolcenter.Agent

ASUSCraft Host Agent (asuscraft-agent RPM)  [agent/, new component]
  ├─ D-Bus activated on the SESSION bus, runs as the user, no privileges
  ├─ privileged writes → pkexec acc-helper <action> <value>
  │     (the same helper, the same whitelist, the same polkit action)
  ├─ gnome-extensions enable/disable/list, the org.gnome.shell GSettings
  ├─ the SBP install/enable into the host's extension directory
  ├─ journalctl, systemctl, iw, nvidia-smi, fwupdmgr, dnf, flatpak, lsmod,
  │  bluetoothctl, smartctl, kreadconfig/kwriteconfig/qdbus
  └─ the GNOME night light (the host has the colour schema)
```

### 2.1 The D-Bus interface (the contract both sides implement)

Bus: **session**. Name: `io.github.asuscontrolcenter.Agent`.
Object: `/io/github/asuscontrolcenter/Agent`.
Interface: `io.github.asuscontrolcenter.Agent`.

| Method | In | Out | Purpose |
|---|---|---|---|
| `GetInfo` | — | `s version`, `s capabilities`, `s desktop` | detection + version |
| `RunCommand` | `s command_id`, `s argument` | `i exit_code`, `s stdout`, `s stderr` | the fixed read-only command table |
| `SetHardware` | `s action`, `s value` | `i exit_code`, `s error` | the privileged path (`acc-helper` + polkit) |
| `CheckAdmin` | — | `i exit_code` | the `pkexec /usr/bin/true` admin probe |
| `SoftBrightness` | `s operation` | `i exit_code`, `s result`, `s error` | `status` / `install` / `enable` |

`RunCommand` accepts an **id from a fixed table**, never an argv. The agent
re-validates the id and the argument against the same table the app uses, so
the agent can never be turned into a remote shell.

---

## 3. The shared command table

One source of truth, compiled into **both** binaries:
`src/control/commands.{h,cpp}`.

| id | program (resolved from a fixed list) | argument |
|---|---|---|
| `wifi-ssid` | `iw` | the interface name (validated) |
| `service-active` | `systemctl` | `asusd` / `supergfxd` / `asus-shutdown` |
| `package-updates` | `dnf` | — |
| `firmware-updates` | `fwupdmgr` | — |
| `flatpak-updates` | `flatpak` | — |
| `flatpak-uninstall-user` | `flatpak` | the app id (validated) |
| `loaded-modules` | builtin (`/proc/modules`) | — |
| `bluetooth-adapters` | `bluetoothctl` | — |
| `smart-health` | `smartctl` | a device from a fixed list |
| `nvidia-smi` | `nvidia-smi` | — |
| `journal-mux` | `journalctl` | the boot id (validated) |
| `gnome-extensions-list` | `gnome-extensions` | — |
| `gnome-extension-enable` | `gnome-extensions` | the extension uuid (validated) |
| `gnome-extension-disable` | `gnome-extensions` | the extension uuid (validated) |
| `nightlight-kde-get` | `kreadconfig5/6` | — |
| `nightlight-kde-set` | `kwriteconfig5/6` | `true` / `false` |
| `nightlight-kde-reload` | `qdbus*` | `true` / `false` |
| `nightlight-gnome-get` | builtin (the host GSettings) | — |
| `nightlight-gnome-set` | builtin (the host GSettings) | the key + the value |

`--` never appears as a user-supplied value; every argument is validated
against a pattern or a fixed list before it reaches the argv.

---

## 4. The implementation phases

The audit's seven phases, mapped to concrete work.

### Phase 0 — the interface and the plan
`docs/host-agent.md` (the D-Bus contract), this document, the backup.
**Exit:** the contract is written down and reviewed before any code.

### Phase 1 — the host agent
`agent/` — `main.cpp`, the D-Bus service, the command table, the D-Bus
activation file, its own `--check` self-test.
**Exit:** `agent --check` passes; the agent answers `GetInfo` over the session
bus; the privileged path still shows the polkit prompt with the same action id.

### Phase 2 — the app talks to the agent
`src/control/commands.*`, `src/control/agent.*`, `src/control/hostcmd.*`;
every `g_spawn_*` call site becomes `acc_host_run(...)` with the same local
fallback; `ctl.cpp` becomes agent-aware; the touchpad loses `xinput`; the
night light gains the GNOME settings.
**Exit:** the build is clean, `--check` passes, `--self-test` passes, the
native path is byte-identical in behaviour, and the app works with no agent.

### Phase 3 — Soft Brightness Plus moves to the host
The agent installs and enables the extension; the app asks the agent for the
status and calls the agent to install/enable.
**Exit:** the SBP install works on the host and the dimming value still
reaches the host's dconf.

### Phase 4 — the manifest and the build files
`flatpak/io.github.asuscontrolcenter.asuscraft.yml`, the Makefile's
`install-flatpak` target, the lower-case desktop file, the icon, the metainfo.
**Exit:** the manifest validates; the build produces the expected tree; no
`--filesystem=host`, no `--device=all`, no unrestricted bus.

### Phase 5 — the tests
The agent's `--check`, the app's `--check` (extended), the sandbox checks, the
end-to-end checks.
**Exit:** every test is **run** and its real result is recorded.

### Phase 6 — the packaging
The agent's RPM spec + the build script; the native RPM keeps its content.
**Exit:** the agent RPM builds; the native RPM still builds and installs.

---

## 5. The test matrix

| # | Test | How | Expected |
|---|---|---|---|
| T1 | the app's pure logic | `./asus-control-center --check` | 0 failures |
| T2 | the app's crypto + self-test | `./asus-control-center --self-test` | exit 0 |
| T3 | the agent's pure logic | `agent --check` | 0 failures |
| T4 | the command table | the agent's `--check` | every id builds a valid argv; every bad argument is rejected |
| T5 | the whitelist | `acc-helper` with a bad action/value | exit 1 / exit 2, unchanged |
| T6 | the polkit action id | the policy file | unchanged, `io.github.asuscontrolcenter.helper` |
| T7 | the agent detection | with and without the agent | "connected" / "not installed" |
| T8 | the app without the agent | the agent uninstalled | the app starts, the hardware controls report "the helper is not installed" |
| T9 | the sandbox permissions | the built manifest | no `--filesystem=host`, no `--device=all`, no `--system-talk-name=org.freedesktop.DBus` |
| T10 | the probes in the sandbox | run the app in the Flatpak | the same readings as a native run |
| T11 | the touchpad | the GNOME schema | the switch works; the `xinput` path is gone |
| T12 | the night light | the GNOME colour schema | the toggle and the temperature work; the other keys are untouched |
| T13 | the RPMs | `rpmbuild` | the native RPM and the agent RPM build |
| T14 | the desktop file | `desktop-file-validate` | no warnings |
| T15 | the metainfo | `appstream-util validate-relax` | no errors |

---

## 6. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | the agent becomes a privilege-escalation surface | it holds no privileges; every privileged call is the existing `acc-helper` with its own whitelist, re-validated in the agent |
| R2 | the agent is called with a crafted argument | the argument is validated in the agent against the same table; the argv is fixed |
| R3 | the agent hangs on a polkit dialog | the calls are bounded by a timeout and run on a worker thread |
| R4 | the app breaks for a native user | every agent call has the identical local fallback; the agent is optional everywhere |
| R5 | the agent blocks the bus | the handlers run the work on a thread and reply asynchronously |
| R6 | the sandbox silently loses a feature | the agent state and the per-feature availability are shown in the UI |
| R7 | the Flatpak id clashes with the native id | the Flatpak uses `io.github.asuscontrolcenter.asuscraft`; the native id is untouched |
| R8 | the session-bus service breaks a project invariant | `src/` still owns **no** bus name; the agent is a separate component in `agent/`, and `AGENTS.md` is updated to say so explicitly |

---

## 7. The deliverable list

- `flatpak/io.github.asuscontrolcenter.asuscraft.yml` — the manifest
- `agent/` — the host agent's source, its Makefile, its D-Bus activation file
- `packaging/rpm/asuscraft-agent.spec` — the agent's RPM
- `docs/host-agent.md` — the D-Bus interface documentation
- `docs/polkit.md` — the updated polkit integration documentation
- `docs/flatpak-install.md` — the Flatpak installation instructions
- `docs/host-agent-install.md` — the agent installation instructions
- `docs/flatpak-security-audit.md` — the security audit
- `docs/flatpak-test-report.md` — the complete test report
- `docs/code-guide.md`, `AGENTS.md` — updated for the new components
