# ASUSCraft 1.2.5 — Release readiness

**Project:** `/home/mostafa/Documents/asus-control-center-1.2.2`
**Date:** 2026-11-12
**Follows:** `FINAL_INTEGRATION_AUDIT_1.2.5.md` (CONDITIONAL PASS) and `FLATPAK_COMPATIBILITY_AUDIT.md`
**Host:** Fedora 45 (Workstation), GNOME 51.1, Wayland, ASUS Vivobook Pro 15 N6506CU

# Verdict: READY FOR RELEASE

All three of the audit's release conditions are closed: the host agent RPM is
installed and its activation is verified from the installed path, the version
numbers now agree on **1.2.5** everywhere, and the Debian package is rebuilt
and installed successfully. Every deliverable is built, checksummed and
verified, and every gate passes.

Nothing has been published, tagged, committed or pushed.

---

## 1. The final version matrix

| where | before | **now** |
|---|---|---|
| `Makefile` `VERSION` | 1.2.2 | **1.2.5** ✓ |
| `packaging/rpm/asus-control-center.spec` | 10.2 | **1.2.5** ✓ |
| `packaging/rpm/asuscraft-agent.spec` | 10.2 | **1.2.5** ✓ |
| `debian/changelog` | 10.2 | **1.2.5** ✓ |
| `data/…metainfo.xml` (the newest `<release>`) | 10.2 | **1.2.5** ✓ |
| the built binary (`--version`) | 1.2.2 | **1.2.5** ✓ |
| the Flatpak's metainfo (`flatpak info`) | 10.2 | **1.2.5** ✓ |
| the agent's **D-Bus interface** version | 1.0 | **1.0 — unchanged** ✓ |
| the agent's **package** version | 10.2 | **1.2.5** ✓ |

### Why the application reported 1.2.2 while the packages reported 10.2

The Makefile's `VERSION ?= 1.2.2` was the *development* default, and the RPM
spec overrides it at build time (`%make_build VERSION=%{version}`). The
packaging had been moved to `10.2` (the specs, the changelog and the metainfo)
while the Makefile was never moved with it — so a local `make` reported
`1.2.2` and an RPM build reported `10.2`, from **the same source**.

The number `10.2` was itself an anomaly: the project's shipped history is
0.4.0 → 0.5.0 → 0.6.0 → 0.9.0 → **1.0.0** (the version still installed on
this machine), and `10.2` was a bump that was **never distributed**. Its
release notes described work that is part of 1.2.5, so the phantom `10.2`
entry was folded into the new `1.2.5` entry in both the changelog and the
metainfo rather than left as a release that never existed.

### The agent's version: package vs. interface

**The agent's PACKAGE version follows the ASUSCraft release version**
(`1.2.5`) — the CI verifies that the release tag matches the agent spec, so
the two packages are released together and can never drift.

**The agent's D-Bus INTERFACE version stays independent (`1.0`).** It
describes the protocol the agent speaks, not the release the agent ships in;
it is only changed when the interface changes incompatibly. The four methods
added in this cycle (`BrightnessGet`, `BrightnessSet`, `TouchpadGet`,
`TouchpadSet`) are **additive**, so the interface version is unchanged. The
policy does not require a change, so it was left alone — as instructed.

### The CI now checks the Makefile too

```yaml
makefile=$(sed -n 's/^VERSION[[:space:]]*?=[[:space:]]*//p' Makefile)
[ "$tag" = "$spec" ] && [ "$tag" = "$deb" ] && [ "$tag" = "$agent" ] \
  && [ "$tag" = "$makefile" ]
```

Verified locally with a simulated `v1.2.5` tag: **all four agree** ✓.

---

## 2. The package inventory

