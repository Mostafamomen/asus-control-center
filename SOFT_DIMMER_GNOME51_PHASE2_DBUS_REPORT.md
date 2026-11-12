# Soft Dimmer — GNOME 51.1 D-Bus repair (Phase 2)

**Subject:** `soft-dimmer@io.github.softdimmer`
**Installed at:** `/home/mostafa/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer`
**Primary file repaired:** `dbus.js` (the only file changed)
**Date:** 2026-11-11
**Environment:** GNOME Shell 51.1 / mutter 51.1 / GJS 1.90.1 / Fedora 45 / kernel 7.3.3-300.fc45
**Phase:** 2 of 3 — **Phase 3 was not started.**

---

## 0. Verdict summary

| # | Acceptance criterion | Verdict |
|---|---|---|
| 1 | Fresh persistent backup, verified | **PASS** |
| 2 | D-Bus signatures remain compatible | **PASS** |
| 3 | Positional argument handling correct | **PASS** |
| 4 | `GetCapabilities` returns the correct type | **PASS** |
| 5 | `GetEnabled` returns the actual boolean | **PASS** |
| 6 | All automated isolated tests pass | **PASS** (98/98) |
| 7 | Registration and teardown are safe | **PASS** |
| 8 | Phase 1 source unchanged | **PASS** (`dimmer.js` byte-identical) |
| 9 | Live extension and GNOME session untouched | **PASS** |
| 10 | The fix is live inside the running Shell | **PENDING** — needs a logout/login (see §9) |

**Phase 2 overall: PASS**, with one explicitly *pending* item (10) that cannot be
verified without restarting the GNOME Shell, which the task forbids.

---

## 1. Pre-implementation backup

A **new** timestamped backup was created before anything was edited. It does not
reuse or depend on the earlier backup.

| Item | Value |
|---|---|
| Backup root | `/home/mostafa/.local/share/soft-dimmer/backups/20261111T062247Z/` |
| Backup tree | `.../20261111T062247Z/extension/` (9 files, 2 sub-directories) |
| Manifest | `.../20261111T062247Z/MANIFEST.sha256` (SHA-256, 9 entries) |
| Metadata | `.../20261111T062247Z/MANIFEST.meta` |
| Captured state | `.../20261111T062247Z/state/STATE.txt` |

### 1.1 Checks performed

| # | Check | Command | Result |
|---|---|---|---|
| 1 | The resolved path is the real installed path | `gnome-extensions info … \| awk '/^  Path:/'` → `realpath` | **PASS** — `/home/mostafa/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer` |
| 2 | The backup is outside `/tmp` and outside the extension directory | `case "$B" in /tmp\|/tmp/*) …` | **PASS** — `~/.local/share/soft-dimmer/backups/…` |
| 3 | The backup tree is identical to the installed tree | `diff -r --no-dereference "$D" "$B/extension"` | **PASS** — no output |
| 4 | The manifest covers every file | `find extension -type f \| wc -l` = 9; manifest has 9 entries | **PASS** |
| 5 | The manifest verifies | `sha256sum -c MANIFEST.sha256` | **PASS** — 9 × `OK`, **exit 0** |
| 6 | The backup is on the same filesystem as the installation | `stat -c %d` | **PASS** — both `55` → `rename(2)` is atomic |
| 7 | Restoration test into a disposable directory | `cp -a` into `.../restore-test/`, re-hash, compare modes/structure, `diff -r` | **PASS** — hashes identical, modes/structure identical, trees identical; the disposable directory was then removed |
| 8 | The installed tree was not modified by the test | re-hash of `$D` vs the manifest | **PASS** — unchanged |

```
extension/dbus/io.github.softdimmer.BrightnessControl.xml: OK
extension/dbus.js: OK
extension/dimmer.js: OK
extension/extension.js: OK
extension/indicator.js: OK
extension/metadata.json: OK
extension/schemas/gschemas.compiled: OK
extension/schemas/org.gnome.shell.extensions.soft-dimmer.gschema.xml: OK
extension/test-logic.js: OK
```

### 1.2 State recorded at backup time

```
Enabled: Yes     State: ACTIVE     Version: 1
brightness = 1.0   enabled = true   minimum-brightness = 0.1
monitor-selection = 'all'   restore-brightness = true
shortcut-toggle/increase/decrease = @as []
D-Bus name io.github.softdimmer owned by :1.16
```

---

## 2. The D-Bus contract that was inspected

