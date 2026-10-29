# ASUS Control Center

**A community-built Linux control center for ASUS laptops.**

ASUS Control Center brings hardware information and available laptop controls into one GTK application. It is an independent open-source project inspired by the convenience of MyASUS; it is **not affiliated with or endorsed by ASUS**.

> **Compatibility notice:** The model list below is a reference for testing, not a guarantee. The app detects interfaces exposed by the running Linux system and firmware. Available controls vary by exact SKU, BIOS, kernel, drivers, and installed services. A model is only **verified** when relevant features have been tested on that machine.

## Features

The app can expose these features when hardware and Linux interfaces support them:

- **System information:** CPU, memory, storage, graphics, display, OS, and battery information.
- **Battery care:** Charge thresholds and charging policies where exposed by the embedded controller.
- **Performance profiles:** Quiet, Balanced, and Performance through supported platform-profile or ASUS interfaces.
- **GPU modes:** ASUS GPU MUX switching on compatible systems; changes may require a reboot or shutdown.
- **Keyboard lighting:** Backlight controls where supported.
- **Monitoring:** Available temperatures, fan readings, and NVIDIA telemetry.
- **Display comfort:** Software dimming and night-light integration.
- **Privileged actions:** A dedicated helper and Polkit authorization for supported system changes.
- **ASUS integration:** Uses `asusd`/`asusctl` where available.

Not every feature is available on every laptop. For example, a system may expose Hybrid and Ultimate GPU modes without exposing a firmware control to disable the discrete GPU completely.

## Compatibility

### Development and test reference

| Device | Status |
|---|---|
| ASUS Vivobook Pro 15 **N6506CU** | Primary development and test machine |
| Other models listed below | Expected/possible compatibility; requires device-level testing unless explicitly marked tested |

### Full-support candidates — dedicated GPU and possible MUX

The project compatibility notes list these families as candidates for a broad feature set. This is **not a claim that every configuration has been tested**.

**ROG Zephyrus**
- G14 — `GA403`
- G16 — `GU605`
- M16 — `GU604`
- Duo 16 — `GX650`
- Flow X13 — `GV302`
- Flow X16 — `GV601`

**ROG Strix**
- G16 — `G614`
- G18 — `G814`
- Scar 16 — `G634`
- Scar 18 — `G834`
- G17 — `G713 / G714`
- Scar 17 — `G733 / G734`

**TUF Gaming**
- A14 — `FA401`
- A15 — `FA506 / FA507`
- A16 — `FA607`
- A17 — `FA706 / FA707`
- F15 — `FX506 / FX507`
- F16 — `FX607`
- F17 — `FX706 / FX707`
- Dash F15 — `FX516`

**ProArt**
- P16 — `H7606`
- PX13 — `HN7306`
- Studiobook 16 — `H7604`

**Vivobook and Zenbook**
- Vivobook Pro 16 — `N7606`
- Vivobook Pro 16X — `K6604`
- Vivobook Pro 15 — `N6506`
- Vivobook Pro 14 — `N3404`
- Zenbook Pro 14 — `UX6404`
- Zenbook Pro Duo 15 — `UX582`
- Zenbook 14X OLED — `UX3404`

### Standard-support candidates — usually no GPU MUX

These families may benefit from battery controls, platform profiles, keyboard lighting, monitoring, or display-comfort features where their firmware exposes the relevant interfaces.

- Zenbook S14 — `UX5406`
- Zenbook S16 — `UM5606`
- Zenbook 14 — `UX3405`
- Zenbook 14 OLED — `UX3402`
- Zenbook Duo — `UX8406`
- Vivobook S14 — `S5406`
- Vivobook S16 — `S5606`
- Vivobook 14 — `X1404`
- Vivobook 15 — `X1504`
- Vivobook 16 — `X1605`
- Vivobook 14X OLED — `X1405`
- Vivobook Go 14 — `E1404`
- Vivobook Flip 14 — `TP3402`
- ExpertBook B5 — `B5404`
- ExpertBook B9 — `B9403`
- ExpertBook B14 / B15 / B16

