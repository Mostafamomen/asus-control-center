# Soft Dimmer removal — final report

**Project:** ASUSCraft 1.2.2 (`/home/mostafa/Documents/asus-control-center-1.2.2`)
**Date:** 2026-11-11
**Scope:** the complete removal of the Soft Dimmer *software* OLED dimming feature from ASUSCraft
**Preceded by:** `SOFT_DIMMER_REMOVAL_AUDIT.md` (Phase 1, approved)

---

## 1. Executive summary

The Soft Dimmer software dimming feature is **completely removed from
ASUSCraft**. The overlay engine, its GNOME Shell extension, the
`io.github.softdimmer` D-Bus client, the MyASUS-style slider widget and the
whole Display Center section that showed them are gone. Nothing of the
feature is left in the source, in the build, in the packages or in the
installer's path.

What was **kept** is everything that was never part of it: the ASUS hardware
brightness, the *Soft Brightness Plus* extension that dims the OLED **below
the panel's lowest backlight level** (the hardware "flicker-free" feature),
Night Light, the resolution and refresh-rate rows, Eye-care and E-Reading.

The distinction was the whole difficulty of this task: **three different
things had almost the same name**, and only one of them was the discontinued
feature. `src/control/softdim.{h,cpp}` — whose name suggests Soft Dimmer —
installs *Soft Brightness Plus* (`SBP_UUID`, `SBP_SCHEMA`, `SBP_XML`), and it
stays.

| | |
|---|---|
| Files deleted | **18** |
| Files modified | **8** |
| Build | clean, 0 errors, 4 pre-existing warnings in untouched files |
| `--check` | **0 failures** (17 assertions, down from 31) |
| `--self-test` | every display row still OK |
| Source audit | **0** remaining references to the feature |
| Runtime | the app runs clean; the Display Center is intact and the removed block is gone |
| RPM | **built** and contains no Soft Dimmer file |
| DEB | **NOT TESTED** — the Debian build dependencies are not installable on this Fedora host |

---

## 2. Final removal verdict

**PASS.** The feature is no longer contained, built, shipped or installed by
ASUSCraft. The Display Center keeps every unrelated display function.

---

## 3. The deleted files (18)

### 3.1 The Soft Dimmer backend and its D-Bus client (4)

| File | SHA-256 (first 12) | What it was |
|---|---|---|
| `src/control/oledsoftdim.h` | `570718bedb2e` | the D-Bus client for `io.github.softdimmer` |
| `src/control/oledsoftdim.cpp` | `f99f42bf18f6` | the same: the calls, the level ↔ brightness mapping, the error reporting |
| `src/ui/oledslider.h` | `0206765f7fa6` | the MyASUS gradient slider widget |
| `src/ui/oledslider.cpp` | `6d5798580650` | the same: the cairo gradient, the ringed handle, the drag and the keyboard |

### 3.2 The bundled GNOME extension (10)

| File | SHA-256 (first 12) | What it was |
|---|---|---|
| `data/soft-dimmer/dimmer.js` | `025a56770544` | **the software overlay engine** |
| `data/soft-dimmer/dbus.js` | `f05c167a06b5` | the `io.github.softdimmer` D-Bus service |
| `data/soft-dimmer/dbus/io.github.softdimmer.BrightnessControl.xml` | `1dc948b5422c` | its introspection XML |
| `data/soft-dimmer/extension.js` | `3f5f1cc55940` | the extension's entry point |
| `data/soft-dimmer/metadata.json` | `20bc496ba37b` | the extension's identity |
| `data/soft-dimmer/schemas/org.gnome.shell.extensions.soft-dimmer.gschema.xml` | `758e601e2375` | the extension's settings schema |
| `data/soft-dimmer/test-logic.js` | `a5cd115d80ac` | the extension's own test suite |
| `data/soft-dimmer/ASUSCRAFT.md` | `12e713013a0d` | the ASUSCraft modification record |
| `data/soft-dimmer/LICENSE` | `05f90eb47b2c` | the upstream GPL-2.0-or-later licence |
| `data/soft-dimmer/README.md` | `da6601b9f77d` | the upstream readme |

