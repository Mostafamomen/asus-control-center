# ASUS laptop support list

**ASUS Control Center has no model whitelist.** It does not look up your SKU in a
table and it never hides a control because "this model isn't listed". At start-up
the app probes the firmware interfaces that Linux exposes and enables exactly the
controls it finds. That is why the same build works on a 2018 Vivobook and a 2025
ROG Strix, and why a BIOS update can add or remove features without a new release.

So this document is **reference documentation, not an allow-list**. Use the table
to get a rough expectation, then use the live check (§ *Verify a specific unit*)
for the truth on your machine.

---

## Support tiers used here

| Tier | What works in the app |
|---|---|
| **Full** | Everything: charge limit + charging policy, performance profiles, GPU MUX switch, NVIDIA telemetry, keyboard backlight, fan/temperature sensors, software dimming, night light |
| **Standard** | Charge limit + policy, performance profiles, keyboard backlight, sensors, software dimming, night light — but **no GPU MUX** (iGPU-only machines, or dGPU without a MUX switch) |
| **Basic** | Battery charge limit (where the EC exposes it), temperatures, fans, software dimming. Older firmware, no profiles, often no keyboard LED |
| **None** | No `asus-nb-wmi` / ACPI platform profile, or no battery: ARM, Chromebooks, detachables, handhelds, desktops |

Everything in the app is **read-only until you move a control**. Writes go
through **asusd** (the service shipped with `asusctl`, when it is running) or,
if it is not installed, through `/usr/local/bin/acc-helper` over polkit.
Nothing in this list changes that.

---

## Full support — dGPU with MUX / full EC surface

| Model (family code) | Year | CPU | GPU | Display | RAM / SSD |
|---|---|---|---|---|---|
| ROG Zephyrus G14 **GA403** | 2024 | Ryzen 9 8945HS | RTX 4060 / 4070 | 14″ 3K OLED 120 Hz | 16–32 GB / 1 TB |
| ROG Zephyrus G16 **GU605** | 2024 | Core Ultra 9 185H | RTX 4060–4090 | 16″ 2.5K OLED 240 Hz | 16–32 GB / 1–2 TB |
| ROG Zephyrus M16 **GU604** | 2023 | Core i9-13900H | RTX 4070–4090 | 16″ 2.5K mini-LED 240 Hz | 16–32 GB / 1–2 TB |
| ROG Zephyrus Duo 16 **GX650** | 2023 | Ryzen 9 7945HX | RTX 4080 / 4090 | 16″ dual 4K | 32 GB / 2 TB |
| ROG Flow X13 **GV302** | 2023 | Ryzen 9 7940HS | RTX 4050 / 4060 | 13.4″ 2.5K 165 Hz | 16 GB / 1 TB |
| ROG Flow X16 **GV601** | 2023 | Ryzen 9 7940HS | RTX 4070 | 16″ 2.5K mini-LED 240 Hz | 16–32 GB / 1 TB |
| ROG Strix G16 **G614** | 2024 | Core i9-14900HX | RTX 4060–4080 | 16″ 2.5K 240 Hz | 16–32 GB / 1 TB |
| ROG Strix G18 **G814** | 2024 | Core i9-14900HX | RTX 4070–4090 | 18″ 2.5K 240 Hz | 32 GB / 1–2 TB |
| ROG Strix Scar 16 **G634** | 2024 | Core i9-14900HX | RTX 4080 / 4090 | 16″ 2.5K mini-LED 240 Hz | 32 GB / 1–2 TB |
| ROG Strix Scar 18 **G834** | 2024 | Core i9-14900HX | RTX 4090 | 18″ 2.5K 240 Hz | 32 GB / 2 TB |
| ROG Strix G17 **G713 / G714** | 2023 | Ryzen 9 7940HX / 7945HX | RTX 4060 / 4070 | 17.3″ 2.5K 240 Hz | 16–32 GB / 1 TB |
| ROG Strix Scar 17 **G733 / G734** | 2023 | Ryzen 9 7945HX3D | RTX 4080 / 4090 | 17.3″ 2.5K 240 Hz | 32 GB / 2 TB |
| TUF Gaming A14 **FA401** | 2024 | Ryzen 9 8945HS | RTX 4060 | 14″ 2.5K 165 Hz | 16 GB / 1 TB |
| TUF Gaming A15 **FA506 / FA507** | 2020–2023 | Ryzen 5 / 7 / 9 | RTX 3050–4070 | 15.6″ FHD 144 Hz | 8–16 GB / 512 GB–1 TB |
| TUF Gaming A16 **FA607** | 2024 | Ryzen 9 8945HS | RTX 4070 | 16″ 2.5K 165 Hz | 16 GB / 1 TB |
| TUF Gaming A17 **FA706 / FA707** | 2020–2023 | Ryzen 7 / 9 | RTX 4060 | 17.3″ FHD 144 Hz | 16 GB / 512 GB–1 TB |
| TUF Gaming F15 **FX506 / FX507** | 2020–2023 | Core i5 / i7 / i9 | RTX 3050–4070 | 15.6″ FHD 144 Hz | 8–16 GB / 512 GB–1 TB |
| TUF Gaming F16 **FX607** | 2024 | Core i7-13650HX | RTX 4060 | 16″ FHD+ 165 Hz | 16 GB / 1 TB |
| TUF Gaming F17 **FX706 / FX707** | 2020–2023 | Core i7 / i9 | RTX 4060 | 17.3″ FHD 144 Hz | 16 GB / 512 GB–1 TB |
| TUF Dash F15 **FX516** | 2021 | Core i7-11370H | RTX 3060 | 15.6″ FHD 240 Hz | 16 GB / 512 GB–1 TB |
| ProArt P16 **H7606** | 2024 | Ryzen AI 9 HX 370 | RTX 4060 / 4070 | 16″ 4K OLED | 32–64 GB / 1–2 TB |
| ProArt PX13 **HN7306** | 2024 | Ryzen AI 9 HX 370 | RTX 4060 | 13.3″ 3K OLED | 32 GB / 1 TB |
| ProArt Studiobook 16 **H7604** | 2023 | Core i9-13980HX | RTX 4070 | 16″ 3.2K OLED | 32 GB / 1–2 TB |
| Vivobook Pro 16 **N7606** | 2023 | Core i9-13980HX | RTX 4070 | 16″ 3.2K OLED 165 Hz | 16–32 GB / 1 TB |
| Vivobook Pro 16X **K6604** | 2023 | Core i9-13980HX | RTX 4070 | 16″ 3.2K OLED | 32 GB / 1 TB |
| **Vivobook Pro 15 N6506 (this machine)** | 2024–2025 | Core Ultra 9 185H / 285H | RTX 4050 / 4060 | 15.6″ 3K OLED | 16–32 GB / 1 TB |
| Vivobook Pro 14 **N3404** | 2023–2024 | Core Ultra 7 155H | RTX 3050 / 4050 | 14″ 2.8K OLED | 16 GB / 1 TB |
| Zenbook Pro 14 **UX6404** | 2023 | Core i9-13900H | RTX 4060 | 14.5″ 2.8K OLED | 16–32 GB / 1 TB |
| Zenbook Pro Duo 15 **UX582** | 2022 | Core i9-12900H | RTX 3070 Ti | dual 4K OLED | 32 GB / 1–2 TB |
| Zenbook 14X OLED **UX3404** | 2023 | Core i9-13900H | RTX 3050 | 14.5″ 2.8K OLED | 16 GB / 1 TB |

