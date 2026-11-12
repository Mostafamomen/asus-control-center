# Pre-Implementation Consistency Audit — Soft Dimmer / ASUSCraft

**Date:** 2026-11-11
**Documents reviewed:** `AGENTS.md`, `SOFT_DIMMER_GNOME51_REPAIR_PLAN.md`, `SOFT_DIMMER_GNOME51_FINAL_AUDIT.md`, `ASUSCRAFT_SOFT_DIMMER_DBUS_INVESTIGATION.md` (plus `SOFT_DIMMER_PHASE1_IMPLEMENTATION_REPORT.md`, the state of the actual extension)
**Mode:** Read-only. Nothing was edited, built, enabled, disabled, restarted or logged out of.

> **Scope note.** The request describes reviewing a *Phase 1 repair plan* before implementing it.
> **Phase 1 is already implemented.** `dimmer.js` (mtime 2026-11-10 20:36) and `test-logic.js`
> (20:37) were changed in the previous session, and the machine **rebooted at 08:27 today**,
> so the running GNOME Shell loaded that code. This review therefore audits the plan
> **retrospectively against the code it produced**, and re-verifies every claim from scratch.
> Where a plan step is already implemented, it is marked *implemented* rather than *pending*.

---

## 1. Verdict summary

| Area | Result |
|---|---|
| Soft Brightness Plus vs. external Soft Dimmer distinction | **PASS** |
| Extension paths | **PASS** (with one **FAIL** on backup paths) |
| Backup requirements | **FAIL** — every documented rollback target is gone |
| GNOME 51.1 API assumptions | **PASS** — all re-verified against upstream `gnome-51` source |
| Brightness semantics | **PASS** |
| Clutter opacity / background-alpha handling | **PASS** (implemented correctly) |
| Easing-state animation compatibility | **PASS on the API**, **PENDING on live rendering** |
| Test commands that actually work | **PASS** |
| No accidental Git / release assumptions | **PASS** |
| No instructions authorizing unrelated changes | **PASS** |
| **Phase 1 can proceed?** | **Already done** — but the live visual check (L3/L4) is still outstanding, and the backup gap must be closed first |

**Overall: the documents are internally consistent and safe to execute.** One **FAIL** is
operational, not editorial: the backups the rollback instructions depend on no longer exist.

---

## 2. Findings

### 2.1 PASS — Soft Brightness Plus vs. external Soft Dimmer

Re-verified, and this is the single most important distinction in the repository:

- `grep -rn "soft-dimmer@" --include=*.cpp --include=*.h --include=Makefile --include=*.sh` over the
  whole repo → **zero hits**. The Soft Dimmer extension is genuinely outside this repository.
- `src/control/softdim.cpp:16` → `#define SBP_UUID "soft-brightness-plus@joelkitching.com"`; the
  bundled payload is `data/soft-brightness-plus/`.
- `src/control/brightness.cpp:14-15` uses the *same* extension's schema
  (`org.gnome.shell.extensions.soft-brightness-plus`) for the OLED dim bridge.
- `AGENTS.md` §0 states this correctly, names both UUIDs, and warns against editing the repo to
  "fix" Soft Dimmer. `SOFT_DIMMER_GNOME51_REPAIR_PLAN.md` §5 and the Phase 1 report both
  honour it.

The two are also separate on disk: Soft Dimmer lives in
`~/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer/`, while the repo's
bundled extension is `data/soft-brightness-plus/` — and the *system* ASC variant
(`/usr/share/gnome-shell/extensions/soft-dimmer-asc@io.github.asuscraft/`) is a third, unrelated
extension. `AGENTS.md` §6 correctly notes the two dimmers dim additively.

**No conflict.**

### 2.2 PASS — Extension paths

Every path referenced across the four documents resolves to the same real location, confirmed
by `realpath`:

```
/home/mostafa/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer
```

The `~` and `$HOME` forms in the docs are equivalent. Files present: `dimmer.js`,
`extension.js`, `indicator.js`, `dbus.js`, `test-logic.js`, `metadata.json`, `schemas/`, `dbus/`.
The schema id `org.gnome.shell.extensions.soft-dimmer` matches `metadata.json`. **No conflict.**

### 2.3 FAIL — Backup requirements (operational, **blocking**)