The whole `data/soft-dimmer/` directory is gone. `data/soft-brightness-plus/`
is untouched.

### 3.3 The ASUSCraft-side documents (4)

| File | SHA-256 (first 12) |
|---|---|
| `ASUSCRAFT_SOFT_DIMMER_DBUS_INVESTIGATION.md` | `fb296c785a8f` |
| `SOFT_DIMMER_MIGRATION_REPORT.md` | `1b7b112c6bc8` |
| `SOFT_DIMMER_QS_OVERLAY_REFINEMENT_REPORT.md` | `a52235062d0d` |
| `SOFT_DIMMER_GNOME51_MYASUS_REPORT.md` | `4dfde55815ea` |

**Nine further documents are kept**: the *extension project's* own audit trail
(`SOFT_DIMMER_GNOME51_FINAL_AUDIT.md`, `…REPAIR_PLAN.md`,
`…PHASE2_DBUS_REPORT.md`, `…PHASE2_LIVE_VERIFICATION.md`,
`…PHASE3_REPORT.md`, `…SLIDER_ERROR_FIX_REPORT.md`,
`SOFT_DIMMER_PHASE1_IMPLEMENTATION_REPORT.md`,
`SOFT_DIMMER_PREIMPLEMENTATION_CONSISTENCY_AUDIT.md`,
`SOFT_DIMMER_BACKUP_ROLLBACK_HARDENING_REPORT.md`). They are the history of a
different project that happens to live in this repository; the audit's
recommendation was to keep them, and that was followed. **Say the word and
they go too** — they are in the backup.

---

## 4. The modified files (8)

| File | Before → After | What changed |
|---|---|---|
| `src/ui/pages.cpp` | `78051c01bc34` → `f70bddd6575e` | the whole "OLED dimming section" (the MyASUS block, its three callbacks, the sync helper), the two includes, the widget handles, the insertion into the card and the refresh hook |
| `src/i18n/i18n.h` | `d2c07bc5c95c` → `58ea04dbeccc` | the 13 `ACC_T_OLEDDIM_*` tokens |
| `src/i18n/i18n.cpp` | `2f40e605ea71` → `e2a697a19504` | the 13 English and the 13 Arabic strings |
| `src/main.cpp` | `e444add79d0f` → `e605dc9d4886` | the include of `control/oledsoftdim.h` and the 14 unit checks for the level ↔ brightness mapping |
| `src/ui/theme.css` | `bd4187c86f7d` → `54b16f5a4d79` | the 6 `oled*` classes (12 rules in two blocks) |
| `src/ui/theme-dark.css` | `22a800a336db` → `7760c688c9f9` | the same 6 classes in the dark skin |
| `Makefile` | `fa12458b4085` → `ded0845bd605` | the two `SRCS` entries and the `data/soft-dimmer` install line |
| `AGENTS.md` | `40c404d323f3` → `0910456f16b9` | the "soft dim" section now describes only the hardware feature and records that the software feature was discontinued on 2026-11-11 |

**Nothing else was touched.** No theme, no navigation, no dashboard layout,
no branding, no version number, no unrelated UI.

---

## 5. The preserved files

| File | Why it stays |
|---|---|
| `src/control/softdim.{h,cpp}` | it installs **Soft Brightness Plus** (`SBP_UUID = "soft-brightness-plus@joelkitching.com"`), the hardware OLED dimming |
| `src/control/brightness.{h,cpp}` | the SBP brightness — the hardware dimming below the panel minimum |
| `src/control/backlight.{h,cpp}` | the ASUS hardware brightness |
| `src/control/nightlight.{h,cpp}` | Night Light |
| `src/control/asusd.{h,cpp}`, `gfx`, `muxreq`, `ctl`, `touchpadctl` | unrelated hardware controls |
| `src/ui/specs.cpp` | it prints the SBP status row (`ACC_T_SOFTDIM`) |
| `src/i18n/*` (`ACC_T_SOFTDIM`, `ACC_T_SOFTDIM_SUB`, `ACC_T_SOFTDIM_NEED`, `ACC_T_OLED_EYE`, `ACC_T_OLED_HINT`, …) | the strings of the controls that stay |
| `src/ui/theme*.css` (`.oledprev`, `.oledbadge`) | the home page's OLED **preview** widget — unrelated to Soft Dimmer |
| `data/soft-brightness-plus/` | the bundled Soft Brightness Plus extension |
| `data/acc-helper`, the polkit policy, `install.sh` | the helper, the policy and the Linux installer — none of them installs an extension |
| `craft-launcher-main/` | the Windows installer — it never touches a GNOME extension |

