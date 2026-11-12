# Soft Dimmer removal — Phase 1 audit

**Project:** ASUSCraft 1.2.2 (`/home/mostafa/Documents/asus-control-center-1.2.2`)
**Phase:** 1 of 2 — **audit only, no file was modified**
**Date:** 2026-11-11
**Objective:** the Soft Dimmer *software* OLED dimming feature is discontinued and is to be removed from ASUSCraft completely — not migrated, not replaced, not re-implemented.

---

## 0. Executive summary

The Soft Dimmer feature touches **19 files** in the project: 2 new C++ modules
(the D-Bus client and the MyASUS slider widget), 1 bundled GNOME extension
(10 files), 5 edited files (the Display Center page, the two i18n tables, the
unit checks and the Makefile), 2 CSS files and 13 feature-specific documents.

**The single most important finding of this audit is a naming collision.**
Three different things in this project and on this machine have almost the
same name, and only one of them is being removed:

| Name | What it really is | Fate |
|---|---|---|
| **Soft Dimmer** (`soft-dimmer@io.github.softdimmer`) | the **software overlay** — a translucent rectangle painted over the screen by a GNOME Shell extension | **REMOVE** (from the project) |
| **Soft Brightness Plus** (`soft-brightness-plus@joelkitching.com`) | **hardware** OLED dimming *below* the panel's lowest backlight level, driven through the display's D-Bus | **KEEP** — this is the "ASUS OLED hardware settings" the task protects |
| **Soft Dimmer Pro** (`soft-dimmer-pro@io.github.softdimmer`) | an independently installed extension, not ours | **KEEP** — do not touch |

And a fourth trap: **the ASUSCraft module `src/control/softdim.{h,cpp}` has
nothing to do with Soft Dimmer.** Its header says *"install/enable the bundled
Soft Brightness Plus"*. The name is a historical misnomer. **It stays.**

---

## 1. Complete file inventory

### 1.1 Files to be removed entirely

| File | Kind | SHA-256 (first 16) | Note |
|---|---|---|---|
| `src/control/oledsoftdim.h` | new module | `570718bedb2ec0cc` | the D-Bus client for `io.github.softdimmer` |
| `src/control/oledsoftdim.cpp` | new module | `f99f42bf18f61e3b` | the same |
| `src/ui/oledslider.h` | new module | `0206765f7fa6b3dd` | the MyASUS gradient slider widget |
| `src/ui/oledslider.cpp` | new module | `6d5798580650c985` | the same |
| `data/soft-dimmer/ASUSCRAFT.md` | bundle doc | `12e713013a0d9ae3` | the ASUSCraft modification record |
| `data/soft-dimmer/LICENSE` | bundle doc | `05f90eb47b2c2684` | the upstream GPL-2.0-or-later licence |
| `data/soft-dimmer/README.md` | bundle doc | `da6601b9f77d7afc` | the upstream readme |
| `data/soft-dimmer/extension.js` | bundle code | `3f5f1cc55940d0e8` | the extension's entry point |
| `data/soft-dimmer/dimmer.js` | bundle code | `025a5677054405e2` | **the software overlay engine** |
| `data/soft-dimmer/dbus.js` | bundle code | `f05c167a06b577d4` | the `io.github.softdimmer` D-Bus service |
| `data/soft-dimmer/dbus/io.github.softdimmer.BrightnessControl.xml` | bundle code | `1dc948b5422c6251` | its introspection XML |
| `data/soft-dimmer/metadata.json` | bundle code | `20bc496ba37eb033` | the extension's identity |
| `data/soft-dimmer/schemas/org.gnome.shell.extensions.soft-dimmer.gschema.xml` | bundle schema | `758e601e23751746` | the extension's settings |
| `data/soft-dimmer/test-logic.js` | bundle test | `a5cd115d80ace212` | the extension's own test suite |

The whole `data/soft-dimmer/` directory goes (10 files). Nothing else in
`data/` is affected — `data/soft-brightness-plus/` stays.

### 1.2 Files to be modified

