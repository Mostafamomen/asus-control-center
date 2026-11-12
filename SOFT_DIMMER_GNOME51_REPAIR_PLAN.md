# Soft Dimmer — GNOME 51 Repair Architecture & Implementation Plan

**Extension:** `soft-dimmer@io.github.softdimmer`
**Target:** Fedora 45 · GNOME Shell 51.1 · Wayland · GJS 1.90
**Plan date:** 2026-11-10
**Status:** READ-ONLY planning. No source file was edited, no setting changed, no extension
enabled/disabled, no shell restarted, no mutating D-Bus method invoked.

**Audit inputs:** `SOFT_DIMMER_GNOME51_FINAL_AUDIT.md`, `ASUSCRAFT_SOFT_DIMMER_DBUS_INVESTIGATION.md`

---

## 1. Executive Summary

Six confirmed defects block correct behaviour on GNOME 51.1. Three are unambiguous
unit-level bugs that can be fixed and proven without a live Shell; one is a broken
integration with the platform that needs a design decision; one is a layering defect whose
correct target depends on a product decision; and one is the absence of runtime evidence
itself.

**Brightness semantics were traced, not assumed.** The schema description is explicit —
*"1.0 means no dimming; lower values apply a darker overlay"* — and `indicator.js:107-121`
confirms the slider maps `minimum-brightness … 1.0` onto `0.0 … 1.0`. So `brightness` is
**remaining luminance**, and **dim strength = `1.0 − brightness`**. Every plan below preserves
this. The `minimum-brightness` key (default 0.10) is the floor on *remaining luminance*,
i.e. a maximum dim of 90 %. The original pre-repair implementation used exactly this
algebra against `background_color` alpha, which was semantically correct.

**The single most important correction:** the repaired code moved the dim level from the
colour alpha (0–255) to `actor.opacity`, which is a `guint` 0–255 — but the value assigned
is a 0.0–1.0 float. GJS truncates it, so every usable brightness becomes `opacity == 0` and
the overlay is fully transparent. The repair fixed a real bug (a deprecated-API cleanup) and
introduced a more severe one. The plan below returns the dim level to a 0–255 channel and
treats the 0–1/0–255 boundary as an explicitly tested invariant.

**Capture handling cannot be made universal, and the plan says so.** The investigation
established that `org.gnome.Mutter.ScreenCast` exposes only `CreateSession` — there is no
enumeration API and no "a capture is happening" signal. The Shell's own
`org.gnome.Shell.Screenshot` and `org.gnome.Shell.Screencast` interfaces declare **no
capture-state signals** either (only `Error` on Screencast). The one reliable in-process
hook is `Main.screenshotUI`'s `screencast-in-progress` property, which covers the Shell's own
screenshot UI — but **not** third-party capture tools, portal-driven sharing, or the
`org.gnome.Shell.Screenshot` D-Bus path. The plan therefore proposes a *scoped, honestly
documented* mechanism plus a permanent limitation statement, rather than a fragile
workaround that appears to work.

**Work is split into seven small phases**, each independently reviewable, each with its own
acceptance criteria and rollback. No phase combines unrelated fixes.

---

## 2. Root-Cause Analysis

### 2.1 Defect 1 — Opacity scale mismatch (Critical)

**Mechanism.** `Clutter.Actor.opacity` is `guint`, range 0–255. The implementation computes
`target = 1.0 - brightness` (0.0–1.0) and assigns it to `actor.opacity`. GJS truncates a
float to the target integer type. Measured on a real C-class `guint` property:

```
brightness -> target -> stored opacity (guint 0..255)
   1.0     -> 0.0    -> 0
   0.75    -> 0.25   -> 0
   0.5     -> 0.5    -> 0
   0.25    -> 0.75   -> 0
   0.0     -> 1.0    -> 1
```

**Effect.** The overlay's effective alpha is `opacity/255`. At `opacity == 0` the overlay is
invisible regardless of `background_color`. **The extension does not dim at all.**

**Contributing factor.** The pre-repair code expressed the same dim level as the *alpha of
the colour* (`Math.round((1.0 - brightness) * 255)`), which is natively 0–255 and therefore
correct. The change to `opacity` was made while cleaning up other issues and silently
changed units.

**Invariant to enforce.** Any 0.0–1.0 "strength" value must be converted to a 0–255 channel
**explicitly and in one place**. A unit test must assert the conversion, because the
symptom (invisible overlay) is indistinguishable from "the brightness is being applied but
is too subtle to see" in manual testing.

### 2.2 Defect 2 — D-Bus method argument convention (Critical)

**Mechanism.** In GJS 1.90, `Gio.DBusExportedObject.wrapJSObject()` invokes **synchronous**
methods with **positional** arguments. Measured:

```
One  (1 in-arg)  -> args.len=2  types=number,object        vals=[5,null]
Two  (2 in-args) -> args.len=3  types=number,number,object  vals=[5,2.5,null]
```

The first parameter is the first in-argument *as a scalar*, not an array. The implementation
destructures it as an iterable:

```js
GetBrightness(params) {
    const [monitorIndex] = params;   // params is a number -> TypeError
```

**Effect.** Confirmed against the **real, unmodified** `dbus.js` driven over the live session
bus:

| Method | Observed |
|---|---|
| `GetBrightness` | `TypeError: params is not iterable` |
| `SetBrightness` | `TypeError: params is not iterable` |
| `GetCapabilities` | `TypeError: Object … is not a subclass of GObject_Struct` |

### 2.3 Defect 3 — D-Bus return-value marshalling (Critical)

Two independent faults:

**(a) `GLib.Variant` returned where a plain value is required.** A synchronous method must
return plain values. Returning `[GLib.Variant.new('d', [0.5])]` produced `(nan,)`; returning
`[GLib.Variant.new('b', [false])]` produced `(true,)` — because a `GLib.Variant` object is
**always truthy**, so `GetEnabled` reports `true` regardless of the real setting. This is
worse than a hard failure: it is a *plausible-looking wrong answer*.

**(b) `a{sv}` construction.** `GLib.Variant.new('a{sv}', [caps])` throws
`not a subclass of GObject_Struct`. The verified-working forms are
`new GLib.Variant('(a{sv})', [caps])` — a **tuple** wrapper — used with
`invocation.return_value()` in the async method form.

**Effect.** `GetEnabled` silently lies; `GetCapabilities` fails outright.

### 2.4 Defect 4 — Wrong capture-detection mechanism (High)

**Mechanism.** `Meta.GrabOp` enumerates *only* window manipulation operations:
`NONE, WINDOW_BASE, MOVING, MOVING_UNCONSTRAINED, RESIZING_{NW,N,NE,E,SW,S,SE,W},
KEYBOARD_MOVING, KEYBOARD_RESIZING_{UNKNOWN,NW,N,NE,E,SW,S,SE,W}`. There is **no** capture
member. Mutter emits `grab-op-begin/end` from `meta_display_begin_grab_op()` /
`meta_display_end_grab_op()` — the window grab entry points. GNOME Shell's only consumer
filters it explicitly for window movement (`ui/keyboard.js:379`).