Sources read: `dbus.js`, `extension.js`, `dimmer.js`, `indicator.js`,
`dbus/io.github.softdimmer.BrightnessControl.xml`, `test-logic.js`,
`metadata.json`, the GJS 1.90 `Gio.js` override (extracted from
`/usr/lib64/libgjs.so.0` with `gresource extract`), and the client side in
`/usr/share/gnome-shell/extensions/soft-dimmer-asc@io.github.asuscraft/dbus_client.js`
(read only, **not** modified).

| Property | Value |
|---|---|
| Bus type | Session bus (`Gio.BusType.SESSION`) |
| Bus name | `io.github.softdimmer` |
| Object path | `/io/github/softdimmer/BrightnessControl` |
| Interface | `io.github.softdimmer.BrightnessControl` |
| Methods | `GetBrightness`, `SetBrightness`, `GetEnabled`, `SetEnabled`, `GetCapabilities` |
| Signals | `BrightnessChanged (i, d)`, `EnabledChanged (b)` |
| Properties | none |
| Error behaviour | `org.freedesktop.DBus.Error.InvalidArgs` on out-of-range values (documented in the shipped XML) |
| Ownership | `Gio.bus_own_name()` in `export()`; released in `unexport()` |
| Unregistration | `exportedObject.unexport()` + `Gio.bus_unown_name()` + `settings.disconnect()` for every handler |

### 2.1 Callers

* Inside the extension: `extension.js` calls `this._dbus.export()` in `enable()`
  and `this._dbus.unexport()` in `disable()`. **Not modified.**
* Outside: nothing in the ASUSCraft repository (`/home/mostafa/Documents/asus-control-center-1.2.2`)
  references this interface — `src/control/softdim.cpp` drives the *bundled*
  Soft Brightness Plus extension, a different code base. The separate
  `soft-dimmer-asc@io.github.asuscraft` extension talks to
  `io.github.asuscraft.SoftDimmer`, a **different** bus name with the same
  method names; it is unaffected by this change. Its client calls
  `GetBrightness(monitor_index = -1)` by default, which is why the `-1`
  semantics are documented and now implemented (§4.1).

---

## 3. Public contract — before and after

**The contract is unchanged.** The introspection XML embedded in `dbus.js` is
byte-identical to the shipped copy, and the shipped file
`dbus/io.github.softdimmer.BrightnessControl.xml` was not touched.

```
$ python3 -c "…extract DBUS_INTERFACE_XML from both files…"
XML identical: True len 941 941

$ sha256sum backup/extension/dbus/io.github.softdimmer.BrightnessControl.xml \
             installed/dbus/io.github.softdimmer.BrightnessControl.xml
1dc948b5422c625134048ac4de58ef23b3e9e3a4e2aab517a9ad5ea5f33bddd4  (both)
```

| Method | Before | After |
|---|---|---|
| `GetBrightness` | in `(i)` → out `(d)` | in `(i)` → out `(d)` — **identical** |
| `SetBrightness` | in `(id)` → out `()` | in `(id)` → out `()` — **identical** |
| `GetEnabled` | in `()` → out `(b)` | in `()` → out `(b)` — **identical** |
| `SetEnabled` | in `(b)` → out `()` | in `(b)` → out `()` — **identical** |
| `GetCapabilities` | in `()` → out `(a{sv})` | in `()` → out `(a{sv})` — **identical** |
| `BrightnessChanged` | `(i, d)` | `(i, d)` — **identical** |
| `EnabledChanged` | `(b)` | `(b)` — **identical** |

No method was renamed, no signature was changed, no method was added or removed.

---

## 4. The defects, and how each was fixed

The GJS 1.90 binding contract was read out of the authoritative source
(`/usr/lib64/libgjs.so.0` → `/org/gnome/gjs/modules/core/overrides/Gio.js`,
extracted to `/tmp/opencode/gjs-Gio.js`):

* `_handleMethodCall()` — a **synchronous** method is invoked as
  `method.apply(this, args)` where `args = parameters.deepUnpack()` **plus a
  trailing unix-fd list**. There is no `params` array.
* `_handleDBusReply()` — a non-`GLib.Variant` return value is packed with
  `new GLib.Variant(outSignature, ret)`, where `outSignature` is the full
  out-tuple. With one out-argument GJS wraps the value itself
  (`ret = [ret]`), so the method must return the value **bare**.
* `_handleDBusError()` — a thrown `GLib.Error` becomes
  `invocation.return_gerror(e)`; a plain JS error becomes
  `org.gnome.gjs.JSError.<name>`.

Every candidate return form was verified empirically on a private bus before
the fix was written (`d`, `b`, `s`, `a{sv}` × bare/array-wrapped).

### A. Positional arguments — **FIXED**

*Before* — the methods expected an iterable `params` array:

```js
GetBrightness(params) {
    const [monitorIndex] = params;   // TypeError: params is not iterable
```