---

## 6. The architecture before and after

### Before

```
ASUSCraft
 ├─ src/ui/pages.cpp ── the Display (OLED) card
 │    ├─ the header (icon, title, resolution & refresh)
 │    ├─ the Backlight slider            → acc_backlight_set()      [hardware]
 │    ├─ the Software dimming slider     → acc_brightness_set()     [Soft Brightness Plus]
 │    ├─ the MyASUS "OLED Flicker-Free Dimming" block
 │    │    ├─ src/ui/oledslider.cpp   the gradient slider widget
 │    │    └─ src/control/oledsoftdim.cpp
 │    │         └─ D-Bus → io.github.softdimmer  [the software overlay]
 │    ├─ Eye-care mode
 │    └─ E-Reading (grayscale)
 └─ data/soft-dimmer/   the bundled extension (overlay engine + D-Bus service)
```

### After

```
ASUSCraft
 ├─ src/ui/pages.cpp ── the Display (OLED) card
 │    ├─ the header (icon, title, resolution & refresh)
 │    ├─ the Backlight slider            → acc_backlight_set()      [hardware]
 │    ├─ the Software dimming slider     → acc_brightness_set()     [Soft Brightness Plus]
 │    ├─ Eye-care mode
 │    └─ E-Reading (grayscale)
 └─ data/soft-brightness-plus/   the bundled extension (the hardware OLED dimming)
```

The Display Center keeps its shape: the removed block was one self-contained
entry in the page's vertical list, so nothing had to be re-laid-out and **no
empty card, blank space or placeholder is left behind**.

---

## 7. The backup and its verification

| Item | Value |
|---|---|
| Location | `/home/mostafa/.local/share/soft-dimmer/backups/20261111T103310Z-asuscraft-softdimmer-removal/` |
| Contents | `files/` — the 26 files that were modified or deleted, **with their original directory structure** |
| Manifest | `MANIFEST.sha256` — 26 entries |
| Timestamp | `20261111T103310Z` (UTC) |

| Check | Result |
|---|---|
| The manifest verifies | **PASS** — 26 × `OK`, exit 0 |
| The backup is outside the project | **PASS** — `~/.local/share/soft-dimmer/backups/` |
| The backup is outside `/tmp` | **PASS** |
| The backup is on the same filesystem | **PASS** — `st_dev 55`, so the restore is a rename |
| The restored tree hashes identically | **PASS** — 26 files identical |
| The restored files match the project byte for byte | **PASS** — 26 identical, 0 different |
| The disposable test directory was removed | **PASS** |
| The backup is intact after the test | **PASS** |
| The earlier backups were deleted or overwritten | **none** — all eight earlier backups still verify |

---

## 8. Build results

```
$ make clean && make -j"$(nproc)"
  errors: 0
  warnings: 4   (src/ui/home.cpp ×2 and src/ui/pcmanager.cpp ×2 — all pre-existing,
                  in files this task never touched)
  the binary: 1 589 864 bytes
```

**PASS.**

---

## 9. Unit test results (`--check`)

```
$ ./asus-control-center --check
  ...
  PASS  whitespace variants are recognised
  PASS  NULL text is safe
  Integrated mode is never offered unverified
  PASS  an unverified dgpu_disable attribute is not used

  0 failure(s)
```

| | Before | After |
|---|---|---|
| `expect()` assertions in `src/main.cpp` | 31 | **17** |
| failures | 0 | **0** |

The 14 that went are exactly the Soft Dimmer level ↔ brightness mapping
checks; the other 17 are untouched and still pass.

**PASS.**

---

## 10. Self-test results

