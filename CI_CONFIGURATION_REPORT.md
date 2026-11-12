# ASUSCraft 1.2.5 — CI configuration report

**Date:** 2026-11-12
**Workflows:** `.github/workflows/ci.yml` (new), `.github/workflows/flatpak.yml` (new), `.github/workflows/packages.yml` (extended)
**Helper scripts:** `flatpak/verify-permissions.sh` (new), `flatpak/verify-versions.sh` (new)

> **No workflow has been executed yet.** GitHub Actions has not run on this
> branch, so **nothing is claimed to have passed in CI** ✓. Every check
> below was executed **locally** on the release machine, and the commands
> are given so the reviewer can reproduce them ✓.

---

## 1. The workflows at a glance

| workflow | when | cost | what it proves |
|---|---|---|---|
| `ci.yml` | every push, every PR, every tag | **cheap** ✓ | the app and the agent build warning-free and pass their gates ✓; the release metadata is consistent ✓; the Flatpak's permissions have not expanded ✓ |
| `flatpak.yml` | the manifest job on every push; the **build** job on a tag or on demand | the manifest job is cheap ✓; the build pulls the GNOME SDK ✗ (≈1 GB ✗) so it is **not** run on every push ✓ | the Flatpak builds ✓ and its installed permissions are the reviewed ones ✓ |
| `packages.yml` (existing) | every push, every PR, every tag | moderate ✓ | the RPMs and the DEB build ✓, the DEB installs and its self-test passes ✓, and the release tag matches every version source ✓ |

The expensive Flatpak build is deliberately **not** on every push ✓
("avoid unnecessarily expensive CI jobs" ✓) — it runs for a release tag and
on demand ✓.

---

## 2. `ci.yml` — the application, the agent and the metadata

### Job `application` (Fedora container ✓)

```sh
dnf install -y gcc-c++ make pkgconf gtk4-devel libadwaita-devel
make CXXFLAGS="-O2 -g -Wall -Wextra -Werror"
./asus-control-center --check
./asus-control-center --self-test
```

* `-Werror` turns "0 warnings" from a convention into a **gate** ✓.
* `--check` is the 31-assertion logic gate ✓; `--self-test` is the
  read-only hardware report ✓ (it passes in a container ✓ — the values that
  cannot be read are shown as `—` ✓).

### Job `agent` (Fedora container ✓)

```sh
make -C agent CXXFLAGS="-O2 -g -Wall -Wextra -Werror"
./agent/asuscraft-agent --check          # 53 assertions
```

plus three **packaging** checks ✓ that the task asked for ✓:

| check | what it asserts |
|---|---|
| the D-Bus activation metadata | the `.service.in` file's `Name=` equals `ACC_AGENT_BUS_NAME` ✓; the rendered `Exec=` is `/usr/libexec/asuscraft-agent/asuscraft-agent` ✓ |
| the polkit action | `data/acc-helper` and the policy are **byte-identical to the audited ones** (their SHA256 sums are pinned ✓); the policy still declares `io.github.asuscontrolcenter.helper` ✓ |
| the helper's whitelist | the helper parses ✓ and still accepts **exactly** the five audited verbs ✓ (`set-perf`, `set-charge`, `set-charge-mode`, `set-gpu-mux`, `set-kbd` ✓) |

### Job `metadata` (no container needed ✓)

| check | what it asserts |
|---|---|
| the version policy | `./flatpak/verify-versions.sh 1.2.5` ✓ — the Makefile, both RPM specs, `debian/changelog` and the metainfo all say **1.2.5** ✓; the agent's **D-Bus interface** version is defined and **does not mirror** the release ✓; the Flatpak's app-id is lower-case ✓ |
| the Flatpak's permissions | `./flatpak/verify-permissions.sh` ✓ — see §4 ✓ |
| the manifest's identity | it parses as YAML ✓ and declares the expected app-id, runtime, SDK, command and modules ✓ |
| the RPM specifications | both parse with `rpmspec` ✓ and the agent's package version is `1.2.5` ✓ |
| the Debian metadata | `debian/changelog` says `1.2.5` ✓ and the build dependencies are declared ✓ |
| the credentials | the whole tracked tree is scanned for token and key patterns ✓ |

---

## 3. `flatpak.yml` — the manifest and the build

### Job `manifest` (every push ✓ — cheap ✓)

* the manifest parses and its identity is right ✓;
* **the permissions have not expanded** ✓ (`verify-permissions.sh` ✓);
* the version policy holds ✓;
* the metainfo's newest release is `1.2.5` ✓.

### Job `build` (a release tag or `workflow_dispatch` ✓)

```sh
flatpak --user install -y flathub org.gnome.Sdk//46 org.gnome.Platform//46
flatpak-builder --user --install --force-clean build flatpak/….yml
```

then it asserts ✓:

* the installed application id is `io.github.asuscontrolcenter.asuscraft` ✓
  and its version is `1.2.5` ✓;
