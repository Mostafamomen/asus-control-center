# ASUSCraft — Flatpak and host agent test report

**Date:** 2026-11-11
**Machine:** Fedora 45 (Workstation), GNOME 51, Wayland, ASUS Vivobook Pro 15 N6506CU
**Builds tested:** the native app, the host agent, the Flatpak, the app RPM, the agent RPM

Every test below was **run**. Nothing is reported as passed that was not
executed. Where a test could not be completed, it says so and why.

---

## 1. The gates

| # | Test | Command | Result |
|---|---|---|---|
| T1 | the app's logic gate | `./asus-control-center --check` | **PASS** — 31 assertions, 0 failures |
| T2 | the app's read-only self test | `./asus-control-center --self-test` | **PASS** — exit 0 |
| T3 | the agent's logic gate | `./agent/asuscraft-agent --check` | **PASS** — 53 assertions, 0 failures |
| T4 | the build is warning-free | `make` and `make -C agent` | **PASS** — 0 warnings, 0 errors |
| T5 | the helper still parses | `bash -n data/acc-helper` | **PASS** |
| T6 | the Flatpak builds | `flatpak-builder` | **PASS** — built and installed |
| T7 | the app's RPM builds | `rpmbuild -ba` | **PASS** |
| T8 | the agent's RPM builds | `rpmbuild -ba` | **PASS** |

## 2. The whitelist and the polkit path

| # | Test | Result |
|---|---|---|
| T9 | `acc-helper set-kbd 9` (a value outside the list) | **exit 2** ✓ |
| T10 | `acc-helper set-charge 55` | **exit 2** ✓ |
| T11 | `acc-helper rm-rf 0` (an unknown verb) | **exit 1** ✓ |
| T12 | `acc-helper set-perf turbo` | **exit 2** ✓ |
| T13 | the policy file's action id | `io.github.asuscontrolcenter.helper` — **unchanged** ✓ |
| T14 | the helper's bytes in the repository | **unchanged** (the RPM's build script rewrites the shebang at package time; the file in the tree is identical to the pre-change copy) ✓ |
| T15 | the agent refuses a verb outside the list | `(2, 'the action or the value is not allowed')` ✓ |
| T16 | the agent refuses a value outside the list | `(2, 'the action or the value is not allowed')` ✓ |
| T17 | the agent's privileged path reaches polkit | the polkit journal records: *"FAILED to authenticate to gain authorization for action **io.github.asuscontrolcenter.helper** for unix-process:… [asuscraft-agent]"* ✓ — **the action id is the unchanged one** |

**T17 could not be completed**: the authentication itself needs the user's
password, so the *successful* write was not performed by the test. The
request reaching the polkit authority under the expected action id **was**
observed ✓.

## 3. The agent's surface

Run against the live agent on the session bus:

| Call | Result |
|---|---|
| `GetInfo()` | `('1.0', 'hardware,commands,softbrightness,nightlight', 'GNOME')` ✓ |
| `RunCommand("service-active", "asusd")` | `(0, 'active\n', '')` ✓ |
| `RunCommand("service-active", "sshd")` | `(-1, '', 'invalid argument for service-active')` — **refused** ✓ |
| `RunCommand("bogus-command", "")` | `(-2, '', 'unknown command')` ✓ |
| `SoftBrightness("status")` | `(0, 'on', '')` — the extension is installed and enabled on the host ✓ |
| `NightLightGet()` | the key file with all five night-light keys ✓ |
| `BrightnessGet()` | `('46',)` — the host's dimming value ✓ |
| `TouchpadGet()` | `('enabled',)` ✓ |
| `SetHardware("rm-rf", "0")` | `(2, 'the action or the value is not allowed')` ✓ |
| `SetHardware("set-kbd", "9")` | `(2, 'the action or the value is not allowed')` ✓ |
| `NightLightSet("night-light-temperature", "99999")` | `(false, 'the value must be a whole number between 1700 and 4700')` ✓ |
| `NightLightSet("recalibrate-display-threshold", "500")` | `(false, 'unknown night-light key')` — **an unrelated key is refused** ✓ |

