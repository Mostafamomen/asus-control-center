# Soft Dimmer — Phase 3: opacity & rendering repair

**Subject:** `soft-dimmer@io.github.softdimmer`
**Installed at:** `/home/mostafa/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer`
**Plan followed:** `SOFT_DIMMER_GNOME51_REPAIR_PLAN.md` (§2.1, §2.5, §3.1, §3.4, §6, §7)
**Baseline documents:** `SOFT_DIMMER_GNOME51_PHASE2_DBUS_REPORT.md`, `SOFT_DIMMER_GNOME51_PHASE2_LIVE_VERIFICATION.md`
**Date:** 2026-11-11
**Environment:** GNOME Shell 51.1 / mutter 51.1 / GJS 1.90.1 / Fedora 45 / kernel 7.3.3-300.fc45

**Final verdict: PASS WITH LIMITATIONS.**
Everything that can be decided without a running GNOME Shell is done and
proven. The *visible* result of the change — the overlay really dimming, the
top panel really staying undimmed — cannot be observed without reloading the
extension, which this phase forbids; those checks are listed as PENDING and
are **not** claimed as passed.

---

## 0. Verdict summary

| # | Requirement | Verdict |
|---|---|---|
| 1 | Plan and sources read before editing | **PASS** — §2 |
| 2 | Installed hashes and backup manifests verified | **PASS** — §3, §4 |
| 3 | Brightness semantics confirmed | **PASS** — §5 |
| 4 | New persistent pre-Phase-3 backup, verified manifest, restoration test | **PASS** — §4 |
| 5.1 | Opacity conversion correct (0–255 integer, no fractional write) | **PASS** — §6.1, §7 |
| 5.2 | Overlay rendering lifecycle inspected and repaired | **PASS** — §6.2 |
| 5.3 | Overlay stacking below the top panel verified against the GNOME 51 API | **PASS (static)** — §6.3 |
| 5.4 | Cleanup removes the actor *and* the tracking state | **PASS** — §6.4 |
| 5.5 | Existing animation/easing behaviour preserved | **PASS** — §6.5 |
| 5.6 | Capture-detection changes only if the plan assigns them to Phase 3 | **DEFERRED with reason** — §10 |
| 6 | Phase 2 D-Bus code and hash unchanged | **PASS** — §8 |
| 7 | Deterministic opacity tests at the five required brightness values | **PASS** — §7.2 |
| 8 | Lifecycle tests for actor removal and tracking cleanup | **PASS** — §7.3 |
| 9 | Static/API verification of GNOME 51 stacking | **PASS** — §6.3, §7.4 |
| 10 | Tests outside the Shell separated from live rendering tests | **PASS** — §7.1, §9 |
| 11 | No D-Bus code, GSettings, extension state or other project touched | **PASS** — §8 |
| 12 | Live rendering verification | **PENDING** — §9 (needs a logout/login) |

---

## 1. Phase 2 baseline confirmed before starting

The premise of this phase is that Phase 2 is verified. It was re-confirmed at
the start of this phase, on a **fresh** GNOME Shell process:

```
$ date '+%Y-%m-%d %H:%M:%S %z'
2026-11-11 10:35:47 +0300
$ ps -eo pid,lstart,etimes,comm | grep gnome-shell
  52999 Wed Nov 11 10:29:53 2026     352 gnome-shell
```

The new Shell (10:29:53) started **after** the Phase 2 fix was written
(09:42:04), so it loaded the repaired `dbus.js` — and the live interface
behaves exactly as the Phase 2 report predicted:

```
$ gdbus call --session --dest io.github.softdimmer \
    --object-path /io/github/softdimmer/BrightnessControl \
    --method io.github.softdimmer.BrightnessControl.GetBrightness 0
(1.0,)
$ gdbus call … GetEnabled
(true,)
$ gdbus call … GetCapabilities
({'version': <'1.0'>, 'monitor-count': <1>, 'has-dbus': <true>, 'supports-per-monitor': <true>},)
```

```
$ sha256sum …/dbus.js
f05c167a06b577d4007aef3ed46eb25d83b6e254c98fa85508655f2fbe384669
```

The Phase 2 live verification that was reported as PENDING in the previous
document is therefore now **PASS** — a new session did happen, between the
previous retry and this phase.

---

## 2. What was read before editing

| Source | Why |
|---|---|
| `SOFT_DIMMER_GNOME51_REPAIR_PLAN.md` (§1, §2.1, §2.5, §2.7, §3.1, §3.4, §6, §7, §8, §9) | the authoritative plan for this phase |
| `SOFT_DIMMER_GNOME51_FINAL_AUDIT.md` | the audit the plan is based on |
| `SOFT_DIMMER_GNOME51_PHASE2_DBUS_REPORT.md` | the Phase 2 contract and hash |
| `dimmer.js` (all 348 lines) | the file being changed |
| `extension.js`, `indicator.js` | lifecycle and callers |
| `dbus/io.github.softdimmer.BrightnessControl.xml` | the public contract |
| `test-logic.js` | the existing test suite |
| **GNOME Shell 51.1 sources**, extracted from `/usr/lib64/gnome-shell/libshell-51.so` with `gresource extract` (`/org/gnome/shell/ui/layout.js`, `/org/gnome/shell/ui/main.js`) | first-hand evidence for the stacking and cleanup API |

