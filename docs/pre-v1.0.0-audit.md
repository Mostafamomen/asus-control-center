# ASUSCraft Pre-v1.0.0 Audit Report

**Date:** 2026-11-03  
**Branch:** `dev/0.6` (HEAD `efd5607`) + uncommitted working-tree changes  
**Scope:** Full source audit — privileged helper, polkit, C++ control/probe/UI layers,
auth module, GPU/MUX logic, packaging, documentation  
**Method:** Source-code inspection, static analysis, logic review (no hardware writes)

---

## 1. Executive Summary

The project is in good shape for a v1.0.0 release. The privilege model is sound: every
write goes through a whitelist-only Bash helper invoked by polkit, the C++ layer has
matching client-side validation, the auth module uses PBKDF2-HMAC-SHA256 with 600 000
iterations and constant-time comparison, and the GPU/MUX double-write bug fixed in commit
45e5560 is correct. No critical or high-severity issues were found.

The uncommitted working-tree changes (System Manager enhancements, version bump to 1.0.0)
introduce two correctness bugs and one packaging inconsistency that should be resolved before
tagging.

---

## 2. Working-Tree State

Five files are dirty; none should be committed without the fixes below.

| File | Change |
|---|---|
| `Makefile` | `VERSION` 0.9.0 → 1.0.0 |
| `packaging/rpm/asus-control-center.spec` | `Version` 0.6.0 → 1.0.0 |
| `src/main.cpp` | adds `#include "ui/sysmgr.h"`, includes `acc_sysmgr_selftest()` in `--self-test` |
| `src/ui/sysmgr.cpp` | adds Smart Recommendations card, health history, CPU/RAM cards, Quick Actions, `proc_timer_cb` guard |
| `src/ui/sysmgr.h` | header update for new symbols |

---

## 3. Findings

### MEDIUM

#### M1 — Memory leak in `run_health()` (sysmgr.cpp)

`g_path_get_dirname()` returns a freshly-allocated string but the return value is passed
directly to `g_mkdir_with_parents` and discarded without being freed.

```cpp
// sysmgr.cpp — run_health(), health history section
gchar *hist = g_build_filename (...);
g_mkdir_with_parents (g_path_get_dirname (hist), 0700);   // ← leak
```

**Fix:**
```cpp
gchar *hist_dir = g_path_get_dirname (hist);
g_mkdir_with_parents (hist_dir, 0700);
g_free (hist_dir);
```

#### M2 — RPM spec `%changelog` ends at 0.6.0

`packaging/rpm/asus-control-center.spec` was not updated when the Makefile advanced to
0.9.0, so the spec jumped from 0.6.0 → 1.0.0 in one step with a missing 0.9.0 entry.
`rpmlint` and downstream rebuild tools expect every version to have a changelog entry.

**Fix:** add a `0.9.0` and a `1.0.0` entry to the `%changelog` section before tagging.

#### M3 — `s_last_exit` not reset on spawn failure (ctl.cpp)

When `g_subprocess_launcher_spawnv()` fails (helper not found, fork error), `acc_ctl_run()`
returns `FALSE` but `s_last_exit` keeps the previous invocation's value. Any UI code that
calls `acc_ctl_last_exit()` after a spawn failure will read a stale code and may show a
misleading toast (e.g. "authorization denied" when the helper binary is simply missing).
This was noted in the independent second-pass audit (SEC-04) and is still present.

```cpp
// ctl.cpp — acc_ctl_run()
if (!p) {
    if (err) *err = g_strdup (e ? e->message : "spawn failed");
    g_clear_error (&e);
    return FALSE;          // s_last_exit still holds the last call's value
}
```

**Fix:** add `s_last_exit = -1;` (or a dedicated `ACC_CTL_EXIT_SPAWN_FAILED` sentinel)
immediately before or after the early return.

---

### LOW

#### L1 — System autostart directory never scanned (sysmgr.cpp)

`build_startup_text()` allocates a two-element `dirs` array but only sets `dirs[0]` (user
autostart). `dirs[1]` is `NULL`; the guard `if (!d) continue` silently skips the second
iteration. The "System" label code path is never reached.