*After* — the in-arguments are ordinary positional parameters; the trailing
fd list that GJS appends is accepted and ignored:

```js
GetBrightness(monitorIndex, _fdList = null) { … }
SetBrightness(monitorIndex, brightness, _fdList = null) { … }
SetEnabled(enabled, _fdList = null) { … }
GetEnabled(_fdList = null) { … }
GetCapabilities(_fdList = null) { … }
```

### B. `GetCapabilities` — **FIXED**

*Before* — the dictionary was packed by hand and wrapped in an array:

```js
return [GLib.Variant.new('a{sv}', [caps])];
// → TypeError: Object 0x… is not a subclass of GObject_Struct, it's a Object
```

*After* — the dictionary is returned as a plain object whose values are
`GLib.Variant` instances; GJS packs it into the `(a{sv})` out-tuple itself:

```js
return {
    'version': GLib.Variant.new_string('1.0'),
    'monitor-count': GLib.Variant.new_int32(this._dimmer.getMonitorCount()),
    'has-dbus': GLib.Variant.new_boolean(true),
    'supports-per-monitor': GLib.Variant.new_boolean(true),
};
```

Verified on the wire: the reply type is `(a{sv})` and the four documented
keys carry the documented variant types (`s`, `i`, `b`, `b`).

### C. `GetEnabled` — **FIXED**

*Before* — the method returned a `GLib.Variant`, and a `GLib.Variant` object
is **always truthy**, so the call reported `true` whatever the real state was:

```js
return [GLib.Variant.new('b', [this._settings.get_boolean('enabled'])])];
```

*After* — the underlying boolean is unwrapped and returned bare:

```js
return Boolean(this._settings.get_boolean('enabled'));
```

Verified on the wire: with the state set to `true` the reply is `(true,)` and
with the state set to `false` the reply is `(false,)` — both real booleans,
not a constant.

### D. Error handling and validation — **IMPROVED**

| Situation | Before | After |
|---|---|---|
| `monitor_index` not an integer / `< -1` | `InvalidArgs` | `InvalidArgs` (unchanged) |
| `monitor_index` names an unknown monitor | silently answered with a fabricated `1.0` | `InvalidArgs` — *"monitor_index N is not a known monitor"* |
| `brightness` not a finite number | `InvalidArgs` | `InvalidArgs` (unchanged) |
| `brightness` outside `[minimum-brightness, 1.0]` | `InvalidArgs` | `InvalidArgs` (unchanged) |
| `enabled` not a boolean | coerced with `Boolean(enabled)` | `InvalidArgs` |
| the current brightness is unavailable | — | `org.freedesktop.DBus.Error.Failed` |

Additional properties:

* The unknown-monitor check uses the **existing** `MonitorDimmer.getMonitors()`
  API. `dimmer.js` was **not** modified. If the monitor list is empty (the
  shell has not reported its monitors yet) the range check is skipped rather
  than rejecting every index.
* A rejected call changes **nothing** — verified in the test suite.
* No error message contains a path, a user name or any other environment
  detail; the messages are fixed strings or contain only the offending value.
* Every failure path is a real D-Bus error
  (`org.freedesktop.DBus.Error.InvalidArgs` / `.Failed`), never an
  `org.gnome.gjs.JSError.*` leak and never a fabricated success value.

### E. Lifecycle — **HARDENED**

| Problem | Fix |
|---|---|
| `Gio.bus_own_name()` is asynchronous: a bus-acquired callback could arrive *after* `unexport()` and export a stale object | A `_generation` counter is bumped on every `export()`/`unexport()`; the callback carries the generation it was created with and is ignored when it no longer matches |
| `_onBusLost()` left the exported object and the settings handlers in place | `_onBusLost()` now unexports the object, disconnects the handlers and marks the name as lost, so no stale object survives |
| After the name was lost, `export()` was a permanent no-op, so the extension could never come back | `export()` releases the stale ownership and retries when the name was lost |
| `unexport()` could throw if the connection was already gone | The unexport call is wrapped in `try`/`catch`; the object reference is cleared in every case |
| The bus name and the object path were string literals in two places | They are now named constants (`BUS_NAME`, `OBJECT_PATH`) — the values are unchanged |
| `export()` was called twice in a row | The `_nameId` guard already prevented a second registration; the test suite now proves it |

`_disconnectSettings()` is still called from `unexport()` — the settings
handlers are never left behind.

---

## 5. One behaviour clarification: `GetBrightness(-1)`

The shipped XML documents *"-1 returns the global value"*, but
`MonitorDimmer.getBrightness(-1)` looks up a map that is keyed by real monitor
index and therefore returns the fallback `1.0`. The old code passed the index
straight through, so `-1` would have reported "no dimming" even when the
global level was 0.5 — a fabricated value.