**Effect — inverted.** The overlay currently hides during **ordinary window dragging**
(a user-visible brightness flash on every window move) and stays visible during **actual
screenshots, recording and sharing**.

### 2.5 Defect 5 — Overlay stacked above the top panel (Medium)

**Mechanism.** `addChrome()` (`ui/layout.js:878`) does `uiGroup.add_child(actor)` then
`set_child_below_sibling(actor, global.top_window_group)`. The `panelBox` is itself added
through `addChrome()` at `ui/layout.js:280`, i.e. **before** any extension runs. A later
`addChrome()` insertion is moved to sit directly below `top_window_group`, which places it
**above** `panelBox`.

**Effect.** The GNOME top panel is dimmed along with the desktop. Whether that is desired is
a product decision (see §6), but the prior report's claim that the overlay sits "below the
top bar" is false.

### 2.6 Defect 6 — No runtime evidence (Process)

Every claim about actor painting, real stacking, animation smoothness and lifecycle cleanup
rests on source reading. Standalone `gjs` cannot construct a `Clutter.Actor` (the
`_clutter_context_get_default` assertion), and GNOME 51 on Wayland refuses in-place Shell
restart. This is a **process** defect: the previous repair was declared complete without a
single live-Shell observation, which is precisely how defects 1–5 survived.

### 2.7 Defect 7 — Teardown race in the extension's own `disable()` (Medium)

`enable()` constructs the dimmer and indicator **synchronously**, but `disable()` nulls
`this._settings` unconditionally. Because `enable()` also assigns `this._restored = false`
and `disable()` does not reset it, a disable→enable cycle re-evaluates `restore-brightness`
from the previous session's `_restored` flag — the "load-time only" semantics are therefore
not reliably load-time. Additionally `disable()` sets `this._settings = null` while
`this._indicator`'s slider retains a reference to the old settings object, and
`this._settingsIds` is nulled *after* being used, leaving no way to detect a double-disable.

*(Note: the bundled ASC extension has an analogous, worse race — it creates resources inside
an unresolved `.then()`, so `disable()` can run before the resources exist. That is the ASC
extension's problem, not this one's, and is explicitly out of scope per §5.)*

---

## 3. Proposed Architecture

### 3.1 Overlay rendering

**Decision: keep the dim level in the colour alpha (0–255), and treat `opacity` as
forbidden for dimming.**

Rationale:

- The alpha channel is natively 0–255, matching the brightness algebra directly with no
  conversion.
- It restores the pre-repair behaviour, which was semantically correct.
- It keeps the actor's `opacity` free for the one thing it is good at: a global
  multiply/visibility transition, if ever needed.
- It eliminates the unit-mismatch class of bug entirely, because there is no second scale in
  play.

**Brightness mapping (preserve existing semantics exactly).**

```
brightness            : remaining luminance, 1.0 = undimmed  (schema: "1.0 means no dimming")
minimum-brightness    : floor on remaining luminance (default 0.10) -> max dim = 90%
dimStrength           = 1.0 - brightness              in [0.0, 0.9]
alpha                 = round(dimStrength * 255)      in [0, 230]
```

The slider mapping already in `indicator.js` (`minimum-brightness … 1.0` ⇄ `0.0 … 1.0`) is
**unchanged** — it is already correct and is not part of any defect.

**Rendering details.**

- Actor: `St.Widget` (not `Clutter.Actor`) — `St.Widget` provides `track_hover` and
  `can_focus` (both confirmed present at runtime), so the original defensive properties
  become legal again and the actor participates in Shell theming conventions.
  `St.Widget` also supports `background-color` of type `CoglColor` (confirmed).
- `background_color` set to solid black with the computed alpha; **never** modulated via
  `actor.opacity`.
- `reactive: false` — the overlay must never intercept pointer input.
- `visible` toggled on `brightness < 1.0` so a fully-undimmed screen has no overlay at all
  (also avoids compositing cost).
- Geometry: per-monitor `x/y/width/height` from `Main.layoutManager.monitors`. These are
  the Shell's own logical-pixel monitor rects — the same values the Shell uses for its own
  chrome — so no manual `geometry_scale` handling is required.

**Smooth transitions.** Keep the existing `save_easing_state()` / `set_easing_duration()` /
`set_easing_mode()` / `restore_easing_state()` pattern, which is the Shell's own idiom
(`ui/padOsd.js:860-863`, `ui/iconGrid.js:230-236`) and is confirmed to still exist in
GNOME 51.1. Apply it around the **alpha** write, not around `opacity`.

**Minimum brightness.** Enforced in exactly one place (`clampBrightness`), which already
exists and is unit-tested. The repair must **not** introduce a second clamp path.

**Multi-monitor.** Per-monitor actors in a `Map` keyed by monitor index, as today. On
`monitors-changed`, preserve each monitor's brightness across the rebuild (the existing
`_onMonitorsChanged` logic already does this and is correct — it should be kept).

**Actor cleanup.** On teardown, each actor must be removed from its parent **and** destroyed,
and `Main.layoutManager` must be told, because `addChrome()` *tracks* actors
(`_trackActor` → `_trackedActors` list) and the tracked list holds a strong reference. A
tracked-but-destroyed actor would keep the layout manager's bookkeeping alive and could
interfere with `_updateRegions()`. The extension currently removes the child from its
parent and destroys it, but does **not** call `removeChrome()`, so `_trackedActors` retains a
stale entry. **Fix: call `Main.layoutManager.removeChrome(actor)` before destroying it.**

**Fullscreen.** `trackFullscreen: false` is correct and should be kept: a dimming overlay
must stay visible over fullscreen windows — that is the entire point of the feature. (The
Shell hides the *panel* in fullscreen via `trackFullscreen: true` on `panelBox`; our overlay
must not follow that behaviour.)

### 3.2 D-Bus

**Contract preservation.** The shipped introspection XML
(`dbus/io.github.softdimmer.BrightnessControl.xml`) is the extension's public contract and
is **unchanged** by this plan: same service name, object path, interface name, method
names, argument names/types, signal names and argument types. Method *implementations* are
corrected to match the contract they already declare. No versioning, no new methods, no
removed methods.

**Target implementation shape.** Two viable shapes were verified at runtime:

*Shape A — synchronous methods with positional arguments and plain return values:*

```js
GetBrightness(monitorIndex) {
    // validate, compute
    return [brightnessValue];        // plain number, not a GLib.Variant
}
```

*Shape B — asynchronous methods (`*Async`) with an invocation, for the `a{sv}` return:*

```js
GetCapabilitiesAsync(params, invocation) {
    const caps = { 'version': GLib.Variant.new_string('1.0'), ... };
    invocation.return_value(new GLib.Variant('(a{sv})', [caps]));
}
```