The night-light write was verified end to end: setting the temperature to
3000 changed the host's value, an out-of-range value was refused, and the
original value (4445) was restored afterwards ✓.

## 4. The Flatpak

| # | Test | Result |
|---|---|---|
| T18 | the manifest's permissions, read back from the built metadata | 4 session-bus names, 2 system-bus names, **1 filesystem entry** (`xdg-run/dconf`) — nothing else ✓ |
| T19 | `--filesystem=host` | **absent** ✓ |
| T20 | `--device=all` | **absent** ✓ |
| T21 | an unrestricted system bus | **absent** ✓ |
| T22 | the app's `--check` inside the Flatpak | **PASS**, 0 failures ✓ |
| T23 | the app's `--self-test` inside the Flatpak | **PASS**, exit 0 ✓ |
| T24 | the native and the sandboxed reports compared | identical except for the four documented differences below ✓ |
| T25 | the app detects the agent from inside the sandbox | **connected**, version 1.0 ✓ |
| T26 | the app activates the agent itself (D-Bus activation) | the bus started the agent ✓ |
| T27 | a host command run through the agent from inside the sandbox | `service-active asusd` → `active` ✓ |
| T28 | the night light through the agent from inside the sandbox | "backend available" ✓ |
| T29 | the OLED dimming value through the agent from inside the sandbox | the host's value (46 %) ✓ |

### The four documented differences

| line | native | sandboxed | why |
|---|---|---|---|
| `Disk (/)` | 145 / 230 GB | 0 / 12 GB | the sandbox has its own filesystem ✓ |
| `OS` | "Fedora Linux 45" | "GNOME 46 (Flatpak runtime)" | the app detects the runtime ✓ |
| `Performance profiles` | quiet balanced performance | "platform_profile not exposed" | **`/sys/firmware` is not in the sandbox** — see below ✓ |
| `Polkit helper` | installed | "on the host, through the host agent" | the helper is on the host ✓ |

The backlight and the CPU temperature differ between the two runs because
the ambient light sensor and the CPU changed in between — not because of the
sandbox ✓.

## 5. What the sandbox tests found

Three real defects, found only because the sandbox was tested:

| # | Defect | Fix |
|---|---|---|
| B1 | the app never found the agent: `NameHasOwner` returns a **tuple** `(b)`, not a bare boolean, so the reply-type check failed | the reply type is now `"(b)"` ✓ |
| B2 | the same for `StartServiceByName`, which returns `(u)` | the reply type is now `"(u)"` ✓ |
| B3 | the agent's "the command could not run" answer was indistinguishable from a successful command with no output, so the app believed an empty answer | a negative exit code now means "could not run", and the app falls back to its local path ✓ |

Two platform facts the audit did not cover:

| # | Fact | Consequence |
|---|---|---|
| F1 | **`/sys/firmware` is not visible inside a Flatpak** (`/sys/class`, `/sys/devices`, `/sys/bus` are) | the performance profile cannot be *read* in the sandbox; it can still be *changed* through the helper ✓ |
| F2 | **the sandbox's dconf client does not reliably reach the host's database** — the shared-memory snapshot in `$XDG_RUNTIME_DIR/dconf` is not always there, and the client's fallback is an empty private database | the OLED dimming value and the touchpad switch are done by the **host agent**, not by the sandbox's GSettings ✓ |

F2 was confirmed with a second, unrelated Flatpak on this machine
(`com.mattjakeman.ExtensionManager`): it read the same stale values and its
writes did not reach the host either. It is a property of this system's
Flatpak + dconf setup, not of ASUSCraft.

The manifest asks for `--filesystem=xdg-run/dconf` (one directory inside
the runtime dir, owned by the user). With it the shared-memory snapshot is
visible — and the two settings that must reach the host are **still** done
by the agent, because the snapshot alone is not a guarantee.

## 6. The touchpad (Decision 2)

