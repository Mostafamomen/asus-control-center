# Soft Dimmer — the MyASUS slider's runtime error, fixed

**Subject:** `soft-dimmer@io.github.softdimmer`
**Installed at:** `/home/mostafa/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer`
**Environment:** ASUS Vivobook Pro 15 N6506CU · Fedora 45 · GNOME Shell 51.1 · GJS 1.90 · Wayland
**Date:** 2026-11-11
**Reported error:** `Could not find definition of virtual function repaint` —
`myasus-slider.js`, line 107 / line 205

**Verdict: the defect is fixed and proven; the live session still has to load
the fixed file, so the visual checks stay PENDING.**

---

## 0. Summary

| # | Requirement | Verdict |
|---|---|---|
| 1 | The complete `myasus-slider.js` was inspected | **PASS** — §2 |
| 2 | The real GNOME 51.1 API was verified | **PASS** — §3 |
| 3 | The drawing uses a supported mechanism | **PASS** — §4 |
| 4 | The MyASUS appearance is preserved | **PASS** — §5 |
| 5 | Keyboard, drag, notifications and the mapping are preserved | **PASS** — §5 |
| 6 | `queue_repaint()` still triggers the drawing | **PASS** — §4.3 |
| 7 | Regression tests for the drawing integration | **PASS** — §6 |
| 8 | Every file backed up and the backup verified | **PASS** — §7 |
| 9 | `dbus.js`, the hardware brightness, the ASUS settings and the other extensions untouched | **PASS** — §8 |
| 10 | No other dimming engine enabled | **PASS** — §8 |
| 11 | No unsupported virtual-function error | **PASS (proven outside the Shell)** — §3.2 |
| 12 | The extension loads without entering ERROR | **PENDING** — the live Shell still holds the failed module (§9) |
| 13 | The slider draws correctly in Quick Settings | **PENDING** — §9 |
| 14 | Dragging and keyboard adjustment work | **PENDING** — §9 |
| 15 | No regression in the D-Bus or the brightness mapping | **PASS** — §6 |

**263 automated checks pass, 0 fail.**

---

## 1. What had happened

The extension was in **`State: ERROR`** in the live session:

```
$ gnome-extensions info soft-dimmer@io.github.softdimmer
  Enabled: Yes
  State: ERROR
$ gdbus call … GetNameOwner io.github.softdimmer
Error: org.freedesktop.DBus.Error.NameHasNoOwner
```

The cause was exactly as reported. `MyAsusTrack` extended `St.Widget` and
defined `vfunc_repaint()`. **`St.Widget` has no `repaint` virtual function**,
so GJS refused to register the class at all — and because the class is
registered at module scope, the *whole module* threw while it was being
evaluated, which is why the extension did not merely lose the slider but
failed to load entirely.

This is a **hard failure at class-registration time**, not a runtime warning:
it happens before any widget is created, so nothing could have caught it.

---

## 2. The inspection

`myasus-slider.js` was read in full (365 lines). The two relevant places:

```js
// line 107 — the class declaration
const MyAsusTrack = GObject.registerClass({
    Properties: { 'value': … },
    Signals: { 'drag-begin': {}, 'drag-end': {} },
}, class MyAsusTrack extends St.Widget {      // <-- the wrong parent

// line 205 — the drawing
    vfunc_repaint() {                          // <-- a virtual function St.Widget
        const cr = this.get_context();         //    does not have
        const [width, height] = this.get_surface_size();
        …
```

Everything else in the file was sound: the Cairo drawing calls, the gradient,
the handle, the ticks, the pan gesture, the keyboard handling and the value
property.

---

## 3. The GNOME 51.1 API, verified rather than assumed

### 3.1 What St actually offers

```
$ gjs -m …
St.DrawingArea gtype = [GType for 'StDrawingArea']
the "repaint" signal id = 55          <- St.DrawingArea HAS a repaint signal
the "repaint" signal id = 0           <- St.Widget has NO repaint signal
the "key-press-event" signal id = 31  <- Clutter.Actor
St.DrawingArea has queue_repaint: function
St.DrawingArea has get_context:   function
```

`St.DrawingArea` has a **signal** named `repaint`; `St.Widget` has neither a
signal nor a virtual function by that name.

