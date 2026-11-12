# Soft Dimmer — Final Independent Audit Report (GNOME 51 / Fedora 45)

**Extension under audit:** `soft-dimmer@io.github.softdimmer`
**Report date:** 2026-11-10
**Verdict:** **NOT READY — CRITICAL DEFECTS CONFIRMED**

---

## 1. Executive Summary

### 1.1 Scope

This audit independently verifies the repaired Soft Dimmer GNOME Shell extension against the
GNOME 51.1 API surface. The audit was performed **read-only**: no project file was modified,
no Git operation was performed, and no extension enable/disable or shell restart was carried
out.

The audit specifically targeted eight areas raised in the prior engineering report:

1. Correctness of `Meta.Display` `grab-op-begin` / `grab-op-end` usage for screenshot,
   screen-recording and screen-sharing handling.
2. `Cogl.Color` construction and `Clutter.Actor.background_color` behaviour.
3. `Main.layoutManager.addChrome()` stacking semantics (above application windows, below
   critical Shell UI, no input interception, fullscreen behaviour).
4. Signal cleanup and repeated enable/disable lifecycle.
5. Opacity animation correctness and absence of rendering artefacts.
6. Keyboard shortcut registration and removal.
7. ASUSCraft 1.2.2 compatibility and conflict with its bundled Soft Dimmer extension.
8. Every claim in the prior engineering report that lacked runtime evidence.

### 1.2 Environment

| Item | Value |
|---|---|
| OS | Fedora 45 |
| GNOME Shell | 51.1 |
| Session type | Wayland |
| GJS | 1.90.1 |
| Clutter / Mutter | `Clutter-51.typelib`, `Meta-51.typelib` (introspected live) |
| Cogl | `Cogl-51.typelib` |
| Shell resources | `/usr/lib64/gnome-shell/libshell-51.so` (via `gresource`) |

### 1.3 Files Inspected

Extension under audit (user extension directory):

- `~/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer/dimmer.js`
- `~/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer/extension.js`
- `~/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer/indicator.js`
- `~/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer/dbus.js`
- `~/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer/test-logic.js`
- `~/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer/metadata.json`
- `~/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer/schemas/org.gnome.shell.extensions.soft-dimmer.gschema.xml`
- `~/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer/dbus/io.github.softdimmer.BrightnessControl.xml`

Pre-repair baseline (read-only comparison):
`/tmp/opencode/soft-dimmer-primary-backup/`

GNOME Shell 51.1 resources extracted to `/tmp/opencode/` for source-level verification:
`ui/layout.js`, `ui/quickSettings.js`, `ui/popupMenu.js`, `ui/panel.js`, `ui/panelMenu.js`,
`ui/status/brightness.js`, `ui/slider.js`, `ui/misc/barLevel.js`, `ui/windowManager.js`,
`ui/screenshot.js`, `ui/status/remoteAccess.js`, `ui/environment.js`, `ui/appDisplay.js`,
`ui/status/../*`, `ui/extensionSystem.js`, `misc/introspect.js`.

Related components inspected:
`/usr/share/gnome-shell/extensions/soft-dimmer-asc@io.github.asuscraft/` (bundled ASUSCraft
Soft Dimmer variant), and the ASUSCraft 1.2.2 project tree in the repository root.

### 1.4 Tests Performed

All tests were executed from standalone `gjs` processes (never inside a live Shell session)
plus direct D-Bus calls against the **real, unmodified** `dbus.js` module.

| # | Test | Method | Outcome |
|---|---|---|---|
| T1 | `Cogl.Color` construction + field readback | `gjs` runtime | Constructed, fields correct |
| T2 | `Clutter.Actor.opacity` pspec type/range | `GObject.find_property` | `guint`, default 255 |
| T3 | `Clutter.Actor` pspec inventory (`track_hover`, `can_focus`, `reactive`, `background_color`, `opacity`) | `GObject.find_property` | `track_hover`/`can_focus` **MISSING** on `Clutter.Actor` |
| T4 | GJS float→`guint` property conversion | Real C-class `guint` property | `0.25→0`, `0.5→0`, `0.75→0` |
| T5 | Extension's exact `_applyBrightness` math applied to a real `guint` property | `Gio.Socket.timeout` | `1.0→0`, `0.75→0`, `0.5→0`, `0.25→0`, `0→1` |
| T6 | `Meta.GrabOp` enum enumeration | `gjs` runtime | Only move/resize/keyboard ops — **no capture op** |
| T7 | `Meta.Display::grab-op-begin/end` existence + signal ids | `GObject.signal_lookup` | Present, ids 33/34 |
| T8 | GNOME Shell's own use of `grab-op-begin` | `gresource` grep of `ui/keyboard.js:379` | Used only for window move/resize |
| T9 | `org.gnome.Mutter.ScreenCast` presence on session bus | `gdbus introspect` | **Present** |
| T10 | `Gio.DBusExportedObject.wrapJSObject` argument convention (sync + async) | `gjs` + `gdbus call` | Sync = **positional args**; `*Async` = array + invocation |
| T11 | **Real `dbus.js`** `GetBrightness` over live session bus | `gdbus call` | **`TypeError: params is not iterable`** |
| T12 | **Real `dbus.js`** `SetBrightness` over live session bus | `gdbus call` | **`TypeError: params is not iterable`** |
| T13 | **Real `dbus.js`** `GetEnabled` over live session bus | `gdbus call` | Returned `(true,)` — **misleading**, see §4 |
| T14 | **Real `dbus.js`** `GetCapabilities` over live session bus | `gdbus call` | **Failed**: `not a subclass of GObject_Struct` |
| T15 | Verified-correct `a{sv}` return form | `gjs` + `gdbus call` | `invocation.return_value(new GLib.Variant('(a{sv})', [caps]))` → works |
| T16 | `Main.layoutManager.addChrome()` implementation | `ui/layout.js:878` | `uiGroup.add_child()` + `set_child_below_sibling(top_window_group)` |
| T17 | `uiGroup` child ordering at runtime | `ui/layout.js:218-320` | Panel added via `addChrome()` **before** extension |
| T18 | ASUSCraft 1.2.2 tree search for `io.github.softdimmer` / `BrightnessControl` / `SoftDimmer` | `grep -rn` | **No matches anywhere in tree** |
| T19 | ASUSCraft binary D-Bus name ownership | `strings` | Owns only `io.github.asuscontrolcenter.AsusControlCenter` |
| T20 | Bundled ASC extension D-Bus identity | direct file read | `io.github.asuscraft.SoftDimmer` (service **and** interface) |
| T21 | ASC extension constructor validity on GNOME 51 | `gjs` runtime | `TypeError: No property track_hover on ClutterActor` |