`dbus.js` now resolves `-1` itself, from the `brightness` setting:

```js
const brightness = monitorIndex === -1
    ? this._settings.get_double('brightness')
    : this._dimmer.getBrightness(monitorIndex);
```

A concrete index still reports the level that is painted on that monitor's
overlay, exactly as before. This is implemented **inside `dbus.js`**;
`dimmer.js` is untouched.

---

## 6. Files modified

| File | Change |
|---|---|
| `dbus.js` | **Rewritten** (the only modified file). SHA-256 `90fb17e4…d24014` → `f05c167a06b577d4007aef3ed46eb25d83b6e254c98fa85508655f2fbe384669` |

Everything else is byte-identical to the pre-Phase-2 backup:

```
  CHANGED    dbus.js
  UNCHANGED  dimmer.js
  UNCHANGED  extension.js
  UNCHANGED  indicator.js
  UNCHANGED  metadata.json
  UNCHANGED  test-logic.js
  UNCHANGED  schemas/gschemas.compiled
  UNCHANGED  schemas/org.gnome.shell.extensions.soft-dimmer.gschema.xml
  UNCHANGED  dbus/io.github.softdimmer.BrightnessControl.xml
```

File modes and directory structure are identical as well
(`diff` of `find -printf '%M %y %P'` → no output).

`dimmer.js` was **not** modified — the repair needed no change there, so the
"STOP and report" condition in §0 of the task was never reached.

---

## 7. Test suite

### 7.1 Isolation

The tests never touch the live GNOME Shell session:

* A **private `dbus-daemon`** is started from `/usr/share/dbus-1/session.conf`
  on its own socket; its address is kept in `/tmp/opencode/dbus/addr.persist`
  and passed to every process through `DBUS_SESSION_BUS_ADDRESS`.
  (`dbus-run-session` and `Gio.TestDBus` both hang in this environment.)
* The private bus is a **different bus** from the live session bus. Verified:
  the live bus still owns `io.github.softdimmer` as `:1.16`, and the private
  bus did not have the name before the test service started.
* The service process owns the **real** name `io.github.softdimmer` **on the
  private bus**, so the extension's real `export()` / `unexport()` code path
  is exercised unchanged — only the bus differs.
* The collaborators (`GSettings`, `MonitorDimmer`) are **stubs**, so the
  brightness and enabled state that the tests exercise are the stubs', never
  the live ones. The live GSettings values were read before and after and are
  unchanged.
* The service and the client run in **separate processes**: a synchronous
  D-Bus call inside the service's own main loop deadlocks, so the client
  cannot be the server.

### 7.2 Files

