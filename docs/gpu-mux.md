# GPU MUX on this laptop — why the switch is queued, not instant

Date: 2026-10-28 · Scope: `gpu_mux_mode` writes on the Vivobook Pro 15
N6506CU (Core Ultra 9 285H + RTX 4050), kernel 7.2.x, asusctl/asusd,
and the wording ACC uses on the GPU page.

This page answers one question: **why does the app say "queued" instead of
switching immediately, and what exactly is refusing the write?** Everything
below is verified against upstream source, not inferred from the UI.

## 1. The two values

| `gpu_mux_mode` | Meaning on this machine | Where the panel goes |
|---|---|---|
| `1` | Hybrid / Optimus | Panel wired to the Intel iGPU; NVIDIA renders on demand |
| `0` | Ultimate / dGPU-only | Panel wired straight to NVIDIA |

Nothing else is a MUX value. "Eco / disable the dGPU" is a **different**
interface (`dgpu_disable`) that this firmware does not expose, so ACC shows
it disabled with an explanation rather than pretending.

## 2. What the kernel does with a write

`drivers/platform/x86/asus-armoury.c`:

```c
if ((retval == NULL) && (result == 0)) {
        pr_err("Failed to set %s: (result): 0x%x\n", attr->attr.name, result);
        return -EIO;
}
```

and the legacy node in `drivers/platform/x86/asus-wmi.c`:

```c
/* !1 is considered a fail by ASUS */
if (result != 1) {
        dev_warn(dev, "Failed to set GPU MUX mode (result): 0x%x\n", result);
        return -EIO;
}
```

So an `-EIO` from the write is **the firmware answering "not now"** (WMI
result `0x0`), not a wrong device id, not a permissions problem and not a
missing driver. Kernel documentation for `armoury_set_devstate()` lists
exactly that: `-EINVAL` unsafe value, `-EIO` WMI function returned an error.

Two extra refusals exist before the write is even attempted:

* `dgpu_disable` says the dGPU is off → `-ENODEV`
  ("Cannot switch MUX to dGPU mode when dGPU is disabled").
* an eGPU is enabled → `-EBUSY`.

On this laptop both attributes are absent, so neither applies.

After a write the kernel also calls `asus_set_reboot_and_signal_event()`,
which sets `pending_reboot` and notifies sysfs. That is the firmware itself
declaring that the change only takes effect on the next boot.

## 3. Why asusd queues instead of writing

`asusd/src/asus_armoury.rs` (`set_current_value`):

```rust
FirmwareAttributeType::Gpu => {
    debug!("Queueing GPU attribute {name} = {value} for delayed apply");
    self.queued_gpu.lock().await.insert(self.name(), value);
    return Ok(());
}
```

A successful D-Bus `CurrentValue = 0` therefore means **"asusd remembered
your request"**, never "the hardware switched". The same file keeps the
queue deliberately out of `CurrentValue`:

> `current_value` doesn't reflect actual firmware state until
> `apply_queued_gpu_value` is called. Instead, queued GPU values are only
> visible through `queued_gpu_value`.

`QueuedGpuValue` returns `-1` when nothing is queued. ACC uses exactly that
rule: radios follow `CurrentValue`, the notice announces `QueuedGpuValue`,
and `queued == current` counts as "nothing pending".

## 4. Who applies the queue

`asus-shutdown/src/main.rs` holds a logind delay inhibitor and waits for
`PrepareForShutdown`, then:

1. reads the queue from asusd,
2. waits for `nvidia-powerd` / `nvidia-persistenced` / `nvidia-fabricmanager`
   to exit (up to 10 s),
3. `modprobe -r nvidia_drm nvidia_modeset nvidia_uvm nvidia`,
4. re-reads the queue and calls `apply_queued_gpu_value()`,
5. removes the entry **only after** the firmware write succeeded.

That is the sequence the EC accepts: no NVIDIA driver holding the display.
`asus-shutdown.service` must be enabled, otherwise the queue lives in RAM
and disappears. ACC checks `GetUnitFileState` + `ActiveState` and says so
in the notice.

Because the hook is `PrepareForShutdown`, **shutdown and restart both
trigger it**; the mode becomes active on the next power-on.

## 5. Independent confirmation

supergfxctl's README on `AsusMuxDgpu`:

> toggle the ASUS MUX to use dGPU as primary. […] A reboot is *always*
> required due to how this works in ACPI.