### 1.5 Critical Defects Discovered

Three independent, runtime-confirmed defects break core functionality:

- **Finding A — Incorrect opacity scale.** `Clutter.Actor.opacity` is a `guint` (0–255). The
  implementation assigns a 0.0–1.0 float, which GJS truncates to `0`. The overlay is
  effectively transparent for all normal brightness values. (§3)
- **Finding B — Broken D-Bus method arguments.** The synchronous `wrapJSObject` methods
  destructure a non-existent `params` array. `GetBrightness` and `SetBrightness` throw
  `TypeError: params is not iterable`; `GetEnabled` returns a misleading constant;
  `GetCapabilities` fails outright. (§4)
- **Finding C — Wrong capture-detection mechanism.** `Meta.Display::grab-op-begin/end` are
  window grab-operation signals, not capture signals. The implementation reacts to ordinary
  window dragging and does **not** react to screenshots, recording or sharing. (§5)

Additionally, the source-level stacking analysis (Finding in §6) shows the overlay is placed
**above** the GNOME top panel, not below it as the prior report claimed.

### 1.6 Readiness Verdict

**NOT READY — CRITICAL DEFECTS CONFIRMED.**

The extension is not ready for normal use. Findings A, B and C are confirmed functional
breakages, not style or robustness concerns: the dimming overlay does not visibly dim, the
public D-Bus control surface does not work, and the capture-avoidance behaviour is inverted
relative to its purpose.

Even after those three defects are corrected, the runtime test matrix in §8 remains
outstanding. Actor-level behaviour (overlay painting, real stacking, animation smoothness,
lifecycle cleanup) cannot be exercised outside a live GNOME Shell session, and GNOME 51 on
Wayland does not permit the usual in-place Shell restart. One logout/login cycle is required
before any feature can be promoted from PENDING to PASS.

---

## 2. Claims vs Evidence