> On the reference machine (N6506CU) everything is green **except**
> `dgpu_disable`, which this BIOS does not expose. That missing item is normal:
> the app shows "Integrated only (Eco)" disabled with the reason and keeps the
> two modes this firmware really has (Hybrid and Ultimate). The MUX write is
> refused in-session by the EC, so asusd queues it and
> `asus-shutdown.service` applies it during shutdown — enable that unit if you
> want the queue to survive.

---

## Standard support — no MUX (most iGPU and thin-and-light dGPU models)

| Model | Year | CPU | GPU | Display | RAM / SSD |
|---|---|---|---|---|---|
| Zenbook S14 **UX5406** | 2024 | Core Ultra 7 258V | Arc 140V | 14″ 3K OLED | 16–32 GB / 1 TB |
| Zenbook S16 **UM5606** | 2024 | Ryzen AI 9 HX 370 | Radeon 890M | 16″ 3K OLED | 16–32 GB / 1 TB |
| Zenbook 14 **UX3405** | 2024 | Core Ultra 7 155H | Arc | 14″ FHD+ OLED | 16 GB / 1 TB |
| Zenbook 14 OLED **UX3402** | 2022 | Core i7-1260P | Iris Xe | 14″ 2.8K OLED | 16 GB / 512 GB |
| Zenbook Duo **UX8406** | 2024 | Core Ultra 9 185H | Arc | dual 14″ 3K OLED | 32 GB / 1 TB |
| Vivobook S14 **S5406** | 2024 | Core Ultra 7 155H | Arc | 14″ FHD+ OLED | 16 GB / 1 TB |
| Vivobook S16 **S5606** | 2024 | Core Ultra 9 185H | Arc | 16″ 3.2K OLED | 16–32 GB / 1 TB |
| Vivobook 14 **X1404** | 2023 | Core i5-1335U | Iris Xe | 14″ FHD | 8–16 GB / 512 GB |
| Vivobook 15 **X1504** | 2023 | Core i5-1335U | Iris Xe | 15.6″ FHD | 8–16 GB / 512 GB |
| Vivobook 16 **X1605** | 2023 | Core i5-13500H | Iris Xe | 16″ FHD+ | 16 GB / 512 GB |
| Vivobook 14X OLED **X1405** | 2023 | Core i5-13500H | RTX 2050 | 14″ 2.8K OLED | 16 GB / 512 GB |
| Vivobook Go 14 **E1404** | 2023 | Ryzen 5 7520U | Radeon 610M | 14″ FHD | 8 GB / 256–512 GB |
| Vivobook Flip 14 **TP3402** | 2022 | Core i5-1335U | Iris Xe | 14″ FHD+ touch | 8–16 GB / 512 GB |
| ExpertBook B5 **B5404** | 2024 | Core Ultra 5 / 7 | Arc | 14″ FHD+ | 16–32 GB / 512 GB–1 TB |
| ExpertBook B9 **B9403** | 2023 | Core i7-1355U | Iris Xe | 14″ 2.8K OLED | 16–32 GB / 1 TB |
| ExpertBook B14 / B15 / B16 | 2023–2025 | Core i5 / i7 | Iris Xe | FHD | 8–16 GB / 512 GB |

