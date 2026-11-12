# ASUSCraft 1.2.5 — Final release checklist

**Date:** 2026-11-12
**Target:** the tag `v1.2.5` and the GitHub Release for `v1.2.5`
**State:** everything below the line is prepared ✓; **nothing is committed, pushed, tagged or published** ✓.

---

## 1. The state of the release

| | |
|---|---|
| the branch | `release/1.2.5`, based on `v1.2.4` (`aa1269c`) ✓ — the upstream's real release line ✓ |
| the staged change | 98 files, +14 320 / −4 742 ✓ |
| the commit | **not made** ✓ |
| the push | **not done** ✓ |
| the tag `v1.2.5` | **not created** ✓ |
| the GitHub Release | **not created** ✓ (it will be created as a **draft** ✓) |

---

## 2. The checklist

### The content ✓

- [x] The application version is **1.2.5** in the Makefile ✓, both RPM specifications ✓, `debian/changelog` ✓ and the AppStream metainfo ✓.
- [x] The agent's **package** version is **1.2.5** ✓; its **D-Bus interface** version stays independent at **1.0** ✓.
- [x] The Flatpak's application id is the lower-case `io.github.asuscontrolcenter.asuscraft` ✓ and its metainfo carries 1.2.5 ✓.
- [x] The CI's tag check covers all five version sources ✓ (the Makefile was added ✓).
- [x] `./flatpak/verify-versions.sh 1.2.5` passes ✓.
- [x] The helper and the polkit policy are **byte-identical** to the audited ones ✓ — and the CI now pins their checksums ✓.
- [x] The Soft Dimmer integration is **removed** ✓ and is **not** reintroduced ✗ — 16 files deleted ✓.
- [x] **Soft Brightness Plus is kept** ✓ (`src/control/sbp_local.cpp` ✓, the bundled extension ✓).
- [x] The D-Bus API is **additive only** ✓ — the four new methods were added ✓ and nothing incompatible changed ✗.
- [x] The Flatpak's permissions are unchanged ✓ and guarded by `flatpak/verify-permissions.sh` ✓.
- [x] The native installation (`asus-control-center-1.0.0-1.fc44`) was not touched ✓.
- [x] No credentials in the tree ✓; the CI scans for them ✓.
- [x] The old release artefacts are preserved in `dist/preserved-10.2/` ✓; the backups (twelve ✗) are intact ✓.

### The build ✓

- [x] `make` — exit 0, **0 warnings** ✓.
- [x] `make -C agent` — exit 0, **0 warnings** ✓.
- [x] `./asus-control-center --check` — **31 assertions, 0 failures** ✓.
- [x] `./asus-control-center --self-test` — exit 0 ✓.
- [x] `./agent/asuscraft-agent --check` — **53 assertions, 0 failures** ✓.
- [x] The installed agent's own `--check` — 53 assertions ✓.
- [x] The Flatpak builds ✓, installs ✓, and its gates pass inside the sandbox ✓.
- [x] The DEB builds on Ubuntu 24.04 ✓, installs ✓, and its self-test passes ✓.
- [x] Both RPMs build ✓ with valid metadata ✓ and no install scripts ✓.

### The artefacts ✓

| asset | size | SHA256 (the first 16) |
|---|---|---|
| `asus-control-center-1.2.5-1.fc45.x86_64.rpm` | 2 296 988 | `2d7861efc4609084…` |
| `asuscraft-agent-1.2.5-1.fc45.x86_64.rpm` | 67 545 | `49cce4713bc99eb4…` |
| `asus-control-center_1.2.5_amd64.deb` | 2 273 996 | `158e0cdd77f05d1e…` |
| `io.github.asuscontrolcenter.asuscraft_1.2.5.flatpak` | 2 327 528 | `3932280665a0bde1…` |
| `SHA256SUMS.txt` | — | the four above ✓ |

- [x] Every checksum re-verified ✓ (`sha256sum -c` → 4 × OK ✓).
- [x] The Flatpak bundle **installs** from the file ✓ and its gates pass ✓.
- [x] The filenames are preserved exactly ✓.

### The CI ✓

- [x] `ci.yml` — the application ✓, the host agent ✓, the metadata and the version policy ✓.
- [x] `flatpak.yml` — the manifest and the permission guard on every push ✓; the build on a tag or on demand ✓.
- [x] `packages.yml` — extended with the agent's gate ✓ and the Makefile in the tag check ✓.
- [x] Both helper scripts run locally ✓ and **catch a deliberate change** ✓ (four negative tests ✓).
- [x] **No workflow has been executed by GitHub yet** ✓ — the first push is the first real run ✓, and nothing is claimed to be green until it is ✓.

