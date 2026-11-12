# Polkit integration

How ASUSCraft asks for permission, and what has to change when you touch it.

---

## 1. There is exactly one action

```
id:      io.github.asuscontrolcenter.helper
file:    /usr/share/polkit-1/actions/io.github.asuscontrolcenter.AsusControlCenter.policy
program: /usr/libexec/asus-control-center/acc-helper
```

```xml
<action id="io.github.asuscontrolcenter.helper">
  <description>Run the ASUSCraft hardware helper</description>
  <message>ASUSCraft wants to change a hardware setting (charge limit,
            performance mode, GPU mode, keyboard light)</message>
  <defaults>
    <allow_any>auth_admin</allow_any>
    <allow_inactive>auth_admin</allow_inactive>
    <allow_active>auth_admin_keep</allow_active>
  </defaults>
  <annotate key="org.freedesktop.policykit.exec.path">/usr/libexec/asus-control-center/acc-helper</annotate>
</action>
```

**The action id, the policy file and the helper have not changed.** The
Flatpak work did not touch them: the id is the same string, the file is
byte-for-byte the same, and the helper is the same script.

## 2. The two ways to reach it

```
                    the native install
   the app  ──pkexec──▶  /usr/libexec/asus-control-center/acc-helper  ──▶ /sys
                            ▲
                            │ pkexec, same action id, same helper
                            │
   the Flatpak ──D-Bus──▶  the host agent
```

- **Native:** the app runs `pkexec /usr/libexec/asus-control-center/acc-helper
  <action> <value>` itself (`src/control/hostcmd.cpp`).
- **Flatpak:** the app cannot run `pkexec` (it is not in the runtime and a
  Flatpak may not run host programs), so it asks the host agent, and the
  agent runs the *same* command (`agent/main.cpp`).

In both cases the polkit authority sees the same action id and the same
helper. The prompt the user sees is the same prompt.

## 3. The whitelist

The helper is whitelist-only: every action and every value is hard-coded in
`data/acc-helper`, and the helper validates both before it writes anything.

| action | the values it accepts | what it writes |
|---|---|---|
| `set-perf` | `quiet`, `balanced`, `performance` | `/sys/firmware/acpi/platform_profile` |
| `set-charge` | `60`, `80`, `100` | the ASUS health-charging limit |
| `set-charge-mode` | `0`, `1`, `2` | `…/asus-armoury/attributes/charge_mode` |
| `set-gpu-mux` | `0`, `1` | `…/asus-armoury/attributes/gpu_mux_mode` |
| `set-kbd` | `0`, `1`, `2`, `3` | `/sys/class/leds/asus::kbd_backlight/brightness` |

The same list now lives in **three** places, and they must agree:

1. `data/acc-helper` — the helper's own `case` arms (the final gate);
2. `src/control/commands.cpp` — `acc_command_helper_action_ok()` and
   `acc_command_helper_value_ok()`, used by the app **and** by the agent;
3. the tests: `asus-control-center --check` and `asuscraft-agent --check`.

**A new verb or a new value is a change to the whitelist.** It needs a
separate review, because it widens what the polkit prompt can do. The
procedure is in §6.

## 4. What the exit codes mean

| code | meaning |
|---|---|
| `0` | done, and the write was read back and confirmed |
| `1` | the action is not one of the helper's verbs |
| `2` | the value is not allowed for that action |
| `3` | the kernel modules are in use — log out and try again |
| `4` | the firmware refused the value (read-back did not match) |
| `5` | the GPU MUX write went through but the firmware did not confirm it |
| `127` | the authorization was dismissed (pkexec) |

The app shows these to the user; `3` and `5` in particular have their own
explanations in the interface.

## 5. How to test the privileged path

```sh
# the whitelist rejects anything else, without a prompt:
pkexec /usr/libexec/asus-control-center/acc-helper set-kbd 9 ; echo $?   # 2
pkexec /usr/libexec/asus-control-center/acc-helper rm-rf    ; echo $?   # 1

# the real thing (a prompt appears):
pkexec /usr/libexec/asus-control-center/acc-helper set-kbd 0 ; echo $?   # 0
```

Through the agent:

```sh
gdbus call --session --dest io.github.asuscontrolcenter.Agent \
  --object-path /io/github/asuscontrolcenter/Agent \
  --method io.github.asuscontrolcenter.Agent.SetHardware set-kbd 0
```

The polkit authority records the request, which is how the agent's path was
verified:

```
Operator of unix-session:5 FAILED to authenticate to gain authorization for
action io.github.asuscontrolcenter.helper for unix-process:... [asuscraft-agent]
```

## 6. Adding a verb or a value

This is a **security change** and needs its own review. In order:

1. change `data/acc-helper` — add the `case` arm, with the read-back
   verification every other arm has;
2. change `acc_command_helper_action_ok()` / `acc_command_helper_value_ok()`
   in `src/control/commands.cpp` (the app and the agent share this file);
3. add the matching assertions to `asus-control-center --check` **and**
   `asuscraft-agent --check`;
4. update the table in `docs/polkit.md` and in `docs/host-agent.md`;
5. update the polkit message if the new action is not covered by the
   current one.

**Never** widen the policy's `<defaults>` (no `yes` for `allow_any`, no
`auth_self` in place of `auth_admin`) and **never** add a second action for
the same helper: one action, one helper, one prompt.

## 7. What is deliberately not done

| not done | why |
|---|---|
| a second polkit action for the agent | the agent needs no privilege of its own |
| the agent running as root, or a setuid agent | polkit decides, not the agent |
| the agent calling the helper directly | only `pkexec` may, because only `pkexec` asks polkit |
| a polkit rule that allows the helper without a password | the prompt is the point |
| the Flatpak talking to polkit directly | it may not; the agent does it |