* the **installed** permissions contain no `--device=all`, no
  `--filesystem=host`, no `--allow=devel` and no unrestricted system bus ✓,
  and the one filesystem entry is `xdg-run/dconf` ✓;
* the application's own gates pass **inside the sandbox** ✓
  (`--check` ✓, `--self-test` ✓);
* and, for a tag, it exports the single-file bundle for the release ✓.

---

## 4. The permission guard — the most important check

`flatpak/verify-permissions.sh` compares the manifest's `finish-args` with
the reviewed set ✓ **exactly** ✓ — a permission that is *added* ✗, *removed*
✗ or *changed* ✗ fails the build ✓.

```
$ ./flatpak/verify-permissions.sh
ok: the manifest asks for exactly the reviewed permissions
  session bus:  ca.desrt.dconf io.github.asuscontrolcenter.Agent
                org.freedesktop.portal.Desktop
                org.gnome.shell.extensions.soft-brightness-plus
  system bus:   org.freedesktop.login1 xyz.ljones.Asusd
  filesystem:   xdg-run/dconf
```

It also refuses, by name, the permissions the project has deliberately never
granted ✓: `--device=all` ✓, `--filesystem=host` ✓, `--allow=devel` ✓,
`--system-talk-name=org.freedesktop.DBus` ✓,
`--system-talk-name=org.freedesktop.systemd1` ✓ (the systemd bus can start
units ✗ — a sandbox has no business holding it ✓), and the raw X11/Wayland
sockets ✓.

**The negative tests were run locally** ✓:

| the manifest was given | the result |
|---|---|
| `--device=all` | **exit 1** ✓ |
| `--filesystem=host` | **exit 1** ✓ |
| `--system-talk-name=org.freedesktop.systemd1` | **exit 1** ✓ |
| `ca.desrt.dconf` removed | **exit 1** ✓ |

The script only inspects the **list items** of `finish-args` ✓ — the
comments in the manifest document the permissions that are deliberately *not*
asked for ✓, and they must not trip the check ✓.

---

## 5. The version guard

`flatpak/verify-versions.sh [expected]` ✓ checks that every version source
agrees ✓ **and** that the agent's D-Bus interface version is defined ✓ but
**does not mirror** the release version ✓ — the two are independent by
policy ✓ (the interface describes the protocol ✗, the package version
follows the release ✓).

```
$ ./flatpak/verify-versions.sh 1.2.5
the version sources:
  the Makefile            1.2.5
  the app RPM spec        1.2.5
  the agent RPM spec      1.2.5
  debian/changelog        1.2.5
  the AppStream metainfo  1.2.5
  the agent's interface   1.0  (independent of the release ✓)
  the Flatpak app-id      io.github.asuscontrolcenter.asuscraft ✓
ok: every version source agrees on 1.2.5
```

The CI's release job checks the **same** five sources against the tag ✓
(the Makefile was added to that check in this phase ✓).

---

## 6. The existing `packages.yml` — what changed

| change | why |
|---|---|
| the RPM job now also runs `./agent/asuscraft-agent --check` | the agent is a shipped component ✓ — its gate must run in CI ✓ |
| the release job's tag check now also compares the **Makefile's** `VERSION` | the audit found the Makefile drifting from the packages ✓ — the check now covers the fifth source ✓ |
| everything else is unchanged ✓ | the RPM/DEB builds, the lint, the DEB install-and-self-test, the checksums and the release attachment ✓ |

---

## 7. Running the checks locally

```sh
./flatpak/verify-versions.sh 1.2.5     # the version policy
./flatpak/verify-permissions.sh        # the sandbox's permissions
make && ./asus-control-center --check && ./asus-control-center --self-test
make -C agent && ./agent/asuscraft-agent --check
```

All five pass on the release machine ✓ (verified in this phase ✓).

---

## 8. What the CI does **not** cover, and why

| not covered | why |
|---|---|
| the Flatpak build on every push | it pulls the whole GNOME SDK ✗ — it runs for the release tag ✓ |
| the privileged (polkit) path | it needs an interactive authorization ✗ — the agent's whitelist and the helper's are covered statically ✓ and the end-to-end path was verified manually ✓ |
| the KDE night-light path | the CI runners are GNOME ✗ — the commands are in the shared table ✓ and the path is unchanged ✗ |
| the camera and microphone tests | they need real devices ✗ |

---

## 9. The honest status

| | |
|---|---|
| the workflows are **written and syntactically valid** ✓ | every YAML file parses ✓ |
| the **helper scripts** were executed locally ✓ | both pass ✓ and both catch a deliberate change ✓ |
| the **workflows themselves have not run** ✗ | GitHub Actions has not seen this branch yet ✗ — **no claim of a green CI is made** ✓ |

The first push will be the first real execution ✓ — and the reviewer should
look at the run before the release is published ✓.
