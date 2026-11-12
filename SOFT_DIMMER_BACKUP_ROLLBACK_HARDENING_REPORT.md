# Soft Dimmer — Backup & Rollback Hardening Report

**Date:** 2026-11-11
**Subject:** `soft-dimmer@io.github.softdimmer`
**Purpose:** close the backup/rollback gap recorded as **B-1** in
`SOFT_DIMMER_PREIMPLEMENTATION_CONSISTENCY_AUDIT.md`.
**Scope:** backup infrastructure only. No extension behaviour was changed and Phase 2 was
**not** started.

---

## 1. Verdict

# PASS — Phase 2 is unblocked

All six acceptance criteria are met:

| Criterion | Result | Evidence |
|---|---|---|
| A persistent backup exists (outside `/tmp`) | **PASS** | `/home/mostafa/.local/share/soft-dimmer/backups/20261111T061208Z` |
| Its SHA-256 manifest verifies | **PASS** | `sha256sum -c MANIFEST.sha256` → 9/9 `OK`, exit 0 |
| A separate restoration test passes | **PASS** | Staged into a separate dir; hashes, permissions, structure and parse-checks all passed |
| The rollback procedure was reviewed for failure safety | **PASS** | §6; 7 failure paths exercised, 5 abort conditions proven to trigger |
| The installed extension is unchanged | **PASS** | Byte-identical to the manifest after every step; mtimes untouched |
| No extension or desktop state was altered | **PASS** | `State: ACTIVE`, `Enabled: Yes`, `brightness = 1.0` — all identical to the pre-task reading |

**No blocker remains.** Phase 2 may begin — with the mandatory rule in §9 (re-create a fresh
timestamped backup before touching `dbus.js`).

---

## 2. Exact backup path

```
/home/mostafa/.local/share/soft-dimmer/backups/20261111T061208Z/
```

Persistent: `/home/mostafa/.local/share/` is on `/home` (`/dev/nvme0n1p8`, 85 GB free),
survives reboot and logout, and is **outside** the extension directory.

### Layout

```
20261111T061208Z/
├── extension/                                    the extension tree (cp -a)
│   ├── dimmer.js  extension.js  indicator.js  dbus.js  test-logic.js
│   ├── metadata.json
│   ├── dbus/io.github.softdimmer.BrightnessControl.xml
│   └── schemas/{org.gnome.shell.extensions.soft-dimmer.gschema.xml, gschemas.compiled}
├── MANIFEST.sha256          SHA-256 of every backed-up file (9 entries)
├── MANIFEST.meta            path, counts, sizes, per-file listing
├── RESTORE.md               the new safe rollback procedure (Step 4)
└── state/STATE.txt          read-only snapshot of the extension's state at backup time
```

| Metric | Value |
|---|---|
| Backup timestamp (UTC) | `20261111T061208Z` |
| Files backed up | **9** (all files in the extension tree) |
| Sub-directories | 2 (`dbus/`, `schemas/`) |
| Total size | 46,762 bytes (64 KB tree; 88 KB including metadata) |
| Manifest entries | 9 — exactly matches the tree |
| Copy method | `cp -a` (modes, ownership, timestamps, symlink-free) |

### Why this location and not the extension directory

- `~/.local/share/soft-dimmer/` is **outside** `~/.local/share/gnome-shell/extensions/`, so the
  backup is never picked up by GNOME's extension scanner (which requires
  `<uuid>/metadata.json` at the top level).
- It sits on the **same filesystem** (`st_dev 55`) as the extensions directory, so the final
  rename into place is atomic (`rename(2)`) rather than a copy.
- It is persistent, unlike the `/tmp/opencode/` location that was lost in the 2026-11-11 reboot.

---

## 3. Step 1 — Identification and inspection

**Resolved directory** (from `gnome-extensions info`, cross-checked with `realpath`):

```
/home/mostafa/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer
drwxr-xr-x mostafa:mostafa
```

**Complete file set required to restore** — 9 regular files, 2 sub-directories, **no symlinks**:

```
dimmer.js                                              12,798 B   2026-11-10 20:36
extension.js                                            7,456 B   2026-11-10 16:27
indicator.js                                            7,013 B   2026-11-10 16:25
dbus.js                                                 6,577 B   2026-11-10 16:25
test-logic.js                                           7,443 B   2026-11-10 20:37
metadata.json                                             489 B   2026-11-05 09:42
dbus/io.github.softdimmer.BrightnessControl.xml         2,142 B   2026-11-05 09:42
schemas/org.gnome.shell.extensions.soft-dimmer.gschema.xml 2,024 B 2026-11-05 09:42
schemas/gschemas.compiled                                  820 B   2026-11-05 09:42
```

`schemas/gschemas.compiled` is a build artefact, but it is part of the installed tree and is
backed up so a restore needs no `glib-compile-schemas` step.

**State recorded (read-only, no state changed):**

| Observation | Value |
|---|---|
| `gnome-extensions info` | `Enabled: Yes`, `State: ACTIVE`, `Version: 1` |
| GSettings `brightness` | `1.0` |
| GSettings `enabled` | `true` |
| GSettings `minimum-brightness` | `0.1` |
| GSettings `monitor-selection` | `'all'` |
| GSettings `restore-brightness` | `true` |
| GSettings shortcut keys | all `@as []` (empty) |
| `enabled-extensions` membership | `soft-dimmer@io.github.softdimmer` present |
| D-Bus name owner (`io.github.softdimmer`) | `:1.16` — the extension owns it |

**Disk space:** `/home` has **88,904,704 KB (85 GB) available** on `/dev/nvme0n1p8`
(60 % used). The backup needs 64 KB; even a triple-size staging area is trivially satisfied.

---

## 4. Step 2 — Backup creation and verification

| Check | Result |
|---|---|
| Backup root created outside `/tmp` | PASS |
| Copy preserves structure | PASS — `diff -r --no-dereference` against the source: **IDENTICAL** |
| Copy preserves modes/ownership/times | PASS — `cp -a`; per-file `%M %u:%g` listing matches |
| File count | PASS — 9 source → 9 backup |
| Manifest covers **every** file | PASS — 9 manifest entries, 9 tree files, sets equal |
| Manifest verifies against the backup | PASS — `sha256sum -c` → 9/9 `OK`, exit 0 |
| Manifest matches the **live** extension | PASS — independent hash of the installed tree equals the manifest |

### Manifest

```
1dc948b5…  extension/dbus/io.github.softdimmer.BrightnessControl.xml
90fb17e4…  extension/dbus.js
2681fe1b…  extension/dimmer.js
2ed1b548…  extension/extension.js
e12aff48…  extension/indicator.js
d731e5fc…  extension/metadata.json
7319aa0d…  extension/schemas/gschemas.compiled
758e601e…  extension/schemas/org.gnome.shell.extensions.soft-dimmer.gschema.xml
e8b36f9b…  extension/test-logic.js
```

The manifest paths are relative to the backup root, so `cd <backup> && sha256sum -c MANIFEST.sha256`
works from anywhere and covers the whole tree.

---

## 5. Step 3 — Restoration test

Performed in a **separate** directory under the persistent backup area
(`20261111T061208Z/restore-test/extension`), never against the installation.

| Check | Result |
|---|---|
| Restore into the test directory | PASS — `cp -a` of the backup tree |
| Restored hashes == manifest | PASS — byte-for-byte identical set |
| Permissions, ownership, file types, directory structure | PASS — `find -printf '%M %u:%g %y %P'` diff against the source: identical |
| Full tree diff vs. the installed extension | PASS — `diff -r --no-dereference`: identical |
| Restored JS parses | PASS — `node --check` on all 5 `.js` files (copies, in `/tmp` scratch only) |
| Restored schema compiles | PASS — `glib-compile-schemas --strict --dry-run` on the **restored** schema |
| Installed extension untouched by the test | PASS — hashes and mtimes unchanged after the test |
| Disposable test directory removed | PASS — `restore-test` gone; only the 13 backup files remain |

---

## 6. Step 4 — The new safe rollback procedure

Written to **`RESTORE.md` inside the backup**, so it travels with the data it protects. It is
also reproduced in §8 below.

**Design rules it enforces**

1. Validate the backup and manifest **before** touching the installation.
2. Stage into a separate directory and verify it **completely** before any swap.
3. Never delete the installed extension before a verified replacement exists.
4. Swap by **rename**, keeping the previous installation recoverable.
5. Refuse to proceed if any validation fails.

**Sequence**