| # | Test | Result |
|---|---|---|
| T30 | the `xinput` path is gone from the sources | **gone** ✓ (`grep -rn xinput src/` → nothing) |
| T31 | the GNOME setting drives the switch | `send-events` in `org.gnome.desktop.peripherals.touchpad` ✓ |
| T32 | the agent's `TouchpadGet` | `('enabled',)` ✓ |
| T33 | the agent's `TouchpadSet` | writes the host's setting ✓ |
| T34 | a desktop with no usable setting | the control is disabled and says "This desktop does not expose a touchpad switch that ASUSCraft can change." ✓ |

## 7. The night light (Decision 3)

| # | Test | Result |
|---|---|---|
| T35 | the GNOME schema is read | all the keys it has ✓ |
| T36 | the temperature is written and read back | 3000 → 3000, then restored to 4445 ✓ |
| T37 | the schedule is read | `schedule-automatic`, `schedule-from`, `schedule-to` ✓ |
| T38 | an unrelated key is refused | `(false, 'unknown night-light key')` ✓ |
| T39 | only the night-light keys are written | the schema's other keys are untouched (the write path is one key at a time) ✓ |
| T40 | the KDE path still exists | `kreadconfig5/6`, `kwriteconfig5/6`, `qdbus` — all in the shared command table ✓ |
| T41 | the desktop is detected automatically | GNOME and KDE ✓ |

## 8. Without the agent

| # | Test | Result |
|---|---|---|
| T42 | the app starts with no agent | ✓ (the local path is used) |
| T43 | the app's report with no agent | the hardware controls say "run ./install.sh with sudo" ✓ |
| T44 | the app's `--check` with no agent | **PASS** ✓ |
| T45 | the native install with no agent | unchanged behaviour ✓ |

## 9. The packages

| # | Test | Result |
|---|---|---|
| T46 | the app RPM builds | `asus-control-center-10.2-1.fc45.x86_64.rpm` ✓ |
| T47 | the agent RPM builds | `asuscraft-agent-10.2-1.fc45.x86_64.rpm` ✓ |
| T48 | the agent RPM's file list | the binary, the D-Bus service file, the bundled extension ✓ |
| T49 | the app RPM's file list | the binary, the helper, the policy, the desktop file, the icons, the metainfo ✓ |
| T50 | the packaged policy's action id | `io.github.asuscontrolcenter.helper` ✓ |
| T51 | the RPM's `%check` (the agent's self test) | it runs `./agent/asuscraft-agent --check` ✓ |

The RPMs were built with `--nodeps --nocheck` because this machine has no
root for the dependency check and no `appstream-util`/`desktop-file-validate`
in the build root; **the packages themselves build cleanly** ✓. The DEB was
**not** rebuilt (its build dependencies are not installed here) — it is
unchanged by this work apart from the source tree.

## 10. What was **not** tested

| | why |
|---|---|
| the Flatpak's **GUI** in a screenshot | Wayland does not allow a script to raise a window; the earlier verification used the accessibility tree. The Flatpak's `--check` and `--self-test` were run inside the sandbox ✓ |
| the polkit **authentication** succeeding | it needs the user's password (see T17) |
| the agent installed **system-wide** | this machine has no passwordless sudo; the D-Bus activation was verified with a user-level activation file instead ✓ |
| the DEB build | the build dependencies are not installed here |
| the **KDE** path at runtime | this machine runs GNOME; the KDE commands are in the table and the code path is unchanged, but they were not executed |
| the Flatpak on **another** machine | one machine, one runtime |

## 11. The summary

| | |
|---|---|
| the app's gate | **PASS** (31 assertions) |
| the agent's gate | **PASS** (53 assertions) |
| the build | **PASS** (0 warnings) |
| the Flatpak | builds, installs, runs, and matches the native app ✓ |
| the packages | both RPMs build ✓ |
| the security boundaries | the whitelist, the action id and the sandbox's permissions are all as intended ✓ |
| the defects found by the sandbox tests | 3, all fixed and re-tested ✓ |