| File | Purpose |
|---|---|
| `~/.local/share/soft-dimmer/tests/p2-service.js` | Hosts the **real** `SoftDimmerDBus` on the private bus, plus a test-only control channel (`io.github.softdimmer.p2ctl`) for lifecycle and state assertions |
| `~/.local/share/soft-dimmer/tests/p2-tests.js` | The 98 assertions |
| `~/.local/share/soft-dimmer/tests/p2-run-service.sh` | Starts/stops the service via a PID file (`pkill -f`/`pgrep -f` would match the agent's own command line and kill the shell) |
| `~/.local/share/soft-dimmer/tests/p2-tests.out` | The captured output of the run quoted below |

### 7.3 How to run

```sh
export GI_TYPELIB_PATH=/usr/lib64/mutter-51:/usr/lib64/gnome-shell
export LD_LIBRARY_PATH=/usr/lib64/mutter-51:/usr/lib64/gnome-shell

# (re)create the private bus if it is not running any more
mkdir -p /tmp/opencode/dbus
nohup dbus-daemon --config-file=/usr/share/dbus-1/session.conf --print-address --fork \
  </dev/null >/tmp/opencode/dbus/addr.txt 2>/dev/null
sleep 3; head -1 /tmp/opencode/dbus/addr.txt > /tmp/opencode/dbus/addr.persist
export DBUS_SESSION_BUS_ADDRESS="$(cat /tmp/opencode/dbus/addr.persist)"

bash ~/.local/share/soft-dimmer/tests/p2-run-service.sh start
gjs -m ~/.local/share/soft-dimmer/tests/p2-tests.js
bash ~/.local/share/soft-dimmer/tests/p2-run-service.sh stop
```

### 7.4 Result — **98/98 checks passed**

```
── 1. introspection XML ──
  ok   the shipped introspection file is readable
  ok   the shipped XML parses
  ok   the interface is named io.github.softdimmer.BrightnessControl
  ok   the five methods are present
  ok   the two signals are present
  ok   GetBrightness is (i) -> (d)
  ok   SetBrightness is (id) -> ()
  ok   GetEnabled is () -> (b)
  ok   SetEnabled is (b) -> ()
  ok   GetCapabilities is () -> (a{sv})

── 2. live introspection matches the implementation ──
  ok   the service is up
  ok   the object can be introspected
  ok   the live introspection XML parses
  ok   the live object exports the interface
  ok   live GetBrightness is (i) -> (d)
  ok   live SetBrightness is (id) -> ()
  ok   live GetEnabled is () -> (b)
  ok   live SetEnabled is (b) -> ()
  ok   live GetCapabilities is () -> (a{sv})

── 3. GetBrightness ──
  ok   GetBrightness succeeds
  ok   the reply is a tuple of one
  ok   the value is a number
  ok   the value is the current brightness
  ok   GetBrightness(-1) succeeds
  ok   GetBrightness(-1) returns the global value

── 4. SetBrightness ──
  ok   SetBrightness succeeds
  ok   SetBrightness replies with an empty tuple
  ok   setBrightness was called exactly once
  ok   the monitor index was passed through unchanged
  ok   the brightness was passed through unchanged
  ok   the minimum brightness was passed through
  ok   the brightness setting was written
  ok   the written setting is the requested one
  ok   SetBrightness(-1, ...) succeeds
  ok   the -1 index was passed through unchanged

── 5. invalid arguments ──
  ok   GetBrightness(-2) is rejected
  ok   GetBrightness(-2) raises InvalidArgs
  ok   GetBrightness(99) - unknown monitor is rejected
  ok   GetBrightness(99) - unknown monitor raises InvalidArgs
  ok   SetBrightness(-2, 0.5) is rejected
  ok   SetBrightness(-2, 0.5) raises InvalidArgs
  ok   SetBrightness(99, 0.5) - unknown monitor is rejected
  ok   SetBrightness(99, 0.5) - unknown monitor raises InvalidArgs
  ok   SetBrightness(0, 0.05) - below the minimum is rejected
  ok   SetBrightness(0, 0.05) - below the minimum raises InvalidArgs
  ok   SetBrightness(0, 1.5) - above 1.0 is rejected
  ok   SetBrightness(0, 1.5) - above 1.0 raises InvalidArgs
  ok   SetBrightness(0, NaN) is rejected
  ok   SetBrightness(0, NaN) raises InvalidArgs
  ok   SetBrightness(0, Infinity) is rejected
  ok   SetBrightness(0, Infinity) raises InvalidArgs
  ok   a rejected SetBrightness changes nothing
  ok   a wrong argument type is rejected before it reaches the service

── 6. GetCapabilities ──
  ok   GetCapabilities succeeds
  ok   the reply is a tuple of one
  ok   the reply is a dictionary
  ok   it has the four documented keys
  ok   version is a string
  ok   version is "1.0"
  ok   monitor-count is an int32
  ok   has-dbus is a boolean
  ok   has-dbus is true
  ok   supports-per-monitor is a boolean

── 7. GetEnabled ──
  ok   GetEnabled succeeds when enabled
  ok   the reply is a tuple of one
  ok   the value is a real boolean
  ok   the value is true
  ok   GetEnabled succeeds when disabled
  ok   the value is a real boolean
  ok   the value is false
  ok   SetEnabled succeeds
  ok   the enabled setting was written
  ok   GetEnabled reports the new state

── 8. signals ──
  ok   BrightnessChanged was emitted
  ok   the signal carries the monitor index
  ok   the signal carries the brightness

── 9. registration ──
  ok   the service owns the bus name
  ok   the object is exported
  ok   exactly two settings handlers are connected
  ok   the bus name has exactly one owner

── 10. repeated initialisation does not duplicate ──
  ok   the second export call succeeds
  ok   the name id is unchanged
  ok   still exactly one exported object
  ok   no extra settings handlers were connected
  ok   the bus name still has exactly one owner
  ok   the export call counter did not create a second registration

── 11. teardown leaves no stale object ──
  ok   unexport succeeds
  ok   the name id is cleared
  ok   the exported object is gone
  ok   the connection reference is gone
  ok   the settings handlers are disconnected
  ok   the bus name is released
  ok   a call to the released service fails
  ok   the failure is a proper D-Bus error
  ok   the service can be exported again
  ok   the name is owned again
  ok   the object is exported again
  ok   the interface works again

98/98 checks passed
```

### 7.5 Requirement → test mapping

| Task §4 requirement | Where it is covered | Result |
|---|---|---|
| 1. Introspection XML parses | suite 1 (+ suite 2 on the live object) | PASS |
| 2. All method signatures match the implementation | suite 1 (shipped file) + suite 2 (live introspection) | PASS |
| 3. `GetBrightness` returns the correct type and value | suite 3 | PASS |
| 4. `SetBrightness` handles positional arguments correctly | suite 4 | PASS |
| 5. Invalid argument types are rejected | suite 5 | PASS |
| 6. `GetCapabilities` returns the expected dictionary type | suite 6 | PASS |
| 7. `GetEnabled` returns the real boolean | suite 7 | PASS |
| 8. Error responses are valid D-Bus errors | suite 5 + suite 11 | PASS |
| 9. Registration and unregistration work | suite 9 + suite 11 | PASS |
| 10. Repeated initialization does not duplicate registration | suite 10 | PASS |
| 11. Teardown does not leave stale exported objects | suite 11 | PASS |

### 7.6 The same defects, before the fix

The identical harness was run against the **pre-fix** `dbus.js` (from the
backup) and reproduced all three reported defects:

```
  GetBrightness    -> Error: GDBus.Error:org.gnome.gjs.JSError.TypeError: params is not iterable
  GetEnabled       -> (true,)          <- always true, whatever the state was
  GetCapabilities  -> Error: GDBus.Error:org.gnome.gjs.JSError.TypeError: Object 0x… is not a subclass of GObject_Struct, it's a Object
```

and the same suite against the repaired file passes 98/98.

### 7.7 Limitations of the test environment

* The tests run **outside** GNOME Shell. They exercise the real `dbus.js`
  class, its real export/unexport lifecycle and the real D-Bus wire format,
  but with stubbed collaborators — there is no Clutter context, so the
  overlays themselves are not drawn by the test.
* The private bus is a `dbus-daemon` started from the *system* session
  configuration, so it is not byte-for-byte the live session bus; the D-Bus
  protocol behaviour is the same.
* The suite does not restart, reload or re-enable the extension, and does
  not touch the live GSettings.

---

## 8. Phase 1 preservation

Verified by hash comparison against the pre-Phase-2 backup
`/home/mostafa/.local/share/soft-dimmer/backups/20261111T062247Z/`:

| Phase 1 item | How it was verified | Result |
|---|---|---|
| `dimmer.js` unchanged | SHA-256 identical (`2681fe1b…fab35`) | **PASS** |
| Overlay alpha calculation | `dimmer.js` byte-identical; `dimAlpha()` still `round((1.0 - brightness) * 255)` | **PASS** |
| Brightness semantics (1.0 = no dimming) | `dimmer.js` byte-identical | **PASS** |
| Minimum brightness behaviour | `dimmer.js` byte-identical; `clampBrightness()` untouched | **PASS** |
| Implicit background-color transitions | `dimmer.js` byte-identical (`_applyBrightness()` untouched) | **PASS** |
| Rapid-update transition behaviour | `dimmer.js` byte-identical (`TRANSITION_MS = 250`, easing state untouched) | **PASS** |
| Overlay construction and stacking | `dimmer.js` byte-identical (`St.Widget`, `addChrome()`) | **PASS** |
| Capture-detection logic | `dimmer.js` byte-identical (`grab-op-begin/end`, `_setGrabbing()`) | **PASS** |

The Phase 1 logic suite still passes in full:

```
$ gjs -m ~/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer/test-logic.js
43/43 checks passed
```

---

## 9. The live session was not touched

| Check | Before Phase 2 | After Phase 2 |
|---|---|---|
| `gnome-extensions info` → `Enabled` | `Yes` | `Yes` |
| `gnome-extensions info` → `State` | `ACTIVE` | `ACTIVE` |
| `gsettings … brightness` | `1.0` | `1.0` |
| `gsettings … enabled` | `true` | `true` |
| `gsettings … minimum-brightness` | `0.1` | `0.1` |
| `gsettings … monitor-selection` | `'all'` | `'all'` |
| `gsettings … restore-brightness` | `true` | `true` |
| `gsettings … shortcut-*` | `@as []` | `@as []` |
| owner of `io.github.softdimmer` | `:1.16` | `:1.16` |

No extension was enabled, disabled, reloaded or restarted; no GSettings value
was written; no mutating D-Bus method was called against the live session;
ASUSCraft, Soft Brightness Plus and Soft Dimmer Pro were not modified; no
commit or push was performed (the working copy is not a Git repository).

### PENDING — the fix is not live yet

The running Shell still executes the **old** module: GJS caches extension
modules by URL, and GNOME 51 on Wayland refuses an in-place Shell restart
("On wayland we cannot restart"). The live bus therefore still answers with
the pre-fix behaviour:

```
$ gdbus call --session --dest io.github.softdimmer \
      --object-path /io/github/softdimmer/BrightnessControl \
      --method io.github.softdimmer.BrightnessControl.GetBrightness 0
Error: GDBus.Error:org.gnome.gjs.JSError.TypeError: params is not iterable
```

This is expected and is **not** a defect of the repair. Bringing the new
code into the live session requires a **logout/login**, which the task
explicitly forbids during Phase 2. The verification steps for that step are:

```sh
# after logging out and back in:
gdbus call --session --dest io.github.softdimmer \
  --object-path /io/github/softdimmer/BrightnessControl \
  --method io.github.softdimmer.BrightnessControl.GetBrightness 0
#   expect: (1.0,)
gdbus call --session --dest io.github.softdimmer \
  --object-path /io/github/softdimmer/BrightnessControl \
  --method io.github.softdimmer.BrightnessControl.GetEnabled
#   expect: (true,)
gdbus call --session --dest io.github.softdimmer \
  --object-path /io/github/softdimmer/BrightnessControl \
  --method io.github.softdimmer.BrightnessControl.GetCapabilities
#   expect: ({'version': <'1.0'>, 'monitor-count': <1>, 'has-dbus': <true>,
#             'supports-per-monitor': <true>},)
```

**Status: PENDING** — cannot be claimed as passed without a live Shell.

---

## 10. Rollback

The hardened procedure from `SOFT_DIMMER_BACKUP_ROLLBACK_HARDENING_REPORT.md`
§8, applied to the **new** backup. **It was not executed against the live
installation** — it is documented here only.

```sh
# ---- 0. preconditions -------------------------------------------------
B=/home/mostafa/.local/share/soft-dimmer/backups/20261111T062247Z
D=/home/mostafa/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer
P="$(dirname "$D")"
STAGE=/home/mostafa/.local/share/soft-dimmer/staging

# ---- 1. validate the backup (read-only; abort on any failure) --------
set -euo pipefail
test -d "$B/extension"                || { echo "ABORT: no backup tree"; exit 1; }
test -f "$B/MANIFEST.sha256"          || { echo "ABORT: no manifest";   exit 1; }
test -s "$B/MANIFEST.sha256"          || { echo "ABORT: empty manifest"; exit 1; }
( cd "$B" && sha256sum -c MANIFEST.sha256 >/dev/null ) \
                                    || { echo "ABORT: manifest mismatch"; exit 1; }
diff <(cd "$B" && find extension -type f | sort) \
     <(awk '{print $2}' "$B/MANIFEST.sha256" | sort) \
                                    || { echo "ABORT: manifest does not cover the tree"; exit 1; }
case "$B" in /tmp|/tmp/*) echo "ABORT: backup is under /tmp"; exit 1;; esac
echo "OK: backup validated"

# ---- 2. stage the restore (never touches the installation) -----------
mkdir -p "$STAGE"
S="$STAGE/soft-dimmer.$(date -u +%Y%m%dT%H%M%SZ)"
[ "$(stat -c %d "$STAGE")" = "$(stat -c %d "$P")" ] \
  || { echo "ABORT: staging area and extensions directory are on different filesystems"; exit 1; }
rm -rf "$S"; mkdir -p "$S"
cp -a "$B/extension" "$S/extension"
( cd "$S" && find extension -type f -print0 | sort -z | xargs -0 sha256sum ) > "$S/STAGED.sha256"
diff <(sort "$B/MANIFEST.sha256") <(sort "$S/STAGED.sha256") \
  || { echo "ABORT: staged copy does not match the manifest"; rm -rf "$S"; exit 1; }
diff <(cd "$D" && find . -printf '%M %u:%g %y %P\n' | sort -k4) \
     <(cd "$S/extension" && find . -printf '%M %u:%g %y %P\n' | sort -k4) \
  || { echo "ABORT: staged copy has different modes/structure"; rm -rf "$S"; exit 1; }
chmod -R u+rwX "$S/extension"
echo "OK: staged copy verified at $S"

# ---- 3. swap (rename; the old copy stays recoverable) -----------------
OLD="$P/.soft-dimmer.previous.$(date -u +%Y%m%dT%H%M%SZ)"
test -d "$S/extension" || { echo "ABORT: no verified staging directory"; exit 1; }
mv "$D" "$OLD"
if ! mv "$S/extension" "$D"; then
  mv "$OLD" "$D"; echo "ABORT: swap failed, original restored"; exit 1
fi
rm -rf "$S"
echo "OK: restored. Previous installation preserved at $OLD"

# ---- 4. verify -------------------------------------------------------
cd "$D" && find . -type f -print0 | sort -z | xargs -0 sha256sum \
  | sed 's|\./|extension/|' > /tmp/post.sha
diff <(sort "$B/MANIFEST.sha256") <(sort /tmp/post.sha) \
  && echo "OK: installed tree matches the backup"

# ---- 5. retire the previous installation (only after step 4 passes) --
# rm -rf "$OLD"
```

The rollback restores the **pre-Phase-2** state (the broken `dbus.js`). If the
goal is only to undo the Phase 2 change, that is exactly what is wanted.

---

## 11. Remaining risks

| # | Risk | Severity | Mitigation |
|---|---|---|---|
| R1 | The repaired code is not loaded by the running Shell; the live interface still misbehaves until a logout/login | Medium | Documented in §9 with the exact verification commands. Nothing else can be done without restarting the Shell, which the task forbids. |
| R2 | `GetBrightness(-1)` now reports the `brightness` setting rather than the dimmer's `-1` map lookup | Low | This is the behaviour the shipped XML documents ("-1 returns the global value"). The old behaviour returned a hard-coded `1.0`, i.e. a fabricated value. |
| R3 | The unknown-monitor check depends on `MonitorDimmer.getMonitors()`. If the shell has not reported its monitors yet, the check is skipped | Low | Deliberate: rejecting every index during shell start-up would be worse. The range check is skipped only while the list is empty. |
| R4 | The tests use stubs, so the interaction between `dbus.js` and the real `MonitorDimmer`/`GSettings` is not covered end-to-end | Low | The Phase 1 logic suite (43/43) covers the dimmer; the D-Bus layer is covered by the stubbed suite; the gap is closed by the post-login verification in §9. |
| R5 | `dimmer.js` cannot be instantiated outside GNOME Shell (Clutter asserts without a context) | Low | Unchanged from Phase 1; the stub approach avoids it. |
| R6 | Two D-Bus extensions that dim additively must not be enabled together | Low | Pre-existing condition, unchanged, and out of scope for Phase 2. |

---

## 12. Artefacts

| Path | Contents |
|---|---|
| `/home/mostafa/.local/share/soft-dimmer/backups/20261111T062247Z/` | The verified pre-Phase-2 backup (tree + `MANIFEST.sha256` + `MANIFEST.meta` + `state/STATE.txt`) |
| `/home/mostafa/.local/share/soft-dimmer/tests/` | The test suite (`p2-service.js`, `p2-tests.js`, `p2-run-service.sh`) and the captured output `p2-tests.out` |
| `/tmp/opencode/gjs-Gio.js` | The GJS 1.90 `Gio.js` override extracted from `libgjs.so.0` — the authoritative binding contract |
| `/tmp/opencode/dbus/addr.persist` | The address of the private test bus |

---

## 13. Requirement-by-requirement verdict

| Task section | Requirement | Verdict |
|---|---|---|
| 0 | Inspect the current source before editing | PASS |
| 0 | New timestamped, persistent, full-extension backup | PASS — `20261111T062247Z` |
| 0 | Backup outside `/tmp` and outside the extension directory | PASS |
| 0 | SHA-256 manifest verified before editing | PASS — 9/9, exit 0 |
| 0 | Hardened rollback strategy reused | PASS — §10 |
| 0 | The installed extension was never deleted | PASS |
| 0 | ASUSCraft / Soft Brightness Plus / Soft Dimmer Pro untouched | PASS |
| 0 | No GSettings change | PASS |
| 0 | No enable / disable / reload / restart of the extension or Shell | PASS |
| 0 | No mutating D-Bus call against the live session | PASS |
| 0 | `dimmer.js` not modified | PASS — byte-identical |
| 1.1–1.8 | Backup steps | PASS — §1 |
| 2 | Contract documented | PASS — §2, §3 |
| 2 | Contract preserved | PASS — identical XML, identical signatures |
| 3.A | Positional arguments | PASS — §4.A, suite 3/4/5 |
| 3.B | `GetCapabilities` type | PASS — §4.B, suite 6 |
| 3.C | `GetEnabled` real boolean | PASS — §4.C, suite 7 |
| 3.D | Error handling and validation | PASS — §4.D, suite 5 |
| 3.E | Lifecycle | PASS — §4.E, suites 9/10/11 |
| 4 | Test suite, no Shell restart, isolated bus, no live mutation | PASS — §7 |
| 5 | Phase 1 preservation by hash | PASS — §8 |
| 6 | Rollback documented, not executed | PASS — §10 |
| 7 | Report created | PASS — this file |
| 8 | Acceptance criteria | PASS (9 of 10), item 10 PENDING — §0, §9 |
| — | Phase 3 | **NOT STARTED** |