| File | What is removed | Where |
|---|---|---|
| `src/ui/pages.cpp` | the whole "OLED dimming section" (the MyASUS block, its three callbacks and the sync helper) | lines **791–996**, the insertion at **1151**, the refresh hook at **2337–2339**, the includes at **11** and **21**, the widget handles at **69–73** |
| `src/i18n/i18n.h` | the 13 tokens `ACC_T_OLEDDIM_*` | lines **160–164** |
| `src/i18n/i18n.cpp` | the 13 English and 13 Arabic strings | the two tables |
| `src/main.cpp` | the include of `control/oledsoftdim.h` and the 14 unit checks for the level ↔ brightness mapping | line **19**, lines **541–568** |
| `src/ui/theme.css` | the 6 `oled*` classes (12 rules: a base block and a refinement block) | lines **226–232** and **566–572** |
| `src/ui/theme-dark.css` | the same 6 classes in the dark skin | lines **226–232** and **563–569** |
| `Makefile` | the `oledsoftdim.cpp` and `oledslider.cpp` entries in `SRCS`, and the `data/soft-dimmer` install line | lines **28**, **32**, **72–73** |
| `AGENTS.md` | the "Soft Dimmer" table row and the two paragraphs about it | lines **6**, **11–15**, **125** |

### 1.3 Documents (a decision for the approver)

Thirteen documents describe this feature. They are the record of the work on
the *extension*, not on ASUSCraft.

| Document | About |
|---|---|
| `ASUSCRAFT_SOFT_DIMMER_DBUS_INVESTIGATION.md` | the ASUSCraft-side investigation |
| `SOFT_DIMMER_MIGRATION_REPORT.md` | the migration plan (Phase 1 of the previous task) |
| `SOFT_DIMMER_QS_OVERLAY_REFINEMENT_REPORT.md` | the overlay/stacking work |
| `SOFT_DIMMER_GNOME51_MYASUS_REPORT.md` | the MyASUS panel |
| `SOFT_DIMMER_GNOME51_PHASE2_REPORT.md`, `…PHASE2_LIVE_VERIFICATION.md`, `…PHASE3_REPORT.md` | the extension's D-Bus repair and the overlay phase |
| `SOFT_DIMMER_GNOME51_FINAL_AUDIT.md`, `…REPAIR_PLAN.md`, `…SLIDER_ERROR_FIX_REPORT.md`, `SOFT_DIMMER_PHASE1_IMPLEMENTATION_REPORT.md`, `SOFT_DIMMER_PREIMPLEMENTATION_CONSISTENCY_AUDIT.md`, `SOFT_DIMMER_BACKUP_ROLLBACK_HARDENING_REPORT.md` | the extension's own history |

The task lists *"feature-specific tests and documentation"* among the things to
remove. Two readings:

* **A — remove them all.** The feature leaves no trace in the repository.
* **B — remove the ASUSCraft-side ones** (`ASUSCRAFT_SOFT_DIMMER_DBUS_INVESTIGATION.md`,
  `SOFT_DIMMER_MIGRATION_REPORT.md`, `SOFT_DIMMER_QS_OVERLAY_REFINEMENT_REPORT.md`,
  `SOFT_DIMMER_GNOME51_MYASUS_REPORT.md`) and keep the ones that are the
  history of the *extension* project.

**Recommendation: B.** The extension is a separate project with its own
repository; its audit trail does not belong to ASUSCraft's feature list, and
the documents explain why the D-Bus contract looks the way it does. This audit
and the final removal report document the removal itself and stay.

### 1.4 Files that must remain — and why

| File | Why it stays |
|---|---|
| `src/control/softdim.{h,cpp}` | it installs **Soft Brightness Plus**, not Soft Dimmer — the hardware OLED dimming |
| `src/control/brightness.{h,cpp}` | the SBP "Software dimming" control (the hardware OLED dimming) |
| `src/control/backlight.{h,cpp}` | the ASUS hardware brightness |
| `src/control/nightlight.{h,cpp}` | Night Light |
| `src/ui/pages.cpp` (the rest) | the Display Center: resolution and refresh rate, the backlight slider, the SBP slider, Eye-care, E-Reading |
| `src/ui/specs.cpp` | it prints the SBP status row (`ACC_T_SOFTDIM`) |
| `src/i18n/*` (`ACC_T_SOFTDIM`, `ACC_T_SOFTDIM_SUB`, `ACC_T_SOFTDIM_NEED`, `ACC_T_OLED_EYE`, `ACC_T_OLED_HINT`, …) | the strings of the controls that stay |
| `data/soft-brightness-plus/` | the bundled **Soft Brightness Plus** extension |
| `data/acc-helper`, the polkit policy, `install.sh` | the helper, the policy and the Linux installer |
| `craft-launcher-main/` | the Windows installer — it never touches a GNOME extension |
| `src/control/asusd.{h,cpp}`, `gfx`, `muxreq`, `ctl`, `touchpadctl` | unrelated hardware controls |