| Step | Action | Touches the installation? |
|---|---|---|
| 1 | Validate: tree present, manifest present/non-empty, `sha256sum -c` passes, manifest covers exactly the tree, backup not under `/tmp` | No |
| 2 | Stage: `cp -a` into `~/.local/share/soft-dimmer/staging/…`, re-hash, compare to manifest, compare structure/permissions, `node --check` each JS file, `chmod -R u+rwX` | No |
| 3 | Swap: `mv "$D" "$OLD"` then `mv "$S/extension" "$D"`; on failure `mv "$OLD" "$D"` immediately | Yes — but the old copy is renamed, never deleted |
| 4 | Verify: hash the installed tree against the manifest; log out/in; check `gnome-extensions info` | No |
| 5 | Retire `$OLD` **only after** step 4 passes | Deletes the previous copy — last step only |

**Details that matter**

- The staging area is **outside** the extensions directory, so GNOME's scanner never sees a
  half-restored tree (it requires `<uuid>/metadata.json` at the top level).
- A **same-filesystem check** (`stat -c %d`) runs before staging; if the staging area and the
  extensions directory ever land on different filesystems, the procedure aborts rather than
  silently degrading the final rename into a non-atomic copy.
- `chmod -R u+rwX` on the staged copy makes the restored files writable, so a *subsequent*
  restore can overwrite them. The backup itself is left as copied.
- The procedure was **executed** for steps 1 and 2 during this task (validation + staging,
  both proven green); the swap (step 3) was **not** executed against the installation, and the
  staged test directory was removed afterwards.

---

## 7. Step 5 — Failure-path analysis

Each failure was reviewed; the abort behaviour below was **empirically exercised** where it
could be tested safely (against copies, never the installation).

| # | Failure | Detection | Abort point | Installation survives? | Backup survives? |
|---|---|---|---|---|---|
| **F1** | Missing backup files / no backup tree | `test -d "$B/extension"` | Step 1 | **Yes** — untouched | Partly — the *backup* is damaged, but the installation is intact and remains the working copy |
| **F2** | Invalid or incomplete manifest | `test -s` + `sha256sum -c` + manifest-vs-tree diff | Step 1 | **Yes** — untouched | **Yes** — the manifest is only read, never written |
| **F3** | Backup under `/tmp` | `case "$B" in /tmp|/tmp/*)` | Step 1 | **Yes** | **Yes** — refused rather than used |
| **F4** | Insufficient disk space | 85 GB free vs 64 KB needed; staging aborts on `cp` failure | Step 2 (`set -e`) | **Yes** — untouched; only the staging dir is partial and is removed | **Yes** — read-only source |
| **F5** | Permission errors (unreadable source, unwritable target) | `cp` / `sha256sum` return non-zero | Step 2 | **Yes** — untouched | **Yes** — the manifest is read-only; the source tree is never chmod-ed |
| **F6** | Interrupted copy | Staged hashes differ from the manifest | Step 2, before any swap | **Yes** — untouched | **Yes** — the copy reads the source, never alters it |
| **F7** | Failed staging verification | hash / structure / parse diffs | Step 2, `rm -rf "$S"` and exit | **Yes** — untouched | **Yes** |
| **F8** | Failed directory swap | `mv "$S/extension" "$D"` returns non-zero | Step 3 → `mv "$OLD" "$D"` restores the original | **Yes** — `$OLD` is renamed back | **Yes** |
| **F9** | Interruption between the two renames | — | Manual recovery | **Yes** — the installation exists under `$D` *or* `$OLD`; both are complete trees, never partially deleted | **Yes** |
| **F10** | Interruption after the swap, before verification | — | Manual recovery | **Yes** — new tree at `$D`, previous at `$OLD`, backup at `$B` | **Yes** |

**Why the installation always survives.** The new procedure never issues `rm -rf` against the
installed extension. The only destructive command is `rm -rf "$OLD"` in step 5, which runs
*after* step 4 has confirmed the new installation matches the manifest. Every earlier failure
leaves `$D` exactly as it was.

**Why at least one valid backup always survives.** The backup is only ever read
(`cp -a` from it, `sha256sum -c` against it). Nothing in the procedure writes into the backup
directory except the new metadata files written once at creation time. The staged copy is a
separate tree, so a failed or aborted staging cannot damage the backup. The only unrecoverable
scenario is **the backup directory itself being deleted** — which is precisely why the backup
must not live under `/tmp` and why this task re-created it under `~/.local/share/`.