## 6. What the app does, in order

1. **Helper first** — `pkexec acc-helper set-gpu-mux <0|1>` writes
   `asus-armoury/…/gpu_mux_mode/current_value`, then **reads it back**.
   Exit `4` = firmware refused, `127` = polkit prompt dismissed.
2. **Queue second** — if the write is refused and the laptop is on AC, the
   request goes to asusd, which defers it to shutdown (section 3–4).
3. **Never assume** — after either path the page re-reads the live value,
   the queue and `pending_reboot`. A mode is shown as live only when the
   firmware says so; a queued mode is shown as a request with the three
   steps and a "Cancel queued change" button.

No path writes an arbitrary value: the helper only accepts `0` and `1`, and
`acc_asusd_set_gpu_mux()` rejects anything else before touching D-Bus.

## 7. Verifying by hand

```sh
# live value (0 = ultimate, 1 = hybrid)
cat /sys/class/firmware-attributes/asus-armoury/attributes/gpu_mux_mode/current_value

# queued value (-1 = nothing queued)
busctl get-property xyz.ljones.Asusd /xyz/ljones/asus_armoury/gpu_mux_mode \
    xyz.ljones.AsusArmoury QueuedGpuValue

# did the shutdown hook run and what did it do?
journalctl -b -1 -u asus-shutdown.service --no-pager

# firmware's own pending-reboot flag
cat /sys/devices/platform/asus-nb-wmi/pending_reboot
```

Expected outcomes:

| Situation | What you should see |
|---|---|
| Write accepted in-session | `current_value` changes, `pending_reboot` set, panel moves after reboot |
| Write refused (`-EIO`) | Nothing changes; asusd queue holds the request instead |
| Queued, service enabled | Mode applies during shutdown, queue empty afterwards |
| Queued, service disabled | Queue lost on restart; ACC warns in amber |
| Reverting | Select Hybrid, shut down the same way |

## 8. Result on this machine

Ultimate was queued from ACC, `asus-shutdown.service` enabled, the laptop
shut down, and after power-on:

```
$ cat /sys/class/firmware-attributes/asus-armoury/attributes/gpu_mux_mode/current_value
0
$ nvidia-smi --query-gpu=display_active --format=csv,noheader
Enabled
```

`card2-eDP-1` is the connected internal panel, i.e. NVIDIA now drives it.
The switch works; it just cannot happen while the NVIDIA driver is loaded.

## 9. Correction (2026-11-02, from the journal of 79 boots)

* The helper's **direct write is what switches the MUX** on the N6506CU
  (BIOS 309): after the 13:25 request the next two boots ran with i915's eDP
  disabled, i.e. the panel on NVIDIA. The firmware keeps reporting the
  running mode until reboot, so the old read-back check failed, the app
  then *also* queued the value in asusd, and that duplicate write is what
  asus-shutdown logs as `Input/output error` at every shutdown. Every
  deferred apply on this laptop (7 of 7) has failed that way.
* Fix: the helper confirms a MUX write by `pending_reboot` (exit 5 =
  written, unconfirmed), the app no longer double-queues, and it records
  the request with the boot id so the next boot reports *verified* or
  *failed* (with the asusd/kernel line from that boot's journal).
* `/etc/modprobe.d/supergfxd.conf`, left behind when supergfxctl was
  removed on 2026-10-30, blacklists `nvidia`; since then nouveau won the
  GPU on 5 boots, including one Ultimate boot. ASUSCraft warns about it;
  removing it (plus `/etc/supergfxd.conf`, then `sudo dracut -f`) is the
  user's decision.

## 10. Integrated (Intel Arc) mode

Not supported on this model and not offered. It needs a firmware dGPU
power switch (`dgpu_disable` in asus-armoury or asus-nb-wmi); the N6506CU
exposes only `gpu_mux_mode`, `charge_mode` and `pending_reboot`. The
alternatives were rejected: supergfxd's Integrated mode is a driver
blacklist plus PCI removal (not verified on this model, conflicts with
asusd and left the blacklist behind), and unbinding/unloading the NVIDIA
driver by hand is exactly what the safety rules forbid. In Hybrid the
dGPU already runtime-suspends (NVreg_DynamicPowerManagement=3, D3cold
allowed) whenever no process holds `/dev/nvidia0`; the GPU page now shows
that state and which processes keep it awake. Supporting Integrated would
need a firmware `dgpu_disable` attribute verified on the model.