### 1.5 The extended identifier search

The specification names more search terms than the first pass used. The
results, with the ownership classification:

| Identifier | Hits | Where | Ownership |
|---|---|---|---|
| `soft-dimmer@io.github.softdimmer` | 100 | `data/soft-dimmer/` (the bundle), the documents | **the feature** — removed |
| `org.gnome.shell.extensions.soft-dimmer` | 36 | the bundle's schema, the documents | **the feature's** own schema — removed with the bundle; it is never an ASUSCraft schema |
| `soft-dimmer-asc@io.github.asuscraft` | 21 | the documents only | an **independent** extension — untouched |
| `soft-brightness-plus@joelkitching.com` | 17 | `src/control/softdim.*`, `data/soft-brightness-plus/` | **Soft Brightness Plus** — kept |
| `org.gnome.shell.extensions.soft-brightness-plus` | 9 | the same | kept |
| `org.gnome.shell.extensions.soft-dimmer-asc` | 7 | the documents only | independent — untouched |
| `soft-dimmer-pro@io.github.softdimmer` | 3 | the documents only | independent — untouched |
| `org.gnome.shell.extensions.soft-dimmer-pro` | 1 | the documents only | independent — untouched |
| `io.github.softdimmer` (the bus name) | 6 | `src/control/oledsoftdim.h`, `data/soft-dimmer/dbus*` | **the feature's** service — removed |
| `OLEDSoftDim` / `OLED Soft Dimming` | 0 | — | the feature never used these spellings; the code uses `oledsoftdim` and `ACC_T_OLEDDIM_*` |
| `dimmer` (the broadest term) | — | every hit outside the bundle is either Soft Brightness Plus, `src/control/softdim.*`, `ACC_T_SOFTDIM*` or a document | see the table above |
| "software dimming" (the prose) | — | the **docs** use it for **Soft Brightness Plus** ("Flicker-free software dimming", "Software dimming"), and `src/main.cpp:402` reports the SBP status | the human-readable name belongs to SBP — **kept** |

**The directories the specification names:** `src/` (81 files), `data/`
(34), `docs/` (41), `assets/` (17), `packaging/` (3), `debian/` (5) exist.
`include/`, `tests/`, `resources/` and `scripts/` **do not exist** in this
project — the tests live inside `src/main.cpp` (`--check`, `--self-test`) and
next to the code they test (`data/soft-dimmer/test-logic.js`).

### 1.6 File ownership classification

| Ownership | Files |
|---|---|
| **exclusively Soft Dimmer** | `src/control/oledsoftdim.{h,cpp}`, `src/ui/oledslider.{h,cpp}`, all 10 files of `data/soft-dimmer/` |
| **Soft Dimmer only in the part that is removed** | `src/ui/pages.cpp` (the block, the includes, the statics), `src/i18n/i18n.{h,cpp}` (13 tokens), `src/main.cpp` (the include + 14 checks), `src/ui/theme.css`, `src/ui/theme-dark.css` (6 classes), `Makefile` (3 references) |
| **shared with unrelated features — keep** | `src/control/softdim.{h,cpp}` (Soft Brightness Plus), `src/control/brightness.{h,cpp}`, `src/control/backlight.{h,cpp}`, `src/control/nightlight.{h,cpp}`, `src/ui/specs.cpp`, the `ACC_T_SOFTDIM*` / `ACC_T_OLED_*` strings, `data/soft-brightness-plus/`, `install.sh`, the packaging |
| **documentation** | 13 feature-specific documents, plus the `AGENTS.md` rules — see §1.3 |

---

## 2. The dependency map