| Feature / Claim | Evidence | Result | Explanation |
|---|---|---|---|
| `Cogl.Color` construction | T1: `new Cogl.Color({red,green,blue,alpha})` constructed; `red/green/blue/alpha` read back correctly (`10,20,30,40`); `to_string()` → `#0a141e80` | **PASS** | Construction and field semantics confirmed at runtime. Alpha defaults to 0 when omitted. |
| `Clutter.Actor` opacity scale | T2/T4/T5: pspec type `guint`, default 255; float→uint truncates `0.25→0`, `0.5→0`, `0.75→0` | **FAIL** | Opacity is 0–255 integer, not a 0.0–1.0 float. The implementation's `1.0 - brightness` collapses to 0. See §3. |
| Overlay background painting | T3: `background-color` pspec exists, type `CoglColor`; no Clutter context available to instantiate an actor | **PENDING** | Property type is correct, but actual rendering could not be observed outside a live Shell. T1/T5 show the *mechanism* is broken regardless. |
| `Main.layoutManager.addChrome()` | T16: `ui/layout.js:878` — `uiGroup.add_child(actor)` then `set_child_below_sibling(actor, global.top_window_group)` | **PASS** | The API exists and behaves as documented: it inserts into `uiGroup` below `top_window_group`. Verified at source level. |
| Overlay stacking relative to the GNOME top panel | T17: `panelBox` is itself added via `addChrome()` at `ui/layout.js:280`, i.e. **before** any extension runs; a later `addChrome()` insertion lands **above** `panelBox` | **PENDING** | Source analysis shows the overlay is **above** the top panel, contradicting the prior report's "below the top bar" claim. Live visual confirmation outstanding. Source-level ordering logic is sound. |
| Overlay stacking relative to app windows | T16/T17: `window_group` is the first `uiGroup` child; `addChrome()` places the overlay above it | **PASS** | The overlay is above application windows at source level. Live confirmation pending. |
| Overlay stacking vs. modal dialogs / input panel / QS menu | T17: `modalDialogGroup`, `inputPanelGroup` and the QS menu are added via plain `uiGroup.add_child()`, i.e. **above** the `addChrome()` band | **PENDING** | Source analysis indicates these remain above the overlay. Live confirmation pending. |
| Quick Settings integration | T3 + source: `St.Button` exposes `checked`; `QuickToggle`/`SystemIndicator`/`Slider` APIs confirmed present in `ui/quickSettings.js` and `ui/slider.js` | **PENDING** | API surface is correct and the indicator was constructed successfully in isolation. Actual slider interaction, menu rendering and grid placement require a live session. |
| D-Bus `GetBrightness` | T11: real `dbus.js` over live session bus → `TypeError: params is not iterable` | **FAIL** | Method throws on every call. See §4. |
| D-Bus `SetBrightness` | T12: real `dbus.js` over live session bus → `TypeError: params is not iterable` | **FAIL** | Method throws on every call. See §4. |
| D-Bus `GetEnabled` | T13: returned `(true,)`; T-variant test shows a `GLib.Variant` coerces truthy regardless of the underlying boolean | **FAIL** | Returns a constant-like `true` and cannot be distinguished from correct behaviour. Behaviour is misleading and must **not** be counted as a verified success. See §4. |
| D-Bus `GetCapabilities` | T14: real `dbus.js` → `TypeError: Object ... is not a subclass of GObject_Struct` | **FAIL** | `GLib.Variant.new('a{sv}', [caps])` is the wrong construction form. Verified correct form in T15. See §4. |
| D-Bus signal handling (`BrightnessChanged`, `EnabledChanged`) | Source: `_emitSignal()` uses `this._exportedObject.emit_signal(name, variant)`; handlers are disconnected in `unexport()` | **NOT VERIFIED** | Emission was not exercised against a live exported object; signal delivery additionally depends on the exported object being functional, which it currently is not. |
| Screenshot handling | T6/T7/T8: `Meta.GrabOp` contains only window-move/resize/keyboard ops; `grab-op-begin` is emitted by `meta_display_begin_grab_op()`; GNOME Shell uses it only in `ui/keyboard.js:379` for window move detection | **FAIL** | `grab-op-begin/end` are window grab-operation signals, not capture signals. See §5. |
| Screen recording and sharing behaviour | T6/T8/T9: no capture member exists in `Meta.GrabOp`; `org.gnome.Mutter.ScreenCast` **is** present on the session bus | **FAIL** | The implemented mechanism cannot detect recording or sharing. A plausible capture API exists but the correct replacement is **not yet determined**. See §5. |
| Multi-monitor handling | T3: `Meta.Monitor.is_builtin()` and `get_connector()` confirmed present; `Meta.MonitorManager.get_logical_monitors()` present | **PENDING** | APIs exist and the selection logic is unit-testable, but per-monitor overlay creation, geometry and builtin detection were not observed live. |
| Enable/disable lifecycle | Source: `disable()` disconnects settings handlers, unexports D-Bus, destroys the indicator, disables the dimmer | **PENDING** | Code path reviewed; actual teardown was not observed in a live Shell. |
| Signal cleanup | T16–T17 + source: `{object, id}` pairs are disconnected in `disable()`; `addChrome()`-tracked actors are untracked on `destroy` via `_trackActor`'s `destroy` handler | **PENDING** | Structure reviewed at source level; live leak/no-leak behaviour unverified. |
| Keyboard shortcut registration and removal | T: `Main.wm.addKeybinding` / `removeKeybinding` confirmed present (`ui/windowManager.js:1106/1113`); `Meta.KeyBindingFlags.IGNORE_AUTOREPEAT` = 16 confirmed; schema keys are type `as` | **PENDING** | API usage is correct and the schema types are right, but no shortcut was actually bound or triggered in a live session. |
| ASUSCraft integration | T18: `grep -rn` over the full v1.2.2 tree found **no** occurrence of `io.github.softdimmer`, `BrightnessControl` or `SoftDimmer`; T19: the binary owns only `io.github.asuscontrolcenter.AsusControlCenter`; T20: the bundled ASC extension uses `io.github.asuscraft.SoftDimmer`; T21: that extension's `new Clutter.Actor({track_hover, can_focus})` **throws on GNOME 51** | **NOT VERIFIED** | The integration path was **not located**. See §9 for the full follow-up checklist. |

---

## 3. Critical Finding A — Incorrect Opacity Scale

**Status: FAIL**

### 3.1 The defect

`Clutter.Actor.opacity` is a **`guint`** property with the range **0–255** (0 = fully
transparent, 255 = fully opaque). It is *not* a floating-point value in the range 0.0–1.0.

Verified at runtime (T2):

```
Clutter.Actor pspec 'opacity' -> type=guint, default=255
```

The upstream API documentation states the same: *"Opacity of an actor, between 0 (fully
transparent) and 255 (fully opaque)."*

The current implementation in `dimmer.js` computes a 0.0–1.0 float and assigns it directly:

```js
// Brightness 1.0 -> fully transparent, brightness 0 -> fully black.
const target = Math.max(0.0, Math.min(1.0, 1.0 - brightness));
...
actor.opacity = target;
```

### 3.2 Observed conversion behaviour

GJS converts an out-of-range float to the target integer type by **truncation toward zero**.
Measured on a real C-class `guint` property (T4/T5):

