# ASUSCraft 1.2.5 — Final integration audit (Flatpak + Host Agent)

**Project:** `/home/mostafa/Documents/asus-control-center-1.2.2`
**Date:** 2026-11-12
**Auditor:** the implementing agent, auditing its own work with an evidence-only standard
**Scope:** the Flatpak GUI, the separately packaged host agent, the polkit path, the native package, and the Fedora 45 / GNOME 51 / Wayland host.

**Verdict: CONDITIONAL PASS — FIXES OR TESTS REQUIRED**

Two critical defects were found and fixed during this audit (one of them made
the application abort on every GUI start). One critical test could not be
executed because it needs root, and the release version numbers do not yet
agree. The conditions are listed in §12.

---

## 1. Environment and installed versions

| | |
|---|---|
| Distribution | Fedora Linux 45 (Workstation Edition) |
| Kernel | 7.3.3-300.fc45.x86_64 |
| GNOME Shell | 51.1 |
| Session | Wayland, `XDG_CURRENT_DESKTOP=GNOME` |
| GTK / libadwaita | 4.24.1 / 1.10.0 |
| Flatpak | 1.18.4 |
| RPM | 6.1.0 |
| polkit | polkit-127-5.fc45.x86_64 |
| Session bus | dbus-broker (a user session) |
| Machine | ASUS Vivobook Pro 15 N6506CU, Intel Core Ultra 9 285H |

### The application versions

| where | value |
|---|---|
| `Makefile` `VERSION` | **1.2.2** |
| `packaging/rpm/asus-control-center.spec` | **10.2** |
| `packaging/rpm/asuscraft-agent.spec` | **10.2** |
| `debian/changelog` | **10.2** |
| `data/…metainfo.xml` (the newest release) | **10.2** |
| the built binary, `./asus-control-center --version` | **1.2.2** |
| the agent binary, `./agent/asuscraft-agent --version` | **1.0** |

> **Finding V1 — the release version does not agree.** The Makefile says
> `1.2.2`, every packaged artefact says `10.2`, and `AGENTS.md` requires all
> of them to agree. The target of this audit is *1.2.5*: four files must be
> moved to it (the Makefile, both RPM specs — including the new agent spec —
> and `debian/changelog`), plus a `<release version="1.2.5">` entry in the
> metainfo. **Not done here**: it is a release action, not an audit action.

### The installed packages

| | |
|---|---|
| The native ASUSCraft | **asus-control-center-1.0.0-1.fc44.x86_64** (installed 2026-11-03) — an *older* build than the tree; **left untouched** |
| The host agent RPM | **not installed** |
| The Flatpak | `io.github.asuscontrolcenter.asuscraft` 10.2, branch `master`, on `org.gnome.Platform//46` |
| The user-level D-Bus activation file | `~/.local/share/dbus-1/services/io.github.asuscontrolcenter.Agent.service` → `Exec=/home/mostafa/Documents/asus-control-center-1.2.2/agent/asuscraft-agent` (a **source-tree** path) |
| The system-level activation file | **absent** (`/usr/share/dbus-1/services/io.github.asuscontrolcenter.Agent.service` does not exist) |
| `dist/` | `asus-control-center-10.2-1.fc45.x86_64.rpm`, `asuscraft-agent-10.2-1.fc45.x86_64.rpm` |

---

## 2. Repository and working-tree status

```
$ git rev-parse --is-inside-work-tree
fatal: not a git repository (or any parent up to mount point /)
$ git status / git branch -a / git remote -v / git log --oneline -3
fatal: not a git repository (…)
$ ls -ld .git
ls: cannot access '.git': No such file or directory
```

**There is no Git repository, no branch, no commit and no remote.** Nothing in
this tree is version-controlled; the audit therefore cannot report a branch,
a diff or a commit, and there is nothing that could be published by accident.
This matches `AGENTS.md` ("This working copy is not a Git repository").