### 3.2 The decisive experiment

GJS resolves a `vfunc_*` against the **parent class**, and refuses the class
if the parent has no such virtual function. That was reproduced directly:

```js
// a throw-away class, registered outside GNOME Shell
GObject.registerClass(class Test extends St.Widget {
    vfunc_repaint() { … }
});
// → Could not find definition of virtual function repaint

GObject.registerClass(class Test extends St.DrawingArea {
    vfunc_repaint() { … }
});
// → ACCEPTED
```

So the error was caused by the **parent class**, and `St.DrawingArea` is the
class that supports drawing.

### 3.3 How the Shell itself draws

The installed GNOME Shell 51.1 shows both patterns:

```js
// ui/barLevel.js:21  and  ui/padOsd.js:306  — the virtual function
class BarLevel extends St.DrawingArea {
    vfunc_repaint() {
        const cr = this.get_context();
        …
        cr.$dispose();
    }
}

// ui/boxpointer.js:45, ui/altTab.js:885, ui/switcherPopup.js:436 — the signal
this._border = new St.DrawingArea();
this._border.connect('repaint', this._drawBorder.bind(this));
```

Both are supported on `St.DrawingArea`. The task asks for the signal form, and
that is what was implemented — it also keeps the drawing code a plain method,
with no virtual-function resolution involved at all.

---

## 4. The fix

### 4.1 The class

```diff
-}, class MyAsusTrack extends St.Widget {
+}, class MyAsusTrack extends St.DrawingArea {
```

### 4.2 The drawing

```diff
+        // The drawing happens in the "repaint" handler, which St.DrawingArea
+        // emits whenever the widget needs to be redrawn.  There is no
+        // vfunc_repaint() here on purpose: St.Widget has no such virtual
+        // function, and GJS refuses to register a class that defines one.
+        this.connect('repaint', () => this._draw());
```

```diff
-    vfunc_repaint() {
+    _draw() {
         const cr = this.get_context();
         const [width, height] = this.get_surface_size();
         …
         cr.$dispose();
     }
```

The context is taken with `this.get_context()` and given back with
`cr.$dispose()` — exactly what the Shell's own drawing code does.

### 4.3 The redraw path

`St.DrawingArea` is the widget whose whole purpose is to be redrawn on
demand, and `queue_repaint()` is its API for asking for one:

```js
this.connect('notify::value', () => this.queue_repaint());
```

`queue_repaint()` → St emits `repaint` → the handler draws. The chain is
St's own, so it is correct by construction.

### 4.4 The proof that the error is gone

The failure happened while the module was being **evaluated**, so the module
can be imported in a plain `gjs` process — no Shell, no Clutter context — and
the class registration is exercised:

```
$ gjs -m importtest.js …/myasus-slider.js
PASS: myasus-slider.js registers without error
  exports: MyAsusSlider
```

The same import fails on the pre-fix file with exactly the reported message —
that is the regression test in §6.

---

## 5. What was preserved

Nothing about the MyASUS appearance or the behaviour was touched:

| Preserved | How |
|---|---|
| the cyan → ASUS blue gradient | the drawing code is byte-identical; only the method it lives in changed |
| the grey ring and the blue centre | unchanged |
| the ten ticks and the 0/50/100 scale | unchanged |
| the sizing and the alignment | unchanged — the CSS `height`, the handle radius and the label boxes are the same |
| the drag gesture | unchanged — the same `Clutter.PanGesture` and `global.stage.grab()` |
| the keyboard | unchanged — `vfunc_key_press_event` (a `Clutter.Actor` virtual function, verified as accepted on `St.DrawingArea`) |
| the value notifications | unchanged — the `value` property, `notify::value` and the `drag-begin`/`drag-end` signals |
| the brightness mapping | unchanged — `qsrow.js` was not touched at all |

The only difference in the whole file is the parent class and the name of the
drawing method.

---

## 6. Tests

### 6.1 The regression test that reproduces the bug

The new test imports the module in a plain `gjs` process, which is exactly
where the error was raised:

```js
try {
    module = await import('./myasus-slider.js');
} catch (e) {
    error = e;
}
check('myasus-slider.js registers without a virtual-function error',
    error === null, error ? error.message : '');
```

