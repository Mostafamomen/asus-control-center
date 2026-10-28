# Portability — how one build covers every supported laptop

## What MyASUS does (and why it looks like this)

MyASUS is **one application** with **no per-model code paths in the UI**. The
model-specific part lives in a separate driver, *ASUS System Control Interface*
(SCI, V1 / V3). ASUS documents the consequence plainly: if the SCI driver is
missing or too old, MyASUS "only supports partial functions", and the feature
table in [FAQ 1045676](https://www.asus.com/us/support/faq/1045676) shows which
features exist per driver generation:

| MyASUS 4.0 feature | No SCI | SCI V1 | SCI V3 |
|---|---|---|---|
| Home · System info · Announcements · Feedback | ✓ | ✓ | ✓ |
| One-click Settings · System Diagnosis · System Update | — | — | ✓ |
| Device Settings | — | ✓ | ✓ |
| Customer support / FAQ / coupons | ✓ | ✓ | ✓ |

The same idea appears across the Windows stack: the SCI driver exposes a device
interface, MyASUS queries it for **capabilities**, draws only those panels, and
hides the rest. ASUS's support site then publishes model lists per driver
generation — the list is documentation, not runtime logic.

**Takeaway:** portability comes from (1) a stable hardware interface, (2) runtime
capability discovery, (3) honest partial support. Not from a table of model
names inside the app.

## The Linux equivalent

Linux already ships the "SCI" as kernel subsystems. This app queries them at
start-up instead of talking to an ASUS driver:

| MyASUS / Windows | Linux interface | Used for |
|---|---|---|
| ASUS System Control Interface | **`asus-armoury` firmware attributes** (`/sys/class/firmware-attributes/asus-armoury/attributes/<name>/current_value`), falling back to `asus-nb-wmi` / `asus-wmi` (`/sys/devices/platform/asus*`) | MUX, charge policy, dGPU flag |
| ACPI platform profile | `/sys/firmware/acpi/platform_profile_choices` | performance profiles |
| Battery class driver | `/sys/class/power_supply/<bat>` | charge %, Wh, cycles, charge limit |
| LED class | `/sys/class/leds/*kbd_backlight*/brightness` | keyboard backlight |
| EC sensors | `/sys/class/hwmon/*` | fan RPM, temperatures |
| GPU driver | `nvidia-smi`, `/proc/modules` | dGPU telemetry, driver state |
| asusctl service (`asusd`, optional) | system bus `xyz.ljones.Asusd` | profiles (fan/EPP presets), GPU MUX queue |
| Display driver | `/sys/class/drm/*eDP*/edid`, `/sys/class/backlight` | resolution, refresh, backlight |

## Portability rules in this codebase

1. **No SKU checks.** Nothing compares `product_name` to a list. DMI is shown to
   the user, never used to gate a feature.
2. **Discover, don't assume** (`src/probe/devs.cpp`):

   | What varies | Discovery |
   |---|---|
   | Battery name `BAT0` / `BAT1` / `BATT` | scan `power_supply` for `type=Battery` + `capacity` |
   | Charge-limit file | `charge_control_end_threshold`, else `charge_stop_threshold` |
   | WMI node | `asus-armoury` firmware attribute first (kernel ≥ 6.17), then `asus-nb-wmi`, `asus-wmi`, then any `asus*` platform dir exposing a known file |
   | Keyboard LED | any LED containing `kbd_backlight` |
   | Internal panel | any DRM connector containing `eDP` |
   | Battery units | `energy_*` (µWh) or `charge_*` × `voltage_*` (µAh × µV) |

3. **Capability gating in the UI.** A control is enabled only when its interface
   exists: MUX radios need `gpu_mux_mode`, charge cards need a charge-limit file,
   profile cards need `platform_profile_choices`, kbd cards need a kbd LED.
   Unsupported controls are shown disabled with an honest note, or not at all.
4. **The writable interface wins.** Since kernel 6.17 ASUS moved these settings
   to the `asus-armoury` firmware-attribute class. The old platform files may
   still exist but are read-only (`charge_mode` is `-r--r--r--` on Fedora 44,
   kernel 7.2) or a deprecated alias, so writing to them looked successful while
   the firmware never changed. `acc_dev_armoury_file()` returns the armoury
   `current_value` path when it exists, and both the app and the helper prefer
   it; the legacy path is only a fallback for older kernels.
5. **Writes are verified.** The helper reads the attribute back after writing
   and exits `4` when the firmware refused the value; the app then says so
   instead of silently snapping the control back. `pending_reboot` is shown as
   "Restart the laptop to apply the new GPU mode".
6. **Writes go through one of two fixed channels.** If `asusd` (from `asusctl`)
   owns the bus name, the app asks it via D-Bus on fixed paths: it is the
   service ASUS tooling uses, it owns the firmware writes, and it reapplies fan
   curves/EPP after a profile change. Otherwise the app falls back to
   `data/acc-helper` over polkit, which performs the same discovery as root and
   whitelists every action and value. Neither path accepts a free-form path or
   command from the UI, and there is no model list.
7. **Read-only fallback.** If a probe fails, the value is `—`; nothing is
   estimated or cached as if it were real.
8. **A request is not a state.** Writes are verified by reading the interface
   back (helper read-back, or asusd's properties polled for ~300 ms). The GPU
   MUX is special: this EC refuses an in-session write, so asusd queues the
   value for `asus-shutdown.service`. The UI keeps the radio on the live value
   and shows the queued one as a notice, including whether the shutdown hook is
   enabled.

## What that means per tier

| Tier (see `support.md`) | What the app discovers | Result |
|---|---|---|
| Full | WMI node + MUX + battery cap + kbd LED + hwmon | every page active |
| Standard | WMI node + battery cap + kbd LED, no `gpu_mux_mode` | MUX radios disabled, rest live |
| Basic | battery cap (maybe only `charge_stop_threshold`), hwmon | charge limit, temps, dimming |
| None | no `asus*`, no battery | read-only system info; control cards honest/empty |

## Verifying portability on a machine

```bash
./asus-control-center --specs          # full report + capability list
ls /sys/class/power_supply/*/type      # which battery node exists
ls /sys/class/firmware-attributes/asus-armoury/attributes/   # new interface
ls /sys/devices/platform/ | grep asus  # legacy WMI node
ls /sys/class/leds | grep kbd          # keyboard LED name
busctl --system tree xyz.ljones.Asusd  # asusd objects (if installed)
asusctl profile get                    # live profile + AC/battery profiles
asusctl armoury list                   # charge_mode / gpu_mux_mode choices
```

`--specs` prints `MUX interface`, so you can see at a glance whether the value
comes from `asus-armoury` or from the legacy platform file, plus an **ASUSD
SERVICE** block with the live profile, the configured AC/battery profiles, the
MUX value and queue, and whether `asus-shutdown.service` is enabled.

If a path is missing, the app treats the matching feature as unavailable rather
than writing to a guessed path. That is the same contract MyASUS gives with its
"partial features" notice — except here the list is generated at runtime, so a
BIOS or kernel update needs no new release.

## Deliberate limits

- **Windows-only features** (System Update, MyASUS deals, ScreenPad) have no
  Linux backend and are not faked; the app links to ASUS for those.
- **Fan curves / manual fan speed** are not exposed by the EC on these models;
  only RPM is read. Selecting a *performance profile* changes the firmware's
  power/thermal preset — it is not a fan-speed control and not a GPU switch.
- **Disabling the dGPU** needs a firmware attribute (`dgpu_disable`) that many
  BIOSes, including the N6506CU, do not expose. The app shows that mode as
  unavailable instead of emulating it by unloading the NVIDIA driver.
- **ARM (Snapdragon X) and Chromebooks** have no `asus-nb-wmi`; they get the
  read-only tier shown above. See `support.md`.