The extracted Shell sources are kept at
`~/.local/share/soft-dimmer/tests/shell-layout.js` and `shell-main.js`.

---

## 3. Pre-flight hash verification

```
$ cd ~/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer \
    && find . -type f -print0 | sort -z | xargs -0 sha256sum
1dc948b5422c625134048ac4de58ef23b3e9e3a4e2aab517a9ad5ea5f33bddd4  dbus/io.github.softdimmer.BrightnessControl.xml
f05c167a06b577d4007aef3ed46eb25d83b6e254c98fa85508655f2fbe384669  dbus.js
2681fe1be1506504205e5979091983b7fe2f6db19a978af5b0e462996b7fab35  dimmer.js
2ed1b54829d5f37116473d3bce058c20b22868efe9ae82b2a82b2e406e2538d9  extension.js
e12aff4888389adb9dc742c6b419c5321bd67cbbf0acc60f2b63a841c1be2122  indicator.js
d731e5fc6f701f72abb4a4e2a5f855c67244ff32e38b65f6ee223b3453eccd99  metadata.json
7319aa0d9f0a4368ebd2eb04133c3d10d3f4e73ddd2fe413ddad8756765a83c5  schemas/gschemas.compiled
758e601e237517466f913191fd6ed15cab635dff60c0100910a29ffb9a6662bc  schemas/org.gnome.shell.extensions.soft-dimmer.gschema.xml
e8b36f9bf466467dc94d6382ebe5824aafb24006e77980a2bc9965e0672501a8  test-logic.js
```

The two pre-existing backups were re-verified and left untouched:

```
  20261111T061208Z -> manifest OK (9 files)
  20261111T062247Z -> manifest OK (9 files)
```

---

## 4. The new pre-Phase-3 backup

| Item | Value |
|---|---|
| Backup root | `/home/mostafa/.local/share/soft-dimmer/backups/20261111T073708Z/` |
| Backup tree | `.../20261111T073708Z/extension/` (9 files, 2 sub-directories) |
| Manifest | `.../20261111T073708Z/MANIFEST.sha256` (SHA-256, 9 entries) |
| Metadata | `.../20261111T073708Z/MANIFEST.meta` |
| Captured state | `.../20261111T073708Z/state/STATE.txt` |

Outside `/tmp`, outside the extension directory, **same filesystem**
(`st_dev 55` for both), so `rename(2)` is atomic.

### 4.1 Verification

| # | Check | Result |
|---|---|---|
| 1 | `diff -r --no-dereference "$D" "$B/extension"` | **PASS** — no output |
| 2 | File count | **PASS** — 9 = 9 |
| 3 | `sha256sum -c MANIFEST.sha256` | **PASS** — 9 × `OK`, **exit 0** |
| 4 | Same filesystem (`stat -c %d`) | **PASS** — 55 = 55 |
| 5 | The manifest covers every file in the tree | **PASS** — no output from the diff |
| 6 | Restoration test into a **disposable** directory: re-hash, compare modes/structure, `diff -r` | **PASS** — hashes identical, modes/structure identical, trees identical |
| 7 | Every `.js` in the restored tree parses (`node --check` on `.mjs` copies) | **PASS** — 5/5 |
| 8 | The disposable directory was removed | **PASS** |
| 9 | The installed tree was unchanged by the restoration test | **PASS** |

The earlier backups were **not** deleted.

---

## 5. Brightness semantics — confirmed, not assumed

Traced from the schema and the code, exactly as the plan requires (§1, §2.1):

| Term | Meaning | Where it comes from |
|---|---|---|
| `brightness` | **remaining luminance**; `1.0` = no dimming, lower = stronger dimming | the schema description ("1.0 means no dimming; lower values apply a darker overlay") and `indicator.js` maps `minimum-brightness … 1.0` ⇄ `0.0 … 1.0` |
| `minimum-brightness` (default `0.10`) | the **floor on remaining luminance** → the strongest possible dim is 90 % | the schema range and `clampBrightness()` |
| dim strength | `1.0 - brightness`, in `[0.0, 0.9]` | the algebra |
| painted channel | the **alpha of the background colour**, a 0–255 channel | `dimAlpha()` and `_applyBrightness()` |

**Nothing about these semantics changed in Phase 3.** The live GSettings values
are still `brightness 1.0`, `enabled true`, `minimum-brightness 0.1`,
`monitor-selection 'all'`, `restore-brightness true` — verified before and
after this phase (§8).

