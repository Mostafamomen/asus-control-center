# ACC Code Guide

Per-file responsibilities, how to build/run, what is live, and what the
hardware does not support. Matched to the light MyASUS-style UI (M2).

The app is C++20 (`g++ -std=gnu++20`). GTK4 and libadwaita expose a C API
(and libadwaita has no official C++ binding), so the sources call that API
directly from C++; every `.cpp` file is still plain procedural code.
Build with `make` — it uses `g++` and links no extra C++ runtime beyond the
standard library.

| File | Responsibility |
|---|---|
| `src/main.cpp` | App entry: sets Arabic/RTL when chosen, loads the local account, shows the **login page**, then runs the 2-second refresh tick, and offers a read-only `--self-test` that prints every probe. |
| `src/i18n/i18n.cpp` / `.h` | English↔Arabic string tables (length-checked with `G_STATIC_ASSERT`); the toggle also flips text direction (LTR/RTL). Every user-facing label comes from here. |
| `src/probe/probe.cpp` / `.h` | Shared helpers: read a sysfs file as trimmed string / int, check path existence. |
| `src/probe/battery.cpp` | Reads `/sys/class/power_supply/BAT0`: %, Wh (now/full/design), cycles, status, charge limit. |
| `src/probe/dmi.cpp` | Motherboard ID strings → "ASUS Vivobook Pro 15 N6506CU". |
| `src/probe/hwmon.cpp` | Walks `/sys/class/hwmon` for CPU/board temps and fan RPMs. |
| `src/probe/gpu.cpp` | Runs `/usr/bin/nvidia-smi` with a fixed argv through `GSubprocess` (no shell) and parses name, driver, util, temp, VRAM, power draw and `display_active`. Cached for 2 s so the 2 s refresh cannot spawn it several times per tick. Unknown fields stay -1 and render as "—". |
| `src/probe/display.cpp` | Parses the `eDP-1` EDID → 2880×1620@120 Hz + raw backlight level. |
| `src/probe/sysinfo.cpp` | OS name, RAM size, NVMe model, CPU name. |
| `src/probe/devs.cpp` / `.h` | **Portability layer**: discovers the battery node (`BAT0`/`BAT1`/`BATT`), the charge-limit file (`charge_control_end_threshold` or `charge_stop_threshold`), the `asus-armoury` firmware attribute (`/sys/class/firmware-attributes/asus-armoury/attributes/<name>/current_value`, kernel ≥ 6.17) with the ASUS WMI platform dir (`asus-nb-wmi`/`asus-wmi`/any `asus*`) as fallback, the `pending_reboot` flag, the keyboard LED (`*kbd_backlight*`) and the internal eDP connector. No model names are ever compared; every feature is gated on what this kernel exposes. |
| `src/probe/load.cpp` / `.h` | Live system metrics, all real: CPU load via `/proc/stat` deltas, memory via `/proc/meminfo`, disk via `statvfs("/")`. First CPU call returns -1 (no baseline yet) → UI shows "—". |
| `src/control/ctl.cpp` / `.h` | Fallback privileged write layer: spawn `pkexec /usr/local/bin/acc-helper <action> <value>` via `GSubprocess`; validates every value client-side too. |
| `src/control/asusd.cpp` / `.h` | asusd (asusctl) D-Bus client: reads `xyz.ljones.Platform` (`PlatformProfile`, `PlatformProfileChoices`, `PlatformProfileOnAc/OnBattery`, `ChangePlatformProfileOnAc/OnBattery`) and the GPU MUX object `/xyz/ljones/asus_armoury/gpu_mux_mode` (`CurrentValue`, `QueuedGpuValue`, `PossibleValues`), plus `asus-shutdown.service` state from systemd. Every setter re-reads the service (6 × 50 ms) and fails unless the value really changed. No shell, fixed bus name/paths. |
| `src/control/brightness.cpp` | OLED dim bridge: reads/writes **one** GSettings key of the user's Soft Brightness Plus extension — no root, value clamped 10–100 %. |
| `src/user/user.cpp` / `.h` | Local account for the login page: `[user] name/salt/hash/remember/signed_in` in `settings.conf`, password stored only as salted SHA-256, config forced to mode 0600. `signed_in` is what makes later launches skip the login page; Sign out clears it. |
| `src/ui/login.cpp` / `.h` | First-run "Create your account" / later "Welcome back" page: split brand panel with the app logo, username + password (+ confirm), reveal toggle, remember-username checkbox, **light/dark toggle and EN/ع switch** in the header, and a reset-local-account link. Calls `acc_ui_boot()` on success. |
| `src/ui/window.cpp` | Main window: header (model + refresh + menu), sidebar with stable page IDs, 8 stack pages, crossfade transitions, pushes probe values into every widget each refresh. Forces `ADW_COLOR_SCHEME_FORCE_LIGHT` so libadwaita widgets match the light theme. |
| `src/ui/window.h` | `AccUI` shared-wiring struct (widget refs, drag-guards `oled_touch`, caption labels `bat_small`/`fan_small`). |
| `src/ui/home.cpp` | Dashboard, mirrored on MyASUS: hero (model title + serial + copy, "More details", **Laptop specifications** button), responsive **vector laptop** (cairo `lap_draw` inside a `GtkAspectFrame`, no bitmap) **with the live battery % overlaid on its screen**, NVIDIA eye mark image on the GPU card, Featured quick links, Warranty card (honest: managed on asus.com), four blue-badged stat tiles (Battery / CPU load+temp / Memory / Disk), System-info + Software-update cards (GNOME Software launch), then the six functional card areas. |
| `src/ui/theme.css` / `theme-dark.css` | All component styles: `.overline` micro-headers, `.iconbadge` tile icons, `.primarybtn` filled CTAs, `.avring` avatar rings, `.langpill`, `button.flat`, thin scrollbars, tile/card hover elevation — every class defined in BOTH skins with identical names. |
| `src/ui/ring.cpp` | Cairo doughnut gauge; big % / temp text centered as one block, small captions moved out to real labels. |
| `src/ui/spark.cpp` | Cairo temperature trend line; x-scale follows collected points, zero-° pushes are skipped upstream. |
| `src/ui/pages.cpp` | Battery / GPU / Display / Performance / System / Apps / Settings / **Customer support** pages as Device-Settings-style `.accentcard` sections (description left, controls right; red honest notice on the fan profiles), each opened by a MyASUS group label. **Performance** shows the live profile plus the configured AC/battery profiles separately, applies changes through asusd (helper fallback) with a spinner and read-back verification. **GPU** shows the live MUX value, the three modes (Eco disabled with an explanation), NVIDIA telemetry, a queued-change notice and a confirmation dialog for Ultimate. Honest-state logic (unknown values show "—"; radios cleared when the source is unreadable; refresh never yanks a slider for 3 s after a user drag). Settings hosts the theme switch; Display hosts Eye-care and E-Reading grayscale; Support hosts real links + on-demand diagnostics. |
| `src/ui/mcui.cpp` / `mcui.h` | **MyASUS UI kit** — reusable constructors (`mc_page`, `mc_panel`, `mc_card`, `mc_group`, `mc_feature`, `mc_badge`, `mc_row`, `mc_switch`, `mc_primary`, `mc_link`, `mc_pill`) plus `mc_gray_new`/`mc_gray_set` (grayscale E-Reading wrapper). Every component is styled by class names shared by both skins. |
| `src/ui/specs.cpp` / `.h` | "Laptop specifications" modal: one honest snapshot (device/model/BIOS, CPU, RAM + NVMe + disk, NVIDIA GPU + VRAM, display mode, battery design/full/health/cycles, OS + kernel) from the existing read-only probes; unavailable values show "—". Ends with **App support on this laptop**: a live *Supported* / *Not available* row for each control (profiles, charge limit, charging policy, MUX, dGPU disable, keyboard backlight, fans, temperatures, software dimming, night light, polkit helper). |
| `src/ui/theme.css` | Light MyASUS skin, **palette-exact**: bg `#F5F7FA`, cards `#FFFFFF`, accent `#006CE1`/`#005BC4`, pale selection `#EAF3FE`, tint `#F0F6FF`, text `#202939`/`#7184A1`, borders `#DCE7F5`, success/warning/error `#16A34A`/`#F59E0B`/`#EF4444`. No other colour is used; tints are those same colours at low alpha. Card radius 16, button radius 12, shadow `0 4px 16px rgba(36,65,105,0.08)`. |
| `src/ui/theme-dark.css` | Dark companion skin (same class names, same twelve colours): canvas `#202939`, text `#FFFFFF`/`#DCE7F5`, surfaces and borders are those palette colours with alpha, accent stays `#006CE1`/`#005BC4`. |
| `src/ui/theme.cpp` / `.h` | Light/dark *choice*: owner of the preference keyfile (`~/.config/asus-control-center/settings.conf`), the matching stylesheet path, and the cairo colors per mode. The header moon/sun button and the Settings switch both call `acc_ui_theme_toggle()` (rebuilds the window in the new skin). |
| `Makefile` | `make` → one GCC command, `-Wall -Wextra`, 0 warnings; `make test` runs `--self-test`. |
| `data/acc-helper` | Installed at `/usr/local/bin/acc-helper` by `install.sh`. Whitelist-only bash: every action AND value hard-coded, no eval, no variable expansion into commands. |
| `data/*.policy` / `*.desktop.in` / icon | Polkit action (visible-session auth) / desktop entry / app icon. |
| `docs/security.md` | Security audit (M2 edition). |
| `docs/system-report.md` | Saved `--specs` snapshot of this machine (2026-10-27) plus the mapping "capability → app page". |
| `docs/gpu-mux.md` | Why a MUX write is refused in-session (`-EIO`, WMI result `0x0`), why asusd queues it, who applies it at shutdown, how to verify and how to revert. Upstream source citations. |
| `docs/portability.md` | How MyASUS stays model-agnostic (SCI driver + runtime feature list) and how this app mirrors that with Linux interfaces, discovery rules and capability gating. |
| `docs/support.md` | ASUS laptop reference list: support tiers per family/SKU, typical specs, unsupported ARM/Chromebook devices, and the live per-unit verification commands. Documentation only — the app never reads it and gates features on capability probes instead. |
| `docs/presentation.html` | Browser presentation (bilingual). |