**Verification of the abort logic** (all run against disposable copies during this task):

```
FP-1  missing backup tree        -> aborts (no backup tree)              PASS
FP-2  empty manifest             -> aborts (empty manifest)              PASS
FP-3  manifest missing a file    -> aborts (manifest incomplete)         PASS
FP-4  corrupted file             -> aborts (hash mismatch detected)      PASS
FP-5  truncated/partial copy     -> detected before any swap            PASS
FP-6  disk space                 -> 64 KB needed, 85 GB available        PASS
FP-7  read-only source file      -> still verifies (read access suffices) PASS
```

---

## 8. The new rollback procedure (reproduced)

```sh
# ---- 0. preconditions -------------------------------------------------
B=/home/mostafa/.local/share/soft-dimmer/backups/20261111T061208Z
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
if [ "$(stat -c %d "$STAGE")" != "$(stat -c %d "$P")" ]; then
  echo "ABORT: staging area and extensions directory are on different filesystems"; exit 1
fi
rm -rf "$S"; mkdir -p "$S"
cp -a "$B/extension" "$S/extension"
( cd "$S" && find extension -type f -print0 | sort -z | xargs -0 sha256sum ) > "$S/STAGED.sha256"
diff <(sort "$B/MANIFEST.sha256") <(sort "$S/STAGED.sha256") \
  || { echo "ABORT: staged copy does not match the manifest"; rm -rf "$S"; exit 1; }
diff <(cd "$D" && find . -printf '%M %u:%g %y %P\n' | sort -k4) \
     <(cd "$S/extension" && find . -printf '%M %u:%g %y %P\n' | sort -k4) \
  || { echo "ABORT: staged copy has different modes/structure"; rm -rf "$S"; exit 1; }
for f in "$S"/extension/*.js; do
  t="$(mktemp --suffix=.mjs)"; cp "$f" "$t"
  node --check "$t" || { echo "ABORT: $f does not parse"; rm -f "$t"; rm -rf "$S"; exit 1; }
  rm -f "$t"
done
chmod -R u+rwX "$S/extension"
echo "OK: staged copy verified at $S"

# ---- 3. swap (rename, keep the old copy recoverable) -----------------
OLD="$P/.soft-dimmer.previous.$(date -u +%Y%m%dT%H%M%SZ)"
test -d "$S/extension" || { echo "ABORT: no verified staging directory"; exit 1; }
mv "$D" "$OLD"
if ! mv "$S/extension" "$D"; then
  mv "$OLD" "$D"; echo "ABORT: swap failed, original restored"; exit 1
fi
rm -rf "$S"
echo "OK: restored. Previous installation preserved at $OLD"

# ---- 4. verify -------------------------------------------------------
cd "$D" && find . -type f -print0 | sort -z | xargs -0 sha256sum | sed 's|\./|extension/|' > /tmp/post.sha
diff <(sort "$B/MANIFEST.sha256") <(sort /tmp/post.sha) && echo "OK: installed tree matches the backup"
# then log out / log in and check:
#   gnome-extensions info soft-dimmer@io.github.softdimmer | grep -E 'State|Enabled'

# ---- 5. retire the previous installation (only after step 4 passes) --
# rm -rf "$OLD"
```

---

## 9. Old rollback defect — why it was unsafe

The superseded one-liner (Phase 1 report §7.2):

```sh
rm -rf "$D" && cp -a /tmp/opencode/soft-dimmer-primary-backup "$D"
```

| # | Defect | Consequence |
|---|---|---|
| **D1** | **Destroys before validating.** `rm -rf "$D"` runs *first*; the copy is attempted afterwards. | If the copy fails, the user is left with **no extension at all** — the D-Bus service and the Quick Settings item disappear with it. |
| **D2** | **The source no longer exists.** The backup lived under `/tmp/opencode`, cleared by the 2026-11-11 reboot. | `cp` fails *after* `rm -rf` has already deleted the working extension. The compound command's exit status is `cp`'s, so a caller checking `$?` sees a failure — but only *after* the damage. |
| **D3** | **No manifest, no verification.** | Nothing proves the restored tree is complete or correct. A truncated copy restores silently and breaks the extension. |
| **D4** | **No recoverable previous version.** The old installation is deleted rather than renamed aside. | A bad restore cannot be undone; the only recourse is the (now missing) backup. |
| **D5** | **Restores a known-broken baseline.** That tree constructs a `Clutter.Actor` with `track_hover`/`can_focus`, which throws on GNOME 51. | The "safety net" would leave the extension **unloadable** — worse than the state it was meant to protect. |