---

## 6. What was changed, and why

### 6.1 Opacity conversion — the conversion itself was already correct (plan Phase 1)

The plan's Defect 1 was that the dim level was written into
`Clutter.Actor.opacity` as a 0.0–1.0 float, and `opacity` is a `guint`
0–255, so GJS truncated every usable level to `0` and the overlay was
invisible. **Phase 1 already moved the dim level into the colour alpha**,
which is natively 0–255.

Phase 3 therefore did **not** need to change the conversion — it needed to
*prove* it and to make it impossible to regress. What was added:

* an explicit, table-driven test of the five required brightness values with
  the expected number **and** the arithmetic that produces it (§7.2);
* a static guard that `dimmer.js` never writes `.opacity` at all, that the
  dim level goes into `background_color` as a `Cogl.Color`, and that the
  conversion still multiplies by 255 (§7.4);
* the API proof that `Clutter.Actor:opacity` is a `guint` with a default of
  255, so the alpha channel stays the only place the dim level lives.

The conversion rule, in one place, in `dimmer.js`:

```js
export function dimAlpha(brightness) {
    const value = Number(brightness);
    if (!Number.isFinite(value))
        return 0;
    const strength = Math.max(0.0, Math.min(1.0, 1.0 - value));
    return Math.round(strength * 255);
}
```

| brightness | dim strength | strength × 255 | **alpha** |
|---|---|---|---|
| `1.00` | `0.00` | `0.00` | **0** — no overlay at all (`visible = brightness < 1.0`) |
| `0.75` | `0.25` | `63.75` | **64** |
| `0.50` | `0.50` | `127.50` | **128** |
| `0.25` | `0.75` | `191.25` | **191** |
| `0.10` | `0.90` | `229.50` | **230** — the floor, a 90 % dim |

**Rounding rule:** `Math.round()` — half rounds up (`127.5 → 128`,
`229.5 → 230`), and the result is always an integer in `[0, 255]`.

### 6.2 Overlay rendering lifecycle — inspected, one defect found and fixed

`dimmer.js`'s rendering lifecycle was walked end to end:

| Stage | State | Action |
|---|---|---|
| `enable()` | — | connects `monitors-changed`, `startup-complete`, the two grab-op handlers; calls `_buildOverlays()` |
| `_buildOverlays()` | — | `_destroyAllOverlays()` then one actor per monitor |
| `_createOverlay()` | — | builds an `St.Widget`, `addChrome()`s it, registers it in `_overlays` and `_brightness` |
| `_applyBrightness()` | — | clamps, computes the alpha, writes `background_color` inside the easing state, toggles `visible` |
| `_onMonitorsChanged()` | — | saves the per-monitor brightness, rebuilds, re-applies the saved levels |
| `disable()` | — | disconnects every handler, then `_destroyAllOverlays()` |

Everything in that chain was already correct **except the teardown** (§6.4).
The lifecycle needed no other change, so `extension.js` was **not** modified.

### 6.3 Overlay stacking below the top panel — **CHANGED** (plan §2.5, §3.4)

**The defect, with first-hand source evidence.** From
`/org/gnome/shell/ui/layout.js` in the installed `libshell-51.so`:

```js
// ui/layout.js:878
addChrome(actor, params) {
    this.uiGroup.add_child(actor);
    if (this.uiGroup.contains(global.top_window_group))
        this.uiGroup.set_child_below_sibling(actor, global.top_window_group);
    this._trackActor(actor, params);
}
```

```js
// ui/layout.js:276
this.panelBox = new St.BoxLayout({ name: 'panelBox', … });
// ui/layout.js:280
this.addChrome(this.panelBox, { affectsStruts: true, trackFullscreen: true });
```

The top panel is itself added through `addChrome()`, i.e. **before any
extension runs**. `addChrome()` inserts an actor *directly below*
`global.top_window_group`, so an overlay added later by an extension lands
**above** `panelBox` — the top panel is dimmed along with the desktop. The
plan's Defect 5 is confirmed.

`Main.uiGroup` is the very same object as `LayoutManager.uiGroup`
(`ui/main.js:84` exports it, `ui/main.js:224` assigns
`uiGroup = layoutManager.uiGroup`), and the Shell itself uses
`set_child_below_sibling` on that group (`ui/layout.js:322`, `:881`).

**The fix** — plan §3.4 Option B, implemented in `dimmer.js`:

```js
this._layoutManager.addChrome(actor, {
    trackFullscreen: false,
    affectsStruts: false,
});

// ... but addChrome() puts the actor directly below
// global.top_window_group, which is *above* the top panel, so the
// panel would be dimmed too.  Move the overlay directly below the
// panel instead.
this._placeBelowPanel(actor);
```

