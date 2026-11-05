# ASUS Control Center — Independent Security Verification (Second Pass)

**Date:** 2026-10-30
**Auditor:** Independent adversarial review
**Scope:** Entire repository — privileged execution, polkit, helper, C++ control layer, installer, login, D-Bus clients
**Method:** Source-code inspection, static analysis, targeted testing (no destructive hardware operations)

---

## 1. Executive Summary

A second-pass adversarial audit was performed on the ASUS Control Center repository. The
previous audit reported several security fixes, but **the installed system files do not
reflect those fixes** — the deployed helper and polkit policy are the pre-fix versions.
Additionally, the repo's `wmi_file()` fallback was inadvertently broken into a no-op loop,
and several defense-in-depth gaps remain in the C++ control layer.

**Key findings:**
- **High**: Installed helper is the old (unhardened) version despite the repo containing fixes. The app's staleness check cannot distinguish old from new.
- **Medium**: `auth_admin_keep` caches authentication for the session.
- **Medium**: `wmi_file()` fallback loop is a no-op in the repo version (functional regression).
- **Low**: `acc_ctl_run()` does not validate the `action` parameter against a whitelist (fixed in repo but not deployed).
- **Low**: Helper exit code 4 conflates "permission denied" with "firmware refused".
- **Low**: `acc_ctl_run()` does not reset `s_last_exit` on spawn failure.
- **Informational**: Multiple good practices confirmed (argv arrays, no shell, PBKDF2, constant-time comparison, O_NOFOLLOW, boot-bound sessions).

---

## 2. Findings

### SEC-01: Installed Helper/Polic y Are Outdated (High)

**Severity:** High
**Affected files:** System files (not in repo): `/usr/libexec/asus-control-center/acc-helper`, `/usr/share/polkit-1/actions/io.github.asuscontrolcenter.AsusControlCenter.policy`
**Repo files:** `data/acc-helper`, `data/io.github.asuscontrolcenter.AsusControlCenter.policy`, `src/control/ctl.cpp` (`acc_ctl_helper_stale()`)

**Description:** The repository contains hardened versions of the helper and policy, but the
system-installed copies are the pre-fix versions:
- Installed helper lacks argument-count validation, armoury attribute allowlist, and the
  restricted `wmi_file` glob (still uses `/sys/devices/platform/asus*`).
- Installed policy still contains `allow_gui=true`.

The app's `acc_ctl_helper_stale()` only checks for the substring `"armoury_file"` in the
helper file. Both old and new helpers contain this string, so the check returns `FALSE`
("not stale") for the old helper — the app believes the hardened helper is present when it
is not.

**Reproduction:**
```bash
diff data/acc-helper /usr/libexec/asus-control-center/acc-helper
# shows 12+ lines of differences
```

**Remediation:** Run `./install.sh` to deploy the hardened versions. Consider strengthening
`acc_ctl_helper_stale()` to check for a version marker (e.g., a comment like `# helper v3`)
rather than a function name.

---

### SEC-02: `auth_admin_keep` Caches Authentication (Medium)

**Severity:** Medium
**Affected file:** `data/io.github.asuscontrolcenter.AsusControlCenter.policy`

**Description:** The policy uses `auth_admin_keep` for active sessions, which caches the
administrator password for the duration of the session. If the user walks away from an
unlocked desktop, an attacker with physical access can invoke any privileged hardware
operation without re-entering a password.

**Trade-off:** Changing to `auth_admin` would require a password for every control change,
significantly degrading usability. The current setting is a reasonable trade-off for a
personal laptop but should be documented.

**Remediation:** Document the trade-off in `docs/security.md` (done). Users on shared
machines can manually edit the policy to use `auth_admin`.

---

### SEC-03: `wmi_file()` Fallback Loop Is a No-Op (Medium)

**Severity:** Medium (functional regression, not security)
**Affected file:** `data/acc-helper` (repo version, lines ~70-78)

**Description:** The repo's `wmi_file()` function has a second loop that iterates over the
same two paths but does nothing (empty body with `:`). The original `asus*` glob fallback
was removed but not replaced with a working alternative. On hardware where the WMI node
is at a non-standard path, the helper will fail to find the sysfs node.

**Reproduction:** On a machine where the armoury interface is absent and the WMI node is
at `/sys/devices/platform/asus-nb-wmi/`, the helper works (first loop catches it). But if
the node is at a path like `/sys/devices/platform/asus-wmi/` (second known path), it also
works. Only unusual paths (e.g., `/sys/devices/platform/asus-nb-wmi-2/`) would fail.

