# Installing ASUSCraft as a Flatpak

ASUSCraft ships as two independent pieces:

| piece | what it is | needed? |
|---|---|---|
| **the Flatpak** | the whole app: the interface, the read-only hardware readings, the settings | yes, for the Flatpak build |
| **the host agent** | a small service that runs on the host and does what a sandbox may not | optional |

The Flatpak is **fully usable without the agent**. The controls that need the
host (the firmware changes, the host commands, the OLED dimming extension)
say so in the interface instead of failing quietly.

---

## 1. Requirements

- Flatpak (`flatpak`)
- the GNOME runtime: `org.gnome.Platform//46` (the builder pulls it)

Nothing else. The Flatpak does **not** need `pkexec`, `gnome-extensions`,
`dnf`, `fwupdmgr` or any other host tool inside the sandbox.

## 2. Install

### From a bundle

```sh
flatpak install --user ./asuscraft.flatpak
```

or system-wide:

```sh
sudo flatpak install ./asuscraft.flatpak
```

### From the source tree

```sh
git clone https://github.com/Mostafamomen/asus-control-center
cd asus-control-center
flatpak-builder --user --install build \
    flatpak/io.github.asuscontrolcenter.asuscraft.yml
```

`flatpak-builder` downloads the GNOME 46 SDK and the runtime the first
time.

### The command line

```sh
flatpak run io.github.asuscontrolcenter.asuscraft
flatpak run io.github.asuscontrolcenter.asuscraft --check        # the self test
flatpak run io.github.asuscontrolcenter.asuscraft --self-test   # the hardware report
flatpak run io.github.asuscontrolcenter.asuscraft --specs        # the full report
```

## 3. Add the host agent (optional but recommended)

Without it, these do not work:

- the charge limit, the charging policy, the performance profile, the
  keyboard light and the GPU mode;
- the host readings: the service states, the package and firmware updates,
  the Wi-Fi SSID, the Bluetooth adapter, the SMART health, the NVIDIA
  telemetry;
- installing and enabling the bundled **Soft Brightness Plus** extension;
- the GNOME night light.

With it, all of them work — and the privileged changes still go through the
same polkit prompt as the native package.

```sh
sudo dnf install asuscraft-agent
```

See `docs/host-agent-install.md`.

**The Flatpak never installs anything on the host.** It does not run
`dnf`, it does not call `pkexec`, it does not write outside its own
sandbox.

## 4. What the Flatpak may do

Exactly this, and nothing more:

| permission | why |
|---|---|
| `--system-talk-name=org.freedesktop.login1` | the panel brightness |
| `--system-talk-name=xyz.ljones.asus_armoury` | the GPU MUX |
| `--talk-name=io.github.asuscontrolcenter.Agent` | the host agent |
| `--talk-name=org.gnome.shell.extensions.soft-brightness-plus` | the OLED dimming value |
| `--talk-name=ca.desrt.dconf` | the settings (the touchpad, the eye-care mode) |
| `--talk-name=org.freedesktop.portal.Desktop` | the software centre button |

There is **no** `--filesystem=host`, **no** `--device=all` and **no**
unrestricted system bus. The hardware is read straight from `/sys`, which a
sandbox can already read.

## 5. What still works without the agent

- the whole interface and both skins;
- the battery, the charger and the health history;
- the CPU, the RAM, the disk, the temperatures and the load;
- the hardware inventory (the DMI, the PCI, the USB, the DRM, the block
  devices, the network, the LEDs);
- the resolution and the refresh rate;
- the panel brightness (through logind);
- the eye-care mode and the E-Reading mode;
- the touchpad (through the desktop's own setting);
- the local account, the password hashing and the language;
- the diagnostics and the interactive tests that need no host tool.

## 6. Known limitations in the Flatpak build

| what | why | what to do |
|---|---|---|
| the firmware changes | the sandbox may not write to `/sys` | install the host agent |
| the host commands (`iw`, `nvidia-smi`, `dnf`, `fwupdmgr`, `systemctl`, `journalctl`) | they are not in the runtime | install the host agent |
| the OLED dimming extension | it belongs to the desktop session | install the host agent |
| the GNOME night light | the colour schema is not in the runtime | install the host agent |
| the OLED dimming value and the touchpad switch | a sandbox's dconf does not reliably reach the host's database | install the host agent (the app asks the agent to do both) |
| "launch at login" | the autostart entry would land in the sandbox | use the native package, or start it from the app menu |
| the System Manager's folders (Downloads, Trash) | the sandbox has its own home | use the native package |
| the camera and the speaker tests | `/dev/video0` and `/dev/snd` are not reachable | use the native package |

Everything above is **reported in the interface**: the switch is disabled and
the reason is written next to it.

## 7. Uninstall

```sh
flatpak uninstall io.github.asuscontrolcenter.asuscraft
```

The agent is a separate package and stays until you remove it:

```sh
sudo dnf remove asuscraft-agent
```

Removing the Flatpak does not touch the helper, the polkit policy or the
native package.

## 8. Where the data lives

| what | where |
|---|---|
| the app's own settings | `~/.var/app/io.github.asuscontrolcenter.asuscraft/config/asus-control-center/` |
| the history files | `~/.var/app/io.github.asuscontrolcenter.asuscraft/.cache/asus-control-center/` |
| the dimming value | the **host's** dconf (the agent writes it there) |
| the extensions the agent installs | the **host's** `~/.local/share/gnome-shell/extensions/` |

## 6.1 Two things the sandbox cannot do

Both were measured, not assumed:

- **`/sys/firmware` is not in the sandbox.** `/sys/class`, `/sys/devices`
  and `/sys/bus` are, but the firmware's own tree (`platform_profile`) is
  not. The performance profile can therefore not be *read* in the Flatpak;
  it can still be *changed*, through the helper on the host.
- **The sandbox's dconf does not reliably reach the host's database.**
  dconf keeps a shared-memory snapshot in the runtime directory; without
  it the client falls back to an empty private database, so a setting the
  app writes would never reach the desktop. The manifest asks for that
  one directory (`--filesystem=xdg-run/dconf`), and the two settings that
  must reach the host anyway — the OLED dimming value and the touchpad
  switch — are done by the **host agent**.