```js
_placeBelowPanel(actor) {
    try {
        const layoutManager = this._layoutManager;
        const uiGroup = layoutManager?.uiGroup;
        const panelBox = layoutManager?.panelBox;
        if (!uiGroup || !panelBox)
            return;
        if (typeof uiGroup.set_child_below_sibling !== 'function')
            return;
        uiGroup.set_child_below_sibling(actor, panelBox);
    } catch (e) {
        g_printerr(`soft-dimmer: could not place the overlay below the panel: ${e}`);
    }
}
```

Every step is guarded: a missing `uiGroup`, a missing `panelBox`, a group
without `set_child_below_sibling`, or a thrown error all leave the overlay
where `addChrome()` put it. **A missing piece never stops the extension from
enabling** — the plan's mandatory guard (§7, Phase 4 item 3).

Resulting order (bottom → top):

```
window_group            application windows
  overlay               ← our dim layer (below the panel)
  panelBox              GNOME top panel        (drawn on top, undimmed)
  top_window_group      popup windows
  modalDialogGroup      modal dialogs          (undimmed)
  inputPanelGroup       OSD / input panel      (undimmed)
```

`trackFullscreen: false` is **kept** (plan §3.1, §3.4): the overlay must stay
over a fullscreen window — that is the whole point of the feature.

### 6.4 Cleanup — actor **and** tracking state — **CHANGED**

**Before:**

```js
_destroyAllOverlays() {
    for (const actor of this._overlays.values()) {
        const parent = actor.get_parent();
        if (parent)
            parent.remove_child(actor);
        actor.destroy();
    }
    this._overlays.clear();
    this._brightness.clear();
}
```

**After:**

```js
_destroyAllOverlays() {
    for (const actor of this._overlays.values()) {
        this._removeChrome(actor);
        actor.destroy();
    }
    this._overlays.clear();
    this._brightness.clear();
}
```

with `removeChrome()` used as the counterpart of the `addChrome()` call, and a
fallback to the old behaviour if it is unavailable or throws:

```js
_removeChrome(actor) {
    try {
        const layoutManager = this._layoutManager;
        if (layoutManager && typeof layoutManager.removeChrome === 'function') {
            layoutManager.removeChrome(actor);
            return;
        }
    } catch (e) {
        // The actor may already be gone; fall back to the plain removal.
    }
    const parent = actor.get_parent();
    if (parent)
        parent.remove_child(actor);
}
```

**An accuracy note on the plan's premise.** The plan (§3.1) states that a
tracked-but-destroyed actor "would keep the layout manager's bookkeeping
alive". In GNOME 51.1 that is **not literally true** — `_trackActor()`
installs a `destroy` handler that untracks the actor:

```js
// ui/layout.js:952
_trackActor(actor, params = {}) {
    …
    actor.connectObject(
        'notify::visible', this._queueUpdateRegions.bind(this),
        'notify::allocation', this._queueUpdateRegions.bind(this),
        'destroy', this._untrackActor.bind(this), this);
    this._trackedActors.push(actorData);
    …
}
```

So destroying the actor already removes it from `_trackedActors`. The change is
still the right one, for two reasons that *are* real:

1. `removeChrome()` is the **documented counterpart of `addChrome()`** —
   `uiGroup.remove_child()` **plus** `_untrackActor()` — so the extension now
   uses the Shell's own pairing instead of hand-rolling half of it.
2. Doing the untracking **before** `destroy()` avoids `_untrackActor()`'s
   `actor.disconnectObject(this)` running inside the actor's own `destroy`
   emission, and it keeps `_queueUpdateRegions()` on the right side of the
   teardown.

### 6.5 Animation / easing — **preserved, unchanged**

The easing pattern is exactly as it was, applied around the **alpha** write
and not around `opacity` (plan §3.1):

```js
actor.save_easing_state();
actor.set_easing_duration(TRANSITION_MS);      // 250 ms, unchanged
actor.set_easing_mode(Clutter.AnimationMode.EASE_OUT_QUAD);
actor.background_color = new Cogl.Color({ red: 0, green: 0, blue: 0, alpha });
actor.restore_easing_state();
```

`TRANSITION_MS = 250`, `EASE_OUT_QUAD`, the `visible = brightness < 1.0`
rule, the `reactive: false` rule, the per-monitor geometry from
`layoutManager.monitors`, and the `_onMonitorsChanged()` brightness-preserving
rebuild are all **byte-for-byte unchanged**. A static test now asserts the
easing block is still present.

### 6.6 What was deliberately *not* changed

| Not changed | Why |
|---|---|
| `dbus.js` | Phase 2 code; the task forbids changing it |
| `dbus/io.github.softdimmer.BrightnessControl.xml` | the public contract |
| `extension.js` | the rendering lifecycle needs no change (§6.2) |
| `indicator.js` | no rendering involvement |
| `metadata.json` | no Phase 3 requirement touches it |
| `schemas/*` | **no GSettings change** (see §10) |
| the capture-detection machinery | see §10 |