**Recommendation:** use **Shape A for every method except `GetCapabilities`**, and Shape B
only where a tuple-wrapped variant is genuinely required. Rationale: Shape A keeps the
method bodies synchronous and simple, and `a{sv}` is the single return type that cannot be
expressed as a plain value.

**Per-method specification.**

| Method | Signature | Behaviour | Return | Errors |
|---|---|---|---|---|
| `GetBrightness` | `(i monitor_index) → d brightness` | Returns the effective brightness for `monitor_index`; `-1` returns the global value. Effective value must account for `minimum-brightness` clamping so the reported number is the value actually in force. | `[plainNumber]` | `InvalidArgs` if `monitor_index < -1` |
| `SetBrightness` | `(i monitor_index, d brightness)` | Applies to `monitor_index`; `-1` applies to all. Must write through to the **settings** object, not only to the actor, so the Quick Settings slider and the D-Bus view stay consistent, and so the value survives a reload. Must respect `minimum-brightness`. | `[]` | `InvalidArgs` if `monitor_index < -1`, if brightness is not finite, or if brightness is outside `[minimum-brightness, 1.0]` |
| `GetEnabled` | `() → b enabled` | Returns the **effective** enabled state: the `enabled` setting AND that at least one overlay is active. Must **not** return a `GLib.Variant`. | `[plainBoolean]` | — |
| `SetEnabled` | `(b enabled)` | Writes the `enabled` setting; the settings-change handler performs the rest. | `[]` | — |
| `GetCapabilities` | `() → a{sv} capabilities` | Keys exactly as the XML documents: `version` (s), `monitor-count` (i), `has-dbus` (b), `supports-per-monitor` (b). | `invocation.return_value(new GLib.Variant('(a{sv})', [caps]))` | — |

**Input validation rules (all read from the schema, so they cannot drift):**

- `monitor_index` must be an integer `>= -1`.
- `brightness` must be a finite number within `[minimum-brightness, 1.0]`.
- Errors raised as `GLib.Error` with `Gio.DBusError.INVALID_ARGS`, which GJS maps to
  `org.freedesktop.DBus.Error.InvalidArgs` — matching the XML's own documentation
  (*"Raises org.freedesktop.DBus.Error.InvalidArgs on out-of-range values"*).

**Signal emission.**

- `BrightnessChanged(i monitor_index, d brightness)` — emit when the effective brightness
  changes; `-1` for "all monitors".
- `EnabledChanged(b enabled)` — emit when the effective enabled state changes.
- Emit **only on actual change** (compare before emitting) so clients are not spammed.
- The current code emits `BrightnessChanged` on every `changed::brightness` settings
  notification, including no-op writes; add a change check.
- `emit_signal(name, variant)` on the exported object is the correct GJS mechanism and is
  already used.

**Signal subscription cleanup.** The exported object's `PropertiesChanged` / `NameOwnerChanged`
subscriptions and the settings handlers must be disconnected in `unexport()`. The existing
`_disconnectSettings()` helper exists and is called from `unexport()` — keep it, and ensure
the `BrightnessChanged` / `EnabledChanged` handlers are also removed. Because
`Gio.DBusExportedObject.unexport()` detaches the object, the risk is not the exported object
but the **settings handlers** keeping the service alive; those must be disconnected.

**Compatibility with GJS 1.90.** The positional-argument convention and the
`invocation.return_value()` convention are the two facts that must hold. Both were verified
empirically against GJS 1.90.1 during the audit. A regression test must pin both.

**Backend stability.** `Gio.DBusExportedObject.wrapJSObject` with the existing introspection
XML keeps the wire contract identical. The **name ownership** should use
`Gio.BusNameOwnerFlags.REPLACE` so a stale owner cannot block re-enable — but only after
confirming no other component owns `io.github.softdimmer` (verified: nothing else does).

### 3.3 Capture handling — feasibility of capture detection