The new procedure eliminates each: D1 → validate-then-stage-then-swap; D2 → durable backup outside
`/tmp`, plus an explicit `case "$B" in /tmp/*` abort; D3 → SHA-256 manifest verified before and
after staging; D4 → the swap renames the old tree to `$OLD` and only retires it after step 4
passes; D5 → the backup contains the **current, working** tree (the Phase 1 code), not the
pre-repair one.

---

## 10. Remaining limitations

1. **The pre-Phase-1 baseline is unrecoverable.** The `/tmp` copy is gone; only the current
   (post-Phase-1) tree can be restored. That is the correct baseline for Phase 2 — but the
   option of reverting to the pre-repair state is permanently gone. The current tree is
   *better* (the pre-repair one does not load on GNOME 51), so this is not a practical loss.
2. **The pre-repair audit baseline hashes** (`/tmp/opencode/primary-baseline.sha256`) are also
   gone. A fresh manifest now covers the current tree instead.
3. **The backup lives on one filesystem.** It is not mirrored. A failure of `/home` loses both
   the backup and the installation. A second copy on another filesystem would close that.
4. **The rollback procedure is written, not executed.** Steps 1–2 were exercised; the swap
   (step 3) was deliberately **not** run against the installation, as required.
5. **This task did not re-verify the live visual behaviour.** The extension remains
   `State: ACTIVE` with `brightness = 1.0` (overlay transparent). The outstanding L2–L6 checks
   from the consistency audit are unaffected by this task.

---

## 11. Recommendations for Phase 2

1. **Re-create a fresh timestamped backup before editing `dbus.js`.** The current backup
   represents the pre-Phase-2 state; a new one should capture the post-Phase-1 state that
   Phase 2 starts from. Use the same location and layout, and keep the old backup too.
2. **Never `rm -rf` the installed extension.** Follow `RESTORE.md` — stage, verify, rename.
3. **Keep `MANIFEST.sha256` in the backup**, and re-verify it after every phase.
4. **Verify the D-Bus contract changes against the shipped XML** — the interface file
   `dbus/io.github.softdimmer.BrightnessControl.xml` is part of the public contract and must
   stay byte-identical unless a versioned change is intended.
5. **Do not place any sole copy under `/tmp`.**

---

## 12. Read-only / safety confirmation

- **No extension file was modified.** `dimmer.js`, `extension.js`, `indicator.js`, `dbus.js`,
  `test-logic.js`, `metadata.json`, the schema and the D-Bus XML are byte-identical to the
  manifest, with their original mtimes intact (`dimmer.js` still 2026-11-10 20:36:57).
- **Nothing was deleted or overwritten in the installation.** The only `rm -rf` commands used
  were against disposable copies: the `restore-test/` directory, staging directories this task
  created, and `mktemp -d` scratch copies.
- **No extension was enabled, disabled, reloaded or restarted.** No logout occurred.
- **No GSettings value was changed.** `brightness = 1.0`, `enabled = true`, and the other six
  keys are exactly as recorded in §3.
- **No mutating D-Bus method was called.** The only D-Bus calls were `ListNames`,
  `GetNameOwner`, `ListExtensions` and `Introspect`.
- **No other extension was touched** — ASUSCraft, Soft Brightness Plus and Soft Dimmer Pro
  were not read-modified or written to.
- **No Git operation was performed** (this directory is not a repository).
- **No runtime code was changed** — the only writes were the new backup directory and this
  report.

---

*Backup and rollback hardening completed 2026-11-11. Verdict: **PASS — Phase 2 is
unblocked.** Backup: `/home/mostafa/.local/share/soft-dimmer/backups/20261111T061208Z`
(9 files, manifest verified 9/9, restoration test passed). The installed extension is
byte-identical and still `State: ACTIVE` with `brightness = 1.0`.*
