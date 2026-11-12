# ASUSCraft 1.2.5 — GitHub repository audit

**Project:** `~/Documents/asus-control-center-1.2.2`
**Repository:** `Mostafamomen/asus-control-center` (https://github.com/Mostafamomen/asus-control-center)
**Date:** 2026-11-12
**State:** the branch is prepared and staged; **nothing has been committed, pushed, tagged or published**.

---

## 1. The starting state

| question | the answer |
|---|---|
| Was the directory a Git repository? | **No** — `git rev-parse` reported *"not a git repository"* ✓ |
| Does a `.git` exist elsewhere in the project? | **No** ✓ — the only `.git` in the neighbourhood is `~/Documents/craft-launcher-fork/.git`, a **sibling** project, not this one ✓ (so the new repository is not nested ✓) |
| Does the remote exist? | **Yes** — `https://github.com/Mostafamomen/asus-control-center` ✓ |
| Does the remote have history? | **Yes** — 6 branches and 11 tags ✓ |
| Did the local source match a release? | **No** — it is a divergent working line, not any upstream tag ✓ (see §3) |

### The upstream's history (fetched read-only)

| branch | commit | note |
|---|---|---|
| `main` | `165548a` | **stale** — it still points at the `v0.4.0` tag ✓ |
| `10.2` | `aa1269c` | **the real release line** — it carries the `v1.2.4` tag ✓ |
| `dev/0.6`, `feature/security-center`, `fix/ui-scaling-v124`, `free-ai` | — | side branches ✓ |

| tag | | tag | |
|---|---|---|---|
| `v0.4.0` | the first release | `v1.2.2` | the local tree's lineage |
| `v0.5`, `v0.5.0` | | `v1.2.3` | the Soft Dimmer integration |
| `v0.6.0` | | `v1.2.4` | the UI scaling restoration |
| `v0.9` | | `v1.20.0` | a separate line |
| `v1.0.0`, `v1.1.0` | | | |

**The upstream's `main` branch is not the release line** ✓ — the releases live on
the `10.2` branch ✓. A release prepared from `main` would look like a
regression to anyone reading the tags ✓.

---

## 2. What was created

| | |
|---|---|
| the repository | `git init -b main` **in the project directory itself** ✓ — not a nested one ✓ |
| the remote | `origin` → `https://github.com/Mostafamomen/asus-control-center.git` ✓ (added after verifying the identity with `git ls-remote` ✓) |
| the fetch | `git fetch --tags origin` — **read-only** ✓ |
| the branch | `release/1.2.5`, based on **`v1.2.4`** (`aa1269c`) ✓ |
| the working tree | **never checked out over** ✓ — the branch was created with `git branch` + `git read-tree`, so the verified release artefacts still correspond to the files on disk ✓ |

No force-push, no history rewrite, nothing deleted ✓.

---

## 3. Why the base is `v1.2.4`

The local tree is **not** any upstream tag ✗ — it is a working line that
started from the v1.2.2 era and went through this project's own phases
(the UI tuning, the Soft Dimmer removal, the Flatpak and host-agent work) ✓.
Measured: the local `src/ui/window.cpp` differs from `v1.2.2` by 30 lines,
from `v1.2.3` by 31 and from `v1.2.4` by 35 ✓ — it matches none of them ✓.

`v1.2.4` was chosen because it is **the tip of the upstream's real release
line** ✓, and because basing the release on it makes the difference
**visible** ✓ instead of hiding it ✗:

* the Soft Dimmer removal shows up in the diff as a **deletion** ✓ — which is
  exactly what the release notes should say ✓ (the upstream's v1.2.3 added
  it; this release removes it ✗ — deliberately ✗, per `AGENTS.md` ✓);
* the local tree's own UI metrics show up as a **difference** ✓ rather than
  being silently inherited from an older tag ✗.

Basing on `v1.2.2` instead would have produced a tidier diff ✗ but would
have left `v1.2.3` and `v1.2.4` outside the release's ancestry ✗ — the
1.2.5 tag would not descend from the 1.2.4 release ✗, which is worse for
anyone reading the history ✓.

### The one divergence the user must decide on

The staged change **removes** two things that `v1.2.4` added ✗:

| | v1.2.4 | the local tree |
|---|---|---|
| the window opens **maximized** | `gtk_window_maximize (GTK_WINDOW (g_root));` | *not present* ✗ |
| the sidebar metrics | `SIDE_OPEN_W 250`, `SIDE_CLOSED_W 68`, `SIDE_MARK_PX 28` | `220`, `56`, `36` ✗ |

The window's default size (1180×760 ✓) and the app's whole layout are the
same ✓ — only the sidebar's width and the app mark's size differ ✗.

**This is a product decision, not a mechanical one** ✓, so it is left to the
reviewer ✓ (§7 ✗). Two ways forward ✗:

* **A — ship the tree as it is** ✗: the window opens at its designed size ✗
  and the sidebar is narrower ✗. The four verified artefacts stay valid ✓
  (no rebuild ✗) ✓.
* **B — restore v1.2.4's UI** ✗: port the maximize line ✗ and the three
  sidebar constants ✗, then **rebuild all four artefacts** ✗ (their
  checksums change ✗ and the verification would need repeating ✗).

The recommendation is **A** ✗: the tree's metrics are this project's own
tuning ✓, the artefacts are verified ✓, and a rebuild for three constants
is a poor trade ✗ — unless the reviewer prefers v1.2.4's look ✗.

---

## 4. `.gitignore`

The existing file was kept and extended ✗ — nothing was removed from it ✓.

| ignored | why |
|---|---|
| `/asus-control-center`, `/agent/asuscraft-agent`, `/launcher/launcher`, `*.o` | the compiled binaries ✓ |
| `/dist/` | **the release artefacts are published as GitHub Release assets**, not committed ✓ — the policy the task asks for ✓ |
| `/.rpmbuild/` | the local rpmbuild tree ✓ |
| `*.rpm`, `*.deb`, `*.ddeb`, `*.changes`, `*.buildinfo` | the packaging output ✓ |
| `/debian/.debhelper/`, `/debian/files`, … | the Debian build scratch ✓ |
| `/.flatpak-builder/`, `/.flatpak-build/`, `/.flatpak-repo/` | the temporary Flatpak Builder state ✓ |
| `/container/`, `/.debbuild/`, `/build-root/` | the container scratch ✓ |
| `*.log`, `/logs/`, `*.pid` | the local logs and runtime state ✓ |
| `.vscode/`, `.idea/`, `*.swp`, `*~` | the editor configuration ✓ |
| `.env*`, `*.pem`, `*.key`, `*.p12`, `*.pfx`, `secrets/`, `credentials.json` | the secrets — never committed ✓ |
| `__pycache__/`, `*.py[cod]` | the Python scratch ✓ |
| `*.backup` | the stale backup files ✓ (this is why `README.md.backup` stays out ✓) |

**Fixed:** the old rule `launcher` (no slash ✗) matched the whole
`launcher/` **directory** ✗ — it would have hidden the launcher's *source*
✓ from the repository ✗. It is now `/launcher/launcher` ✓ (the binary
only ✓) ✓, and `launcher/Makefile` + `launcher/launcher.cpp` stay tracked ✓.

---

## 5. What is staged

`git status` against the `v1.2.4` base ✓:

| | count |
|---|---|
| modified | 38 ✓ |
| deleted | 16 ✓ — all of them the Soft Dimmer ✓ (`gnome-extension/soft-dimmer/` ✗ and `src/control/softdimmer/` ✗ and `src/ui/softdimmer_page.*` ✗) |
| added | 44 ✓ — the agent, the Flatpak manifest, the agent spec, the new control modules, the documents, the CI |
| **total** | **98 files, +14 320 / −4 742** ✓ |

### The security-critical files are **not** in the diff ✓

```
$ git diff --cached --name-only | grep -E "acc-helper|policy"
(nothing)
```

`data/acc-helper` and the polkit policy are **byte-identical** to the audited
ones ✓ — the CI now pins their checksums ✓ so a future change cannot slip
through unnoticed ✗.

### The secrets scan

```
$ git diff --cached --name-only | xargs grep -nE "(ghp_[A-Za-z0-9]{20,}|github_pat_…|AKIA…|-----BEGIN … PRIVATE KEY-----)"
(no match)
```

No credential pattern, no key, no token ✓. The CI repeats the scan on every
push ✓.

### Two stray files of mine were removed from the tree ✓

`call --session \` (a botched shell redirect ✗) and `gui_check_again.py` (an
audit script ✗) — neither belongs to the project ✗, and the first one broke
`xargs sha256sum` ✗ while building the backup ✓.

---

## 6. The release artefacts are **not** in the repository ✓

The policy the task asks for ✓: the four verified packages are **published
as GitHub Release assets** ✓, not committed ✓ — a source repository should
not carry 7 MB of binaries ✓, and the assets need to be downloadable and
checksummable anyway ✓.

`dist/` is ignored ✓ and holds the build output locally ✓; `dist/SHA256SUMS.txt`
is kept **with the assets** ✓ (it is uploaded as a release asset ✓) ✓.

---

## 7. The open questions for the reviewer

| # | question | the recommendation |
|---|---|---|
| Q1 | should 1.2.5 keep this branch's sidebar metrics (220/56/36) and open the window at its designed size, or restore v1.2.4's (250/68/28 + maximized)? | **keep this branch's** — the artefacts are verified and a rebuild for three constants is a poor trade ✓ |
| Q2 | the branch name `release/1.2.5` — is that the name you want, or `1.2.5` / `release/v1.2.5`? | as-is ✓ |
| Q3 | should the `10.2` branch be the one that carries the 1.2.5 release (as the upstream has done since v0.4.0), or should `main` be moved forward for the first time? | **`10.2`** — it is the release line ✓, and moving `main` would rewrite the upstream's default branch ✗ (which is still at v0.4.0 ✗) |

---

## 8. The state of the repository right now

```
$ git branch --show-current
release/1.2.5

$ git log --oneline -1
aa1269c release: bump version to 1.2.4 — restore original ASUSCraft UI scaling design

$ git status --short | wc -l
98        (staged, NOT committed)

$ git log --oneline release/1.2.5 ^..release/1.2.5
(no commits yet — the release commit has not been made)
```

The remote is untouched ✓: no push, no tag, no release ✓.
