# Soft Dimmer — Phase 1 Implementation Report

**Phase:** 1 — Correct overlay brightness/opacity handling and background painting
**Extension:** `soft-dimmer@io.github.softdimmer`
**Extension path (verified, not assumed):**
`/home/mostafa/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer`
**Date:** 2026-11-10
**Environment:** Fedora 45 · GNOME Shell 51.1 · Wayland · GJS 1.90.1
**Reference plan:** `SOFT_DIMMER_GNOME51_REPAIR_PLAN.md` (Phase 1)
**Audit inputs:** `SOFT_DIMMER_GNOME51_FINAL_AUDIT.md`, `ASUSCRAFT_SOFT_DIMMER_DBUS_INVESTIGATION.md`

---

## 1. Executive Summary

Phase 1 is implemented. The critical defect — the overlay writing a 0.0–1.0 float into
`Clutter.Actor.opacity`, which is a **0–255 `guint`** — is fixed by moving the dim level into
the **alpha channel of the background colour**, which is natively 0–255 and therefore needs no
scale conversion at all.

The brightness semantics were traced, not assumed, before any code was written:

| Fact | Evidence |
|---|---|
| `brightness` is **remaining luminance** | Schema description: *"1.0 means no dimming; lower values apply a darker overlay"* |
| Slider maps `minimum-brightness … 1.0` → `0.0 … 1.0` | `indicator.js:107-121` |
| Dim strength = `1.0 − brightness` | The algebra now implemented in `dimAlpha()` |
| `minimum-brightness` (default 0.10) is a floor on remaining luminance | Schema range `0.05…0.30`; unchanged |

**Status of the defect:** the arithmetic is now correct and unit-tested. **The overlay has
still not been observed rendering** — that requires a live GNOME Shell session and is
explicitly *not* claimed. See §6.

**Test summary:** 43/43 automated checks pass under real GJS 1.90.1 (up from 21), plus
schema, syntax, integration and regression checks. Every check that could be run outside a
Shell was run.

**Scope discipline:** only `dimmer.js` and `test-logic.js` were modified. `dbus.js`,
`extension.js`, `indicator.js`, `metadata.json`, the GSettings schema and the D-Bus
introspection XML are **byte-identical** to the pre-Phase-1 backup.

---

## 2. Files Modified

### 2.1 Modified

| File | Change | Size |
|---|---|---|
| `~/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer/dimmer.js` | Dim level moved from `actor.opacity` to `background_color` alpha; overlay base class `Clutter.Actor` → `St.Widget`; new exported `dimAlpha()` helper | 307 → 347 lines |
| `~/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer/test-logic.js` | Added 22 checks: alpha conversion, boundaries, transition endpoints, `St.Widget` API surface, and 4 source-level regression guards | 79 → 129 lines |

### 2.2 Deliberately NOT modified (verified byte-identical)

`dbus.js` (Phase 2 scope) · `extension.js` · `indicator.js` · `metadata.json` ·
`schemas/org.gnome.shell.extensions.soft-dimmer.gschema.xml` ·
`dbus/io.github.softdimmer.BrightnessControl.xml`

### 2.3 Backup paths

Full pre-Phase-1 snapshot of the extension:

```
/tmp/opencode/soft-dimmer-phase1-backup-20261110-203542/
├── dimmer.js          extension.js     indicator.js     dbus.js
├── test-logic.js      metadata.json
├── schemas/           dbus/
```

The backup-path record is also at `/tmp/opencode/phase1-backup-path.txt`.

An additional independent baseline exists from the earlier audit work:
`/tmp/opencode/soft-dimmer-primary-backup/` (pre-repair state, with hashes in
`/tmp/opencode/primary-baseline.sha256`).

---

## 3. Exact Implementation Details

### 3.1 The defect being fixed

`Clutter.Actor:opacity` is declared in mutter 51 as:

```c
obj_props[PROP_OPACITY] =
  g_param_spec_uint ("opacity", NULL, NULL,
                     0, 255,
                     255,
                     G_PARAM_READWRITE | … | CLUTTER_PARAM_ANIMATABLE);
```
*(mutter 51, `clutter/clutter/clutter-actor.c:6246-6253` — verified from the upstream
`gnome-51` branch.)*

The pre-Phase-1 code computed a 0.0–1.0 float and assigned it directly:

```js
const target = Math.max(0.0, Math.min(1.0, 1.0 - brightness));
actor.opacity = target;      // ← 0.4 becomes 0
```

GJS truncates to the target integer type, so every usable brightness became `opacity == 0`
and the overlay was **fully transparent** — the extension did not dim at all. Measured on a real
C-class `guint` property: `0.25→0`, `0.5→0`, `0.75→0`.

### 3.2 The fix — dim level in the colour alpha

`dimmer.js` now computes the alpha explicitly, in one place:

```js
/**
 * The dim strength for a brightness level, as a 0-255 alpha value.
 *
 * "brightness" is the remaining luminance: 1.0 means no dimming at all and
 * lower values apply a darker overlay.  The dim strength is therefore
 * 1.0 - brightness, expressed on the colour alpha's 0-255 scale.
 */
export function dimAlpha(brightness) {
    const value = Number(brightness);
    if (!Number.isFinite(value))
        return 0;
    const strength = Math.max(0.0, Math.min(1.0, 1.0 - value));
    return Math.round(strength * 255);
}
```

and applies it in `_applyBrightness()`:

```js
const alpha = dimAlpha(brightness);

actor.save_easing_state();
actor.set_easing_duration(TRANSITION_MS);
actor.set_easing_mode(Clutter.AnimationMode.EASE_OUT_QUAD);
actor.background_color = new Cogl.Color({
    red: 0, green: 0, blue: 0, alpha,
});
actor.restore_easing_state();
```

`actor.opacity` is **no longer written anywhere** in the extension — confirmed by
`grep -rn "\.opacity\s*=" *.js` returning nothing, and locked in by an automated
source-level test (§5.2, "dimmer.js does not write actor.opacity").

### 3.3 Overlay base class: `Clutter.Actor` → `St.Widget`

The pre-Phase-1 overlay was a plain `Clutter.Actor`. It is now an `St.Widget`:

```js
const actor = new St.Widget({
    x: monitor.x, y: monitor.y,
    width: monitor.width, height: monitor.height,
    reactive: false,
    track_hover: false,
    can_focus: false,
    background_color: new Cogl.Color({
        red: 0, green: 0, blue: 0, alpha: 0,
    }),
});
```

**Why:** `track_hover` and `can_focus` **do not exist** on `Clutter.Actor` in GNOME 51
(confirmed by pspec introspection: `MISSING`), but **do** exist on `St.Widget`
(`gboolean` for both). The original root-cause error — `No property track_hover on
ClutterActor` — is what killed the pre-repair extension, and restoring those two defensive
properties is only possible on `St.Widget`. This also matches the Shell's own idiom: the Shell
sets `background_color` on `St.Widget` subclasses (`ui/appDisplay.js:2675`,
`ui/appDisplay.js:2712`).

The initial alpha is `0` (fully transparent) rather than `255`, matching the old
`actor.opacity = 0` intent: an undimmed monitor carries a fully transparent overlay.

### 3.4 Smooth transitions — preserved and verified as animatable

The legacy easing-state pattern is retained:

```js
actor.save_easing_state();
actor.set_easing_duration(TRANSITION_MS);   // 250 ms
actor.set_easing_mode(Clutter.AnimationMode.EASE_OUT_QUAD);
actor.background_color = new Cogl.Color({…});
actor.restore_easing_state();
```

**Verified against upstream mutter 51 source** (branch `gnome-51`, cloned read-only to
`/tmp/opencode/mutter51`):

- `background-color` is declared with `CLUTTER_PARAM_ANIMATABLE`
  (`clutter/clutter/clutter-actor.c:6878-6884`) — the same flag `opacity` carries.