**Remediation:** Restore the `asus*` glob fallback but validate that the discovered path
is a regular file (not a symlink) and matches a known pattern. Or remove the dead loop
and document the limitation.

---

### SEC-04: `acc_ctl_run()` Does Not Reset `s_last_exit` on Spawn Failure (Low)

**Severity:** Low
**Affected file:** `src/control/ctl.cpp`, function `acc_ctl_run()`

**Description:** If `g_subprocess_launcher_spawnv` fails (returns NULL), `s_last_exit` retains
its previous value. Callers that check `acc_ctl_last_exit()` after a failed spawn would
see a stale exit code.

**Reproduction:** Call `acc_ctl_run()` when pkexec is not installed. `s_last_exit` would
retain whatever value it had from a previous call.

**Remediation:** Set `s_last_exit = -1` before attempting to spawn. (Note: the repo's current
version already does this in the modified `acc_ctl_run`.)

---

### SEC-05: Helper Exit Code 4 Conflates Multiple Conditions (Low)

**Severity:** Low
**Affected files:** `data/acc-helper` (function `set_val`), `src/ui/pages.cpp` (GPU page)

**Description:** The helper's `set_val` function returns exit code 4 for both:
1. Write failure (permission denied, read-only file)
2. Read-back mismatch (firmware refused the value)

The C++ UI interprets exit 4 as "firmware refused the value" (ACC_T_ERR_FW), which is
misleading when the actual cause is a permission error.

**Remediation:** Use distinct exit codes: 4 for firmware refusal, 5 for permission denied.
Or have the helper print a diagnostic message to stderr that the C++ side can parse.

---

### SEC-06: `acc_ctl_run()` Action Not Validated Before pkexec (Low, Fixed in Repo)

**Severity:** Low
**Affected file:** `src/control/ctl.cpp`, function `acc_ctl_run()`

**Description:** The original code passed the `action` parameter directly to pkexec without
validation. While the helper itself validates actions, client-side validation provides
defense in depth. The repo version now includes a whitelist check.

**Status:** Fixed in repo (commit 4462328). Installed system still runs old code (see SEC-01).

---

### SEC-07: `armoury_file()` Path Interpolation Without Validation (Low, Fixed in Repo)

**Severity:** Low
**Affected file:** `data/acc-helper`, function `armoury_file()`

**Description:** The original code interpolated `$1` (attribute name) directly into a sysfs
path without validation. If an attacker could control the attribute name, they could
potentially read/write arbitrary paths. The repo version now validates against an allowlist.

**Status:** Fixed in repo (commit 4462328). Installed system still runs old code (see SEC-01).

---

### SEC-08: `acc_ctl_available()` Does Not Check Ownership (Informational)

**Severity:** Informational
**Affected file:** `src/control/ctl.cpp`, function `acc_ctl_available()`

**Description:** The function only checks if pkexec and the helper exist (via `acc_path_exists`),
not if they're owned by root or have correct permissions. An attacker with write access to
the helper path could replace it with a malicious version.

**Mitigation:** The helper is installed in `/usr/libexec/asus-control-center/` which is
root-owned. Standard Linux permissions prevent non-root users from modifying files there.

**Remediation:** Optionally verify the helper's ownership and permissions in `acc_ctl_available()`.

---

### SEC-09: `g_subprocess_wait_check` and Exit Status (Informational)

**Severity:** Informational
**Affected file:** `src/control/ctl.cpp`, function `acc_ctl_run()`

**Description:** `g_subprocess_wait_check()` returns FALSE for non-zero exit statuses, but
the code separately reads `g_subprocess_get_exit_status()` to get the specific exit code.
This is correct behavior but relies on the helper always exiting with a defined code.
If the helper is killed by a signal, `g_subprocess_get_if_exited()` would return FALSE
and `s_last_exit` would be -1 (or stale).

**Remediation:** Already handled in the repo's updated `acc_ctl_run` which checks
`g_subprocess_get_if_exited()`.

---

### SEC-10: Login Password Hashing (Informational — Good Practice)

**Severity:** Informational
**Affected file:** `src/user/user.cpp`