---

## 7. Tests

### 7.1 Test separation

| Suite | Runs where | Result |
|---|---|---|
| `test-logic.js` (Phase 3 extended) | **outside** GNOME Shell — plain `gjs`, no Clutter context needed | **92/92 pass** |
| the Phase 2 D-Bus suite (`p2-tests.js`) | outside GNOME Shell — private `dbus-daemon` | **98/98 pass** (regression) |
| real painting, real stacking, real animation | **requires a live GNOME Shell** | **PENDING** — §9 |

The live-rendering checks are printed by `test-logic.js` itself as an explicit
`PENDING` list, so the suite never implies that they passed:

```
PENDING — these need a live GNOME Shell and are NOT claimed here:
  L2  the overlay actor is really created, one per monitor
  L3  the screen visibly dims, monotonically, and not at brightness 1.0
  L4  the dimmed region really has the expected transmission
  L5  the minimum brightness really stops the dimming
  L6  the transition really eases over ~250 ms without flicker
  L7  the top panel is really drawn on top of the overlay
  L8  modal dialogs, L9 the Quick Settings menu and L10 the OSD are
      really unaffected by the dimming
  L11 the overlay really lets every click through
  L12 the overlay really still dims a fullscreen window
  L19 the overlay really disappears when the extension is disabled
```

### 7.2 Opacity conversion — the five required brightness values

```
  ok   brightness 1.00 -> alpha 0
  ok   brightness 1.00 -> dim strength 0.00
  ok   brightness 1.00 -> 0 rounds to 0
  ok   brightness 1.00 -> an integer in 0..255
  ok   brightness 0.75 -> alpha 64
  ok   brightness 0.75 -> dim strength 0.25
  ok   brightness 0.75 -> 63.75 rounds to 64
  ok   brightness 0.75 -> an integer in 0..255
  ok   brightness 0.50 -> alpha 128
  ok   brightness 0.50 -> dim strength 0.50
  ok   brightness 0.50 -> 127.5 rounds to 128
  ok   brightness 0.50 -> an integer in 0..255
  ok   brightness 0.25 -> alpha 191
  ok   brightness 0.25 -> dim strength 0.75
  ok   brightness 0.25 -> 191.25 rounds to 191
  ok   brightness 0.25 -> an integer in 0..255
  ok   brightness 0.10 -> alpha 230
  ok   brightness 0.10 -> dim strength 0.90
  ok   brightness 0.10 -> 229.5 rounds to 230
  ok   brightness 0.10 -> an integer in 0..255
  ok   the rounding rule is Math.round(), half up
  ok   the floor of 0.10 is a maximum dim of 90 %
  ok   brightness 1.0 is never dimmed at all
  ok   a brightness below the floor is raised to the floor, not below it
```

Each of the five points is asserted three ways: the exact expected alpha, the
exact intermediate arithmetic, and the general rule
`alpha === Math.round((1.0 - brightness) * 255)` plus "an integer in 0..255".

### 7.3 Lifecycle tests — actor removal and tracking-state cleanup

These exercise the real `MonitorDimmer` methods with stubbed collaborators, so
they run outside the Shell:

```
  ok   the overlay is re-ordered directly below the top panel
  ok   a missing panel is skipped instead of throwing
  ok   a missing panel leaves the placement alone
  ok   a group without set_child_below_sibling does not throw
  ok   teardown removes the overlay through removeChrome()
  ok   teardown falls back to removing the actor from its parent
  ok   a failing removeChrome() does not break the teardown
  ok   a failing removeChrome() still removes the actor from its parent
  ok   the actor is removed from the layout before it is destroyed
  ok   the teardown clears the overlay map
  ok   the teardown clears the brightness map
  ok   an empty teardown does nothing and does not throw
```

The `_destroyAllOverlays()` case asserts the **order**
(`removeChrome,destroy`), not just the end state — that is the tracking-state
guarantee.

### 7.4 Static / API guards

```
  ok   Cogl.Color exists in this GNOME
  ok   Clutter.Color does not exist any more
  ok   a Clutter.Actor has no track_hover property
  ok   a Clutter.Actor has no can_focus property
  ok   a Clutter.Actor still has reactive
  ok   a Clutter.Actor still has opacity
  ok   a Clutter.Actor still has background_color
  ok   St.Widget has track_hover (Clutter.Actor does not)
  ok   St.Widget has can_focus (Clutter.Actor does not)
  ok   St.Widget has reactive
  ok   St.Widget has background_color
  ok   St.Widget has opacity
  ok   St.Widget is a Clutter.Actor
  ok   Clutter.Actor:opacity is a guint (0-255), not a 0.0-1.0 float
  ok   Clutter.Actor:opacity defaults to 255 (fully opaque)
  ok   Clutter.Actor has set_child_below_sibling
  ok   Clutter.Actor has get_parent
  ok   Clutter.Actor has remove_child
  ok   Clutter.Actor has destroy
  ok   dimmer.js could be read
  ok   dimmer.js does not write actor.opacity
  ok   dimmer.js writes the dim level into background_color
  ok   dimmer.js scales the dim strength by 255
  ok   dimmer.js builds the overlay as an St.Widget
  ok   dimmer.js keeps the easing transition around the alpha write
  ok   dimmer.js orders the overlay below the top panel
  ok   dimmer.js looks the panel up on the layout manager
  ok   dimmer.js takes the overlay out of the layout with removeChrome()
```