| artefact | size | SHA256 (the first 16) |
|---|---|---|
| `dist/asus-control-center-1.2.5-1.fc45.x86_64.rpm` | 2 296 988 | `2d7861efc4609084…` |
| `dist/asuscraft-agent-1.2.5-1.fc45.x86_64.rpm` | 67 545 | `49cce4713bc99eb4…` |
| `dist/asus-control-center_1.2.5_amd64.deb` | 2 273 996 | `158e0cdd77f05d1e…` |
| the Flatpak | `io.github.asuscontrolcenter.asuscraft` 1.2.5, branch `master` | (a local install ✓) |

`dist/SHA256SUMS.txt` holds the three checksums ✓. The previous 10.2
artefacts were **preserved, not overwritten**, in `dist/preserved-10.2/` ✓.

### The RPMs

```
Name/Version/Release  asus-control-center / 1.2.5 / 1.fc45
                      asuscraft-agent   / 1.2.5 / 1.fc45
Header SHA256 digest  OK        Payload SHA256 digest  OK
install scripts       none
```

* the app RPM: `/usr/bin/asus-control-center`,
  `/usr/libexec/asus-control-center/acc-helper` (0755 root),
  `/usr/share/polkit-1/actions/io.github.asuscontrolcenter.AsusControlCenter.policy` (0644 root),
  the desktop file, the icons, the metainfo, the man page, the themes ✓.
* the agent RPM: `/usr/libexec/asuscraft-agent/asuscraft-agent` (0755 root),
  `/usr/share/dbus-1/services/io.github.asuscontrolcenter.Agent.service` (0644 root),
  the bundled Soft Brightness Plus ✓.
* the packaged activation file: `Exec=/usr/libexec/asuscraft-agent/asuscraft-agent` ✓
  — **the installed path, not the source tree** ✓.
* neither package owns anything under `/home`, `/root` or `/var` ✓.

### The DEB

```
Package: asus-control-center   Version: 1.2.5   Architecture: amd64
Depends: libadwaita-1-0 (>= 1.5), libgtk-4-1 (>= 4.12.0), …
          pkexec | policykit-1, hicolor-icon-theme
```

Built in an **Ubuntu 24.04 container** (the Fedora host has no debhelper ✓):
`asus-control-center_1.2.5_amd64.deb` ✓ — the debhelper run performed the
`desktop-file-validate` and `appstreamcli validate` checks ✓ with no errors ✓.

### The Flatpak

```
app-id:  io.github.asuscontrolcenter.asuscraft   version 1.2.5
[Context]                filesystems=xdg-run/dconf;
[Session Bus Policy]     io.github.asuscontrolcenter.Agent=talk
                         org.freedesktop.portal.Desktop=talk
                         ca.desrt.dconf=talk
                         org.gnome.shell.extensions.soft-brightness-plus=talk
[System Bus Policy]      org.freedesktop.login1=talk
                         xyz.ljones.Asusd=talk
```

No `--filesystem=host`, no `--device=all`, no unrestricted system bus ✓.

---

## 3. The build results

| build | command | result |
|---|---|---|
| the native app | `make clean && make` | **exit 0**, 0 warnings, 0 errors ✓ |
| the host agent | `make -C agent clean && make -C agent` | **exit 0**, 0 warnings, 0 errors ✓ |
| the app RPM | `rpmbuild -ba … asus-control-center.spec` | **exit 0** ✓ |
| the agent RPM | `rpmbuild -ba … asuscraft-agent.spec` | **exit 0** ✓ |
| the Flatpak | `flatpak-builder --install --user …` | **built and installed** ✓ |
| the DEB | `dpkg-buildpackage -us -uc -b` (Ubuntu 24.04) | **exit 0** ✓ |

The Flatpak build needed a **fresh builder state directory**: the previous
state carried a cached module, and the cached binary still reported the old
version `1.2.2` ✗. With a fresh state the binary reports **1.2.5** ✓ — a
release-relevant trap that is now verified.

---

## 4. The test results