---

## Basic support — older units (2018–2021)

Representative families. Expect battery charge limit (only where the EC exposes
`charge_control_end_threshold`), temperatures, fan RPM and software dimming. No
MUX, often no keyboard backlight, usually no platform profiles.

| Family | Examples |
|---|---|
| Zenbook | **UX430UNR**, **UX330UAK**, **UX410**, **UX530** |
| Vivobook | **X512**, **X712**, **X509**, **S14 S406** |
| TUF | early **FX505 / FX705**, early **FA506** revisions |
| ROG | **G531 / G731**, Zephyrus **GA401 / GA502 / GA503** |

Because these depend heavily on BIOS revision, treat the tier as a hint only and
run the live check.

---

## Not supported — no ASUS WMI on Linux

| Device | Why |
|---|---|
| Zenbook A14 **UX3407** | Snapdragon X (ARM) — no `asus-nb-wmi` |
| Vivobook S15 **S5507** | Snapdragon X Elite (ARM) |
| Vivobook S16 **S3607QA** | Snapdragon X (ARM) |
| ProArt PZ13 **HT5306** | Snapdragon X Plus (ARM) |
| All **Chromebook** models | Different firmware/EC, no ACPI ASUS WMI |
| **Transformer / T100 / T200** and other detachables | Legacy tablet platform |
| **ROG Ally / Ally X** | Handheld, not a laptop |
| ASUS desktops and mini-PCs | No laptop EC / battery interface |

On these the app still opens and shows system information (CPU, RAM, disk,
display, OS), but the hardware-control cards show `—` and are disabled. Nothing
is faked.

---

## Verify a specific unit

The definitive test is the machine itself. From the app, click **Laptop
specifications** (Home hero, System page, or the login page) — the login button
opens a terminal with:

```bash
./asus-control-center --specs
```

Or run the probes directly:

```bash
cat /sys/firmware/acpi/platform_profile_choices
cat /sys/class/power_supply/BAT0/charge_control_end_threshold
cat /sys/devices/platform/asus-nb-wmi/charge_mode
cat /sys/class/firmware-attributes/asus-armoury/attributes/gpu_mux_mode/current_value
cat /sys/devices/platform/asus-nb-wmi/gpu_mux_mode          # legacy kernels
cat /sys/class/leds/asus::kbd_backlight/brightness
ls  /sys/devices/platform/asus-nb-wmi/
asusctl profile get                       # live + AC + battery profiles
asusctl armoury list                      # charge_mode / gpu_mux_mode
systemctl is-enabled asus-shutdown.service
```

A path that exists and is readable means the matching feature is available; a
missing path means the app disables that control instead of pretending.

The same report is inside the app: **Laptop specifications → App support on this
laptop** lists every control with a live *Supported* / *Not available* pill.

---

## Notes

- Values in the tables are **typical configurations**, not a promise about any
  specific retail SKU (ASUS varies CPU/GPU/RAM per region and per year).
- BIOS updates can add or remove interfaces; re-run the check after updating.
- All probes are read-only. The only writes are the ones you trigger, and they
  go through asusd or the polkit helper installed by `./install.sh`.
- This file is documentation. The app never reads it and never gates features on
  it.
