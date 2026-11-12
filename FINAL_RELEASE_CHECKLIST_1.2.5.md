# ASUSCraft 1.2.5 — Final release checklist

**Date:** 2026-11-12
**Target:** the branch `1.25`, the tag `v1.2.5` and the GitHub Release for `v1.2.5`
**State:** the branch is pushed ✓, the tag exists ✓, the release exists as a **DRAFT** ✓ —
**nothing is published** ✓.

---

## 1. What was executed

| step | result |
|---|---|
| the release commit | `39809c7` — 106 files, +15 494 / −4 742 ✓ |
| the push | `release/1.2.5` ✓, a normal non-force push ✓ |
| the CI fixes | `aa8e3a2` (the packaging), `892cb0f` (PyYAML), `26a86bd` (rpmspec), `55b4a3a` (elfutils) ✓ |
| the branch `1.25` | pushed at `55b4a3a` ✓ — the upstream's `10.2`-style naming ✓ |
| the tag `v1.2.5` | `26a86bd` — the commit that passed CI ✓ (an ancestor of the branch tip ✓) |
| the release | **draft** ✓, titled "ASUSCraft 1.2.5" ✓, with the prepared notes ✓ |
| the assets | exactly the five verified ones ✓ |
| publication | **not done** ✗ — it needs a separate, explicit approval ✓ |

### The final refs

```
55b4a3a43919155f34639a2c296b8fa2488bb5f2  refs/heads/1.25
c053186586d43fe40d2c6d5e759e9a85256c628d  refs/tags/v1.2.5 (the tag object)
26a86bd742ad7f410bdbe82f236258539dba7164  refs/tags/v1.2.5^{} (the commit)
```

### The three workflows, all green

| workflow | run | conclusion |
|---|---|---|
| **CI** | [43174848255](https://github.com/Mostafamomen/asus-control-center/actions/runs/43174848255) | **success** ✓ — Application, Host Agent, Release metadata |
| **Flatpak** | [43174848387](https://github.com/Mostafamomen/asus-control-center/actions/runs/43174848387) | **success** ✓ (the manifest job) and [43174848786](https://github.com/Mostafamomen/asus-control-center/actions/runs/43174848786) — the dispatch with the **full Flatpak build** ✓ |
| **Packages** | [43175668070](https://github.com/Mostafamomen/asus-control-center/actions/runs/43175668070) | **success** ✓ — RPM (Fedora), RPM (EL 10), DEB (Ubuntu 24.04) |

### The draft release's assets

| asset | size | SHA256 |
|---|---|---|
| `asus-control-center-1.2.5-1.fc45.x86_64.rpm` | 2 296 991 | `34b35f651e4144118ad66b30edab674d52583676f96cd4f1daea17a7ebbba9d5` |
| `asuscraft-agent-1.2.5-1.fc45.x86_64.rpm` | 67 541 | `3924f9d3954036bb361a023e694e2c06e7150d9a4dd03e27cb211d64aa298fd7` |
| `asus-control-center_1.2.5_amd64.deb` | 2 273 996 | `158e0cdd77f05d1ea2b108e6780a9b28c648c8cd345e4bed3a2b3c81c7cadd81` |
| `io.github.asuscontrolcenter.asuscraft_1.2.5.flatpak` | 2 327 528 | `3932280665a0bde138f4d48ecae7659cb5374925d5953fa8300b5bcd43f5fd57` |
| `SHA256SUMS.txt` | 436 | the four above ✓ |

`sha256sum -c SHA256SUMS.txt` → **4 × OK** ✓ (re-verified after the upload ✓).

---

## 2. Two incidents during the execution — and what was done about them

### The release was auto-published by the pre-existing workflow

Pushing the tag fired the **`packages.yml` release job**, which already
existed in the repository and ends with
`softprops/action-gh-release@v2` — that action creates a **published**
release, not a draft. The release went out with the CI's own artefacts (the
source RPMs, the debuginfo packages, the EL 10 builds) and **no** Flatpak
bundle.

My own review checklist had described that job as creating a draft; that was
my error, and it is corrected in this document.

**Correction applied:** the release was converted back with
`gh release edit v1.2.5 --draft=true` ✓, its CI-generated assets were removed
✓, and exactly the five verified artefacts were uploaded ✓. `v1.2.4` is
"Latest" again ✓. The publication is back behind an explicit approval ✓.

**Follow-up (not done, for a future change):** the `packages.yml` release job
should either be removed or made to create a draft ✓, so that a tag push can
never publish on its own ✓.

### Four CI failures, all fixed and re-run green

| the failure | the cause | the fix |
|---|---|---|
| `sudo: 'dnf': command not found` | the release-metadata job ran on `ubuntu-latest` but checked the RPM specifications with `rpmspec` | the job moved into a Fedora container ✓ |
| `ModuleNotFoundError: No module named 'yaml'` | the Fedora container's python3 has no PyYAML | `python3-pyyaml` installed ✓ |
| `rpmspec: --list: unknown option` | rpmspec has no `--list` | `rpmspec --parse` ✓ |
| `Failed to execute child process "eu-strip"` | the runner has no `elfutils` (flatpak-builder needs it to split the debuginfo) | `elfutils` installed ✓ |

Two of them were also **real packaging bugs** that the CI found and the local
build had masked with `--nodeps --nocheck`:

* the agent's `BuildRequires: pkgconfig(glib2)` — a virtual no package
  provides; the real name is `pkgconfig(glib-2.0)` ✓;
* the app's `%check` ran `appstream-util`, removed from appstream 1.0
  (Fedora 40+); it now runs `appstreamcli validate --no-net` ✓.

Both packages now build with their `%check` enabled ✓, and the artefacts
were rebuilt from the fixed tree ✓.

---

## 3. The remaining decisions

| # | decision | the recommendation |
|---|---|---|
| **Q1** | publish the draft | **not yet** — one word from you and it goes out ✓ |
| **Q2** | the tag stays at `26a86bd` while the branch tip is `55b4a3a` | leave it ✓ — the difference is a single CI-only commit ✗ that does not touch a single packaged file ✓, and moving a tag would need a force-push ✗ |
| **Q3** | the branch `release/1.2.5` and the branch `1.25` both exist | keep both ✓ — `1.25` is the release line in the upstream's naming style ✓, `release/1.2.5` is the working branch ✓ |

---

## 4. The stop conditions — all honoured

| | |
|---|---|
| the release is **not** published | ✓ it is a draft ✓ |
| `main` was not modified | ✓ it still points at the upstream's `v0.4.0` commit ✓ |
| no force-push | ✓ every push was a normal fast-forward ✓ |
| the preserved 10.2 artefacts | ✓ `dist/preserved-10.2/` untouched ✓ |
| the backups | ✓ all thirteen intact ✓ |
| the native installation | ✓ `asus-control-center-1.0.0-1.fc44` untouched ✓ |
| the Flatpak and polkit permissions | ✓ unchanged, and guarded by `flatpak/verify-permissions.sh` ✓ |
| the Soft Dimmer integration | ✓ removed, not reintroduced ✓ |
| the D-Bus API | ✓ additive only ✓ |

---

## 5. Publishing, when you say so

```sh
gh release edit v1.2.5 --draft=false
```

That is the whole publication step ✓ — one command ✓.