### 7.5 Commands and results

```sh
export GI_TYPELIB_PATH=/usr/lib64/mutter-51:/usr/lib64/gnome-shell
export LD_LIBRARY_PATH=/usr/lib64/mutter-51:/usr/lib64/gnome-shell
cd ~/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer

gjs -m test-logic.js
#   → 92/92 checks passed            (exit 0)
```

```sh
# the Phase 2 D-Bus suite, re-run as a regression check (private bus)
export DBUS_SESSION_BUS_ADDRESS="$(cat /tmp/opencode/dbus/addr.persist)"
bash ~/.local/share/soft-dimmer/tests/p2-run-service.sh start
gjs -m ~/.local/share/soft-dimmer/tests/p2-tests.js
#   → 98/98 checks passed            (exit 0)
bash ~/.local/share/soft-dimmer/tests/p2-run-service.sh stop
```

| Suite | Checks | Passed | Failed | Exit |
|---|---|---|---|---|
| `test-logic.js` (Phase 3) | 92 | **92** | 0 | 0 |
| Phase 2 D-Bus suite (regression) | 98 | **98** | 0 | 0 |
| **Total automated** | **190** | **190** | **0** | 0 |

The Phase 1 suite went from 43 to 92 checks. Four of the original 43 were
**dead code** — the source-level regression guard sat *after*
`imports.system.exit()` and therefore never ran; it now runs before the
summary, and its four checks are live.

### 7.6 The live D-Bus interface is unaffected

Re-run against the live session after the changes (read-only):

```
GetBrightness(0) → (1.0,)
GetEnabled()     → (true,)
GetCapabilities()→ ({'version': <'1.0'>, 'monitor-count': <1>, 'has-dbus': <true>, 'supports-per-monitor': <true>},)
```

```
$ journalctl --user --since "-15 min" | grep -iE "soft-dimmer|params is not iterable|GObject_Struct"
(no output)
```

---

## 8. Integrity — nothing else was touched

### 8.1 Files modified

| File | Before | After |
|---|---|---|
| `dimmer.js` | `2681fe1be1506504205e5979091983b7fe2f6db19a978af5b0e462996b7fab35` | `076f22a63b0b11492ab00b98a9ab4fd9f8174327176517ee30a104d2c8b051da` |
| `test-logic.js` | `e8b36f9bf466467dc94d6382ebe5824aafb24006e77980a2bc9965e0672501a8` | `04e78202fd381e20d02fc37aef4eccdac26cf8f3436714f4c196ed3cfca29068` |

### 8.2 Files unchanged

```
  UNCHANGED  dbus.js
  UNCHANGED  extension.js
  UNCHANGED  indicator.js
  UNCHANGED  metadata.json
  UNCHANGED  schemas/gschemas.compiled
  UNCHANGED  schemas/org.gnome.shell.extensions.soft-dimmer.gschema.xml
  UNCHANGED  dbus/io.github.softdimmer.BrightnessControl.xml
```

**The Phase 2 D-Bus code and hash are unchanged:**

```
$ sha256sum …/dbus.js
f05c167a06b577d4007aef3ed46eb25d83b6e254c98fa85508655f2fbe384669   ✔ unchanged
```

The Phase 2 D-Bus suite was re-run end to end against the current tree and
still passes 98/98.

### 8.3 Session, settings and other projects

| Item | Before | After | Verdict |
|---|---|---|---|
| `brightness` | `1.0` | `1.0` | unchanged |
| `enabled` | `true` | `true` | unchanged |
| `minimum-brightness` | `0.1` | `0.1` | unchanged |
| `monitor-selection` | `'all'` | `'all'` | unchanged |
| `restore-brightness` | `true` | `true` | unchanged |
| `shortcut-*` | `@as []` | `@as []` | unchanged |
| owner of `io.github.softdimmer` | `:1.17` | `:1.17` | unchanged |
| extension `State` / `Enabled` | `ACTIVE` / `Yes` | `ACTIVE` / `Yes` | unchanged |

* `SetBrightness` / `SetEnabled` were **never** called — not against the live
  session, not against the private bus (the Phase 2 suite uses stubs).