```cpp
const gchar *dirs[] = { NULL, NULL };
gchar *user_dir = g_build_filename (..., "autostart", NULL);
dirs[0] = user_dir;
// dirs[1] is never set — the "System:" branch is dead code
```

This is a logic / dead-code issue, not a crash or security bug. Either set `dirs[1]` to
`"/etc/xdg/autostart"` to also count system entries, or simplify the loop to a single
iteration and remove the two-element pattern.

#### L2 — `health_history.txt` file permissions may be too broad

The config directory is created as 0700, but `g_file_set_contents()` creates the file under
the process umask. On a default system (umask 0022) the resulting file is 0644, readable by
other local users. The content (disk%, RAM%, battery health, failed-service flag) is
low-sensitivity telemetry, but the 0700 directory implies the intent was to keep it private.

**Fix:** open and write with explicit 0600:
```cpp
g_file_set_contents_full (hist, entry->str, -1,
                          G_FILE_SET_CONTENTS_CONSISTENT, 0600, NULL);
```
(`g_file_set_contents_full` is available from GLib 2.66, well within the project's
dependency range.)

#### L3 — Shell commands use `G_SPAWN_SEARCH_PATH` (sysmgr.cpp, softcenter.cpp)

Both `run_cmd()` helper functions pass `G_SPAWN_SEARCH_PATH`, meaning `sh`, `ps`, `dnf`,
etc. are resolved from the user's `PATH`. No user-controlled data is interpolated into the
command strings, so this is not an injection risk. However it is inconsistent with the
rest of the codebase: `gpu.cpp` hardcodes `/usr/bin/nvidia-smi`, `muxreq.cpp` hardcodes
`/usr/bin/journalctl` and `/usr/bin/timeout`, and `ctl.cpp` hardcodes `/usr/bin/pkexec`.

Consider adding a shared helper that requires an absolute path and rejects relative ones,
or at minimum move to absolute paths for `sh` (`/bin/sh`) in the two `run_cmd` wrappers.

#### L4 — Implicit network requests on Software Center page open (softcenter.cpp)

`scan_updates()` runs `fwupdmgr get-updates` and `flatpak remote-ls --updates` when the
Software Center page is first opened, without any explicit user action. Both tools may make
network requests. `dnf check-update -q` contacts the configured RPM repositories.

These run on background GLib threads so the UI stays responsive, but the network activity
is unexpected and undisclosed. A "Check for updates" button (or a disclosure note in the
page header) would align with the rest of the app's read-only-by-default posture.

#### L5 — Trailing semicolon in `ctl.cpp` line 74

```cpp
gboolean ok = g_subprocess_wait_check (p, NULL, &e) ;   // extra space + semicolon
```
Cosmetic — no semantic effect — but inconsistent with the rest of the file's style.

#### L6 — `README.md.backup` tracked in the repository

`README.md.backup` is a stale working file in the repo root. It should be deleted and added
to `.gitignore` (or just deleted, if the intent is that it never recurs).

---

## 4. GPU/MUX Logic Audit