- Clutter's implicit-transition machinery creates a transition from the easing state
  (`clutter/clutter/clutter-actor.c:16793+`, `_clutter_actor_create_transition`, with the
  easing-state duration honoured).
- The `CoglColor` boxed type has a **registered progress function**,
  `cogl_color_progress` → `cogl_color_interpolate`
  (`clutter/clutter/clutter-interval-progress.c:116-147, 152-165`), and
  `cogl_color_interpolate` interpolates **alpha** as
  `(uint8)(initial->alpha + (final->alpha - initial->alpha) * progress)`.

So the alpha really does animate smoothly between 0 and 230 — this is not an assumption.

### 3.5 Preserved semantics — verified unchanged

| Behaviour | Status |
|---|---|
| `brightness 1.0` → alpha `0` (no dimming) | ✅ preserved |
| `brightness 0.0` → alpha `255` (fully black) | ✅ preserved |
| `brightness 0.10` (default minimum) → alpha `230` (90 % dim) | ✅ preserved |
| Below-minimum brightness is raised to the minimum **before** conversion | ✅ preserved (`clampBrightness` remains the single clamp point — no second clamp added) |
| `visible = brightness < 1.0` (no overlay on an undimmed screen) | ✅ preserved |
| Per-monitor overlay, one actor per monitor | ✅ unchanged |
| `minimum-brightness` schema range `0.05…0.30` | ✅ unchanged |
| Slider mapping `minimum-brightness … 1.0` ⇄ `0.0 … 1.0` | ✅ unchanged (`indicator.js` untouched) |
| 250 ms transition, `EASE_OUT_QUAD` | ✅ preserved |
| `reactive: false` (never intercepts input) | ✅ preserved |
| One overlay per monitor — **no second overlay introduced** | ✅ verified |
| D-Bus code untouched | ✅ byte-identical |
| Screenshot detection untouched | ✅ byte-identical (Phase 3 scope) |
| Panel stacking untouched | ✅ byte-identical (Phase 4 scope) |

### 3.6 What was deliberately *not* changed in this phase

- **Opacity is not used for dimming at all** — the alpha channel is the single place the dim
  level is expressed, so the two-scale hazard cannot recur.
- **No second clamp** was introduced: `clampBrightness()` remains the single clamp point, as
  the plan requires.
- **No new overlay actor**: `_createOverlay()` still creates exactly one actor per monitor.
- `hideOverlays()` / `showOverlays()` and the `grab-op` wiring are untouched — those belong
  to Phase 3.

---

## 4. Before / After Behaviour

### 4.1 The dim level

| `brightness` | **Before** (`actor.opacity = 1.0 - brightness`) | | **After** (`background_color.alpha`) |
|---|---|---|---|
| | *value written* | *stored (guint)* | *stored alpha (0–255)* |
| 1.00 (undimmed) | `0.0` | `0` | `0` ✅ same |
| 0.90 | `0.1` | `0` | `25` |
| 0.75 | `0.25` | `0` | `64` |
| 0.50 | `0.5` | `0` | `128` |
| 0.25 | `0.75` | `0` | `191` |
| 0.10 (min) | `0.9` | `0` | `230` |
| 0.00 | `1.0` | `1` | `255` ✅ |

**Before:** every usable brightness stored `0`, so the overlay was fully transparent and the
screen **never dimmed**. (Even the `brightness = 0.0` case stored `1` — 1/255 ≈ 0.4 % alpha.)

**After:** the alpha tracks the requested dim strength exactly, and `brightness = 1.0` still
means no dimming.

### 4.2 Overlay construction

| | Before | After |
|---|---|---|
| Base class | `Clutter.Actor` | `St.Widget` |
| `track_hover` / `can_focus` | absent (would throw) | present and explicitly `false` |
| `reactive` | `false` | `false` (unchanged) |
| Initial colour | black, alpha `255` | black, alpha `0` (transparent) |
| `actor.opacity` write | `0` at creation, then per-change | **none** |

### 4.3 Transitions