*(This subsection is the deliverable's "Capture-handling feasibility" section.)*

**Investigation results (GNOME 51.1, this machine).**

| Candidate | Availability | Can it *detect* a capture? |
|---|---|---|
| `Meta.Display::grab-op-begin/end` | Present | **No** — window manipulation only (§2.4) |
| `org.gnome.Mutter.ScreenCast` | Present on session bus | **No** — the interface exposes only `CreateSession`; there is no session-list, no "capture in progress" signal, and no `PropertiesChanged` on the root object that would report one |
| `org.gnome.Shell.Screenshot` | Present & owned by the Shell | **No** — declares methods only, **zero signals**; a call is a request, and the caller learns nothing about other clients' captures |
| `org.gnome.Shell.Screencast` | Present & owned by the Shell | **No** — declares `Screencast`, `ScreencastArea`, `StopScreencast` and an `Error` signal only; no start/stop notification |
| `Main.screenshotUI` `screencast-in-progress` | Shell-internal, **readable property** | **Yes, but scoped** — set by `_setScreencastInProgress()` (`ui/screenshot.js:2568-2575`) around the Shell's own screencast, and notified via `notify('screencast-in-progress')` |
| `Main.screenshotUI` `screenshot-taken` signal | Shell-internal signal | Fires **after** a capture completes (`ui/screenshot.js:2389-2390`) — too late to affect that capture |

**Conclusion: no universal mechanism exists.** The Shell exposes no bus-level signal that
fires when *any* client captures the screen, and Mutter's `ScreenCast` API is a *producer*
API with no observer side. A portal-driven capture (`org.freedesktop.portal.ScreenCast`,
present on this machine) bypasses the Shell entirely and is invisible to it.

**Proposed design — scoped detection plus an honest limitation.**

1. **Remove** the `grab-op-begin/end` wiring entirely. It is not merely ineffective for
   captures — it actively causes the overlay to vanish during window dragging, which is a
   user-visible regression.
2. **Detect the Shell's own screencast** via the in-process property, which is the only
   reliable, race-free signal:

   ```js
   // in the dimmer / extension
   this._screencastId = Main.screenshotUI.connect(
       'notify::screencast-in-progress',
       () => this._setCapturing(Main.screenshotUI.screencast_in_progress));
   ```

   This covers the Shell's own screen-recording UI — the dominant local capture path — and
   is read in-process, so it needs no D-Bus and cannot miss a race.
3. **Do not attempt to detect** third-party recorders, portal sharing, or the
   `org.gnome.Shell.Screenshot` D-Bus path. Doing so reliably is not possible with the
   interfaces GNOME 51.1 exposes.
4. **Document the limitation explicitly** in the README and in `metadata.json`'s
   description: *"Dimming overlays are excluded from screen recordings started from the
   GNOME Shell screenshot interface. Recordings or captures made by other applications are
   not detected, because GNOME 51 exposes no interface that reports them."*
5. **Offer a user-facing mitigation rather than a silent failure:** a new GSettings key
   (e.g. `hide-overlay-during-capture`, default `true`) that, when on, hides the overlay for
   a short grace period after any Shell-observed capture, and — more usefully — a key that
   lets the user disable the overlay entirely for capture-sensitive work. Even simpler and
   more honest: expose `hide-overlay-while-capturing` (default `true`) so users can turn the
   behaviour off when it interferes with their capture tooling.

*Rationale for accepting a limitation rather than a workaround:* the previous repair already
shipped a fragile, wrong mechanism that produced a **worse** user-visible bug (overlay
flickering during window drags). A documented, scoped limitation is strictly better than a
mechanism that appears to work but silently fails for most capture paths.

**If the maintainers later want broader coverage,** the only viable route is a
GNOME/Mutter upstream API that reports capture state to observers — that is an upstream
request, not something an extension can implement on GNOME 51.1.

### 3.4 Layer stacking — layout strategy

*(This subsection is the deliverable's "Layer stacking design" section.)*

**Goal.** Dim the desktop and application windows; leave the GNOME top panel readable;
leave modal dialogs, the OSD and the Quick Settings menu unaffected; never intercept input.

**The problem with `addChrome()`.** It inserts directly below `top_window_group`, which is
*above* `panelBox` (added earlier through `addChrome()` itself). The overlay therefore dims
the panel.

**Options considered.**

| Option | Mechanism | Panel readable | Input | Modal/OSD above | Verdict |
|---|---|---|---|---|---|
| **A.** `addChrome()` as today | `uiGroup.add_child()` + below `top_window_group` | ❌ panel dimmed | ✅ | ✅ | Rejected — panel dimmed |
| **B.** `addChrome()` then reposition | `addChrome(actor)` then `Main.uiGroup.set_child_below_sibling(actor, Main.layoutManager.panelBox)` | ✅ | ✅ | ✅ | **Recommended** |
| **C.** Direct `uiGroup` insert | `Main.uiGroup.insert_child_below(actor, Main.layoutManager.panelBox)` | ✅ | ✅ | ✅ | Equivalent to B, but skips `addChrome()` bookkeeping |
| **D.** Exclude the panel rect from the overlay | Overlay drawn over the full screen but with a transparent notch where the panel is | ✅ | ✅ | ✅ | Rejected — complex, and the panel is not always full-width |

**Recommendation — Option B**, with a documented fallback to C.

- `Main.uiGroup` is a documented export of the Shell (`ui/main.js:84`), and the Shell uses
  exactly this sibling-reordering technique itself (`ui/layout.js:1408`,
  `ui/layout.js:322`). `Clutter.Actor.set_child_below_sibling` is confirmed present at
  runtime.
- Using `addChrome()` **and then** repositioning keeps the Shell's own bookkeeping
  (`_trackActor`, input-region and fullscreen tracking) while achieving the desired
  ordering. Using C alone would bypass `_trackActor`, which `removeChrome()` later expects
  to find — creating a cleanup asymmetry.
- Because the panel is not always at the top of every monitor, the overlay should be
  **inset by the panel height on the monitor(s) where the panel sits**, rather than merely
  reordered. Reordering alone would still let the overlay cover the panel's *pixels* while
  the panel's own actor draws on top — which is sufficient for readability, since the panel
  is drawn after (above) the overlay. Reordering is therefore the correct minimal change;
  insetting is only needed if the design goal is "the panel area should not be dimmed at
  all", which is a separate (and undesirable) requirement, since the panel is translucent
  over the desktop anyway.

**Ordering after the fix** (bottom → top):

```
window_group            application windows
  overlay               ← our dim layer (below the panel)
  panelBox              GNOME top panel        (readable, undimmed)
  top_window_group      popup windows
  modalDialogGroup      modal dialogs          (undimmed)
  inputPanelGroup       OSD / input panel      (undimmed)
  feedbackGroup         Shell feedback         (undimmed)
```

**Input.** `reactive: false` on the overlay. Confirmed: `Clutter.Actor.reactive` exists
(type `gboolean`) and a non-reactive actor is excluded from Clutter picking. The Shell's own
`ui/shot.js:1585` uses `Shell.util_set_hidden_from_pick(actor, true)` for the same purpose;
if belt-and-braces is wanted, that call can be applied to the overlay as well.

**Fullscreen.** Keep `trackFullscreen: false`. The overlay must remain over fullscreen
windows.

**Modal dialogs / QS.** These are `uiGroup` children added **after** `panelBox` through plain
`add_child()`, so they are above the overlay automatically. The Quick Settings menu is
parented to `Main.uiGroup` via `Main.uiGroup.add_child(this.menu.actor)`
(`ui/panelMenu.js:142`), so it is also above. No extra work required — but this must be
**verified live**, because it is a source-level inference.

### 3.5 ASUSCraft compatibility

**Position (per §5 of the task):** treat ASUSCraft v1.2.2 as independent; do not integrate
with the bundled ASC extension; do not invent a D-Bus server; keep this extension's public
D-Bus contract unchanged.

**Plan implications — all "no change required":**

- The extension's bus name `io.github.softdimmer`, object path
  `/io/github/softdimmer/BrightnessControl` and interface
  `io.github.softdimmer.BrightnessControl` are **unchanged**. The D-Bus repair (§3.2) fixes
  the *implementation* to match the contract it already declares; it does not alter the
  contract.
- The ASC extension is a **client** of `io.github.asuscraft.SoftDimmer` and owns no bus
  name, so there is **no D-Bus name collision** with this extension.
- The two extensions use **different GSettings schemas**
  (`org.gnome.shell.extensions.soft-dimmer` vs `…soft-dimmer-asc`) and different overlay
  parents (`uiGroup` vs `global.stage`), so no settings or overlay conflict arises.
- No ASUSCraft file, and no ASC extension file, is read, written or referenced by this plan.

**One documented caveat to record, not to fix.** If both extensions were functional and
enabled simultaneously, they would dim the **same monitor additively** — two independent
translucent overlays produce a compounded transmission
(`1 − (1−a)(1−b)`), so a 50 % dim in each would read as ≈75 % dim. This is a
**deployment** consideration (which dimming engine owns the session), not an interface
conflict, and it is already noted in
`ASUSCRAFT_SOFT_DIMMER_DBUS_INVESTIGATION.md` §6.3. No code change is warranted; a
README note is.

---

## 4. D-Bus Contract (as implemented after repair)

Identical to the shipped XML; the table records *implementation* requirements.

### 4.1 Contract summary

| Aspect | Value | Changed by this plan? |
|---|---|---|
| Bus | Session | No |
| Service name | `io.github.softdimmer` | No |
| Object path | `/io/github/softdimmer/BrightnessControl` | No |
| Interface | `io.github.softdimmer.BrightnessControl` | No |
| Methods | `GetBrightness`, `SetBrightness`, `GetEnabled`, `SetEnabled`, `GetCapabilities` | No |
| Signals | `BrightnessChanged(i,d)`, `EnabledChanged(b)` | No |

### 4.2 Implementation requirements

| Method | JS shape | Arguments | Returns | Validation |
|---|---|---|---|---|
| `GetBrightness` | sync, positional | `monitorIndex` (number) | `[brightness]` — plain number in `[minimum-brightness, 1.0]` | `monitorIndex` integer `>= -1` → else `InvalidArgs` |
| `SetBrightness` | sync, positional | `monitorIndex` (number), `brightness` (number) | `[]` | integer `>= -1`; finite; within `[minimum-brightness, 1.0]` → else `InvalidArgs` |
| `GetEnabled` | sync, positional | none | `[enabled]` — plain boolean | — |
| `SetEnabled` | sync, positional | `enabled` (boolean) | `[]` | — |
| `GetCapabilities` | **async** (`GetCapabilitiesAsync`) | `params`, `invocation` | `invocation.return_value(new GLib.Variant('(a{sv})', [caps]))` | — |

### 4.3 Capability dictionary

| Key | Type | Value |
|---|---|---|
| `version` | `s` | e.g. `"1.0"` — the extension's interface version |
| `monitor-count` | `i` | number of monitors the dimmer currently manages |
| `has-dbus` | `b` | `true` |
| `supports-per-monitor` | `b` | `true` |

### 4.4 Signal semantics

| Signal | Emitted when | Payload |
|---|---|---|
| `BrightnessChanged` | effective brightness for a monitor (or all, `-1`) actually changes | `(i monitor_index, d brightness)` |
| `EnabledChanged` | effective enabled state actually changes | `(b enabled)` |

### 4.5 Cleanup obligations

- Disconnect the `changed::brightness` / `changed::enabled` settings handlers in
  `unexport()`.
- Call `Gio.DBusExportedObject.unexport()` before `Gio.bus_unown_name()`.
- Reset `_signalIds` to empty after disconnecting, so a double-`unexport()` is a no-op.

---

## 5. Scope Boundaries (explicit)

**In scope for this plan:**

- `soft-dimmer@io.github.softdimmer` only.

**Explicitly out of scope:**

- The bundled `soft-dimmer-asc@io.github.asuscraft` extension — **not modified, not
  integrated with, and not referenced.** Its own defects (missing `schemas/`, the
  `track_hover`/`can_focus` constructor failure, its `enable()`/`disable()` teardown race)
  belong to its own project and are documented in
  `ASUSCRAFT_SOFT_DIMMER_DBUS_INVESTIGATION.md` §5.3 and §6.8.
- ASUSCraft v1.2.2 itself — untouched.
- `soft-brightness-plus@joelkitching.com` — a third-party extension that is separately
  enabled and draws its own overlay. Its presence is recorded as a fact; changing it is not
  part of this plan.
- Inventing a new D-Bus service, interface, or versioned contract for ASUSCraft
  compatibility. **The existing public D-Bus contract of `soft-dimmer@io.github.softdimmer`
  is preserved exactly.**

---

## 6. Test Matrix

### 6.1 Automated tests (runnable outside a live Shell)

| ID | Test | Method | Gate |
|---|---|---|---|
| **A1** | ESM syntax of every `.js` | `node --check` on `.mjs` copies | All files parse |
| **A2** | Metadata validity | JSON assertions (uuid, `shell-version` contains `51`, schema id matches) | Pass |
| **A3** | Schema compiles | `glib-compile-schemas --strict` | Exit 0, no warnings |
| **A4** | Schema keys unchanged | Assert all 8 keys + types + ranges present | Pass |
| **A5** | `clampBrightness` bounds | Unit: below-min → min, above-1 → 1, NaN → 1.0, non-numeric → 1.0 | Pass |
| **A6** | **Brightness→alpha conversion** | Unit: for brightness ∈ {1.0, 0.9, 0.5, 0.1, 0.0} assert `alpha === round((1-b)*255)` and `alpha ∈ [0,255]` | **Pass — this is the regression test for Defect 1** |
| **A7** | **No 0–1 value ever reaches a 0–255 channel** | Static assertion over the source that every `opacity` assignment is absent, and that the alpha computation uses `* 255` | **Pass — Defect 1** |
| **A8** | Monitor selection logic | Unit: `all` / `builtin` / `external` / unknown / empty-builtin | Pass |
| **A9** | `Cogl.Color` construction | Unit: fields read back; alpha defaults to 0 | Pass |
| **A10** | GNOME 51 API surface | Unit: `Cogl.Color` exists; `Clutter.Color` absent; `Clutter.Actor` has no `track_hover`/`can_focus`; `reactive`/`background-color`/`opacity` exist; `opacity` pspec type is `guint` | Pass |
| **A11** | **D-Bus method dispatch** | **Integration:** import the real `dbus.js` standalone (it imports only `Gio`/`GLib` — no Clutter), export it on the session bus with a unique test name, and call every method via `gdbus call` | **Pass — Defects 2 & 3** |
| **A12** | `GetBrightness` returns a real number | A11: assert `0.0 ≤ r ≤ 1.0` and `r != nan` | Pass |
| **A13** | `GetEnabled` reflects the setting both ways | A11: set enabled true → `(true,)`; set false → `(false,)` | **Pass — Defect 3(a)** |
| **A14** | `GetCapabilities` returns a populated dict | A11: assert all four documented keys present with correct types | **Pass — Defect 3(b)** |
| **A15** | `SetBrightness` validation | A11: `-2` → `InvalidArgs`; `2.0` → `InvalidArgs`; `0.001` (< min) → `InvalidArgs`; `0.5` → accepted | Pass |
| **A16** | `SetBrightness` round-trip | A11: `SetBrightness(-1, 0.4)` then `GetBrightness(-1)` → `0.4` | Pass |
| **A17** | Signals fire on change only | A11: subscribe, `SetBrightness`, assert exactly one `BrightnessChanged`; repeat the same value, assert no further signal | Pass |
| **A18** | D-Bus teardown | A11: `unexport()` twice → no error; settings handlers disconnected (assert via a spy) | Pass |
| **A19** | No grab-op wiring remains | Static assertion: the strings `grab-op-begin`/`grab-op-end` do not appear in any source file | **Pass — Defect 4** |
| **A20** | Overlay ordering call present | Static assertion: the source orders the overlay below `Main.layoutManager.panelBox` | Pass — Defect 5 |

> **A11–A18 are executable today.** The audit proved that `dbus.js` imports only `Gio` and
> `GLib` and can therefore be imported, instantiated and driven over the live session bus
> from a standalone `gjs` process. This is how Defects 2 and 3 were found, and it converts
> the entire D-Bus surface from PENDING to automated coverage.

### 6.2 Live GNOME Shell tests (require a real session)

| ID | Test | Method | Pass criterion |
|---|---|---|---|
| **L1** | Extension enable | Log out, log in, `gnome-extensions info` | `State: ACTIVE`, no errors in `journalctl --user -b` |
| **L2** | Overlay creation | Inspect `uiGroup` children at runtime | One overlay actor per selected monitor |
| **L3** | **Visible dimming** | Set brightness 0.5, 0.25, 0.1; observe | Screen visibly dims, monotonically; undimmed at 1.0 |
| **L4** | Dim strength accuracy | At brightness 0.5 measure the darkened region's luminance | ≈50 % transmission (within tolerance) |
| **L5** | Minimum brightness | Set brightness to `minimum-brightness` | Dimming stops at the configured floor and cannot go darker |
| **L6** | Transition smoothness | Move the slider | No flicker, no banding, ~250 ms ease |
| **L7** | **Top panel readability** | Observe the top bar at brightness 0.3 | Panel text/icons fully legible and **undimmed** |
| **L8** | Modal dialog | Open a modal dialog over a dimmed desktop | Dialog unaffected by dimming |
| **L9** | Quick Settings | Open the QS menu | Menu unaffected and fully interactive |
| **L10** | OSD | Trigger a volume/brightness OSD | OSD unaffected |
| **L11** | Input | Click, drag and scroll across the dimmed area | All input reaches the window beneath; overlay never steals events |
| **L12** | Fullscreen | Fullscreen a video/game | Overlay still dims the fullscreen content |
| **L13** | **Window drag** | Drag a window | **No brightness flash** — the Defect-4 regression must be gone |
| **L14** | Screenshot UI recording | Start a recording from the Shell screenshot UI | Overlay hidden during recording, restored after |
| **L15** | Shell screenshot (still image) | Take a screenshot | Overlay **may** appear in the still (documented limitation) — verify it matches the documented behaviour |
| **L16** | Third-party recorder | Record with a non-Shell tool | Overlay present in the recording — **expected**, confirms the documented limitation is accurate |
| **L17** | Multi-monitor | Dim one monitor only | Only the targeted monitor dims; others untouched |
| **L18** | `builtin` / `external` | Change `monitor-selection` | Correct monitor set dims |
| **L19** | Disable cleanup | Disable the extension | Overlay removed; no residual dimming; no leaked handlers |
| **L20** | Re-enable lifecycle | Enable → disable → enable, ×5 | No duplicate overlays; no duplicated signal handlers; no errors |
| **L21** | Settings persistence | Log out / log in | Brightness, enabled state and selection persist |
| **L22** | `restore-brightness` | Set false, log out/in | Starts undimmed; moving the slider then dims |
| **L23** | D-Bus under live shell | `gdbus call` all five methods | Same results as A11–A16 but inside the Shell process |
| **L24** | Concurrent D-Bus + UI | Change brightness by D-Bus while the slider is open | Slider updates; no feedback loop or fight |

### 6.3 Regression tests

| ID | Scenario | Assertion |
|---|---|---|
| **R1** | Slider ↔ settings ↔ D-Bus | All three agree on the value at all times (no drift) |
| **R2** | Repeated identical writes | No signal storms; no unnecessary repaints |
| **R3** | `SetBrightness` while disabled | Value is stored; enabling later applies it |
| **R4** | Monitor hot-plug while dimming | Overlays rebuilt; brightness preserved; no leak |
| **R5** | Extension disabled while a D-Bus client is connected | Client sees the name disappear cleanly; no crash |
| **R6** | Two extensions dimming the same monitor | Additive behaviour is as documented; no crash, no interference |

---

## 7. Phased Implementation Plan

Each phase is independently reviewable, independently revertible, and has its own
acceptance criteria. **Phases must not be bundled.**

### Phase 1 — Restore correct dim rendering (Defect 1)

**Files:** `dimmer.js` only.

**Changes:**
1. Replace the `opacity`-based dim application with an alpha-based one:
   `alpha = Math.round((1.0 - brightness) * 255)`, applied to
   `background_color` as `new Cogl.Color({red: 0, green: 0, blue: 0, alpha})`.
2. Remove the `actor.opacity` write entirely. Do **not** use `opacity` for dimming.
3. Move the overlay actor from `Clutter.Actor` to `St.Widget` so `track_hover`/`can_focus`
   become legal again (they are absent from `Clutter.Actor` on GNOME 51 — the original
   root-cause error). Keep `reactive: false`.
4. Keep the existing easing-state transition pattern, applied around the alpha write.
5. Keep `clampBrightness` as the single clamp point; do not add another.
6. Preserve `visible = brightness < 1.0` so an undimmed screen has no overlay.

**Dependencies:** none.

**Risks:** low. The algebra is a faithful restoration of the pre-repair semantics.

**Acceptance:** A6, A7, A10 pass statically; L2, L3, L4, L5, L6 pass live.

**Rollback:** revert the single file; the pre-repair baseline is preserved at
`/tmp/opencode/soft-dimmer-primary-backup/`.

### Phase 2 — Correct the D-Bus implementation (Defects 2 & 3)

**Files:** `dbus.js` only.

**Changes:**
1. Convert all synchronous methods to the **positional-argument** form:
   `GetBrightness(monitorIndex)`, `SetBrightness(monitorIndex, brightness)`,
   `GetEnabled()`, `SetEnabled(enabled)`.
2. Return **plain values**, never `GLib.Variant`: `return [brightness]`,
   `return [enabled]`.
3. Convert `GetCapabilities` to the async form `GetCapabilitiesAsync(params, invocation)`
   and return `invocation.return_value(new GLib.Variant('(a{sv})', [caps]))`.
4. Keep the existing validation logic; make it use `Number.isInteger` for `monitor_index`
   (the pre-repair code used `monitorIndex < -1`, which lets `1.5` through).
5. Make `GetEnabled` report the **effective** state, not merely the raw setting.
6. Make `SetBrightness` write through to the settings object so the UI and D-Bus stay
   consistent.
7. Emit `BrightnessChanged` / `EnabledChanged` **only on an actual change**.
8. Disconnect settings handlers in `unexport()` and reset `_signalIds` so a double
   `unexport()` is a no-op.
9. **Do not change** the introspection XML, the service name, the object path or the
   interface name.

**Dependencies:** Phase 1 (so `GetBrightness` can report the *effective* value).

**Risks:** low-to-medium. Changing `GetEnabled` semantics from "raw setting" to "effective
state" is a behaviour change; it must be documented in the XML comment.

**Acceptance:** A11–A18 pass (fully automatable); L23, L24 pass live.

**Rollback:** revert the single file; the interface XML is untouched, so the wire contract
survives either way.

### Phase 3 — Remove the incorrect capture detection (Defect 4)

**Files:** `dimmer.js`, `extension.js` (or a small new `capture.js`).

**Changes:**
1. Delete the `grab-op-begin` / `grab-op-end` connections **and** the `_setGrabbing` /
   `hideOverlays` / `showOverlays` machinery that exists only to serve them.
2. Add detection of the Shell's own screencast via
   `Main.screenshotUI.connect('notify::screencast-in-progress', …)`, hiding overlays while
   it is `true`. Disconnect it on teardown.
3. Add a GSettings key (e.g. `hide-overlay-during-capture`, default `true`) so the behaviour
   can be turned off.
4. Document the limitation in the README and in `metadata.json`'s description: captures made
   by applications other than the GNOME Shell screenshot UI are not detected, because GNOME
   51 exposes no interface that reports them.

**Dependencies:** Phase 1 (overlay visibility API).

**Risks:** **medium.** `Main.screenshotUI` is a Shell-internal object; its availability and
the exact property name are verified from the GNOME 51.1 source but are not a public API
contract. If the property is absent the extension must degrade gracefully (no crash, no
capture handling) rather than fail to enable.

**Acceptance:** A19 passes statically; L13, L14, L15, L16 pass live and the observed
behaviour matches the documented limitation.

**Rollback:** revert the two files; the extension then simply has no capture handling.

### Phase 4 — Fix overlay stacking (Defect 5)

**Files:** `dimmer.js`.

**Changes:**
1. Keep `Main.layoutManager.addChrome(actor, {trackFullscreen: false, affectsStruts: false})`.
2. Immediately afterwards, reposition the overlay below the panel:
   `Main.layoutManager.uiGroup.set_child_below_sibling(actor, Main.layoutManager.panelBox)`.
3. Add a defensive guard: if `Main.layoutManager.panelBox` is unavailable, skip the
   repositioning rather than throwing.
4. Call `Main.layoutManager.removeChrome(actor)` before destroying an overlay during
   teardown, so the Shell's `_trackedActors` bookkeeping does not retain a stale entry.

**Dependencies:** Phase 1.

**Risks:** **medium.** `Main.layoutManager.uiGroup` and `.panelBox` are Shell internals.
Both are confirmed present in GNOME 51.1 (`ui/main.js:84`, `ui/layout.js:276`), and the
Shell uses `set_child_below_sibling` on `uiGroup` itself (`ui/layout.js:1408`). A guard is
mandatory.

**Acceptance:** A20 passes statically; L7, L8, L9, L10, L11, L12 pass live.

**Rollback:** revert the single file; the overlay reverts to `addChrome()`-only placement.

### Phase 5 — Lifecycle and cleanup hardening (Defect 7)

**Files:** `extension.js`, `dimmer.js`, `indicator.js`.

**Changes:**
1. Reset `this._restored = false` in `enable()` **and** clear it in `disable()`, so
   `restore-brightness` is genuinely load-time-only across enable/disable cycles.
2. Make `disable()` idempotent: null out `_settingsIds`, `_shortcutIds`, `_dbus`, `_indicator`
   and `_dimmer` **before** performing teardown work, or guard each step with a
   `this._disabled` flag, so a double-`disable()` is a no-op.
3. Ensure the indicator's settings handlers are disconnected even when the indicator is
   destroyed by the Shell rather than by `disable()`.
4. Ensure the `Main.screenshotUI` notification handler (Phase 3) is disconnected on teardown.
5. Ensure `dimmer.disable()` disconnects every handler it connected, including the
   `monitors-changed` and `startup-complete` connections.

**Dependencies:** Phases 1, 3.

**Risks:** low.

**Acceptance:** L19, L20, L22 pass live; A18 passes.

**Rollback:** revert the three files.

### Phase 6 — Test-suite expansion

**Files:** `test-logic.js` (extend), **new** `test-dbus.js`.

**Changes:**
1. Extend `test-logic.js` with A6, A7, A10 (alpha conversion, no-0–1-into-0–255 static
   assertion, API surface).
2. Add `test-dbus.js` — a standalone `gjs` script implementing A11–A18: import the real
   `dbus.js`, instantiate it with stub settings/dimmer, export it under a **test-only** bus
   name, drive it with `gdbus call`, and assert the results. It must use a distinct service
   name and object path so it cannot collide with the real extension.
3. Document how to run both suites (exact commands, required `GI_TYPELIB_PATH` /
   `GSETTINGS_SCHEMA_DIR`).

**Dependencies:** Phases 1–2 (the tests encode the corrected behaviour).

**Risks:** low. The D-Bus test must clean up its bus name on exit.

**Acceptance:** both suites run green from a clean shell.

**Rollback:** the new test file is additive and removable.

### Phase 7 — Documentation and release readiness

**Files:** `README.md`, `metadata.json` description, `CHANGELOG.md`.

**Changes:**
1. Document the brightness semantics explicitly: *"`brightness` is remaining luminance;
   1.0 = undimmed; `minimum-brightness` is the floor."*
2. Document the **capture limitation** from Phase 3 in plain language.
3. Document the stacking behaviour (panel deliberately left undimmed).
4. Document that the two Soft Dimmer extensions must not both be enabled for the same
   session (additive dimming), cross-referencing the ASUSCraft investigation.
5. Note the `restore-brightness` load-time semantics.
6. Update `CHANGELOG.md` with the defect fixes.

**Dependencies:** Phases 1–6.

**Risks:** none.

**Acceptance:** documentation matches the shipped behaviour; no stale claims remain.

**Rollback:** revert the documentation files.

---

## 8. Risks and Rollback

### 8.1 Risk register

| ID | Risk | Phase | Likelihood | Impact | Mitigation |
|---|---|---|---|---|---|
| R-1 | **Live-shell behaviour differs from source-level reasoning** — the previous repair's central failure mode | all | **High** | High | Phase 7 gates on the live matrix; no phase is called "done" on static evidence alone |
| R-2 | `Main.screenshotUI` / `Main.layoutManager.panelBox` are Shell internals that may change | 3, 4 | Medium | Medium | Defensive guards; graceful degradation (never block `enable()`); documented as the first thing to re-check on a Shell upgrade |
| R-3 | `St.Widget` vs `Clutter.Actor` swap changes behaviour (theming, `background-color` semantics) | 1 | Low | Medium | A9/A10 cover the API surface; L2/L3 verify live; `St.Widget` is what the Shell's own widgets use |
| R-4 | Fixing `GetEnabled` to "effective state" changes observable behaviour for existing clients | 2 | Medium | Low | Documented in the XML comment; no existing client exists in this project |
| R-5 | Signal-emission change (only-on-change) alters client-observed traffic | 2 | Low | Low | Strictly an improvement; A17 pins it |
| R-6 | A test that owns a bus name collides with the live extension | 6 | Low | Medium | Test-only service/object names, distinct from production |
| R-7 | Cleanup changes (Phase 5) alter the `restore-brightness` timing | 5 | Low | Low | L22 verifies it explicitly |
| R-8 | Capture detection is scoped, so some captures still include the overlay | 3 | **Certain** | Low | Documented as a known limitation, not a defect; upstream API request is the only real remedy |
| R-9 | Phase ordering mistakes — e.g. shipping D-Bus before rendering | all | Low | High | Explicit dependencies; each phase independently revertible |

### 8.2 Rollback strategy

**Per-phase.** Every phase touches one to three files and has a single logical change.
Reverting a phase is a file-level revert; no phase depends on another's *runtime* state, only
on its API shape.

**Per-release.** All phases land together only after the live matrix passes. If a phase
must be reverted after release, the extension must still enable cleanly — so:

- Every new Shell-internal access is guarded so that its absence degrades behaviour rather
  than aborting `enable()`.
- The D-Bus interface XML is **never** changed by a rollback, so external clients keep
  working.
- The public settings keys are **never removed**; the Phase 3 key is added with a default
  that preserves current behaviour.

**Full rollback.** The pre-repair baseline is preserved at
`/tmp/opencode/soft-dimmer-primary-backup/` (with baseline hashes in
`/tmp/opencode/primary-baseline.sha256`). Note that the pre-repair baseline is **itself
broken on GNOME 51** (`track_hover`/`can_focus` on `Clutter.Actor` throw), so a full
rollback returns to a non-loading extension. Rollback is therefore a *safety net*, not a
viable shipping state.

**Recovery if a live test corrupts state.** The extension writes only to
`org.gnome.shell.extensions.soft-dimmer`; resetting is a single `gsettings reset-recursively`
of that schema. No system state is modified by this extension.

---

## 9. Acceptance Criteria

### 9.1 Confidently fixable (static + automated evidence sufficient)

| Criterion | Verified by |
|---|---|
| Dim level is expressed on a 0–255 channel with an explicit, tested conversion | A6, A7 |
| No 0.0–1.0 value is ever written to a 0–255 channel | A7, A19 |
| `Clutter.Actor` is not constructed with `track_hover`/`can_focus` | A10 |
| Every D-Bus method is callable and returns the documented type | A11–A16 |
| `GetEnabled` reflects the real state in both directions | A13 |
| `GetCapabilities` returns all four documented keys | A14 |
| Invalid arguments raise `InvalidArgs` | A15 |
| Signals fire on change only | A17 |
| D-Bus teardown is idempotent and leak-free | A18 |
| No capture-detection code remains wired to window-grab signals | A19 |
| The overlay is ordered below the GNOME top panel | A20 + L7 |
| `restore-brightness` is load-time-only across enable/disable | L22 |
| Schema and metadata remain valid and unchanged in contract | A2, A3, A4 |

### 9.2 Requires live-shell validation

| Criterion | Verified by |
|---|---|
| The overlay is actually visible and dims the screen | L2, L3 |
| Dim strength matches the requested value within tolerance | L4 |
| The minimum-brightness floor is honoured | L5 |
| Transitions are smooth and artefact-free | L6 |
| The top panel stays readable and undimmed | L7 |
| Modal dialogs, OSD and the Quick Settings menu are unaffected | L8, L9, L10 |
| Input passes through the overlay to the windows beneath | L11 |
| Fullscreen content is still dimmed | L12 |
| No brightness flash while dragging a window | L13 |
| Shell-UI recordings exclude the overlay | L14 |
| Per-monitor targeting works | L17, L18 |
| Disable removes the overlay and all handlers | L19 |
| Repeated enable/disable is stable | L20 |
| Settings persist across a session restart | L21 |
| D-Bus and the UI stay consistent under concurrent use | L24 |

### 9.3 GNOME 51 may not expose reliably — accept and document

| Item | Reality | Disposition |
|---|---|---|
| **Detection of captures by non-Shell applications** | No bus-level API reports them. `org.gnome.Mutter.ScreenCast` exposes only `CreateSession`; `org.gnome.Shell.Screenshot` / `Screencast` declare no capture-state signals; portal captures bypass the Shell entirely | **Documented limitation.** The extension detects the Shell's own screencast only. Screenshots and third-party recordings will include the overlay |
| **Detecting the moment a screenshot is taken, before it is captured** | `screenshot-taken` fires *after* capture; the D-Bus `Screenshot` method has no pre-notification | **Not implementable on GNOME 51.** Accept that still screenshots include the overlay |
| **Un-dimming a portal-driven screen share** | The portal talks to Mutter/PipeWire directly; the Shell is not in the path | **Not implementable.** Documented |
| **A `Main.screenshotUI` API guarantee** | It is a Shell-internal object, not a public API; its property name could change between releases | Use with a **guard**; treat absence as "no capture handling"; re-verify on every Shell upgrade |
| **Directly detecting a `Clutter` "capture in progress" state** | No such public signal exists on GNOME 51 | Not attempted |

---

## 10. Final Verdict

**What can be fixed confidently.** Defects 1, 2, 3, 5 and 7 are ordinary implementation
errors with unambiguous correct answers, all derivable from the extension's own schema, its
own shipped introspection XML, the GNOME 51.1 API surface and empirically measured GJS
behaviour. Defect 1 (opacity scale) is the highest-impact: the extension currently does not
dim at all. Defects 2 and 3 can be pinned by an **automatable** D-Bus integration test
(§6.1 A11–A18) because `dbus.js` imports only `Gio` and `GLib` and can therefore be driven
over the session bus from a standalone process — the technique that found these defects in
the first place. Defect 5 requires a one-line sibling reorder plus a guard. Defect 7 is
ordinary lifecycle hygiene.

**What requires live-shell testing.** Everything about *rendering*. Whether the overlay
paints, whether the panel stays readable, whether the dim strength is accurate, whether
transitions are smooth, whether input truly passes through, whether fullscreen content is
dimmed, and whether repeated enable/disable is stable — none of this can be observed
outside a live GNOME Shell, because `Clutter.Actor` construction aborts without a Clutter
context and GNOME 51 on Wayland refuses in-place Shell restart. **One logout/login cycle is
required, and the live matrix in §6.2 must gate the release.** The previous repair's central
process failure was declaring completion on static evidence; this plan explicitly forbids
that.

**What GNOME 51 may not expose reliably.** Universal capture detection. There is no
bus-level signal for "a capture is in progress", Mutter's `ScreenCast` API is
producer-only, and portal-driven captures bypass the Shell entirely. The plan therefore
proposes **scoped detection of the Shell's own screencast plus a documented limitation**,
and explicitly rejects the alternative of shipping another fragile mechanism that appears to
work — which is what produced the current window-drag flicker. This limitation is a
permanent property of the platform, not a defect in the extension, and it should be stated
plainly in the README rather than papered over.

**Recommended sequencing.** Phases 1 → 2 → 3 → 4 → 5 → 6 → 7, each merged separately.
Phase 1 alone restores the feature's core function and is the highest-value single change.
Phases 1 and 2 are verifiable without a live session; Phases 3–5 require the live matrix;
Phase 6 turns the D-Bus surface into continuous automated coverage so that this class of
defect cannot recur silently; Phase 7 closes the loop on documentation that currently
contains at least two claims contradicted by evidence.

---

*Plan prepared 2026-11-10. Read-only: no source file was edited, no extension setting was
changed, no extension was enabled or disabled, GNOME Shell was not restarted, and no
mutating D-Bus method was invoked. All runtime statements in this plan are either directly
measured or explicitly marked as requiring live-shell validation.*
