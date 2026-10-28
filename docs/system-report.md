# System report — ASUS Vivobook Pro 15 N6506CU

Saved snapshot of `./asus-control-center --specs` (read-only) plus the
conclusion: **what this machine exposes, and where each item lives in the app.**

- Captured: **2026-10-27**
- App build: local `asus-control-center`, 0 warnings
- Kernel: `7.2.12-200.fc44.x86_64`
- NVIDIA driver: `615.71.09`

---

## Raw report

```text
==========================================================
  ASUS Control Center — laptop report (read-only)
==========================================================

DEVICE
  Model            ASUS Vivobook Pro 15 N6506CU
  Vendor           ASUSTeK COMPUTER INC.
  Serial           —
  BIOS             N6506CU.309 (10/08/2025)
  Board            N6506CU

PROCESSOR
  CPU              Intel(R) Core(TM) Ultra 9 285H
  Threads          16
  Architecture     x86_64

MEMORY & STORAGE
  RAM              22.8 GB
  NVMe             MTFDKBA1T0QGN-1BN1AABGA
  Disk (/)         117 / 607 GB

GRAPHICS
  GPU vendor       NVIDIA
  GPU              NVIDIA GeForce RTX 4050 Laptop GPU
  Video memory     6 GB total (0.0 GB in use)
  NVIDIA module    loaded
  MUX mode         1 (hybrid)
  MUX interface    asus-armoury (firmware-attributes)
  Restart pending  no

DISPLAY
  Panel            2880 × 1620 @ 120 Hz
  Backlight        294 / 400

BATTERY
  Charge           47% (Discharging)
  Capacity         75.0 Wh design / 68.0 Wh full
  Health           91%
  Cycles           1171
  Charge limit     100%

SYSTEM
  OS               "Fedora Linux 44 (Workstation Edition)"
  Kernel           7.2.12-200.fc44.x86_64

ASUSD SERVICE
  Service          running
  Profile (live)   balanced
  Profile on AC    performance
  Profile on bat   quiet
  Auto AC/battery  yes
  MUX (asusd)      1 (hybrid)
  MUX queued       0 (ultimate/dGPU)
  asus-shutdown    disabled

SUPPORTED CONTROLS
  OK   Performance profiles       quiet balanced performance
  OK   Charge limit               60 / 80 / 100 % via polkit helper
  OK   Charging policy            standard / balanced / max lifespan
  OK   GPU MUX switch             ultimate / hybrid (applies on reboot)
  --   dGPU disable               not exposed
  OK   Keyboard backlight         levels 0–3
  OK   Fan sensors                RPM monitoring only (no manual control)
  OK   Temperature sensors        CPU 70.0 °C, board 27.8 °C
  OK   Software dimming           current 55%, min 10%
  OK   Night light                GSettings available
  OK   Polkit helper (writes)     /usr/local/bin/acc-helper installed

Notes
  • Values marked — could not be read on this machine.
  • Writes go through asusd or the polkit helper (no root here).
  • Nothing in this report writes to the hardware.
```

---

## What is available here, and where it lives

| Capability | Status | Control in the app | Page |
|---|---|---|---|
| Performance profiles | live Balanced; AC Performance; battery Quiet | three radios + separate live/AC/battery rows via asusd (verified) | Performance |
| Charge limit | 60 / 80 / 100 % | option cards + active-limit row | Battery |
| Charging policy | standard / balanced / max lifespan | read-only status row (EC-driven) | Battery |
| GPU MUX switch | hybrid ↔ ultimate | two radios + confirmation; request queued through asusd when the EC refuses, applied at shutdown/restart | GPU |
| NVIDIA telemetry | util/temp/VRAM/power/output | read-only rows from `nvidia-smi` | GPU |
| dGPU disable | **not exposed** on this BIOS | shown disabled with the reason — nothing is faked | GPU |
| Keyboard backlight | levels 0–3 | four level cards | Performance |
| Fan sensors | RPM only, no manual control | live RPM + trend, honest "no fan control" note | Performance |
| Temperature sensors | CPU + board | live ring / trend | Home + Performance |
| Software dimming | Soft Brightness Plus, ≥ 10 % | dim slider | Display |
| Night light | GSettings available | eye-care switch | Display |
| asusd | running (asusctl 6.5.0) | profile writes, MUX queue | — |
| Polkit helper | installed | writes when asusd is absent | — |

### Deliberately not available

- **dGPU disable** (`dgpu_disable`) — this BIOS does not export the flag, so the
  app shows "Integrated only (Eco)" disabled with that explanation and does not
  emulate it by unloading the NVIDIA driver.
- **Manual fan speed** — the EC exposes RPM reading only; the Performance page
  says so in red instead of pretending.
- **Serial number** — `/sys/class/dmi/id/product_serial` is unreadable for a
  normal user, so the report and the hero show `—`.

---

## How to reproduce

```bash
cd asus-control-center
./asus-control-center --specs      # this report
./asus-control-center              # GUI: Laptop specifications → App support
```

Inside the app the same list is under **Laptop specifications → App support on
this laptop**; every *Supported* row there is a shortcut to the page that owns
the control.

See also: [`support.md`](support.md) for the ASUS family/SKU reference list.