```
                 ASUSCraft
                    │
    ┌───────────────┴────────────────────────────┐
    │                                            │
 src/ui/pages.cpp                        src/control/oledsoftdim.cpp
    │  the MyASUS block                        │  the D-Bus client
    │  (oled_dimming_card)                     │
    ├── src/ui/oledslider.{h,cpp} ────────────┤   (the MyASUS slider widget)
    │      the cairo gradient slider           │
    │                                           │
    └── src/i18n/i18n.{h,cpp}                  │
           the ACC_T_OLEDDIM_* strings           │
                                                │
 src/main.cpp ──── the mapping unit checks ────┘
                                                │
 src/ui/theme*.css ── the .oled* classes         │
                                                │
 Makefile ── SRCS + the data/soft-dimmer install │
                                                │
 data/soft-dimmer/ ─────────────────────────────┘
    the bundled extension: dimmer.js (the overlay engine),
    dbus.js (the service), extension.js, the schema, the XML
```

The dependency is strictly one-way: the Display Center knows the backend, the
backend knows nothing about ASUSCraft. Removing the leaf modules
(`oledsoftdim.*`, `oledslider.*`) first makes the rest of the removal
mechanical.

### 2.1 The shared-component analysis

Four components in the tree carry a name that points at Soft Dimmer but
belong to something else. None of them may be deleted without proof, and the
proof is in this table:

| Component | What it really drives | Proof | Verdict |
|---|---|---|---|
| `src/control/softdim.{h,cpp}` | **Soft Brightness Plus** — it copies `data/soft-brightness-plus/` into the user's extension folder, compiles *that* schema (`SBP_SCHEMA = "org.gnome.shell.extensions.soft-brightness-plus"`), enables *that* UUID (`SBP_UUID = "soft-brightness-plus@joelkitching.com"`) and writes *its* `monitors` key | the constants `SBP_UUID`, `SBP_SCHEMA` and `SBP_XML` in `softdim.cpp:16-18` | **keep** |
| `src/control/brightness.{h,cpp}` | the SBP brightness — the *hardware* OLED dimming below the panel minimum | `acc_brightness_get/set` are called from the Display page's "Software dimming" slider, which is the SBP control | **keep** |
| `ACC_T_SOFTDIM`, `ACC_T_SOFTDIM_SUB`, `ACC_T_SOFTDIM_NEED` | the strings of the SBP control and of its installer button | `src/ui/pages.cpp:1062,1100,1126`, `src/ui/specs.cpp:313` | **keep** |
| `data/soft-brightness-plus/` | the bundled **Soft Brightness Plus** extension | its own `metadata.json` (`soft-brightness-plus@joelkitching.com`) | **keep** |

The same reasoning applies to the two "dimming" sliders that sit next to each
other on the Display page:

```
Backlight          →  acc_backlight_set()   →  the ASUS hardware backlight      KEEP
Software dimming   →  acc_brightness_set()  →  Soft Brightness Plus (hardware)   KEEP
OLED Flicker-Free  →  acc_oledsoftdim_*()   →  the Soft Dimmer overlay           REMOVE
```

Only the third one is the discontinued feature. The first two are the
"ASUS hardware brightness" and the "ASUS OLED hardware settings" the task
protects.

### 2.2 The D-Bus inventory

| Side | Name | Object | Interface | Methods | Fate |
|---|---|---|---|---|---|
| **client** (in ASUSCraft) | `io.github.softdimmer` | `/io/github/softdimmer/BrightnessControl` | `io.github.softdimmer.BrightnessControl` | `GetBrightness(i)→d`, `SetBrightness(i,d)`, `GetEnabled()→b`, `SetEnabled(b)`, `GetCapabilities()→a{sv}` | **remove** (`src/control/oledsoftdim.*`) |
| **service** (in the bundle) | `io.github.softdimmer` | `/io/github/softdimmer/BrightnessControl` | `io.github.softdimmer.BrightnessControl` | the same five | **remove** (`data/soft-dimmer/dbus.js` + the XML) |
| client | `io.gnome.Mutter.DisplayConfig` | `/io/gnome/mutter/DisplayConfig` | — | the display's brightness properties | **keep** (the hardware backlight) |
| client | `io.gnome.Mutter.DisplayConfig` / the SBP service | — | — | the OLED dimming below the panel minimum | **keep** (`src/control/brightness.cpp`) |

**No public D-Bus method of any *other* feature is touched.** The only D-Bus
contract that disappears is the one this feature added.

---

## 3. The Display Center UI inventory

The Display (OLED) page (`acc_page_disp`, `src/ui/pages.cpp`) currently holds,
in this order:

| Element | Widget | Fate |
|---|---|---|
| the page header (icon, title, resolution and refresh rate) | `ACC_T_PG_DISP` | keep |
| the **Backlight** slider | `f_bl_slider` → `acc_backlight_set()` | keep (hardware brightness) |
| the **Software dimming** slider | `f_dsp_slider` → `acc_brightness_set()` | keep — this is Soft Brightness Plus, i.e. the hardware OLED dimming |
| the "Turn on / Enable OLED dimming" button | `softdim_cb()` → `acc_softdim_install()` | keep — it installs Soft Brightness Plus |
| **the MyASUS "OLED Flicker-Free Dimming" block** (icon, title, description, red notice, caption, gradient slider, 0/50/100 scale, percentage, switch, state) | `oled_dimming_card()` and its three callbacks | **remove** |
| the **Eye-care mode** row | `ACC_T_OLED_EYE` | keep |
| the **E-Reading (grayscale)** row | `ACC_T_EREAD` | keep |

The removed block is the *only* thing in the page that talks to Soft Dimmer.
Nothing in the page has to be re-laid-out afterwards: the block is a self
contained entry in the page's vertical list.

---

## 4. The build system, the resources and the packaging

| Reference | Where | Fate |
|---|---|---|
| `src/control/oledsoftdim.cpp` in `SRCS` | `Makefile:28` | remove |
| `src/ui/oledslider.cpp` in `SRCS` | `Makefile:32` | remove |
| `cd data && find soft-dimmer -type f -exec install …` | `Makefile:72–73` | remove |
| `cd data && find soft-brightness-plus …` | `Makefile:70–71` | **keep** |
| `%{_datadir}/asus-control-center/` | `packaging/rpm/asus-control-center.spec:61` | keep — the directory is shipped wholesale, so removing the bundle needs no packaging change |
| `debian/rules` | — | no per-file extension references |
| `install.sh` | — | **keep** — it installs the helper, the policy and the services; it never installs an extension |

### 4.1 The packaging, the CI and the AppImage target

| Target | Present? | What it needs |
|---|---|---|
| **RPM** (`packaging/build-rpm.sh` → `packaging/rpm/asus-control-center.spec`) | yes | nothing: `%{_datadir}/asus-control-center/` is shipped wholesale, so the bundle leaves with the directory |
| **DEB** (`packaging/build-deb.sh`, `debian/`) | yes | nothing, for the same reason |
| **AppImage** | **no such target in this project** | not applicable — there is no AppImage recipe, no `appimagetool` call and no CI job for one. The Windows launcher (`craft-launcher-main/`) is a different artefact and is untouched |
| `install.sh` (Linux) | yes | nothing — it installs the helper, the policy and the firmware services; it never installs an extension |
| `make uninstall` | yes | nothing — it removes `$(DATADIR)/asus-control-center/` as a whole, so the bundle leaves with it |
| CI (`.github/workflows/packages.yml`) | yes | nothing — it builds the RPM and the DEB, lints the DEB and runs `asus-control-center --self-test`; it never touches an extension |

**No schema, icon or desktop file belongs to the Soft Dimmer feature.** The
extension's GSettings schema (`org.gnome.shell.extensions.soft-dimmer`) lives
in the bundle and in the *installed* extension, never in ASUSCraft's own
schema — so no ASUSCraft schema key is removed and no user setting is
touched.

---

## 5. The tests and the settings

### 5.1 Tests

| Suite | Where | Fate |
|---|---|---|
| the extension's own suite (108 checks) | `data/soft-dimmer/test-logic.js` | removed with the bundle |
| the 14 mapping checks | `src/main.cpp:541–568` | remove |
| ASUSCraft's other `--check` checks | `src/main.cpp` | keep |
| `make test` (`--check` + `--self-test`) | `Makefile` | keep |

**Current counts, before the removal**

| Suite | Checks |
|---|---|
| `data/soft-dimmer/test-logic.js` | 108 |
| `src/main.cpp` `--check` | 14 (Soft Dimmer) + 24 (the rest) |

**Expected after the removal**: 0 Soft Dimmer checks, 24 remaining
`--check` checks, and the `--check`/`--self-test` gates unchanged.

### 5.2 Settings and configuration