```
$ ./asus-control-center --self-test
  OK   Backlight                  current 59%, set through logind
  OK   Software dimming           current 46%, min 10%
  OK   Night light / Night color  backend available
  OK   Polkit helper (writes)     /usr/libexec/asus-control-center/acc-helper installed
  ...
  0 failures
```

The "Software dimming" row is **Soft Brightness Plus**, the hardware OLED
dimming, and it is still there and still readable.

**PASS.**

---

## 11. Runtime verification

The application was started and driven to the Display Center over the
accessibility bus, and the page's contents were read back:

| Check | Result |
|---|---|
| the application starts | **PASS** — no errors, no warnings of its own |
| the Display Center opens | **PASS** |
| the page shows "Display (OLED)" | **PASS** |
| the hardware "Backlight" slider is there | **PASS** (58 %) |
| the Soft Brightness Plus "Software dimming" slider is there | **PASS** (46 %) |
| "Resolution & refresh" is there | **PASS** — 2880 × 1620 @ 120 Hz |
| "Eye-care mode" is there | **PASS** |
| "E-Reading (grayscale)" is there | **PASS** |
| the removed "OLED Flicker-Free Dimming" block is **gone** | **PASS** |
| the removed "Drag to adjust the dimming level" caption is **gone** | **PASS** |
| the removed "Notice" block is **gone** | **PASS** |
| the journal shows no new errors | **PASS** |

**PASS.**

---

## 12. Packaging results

| Package | Status | Detail |
|---|---|---|
| **RPM** | **PASS** | built with `rpmbuild -ba --nodeps --nocheck`; the `%check` step was skipped because `appstream-util` (from `libappstream-glib`) is not installable here without root — an **environment** gap, not a code problem. The package contains **no** Soft Dimmer file and still contains the whole Soft Brightness Plus bundle. |
| **DEB** | **NOT TESTED** | the Debian build dependencies (`debhelper-compat (= 13)`, `pkgconf`, `libgtk-4-dev`, `libadwaita-1-dev (>= 1.5)`, `desktop-file-utils`, `appstream`) are Debian/Ubuntu packages and cannot be installed on this Fedora host. |
| **AppImage** | **NOT APPLICABLE** | there is no AppImage target in this project: no recipe, no `appimagetool` call, no CI job. The Windows launcher (`craft-launcher-main/`) is a different artefact and was not touched. |
| `make install DESTDIR=…` | **PASS** | the staged tree contains no `soft-dimmer` and still contains `soft-brightness-plus` |
| `make uninstall` | **PASS** (by inspection) | it removes `$(DATADIR)/asus-control-center/` wholesale, so the bundle leaves with it — no change was needed |

---

## 13. The source audit

```sh
$ grep -RniE 'soft-dimmer|softdimmer|SoftDimmer|Soft Dimmer|OLEDSoftDim' \
    src include data tests resources scripts packaging Makefile
  (no output — the directories that do not exist are simply skipped)

$ grep -rniE "dimmer" src/ data/ Makefile
  (no output)
```

**PASS — zero remaining references in the executable code, the build files
and the resources.**

The identifiers that legitimately remain:

| Identifier | Where | Why it is correct |
|---|---|---|
| `softdim` (as `ACC_SOFTDIM*` and `acc_softdim_*`) | `src/control/softdim.{h,cpp}`, `src/ui/specs.cpp`, `src/ui/pages.cpp` | **Soft Brightness Plus** — the hardware feature. The name is a historical misnomer and was deliberately left alone (renaming it would be unrelated refactoring). |
| `soft-dimmer@io.github.softdimmer` | `AGENTS.md` only | the note that the feature was discontinued and that a separately installed extension is not ours |
| `org.gnome.shell.extensions.soft-dimmer` | nowhere in the project | the schema lived in the bundle, never in ASUSCraft |
| `soft-dimmer-pro`, `soft-dimmer-asc` | `AGENTS.md` only | independent extensions, not ours |

---

## 14. Known limitations

1. **The separately installed Soft Dimmer extension is still on the machine**
   (`~/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer/`,
   enabled and ACTIVE, owning `io.github.softdimmer`). It is an
   **independent installation** that this project neither ships nor talks to
   any more, and the task forbids removing independently installed extensions,
   so it was left untouched. The practical consequence: the screen can still
   be dimmed by that overlay, independently of ASUSCraft.