```
brightness -> target (1.0 - brightness) -> stored opacity (guint 0..255)
   1.0     -> 0.0    -> 0
   0.75    -> 0.25   -> 0
   0.5     -> 0.5    -> 0
   0.25    -> 0.75   -> 0
   0.0     -> 1.0    -> 1
```

Every brightness value except exactly `0.0` collapses to **0**.

### 3.3 Consequence

The overlay's effective alpha is multiplied by `opacity / 255`. With `opacity == 0` the
overlay is **fully transparent regardless of the background colour**, so:

- The screen does **not dim** at any usable brightness setting.
- The Quick Settings slider appears to move but produces no visible effect.
- The D-Bus `SetBrightness` call path, even if it were functional, would have no visual
  result.

The only non-zero value reachable is `opacity == 1` (i.e. 1/255 ≈ 0.4% alpha) at exactly
`brightness == 0.0`, which is imperceptible.

### 3.4 Note on the previous approach

The pre-repair implementation modulated the **alpha channel of `background_color`** on a
`0–255` scale:

```js
const alpha = Math.round((1.0 - brightness) * 255);
actor.background_color = { red: 0, green: 0, blue: 0, alpha };
```

That is structurally the more appropriate approach for this API surface, because the colour
alpha is natively expressed in the same 0–255 units. The switch to `opacity` introduced the
scale mismatch. This observation is recorded for audit completeness only — **no fix is
prescribed or implemented in this report.**

---

## 4. Critical Finding B — Broken D-Bus Method Arguments

**Status: FAIL**

### 4.1 The defect

The D-Bus interface is exported via `Gio.DBusExportedObject.wrapJSObject()`. The methods in
`dbus.js` are written in the *older* GJS convention, where a single `params` array carried
all inbound arguments:

```js
GetBrightness(params) {
    const [monitorIndex] = params;
    ...
}
SetBrightness(params) {
    const [monitorIndex, brightness] = params;
    ...
}
SetEnabled(params) {
    const [enabled] = params;
    ...
}
```

In **GJS 1.90.1**, `wrapJSObject()` dispatches synchronous methods with **positional
arguments**, not a packed array. Measured directly (T10):

```
One  (1 in-arg)  -> args.len=2  types=number,object  vals=[5,null]
Two  (2 in-args) -> args.len=3  types=number,number,object  vals=[5,2.5,null]
```

The first parameter is the first in-argument itself (the number `5`), so `params` is a
`number`. Destructuring it as an iterable throws immediately.

### 4.2 Real session-bus test results

The **actual, unmodified `dbus.js`** module was imported into a standalone GJS process, its
`SoftDimmerDBus` class instantiated with stub settings/dimmer objects, and its `export()`
method invoked so that it genuinely owned `io.github.softdimmer` on the live session bus. It
was then called with `gdbus call`:

| Method | Observed result | Status |
|---|---|---|
| `GetBrightness` | `GDBus.Error: org.gnome.gjs.JSError.TypeError: params is not iterable` | **FAIL** |
| `SetBrightness` | `GDBus.Error: org.gnome.gjs.JSError.TypeError: params is not iterable` | **FAIL** |
| `GetEnabled` | `(true,)` — **misleading** | **FAIL** |
| `GetCapabilities` | `GDBus.Error: org.gnome.gjs.JSError.TypeError: Object 0x… is not a subclass of GObject_Struct, it's a Object` | **FAIL** |

Server-side log confirmed the same failures inside the shell of the GJS process:

```
Gjs-WARNING **: Exception in method call: GetBrightness: TypeError: params is not iterable
Gjs-WARNING **: Exception in method call: SetBrightness: TypeError: params is not iterable
Gjs-WARNING **: Exception in method call: GetCapabilities: TypeError: Object … is not a subclass of GObject_Struct
```

### 4.3 Per-method notes

**`GetBrightness` — FAIL.** Always throws. No client can read the current brightness.

**`SetBrightness` — FAIL.** Always throws. No client can set brightness through D-Bus.
Because the method throws before reaching its validation logic, none of the documented
input-validation behaviour (`monitor_index >= -1`, finite brightness, range
`[minimum-brightness, 1.0]`) is reachable — the errors are masking, not exercising, that
code.

**`GetEnabled` — FAIL (misleading).** This method takes no inbound arguments, so it never
destructures `params` and therefore does not throw. However it returns
`[GLib.Variant.new('b', [value])]`, and a `GLib.Variant` object is **always truthy** when
coerced to a boolean. Measured:

```
Variant('b',[true])  as JS boolean -> true
Variant('b',[false]) as JS boolean -> true
```

A separate control confirmed the behavioural difference (T13/T-variant): an implementation
returning `[false]` yields `(true,)`, whereas returning a bare `false` yields `(false,)`.
Consequently `GetEnabled` reports `true` regardless of the actual setting. It must **not**
be recorded as a verified functional success — it "works" only in the sense that it does not
throw.

**`GetCapabilities` — FAIL.** Two independent problems:

1. The construction `GLib.Variant.new('a{sv}', [caps])` where `caps` maps to `GLib.Variant`
   values throws `Object … is not a subclass of GObject_Struct`.
2. Even when the variant is constructed correctly, a synchronous method must return a
   **plain** value, not a `GLib.Variant`.

### 4.4 Verified correct calling convention (audit evidence only)

The following forms were verified working against a live session bus in an isolated test
harness (T10, T15). They are recorded as **evidence of the correct convention** for the
correcting work; **no project file was modified to apply them.**