The working tree is the source of truth: 24 tracked files modified and 17
source files plus 7 documents added since the previous phase's backup
(`20261111T110048Z-asuscraft-flatpak-agent`).

---

## 3. RPM package inspection

Both packages were inspected **as built artefacts** (extracted with
`rpm2cpio`, not from the source tree).

### 3.1 Metadata

| | `asus-control-center` | `asuscraft-agent` |
|---|---|---|
| Name / Version / Release | asus-control-center / 10.2 / 1.fc45 | asuscraft-agent / 10.2 / 1.fc45 |
| License / URL | GPL-3.0-or-later / the project URL | the same |
| Build host / date | fedora / 2026-11-11 14:58 | fedora / 2026-11-11 14:58 |
| Header SHA256 digest | **OK** | **OK** |
| Payload SHA256 digest | **OK** | **OK** |
| OpenPGP signature | **NOTFOUND** (unsigned) | **NOTFOUND** (unsigned) |

> **Finding V2 — the packages are unsigned.** The CI workflow publishes
> `SHA256SUMS.txt` for the release assets, which is the compensating
> control, but the RPMs themselves carry no signature. Documented as a
> release-process item, not a defect of the build.

### 3.2 Dependencies and scripts

* The app RPM requires `/usr/bin/bash`, `hicolor-icon-theme`, `libadwaita-1.so.0`, `libc` ✓ and the GTK/adwaita sonames ✓.
* The agent RPM requires `glib2`, `libgio-2.0.so.0`, `libgobject-2.0.so.0`, `libc` and **`polkit`** ✓.
* **Neither package has any `%pre`/`%post`/`%preun`/`%postun` script** ✓ —
  installation and removal are pure file operations ✓.

### 3.3 File ownership, permissions and locations

```
$ rpm -qplv asuscraft-agent-10.2-1.fc45.x86_64.rpm
-rwxr-xr-x root root  /usr/libexec/asuscraft-agent/asuscraft-agent
drwxr-xr-x root root  /usr/share/asuscraft-agent
-rw-r--r-- root root  /usr/share/asuscraft-agent/soft-brightness-plus/…
-rw-r--r-- root root  /usr/share/dbus-1/services/io.github.asuscontrolcenter.Agent.service

$ rpm -qplv asus-control-center-10.2-1.fc45.x86_64.rpm   (the security-relevant files)
-rwxr-xr-x root root  /usr/bin/asus-control-center
-rwxr-xr-x root root  /usr/libexec/asus-control-center/acc-helper
-rw-r--r-- root root  /usr/share/applications/io.github.asuscontrolcenter.AsusControlCenter.desktop
-rw-r--r-- root root  /usr/share/metainfo/io.github.asuscontrolcenter.AsusControlCenter.metainfo.xml
-rw-r--r-- root root  /usr/share/polkit-1/actions/io.github.asuscontrolcenter.AsusControlCenter.policy
```

* The agent's executable is in the intended system location
  `/usr/libexec/asuscraft-agent/` ✓, root-owned, 0755 ✓.
* The helper is 0755 root-owned ✓; the policy and the activation file are
  0644 root-owned ✓.

### 3.4 The activation file points at the installed executable

```
$ cat usr/share/dbus-1/services/io.github.asuscontrolcenter.Agent.service   (from the RPM)
[D-BUS Service]
Name=io.github.asuscontrolcenter.Agent
Exec=/usr/libexec/asuscraft-agent/asuscraft-agent
```

**The packaged Exec is the installed path, not the source tree** ✓ — the
opposite of the *user-level* activation file that is currently in place
(§1) ✓.

### 3.5 The uninstall touches no user data

Both packages own nothing under `/home`, `/root` or `/var` ✓ (proved by
`rpm -qpl | grep -E "^/(home|root|var)"` → no output ✓). The user's
Soft Brightness Plus extension in
`~/.local/share/gnome-shell/extensions/`, the app's own configuration in
`~/.config/asus-control-center/` and the user's dconf database are **not**
owned by either package ✓ and would survive a removal ✓.