**Description:** The login system uses PBKDF2-HMAC-SHA256 with 600,000 iterations (OWASP 2023
recommendation). It includes:
- Constant-time comparison (`equal_ct`)
- Boot-bound sessions (sign-in doesn't outlive a reboot)
- Exponential backoff on failed attempts
- In-place upgrade from legacy SHA-256 to PBKDF2
- Secure file permissions (0600) with O_NOFOLLOW
- Atomic writes (temp file + rename + fsync)

No security issues found in this module.

---

### SEC-11: D-Bus Client Security (Informational — Good Practice)

**Severity:** Informational
**Affected file:** `src/control/asusd.cpp`

**Description:** The asusd D-Bus client uses:
- Fixed system-bus connection
- Fixed object paths and interface names
- Fixed method names and parameter types
- No shell, no command line
- Timeout on all calls (2500ms)
- Read-back verification after writes

No security issues found in this module.

---

### SEC-12: Softdim Installation (Informational — Good Practice)

**Severity:** Informational
**Affected file:** `src/control/softdim.cpp`

**Description:** The softdim installer:
- Writes only to the user's own `~/.local/share/gnome-shell/extensions`
- Copies a fixed list of files from a bundled folder
- Runs `glib-compile-schemas` and `gnome-extensions` with fixed argv
- No shell, no pkexec, no sudo
- Uses `g_file_copy` with overwrite flag

No security issues found in this module.

---

### SEC-13: Backlight Device Discovery (Informational — Good Practice)

**Severity:** Informational
**Affected file:** `src/control/backlight.cpp`

**Description:** The backlight device discovery:
- Opens `/sys/class/backlight` with `g_dir_open`
- Validates device names (no `/` or `.` prefix)
- Uses logind D-Bus for brightness changes (no shell, no pkexec)
- One fixed D-Bus method on the system bus

No security issues found in this module.

---

### SEC-14: `spawn_in_terminal` Uses `bash -c` (Informational)

**Severity:** Informational
**Affected file:** `src/ui/login.cpp`, function `spawn_in_terminal()`

**Description:** The function uses `bash -c cmd` where `cmd` is a fixed template built from
the absolute executable path. While the command string is not user-controlled, using
`bash -c` is inherently riskier than using `execvp` directly. The current implementation
is safe because `cmd` is always a compile-time constant string, but future modifications
could introduce injection.

**Remediation:** Consider using `execvp` directly instead of `bash -c` for the terminal
launch, or ensure the command string remains a compile-time constant.

---

## 3. Tests Performed

| Test | Result |
|------|--------|
| `make clean && make` | ✅ 0 warnings, 0 errors |
| `./asus-control-center --self-test` | ✅ All controls pass |
| `xmllint --noout data/...policy` | ✅ Policy XML valid |
| `git diff --check` | ✅ No whitespace errors |
| `bash -n data/acc-helper` | ✅ Helper syntax valid |
| Missing args: `bash data/acc-helper` | ✅ Prints usage, exits 1 |
| Invalid action: `bash data/acc-helper "evil"` | ✅ Exits 1 |
| Invalid value: `bash data/acc-helper "set-charge" "999"` | ✅ Exits 2 |
| Valid action (no perms): `bash data/acc-helper "set-kbd" "2"` | ✅ Exits 4 |
| `diff repo/helper installed/helper` | ❌ Differences found (installed is old version) |
| `diff repo/policy installed/policy` | ❌ Differences found (installed has allow_gui) |

---

## 4. Remaining Risks

1. **Deployed system runs old helper/policy** (High): The installed system files predate
   the security fixes. Run `./install.sh` to deploy the hardened versions.

2. **`auth_admin_keep` session caching** (Medium): Authentication is cached for the session.
   Acceptable for personal use but should be documented.

3. **Bash helper** (Low): The helper is written in Bash. While all values are whitelisted,
   Bash parsing could have edge cases. A future rewrite in C would eliminate this risk.

4. **Exit code 4 conflation** (Low): Exit code 4 means both "permission denied" and
   "firmware refused". The UI interprets it as "firmware refused" which may be misleading.

5. **Staleness check too coarse** (Low): `acc_ctl_helper_stale()` only checks for the
   presence of "armoury_file" string, not for the new security features.

6. **`bash -c` in login.cpp** (Informational): Uses `bash -c` with a fixed template.
   Safe currently but could become a risk if modified.

---

## 5. Final Release-Readiness Assessment

**From a security perspective, the repository code is ready for public release** with the
following caveats:

1. The install script must be run to deploy the hardened helper and policy.
2. The `auth_admin_keep` setting should be documented as a usability/security trade-off.
3. The remaining risks (Bash helper, exit code conflation) are low-impact and should be
   addressed in future releases.

**Recommendation:** Run `./install.sh` on the developer's machine, verify the deployed
files match the repo, and then proceed with the GitHub release.