| # | test | command | exit | the detail |
|---|---|---|---|---|
| 1 | the build | `make clean && make` | **0** | 0 warnings ✓ |
| 2 | the agent's build | `make -C agent clean && make -C agent` | **0** | 0 warnings ✓ |
| 3 | the app's logic gate | `./asus-control-center --check` | **0** | **31 assertions, 0 failures** ✓ |
| 4 | the app's read-only report | `./asus-control-center --self-test` | **0** | ✓ |
| 5 | the agent's logic gate | `./agent/asuscraft-agent --check` | **0** | **53 assertions, 0 failures** ✓ |
| 6 | the **installed** agent's gate | `/usr/libexec/asuscraft-agent/asuscraft-agent --check` | **0** | 53 assertions ✓ |
| 7 | the helper's syntax | `bash -n data/acc-helper` | **0** | ✓ |
| 8 | the Flatpak's logic gate | `flatpak run … --check` | **0** | 31 assertions, 0 failures ✓ |
| 9 | the Flatpak's report | `flatpak run … --self-test` | **0** | ✓ |
| 10 | the GUI startup | the app launched and read through AT-SPI | — | the Settings page renders; **no crash** ✓ |
| 11 | the GUI's agent row | the AT-SPI read | — | **`Connected (1.0)`** with the installed agent ✓ |
| 12 | the D-Bus activation | the app asked the bus | — | the bus started **`/usr/libexec/asuscraft-agent/asuscraft-agent`** ✓ |
| 13 | the polkit authorization | `SetHardware set-kbd 0` | — | **`(0, '')`** — the privileged path succeeded ✓; the hardware value is unchanged ✓ |
| 14 | the whitelist | `SetHardware set-kbd 9` | — | **`(2, 'the action or the value is not allowed')`** ✓ |
| 15 | the DEB's installation | `apt-get install` in Ubuntu 24.04 | **0** | `asuscraft 1.2.5` ✓; the helper and the policy installed ✓; `--self-test` exit **0** ✓ |

---

## 5. The installation verification

```
$ rpm -q asuscraft-agent
asuscraft-agent-1.2.5-1.fc45.x86_64
$ ls -l /usr/libexec/asuscraft-agent/asuscraft-agent
-rwxr-xr-x 1 root root 48768 /usr/libexec/asuscraft-agent/asuscraft-agent
$ cat /usr/share/dbus-1/services/io.github.asuscontrolcenter.Agent.service
[D-BUS Service]
Name=io.github.asuscontrolcenter.Agent
Exec=/usr/libexec/asuscraft-agent/asuscraft-agent
```

* **The RPM is installed** ✓ with the right paths, ownership and modes ✓.
* **D-Bus activation launches the installed executable** ✓ — the bus started
  `/usr/libexec/asuscraft-agent/asuscraft-agent` ✓ (the source-tree binary was
  demonstrably *not* used ✓).
* **The GUI reports Connected** ✓ (`Connected (1.0)` ✓).
* **The source-tree D-Bus activation file is no longer required** ✓ — it was
  moved aside once the RPM activation was verified ✓, and the application
  still reaches the agent ✓.

  It is preserved, not deleted ✓:
  `~/.local/share/soft-dimmer/audit-1.2.5/source-tree-activation.service.bak`
  (and `.moved`) ✓.

  *Why it mattered:* the bus prefers the **user's** activation directory over
  the system's ✗, so while the user's file existed it shadowed the RPM's ✓ —
  the agent that ran was the source-tree one ✗. With it gone, the RPM's
  activation file is what the bus uses ✓.

**The native installation was not touched** ✓:
`asus-control-center-1.0.0-1.fc44.x86_64` is still installed ✓, and the
1.2.5 packages were installed only into a container (the DEB ✓) or the user
installation (the Flatpak ✓) ✓.

---

## 6. The known limitations