### Basic-support candidates — older models

Expect a more limited set of controls, depending heavily on firmware and kernel support.

- **Zenbook:** `UX430UNR`, `UX330UAK`, `UX410`, `UX530`
- **Vivobook:** `X512`, `X712`, `X509`, `S14 S406`
- **TUF:** some revisions of `FX505`, `FX705`, and `FA506`
- **ROG:** `G531`, `G731`, Zephyrus `GA401`, `GA502`, `GA503`

### Models/platforms not expected to expose the required laptop controls

The project notes identify these as unsupported for hardware control through the current ASUS WMI approach:

- Snapdragon/ARM models: Zenbook A14 `UX3407`, Vivobook S15 `S5507`, Vivobook S16 `S3607QA`, and ProArt PZ13 `HT5306`
- ASUS Chromebooks
- Older Transformer/T100/T200 detachables
- ROG Ally and Ally X handhelds
- ASUS desktops and mini-PCs

The application may still display general system information on such systems, while unavailable hardware controls should remain disabled. This list can change as Linux support evolves.

For more detail, see [the full compatibility notes](docs/support.md).

## Check compatibility on your laptop

Run these commands in a terminal to inspect common interfaces:

```bash
cat /sys/firmware/acpi/platform_profile_choices 2>/dev/null
cat /sys/class/power_supply/BAT0/charge_control_end_threshold 2>/dev/null
cat /sys/devices/platform/asus-nb-wmi/charge_mode 2>/dev/null
cat /sys/class/firmware-attributes/asus-armoury/attributes/gpu_mux_mode/current_value 2>/dev/null
cat /sys/devices/platform/asus-nb-wmi/gpu_mux_mode 2>/dev/null
cat /sys/class/leds/asus::kbd_backlight/brightness 2>/dev/null
asusctl profile get 2>/dev/null
```

A readable interface is a useful clue, but it does **not** prove that every write operation works. Test each control carefully and record the exact laptop model, BIOS version, Linux distribution, kernel, and result. Do not assume that two configurations with the same family name have identical features.

## Requirements

- Linux
- C++20 compiler and `make`
- GTK 4 and libadwaita development packages
- Polkit for privileged operations
- `asusd`/`asusctl` for controls that use ASUS Linux services, where supported

Package names and dependencies vary by distribution. Fedora is the current development environment.

## Build from source

```bash
git clone https://github.com/Mostafamomen/asus-control-center.git
cd asus-control-center
make
```

Run the application:

```bash
./asus-control-center
```

Run the read-only self-test:

```bash
./asus-control-center --self-test
```

Run tests if the project Makefile provides the test target:

```bash
make test
```

## Install

Review the installer, helper, and Polkit policy before installing system-level components:

```bash
less install.sh
less src/control/ctl.cpp
find data -maxdepth 2 -iname '*policy*' -print
./install.sh
```

Installation may request administrator authorization. Only install software you trust. Confirm that the helper path installed by the script matches the path compiled into the application.

## Contributing compatibility reports

Testing on additional ASUS laptops is especially useful. When opening an issue, include:

- Exact product/model code (not only “ROG” or “Vivobook”)
- BIOS version
- Linux distribution and kernel version
- Whether `asusd` is installed and running
- Which controls were tested, and whether the change actually took effect
- Relevant self-test output and logs, with personal information removed

Please distinguish **interface detected**, **write accepted**, and **behavior verified**. This helps the project build a reliable compatibility matrix.

## Project status

ASUS Control Center is under active development. The Vivobook Pro 15 N6506CU is the main development reference. Compatibility entries for other models are provisional until confirmed by reproducible device-level tests.

## License

See [LICENSE](LICENSE) for the license text.

---

**ASUS Control Center** — community-built laptop controls for ASUS on Linux.
