# ASUSCraft — Flatpak security audit

**Date:** 2026-11-11
**Scope:** the Flatpak build, the host agent, the Flatpak manifest, the
polkit path and the new command layer.
**Method:** the code was read line by line, the manifest was built and its
resulting permissions were read back from the built metadata, the privileged
path was exercised and the polkit authority's own record was checked.

---

## 1. The verdict

**The sandbox is not weakened anywhere.** The Flatpak asks for six named
D-Bus names and nothing else: no host filesystem, no device access, no
unrestricted bus, no root.

The build's own metadata is the evidence:

```
[Context]
filesystems=xdg-run/dconf;

[Session Bus Policy]
io.github.asuscontrolcenter.Agent=talk
org.freedesktop.portal.Desktop=talk
ca.desrt.dconf=talk
org.gnome.shell.extensions.soft-brightness-plus=talk

[System Bus Policy]
org.freedesktop.login1=talk
xyz.ljones.Asusd=talk
```

There is no `--filesystem=host`, no `--device=all`, no
`--system-talk-name=org.freedesktop.DBus` and no `--allow=devel`.

## 2. The threat model

| the attacker | what they want | what stops them |
|---|---|---|
| a malicious Flatpak | the user's hardware | it cannot write to `/sys` (the filesystem is read-only) and it has no polkit action |
| a compromised ASUSCraft build | root | the only privileged path is `pkexec` → the whitelist-only helper → polkit, which asks the user |
| a hostile D-Bus caller | to use the agent as a remote shell | the agent takes a **command id** from a fixed table, not an argv, and re-validates the argument |
| a hostile argument | to smuggle a flag or a shell metacharacter | every argument is checked against a pattern or a fixed list before it reaches the argv; a value that starts with `-` is refused |
| a hostile extension uuid | to run something else | the uuid must match `name@domain` and may only contain letters, digits, `.`, `_`, `-` and `@` |
| the agent itself | to become a privilege-escalation service | it holds **no** privilege: it can only run `pkexec`, and polkit decides. The helper's whitelist is the real boundary and it is unchanged |

## 3. The sandbox's permissions, one by one

| permission | necessary? | evidence |
|---|---|---|
| `--system-talk-name=org.freedesktop.login1` | **yes** | the panel brightness (`Session.SetBrightness`) |
| `--system-talk-name=xyz.ljones.asus_armoury` | **yes** | the GPU MUX |
| `--talk-name=io.github.asuscontrolcenter.Agent` | **yes** | the host agent |
| `--talk-name=org.gnome.shell.extensions.soft-brightness-plus` | **yes** | the OLED dimming value (a GSettings key) |
| `--talk-name=ca.desrt.dconf` | **yes** | the settings: the touchpad, the eye-care mode |
| `--talk-name=org.freedesktop.portal.Desktop` | **yes** | the software centre button |
| `--filesystem=xdg-run/dconf` | **yes** | dconf keeps a shared-memory snapshot in the runtime directory. Without it the client falls back to an empty private database and the settings would never reach the host. It is **one directory**, inside the runtime dir, owned by the user — the narrowest filesystem permission there is. |
| `--filesystem=host` | **no** | the hardware is read from `/sys` and `/proc`, which a sandbox already sees read-only. **Not granted.** |
| `--device=all` | **no** | nothing in the app opens a device node. **Not granted.** |
| `--system-talk-name=org.freedesktop.DBus` | **no** | every system-bus call names one service. **Not granted.** |
| `--allow=devel` | **no** | **Not granted.** |

## 4. The host agent

### 4.1 What it is

A per-user, unprivileged, D-Bus-activated service on the **session** bus. It
starts on demand and exits five minutes after the last call.

### 4.2 What it cannot do

- it refuses to run as root (`geteuid () == 0` → it exits);
- it owns **no** bus name on the system bus;
- it cannot write to `/sys` (it is an ordinary user process);
- it cannot run anything except the commands in the fixed table, and the
  only privileged program it may run is `pkexec` with the helper.

### 4.3 The command table

`src/control/commands.cpp` is compiled into **both** binaries, so the app
and the agent cannot disagree. The agent re-validates the id and the
argument even though the app already did:

| the caller sends | the agent does |
|---|---|
| a command id | looks it up; an unknown id is refused (`-2`) |
| an argument | checks it against the command's rule |
| anything else | it is not a parameter of the interface at all |

The rules are tested by `asuscraft-agent --check` (48 assertions) and by
`asus-control-center --check`.

### 4.4 The privileged path

```
the app  →  the agent  →  pkexec  →  acc-helper  →  /sys
                             ↓
                        polkit (auth_admin)
```

The polkit authority's own record of a request made by the agent:

```
Operator of unix-session:5 FAILED to authenticate to gain authorization for
action io.github.asuscontrolcenter.helper for unix-process:...[asuscraft-agent]
```

The action id is the **unchanged** one. The helper is the **unchanged**
script. The whitelist is the **unchanged** list.

### 4.5 The threads and the timeouts

- every call runs on its own thread, so a slow command never blocks the
  bus;
- the privileged call is cut off after 120 seconds (a polkit dialog nobody
  answers must not hang the agent for ever);