For a **synchronous** method — positional inbound arguments, plain outbound values:

```js
GetB(monitorIndex) {
    return [0.5];          // plain value, not a GLib.Variant
}
```
Verified: `GetB(3)` → `(0.5,)`.

For an `a{sv}` out-argument — the **async** form with `invocation.return_value()` and a
tuple-wrapped variant:

```js
GetCAsync(params, invocation) {
    const caps = {'version': GLib.Variant.new_string('1.0')};
    invocation.return_value(new GLib.Variant('(a{sv})', [caps]));
}
```
Verified: `GetC()` → `({'version': <'1.0'>},)`.

Note that `new GLib.Variant('(a{sv})', [caps])` succeeds while
`new GLib.Variant('a{sv}', [caps])` fails — the tuple wrapper is required.

### 4.5 Conclusion

The D-Bus control surface — the extension's documented public interface, matching the
shipped introspection XML at `dbus/io.github.softdimmer.BrightnessControl.xml` — is
**non-functional**. The interface XML is unchanged from the pre-repair baseline, but the
implementing code does not satisfy it.

---

## 5. Critical Finding C — Screenshot and Recording Detection

**Status: FAIL**

### 5.1 The defect

`dimmer.js` connects to `Meta.Display::grab-op-begin` and `Meta.Display::grab-op-end` and
treats them as capture events:

```js
// Meta.Display raises these around a screen grab - a screenshot, a
// screen recording or a screen share.  The overlays step out of the
// way for the duration so the capture is not darkened.
this._connect(global.display, 'grab-op-begin', () => this._setGrabbing(true));
this._connect(global.display, 'grab-op-end',   () => this._setGrabbing(false));
```

This interpretation is incorrect.

### 5.2 Verified evidence

**The `Meta.GrabOp` enumeration contains no capture operation** (T6). Complete runtime
enumeration:

```
NONE, WINDOW_BASE, MOVING, MOVING_UNCONSTRAINED,
RESIZING_NW, RESIZING_N, RESIZING_NE, RESIZING_E,
RESIZING_SW, RESIZING_S, RESIZING_SE, RESIZING_W,
KEYBOARD_MOVING, KEYBOARD_RESIZING_UNKNOWN,
KEYBOARD_RESIZING_NW, KEYBOARD_RESIZING_N, KEYBOARD_RESIZING_NE,
KEYBOARD_RESIZING_E, KEYBOARD_RESIZING_SW, KEYBOARD_RESIZING_S,
KEYBOARD_RESIZING_SE, KEYBOARD_RESIZING_W
```

Every member describes **window movement or window resizing**, with or without the
keyboard modifier. There is no screenshot, recording, casting or screen-capture member.

**GNOME Shell 51.1 uses the signal only for window manipulation** (T8). The sole consumer
in the entire Shell is `ui/keyboard.js:379`:

```js
'grab-op-begin', (display, window, op, _sprite) => {
    if (window === this._currentWindow &&
        (op === Meta.GrabOp.MOVING || op === Meta.GrabOp.KEYBOARD_MOVING))
        this.emit('window-grabbed');
},
```

The signal is filtered specifically for `MOVING` / `KEYBOARD_MOVING` to detect that a
**window is being dragged** — which is precisely the semantic the extension is misreading.

**Upstream provenance** (Mutter history): `grab-op-begin`/`grab-op-end` were introduced in
2012 *"display: Add signals for plugins to know when a grab op begins/ends"*, emitted from
`meta_display_begin_grab_op()` / `meta_display_end_grab_op()` — the window grab-operation
entry points.

### 5.3 Behavioural consequences

The implementation is therefore **inverted** relative to its stated purpose:

- **Screenshots, screen recording and screen sharing are NOT detected.** The overlays remain
  in place during capture, so captures still include the dimming overlay — the exact problem
  the feature was introduced to solve.
- **Ordinary window dragging DOES trigger the handlers.** Moving or resizing any window
  raises `grab-op-begin`, so `_setGrabbing(true)` runs and `hideOverlays()` hides the overlay
  for the duration of the drag. The screen will visibly flash to full brightness while the
  user moves a window, then dim again on release.

The second point is a user-visible regression that is independent of the missing capture
handling.

### 5.4 Candidate API — not a solution

`org.gnome.Mutter.ScreenCast` **was confirmed present** on this machine's session bus (T9):

```
node /org/gnome/Mutter/ScreenCast {
  interface org.gnome.Mutter.ScreenCast {
      CreateSession(in  a{sv} properties, out o session_path);
```

The Shell also tracks capture state via `Main.screenshotUI.screencast-in-progress`
(`ui/screenshot.js:1242`) and the `ScreenRecordingIndicator` / `ScreenSharingIndicator`
panel items (`ui/status/remoteAccess.js`).

**This is explicitly recorded as a lead requiring further investigation, not as a fix.** The
correct capture-handling strategy for GNOME 51.1 — covering screenshots taken through the
Shell's screenshot UI, screen recording through the Shell, and sharing through the portal —
has **not** been determined, and no replacement implementation has been validated. Any
correcting work must establish which mechanism covers each capture path before relying on it.

---

## 6. Layer Stacking Analysis

**Source-level stacking analysis: PASS.**
**Actual live-shell visual behaviour: PENDING.**

### 6.1 Verified `addChrome()` behaviour