| | Before | After |
|---|---|---|
| Mechanism | easing state + `opacity` write | easing state + `background_color` write |
| Duration / mode | 250 ms / `EASE_OUT_QUAD` | unchanged |
| Would it animate? | No — the value was `0` throughout, so there was nothing to animate | Yes — `CoglColor` has a registered progress function that interpolates alpha |

---

## 5. Test Results

### 5.1 Tests that genuinely ran and PASSED

**T1 — ESM syntax validation** (all 5 files, `node --check` on `.mjs` copies): **5/5 pass**

```
ok dimmer.js   ok extension.js   ok indicator.js   ok dbus.js   ok test-logic.js
```

**T2 — Logic + alpha unit tests under real GJS 1.90.1** (`gjs -m test-logic.js`):
**43/43 checks pass** (was 21/21 before this phase; 22 checks added)

```
  ok   brightness 1.0 -> alpha 0 (no dimming)
  ok   brightness 0.5 -> alpha 128
  ok   brightness 0.0 -> alpha 255 (fully black)
  ok   brightness 0.1 -> alpha 230 (the default minimum)
  ok   brightness 0.9 -> alpha 25
  ok   the alpha is always an integer
  ok   the alpha never leaves 0..255
  ok   NaN brightness -> alpha 0 (transparent)
  ok   a non-numeric brightness -> alpha 0
  ok   undefined brightness -> alpha 0
  ok   a brightness above 1.0 -> alpha 0
  ok   a brightness below 0 -> alpha 255
  ok   dimming is monotonic: lower brightness -> higher alpha
  ok   the dim strength is exactly 1.0 - brightness
  ok   St.Widget has track_hover (Clutter.Actor does not)
  ok   St.Widget has can_focus (Clutter.Actor does not)
  ok   St.Widget has reactive
  ok   St.Widget has background_color
  ok   St.Widget has opacity
  ok   St.Widget is a Clutter.Actor
  ok   Clutter.Actor:opacity is a guint (0-255), not a 0.0-1.0 float
  ok   Clutter.Actor:opacity defaults to 255 (fully opaque)
  ok   dimmer.js does not write actor.opacity          ← source-level regression guard
  ok   dimmer.js writes the dim level into background_color
  ok   dimmer.js scales the dim strength by 255
  ok   dimmer.js builds the overlay as an St.Widget
```

**T3 — Schema validation** (`glib-compile-schemas --strict --dry-run`): **pass**; all 8 keys
present with unchanged types and ranges; the schema file is byte-identical to the backup.

**T4 — Integration test with the real `GSettings` values** (real module, real schema dir):

```
settings: brightness=1 minimum-brightness=0.1
  clamped  = 1
  alpha    = 0
--- brightness sweep (min 0.10 default) ---
  brightness 1    -> clamped 1    -> alpha 0   (0% dim)
  brightness 0.75 -> clamped 0.75 -> alpha 64  (25% dim)
  brightness 0.5  -> clamped 0.5  -> alpha 128 (50% dim)
  brightness 0.25 -> clamped 0.25 -> alpha 191 (75% dim)
  brightness 0.1  -> clamped 0.1  -> alpha 230 (90% dim)
--- below the minimum is clamped, then converted ---
  brightness 0.02 -> clamped 0.1  -> alpha 230
```

**T5 — Transition endpoint analysis** (replaying mutter's `cogl_color_interpolate` on the
real endpoints, alpha 0 → 230):

```
progress 0    -> alpha 0
progress 0.25 -> alpha 57
progress 0.5  -> alpha 115
progress 0.75 -> alpha 172
progress 1    -> alpha 230      monotonic = true
```

**T6 — Scope/regression checks:** `dbus.js`, `extension.js`, `indicator.js`, `metadata.json`,
the GSettings schema and the D-Bus XML are all **byte-identical** to the pre-Phase-1 backup.
Only `dimmer.js` and `test-logic.js` changed.