- the network-backed commands are bounded with `/usr/bin/timeout`;
- the agent exits after five idle minutes.

### 4.6 What the agent cannot be turned into

| attempt | result |
|---|---|
| `RunCommand` with an argv | there is no such parameter |
| `RunCommand` with an unknown id | `-2`, nothing is run |
| `RunCommand` with `; rm -rf /` as an argument | refused by the argument rule |
| `SetHardware` with a new verb | refused: the whitelist is the helper's list |
| `SetHardware` with a value outside the list | refused (exit `2`) |
| making the agent run as root | it refuses to start |

## 5. The polkit action

| | |
|---|---|
| the action id | `io.github.asuscontrolcenter.helper` — **unchanged** |
| the policy file | **unchanged**, byte for byte |
| the helper | **unchanged**, byte for byte |
| the verbs | `set-perf`, `set-charge`, `set-charge-mode`, `set-gpu-mux`, `set-kbd` — **unchanged** |
| the values | **unchanged** |

Nothing in the Flatpak work widened the whitelist. A new verb or a new value
is a separate review (see `docs/polkit.md` §6).

## 6. The things the Flatpak does not get

Inside the sandbox, these are simply **not available**, and the interface
says so:

| not available | why |
|---|---|
| writing to `/sys` | the filesystem is read-only; the helper is the only way |
| `pkexec` | not in the runtime, and a Flatpak may not run host programs |
| `gnome-extensions` | not in the runtime |
| the user's extension folder | the sandbox has its own home |
| `/dev/video0`, `/dev/snd` | no `--device=all` |
| the host's `dnf`, `fwupdmgr`, `systemctl`, `journalctl`, `iw`, `nvidia-smi` | not in the runtime; the agent runs them |
| the host's `~/.config`, `~/.local/share` | the sandbox has its own home |
| the host's dconf **write** for the app's own settings | the app's own settings live in the sandbox |

## 7. A note on the project invariant

`AGENTS.md` says: *"The daemon is a system-bus **client** only. It owns no
bus name and never touches the session bus. Do not add a session-bus
service or a `g_bus_own_name` to `src/`."*

That rule is kept, with one deliberate addition:

- the **app** (`src/`) still owns **no** bus name. `src/control/agent.cpp`
  is a pure client: it calls `org.freedesktop.DBus.NameHasOwner` and
  `StartServiceByName` and then calls the agent's methods. There is no
  `g_bus_own_name` anywhere in `src/`.
- the **agent** (`agent/`) is a new, separate component — a separate
  package, a separate source tree — and it owns **one** session-bus name.
  It was reviewed and approved as the host side of the Flatpak
  architecture before any code was written.

The agent is a session service because it has to be: it installs an
extension into the user's own extension folder and talks to the user's
desktop session. A system-bus service would need polkit for everything,
including the harmless reads, which would be strictly worse.

## 8. What was checked, and how

| check | how | result |
|---|---|---|
| the manifest's permissions | built, then read back from the metadata | 6 named names and one runtime directory, nothing else |
| `--filesystem=host` absent | the metadata above | absent |
| `--device=all` absent | the metadata above | absent |
| an unrestricted system bus | the metadata above | absent |
| the helper is unchanged | compared with the pre-change copy | identical |
| the policy is unchanged | compared with the pre-change copy | identical |
| the action id is unchanged | read from the policy and from the polkit journal | identical |
| the whitelist is unchanged | read from the helper and from `commands.cpp` | identical |
| the command arguments are validated | `asuscraft-agent --check` | refused |
| the agent refuses to run as root | the code path | refused |
| the privileged path reaches polkit | the polkit journal | the action id is the expected one |
| the app works without the agent | the agent stopped, the app run | the local path is used |
| the agent's privileged path reaches polkit | the polkit journal | the action id is the expected one |
| the sandbox's GSettings cannot be trusted to reach the host | a second, unrelated Flatpak on the same machine | it could not either → the agent does those two settings |
| `/sys/firmware` is not in a sandbox | measured | the performance profile is not readable there, and the app says so |
| no shell in the new code | the sources | `g_spawn_*` is only used with a fixed argv |

## 9. The residual risks

| risk | how bad | what we do about it |
|---|---|---|
| the agent runs a command for whoever asks on the session bus | low: the commands are read-only and the arguments are validated | the agent only listens on the user's own session bus; another user cannot reach it |
| the agent's session bus name can be squatted | low: another process could own the name first | the app would then talk to the impostor — the agent's `GetInfo` returns a version and the capabilities; this is the same exposure as any session-bus service |
| the Flatpak could be replaced by a malicious build | the usual Flatpak trust model | Flathub's signing and the per-app permissions; the privileged path is still behind polkit |
| the polkit prompt is the only thing between a change and the firmware | unchanged from before | the whitelist, the read-back verification and the `auth_admin` policy |

## 10. The conclusion

The Flatpak build is **narrower** than the native one, not wider: the
sandbox physically cannot write to `/sys`, cannot run `pkexec` and cannot
see the user's home. Everything the native build could do through a
privileged write still goes through the same helper, under the same polkit
action, with the same whitelist.

Nothing was weakened to make a feature work. The features that need the
host get the host agent; the features that have no host access say so.