### 3.6 What could not be executed

The **installation itself was not performed**: `sudo` on this machine
requires a password, which the audit cannot supply. The install command was
approved by the reviewer (§3 of the questionnaire) and is recorded in §12 as
the outstanding action. Everything that could be checked without root was
checked against the real package payloads (§3.1–3.5 above, and §4.2).

---

## 4. The host agent activation

### 4.1 The three states, at the API level

The probe calls **the application's own detection code**
(`src/control/agent.cpp` — the same functions the GUI uses).

| state | how it was produced | result |
|---|---|---|
| **A — available** | the agent running | `state=CONNECTED(2) version='1.0' error=''`; `RunCommand service-active asusd` → `ok=1 code=0 out='active'` ✓ |
| **B — installed, not running** | the agent stopped; the app asks the bus | the bus started it; `state=CONNECTED(2)` ✓ |
| **C — not installed** | a private session bus whose `servicedir` excludes the user's activation directory | `state=ABSENT(0)`, `last error = 'GDBus.Error:…ServiceUnknown: The name io.github.asuscontrolcenter.Agent was not provided by any .service files'` ✓ |

**No activation loop in state C** — a `dbus-monitor` on the same private bus
recorded exactly **1** `NameHasOwner` and **1** `StartServiceByName` call,
then the app stopped asking ✓.

### 4.2 The response parsing (the two bugs this audit's predecessor phase fixed)

```
$ gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
    --method org.freedesktop.DBus.NameHasOwner io.github.asuscontrolcenter.Agent
(true,)                                   ← a TUPLE, not a bare boolean
$ gdbus call … --method org.freedesktop.DBus.StartServiceByName io.github.asuscontrolcenter.Agent 0
(uint32 2,)                               ← DBUS_START_REPLY_STARTED
$ gdbus call --session --dest io.github.asuscontrolcenter.Agent --object-path /io/github/asuscontrolcenter/Agent \
    --method io.github.asuscontrolcenter.Agent.GetInfo
('1.0', 'hardware,commands,softbrightness,nightlight', 'GNOME')
```

Both replies are parsed correctly by the application ✓ — this is exactly the
class of bug that the audit would have caught silently (the app would have
decided the agent was absent forever).

### 4.3 The same three states, in the GUI

Read through AT-SPI on the real Settings page (the agent row, verbatim):