| Item | Where it lives | Fate |
|---|---|---|
| `org.gnome.shell.extensions.soft-dimmer` (`brightness`, `enabled`, `minimum-brightness`, `monitor-selection`, `restore-brightness`, the three shortcuts) | dconf, owned by the **extension** | **untouched** — the feature is removed from ASUSCraft, the extension's own settings are the user's |
| `org.gnome.shell.extensions.soft-brightness-plus` | dconf, owned by Soft Brightness Plus | untouched |
| `org.gnome.shell.extensions.soft-dimmer-asc` | dconf, owned by the ASC extension | untouched |
| `org.gnome.shell.extensions.soft-dimmer-pro` | dconf, owned by Soft Dimmer Pro | untouched |
| ASUSCraft's own settings (`~/.config/asus-control-center/settings.conf`) | the local account | untouched |
| the polkit policy | `/usr/share/polkit-1/actions/…` | untouched |

**No ASUSCraft configuration depends on the feature**, so nothing has to be
migrated or cleaned up in the user's configuration.

---

## 6. Removal risks

| # | Risk | Severity | Mitigation |
|---|---|---|---|
| R1 | **Removing the wrong thing**: `src/control/softdim.cpp` *looks* like Soft Dimmer but is Soft Brightness Plus | **high** | the file is explicitly listed as "must remain" (§1.4); the audit greps for the *bundle* and the *client*, not for the word "softdim" |
| R2 | The removed block leaves a hole in the Display page | low | the block is one entry in a vertical list; the page keeps its header, the backlight slider, the SBP slider, Eye-care and E-Reading |
| R3 | The i18n tables fall out of step (a missing string becomes `NULL`) | medium | the arrays are declared **without** a size, so `G_STATIC_ASSERT (G_N_ELEMENTS (EN) == ACC_T_COUNT)` is now a real check and a mismatch is a compile error |
| R4 | The CSS classes are left behind in one skin only | low | both skins are edited in the same step and the class lists are compared afterwards |
| R5 | The Makefile still ships the bundle | low | the install line is removed in the same step; `make install` into a staging directory is verified afterwards |
| R6 | The installed extension keeps running and still dims the screen | medium | the *installed* extension is an independently installed extension (see §7); it is **not** touched by this task, and the task forbids removing independently installed extensions |
| R7 | A dangling include or a stale static widget pointer | low | the build is run after every step and `--check`/`--self-test` must pass |
| R8 | The `data/soft-dimmer` directory survives because of a wildcard | low | every file is removed by name; no wildcard deletion is used anywhere |

---

## 7. The independently installed extensions — out of scope

These live in `~/.local/share/gnome-shell/extensions/` and are **not** part of
the project. The task forbids removing them, and this audit does not propose
to:

| Extension | State |
|---|---|
| `soft-dimmer@io.github.softdimmer` | enabled and ACTIVE — it is the trimmed (Quick-Settings-free) build that Phase 2 of the previous task installed. **Left alone.** |
| `soft-brightness-plus@joelkitching.com` | enabled — the hardware OLED dimming. **Left alone.** |
| `soft-dimmer-pro@io.github.softdimmer` | installed, not enabled. **Left alone.** |
| `soft-dimmer-asc@io.github.asuscraft` | installed, not enabled. **Left alone.** |

If the Soft Dimmer *extension* itself should also go from the user's
machine, that is a separate decision outside this task — the task's own
wording forbids removing independently installed extensions.

---

## 8. The proposed removal sequence

Each step is independent and verifiable; the build and the gates run after
every one.

| Step | Action | Verification |
|---|---|---|
| 0 | **Backup**: a persistent copy of every file that is about to change, with a SHA-256 manifest and a restoration test | the manifest verifies; the restored tree hashes identically |
| 1 | Remove the two new C++ modules | `make` still compiles (the references are removed in the same step, see step 2) |
| 2 | `src/ui/pages.cpp`: remove the include of `oledslider.h`, the include of `oledsoftdim.h`, the widget handles, the whole "OLED dimming section" (791–996), the insertion at 1151 and the refresh hook at 2337–2339 | `make` |
| 3 | `src/i18n`: remove the 13 tokens and the 26 strings | `make` — the count guard proves both tables still match |
| 4 | `src/main.cpp`: remove the include and the 14 checks | `./asus-control-center --check` → 0 failures |
| 5 | `src/ui/theme.css` and `theme-dark.css`: remove the 6 `oled*` classes from both | the class lists of the two skins are compared |
| 6 | `Makefile`: remove the two `SRCS` entries and the bundle install line | `make` and a staged `make install` |
| 7 | Remove the `data/soft-dimmer/` directory (10 files, by name) | the directory is gone; `make install` into a staging root contains no `soft-dimmer` |
| 8 | `AGENTS.md`: update the "Soft Dimmer" row and the paragraphs about it | the document no longer claims a Soft Dimmer integration |
| 9 | The documents, according to the decision in §1.3 | — |
| 10 | Final verification | `make`, `--check`, `--self-test`, a grep for the remaining references, and a run of the app |