### The documentation ✓

- [x] `GITHUB_REPOSITORY_AUDIT.md` ✓
- [x] `CI_CONFIGURATION_REPORT.md` ✓
- [x] `GITHUB_RELEASE_NOTES_1.2.5.md` ✓ — the release notes ✓, including the **untested KDE night light** ✓ and the **sandbox-limited camera/microphone tests** ✓
- [x] `FINAL_RELEASE_CHECKLIST_1.2.5.md` ✓ (this file)
- [x] `RELEASE_READINESS_1.2.5.md` ✓ and `FINAL_INTEGRATION_AUDIT_1.2.5.md` ✓ (the audit trail ✓)

---

## 3. The decisions the reviewer still has to make

| # | decision | the recommendation |
|---|---|---|
| **Q1** | the staged change removes `v1.2.4`'s "open maximized" line ✗ and uses this branch's sidebar metrics (220/56/36 ✗ vs 250/68/28 ✗) ✗ — ship it, or restore v1.2.4's UI and **rebuild all four artefacts** ✗? | **ship it** ✓ — the artefacts are verified ✓ and the metrics are this project's own tuning ✓ |
| **Q2** | the branch name `release/1.2.5` ✓ | as-is ✓ |
| **Q3** | the release branch: `10.2` (the upstream's release line ✓) or `main` (still at v0.4.0 ✗)? | **`10.2`** ✓ — moving `main` would rewrite the upstream's default branch ✗ |
| **Q4** | the commit message and the author | the repository's own identity is already configured ✓ (`Mostafamomen` ✗ / the maintainer's address ✗) ✓ |

---

## 4. The proposed actions, in order — **each needs your approval**

Nothing below has been done ✓.

### Step 1 — the commit (local only ✓)

```sh
git commit -m "release: ASUSCraft 1.2.5 — the Flatpak build and the host agent

  The Flatpak GUI and the separately packaged host agent (asuscraft-agent).
  The agent is unprivileged: the whitelist-only helper and the polkit
  action io.github.asuscontrolcenter.helper are unchanged.

  The night light reads and writes the GNOME colour schema and keeps the
  KDE path; the touchpad uses the desktop's own setting and the obsolete
  xinput path is gone; the Soft Dimmer integration is removed and Soft
  Brightness Plus is kept.

  The version is 1.2.5 in the Makefile, both RPM specifications, the
  changelog and the AppStream metadata.

  Fix a double free in the System Diagnosis SMART check and one in the
  night light's error path."
```

### Step 2 — the push (the first time this code reaches GitHub ✗)

```sh
git push -u origin release/1.2.5
```

**Never** `--force` ✗ ✓.

### Step 3 — watch the CI ✓

The first push triggers three workflows ✗ ✓ — `ci.yml` ✓, `flatpak.yml`'s
manifest job ✓ and `packages.yml` ✓. **Nothing is published until they are
green ✓** ✗.

### Step 4 — the tag (only after the CI is green ✓)

```sh
git tag -a v1.2.5 -m "ASUSCraft 1.2.5"
git push origin v1.2.5
```

### Step 5 — the draft release ✗

```sh
gh release create v1.2.5 --draft \
  --title "ASUSCraft 1.2.5" \
  --notes-file GITHUB_RELEASE_NOTES_1.2.5.md \
  dist/asus-control-center-1.2.5-1.fc45.x86_64.rpm \
  dist/asuscraft-agent-1.2.5-1.fc45.x86_64.rpm \
  dist/asus-control-center_1.2.5_amd64.deb \
  dist/io.github.asuscontrolcenter.asuscraft_1.2.5.flatpak \
  dist/SHA256SUMS.txt
```

**`--draft`** ✗ — the release is created as a draft ✓ and is **not**
published ✗ ✓. The installation instructions are in the notes ✓; they can
also be attached as a separate file if you prefer ✗.

### Step 6 — the publication ✗

A **separate, explicit** approval ✗ — `gh release edit v1.2.5 --draft=false`
✗ ✗.

---

## 5. The stop conditions

| | |
|---|---|
| the commit | **not made** ✓ — waiting for the approval ✓ |
| the push | **not done** ✓ |
| the tag | **not created** ✓ |
| the release | **not created** ✓ — it will be a **draft** ✓ |
| the publication | **not done** ✓ — a separate approval ✗ |
| the force-push | **never** ✓ |
| the upstream's history | **untouched** ✓ |
| `dist/preserved-10.2/` | **untouched** ✓ |
| the backups | **all twelve intact** ✓ |
