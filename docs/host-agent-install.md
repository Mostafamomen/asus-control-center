# Installing the ASUSCraft host agent

The host agent is a **separate package**. The Flatpak works without it; the
agent only adds the controls that need the machine itself.

| | |
|---|---|
| Package | `asuscraft-agent` (RPM) |
| Binary | `/usr/libexec/asuscraft-agent/asuscraft-agent` |
| Activation | `/usr/share/dbus-1/services/io.github.asuscontrolcenter.Agent.service` |
| Bus | the session bus, as your own user |
| Privileges | none of its own — polkit decides, exactly as before |

---

## 1. What it gives you

With the agent installed, the Flatpak build can:

- change the charge limit, the charging policy, the performance profile, the
  keyboard light and the GPU mode — through the same polkit prompt as the
  native app;
- read the host's tools: the service states, the package and firmware
  updates, the Wi-Fi SSID, the Bluetooth adapter, the SMART health, the
  NVIDIA telemetry, the journal;
- install and enable the bundled **Soft Brightness Plus** extension;
- read and change the GNOME night light.

Without it, those controls say so instead of failing quietly.

## 2. Install it

### Fedora

```sh
sudo dnf install asuscraft-agent
```

or, from a built RPM in this repository:

```sh
sudo dnf install ./dist/asuscraft-agent-*.rpm
```

### From source

```sh
git clone https://github.com/Mostafamomen/asus-control-center
cd asus-control-center
make -C agent
sudo make -C agent install
```

That installs:

- `/usr/libexec/asuscraft-agent/asuscraft-agent`
- `/usr/share/dbus-1/services/io.github.asuscontrolcenter.Agent.service`
- `/usr/share/asuscraft-agent/soft-brightness-plus/` (the bundled extension)

### Build the RPM

```sh
packaging/build-rpm.sh        # builds both the app and the agent
ls -l dist/*.rpm
```

## 3. Check that it works

The agent starts on demand; there is nothing to enable.

```sh
# the pure-logic self test (no bus, no hardware)
asuscraft-agent --check

# ask the bus whether it is there
gdbus call --session --dest io.github.asuscontrolcenter.Agent \
  --object-path /io/github/asuscontrolcenter/Agent \
  --method io.github.asuscontrolcenter.Agent.GetInfo
# ('1.0', 'hardware,commands,softbrightness,nightlight', 'GNOME')
```

In the app: **Settings → Host agent** shows *Connected* (with the version),
*Not answering* or *Not installed*, and a **Check again** button.

## 4. The helper and the polkit policy

The agent still uses the same helper, so it still needs it:

```sh
sudo install -d -m 0755 /usr/libexec/asus-control-center
sudo install -m 0755 data/acc-helper /usr/libexec/asus-control-center/acc-helper
sudo install -m 0644 data/io.github.asuscontrolcenter.AsusControlCenter.policy \
     /usr/share/polkit-1/actions/io.github.asuscontrolcenter.AsusControlCenter.policy
```

or simply `./install.sh` from the repository, which installs the helper and
the policy and enables the ASUS services.

The helper's whitelist and the polkit action id are **unchanged** — see
`docs/polkit.md`.

## 5. Remove it

```sh
sudo dnf remove asuscraft-agent
# or
sudo make -C agent uninstall
```

The Flatpak keeps working; the controls that need the host report that the
agent is not installed.

## 6. Troubleshooting

| symptom | cause | what to do |
|---|---|---|
| *Not installed* in the app | the service file is missing | `ls /usr/share/dbus-1/services/io.github.asuscontrolcenter.Agent.service` |
| *Not answering* | the agent started but did not reply | `journalctl --user -n 50 \| grep asuscraft-agent` |
| the polkit prompt never appears | no authentication agent in the session | `systemctl --user status polkit-gnome` (or gnome-shell, which provides one) |
| the prompt appears but the change fails | the helper is missing or old | run `./install.sh` |
| the charge limit does not change | the firmware refused it | the helper exits with `4`; the app says so |