From `ui/layout.js:878` (T16):

```js
addChrome(actor, params) {
    this.uiGroup.add_child(actor);
    if (this.uiGroup.contains(global.top_window_group))
        this.uiGroup.set_child_below_sibling(actor, global.top_window_group);
    this._trackActor(actor, params);
}
```

Verified properties:

- The actor is inserted into `uiGroup` — the Shell's UI container that also holds
  `global.window_group` (application windows) and `global.top_window_group`.
- The actor is positioned **below** `top_window_group`.
- `params` supports `trackFullscreen` and `affectsStruts`; the extension passes
  `{trackFullscreen: false, affectsStruts: false}`, which is the correct choice for a
  full-screen overlay that should neither hide on fullscreen windows nor alter struts.

### 6.2 Resulting ordering — the top-panel correction

The `uiGroup` child order is established at `ui/layout.js:218–320` (T17). Relevant
sub-sequence:

1. `global.window_group` — first child (line 224).
2. `global.top_window_group` — added at line 251.
3. `overviewGroup` — `addChrome()` (line 262).
4. `screenShieldGroup` — `addChrome()` (line 274).
5. **`panelBox` (the GNOME top panel) — `addChrome()` (line 280).**
6. `modalDialogGroup` — plain `uiGroup.add_child()` (line 291) → **above** the `addChrome` band.
7. `inputPanelGroup` — plain `uiGroup.add_child()` (line 295) → **above** the `addChrome` band.
8. `dummyCursor`, `feedbackGroup` — plain `add_child()`.
9. `screenshotUIGroup` — `addTopChrome()` (line 309) → topmost.

Because `panelBox` is itself inserted through `addChrome()`, it sits **inside** the
`addChrome` band. An extension's `addChrome()` insertion performed later (extensions are
enabled after `layoutManager.init()` completes) is appended to `uiGroup` and then moved
directly below `top_window_group` — that is, **above `panelBox`**.

Therefore, at source level:

- The overlay is **above** application windows (`window_group` is the bottom-most child).
- The overlay is **above** the GNOME top panel / `panelBox`.
- The overlay is **below** `top_window_group`, and therefore below `modalDialogGroup`,
  `inputPanelGroup` (OSD) and the Quick Settings menu, which are added above the band.

**This corrects the prior engineering report**, which stated the overlay sits *"below the top
bar and the Quick Settings menu"*. Only the second half is correct; the top-panel half is
not.

### 6.3 Input interception

`reactive: false` is set on the overlay (verified: `Clutter.Actor` pspec `reactive` exists,
type `gboolean`). A non-reactive actor does not participate in Clutter picking, so it does
not intercept pointer events. `affectsStruts: false` prevents the overlay from altering
workspace struts. This reasoning is sound, but the practical outcome in a live session is
**PENDING** verification.

### 6.4 Fullscreen behaviour

`trackFullscreen: false` means the overlay is **not** hidden when a fullscreen window is
present — the correct behaviour for a dimming overlay that should apply to fullscreen
content too. Source-level reasoning is sound; live behaviour is **PENDING**.

### 6.5 Fullscreen / stacking caveat

`addChrome()` tracks the actor for input-region and fullscreen bookkeeping via
`_trackActor()`, which connects a `'destroy'` handler that untracks the actor. Because the
extension destroys its overlay actors on teardown, this pairing is structurally sound — but
leak/no-leak behaviour is **PENDING** live confirmation.

---

## 7. Cogl.Color Verification

**Status: PASS (construction only)**

Verified at runtime (T1):

```
Cogl.Color typeof: function
fields: 0 0 0 255
fields2: 10 20 30 128
is Cogl.Color instance: true
no-alpha alpha= 0
to_string: #0a141e80
```

Findings:

- **Construction succeeded.** `new Cogl.Color({red, green, blue, alpha})` produced a valid
  instance with the expected field values.
- **Fields were read back correctly.** `red`, `green`, `blue`, `alpha` all round-trip.
- **Alpha defaults to zero.** When `alpha` is omitted the constructed colour is fully
  transparent (`alpha = 0`). Any code constructing a `Cogl.Color` must pass `alpha`
  explicitly.
- **The property type is `CoglColor`.** `Clutter.Actor`'s `background-color` pspec is
  confirmed as type `CoglColor` (and `background-color-set` exists as a read-only companion).
- **GJS also accepts a plain object** for the property. Assigning `{red, green, blue, alpha}`
  directly to a `CoglColor` property succeeded in an isolated test
  (`assign PLAIN OBJECT -> 2`). The explicit `new Cogl.Color(...)` form is preferable for
  clarity and is the form the Shell itself uses
  (`ui/appDisplay.js:57,2712` → `new Cogl.Color({...})`).
- **`Clutter.Color` no longer exists** in this GNOME version; `Cogl.Color` is the correct
  replacement. Confirmed by enumeration and by the Shell's own usage.
- **`Clutter.Actor` has no `track_hover` or `can_focus` pspec** (T3), so passing either to
  the constructor throws `TypeError: No property track_hover on ClutterActor` (T21 confirms
  this for the bundled ASC extension). `St.Widget` *does* expose both.

**Explicitly not inferred:** none of the above demonstrates that the overlay *paints*
correctly inside GNOME Shell. The painting path requires a live Clutter context and remains
**PENDING** (§8). Moreover, even if painting works, Finding A means the dimming result would
still be invisible.