The commit 45e5560 ("GPU: fix MUX double-write, verify switch after reboot, honest
Integrated status") correctly resolves the double-write race documented in `docs/gpu-mux.md`
§9. The current flow is:

1. Helper writes `gpu_mux_mode`; confirms by read-back **or** `pending_reboot = 1` (exit 0).
   Exit 5 means "written but neither confirmed" — a new status the UI must handle (see §4.1).
2. App records the request with `boot_id` in `~/.config/asus-control-center/gpu-request.ini`.
3. On next boot, `acc_muxreq_check()` compares the recorded boot to the running boot:
   - same boot → `PENDING` (reboot not yet done);
   - different boot, live mode matches request → `VERIFIED`;
   - different boot, live mode differs → `FAILED` (with journal reason if available).
4. `acc_muxreq_reason_from_log()` correctly picks `asusd`'s error line over the kernel line.
5. All six `acc_muxreq_classify()` cases are covered by unit tests (`--check`).

### 4.1 — Helper exit 5 needs explicit UI handling

When neither the read-back nor `pending_reboot` confirms the write, the helper exits 5.
`g_subprocess_wait_check()` returns `FALSE` for exit 5, so `acc_ctl_run()` sets `*err` to a
generic message. If the GPU page does not inspect `acc_ctl_last_exit() == 5` specifically,
the user sees a generic "action failed" toast instead of "written — please reboot to
confirm". Verify that `pages.cpp` maps exit code 5 to the "reboot required" notice path,
not the error path.

---

## 5. Security Review

All findings from the M3 independent audit (2026-10-30) have been addressed in-code,
with one exception (M3 / `s_last_exit`, now listed as M3 above). No new privilege-escalation
or injection surface was found. Confirmed-sound practices:

| Practice | Status |
|---|---|
| Helper: whitelist-only (no `eval`, no variable interpolation into commands) | ✓ |
| `ctl.cpp`: client-side action + value validation before spawning | ✓ |
| `armoury_file()`: attribute name allowlist (defense in depth) | ✓ |
| `wmi_file()`: two known paths only (no broad glob) | ✓ |
| polkit: `allow_gui` annotation removed; `auth_admin_keep` for active session | ✓ |
| Argv arrays throughout (no shell, no `popen`) | ✓ |
| PBKDF2-HMAC-SHA256 × 600 000 iterations; constant-time comparison | ✓ |
| Password file: O_NOFOLLOW, temp-then-rename, 0600 | ✓ |
| Boot-bound sessions (sign-in lost on reboot) | ✓ |
| GPU MUX: no automatic write; confirmation required; Integrated gated | ✓ |

---

## 6. Test Coverage

`make test` runs three passes:

| Pass | What it covers |
|---|---|
| `--check` | MUX request lifecycle (6 cases), journal reason parsing (4 cases), supergfxd leftover detection (5 cases), Integrated mode gate (1 case) |
| `bash -n data/acc-helper` | Bash syntax check only |
| `--self-test` | PBKDF2 test vector, System Manager history logic, hardware spec report (read-only) |

**Gaps:**

- The helper's argument-count and whitelist logic has no automated test. A simple shell test
  (`bash data/acc-helper; bash data/acc-helper "set-charge" "999"`) should be added to
  `make test`.
- `acc_ctl_helper_stale()` has no unit test; if the substring markers change, the staleness
  check could silently break.
- `acc_gfx_integrated_supported()` always returns `FALSE`. The gate is tested but the
  return value path for `TRUE` (future verified models) has no test.

---

## 7. Documentation

- `docs/gpu-mux.md` is accurate and complete through §10 (the 2026-11-02 correction).
- `docs/security.md` covers M2 through M5 from the second-pass audit and documents the
  `auth_admin_keep` trade-off.
- `docs/security-audit-second-pass.md` remains as-is (a point-in-time record); it does not
  need updating.
- The man page (`docs/asus-control-center.1`) should be checked for version number — it may
  still reference an earlier release.

---

## 8. Packaging

- `dist/` contains pre-built RPMs for three versions (0.5.0, 0.9.0, 1.0.0). The 1.0.0
  RPM was built before the version was committed. Consider whether built artifacts belong
  in the repo or should live only in GitHub Releases. If kept, a `.gitattributes` rule to
  mark them as binary (skipping diffs) would help.
- `debian/changelog` has a 0.6.0 entry; if a Debian package is planned for 1.0.0 it needs
  a matching entry.

---

## 9. Recommended Actions Before Tagging v1.0.0

| Priority | Action |
|---|---|
| Must | Fix memory leak in `run_health()` (M1) |
| Must | Add `%changelog` entries for 0.9.0 and 1.0.0 in the RPM spec (M2) |
| Must | Reset `s_last_exit` on spawn failure in `ctl.cpp` (M3) |
| Must | Verify pages.cpp maps helper exit 5 to the "reboot required" notice (§4.1) |
| Should | Fix dead-code `dirs[1]` in `build_startup_text()` (L1) |
| Should | Use `g_file_set_contents_full(..., 0600)` for `health_history.txt` (L2) |
| Should | Remove `README.md.backup` and add it to `.gitignore` (L6) |
| Consider | Add a "Check for updates" button to Software Center (L4) |
| Consider | Add shell-level tests for the helper whitelist to `make test` |

---

*This report covers the repository state as of HEAD `efd5607` plus the uncommitted
working-tree changes visible in `git diff HEAD`. Nothing was written to hardware or
committed during this audit.*
