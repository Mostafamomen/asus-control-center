# AGENTS.md: guide for AI agents working in this repository

## 0. Read this first: "soft dim" means the hardware feature only

This repo is **ASUSCraft** — a C++20 GTK4/libadwaita app for ASUS laptops.

| | What it is | Where it lives |
|---|---|---|
| **Soft Brightness Plus** | The GNOME extension this repo *bundles and installs* (`data/soft-brightness-plus/`, installed by `src/control/sbp_local.cpp` — or by the host agent — as `soft-brightness-plus@joelkitching.com`). It dims the OLED **below the panel's lowest backlight level** — the hardware "flicker-free" feature. | in this repo |
| **The host agent** | A small, unprivileged, D-Bus-activated service that runs on the **host** so the Flatpak build can reach the hardware and the desktop session. It is a **separate package** (`asuscraft-agent`) with its own source tree in `agent/`. It holds **no** privilege: the whitelist-only helper and the polkit action id are unchanged. | `agent/` |
| **The Flatpak** | The GUI, the read-only probes and the app's own settings, running in a sandbox. It needs the agent for the firmware changes and the host commands, and says so when they are missing. | `flatpak/` |

**The Soft Dimmer *software* dimming feature has been discontinued and removed from
this project** (2026-11-11). There is no software overlay engine, no
`io.github.softdimmer` client and no bundled Soft Dimmer extension any more. Do not
reintroduce one, and do not substitute the hardware backlight for it: the two are
different features and the user chooses.

A **separately installed** Soft Dimmer extension
(`~/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer/`) may still
exist on the user's machine. It is **not** part of this project: do not modify it,
do not enable it and do not remove it from here. A change to
`src/control/softdim.cpp` or `data/soft-brightness-plus/` does not affect it — it is a
different codebase with its own schema
(`org.gnome.shell.extensions.soft-dimmer`) and its own D-Bus service
(`io.github.softdimmer`).

## 1. Build & verify

```sh
make                        # one g++ command; 0 warnings expected (-Wall -Wextra)
./asus-control-center --check        # pure-logic assertions, NO hardware touched
./asus-control-center --self-test    # read-only hardware dump; exit != 0 if the
                                    #   sign-in crypto or System Manager self-test broke
./asus-control-center --specs        # read-only hardware report
ACC_DEBUG=1 ./asus-control-center    # extra refresh prints

make -C agent               # the host agent (its own binary, its own package)
./agent/asuscraft-agent --check       # the agent's pure-logic self test
make -C agent install       # into /usr (needs sudo); the D-Bus service file
                            #   and the bundled Soft Brightness Plus

flatpak-builder --user --install build \
    flatpak/io.github.asuscontrolcenter.asuscraft.yml    # the Flatpak
```

**There is no unit-test framework.** The only automated tests are the in-binary assertions
in `unit_checks()` (`src/main.cpp`) reached via `--check`; add new cases there, not in a
new framework.

**`make test` is currently broken in this working copy** and cannot be used as a gate:
it depends on `launcher-bin`, and `launcher/launcher.gresource.xml` (plus `style.css` and
`launcher/data/`) are **missing**, so `make -C launcher` fails with *"No rule to make
target 'launcher.gresource.xml'"*. This is pre-existing (those assets were never in this
copy), not something you broke. Use `./asus-control-center --check` for the logic gate.

Packaging (needs sudo the first time, for build deps): `packaging/build-rpm.sh`
(builds **both** the app RPM and the agent RPM), `packaging/build-deb.sh` → `dist/`.

**This working copy is not a Git repository** (root and subdirectories alike). `make dist`
(`git archive`), the release checklist's tag/push steps, and any `git log`/`git diff` will
fail. Do not assume history exists, and do not try to create a repo to make them work.

`launcher/` is a **separate small C++ binary** (`launcher.cpp`, GTK4) built by
`make -C launcher`; `make` at the root does not build it.

`craft-launcher-main/` is a **vendored, unrelated Rust project** (ArtCraft Launcher) with
its own `AGENTS.md` and `CLAUDE.md`. Its rules (Rust-only, egui/wasm, no-`unwrap`,
`cargo xtask ci`) apply **only inside that directory** — they do not govern the C++ code
here, and the C++ rules do not govern it.

## 2. Architecture in one screen

`docs/code-guide.md` is the authoritative per-file map (responsibility of every
`src/**` file, the live-vs-approval split, and the palette). Read it before touching
anything unfamiliar. Shape:

- `src/main.cpp` — entry point, i18n/locale, login page, refresh tick, `--check` /
  `--self-test` / `--specs`.
- `src/probe/` — **read-only** hardware readers (sysfs, EDID, `/proc`, `nvidia-smi`).
  `devs.cpp` is the portability layer that *discovers* nodes; model names are never
  compared anywhere.
- `src/control/` — the write path: `ctl.cpp` (the privileged API), `hostcmd.cpp` (one
  way to reach the host, with or without the agent), `commands.cpp` (the fixed command
  table, shared with the agent), `agent.cpp` (the client for the host agent),
  `asusd.cpp` (asusd system-bus client), `gfx.cpp`/`muxreq.cpp` (GPU MUX), `backlight.cpp`
  (logind `Session.SetBrightness`), `sbp_local.cpp` (installs/enables the bundled
  extension), `softdim.cpp` (the app's wrapper around it), `nlsettings.cpp` (the GNOME
  night light), `nightlight.cpp` (GNOME and KDE), `touchpadctl.cpp` (the desktop's own
  touchpad setting), `portal.cpp` (the XDG portals), `brightness.cpp` (OLED dim via the
  extension's GSettings key).
- `agent/` — the **host agent**, a separate component and a separate package. It owns one
  session-bus name (`io.github.asuscontrolcenter.Agent`) and runs the commands of
  `src/control/commands.cpp` on the host. See `docs/host-agent.md`.
- `flatpak/` — the Flatpak manifest. The Flatpak has its own lower-case application id
  (`io.github.asuscontrolcenter.asuscraft`); the native id is unchanged.
- `src/ui/` — GTK4 UI; `mcui.cpp` is the reusable component kit, `theme.css` /
  `theme-dark.css` the two skins.
- `src/user/` — local account, `settings.conf`, salted SHA-256 password.

## 3. Invariants an agent must not break

From `docs/security.md` and `docs/code-guide.md`:

- **The GUI never runs as root and never writes to sysfs itself.** Every privileged write
  goes through the whitelist-only bash helper `data/acc-helper` (fixed argv, no `eval`, no
  shell string building) or the `xyz.ljones.Asusd` system-bus service.
- **The app is a system-bus *client* only.** It owns no bus name and never touches the
  session bus. Do not add a session-bus service or a `g_bus_own_name` to `src/`. The one
  exception is `src/control/agent.cpp`, which is a pure *client* of the host agent: it
  calls `org.freedesktop.DBus.NameHasOwner`/`StartServiceByName` and then the agent's
  methods. It owns nothing.
- **The host agent (`agent/`) is the only session-bus service**, and it is outside `src/`.
  It runs as the ordinary user, refuses to start as root, holds no privilege, and may only
  run the commands of the fixed table. Adding a verb to the helper or to the command
  table is a **security change** and needs its own review (`docs/polkit.md` §6).
- No `sudo`, `pkexec`, `setuid`, or shell-out with interpolated values. Use `GSubprocess`
  with a fixed argv (`path + NULL`), never `g_shell_parse_argv`/`system()`.
- Every privileged write is **read back and verified**; failures must not be reported as
  success.
- **GPU MUX is never applied automatically in-session.** It is queued via asusd and applied
  at shutdown; it is never queued on battery. See `docs/gpu-mux.md`.
- **A failing probe must never overwrite the last valid reading** — show `—` instead.
- **Never compare model names.** Gate every feature on what the kernel exposes
  (`docs/portability.md`).
- Only one user-level dconf write **in the app**: the OLED dim key, clamped **10–100 %**.
  The **host agent** writes two more on the host's behalf — the touchpad switch and the
  night-light keys — because a sandbox's dconf does not reliably reach the host's
  database (see `docs/flatpak-install.md` §6.1). Every one of them is read and written
  **one key at a time**, never a whole schema.
- **The Flatpak's permissions stay narrow**: no `--filesystem=host`, no `--device=all`,
  no unrestricted system bus. A feature that needs the host gets the host agent, or it
  reports that it is unavailable. See `docs/flatpak-security-audit.md`.

## 4. Conventions that are easy to get wrong

- **Localisation**: every user-facing string lives in the English↔Arabic tables in
  `src/i18n/i18n.cpp`, length-checked with `G_STATIC_ASSERT`. Arabic is the default
  language and the direction flips to RTL. Adding a string means updating the table
  consistently — an unbalanced edit fails the build.
- **Two skins, one set of class names**: `theme.css` and `theme-dark.css` must define the
  same class names; the palette is fixed (light `#F5F7FA` canvas, `#006CE1` accent, …).
  Never hard-code a colour outside the palette.
- **Stable page IDs** are 1:1 between the sidebar rows, the stack pages and the dashboard
  ">" buttons: `home, battery, gpu, display, performance, system, apps, settings`. This
  mapping is machine-checked — keep it that way.
- `src/ui/theme.cpp` owns the light/dark preference keyfile and the stylesheet path; both the
  header button and the Settings switch call `acc_ui_theme_toggle()`, which rebuilds the
  window.
- `docs/system-report.md` is a saved `--specs` snapshot; `docs/support.md` is reference
  material the app never reads.
- **The touchpad uses the desktop's own setting** (the GNOME periphls schema). The old
  `xinput` path is gone: it only ever saw X11 devices. Where the desktop exposes no
  usable setting, the control says so instead of pretending to work.
- **The host command layer is `acc_host_run()`** (`src/control/hostcmd.cpp`). A new
  command goes into the shared table in `src/control/commands.cpp` — never as a bare
  `g_spawn_*` at the call site — so the app and the agent stay in step.

## 5. Releasing

Version lives in **four** places that must agree: `Makefile` (`VERSION`),
`packaging/rpm/asus-control-center.spec` (with a `%changelog` entry), `debian/changelog`,
and the `<releases>` list in `data/io.github.asuscontrolcenter.AsusControlCenter.metainfo.xml`.
The **agent** has a fifth place: `packaging/rpm/asuscraft-agent.spec`. It is released
together with the app and its version must match the app's.
CI (`.github/workflows/packages.yml`) builds the RPM and DEB on every push and PR, runs
`lintian --fail-on error` on the DEB and installs it to run `--self-test`; on a `v*` tag it
also verifies the tag matches the spec and debian versions, then attaches the packages and
`SHA256SUMS.txt` to the GitHub release. Not applicable to this working copy (no Git), but
the version files still have to agree.

## 6. GNOME Shell extension work (sibling project, outside this repo)

The Soft Dimmer extension in `~/.local/share/gnome-shell/extensions/` is an **independent**
installation that this project neither ships nor talks to any more. These platform
facts were expensive to establish — reuse them if it is ever worked on again:

- **GJS 1.90 D-Bus**: synchronous `Gio.DBusExportedObject.wrapJSObject()` methods receive
  **positional** arguments, not a `params` array; `const [x] = params` throws
  `TypeError: params is not iterable`. The async form `FooAsync(params, invocation)` does
  get an array. Return **plain** values from sync methods — a `GLib.Variant` is always
  truthy, so returning one for a `b` out-arg yields `true` regardless of the real value.
  An `a{sv}` out-arg needs `new GLib.Variant('(a{sv})', [caps])` returned via
  `invocation.return_value()`; `GLib.Variant.new('a{sv}', [caps])` throws.
- **Clutter on GNOME 51**: `Clutter.Actor.opacity` is a **guint 0–255**, not a 0.0–1.0
  float — writing a float truncates to 0. `Clutter.Actor` has **no** `track_hover`/`can_focus`
  (`St.Widget` does). `Clutter.Color` is gone; use `new Cogl.Color({...})`. `global.get_dbus_proxy`
  is gone.
- **You cannot test actors outside a live Shell**: constructing a `Clutter.Actor` aborts with
  `_clutter_context_get_default: assertion failed`. GNOME 51 on Wayland also refuses an
  in-place Shell restart ("On wayland we cannot restart"), and GJS caches extension modules
  by URL — so verifying extension code needs a **logout/login**, and only then.
- `Meta.Display::grab-op-begin/end` are **window move/resize** signals, not capture signals.
  `org.gnome.Mutter.ScreenCast` exposes only `CreateSession` — no capture detection exists.
  `Main.screenshotUI.screencast-in-progress` is the only in-process capture hint.
- `Main.layoutManager.addChrome()` puts an actor **below `top_window_group` but above the top
  panel** (`panelBox` is added through `addChrome()` earlier, so a later `addChrome()` lands
  above it). To stay under the panel, reorder with
  `Main.uiGroup.set_child_below_sibling(actor, Main.layoutManager.panelBox)`.
- Never leave both dimming extensions enabled at once; they dim additively.