---

## 8. Runtime Tests Still Pending

### 8.1 Why these cannot be completed from an external GJS process

- **Actor construction requires a live Clutter context.** Every attempt to instantiate a
  `Clutter.Actor` outside the Shell aborts with:

  ```
  Clutter:ERROR:../clutter/clutter/clutter-main.c:66:_clutter_context_get_default: assertion failed: (ClutterCntx)
  Bail out!
  ```

  This blocks direct verification of overlay painting, `opacity` rendering, `addChrome()`
  stacking and the `background_color` setter in a real scene graph.

- **GNOME 51 on Wayland does not allow the usual Shell restart.**
  `org.gnome.Shell@.service` refuses with *"On wayland we cannot restart"*, and GJS caches
  extension modules by URL, so a plain extension reload cannot pick up new code either.

- **A logout/login is required** for reliable live-Shell validation of any change, since
  module caching survives an in-session reload.

### 8.2 Test matrix

| Test | Current Status | Required Validation |
|---|---|---|
| Extension enable | PENDING | Enable after a fresh login; confirm `gnome-extensions info` reports `State: ACTIVE` |
| Overlay creation | PENDING | Confirm a `Clutter.Actor` is actually parented under `uiGroup` |
| Background painting | PENDING | Verify a visible darkening on screen at several brightness values |
| Brightness adjustment | PENDING | Test several values end-to-end and confirm the rendered alpha tracks the request |
| Quick Settings slider | PENDING | Move the slider and observe the overlay change in real time |
| D-Bus read methods | FAIL | Retest `GetBrightness` / `GetEnabled` / `GetCapabilities` after correction |
| D-Bus write methods | FAIL | Retest `SetBrightness` / `SetEnabled` after correction |
| Screenshot behavior | FAIL | Test after the capture-detection mechanism is redesigned |
| Recording/sharing behavior | PENDING | Test each supported capture path once a mechanism is chosen |
| Panel stacking | PENDING | Visually inspect the top panel and confirm the intended ordering |
| Multi-monitor | PENDING | Test each connected monitor and the `builtin` / `external` selections |
| Disable cleanup | PENDING | Confirm the overlay actor is removed and all signal handlers are disconnected |
| Re-enable lifecycle | PENDING | Repeat enable/disable several times and watch for duplicated overlays or leaked handlers |
| Logout/login persistence | PENDING | Verify settings and extension state survive a session restart |

**None of these tests were performed during this audit.** They require a live GNOME Shell
session and, for several rows, corrected source.

---

## 9. ASUSCraft Integration — Incomplete Investigation

**Status: NOT VERIFIED**

### 9.1 Verified facts

- The ASUSCraft v1.2.2 project tree was searched recursively for `io.github.softdimmer` and
  `BrightnessControl` (T18). **No references were found.** A broader search for the string
  `SoftDimmer` across the entire tree likewise returned **no matches**.
- The search for the assumed ASUSCraft-specific interface also returned no matches.
  Consequently, the actual integration path of `soft-dimmer-asc@io.github.asuscraft` has
  **not been located** in the v1.2.2 tree.
- The shipped `asus-control-center` binary owns exactly one D-Bus name:
  `io.github.asuscontrolcenter.AsusControlCenter` (T19). The string `SoftDimmer` does not
  appear in the binary at all. The service `io.github.asuscraft.SoftDimmer` is **not
  currently registered** on the session bus.