**Every rollback path documented in `SOFT_DIMMER_PHASE1_IMPLEMENTATION_REPORT.md` §7 is gone.**

| Documented path | Status |
|---|---|
| `/tmp/opencode/soft-dimmer-phase1-backup-20261110-203542` | **MISSING** |
| `/tmp/opencode/soft-dimmer-primary-backup` | **MISSING** |
| `/tmp/opencode/primary-baseline.sha256` | **MISSING** |
| `/tmp/opencode/phase1-backup-path.txt` | **MISSING** |

`/tmp/opencode/` is now **empty**. The machine rebooted between sessions, and `/tmp` did not
survive. The report's rollback command

```sh
B=/tmp/opencode/soft-dimmer-phase1-backup-20261110-203542
cp -a "$B/dimmer.js" "$D/dimmer.js"
```

would now **fail silently-ish** (`cp` errors, leaving the current file in place) or, worse, the
`rm -rf "$D" && cp -a …` variant in §7.2 would **delete the extension** and then fail to
restore it, leaving the user with no extension at all.

**This is the one blocker.** Two sub-problems:

1. **Durability.** A backup under `/tmp` is not a backup. The task's own restriction says temp
   files must stay in `/tmp/opencode/`, which is right for scratch work but wrong for the
   *only* copy of the pre-Phase-1 source.
2. **`rm -rf "$D"` before a verified restore.** §7.2 orders the destructive step *before* the
   copy and never checks that the source exists. It should be
   `cp -a <backup>/. "$D"/` (never `rm -rf` first), and the backup must be validated
   (hash + `node --check`) before any destructive step.

**Consequence for the plan.** `SOFT_DIMMER_GNOME51_REPAIR_PLAN.md` §7 (Risks and Rollback)
inherits this: it offers the pre-repair baseline at `/tmp/opencode/soft-dimmer-primary-backup`
as the "safety net" — which no longer exists, and which the plan itself warns is *broken on
GNOME 51 anyway* (it constructs a `Clutter.Actor` with `track_hover`/`can_focus`, which throws).
So the plan's rollback story for **Phases 2–7** currently has **no working target**.

**Required before further phases:** re-create a durable backup outside `/tmp` (e.g.
`~/Documents/asus-control-center-1.2.2/.soft-dimmer-backups/<timestamp>/`) containing the
current `dimmer.js`, `extension.js`, `indicator.js`, `dbus.js`, `test-logic.js`, plus the
pre-Phase-1 `dimmer.js`/`test-logic.js` and a `sha256sum` manifest. This is a *documentation
and process* fix, not a code change.

### 2.4 PASS — GNOME 51.1 API assumptions

Every API claim was re-verified against the upstream `gnome-51` branch (cloned read-only to
`/tmp/opencode/m51` for this audit) and against the live typelibs:

| Claim | Verified |
|---|---|
| `Clutter.Actor:opacity` is `guint` 0–255, default 255 | `clutter-actor.c:6246-6253` — `g_param_spec_uint("opacity", …, 0, 255, 255, …, CLUTTER_PARAM_ANIMATABLE)`; live pspec confirms `guint`/255 |
| `Clutter.Actor` has **no** `track_hover`/`can_focus` | live pspec lookup: `MISSING`; `St.Widget` has both |
| `Clutter.Color` gone; `Cogl.Color` current | live: `typeof Clutter.Color === 'undefined'`, `Cogl.Color` is a function |
| `background-color` is `CoglColor` | live pspec on both `Clutter.Actor` and `St.Widget` |
| `global.get_dbus_proxy` removed | unchanged from the audit; not re-probed here |
| `Meta.Display` grab-op = window ops only | `Meta.GrabOp` enumeration contains only `MOVING`/`RESIZING_*`/`KEYBOARD_*` — no capture member |
| `addChrome()` lands **above** the top panel | `ui/layout.js:878-883` — `uiGroup.add_child()` + `set_child_below_sibling(actor, top_window_group)`; `panelBox` is added via `addChrome()` earlier (`ui/layout.js:280`), so a later `addChrome()` sits above it |

**No conflict.** The plan's §3.1–§3.4 API reasoning matches the source exactly.

### 2.5 PASS — Brightness semantics

Consistent across all four documents and re-derived from the schema itself:

- `brightness` = **remaining luminance** (schema: *"1.0 means no dimming; lower values apply a
  darker overlay"*).
- `minimum-brightness` (default 0.10) = floor on remaining luminance → max 90 % dim.
- dim strength = `1.0 − brightness`; alpha = `round(strength × 255)`.

Verified live against the real module: `dimAlpha(1.0)=0`, `dimAlpha(0.5)=128`, `dimAlpha(0.1)=230`.
The plan (§3.1), the audit (§2.1) and `AGENTS.md` §6 all state the same algebra. **No conflict.**

### 2.6 PASS — Clutter opacity / background-alpha handling

The implemented fix is correct and matches the plan §3.1 exactly:

- `dimAlpha()` is a **single, exported, unit-tested** conversion point.
- `actor.opacity` is **never written** — `grep -c "actor.opacity"` over the extension → **0**.
  The pre-Phase-1 code wrote `1.0 - brightness` into that 0–255 `guint`, truncating to `0` for
  every usable brightness — the exact defect the audit found.
- The dim level now lives in `background_color`'s alpha, which is natively 0–255.
- `clampBrightness()` remains the only clamp point (plan §3.1 change 5 — no second clamp added).
- `visible = brightness < 1.0` preserved.
- One overlay per monitor — no second overlay introduced.

The `test-logic.js` suite now carries **43/43 passing checks** (from 21), including four
*source-level* guards that fail if anyone reintroduces a 0–1 value into a 0–255 channel.

### 2.7 PASS (API) / PENDING (live rendering) — Easing-state animation

This is the most subtle item, so it was checked against source rather than assumed.

**What the code does now** (`dimmer.js:326-334`):

```js
const alpha = dimAlpha(brightness);
actor.save_easing_state();
actor.set_easing_duration(TRANSITION_MS);   // 250
actor.set_easing_mode(Clutter.AnimationMode.EASE_OUT_QUAD);
actor.background_color = new Cogl.Color({ red: 0, green: 0, blue: 0, alpha });
actor.restore_easing_state();
```

**Verified against mutter 51 source:**

1. **`save_easing_state()` / `restore_easing_state()` are used correctly** — the write happens
   strictly *between* them, which is the required pairing.
2. **`set_easing_duration()` / `set_easing_mode()` are used correctly** — they mutate the
   *current* easing state, which is exactly what
   `should_skip_implicit_transition()` reads (`info->cur_state->easing_duration == 0` → skip).
   With a 250 ms duration the implicit transition is **not** skipped.
3. **The target property is animatable in Mutter 51.** This was the real risk, and it resolves
   favourably: `clutter_actor_set_background_color()` ends in
   `_clutter_actor_create_transition(self, obj_props[PROP_BACKGROUND_COLOR], &priv->bg_color, color)`
   (`clutter-actor.c:16211-16214`) — a real transition on the boxed colour, and
   `COGL_TYPE_COLOR` has a registered progress function (`cogl_color_progress` →
   `cogl_color_interpolate`, `clutter-interval-progress.c:116-165`) that interpolates **alpha**
   linearly. So the alpha genuinely animates.
4. **Rapid brightness updates behave correctly.** A second write for the same property
   **reuses and retargets** the existing transition rather than stacking a new one: the code
   looks the property up in `info->transitions`, rewinds the timeline, and resets the interval's
   initial/final values (`clutter-actor.c:16917-16945`). Dragging the slider therefore produces
   one smooth retargeted transition, not a pile-up. `set_transition_set_remove_on_complete(res, TRUE)`
   means a finished transition is dropped cleanly.
5. **`disable()` does not explicitly cancel pending animations** — `_destroyAllOverlays()` calls
   `parent.remove_child(actor)` then `actor.destroy()`. There is **no** `remove_all_transitions()`
   call. In practice the destroyed actor takes its transitions with it (they live in the actor's
   `ClutterAnimationInfo`), but this is *not* explicitly guaranteed by the API and is a latent
   risk worth noting. The plan's Phase 5 (lifecycle hardening) is the right place to make
   teardown explicit.

**One caveat that matters:** `should_skip_implicit_transition()` also returns TRUE when the actor
**is not mapped**. An overlay that is `visible = false` (brightness 1.0 → no dimming) is unmapped,
so a subsequent write to its `background_color` would take the *immediate* path, not a transition.
That is harmless for the current design (an unmapped overlay has nothing to animate), but it
means **the transition is only observable while the overlay is visible** — i.e. only when
`brightness < 1.0`.

**PENDING, and not claimed as passing:** no live rendering has been observed. The transition
machinery is proven to *exist and be reachable*, but "the screen visibly dims smoothly" is a
visual claim that requires a human look at the screen with the slider below 1.0.

### 2.8 PASS — Test commands that actually work

Re-executed today:

| Command | Result |
|---|---|
| `make` | OK (up to date) |
| `./asus-control-center --check` | OK, 0 failures |
| `./asus-control-center --self-test` | OK, exit 0 |
| `make test` | **FAILS** — pre-existing: `launcher/launcher.gresource.xml` (plus `style.css`, `launcher/data/`) are absent, so `make -C launcher` stops with *"No rule to make target 'launcher.gresource.xml'"* |
| `gjs -m test-logic.js` (Soft Dimmer) | OK, 43/43 |

`AGENTS.md` §1 documents the `make test` breakage accurately and points at `--check` as the usable
gate. `AGENTS.md` §1 also correctly warns that there is **no unit-test framework** — the only
automated tests are the in-binary `unit_checks()` assertions in `src/main.cpp`.

### 2.9 PASS — No accidental Git / release assumptions

- The working copy is **not a Git repository** — root and every subdirectory. Verified by
  `git rev-parse --is-inside-work-tree` failing everywhere.
- `AGENTS.md` §1 states this explicitly and warns that `make dist` (`git archive`) and the
  release checklist's tag/push steps will fail.
- `AGENTS.md` §5 keeps the four-place version rule (`Makefile`, RPM spec, `debian/changelog`,
  metainfo `<releases>`) and the CI behaviour, while noting the release flow is "not applicable to
  this working copy (no Git)". No document instructs an agent to commit, push, tag or reset.
- No Git operation was performed during this audit.

### 2.10 PASS — No instructions authorizing unrelated changes

- `SOFT_DIMMER_GNOME51_REPAIR_PLAN.md` §5 marks ASUSCraft as out of scope: "Treat ASUSCraft v1.2.2
  as independent. Do not integrate with its bundled ASC extension. Do not invent a D-Bus server.
  Keep the external extension's existing public D-Bus contract."
- The plan's phases are scoped to `soft-dimmer@io.github.softdimmer` only, with `softdim.cpp`,
  `data/soft-brightness-plus/` and the ASC extension explicitly untouched.
- `AGENTS.md` §0 forbids using repo edits to "fix" Soft Dimmer.
- Phase 1 respected this: only `dimmer.js` and `test-logic.js` changed; `dbus.js`, `extension.js`,
  `indicator.js`, `metadata.json`, the schema and the D-Bus XML were byte-identical to the backup.
- `AGENTS.md` §3 forbids adding a session-bus service to `src/` — the repo's daemon is a
  system-bus client only (verified: no `g_bus_own_name` / `G_BUS_TYPE_SESSION` anywhere in `src/`).

---

## 3. Conflicts between documents

| # | Conflict | Severity | Detail |
|---|---|---|---|
| C-1 | **State of the world changed between documents** | Medium | `ASUSCRAFT_SOFT_DIMMER_DBUS_INVESTIGATION.md:343-344` and `SOFT_DIMMER_PHASE1_IMPLEMENTATION_REPORT.md:472` state the extension is `State: ERROR` and its bus name unowned. **Today it is `State: ACTIVE` and owns `io.github.softdimmer` (`GetNameOwner → ':1.16'`)** — the machine rebooted after Phase 1, so the Shell loaded the fixed code. The stale statements should be updated; the *conclusion* they support (the D-Bus defect is unverified/present) is **confirmed correct** — a live read-only `GetBrightness` call today returns `TypeError: params is not iterable`. |
| C-2 | **Phase 1 status** | Low | The plan presents Phase 1 as future work; it is implemented. The plan's acceptance line ("A6, A7, A10 pass statically; L2, L3, L4, L5, L6 pass live") is half-met: the static half is met, the live half is not (except that the extension now reaches `ACTIVE`, which is stronger evidence than the plan assumed). |
| C-3 | **Backup location** | **High** | Plan §7 and the Phase 1 report §7 both point at `/tmp/opencode/…` for rollback. `/tmp/opencode` is empty after the reboot. See §2.3. |
| C-4 | Animation evidence strength | Low | The Phase 1 report says the eased alpha change "animates smoothly", justified by the `cogl_color_progress` registration. That justification is **correct but was incomplete** — it did not establish that `background_color` gets an *implicit* transition at all. Upstream source shows `clutter_actor_set_background_color()` **does** call `_clutter_actor_create_transition()`, and that the transition is skipped when the actor is unmapped. The report's conclusion holds; the reasoning should cite `clutter-actor.c:16211-16214` and the unmapped caveat. |

No conflict was found between `AGENTS.md` and the three Soft Dimmer documents: the Soft
Brightness Plus / Soft Dimmer distinction, the extension paths, the schema ids, the D-Bus
service name and the GNOME 51 platform facts all agree across all four files.

---

## 4. Missing safety requirements

1. **Durable, verified backups outside `/tmp`** (blocking — §2.3). Requirements: keep them out of
   `/tmp`; include a `sha256sum` manifest; validate the backup *before* any destructive step;
   never `rm -rf` the extension before the restore source is confirmed present.
2. **A pre-flight backup step at the start of every future phase.** Phase 1 got this right
   (timestamped backup, then edit); Phases 2–7 have no such rule.
3. **Explicit transition cancellation on teardown.** `disable()` relies on `actor.destroy()` to
   drop in-flight transitions rather than calling `remove_all_transitions()`. Worth making
   explicit in Phase 5.
4. **A live-shell acceptance checklist that does not require changing settings.** With
   `brightness = 1.0` the overlay is fully transparent and nothing is observable; every
   L2–L6 check requires the user to move the slider or set a value — which must be stated as a
   *user-performed* step, not an agent-performed one.
5. **A note that `/tmp/opencode` is scratch space, not storage.** `AGENTS.md` §6 currently
   mentions `/tmp` only implicitly.

---

## 5. Can Phase 1 proceed?

**Phase 1 has already been implemented and is correct.** The remaining question is not "should
it proceed" but "is it verifiably done".

**Implemented and statically proven (PASS):** the opacity-scale defect is fixed; the alpha
channel carries the dim level; the conversion is single-sourced in `dimAlpha()`; the
`Clutter.Actor` → `St.Widget` change restores the legal `track_hover`/`can_focus`; the semantics
are preserved; the D-Bus, screenshot-detection and stacking scopes were respected;
43/43 automated checks pass; the extension now reaches **`State: ACTIVE`** in a live GNOME 51.1
session, owning its bus name with no errors in the journal.

**Still outstanding (PENDING, and correctly not claimed as done):** the *visual* result —
that the screen visibly dims by the requested amount, that the minimum-brightness floor holds,
and that the 250 ms transition looks smooth. These need a human at the screen with the slider
below 1.0; they cannot be established by any read-only probe, because the Shell exposes no
public API for the scene graph and the current setting is `1.0` (fully transparent overlay).

**Blocking before Phase 2 (the next implementation phase):** the backup gap (§2.3). Phase 2
rewrites `dbus.js`, which is the file carrying the extension's public contract; a rollback that
silently does nothing would be much worse here than for Phase 1.

---

## 6. Exact remaining blockers

| # | Blocker | Severity | Owner | How to clear it |
|---|---|---|---|---|
| **B-1** | **All rollback targets under `/tmp/opencode/` are gone**; §7.2's `rm -rf "$D"` before a verified restore would destroy the extension | **Blocking** | Any agent starting Phase 2 | Re-create a durable backup outside `/tmp` with a `sha256sum` manifest; rewrite §7 to restore non-destructively and validate the source first |
| **B-2** | Live visual verification of dimming (L2–L6) not performed | High (but **not** a code defect) | User, at the screen | Set brightness < 1.0 (e.g. 0.5) and confirm the screen dims; confirm no flicker while dragging the slider |
| **B-3** | Stale "State: ERROR / bus name unowned" statements in the ASUSCraft investigation (§4.2) and the Phase 1 report (§8) | Medium | Agent, next doc update | Update both to the current reality: `State: ACTIVE`, name owned, D-Bus defect reproduced live |
| **B-4** | `make test` unusable as a gate (missing `launcher/` assets) | Medium | Repo maintainer | Restore `launcher/launcher.gresource.xml`, `launcher/style.css`, `launcher/data/`; meanwhile use `--check` |
| **B-5** | Transitions not explicitly cancelled on teardown | Low | Phase 5 | Call `remove_all_transitions()` before destroying an overlay, or rely on destroy and document it |
| **B-6** | Phase 2 D-Bus defects still live (confirmed today: `GetBrightness` → `TypeError`, `GetCapabilities` → `TypeError`, `GetEnabled` → misleading `true`) | High | Phase 2 | Convert to positional-arg sync methods returning plain values; `a{sv}` via `invocation.return_value(new GLib.Variant('(a{sv})', [caps]))` |
| **B-7** | Phase 3 grab-op detection still wrong (overlay hides on window drags; capture not detected) | High | Phase 3 | Remove the grab-op wiring; scope capture handling to `Main.screenshotUI.screencast-in-progress`; document the platform limit |
| **B-8** | Phase 4 stacking still puts the overlay above the top panel | Medium | Phase 4 | Reorder with `Main.uiGroup.set_child_below_sibling(actor, Main.layoutManager.panelBox)` |

---

## 7. Special animation check — consolidated evidence

| Question | Answer | Evidence |
|---|---|---|
| Are `save_easing_state()` / `restore_easing_state()` used? | **Yes, correctly paired** | `dimmer.js:328` and `:334`, with the property write strictly between them |
| Are `set_easing_duration()` / `set_easing_mode()` used correctly? | **Yes** | `dimmer.js:329-330`; they set the current easing state, which `should_skip_implicit_transition()` reads — a non-zero duration is what makes the implicit transition happen |
| Is the target property animatable in Mutter 51? | **Yes** | `background-color` pspec carries `CLUTTER_PARAM_ANIMATABLE` (`clutter-actor.c:6878-6884`); `clutter_actor_set_background_color()` calls `_clutter_actor_create_transition()` (`:16211-16214`); `COGL_TYPE_COLOR` has `cogl_color_progress` → `cogl_color_interpolate` (`clutter-interval-progress.c:116-165`), which interpolates alpha linearly |
| How do rapid updates behave? | **Correctly — one retargeted transition, not a pile-up** | `clutter-actor.c:16917-16945`: an existing transition for the same property is reused, the timeline rewound, and the interval's initial/final values reset; `set_remove_on_complete(TRUE)` drops it when finished |
| Does `disable()` cancel pending animations? | **Not explicitly** | `_destroyAllOverlays()` does `remove_child()` + `destroy()`; no `remove_all_transitions()` call. Transitions live in the actor's `ClutterAnimationInfo` and die with the actor, but this is not an API guarantee |
| Caveat | **An unmapped actor skips the transition** | `should_skip_implicit_transition()` returns TRUE when `!clutter_actor_is_mapped()` (`clutter-actor.c:16779-16783`). With `brightness = 1.0` the overlay is hidden → unmapped → a write would apply immediately. Harmless today, but the transition is only *observable* while `brightness < 1.0` |
| Live proof? | **NO — not claimed** | No human observation of dimming or of the transition. The extension is `ACTIVE` and error-free, which proves the overlay is *constructed*, not that it renders or animates as intended |

---

## 8. Review method

Read-only throughout. Specifically: no file in the repository, in the extension directory, or in
`/usr/share/gnome-shell/extensions/` was edited; no extension was enabled, disabled or reloaded;
no GSettings value was changed (`brightness` is still `1.0`, `enabled` still `true`); GNOME Shell
was not restarted and no logout occurred; **no mutating D-Bus method was invoked** — the only
D-Bus calls were `ListNames`, `GetNameOwner`, `ListExtensions`, `Introspect` and the
*read-only* `GetEnabled` / `GetBrightness` / `GetCapabilities`; no Git operation was performed
(the directory is not a repository); the mutter `gnome-51` source was cloned read-only to
`/tmp/opencode/m51` for verification.

---

*Audit completed 2026-11-11. Verdict: the documents are consistent and the plan is sound; the
one **FAIL** is the vanished `/tmp` backups, which must be re-created durably before Phase 2
touches `dbus.js`. Phase 1 itself is implemented, statically verified, and — new since the
reboot — confirmed to load cleanly as `State: ACTIVE` in a live GNOME 51.1 session; only the
visual confirmation of dimming remains outstanding.*