* No extension was enabled, disabled, reloaded or restarted; the GNOME Shell
  was not restarted.
* No GSettings key was added, changed or removed; `gschemas.compiled` and the
  schema XML are byte-identical.
* ASUSCraft, Soft Brightness Plus, Soft Dimmer Pro and the bundled ASC
  extension were not modified or referenced.
* No existing backup was deleted. The two earlier backups and the new one all
  verify.

---

## 9. PENDING — the live rendering verification

The changes are on disk, but the running Shell still executes the **old**
`dimmer.js`:

```
$ ps -o pid,lstart,args -p "$(pgrep -x gnome-shell | head -1)"
  52999 Wed Nov 11 10:29:53 2026 /usr/bin/gnome-shell --mode=user
$ stat -c 'dimmer.js mtime: %y' …/dimmer.js
dimmer.js mtime: 2026-11-11 10:37:55
```

The Shell started at 10:29:53, the file was written at 10:37:55 — and
reloading the extension is forbidden in this phase. GJS caches extension
modules by URL inside the process, so even a reload would not help; only a
**new Shell process** (a logout/login) loads the new file.

**These checks are therefore PENDING and are not claimed as passed.**

### 9.1 The steps, for after the next logout/login

```sh
# 0. the new Shell must be newer than the new dimmer.js
ps -o pid,lstart -p "$(pgrep -x gnome-shell | head -1)"
stat -c '%y' ~/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer/dimmer.js

# 1. the extension must come up cleanly
gnome-extensions info soft-dimmer@io.github.softdimmer | grep -E 'State|Enabled'
journalctl --user --since "-5m" | grep -i soft-dimmer      # expect: no errors

# 2. L3/L4/L5 — the dim must be visible, monotonic and bounded
#    move the Quick Settings slider to 0.75, 0.50, 0.25 and 0.10 and watch:
#      1.00 → no dimming at all (no overlay is even shown)
#      0.75 → a light dim      (alpha 64)
#      0.50 → a clear dim       (alpha 128)
#      0.25 → a strong dim      (alpha 191)
#      0.10 → the maximum dim   (alpha 230) and it must not go further

# 3. L7 — the top panel must stay readable while the desktop is dimmed
#    set the brightness to 0.30 and look at the top bar

# 4. L8/L9/L10/L11 — modal dialog, the QS menu and the OSD must be unaffected,
#    and every click must reach the window underneath

# 5. L12 — a fullscreen window must still be dimmed

# 6. L19 — disable the extension: the dim must disappear completely

# 7. the D-Bus interface must still be correct
gdbus call --session --dest io.github.softdimmer \
  --object-path /io/github/softdimmer/BrightnessControl \
  --method io.github.softdimmer.BrightnessControl.GetBrightness 0     # (1.0,)
gdbus call --session --dest io.github.softdimmer \
  --object-path /io/github/softdimmer/BrightnessControl \
  --method io.github.softdimmer.BrightnessControl.GetEnabled         # (true,)
gdbus call --session --dest io.github.softdimmer \
  --object-path /io/github/softdimmer/BrightnessControl \
  --method io.github.softdimmer.BrightnessControl.GetCapabilities    # a{sv}
```

---

## 10. Capture detection — deferred, and why

The plan's **Phase 3** is *"Remove the incorrect capture detection (Defect 4)"*:
drop the `grab-op-begin` / `grab-op-end` wiring, and use
`Main.screenshotUI`'s `notify::screencast-in-progress` instead. The task's
rule 6 says: *"Do not implement capture-detection changes in this phase unless
the repair plan explicitly assigns them to Phase 3."*

**The condition is met — the plan does assign it to its Phase 3 — but the
change was still deliberately not made in this phase**, for three reasons that
are stated here so the decision can be reviewed:

1. **The task's Phase 3 is a different phase.** The task defines Phase 3 as
   the *opacity & rendering* work, and its required-work list is entirely
   rendering (opacity conversion, rendering lifecycle, stacking, cleanup). It
   deliberately pulls in the plan's *Phase 4* stacking/cleanup items while
   excluding capture handling, and then says *"Do not proceed to Phase 4"* —
   so the task's phase numbering and the plan's are deliberately offset.
2. **The plan's Phase 3 needs a GSettings key** (`hide-overlay-during-capture`,
   default `true`), which would mean changing the schema — and the task
   forbids that: *"Do not change GSettings or user brightness preferences."*
   The schema and `gschemas.compiled` are byte-identical after this phase.
3. **It is a live-untestable Shell-internal dependency.** `Main.screenshotUI`
   is not a public API, and the change could not be verified at all in this
   phase (no reload is allowed), so it would ship unproven.

**The defect therefore remains live**, and it is worth being explicit about
its user-visible effect: `Meta.Display::grab-op-begin/end` are **window
move/resize** signals, not capture signals, so today the overlay hides
during ordinary window dragging (a brightness flash on every window move)
and stays visible during actual screen recordings.