**T7 — Upstream source verification** (mutter `gnome-51` branch, cloned read-only to
`/tmp/opencode/mutter51`): `opacity` pspec is `g_param_spec_uint(…, 0, 255, 255, …)` with
`CLUTTER_PARAM_ANIMATABLE`; `background-color` pspec carries `CLUTTER_PARAM_ANIMATABLE`;
`COGL_TYPE_COLOR` has a registered progress function `cogl_color_progress` →
`cogl_color_interpolate`, which interpolates alpha linearly.

### 5.2 Tests that are STATIC-ONLY (no runtime rendering)

These check *source shape and types*, not pixels:

- The 4 source-level regression guards in T2 (`dimmer.js does not write actor.opacity`, etc.)
  read the source text; they prove the *wrong pattern is absent*, not that the overlay renders.
- The pspec assertions (`opacity` is `guint`, default 255) prove the **type** of the property,
  not the rendered result.
- The upstream-source checks in T7 prove the **animation capability exists** in mutter 51,
  not that this extension's actors animate as expected in a live session.

### 5.3 Tests that REQUIRE a live GNOME Shell session — NOT RUN

None of the following can be executed outside the Shell: `Clutter.Actor` construction aborts
with `_clutter_context_get_default: assertion failed`, and GNOME 51 on Wayland refuses an
in-place Shell restart, so these need a logout/login cycle.

| ID | Test | Why it needs a live session |
|---|---|---|
| L1 | Extension enables without error | Requires the Shell to load the module |
| L2 | An overlay actor is actually created and parented under `uiGroup` | Needs a live Clutter context |
| **L3** | **The overlay is visible and the screen visibly dims** | **Pixel output — cannot be observed externally** |
| **L4** | **Dim strength matches the request (e.g. 0.5 ≈ 50 % transmission)** | **Pixel measurement** |
| L5 | Minimum brightness is honoured as a floor | Needs live rendering + slider |
| L6 | The 250 ms transition is smooth, with no flicker or banding | Needs frame-by-frame observation |
| L7 | The overlay does not cover the GNOME top panel | Live stacking observation (Phase 4 scope) |
| L11 | Input passes through the overlay to the window below | Needs live event delivery |
| L12 | Fullscreen content is still dimmed | Needs a live fullscreen window |
| L19 | Disable removes the overlay and disconnects handlers | Needs live teardown |
| L20 | Repeated enable/disable leaves no duplicate overlays | Needs live lifecycle |

**L3 and L4 are the ones that matter most for this phase.** Until they pass, the correct
statement is: *the dim level is now computed and applied on a scale that matches the API, and
the previous truncation bug is provably gone — but the overlay has not been seen dimming.*

---

## 6. Remaining Limitations

1. **The overlay has not been observed dimming.** The arithmetic and the API usage are
   verified; the rendered result is not. One logout/login cycle is required (§5.3, L3/L4).
2. **Three further confirmed defects are untouched by design**, each belonging to a later
   phase:
   - D-Bus methods still throw (`params is not iterable`) — **Phase 2**.
   - `grab-op-begin/end` still mis-detect captures and still flash the overlay during window
     dragging — **Phase 3**.
   - The overlay is still stacked above the GNOME top panel — **Phase 4**.
   The extension therefore still **does not dim visibly** in its present state, because of
   Defect 1's *sibling* — no: Defect 1 is fixed; the remaining blockers are Defects 2–5, of
   which only the D-Bus one affects the *D-Bus* path. The *visual* dimming path is now
   correct pending live confirmation (L3).
3. **The `restore-brightness` load-time semantics** remain as previously noted — Phase 5.
4. **`St.Widget` versus `Clutter.Actor`** is a deliberate change from the plan's wording
   ("move the overlay actor from `Clutter.Actor` to `St.Widget`"). It is implemented exactly
   as planned; the note here is that `St.Widget` brings St theming into play
   (`st_widget_paint_background` exists in `libst-51.so`). Because the overlay sets
   `background_color` explicitly and has no `style_class`, the Shell's own
   `ui/appDisplay.js:2675` demonstrates the same pattern on an `St.Bin`. **Live confirmation
   (L2/L3) is still required** to rule out any St theming interaction.