## Build & run

```sh
cd asus-control-center
make            # compile (0 warnings expected)
./asus-control-center --self-test   # read-only hardware printout
./asus-control-center               # run the GUI
```

 Enable debug refresh prints: `ACC_DEBUG=1 ./asus-control-center`.

## Stable page IDs

Sidebar rows, stack pages, and the dashboard ">" buttons all use the same
identifiers: `home, battery, gpu, display, performance, system, apps,
settings` — nav↔page is 1:1 (machine-checked).

## What is live vs. needs approval (M2)

| Control | Status |
|---|---|
| All gauges, labels, trend line (read-only telemetry) | ✅ Live |
| OLED flicker-free dim slider | ✅ Live (Soft Brightness Plus GSettings key) |
| Login page / remembered username | ✅ Local only (salted SHA-256; no plaintext password) |
| Laptop specifications sheet | ✅ Live, read-only snapshot of the real probes |
| Charge limit 60/80/100 + charging policy | ✅ Live via polkit helper (`set-charge`, `set-charge-mode`) |
| Performance profile (Quiet/Balanced/Performance) | ✅ Live via **asusd** (`PlatformProfile`, verified by read-back); helper `set-perf` only when asusd is absent. AC/battery profiles and automatic switching are shown as read-only facts. |
| GPU MUX (Hybrid/Ultimate) | ✅ Requested through the helper, queued through asusd when the EC refuses; applies at shutdown + restart. Never automatic; Ultimate asks first. |
| Keyboard LED level 0–3 | ✅ Live (`set-kbd`) when the LED node exists |
| NVIDIA telemetry (util/temp/VRAM/power/display output) | ✅ Read-only, live from `nvidia-smi`; 0 % never means "disabled" |

## Known hardware limits on N6506CU (honest states)

- Fans: **monitoring only** — hwmon exposes RPMs but no manual control;
  the UI does not pretend otherwise.
- `charge_mode` (Standard/Balanced/Max-lifespan) is read via helper file
  semantics; the readable value is displayed, "—" when unreadable.
- Unknown charge threshold (>0, <100, not 60/80) → **all limit radios
  cleared** and the limit row shows "—".
- GPU page shows driver state (Loaded/Not loaded from `/proc/modules`)
  separately from the live MUX mode; one is never inferred from the other.
- This BIOS exposes **no `dgpu_disable`** attribute, so "Integrated only
  (Eco)" is shown disabled with the reason; only Hybrid and Ultimate are
  switchable. Unloading the NVIDIA driver is no longer offered as a mode.
- The MUX write is refused in-session by the EC; asusd queues it for
  `asus-shutdown.service`. The UI says "queued" (with the shutdown-hook
  state) and never claims the live mode changed before a restart.
- `asusd` and `tuned` both own `platform_profile`; if another daemon
  re-applies a profile, the app shows the value the firmware actually
  reports at the next refresh instead of the last click.
- Values the probes cannot read are shown as `—`, never as 0.