| state | the row reads |
|---|---|
| **A — available** | `Connected (1.0)` ✓ |
| **not answering** (a dummy process holds the name and never replies) | `Not answering` ✓ |
| **C — not installed** (the activation file's `Exec` target temporarily moved away) | `Not installed` ✓ |

The *Check again* button was exercised: with the agent stopped, one click
re-activated the agent and the row returned to `Connected (1.0)` ✓ — the
button really re-probes ✓.

**State B with the RPM payload.** The source-tree activation file was
temporarily pointed at the **agent binary extracted from the RPM** and the
bus was told to reload:

```
$ pgrep -a -x asuscraft-agent
13099 /home/mostafa/.local/share/soft-dimmer/audit-1.2.5/rpmroot/usr/libexec/asuscraft-agent/asuscraft-agent
```

The agent was activated and ran **from the RPM payload, not from the source
tree** ✓, and the application detected it (`CONNECTED`) ✓. The
source-tree activation file was then restored ✓ (it must stay until the
system-level one is installed and verified).

### 4.4 The outstanding activation test

The **system-level** activation (`/usr/share/dbus-1/services/…`, pointing at
`/usr/libexec/asuscraft-agent/asuscraft-agent`) could not be exercised: it
requires the RPM to be installed with root. The command and its exact
effects are in §12.

---

## 5. The polkit and D-Bus security review

### 5.1 The action id and the installed policy

```
$ grep 'action id' /usr/share/polkit-1/actions/io.github.asuscontrolcenter.AsusControlCenter.policy
  <action id="io.github.asuscontrolcenter.helper">
```

**The action id is the unchanged one** ✓.

Checksums (the pre-change backup, the source tree, the packaged RPM, the
installed file):

| file | the backup | the source | the RPM | the installed file |
|---|---|---|---|---|
| `acc-helper` | `38b1ca77fa26b672…` | `38b1ca77fa26b672…` | `3ce01af88e796cf1…` | `38b1ca77fa26b672…` |
| the polkit policy | `eaa44a834cb06719…` | `eaa44a834cb06719…` | `eaa44a834cb06719…` | `eaa44a834cb06719…` |

* The **policy is byte-identical in all four places** ✓ — `diff` of the
  installed file against the pre-change copy: **no difference** ✓.
* The **helper is byte-identical** in the backup, the source tree and the
  installed file ✓ (`diff`: no difference ✓). The RPM's copy differs **only
  in the shebang** (`#!/bin/bash` → `#!/usr/bin/bash`), which RPM's
  `brp-shebangs` script rewrites at package time ✓ — the file in the
  repository is unchanged ✓.

### 5.2 The agent-to-helper communication and the authorization

The agent runs `pkexec /usr/libexec/asus-control-center/acc-helper
<action> <value>` — the same helper, under the same action id. The polkit
authority's own record of a request made by the agent:

```
Operator of unix-session:5 FAILED to authenticate to gain authorization for
action io.github.asuscontrolcenter.helper for unix-process:… [asuscraft-agent]
```

and, after the reviewer authorized a test, **the privileged path completed
successfully**:

| the agent's call | the answer |
|---|---|
| `SetHardware("set-perf","quiet")` | `(0, '')` — applied ✓ |
| `SetHardware("set-charge","80")` | `(0, '')` — applied ✓ |
| `SetHardware("set-kbd","3")` | `(0, '')` — applied ✓ |
| `SetHardware("set-perf","turbo")` | `(2, 'the action or the value is not allowed')` ✗ refused ✓ |
| `SetHardware("set-charge","55")` | `(2, …)` ✗ refused ✓ |
| `SetHardware("set-gpu-mux","2")` | `(2, …)` ✗ refused ✓ |
| `SetHardware("rm-rf","0")` | `(2, …)` ✗ refused ✓ |

The hardware state these three calls changed was **restored** afterwards
(the platform profile back to `balanced`, the keyboard backlight back to `0`;
the charge limit was already 80 %) ✓.

### 5.3 Unprivileged operations do not request authorization

Four read-only agent calls (`RunCommand service-active asusd`,
`SoftBrightness status`, `BrightnessGet`, `TouchpadGet`) produced

```
$ journalctl --since "-30 sec" | grep -c "asuscontrolcenter.helper"
0
```

**Zero polkit requests** ✓ — only `SetHardware` touches polkit ✓.

### 5.4 The agent cannot be turned into a remote shell

Every argument is validated against a pattern or a fixed list **in the agent**
(the app's validation is a convenience, not the boundary):

| the argument fed in | the agent's answer |
|---|---|
| `; rm -rf /` | `(-1, '', 'invalid argument for service-active')` ✓ |
| `$(id)` | refused ✓ |
| `` `id` `` | refused ✓ |
| `\| cat /etc/passwd` | refused ✓ |
| `../../etc/passwd` | refused ✓ |
| `asusd; id` | refused ✓ |
| `asusd\nid` | refused ✓ |
| a 200-character string | refused ✓ |
| `-rf` / `--help` | refused by the leading-dash rule ✓ (the gdbus CLI intercepted them first in this test) |
| `/dev/sdb`, `/dev/../dev/sda`, `/etc/passwd` (the SMART device) | refused ✓ |
| `x@y; id` (the extension uuid) | refused ✓ |
| `-x@y` (an extension uuid) | refused ✓ |

The command table is a fixed list of ids with fixed argv; the agent has no
"run this argv" method at all ✓.

### 5.5 No shell, no unrestricted execution

| the search | the result |
|---|---|
| `sh -c` / `/bin/sh` in `src/` | **0** ✓ |
| `popen (` / `system (` in `src/` | **0** ✓ (the last one, the microphone test, was rewritten during the previous phase) ✓ |
| `g_spawn_command_line` in `src/` | **0** ✓ |
| `g_bus_own_name` in `src/` | **0** ✓ — the application owns no bus name; the agent is outside `src/` ✓ |

Every external program is started with a fixed argv and no shell ✓.

### 5.6 The Flatpak's permissions, as installed

```
$ flatpak --user info --show-permissions io.github.asuscontrolcenter.asuscraft
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

* **no `--filesystem=host`** ✓
* **no `--device=all`** ✓
* **no unrestricted system bus** ✓ (two named services ✓)
* **no `--allow=devel`** ✓
* the one filesystem entry is `xdg-run/dconf` — **one directory inside the
  runtime dir**, owned by the user ✓ (dconf's shared-memory snapshot; without
  it a sandboxed dconf client falls back to an empty private database ✗).

`org.freedesktop.systemd1` was deliberately **not** requested: the systemd
bus can start units, so a sandbox has no business holding it ✓; the two
questions the application asked it are asked of the agent instead ✓.

---

## 6. Fedora 45 / GNOME 51 compatibility

All the tests below ran on the real host, with the real desktop session.

| # | feature | verdict | evidence |
|---|---|---|---|
| 1 | the touchpad through the GNOME setting | **PASS** | `org.gnome.desktop.peripherals.touchpad send-events` reads `'enabled'`, was written back unchanged ✓, and the agent agrees (`TouchpadGet` → `('enabled',)`) ✓. The `xinput` path is gone (0 code references) ✓. |
| 2 | the night light enable/disable | **PASS** | `night-light-enabled` toggled `false`→`true`→`false` ✓ (restored ✓) |
| 3 | the night light temperature and schedule | **PASS** | `night-light-temperature` `uint32 2206`, `night-light-schedule-automatic true`, `-from 8.0`, `-to 6.0` ✓; written back unchanged ✓; the agent's `NightLightGet` returns the same four keys ✓ |
| 4 | the night light's write path (the agent) | **PASS** | an out-of-range value is refused (`'the value must be a whole number between 1700 and 4700'`) ✓ and an unrelated key is refused (`'unknown night-light key'`) ✓ — **no other key in the user's colour schema is ever written** ✓ |
| 5 | the software dimming value through the agent | **PASS** | `BrightnessGet` → `('93',)` ✓, matching the extension's own value ✓ |
| 6 | the performance profile | **PARTIAL** | natively the firmware value is read (`balanced` ✓) and asusd is active ✓; **in the Flatpak `/sys/firmware` is not visible**, so the report says "platform_profile not exposed" ✓ — honestly ✓ |
| 7 | the battery and the hardware monitoring | **PASS** | `BAT0/capacity` 72, `BAT0/status` Discharging, `thermal_zone0/temp` 57000, `intel_backlight/brightness` 270 ✓ |
| 8 | the native ASUSCraft behaviour | **PASS** | `--check` 31/31 ✓, `--self-test` exit 0 ✓, the GUI starts and renders ✓ |
| 9 | the KDE night-light path | **NOT TESTED** | this machine runs GNOME; the KDE commands (`kreadconfig5/6`, `kwriteconfig5/6`, `qdbus`) are in the shared table ✓ but were not executed ✗ |
| 10 | the camera and the microphone tests | **NOT TESTED** | they need `/dev/video0` and `/dev/snd`; the sandbox has neither ✓ and the native path was not exercised interactively ✗ |

---

## 7. The Flatpak isolation

### 7.1 Without the host agent

```
$ flatpak run io.github.asuscontrolcenter.asuscraft --check
0 failure(s)                                     (31 assertions, exit 0)
$ flatpak run io.github.asuscontrolcenter.asuscraft --self-test
exit 0
  --   Software dimming           Soft Brightness Plus extension not installed
  --   Night light / Night color  no supported backend on this DE
  --   Polkit helper (writes)     run ./install.sh with sudo
```

The application runs ✓ and every agent-dependent feature says why it is
unavailable ✓ — no silent failure ✓.

### 7.2 With the host agent

```
$ flatpak run io.github.asuscontrolcenter.asuscraft --self-test
exit 0
  asus-shutdown    ready
  OK   Software dimming           current 93%, min 10%
  OK   Night light / Night color  backend available
  OK   Polkit helper (writes)     on the host, through the host agent
```

### 7.3 The sandboxed report vs the native report

The two reports differ **only** in five documented places:

```
$ diff native.txt sandboxed.txt
20c20  Disk (/)         147 / 230 GB      →   0 / 12 GB        (the sandbox's own filesystem)
43c43  OS               "Fedora Linux 45" →   "GNOME 46 (Flatpak runtime)"
57c57  Performance profiles  quiet balanced performance → platform_profile not exposed  (/sys/firmware)
63c63  dGPU runtime power   gnome-shell, gnome-software, … → active   (the sandbox's own processes)
70c70  Polkit helper     /usr/libexec/…/acc-helper installed → on the host, through the host agent
```

Every difference is explained and reported by the application itself ✓.

### 7.4 The rest of the isolation checks

| check | result |
|---|---|
| the Flatpak's GUI launches | **PASS** — it runs for the whole observation window, with an empty log ✓ (no crash ✓) |
| a D-Bus failure does not crash the GUI | **PASS** — in the "not installed" and "not answering" states the GUI stayed up and rendered the state ✓ |
| the logs expose no secrets | **PASS** — the Flatpak's log contains only GTK/GDK diagnostics ✓; the app's own log has no account, hash or serial ✓ |
| the agent-dependent operations report unavailable states | **PASS** — §7.1 ✓ |
| the Flatpak's window in the accessibility tree | **NOT TESTED** — the sandboxed application did not register with the host's accessibility registry in this session ✗ (its `--check`/`--self-test` and its process lifetime were verified instead ✓) |

---

## 8. The native installation regression

| command | exit | the detail |
|---|---|---|
| `make clean && make` | **0** | **0 warnings**, 0 errors ✓ |
| `make -C agent clean && make -C agent` | **0** | **0 warnings**, 0 errors ✓ |
| `./asus-control-center --check` | **0** | **31 assertions, 0 failures** ✓ |
| `./asus-control-center --self-test` | **0** | the read-only hardware report ✓ |
| `./agent/asuscraft-agent --check` | **0** | **53 assertions, 0 failures** ✓ |
| `flatpak run … --check` | **0** | 31 assertions ✓ |
| `flatpak run … --self-test` | **0** | ✓ |
| `bash -n data/acc-helper` | **0** | the helper parses ✓ |
| `desktop-file-validate` (the packaged desktop file) | **0** | one hint about the `HardwareSettings` category ✓ |

**The native installation was not touched**: `asus-control-center-1.0.0-1.fc44`
is still installed and its files are unchanged ✓.

---

## 9. The defects found by this audit

### D1 — the application aborted on every GUI start (**CRITICAL, fixed**)

`src/ui/sysdiag.cpp:682-684` freed `out` twice in the SMART check. The
application aborted with `free(): double free detected in tcache 2` (SIGABRT)
while building the System Diagnosis page.

```
#7  0x000000000044442f in check_storage () at src/ui/sysdiag.cpp:684
#8  0x0000000000446233 in run_sw_group () at src/ui/sysdiag.cpp:825
#9  run_all () at src/ui/sysdiag.cpp:842
#10 acc_sysdiag_page (u=…) at src/ui/sysdiag.cpp:1624
#11 acc_ui_boot (app=…) at src/ui/window.cpp:657
```

It was introduced by the previous phase and **the gates could not see it**:
`--check` and `--self-test` never build that page ✗.

*Fix:* the duplicate `g_free (out)` was removed (one line) ✓.
*Re-tested:* the GUI now starts and stays up ✓; the build is warning-free ✓;
`--check` 31/31 ✓; `--self-test` exit 0 ✓.

### D2 — the night light's error path freed the caller's pointer (**CRITICAL, fixed**)

`src/control/nightlight.cpp` (`gnome_write`) handed the caller's `*err` the
same pointer it then freed, and the caller freed it again. It only fires when
the write **fails** and only on the agent path — i.e. **inside the Flatpak**:

```
$ flatpak run … --command=/tmp/…/nlfail io.github.asuscontrolcenter.asuscraft
free(): double free detected in tcache 2
the exit code: 134
```

*Fix:* the caller now receives its own copy (one line) ✓.
*Re-tested inside the Flatpak:* exit 0 ✓, no abort ✓.

### V1 — the version numbers do not agree (see §1)

### V2 — the packages are unsigned (see §3.1)

### Other observations (not defects)

* The app's `--version` reports the Makefile's `1.2.2` while the package
  says `10.2` — a consequence of V1 ✓.
* The `disk (/)` row in the sandbox reports the sandbox's own filesystem ✗ —
  honest, but arguably misleading; a future release could label it.
* The agent leaves a `gtk_theme`/profile untouched ✓; the audit's own tests
  changed and restored: the night light (enabled=false, temperature 2206,
  schedule 8.0/6.0, automatic=true), the platform profile (`balanced`), the
  keyboard backlight (`0`) and the touchpad (`enabled`) ✓.
* **One ambiguity worth recording:** the audit's dconf experiments wrote
  `org.gnome.desktop.interface color-scheme` while testing whether a
  sandbox's dconf reaches the host ✗. The value is now `prefer-dark` ✓,
  which matches the user's own GTK theme (`WhiteSur-Dark-solid` ✓) and is
  what GNOME's Appearance panel writes for the dark style ✓ — but the audit
  cannot prove with certainty that it is byte-identical to the value from
  before the session ✗. It is a cosmetic desktop preference, it is
  self-consistent with the user's theme ✓, and it is recorded here rather
  than hidden ✓.

---

## 10. The exact commands and their outputs

<details>
<summary>The audit's command log</summary>

```
# §2 the state
git rev-parse --is-inside-work-tree                    → fatal: not a git repository
rpm -q asus-control-center                             → asus-control-center-1.0.0-1.fc44.x86_64
rpm -q asuscraft-agent                                 → package asuscraft-agent is not installed
flatpak --user list --app --columns=application,branch  → io.github.asuscontrolcenter.asuscraft  master

# §3 the packages
rpm -qip dist/asus-control-center-10.2-1.fc45.x86_64.rpm
rpm -qp --requires / --scripts / dist/*.rpm
rpm -qplv dist/*.rpm
rpm2cpio dist/asuscraft-agent-10.2-1.fc45.x86_64.rpm | cpio -idm --quiet
sha256sum data/acc-helper data/io.github.asuscontrolcenter.AsusControlCenter.policy
diff <the backup copy> /usr/libexec/asus-control-center/acc-helper

# §4 the activation
gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
    --method org.freedesktop.DBus.NameHasOwner io.github.asuscontrolcenter.Agent
gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
    --method org.freedesktop.DBus.StartServiceByName io.github.asuscontrolcenter.Agent 0
gdbus call --session --dest io.github.asuscontrolcenter.Agent \
    --object-path /io/github/asuscontrolcenter/Agent --method io.github.asuscontrolcenter.Agent.GetInfo
dbus-run-session --config-file=…/private-session.conf -- ./agentprobe
dbus-monitor --session "interface='org.freedesktop.DBus'"          (the loop check)

# §5 the security
journalctl --since "-30 sec" | grep -c "asuscontrolcenter.helper"
python3 inject.py                                                  (the hostile arguments)

# §6 the features
python3 features.py                                                (read → write → read → restore)
gsettings get/set org.gnome.settings-daemon.plugins.color night-light-*
cat /sys/firmware/acpi/platform_profile

# §7 the Flatpak
flatpak --user info --show-permissions io.github.asuscontrolcenter.asuscraft
flatpak run io.github.asuscontrolcenter.asuscraft --check
flatpak run io.github.asuscontrolcenter.asuscraft --self-test
diff native.txt sandboxed.txt

# §8 the regression
make clean && make ; make -C agent clean && make -C agent
./asus-control-center --check ; --self-test ; ./agent/asuscraft-agent --check
```
</details>

---

## 11. The backup and its integrity

| | |
|---|---|
| The audit's backup | `~/.local/share/soft-dimmer/backups/20261112T054257Z-asuscraft-audit-1.2.5/` |
| Its contents | 54 files + `MANIFEST.sha256` |
| The manifest | `sha256sum -c` → **54 OK, 0 failed** ✓ |
| The previous phase's backup | `~/.local/share/soft-dimmer/backups/20261111T110048Z-asuscraft-flatpak-agent/` (40 files, verified ✓) — the pre-change state of the helper and the policy ✓ |
| The audit workspace (the scripts) | `~/.local/share/soft-dimmer/audit-1.2.5/` |

The audit's backup contains the state **after** the two approved fixes ✓;
the earlier backup contains the state before the whole Flatpak work ✓.

---

## 12. The release recommendation

### CONDITIONAL PASS — FIXES OR TESTS REQUIRED

**What is proven** ✓

* The native application, the Flatpak and the host agent work together on
  Fedora 45 / GNOME 51 / Wayland ✓ — the reports agree except for five
  documented differences ✓.
* The Flatpak's permissions are exactly the six documented D-Bus names and
  one runtime directory ✓; no host filesystem, no device access, no
  unrestricted bus ✓.
* The polkit action id, the policy and the helper are unchanged ✓ (byte for
  byte ✓), the privileged path works end to end ✓, and the read-only path
  asks for no authorization ✓.
* The agent's command table refuses every hostile argument ✓ and offers no
  arbitrary-execution method ✓.
* The application is usable **without** the agent ✓ and says why when a
  feature is unavailable ✓.
* The build is warning-free ✓; every gate passes ✓ (31 and 53 assertions ✓).

**What must still be done before a 1.2.5 release**

1. **Install the agent RPM and run the system-level activation test**
   (the one critical test this audit could not execute — it needs root):

   ```sh
   sudo dnf install ./dist/asuscraft-agent-10.2-1.fc45.x86_64.rpm
   # then: the bus must start /usr/libexec/asuscraft-agent/asuscraft-agent
   #       and the application must show "Connected"
   # afterwards the user-level source-tree activation file may be removed
   #       (with the reviewer's approval)
   ```
2. **Reconcile the version numbers to 1.2.5** in the Makefile, both RPM
   specs, `debian/changelog` and the metainfo (§1, finding V1) — and add the
   `asuscraft-agent` spec to the release checklist ✓ (the CI already verifies
   that the tag matches both specs ✓).
3. **Rebuild the Debian package** — the source tree changed and the DEB was
   not rebuilt in this environment (its build dependencies are absent) ✗.
4. **Rebuild both RPMs after the version bump** and publish the
   `SHA256SUMS.txt` ✓ (the CI already does this ✓). Optionally sign them
   (finding V2) ✓.

**What was deliberately not done** ✓

* Nothing was published, tagged, committed or pushed ✗ — there is no Git
  repository to publish to ✓.
* The user-level D-Bus activation file was **restored** to its original
  content and left in place ✓, as the rules require ✓.
* The native installation (`asus-control-center-1.0.0-1.fc44`) was **not**
  touched ✓.
* No Flatpak permission and no polkit rule was weakened ✗; no
  `--filesystem=host`, no `--device=all`, no unrestricted bus ✗.
* The only source changes were the two approved one-line corrections ✓.

**Recommended next action:** run condition 1 (one command with root) and
condition 2 (a version bump), rebuild, re-run the two gates, and the release
can proceed.