**Recommendation:** schedule the plan's Phase 3 capture work as its own
phase, with the schema change explicitly authorised. The plan's own
acceptance criteria for it (A19, L13–L16) are ready to use.

---

## 11. Remaining risks

| # | Risk | Severity | Mitigation |
|---|---|---|---|
| R1 | The change is not live; the visible result is unverified | Medium | §9 gives the exact steps; the static and API evidence is strong (the Shell's own `addChrome` source proves the ordering) |
| R2 | `layoutManager.uiGroup` / `.panelBox` are Shell internals and could change in a future GNOME | Low | every access is guarded; a missing piece leaves the old placement and never breaks the extension |
| R3 | `set_child_below_sibling(actor, panelBox)` requires both to be siblings in `uiGroup` | Low | they are: `panelBox` is added through `addChrome()` (`ui/layout.js:280`), and the call is wrapped in `try`/`catch` |
| R4 | The overlay is now *below* the panel, so a dimmed desktop no longer dims the panel | By design | that is the plan's Option B goal (§3.4); the panel is translucent over the desktop anyway |
| R5 | The capture-detection defect (window-drag flash) is still live | Medium | §10 — deliberately deferred, with the plan's phase identified |
| R6 | The plan's Defect 7 (`_restored` flag, non-idempotent `disable()`) is still live | Low | out of scope for a rendering phase; the plan assigns it to its Phase 5 |
| R7 | The live rendering could still differ from the static reasoning | Low | that is exactly why §9 is PENDING and not PASS |

---

## 12. Rollback

The hardened procedure from `SOFT_DIMMER_BACKUP_ROLLBACK_HARDENING_REPORT.md`
§8, applied to the **new** pre-Phase-3 backup. **It was not executed against
the live installation.**

```sh
# ---- 0. preconditions -------------------------------------------------
B=/home/mostafa/.local/share/soft-dimmer/backups/20261111T073708Z
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
  || { echo "ABORT: staging and the extensions directory are on different filesystems"; exit 1; }
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

Rolling back restores the pre-Phase-3 tree, i.e. the Phase 2 code with the
old stacking and the old teardown — a known-good D-Bus implementation with
the rendering defects back.

---

## 13. Artefacts

| Path | Contents |
|---|---|
| `/home/mostafa/.local/share/soft-dimmer/backups/20261111T073708Z/` | the verified pre-Phase-3 backup (tree + `MANIFEST.sha256` + `MANIFEST.meta` + `state/STATE.txt`) |
| `/home/mostafa/.local/share/soft-dimmer/tests/p3-test-logic.out` | the captured 92/92 output |
| `/home/mostafa/.local/share/soft-dimmer/tests/p3-dbus-regression.out` | the captured 98/98 regression output |
| `/home/mostafa/.local/share/soft-dimmer/tests/shell-layout.js` | GNOME Shell 51.1 `ui/layout.js`, extracted from `libshell-51.so` — the API evidence |
| `/home/mostafa/.local/share/soft-dimmer/tests/shell-main.js` | GNOME Shell 51.1 `ui/main.js`, same origin |

---

## 14. Final verdict

**PASS WITH LIMITATIONS.**

* The opacity conversion is correct, proven at the five required brightness
  values with the exact expected numbers and the exact rounding rule, and
  guarded by a static test so a 0.0–1.0 value can never reach a 0–255
  channel again.
* The overlay is now ordered **directly below the GNOME top panel**, using
  the API approach the repair plan documents — confirmed against the GNOME
  51.1 Shell's own `ui/layout.js`, with every step guarded.
* The teardown now uses `removeChrome()`, the counterpart of `addChrome()`, so
  the actor leaves the layout **and** the tracking state before it is
  destroyed; the order is asserted by a test.
* The easing behaviour, the colour semantics, the brightness semantics, the
  minimum-brightness meaning and the `visible` rule are all **unchanged**.
* The Phase 2 D-Bus code and its hash `f05c167a…` are **unchanged**, and the
  Phase 2 suite still passes 98/98 against the current tree.
* 190 automated checks pass (92 + 98); 0 fail.
* The live session was not disturbed, no GSettings value changed, no other
  project was touched, and no backup was deleted.

**Limitations:**

1. The **live rendering** verification is **PENDING** — the change is on disk
   but the running Shell still has the old module, and reloading is forbidden
   in this phase. The visible dimming, the panel readability and the
   animation are **not** claimed as proven.
2. The **capture-detection** defect (Defect 4 — the overlay hides during
   window dragging) is **deliberately still live**; the reasoning is in §10
   and the plan's own Phase 3 is the right home for it.
3. The plan's **Defect 7** (`_restored` flag, non-idempotent `disable()`) is
   out of scope for a rendering phase and remains open.

**Phase 4 was not started.**