### 8.2 The testing plan

| # | Check | Command | Expected |
|---|---|---|---|
| T1 | a clean build | `make clean && make -j"$(nproc)"` | no warnings, no errors |
| T2 | the pure-logic gate | `./asus-control-center --check` | **0 failures**; the Soft Dimmer block is gone, the other checks are untouched |
| T3 | the hardware self-test | `./asus-control-center --self-test` | the same output as before the removal (the SBP "Software dimming" row is still there) |
| T4 | the extension's own suite | `gjs -m data/soft-dimmer/test-logic.js` | **not applicable** — the bundle is gone |
| T5 | the source audit | the greps in §8.1 | no output for the feature identifiers |
| T6 | the class lists of the two skins match | compare the selector lists of `theme.css` and `theme-dark.css` | identical |
| T7 | a staged install | `make install DESTDIR=/tmp/acc-stage` | the staged tree contains no `soft-dimmer` and still contains `soft-brightness-plus` |
| T8 | the runtime | start the app and open the Display Center | the page shows the header, the backlight slider, the SBP slider, Eye-care and E-Reading — and no Soft Dimmer block |
| T9 | the runtime D-Bus | `gdbus call --session --dest io.github.softdimmer …` | the *independently installed* extension still answers; ASUSCraft no longer talks to it |
| T10 | the packages | `packaging/build-rpm.sh`, `packaging/build-deb.sh` | both build; the DEB passes `lintian --fail-on error` |

### 8.1 The final greps

```sh
grep -rniI --exclude-dir=.git --exclude=asus-control-center \
     -e "oledsoftdim" -e "oledslider" -e "ACC_T_OLEDDIM" \
     -e "io\.github\.softdimmer" -e "data/soft-dimmer" src/ data/ Makefile
#   expect: no output

grep -rn "oledpct\|oledscale\|oledstat\|oleddesc\|olednotice\|oledcaption" \
     src/ui/theme.css src/ui/theme-dark.css
#   expect: no output
```

`grep -rn "softdim" src/` will still match `src/control/softdim.{h,cpp}`
(Soft Brightness Plus) and `ACC_T_SOFTDIM*` — **that is expected and
correct**.

---

## 9. The rollback

Everything is reversible from the backups, which are persistent, verified and
outside the repository:

| Backup | Contents |
|---|---|
| `~/.local/share/soft-dimmer/backups/20261111T094949Z-asuscraft-phase3/` | the ASUSCraft files as they were before the OLED panel work (9 files) |
| `~/.local/share/soft-dimmer/backups/20261111T102150Z-phase2-qs-removal/` | the extension as it was before the Quick Settings removal (11 files) |

A fresh backup of the exact set of files this removal touches will be taken
before the first edit, and the restoration instructions will be written out in
the final report.

---

## 10. Open questions for the approver

1. **The documents** (§1.3): remove all thirteen, or keep the ones that are the
   history of the extension project? The recommendation is to keep those.
2. **The installed extension** (§7): `soft-dimmer@io.github.softdimmer` is
   enabled and running on this machine. The task forbids removing independently
   installed extensions, so this audit leaves it alone — but the *effect* is
   that the screen can still be dimmed by the overlay even after ASUSCraft
   loses the feature. Confirm that this is the intent.
3. **The historical `AGENTS.md` rules** about the extension (lines 6, 11–15,
   125): they will be corrected to say that the feature no longer exists in
   the project. Confirm that the rule *"do not modify the Soft Dimmer
   extension from this repo"* should be kept (it is still the right rule for
   the independently installed copy).

---

**Phase 1 complete — audit only, no file was modified. Waiting for approval
before Phase 2.**
