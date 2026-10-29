# MyASUS parity map

This app mirrors the **structure, placement and grouping** of the MyASUS
desktop app, using plain GTK4/libadwaita and free symbolic icons. It does
not ship ASUS artwork, logos or proprietary icon files — every glyph is an
Adwaita symbolic icon chosen to match the MyASUS meaning.

Reference for the Device Settings grouping: ASUS FAQ 1045651
("MyASUS – Device Settings").

## Window frame

| MyASUS | This app | Notes |
|---|---|---|
| Sign-in / account | **Login page first**: first run creates a local account (username + password), later runs sign in with the remembered username | salted SHA-256 stored in `settings.conf`; password never stored |
| Top bar with app name + actions | Header bar: back arrow, theme button, language pill, refresh, menu | |
| Left sidebar: device name, account chip | Sidebar: avatar + "Hello, {username}!", user chip at the bottom | uses the login name, no "Mostafa" placeholder |
| Sidebar sections | "Device settings" / "Preferences" group labels | `.navgroup` CSS |
| Footer slogan | `ASUS · IN SEARCH OF INCREDIBLE` footer bar | |

## Home

| MyASUS block | This app | Data source |
|---|---|---|
| Product title + serial number | Hero: DMI product name + serial, copy button | `/sys/class/dmi/id/*` |
| Device picture with battery | Responsive **vector** laptop (cairo, crisp at any size) with the live battery % on its screen | `BAT0/capacity` |
| Device specifications | Header button **"Laptop specifications"** opens a read-only spec sheet (device, CPU, RAM/storage, GPU, display, battery, OS/kernel) followed by **App support on this laptop** — a live per-control *Supported* / *Not available* list | all live probes |
| Featured quick links | Featured card → Battery / GPU / Display / Performance | navigation |
| Warranty card | Warranty card + "Open support site" | link only (managed by ASUS) |
| Stat tiles (battery, CPU, RAM, storage) | Four blue-badged tiles, red ≥ 90 % | `/sys`, `/proc` |
| System Diagnosis | "System info" card → System page + real diagnostics | live probes |
| Software Update | Update card → GNOME Software (`gnome-software`) | real launcher |

Icon map: `battery-symbolic`, `applications-system-symbolic`,
`drive-harddisk-symbolic`, `view-grid-symbolic`, `software-update-available-symbolic`.

## Device settings — groups

MyASUS groups: **Power & Performance · Audio & Visual · Connectivity ·
Input Device Settings · Privacy & Security · Exclusives**.
Each page here opens with the matching uppercase group label.

### Power & Performance

| MyASUS | This app | Real control |
|---|---|---|
| Battery Care Mode (80 % / 60 % / 100 %) | Battery page, radio cards + home switch | `charge_control_end_threshold` via polkit helper |
| Fan Profile (Quiet / Balanced / Performance) | Performance page, radio cards + live/AC/battery rows | `PlatformProfile` via asusd (helper fallback) |
| MUX Switch / GPU Mode | GPU page, radio cards + confirmation | `gpu_mux_mode` (request; queued through asusd for shutdown). `dgpu_disable` is absent on this BIOS and shown as unavailable. |
| Live fans + temperature | Performance page three-column block | hwmon |

Icons: `battery-symbolic`, `weather-windy-symbolic`, `video-display-symbolic`.

### Audio & Visual

| MyASUS | This app | Real control |
|---|---|---|
| Splendid / brightness | Display page brightness slider | `brightnessctl`-equivalent sysfs write |
| Eye Care | Eye-care switch | GNOME night-light GSettings |
| E-Reading (black & white) | **E-Reading (grayscale)** switch — renders the whole app in grayscale | `GtkSnapshot` Rec. 709 colour matrix |
| OLED Care / Flicker-Free | Notice text only (firmware feature not exposed on Linux) | — |
| Refresh rate / resolution | Read-only row | DRM + display probe |

Icons: `display-brightness-symbolic`, `night-light-symbolic`,
`applications-graphics-symbolic`.

### Connectivity

| MyASUS | This app | Real control |
|---|---|---|
| Wi-Fi SmartConnect etc. | Connectivity card: live Wi-Fi SSID + interface | `iw dev <if> link` |
| Bluetooth | Live radio state | `/sys/class/rfkill/*` |

Icon: `network-wireless-symbolic`.

### Input Device Settings

| MyASUS | This app | Real control |
|---|---|---|
| Keyboard backlight | 0–3 level cards | `asus::kbd_backlight` via helper |
| Touchpad | Touchpad switch | GNOME `send-events` GSettings |

Icons: `input-keyboard-symbolic`, `input-touchpad-symbolic`.

### Privacy & Security / Exclusives

Not exposed by this hardware on Linux (adaptive lock, RGB lighting).
The app shows the real security/warranty links instead of fake toggles.

## Customer Support

| MyASUS | This app |
|---|---|
| Warranty check | Warranty card → asus.com/support |
| Download Center | "Download drivers" → ASUS Download Center |
| Repair status / service centres / contact | Four quick links |
| System diagnosis | "Run diagnostics" — real battery health, CPU temp, disk and RAM checks |

Icons: `security-high-symbolic`, `system-help-symbolic`,
`applications-utilities-symbolic`, `software-update-available-symbolic`.

## Preferences

| MyASUS | This app |
|---|---|
| Settings: language, theme | Settings page: helper status, version, EN/ع, light/dark switch, About card |

## Reusable UI kit

`src/ui/mcui.cpp` + `mcui.h` implement the MyASUS look as a small library:

- `mc_page`, `mc_panel`, `mc_card`, `mc_group`, `mc_title`
- `mc_feature` (icon | text | controls three-column block)
- `mc_badge`, `mc_row`, `mc_switch`, `mc_primary`, `mc_link`, `mc_pill`
- `mc_gray_new` / `mc_gray_set` (grayscale "E-Reading" wrapper)

All styling lives in `theme.css` and `theme-dark.css` under identical class
names, so anything built with the kit gets both skins automatically.