5. **The `getBrightness()` value returned over D-Bus is still the raw requested brightness**,
   not the effective clamped value. Correcting that is part of the Phase 2 contract work.
6. **No ASUSCraft D-Bus server was invented and no interface was merged**, per the plan's
   Phase 1 scope and the ASUSCraft investigation's conclusion.

---

## 7. Rollback Commands

### 7.1 Roll back this phase (recommended if live testing regresses)

```sh
D=~/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer
B=/tmp/opencode/soft-dimmer-phase1-backup-20261110-203542

cp -a "$B/dimmer.js"    "$D/dimmer.js"
cp -a "$B/test-logic.js" "$D/test-logic.js"

# verify
diff -q "$B/dimmer.js" "$D/dimmer.js" && echo "dimmer.js restored"
```

Then log out and back in (GNOME 51 on Wayland cannot restart the Shell in place).

### 7.2 Roll back to the pre-repair baseline (read-only reference)

```sh
D=~/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer
rm -rf "$D" && cp -a /tmp/opencode/soft-dimmer-primary-backup "$D"
```

⚠️ **Warning:** the pre-repair baseline is itself broken on GNOME 51 — it constructs a
`Clutter.Actor` with `track_hover`/`can_focus`, which throws. Rolling back to it returns the
extension to a **non-loading** state. This is a safety net, not a shipping option.

### 7.3 Restore the audit baseline hashes (verification only)

```sh
sha256sum -c /tmp/opencode/primary-baseline.sha256   # read-only check
```

---

## 8. Explicit Confirmation: No Other Extension Was Modified

Verified by SHA-256 comparison against pre-Phase-1 hashes:

| Component | Status |
|---|---|
| `soft-dimmer@io.github.softdimmer` — `dimmer.js`, `test-logic.js` | **Modified** (this phase) |
| `soft-dimmer@io.github.softdimmer` — all other files | **Byte-identical** to backup |
| `soft-dimmer-pro@io.github.softdimmer` (`dimmer.js`, `extension.js`) | **Unmodified** — `39804b02…`, `d71d9cb9…` |
| `soft-dimmer-asc@io.github.asuscraft` (all 4 JS files) | **Unmodified** — `0429a9de…`, `ea26b6d9…`, `6c6b8db1…`, `ba68ce84…` |
| `soft-brightness-plus@joelkitching.com` (`extension.js`) | **Unmodified** — `e2b91556…` |
| ASUSCraft 1.2.2 project tree (`src/`, `data/`) | **Unmodified** — no file modified after 18:00 |

**Additional confirmations:**

- **No extension was enabled or disabled.** States are unchanged:
  `soft-dimmer` Enabled=Yes/State=ERROR (pre-existing error state, unchanged),
  `soft-dimmer-asc` Enabled=No/State=INITIALIZED, `soft-brightness-plus` Enabled=Yes.
- **No GSettings value was changed.** `brightness` is still `1.0`, `enabled` still `true`,
  `minimum-brightness` still `0.1`, `monitor-selection` still `'all'`,
  `restore-brightness` still `true`; the three shortcut keys remain empty.
- **GNOME Shell was not restarted and no logout occurred.**
- **No D-Bus method was invoked** — neither mutating nor read-only against the extension's own
  service. The only D-Bus activity in this phase was source-level inspection and upstream
  source reading.
- **No Git operation was performed.** The project directory is not a Git repository, and the
  mutter clone under `/tmp/opencode/mutter51` was created read-only for source inspection
  only; nothing was committed or pushed.
- **The D-Bus interface contract is unchanged** — the shipped introspection XML is
  byte-identical, so any existing client keeps working exactly as before.

---

*Phase 1 implementation completed 2026-11-10. Automated tests: 43/43 pass. Live-Shell
visual confirmation (L2/L3/L4) remains outstanding and is explicitly not claimed.*