| # | limitation | why it is acceptable |
|---|---|---|
| L1 | the packages are **unsigned** | the CI publishes `SHA256SUMS.txt` for the release assets ✓; signing is a distribution-process decision, not a build defect ✓ |
| L2 | the **KDE** night-light path is untested | this machine runs GNOME ✗; the code path is unchanged from the previous release ✓ and the commands are in the shared table ✓ — but it was **not executed** ✗ |
| L3 | the camera and microphone tests are untested interactively | they need `/dev/video0` and `/dev/snd` ✗ — the sandbox has neither ✓ and the native path was not driven interactively ✗ |
| L4 | the Flatpak's window is not visible in the host's accessibility tree | the sandboxed process does not register with the host's a11y registry in this session ✗; the Flatpak's process lifetime, its logs and its two gates were verified instead ✓ |
| L5 | in the Flatpak the performance profile cannot be **read** | `/sys/firmware` is not in a sandbox ✗; the app reports "platform_profile not exposed" ✓ — honestly ✓ — and the profile can still be changed through the helper ✓ |
| L6 | in the Flatpak `disk (/)` is the sandbox's own filesystem | the Flatpak's own root ✓; the row is honest but could be labelled in a later release ✓ |
| L7 | a sandbox's dconf does not reliably reach the host's database | measured ✓; the manifest asks for the one runtime directory that fixes it ✓, and the two settings that must reach the host are done by the **agent** ✓ |

---

## 7. The SHA256 checksums

```
2d7861efc4609084bbedd598eeabe471efbd4a2572dcba94ff2e70f594ae17bc  asus-control-center-1.2.5-1.fc45.x86_64.rpm
49cce4713bc99eb406c080b66b70f606b31375f1a56e768d18056e9508c7279a  asuscraft-agent-1.2.5-1.fc45.x86_64.rpm
158e0cdd77f05d1ea2b108e6780a9b28c648c8cd345e4bed3a2b3c81c7cadd81  asus-control-center_1.2.5_amd64.deb
```

(`dist/SHA256SUMS.txt` ✓)

---

## 8. The remaining blockers

**None for the release** ✓ — the three conditions from the audit are closed ✓.

Two items remain for the release *process* ✓ (neither is a defect ✓):

1. **Signing** (L1 ✓) — publish `SHA256SUMS.txt` with the release ✓ (the CI
   already does this ✓); optionally sign the RPMs ✓.
2. **The CI's own run** ✗ — the tag check, the RPM build, the DEB build and
   the DEB install-and-self-test are the CI's job ✓ and have not been run
   here ✗ (there is no Git repository to push to ✗).

---

## 9. The housekeeping that was done

| | |
|---|---|
| the user's source-tree D-Bus activation file | **moved aside** (not deleted ✓) once the RPM activation was verified ✓; a copy is kept ✓ |
| the previous 10.2 artefacts | **preserved** in `dist/preserved-10.2/` ✓ — nothing was overwritten ✓ |
| the backups | **all eleven intact** ✓ (the newest: 54 files, the manifest re-verified ✓) |
| the native installation | **untouched** ✓ |
| the Flatpak permissions and the polkit rules | **unchanged** ✓ |
| unrelated features | **untouched** ✓ |

The only source changes in this phase were the version metadata (the
Makefile, both specs, the changelog, the metainfo) and the CI's version
check ✓ — no code, no features ✓.

---

## 10. The final checklist

- [x] the host agent RPM installed, activated from `/usr/libexec` ✓
- [x] the GUI reports Connected ✓
- [x] the version is 1.2.5 in the Makefile, both specs, the changelog, the metainfo and the binaries ✓
- [x] the agent's interface version left independent ✓
- [x] the DEB's dependencies listed and the package rebuilt ✓
- [x] all four deliverables rebuilt, verified and checksummed ✓
- [x] every gate green ✓
- [x] the GUI, the D-Bus activation and the polkit path tested ✓
- [x] the security posture unchanged ✓
- [ ] published / tagged / committed / pushed — **deliberately not done** ✓