2. **The DEB was not built** (Debian build dependencies unavailable on this
   host). The DEB's file list is produced from the same `data/` directory as
   the RPM's, and the staged install was verified ✓, but the package itself
   is **NOT TESTED**.
3. **The RPM's `%check` step was skipped** (`appstream-util` unavailable). The
   `desktop-file-validate` step before it passed ✓.
4. **No screenshot of the Display Center** is in this report: the ASUSCraft
   window could not be brought to the front of the session from a script
   (Wayland forbids synthetic window placement), and the screen capture
   reached whatever was on top instead. The runtime verification was done
   through the accessibility tree instead, which reads the *live* widgets and
   is more precise than a picture.
5. **The extension project's nine documents remain** in the repository root.
   They are the history of a different project; the audit recommended
   keeping them.

---

## 15. The rollback

Everything is reversible from the verified backup. The exact commands:

```sh
B=/home/mostafa/.local/share/soft-dimmer/backups/20261111T103310Z-asuscraft-softdimmer-removal
P=/home/mostafa/Documents/asus-control-center-1.2.2

# 1. verify the backup before touching anything
( cd "$B/files" && sha256sum -c ../MANIFEST.sha256 ) || { echo "ABORT: the backup is broken"; exit 1; }

# 2. restore the modified and deleted files, with their directory structure
( cd "$B/files" && find . -type f -print0 | sort -z \
    | xargs -0 -I{} cp -a --parents {} "$P" ) || { echo "ABORT"; exit 1; }

# 3. remove the files that the removal added and the backup does not have
#    (there are none: the removal only deleted files, it added none)

# 4. verify
( cd "$B/files" && find . -type f -print0 | sort -z | xargs -0 sha256sum ) | sort -k2 > /tmp/expect.sha
( cd "$P" && find Makefile AGENTS.md src data -type f -print0 | sort -z | xargs -0 sha256sum ) | sort -k2 > /tmp/actual.sha
diff /tmp/expect.sha /tmp/actual.sha

# 5. rebuild and re-test
cd "$P" && make clean && make -j"$(nproc)" && ./asus-control-center --check
```

Step 2 restores the eight modified files and brings the eighteen deleted ones
back; step 4 proves the tree matches the backup exactly.

To restore only the *extension* side, the earlier backup
`~/.local/share/soft-dimmer/backups/20261111T102150Z-phase2-qs-removal/` holds
the extension as it was before the Quick Settings removal.

---

## 16. Acceptance criteria

| # | Criterion | Status |
|---|---|---|
| 1 | the Soft Dimmer backend is removed from ASUSCraft | **PASS** |
| 2 | the Soft Dimmer D-Bus integration is removed | **PASS** |
| 3 | the Soft Dimmer GTK interface is removed | **PASS** |
| 4 | the bundled extension is removed | **PASS** |
| 5 | the automatic installation logic is removed | **PASS** — no installation logic for it existed; the bundle and its install line are gone |
| 6 | obsolete settings are removed safely | **PASS** — the feature had no ASUSCraft settings; the extension's own schema left with the bundle; no user configuration was touched |
| 7 | the build references are cleaned | **PASS** |
| 8 | the documentation is updated | **PASS** — `AGENTS.md` rewritten; the four ASUSCraft-side documents removed |
| 9 | no broken references remain | **PASS** — the source audit is empty |
| 10 | ASUSCraft builds successfully | **PASS** |
| 11 | `--check` passes | **PASS** — 0 failures |
| 12 | `--self-test` passes | **PASS** |
| 13 | the Display Center still works | **PASS** — verified over the accessibility tree |
| 14 | the hardware brightness functionality is preserved | **PASS** — the Backlight slider and the SBP slider are both there and readable |
| 15 | the backup and rollback are verified | **PASS** — 26 files, manifest, restoration test, exact commands |
| 16 | the final report is written | **PASS** — this file |

---

**Removal complete. No unrelated work was done, no release was published, no
commit was pushed, no tag was created, and no independently installed
extension was touched.**