- The bundled extension at `/usr/share/gnome-shell/extensions/soft-dimmer-asc@io.github.asuscraft/`
  exists on this system (installed by no RPM package — `rpm -qf` reports *"not owned by any
  package"*). Its `dbus_client.js` declares, by direct file read (T20):
  - service name: `io.github.asuscraft.SoftDimmer`
  - object path: `/io/github/asuscraft/SoftDimmer`
  - interface name: `io.github.asuscraft.SoftDimmer`
  - methods: `GetBrightness`, `SetBrightness`, `GetEnabled`, `SetEnabled`,
    `GetMinimumBrightness`, `SetMinimumBrightness`, `GetTransitionMs`, `SetTransitionMs`,
    `GetStatus`
- That bundled extension is a **D-Bus client**, not a provider: it connects *to*
  `io.github.asuscraft.SoftDimmer` and *"Falls back gracefully when ASUSCraft is not
  running."*
- The bundled ASC extension's overlay creator still uses `new Clutter.Actor({ … track_hover:
  false, can_focus: false … })` (T21). On GNOME 51 that pattern **throws**
  `TypeError: No property track_hover on ClutterActor`, so the bundled ASC extension cannot
  create overlays on this GNOME version as written.
- The audited extension exports a **different** bus name (`io.github.softdimmer`, object
  `/io/github/softdimmer/BrightnessControl`), so there is no D-Bus name collision with the
  bundled ASC extension's client-side name.
- `src/control/softdim.cpp` in the v1.2.2 tree installs and enables
  **`soft-brightness-plus@joelkitching.com`**, not the ASC Soft Dimmer extension.
  `soft-brightness-plus` is currently enabled in
  `org.gnome.shell enabled-extensions` and draws its own overlay under `Main.uiGroup`,
  meaning a second, independent dimming layer is active at the same time as the audited
  extension.

### 9.2 Conclusion

Compatibility between `soft-dimmer@io.github.softdimmer` and ASUSCraft 1.2.2 is **NOT
VERIFIED**. The integration path was not located in the shipped tree, the bus name the
bundled ASC extension expects is not provided by the v1.2.2 binary, and the bundled ASC
extension's own overlay code is itself incompatible with GNOME 51.

### 9.3 Follow-up investigation checklist

1. Inspect the complete ASUSCraft v1.2.2 directory tree, including packaging and install
   paths, not only `src/`.
2. Locate all D-Bus interface declarations and XML files shipped or referenced by the
   project (`*.xml` under `data/`, `dbus-interfaces/`, etc.).
3. Search the full tree for `Gio.DBusProxy`, `Gio.DBusConnection`, `wrapJSObject`,
   `g_bus_get`, `g_dbus_proxy_new_for_bus` and related calls.
4. Locate the code that enables, disables or communicates with the bundled extension
   (`gnome-extensions` invocation in `src/control/softdim.cpp` and any sibling module).
5. Inspect the bundled extension's `metadata.json`, `dbus_client.js`, `dimmer.js` and
   settings schema in full.
6. Establish the exact service name, object path, interface name and method signatures used
   on each side of the integration.
7. Compare both extensions' ownership of overlay state and brightness to determine whether
   simultaneous activation can produce duplicate overlays or conflicting settings.
8. Report compatibility **only** after the evidence above confirms it.

**No ASUSCraft file and no extension file was modified during this investigation.**

---

## 10. Final Recommendations

### Priority 1 — Critical

1. **Correct opacity handling.** Align the dimming expression with the `guint` 0–255 scale of
   `Clutter.Actor.opacity`, or return to modulating the `background_color` alpha on its
   native 0–255 scale. Verify the rendered result in a live session, not only by inspection.
2. **Correct D-Bus method argument handling.** Adopt the GJS 1.90 dispatch convention:
   positional arguments for synchronous methods, or the `*Async(params, invocation)` form
   with `invocation.return_value(...)`.
3. **Correct D-Bus return values.** Return plain values (not `GLib.Variant`) from
   synchronous methods, and construct `a{sv}` payloads as
   `new GLib.Variant('(a{sv})', [caps])` inside `invocation.return_value(...)`. Re-check
   `GetEnabled` specifically, since it currently returns a truthy constant and would mask
   regressions.

### Priority 2 — High

4. **Replace the incorrect screenshot detection.** `Meta.Display::grab-op-begin/end` must not
   be used for capture handling — they detect window drags and currently cause the overlay
   to flash off during ordinary window movement. Research and validate a capture-handling
   strategy that covers the screenshot UI, screen recording and screen sharing paths on
   GNOME 51.1 before relying on it. `org.gnome.Mutter.ScreenCast` and the Shell's
   `screencast-in-progress` state are candidate leads, not verified solutions.
5. **Verify actual overlay stacking.** Confirm in a live session that the intended ordering
   holds, and specifically determine whether the overlay should sit above or below the GNOME
   top panel; the current `addChrome()` call site places it above.
6. **Verify actor painting in a live GNOME Shell session.** Confirm that the overlay
   background is rendered as expected and that the opacity/alpha transition produces no
   artefacts.

### Priority 3 — Integration

7. **Complete the ASUSCraft D-Bus investigation** per the checklist in §9.3, and record the
   service name, object path, interface and method signatures actually in use.
8. **Confirm only one dimming engine owns the overlay.** `soft-brightness-plus@joelkitching.com`
   is currently enabled alongside the audited extension and draws its own overlay; establish
   the intended ownership model and make the active configuration unambiguous.
9. **Verify cleanup and lifecycle behaviour**, including repeated enable/disable cycles and
   the absence of duplicated overlays or leaked signal handlers.

### Verification requirement

After any correction, the entire §8 matrix must be re-run in a live session following a
logout/login cycle. Static review and unit-level tests are not sufficient to promote any
PENDING row to PASS — in particular, the existing `test-logic.js` suite exercises only the
pure helper functions and cannot detect Findings A, B or C.

---

## 11. Final Verdict

# NOT READY — CRITICAL DEFECTS CONFIRMED

The repaired Soft Dimmer extension does not currently provide working functionality on
Fedora 45 / GNOME Shell 51.1 / Wayland:

- **The dimming overlay does not visibly dim.** `Clutter.Actor.opacity` is a 0–255 `guint`,
  but the implementation assigns a 0.0–1.0 float that truncates to `0` for every usable
  brightness value.
- **The D-Bus control surface does not work.** `GetBrightness` and `SetBrightness` throw
  `TypeError: params is not iterable` on every call; `GetCapabilities` fails outright;
  `GetEnabled` returns a misleading constant.
- **Capture handling is inverted.** `Meta.Display::grab-op-begin/end` are window
  grab-operation signals; they detect ordinary window dragging and do not detect screenshots,
  recording or sharing.

In addition, source-level stacking analysis contradicts the prior report's claim that the
overlay sits below the GNOME top panel.

All three findings were confirmed by direct runtime execution against the real, unmodified
extension code — not by code inspection. The remaining live-Shell behaviours listed in §8
remain unverified and must be exercised after correction and after a logout/login cycle.

**This audit made no code changes. The extensions require correction and live validation
before normal use.**

---

*Report generated 2026-11-10. All tests were executed read-only; no project source file was
modified, no Git operation was performed, and no extension state was altered.*