On the pre-fix file this fails with *"Could not find definition of virtual
function repaint"*; on the fixed file it passes.

### 6.2 The static guards

* the track is an `St.DrawingArea` — the Shell's own drawing widget;
* the track does **not** define an unsupported `vfunc_repaint()` (the comment
  in the file mentions the name, so only the code is examined);
* the drawing happens in the `repaint` signal handler;
* the Cairo context is taken with `get_context()` and given back with
  `cr.$dispose()`;
* a value change asks for a redraw with `queue_repaint()`.

### 6.3 The results

| Suite | Checks | Passed | Failed |
|---|---|---|---|
| `test-logic.js` | 165 | **165** | 0 |
| the Phase 2 D-Bus suite (regression) | 98 | **98** | 0 |
| **Total** | **263** | **263** | **0** |

```sh
export GI_TYPELIB_PATH=/usr/lib64/mutter-51:/usr/lib64/gnome-shell
export LD_LIBRARY_PATH=/usr/lib64/mutter-51:/usr/lib64/gnome-shell
cd ~/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer
gjs -m test-logic.js          # → 165/165 checks passed (exit 0)
```

The D-Bus suite still passes unchanged, so the Phase 2 contract and the
brightness mapping are untouched.

---

## 7. The backup

| Item | Value |
|---|---|
| Backup root | `/home/mostafa/.local/share/soft-dimmer/backups/20261111T093416Z/` |
| Backup tree | `.../20261111T093416Z/extension/` (11 files) |
| Manifest | `.../20261111T093416Z/MANIFEST.sha256` (11 entries) |
| Location | outside `/tmp`, outside the extension directory, **same filesystem** (`st_dev 55`) |

| Check | Result |
|---|---|
| `diff -r --no-dereference` source vs. backup | **PASS** — identical |
| `sha256sum -c MANIFEST.sha256` | **PASS** — 11 × `OK`, exit 0 |
| Same filesystem | **PASS** — 55 = 55 |
| Restoration into a disposable directory | **PASS** |
| The disposable directory was removed | **PASS** |

The backup was taken **before** the edit, and it still verifies afterwards.

---

## 8. What was not touched

| Item | State |
|---|---|
| `dbus.js` | unchanged — `f05c167a06b577d4007aef3ed46eb25d83b6e254c98fa85508655f2fbe384669` |
| `dimmer.js`, `extension.js`, `indicator.js`, `qsrow.js` | unchanged |
| The introspection XML, the schema, `metadata.json` | unchanged |
| The hardware brightness | not written |
| The ASUS Flicker-Free Dimming settings | not written |
| The other extensions | not modified; Soft Dimmer Pro and the ASC extension were not touched |
| Another dimming engine | not enabled — Soft Dimmer Pro is enabled by the user's own configuration and is in `ERROR` of its own accord; it was left alone |

Only two files changed:

| File | Before | After |
|---|---|---|
| `myasus-slider.js` | `4c6ef44d30e325d7f7dc6c309172be37c2ab3163375db7738e945fe2091a025a` | `a1404f1ccb52b35dae055f9face6f215b06f0f60b6a199752f9a7ba8da363fdf` |
| `test-logic.js` | `c33251942ca22252a3ebe699fe9d61f2d5828b6d63fa213254e7c095991bac41` | `6be205e6f74180ae1c961241b7323a1e9c902287cb6feb5fb7793c4ecbd61bfd` |

---

## 9. The live session — and why it is still in ERROR

The running Shell (PID 112203, started **11:58:57**) is the process that
imported the broken module. GJS caches a module by URL, and a module that
threw during evaluation stays failed in that process, so **the fixed file
cannot be loaded by that Shell**.

This was tested, not assumed:

```
$ gnome-extensions disable soft-dimmer@io.github.softdimmer
$ gnome-extensions enable  soft-dimmer@io.github.softdimmer
$ gnome-extensions info soft-dimmer@io.github.softdimmer | grep State
  State: ERROR
```

A disable/enable cycle inside the same process does **not** help, exactly as
expected. (The extension was in `ERROR` before this test, so the cycle could
not make anything worse.)

**The fix needs a new Shell process: a logout and a login.** Then:

```sh
gnome-extensions info soft-dimmer@io.github.softdimmer | grep -E 'State|Enabled'
#   expect: State: ACTIVE, Enabled: Yes
journalctl --user --since "-5m" | grep -i soft-dimmer
#   expect: no errors at all
```

### 9.1 The remaining live verification steps

| # | Check | How |
|---|---|---|
| 1 | the extension loads | `gnome-extensions info` → `State: ACTIVE` |
| 2 | no virtual-function error | `journalctl --user --since "-5m" \| grep -i "repaint\|virtual function"` → no output |
| 3 | the slider draws | open the Quick Settings menu; the MyASUS block must be directly below the native brightness slider, with the cyan→blue gradient, the ringed handle and the 0/50/100 scale |
| 4 | the drag works | drag the handle; the dimming must follow |
| 5 | the keyboard works | focus the slider and press ←/→/Home/End |
| 6 | the D-Bus interface still works | `gdbus call … GetBrightness 0`, `GetEnabled`, `GetCapabilities` |
| 7 | the mapping is right | at level 0 the screen is undimmed, at level 100 it is as dark as `minimum-brightness` allows |
| 8 | the overlay covers the panel | the top bar is as dark as the desktop |
| 9 | the popups stay readable | the Quick Settings menu is not dimmed |
| 10 | no duplicates | disable and enable the extension; exactly one block |

The "after" screenshot is produced with the capture tool from the previous
phase:

```sh
python3 ~/.local/share/soft-dimmer/tests/qs-capture.py /tmp/after 10
python3 ~/.local/share/soft-dimmer/tests/qs-frames.py /tmp/after.mp4 \
        ~/.local/share/soft-dimmer/live/after-quick-settings.png
```

---

## 10. Rollback

The hardened procedure from `SOFT_DIMMER_BACKUP_ROLLBACK_HARDENING_REPORT.md`
§8, with the new backup. **It was not executed.**

```sh
B=/home/mostafa/.local/share/soft-dimmer/backups/20261111T093416Z
D=/home/mostafa/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer
P="$(dirname "$D")"
STAGE=/home/mostafa/.local/share/soft-dimmer/staging

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

OLD="$P/.soft-dimmer.previous.$(date -u +%Y%m%dT%H%M%SZ)"
test -d "$S/extension" || { echo "ABORT: no verified staging directory"; exit 1; }
mv "$D" "$OLD"
if ! mv "$S/extension" "$D"; then
  mv "$OLD" "$D"; echo "ABORT: swap failed, original restored"; exit 1
fi
rm -rf "$S"
echo "OK: restored. Previous installation preserved at $OLD"

cd "$D" && find . -type f -print0 | sort -z | xargs -0 sha256sum \
  | sed 's|\./|extension/|' > /tmp/post.sha
diff <(sort "$B/MANIFEST.sha256") <(sort /tmp/post.sha) \
  && echo "OK: installed tree matches the backup"
# rm -rf "$OLD"   <- only after the verification above passed
```

---

## 11. Verdict

**The defect is fixed.**

* `MyAsusTrack` now extends **`St.DrawingArea`**, the Shell's own drawing
  widget, and the drawing happens in the **`repaint` signal handler** — the
  pattern the Shell itself uses in `ui/boxpointer.js` and `ui/altTab.js`.
* The unsupported `vfunc_repaint()` is gone; the class registers cleanly,
  which was **proven by importing the module outside GNOME Shell** — the very
  step that used to fail with *"Could not find definition of virtual function
  repaint"*.
* The MyASUS appearance, the gradient, the ringed handle, the ticks, the
  scale, the sizing, the drag, the keyboard, the notifications and the
  brightness mapping are all unchanged.
* `dbus.js` and the brightness mapping are untouched; the D-Bus suite still
  passes **98/98**, the logic suite **165/165**.
* A verified backup of every file was taken before the edit.

**What is still pending:** the live Shell still holds the failed module, so
the extension stays in `ERROR` until a **logout/login** loads the fixed file;
a disable/enable cycle in the same process does not help, and that was
verified rather than assumed. The visual checks — the block's appearance, the
drag, the keyboard, the panel dimming and the "after" screenshot — are
therefore **PENDING**, and are **not** claimed as passed.

**No further phase was started.**
