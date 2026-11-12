# Soft Dimmer — Phase 2 live verification (retry)

**Subject:** `soft-dimmer@io.github.softdimmer`
**Installed at:** `/home/mostafa/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer`
**Companion document:** `SOFT_DIMMER_GNOME51_PHASE2_DBUS_REPORT.md`
**Retry date:** 2026-11-11, 10:06–10:09 (+03)
**Environment:** GNOME Shell 51.1 / mutter 51.1 / GJS 1.90.1 / Fedora 45 / kernel 7.3.3-300.fc45

**Headline: the retry is PENDING.** The retry was asked to verify a *fresh*
session, but **no logout/login has taken place** — the GNOME Shell process is
still the one that started at 08:28:38, 74 minutes *before* the fixed file was
written. The timing precondition of step 1 is **not satisfied**, so every
session-dependent check is reported as **PENDING** rather than PASS or FAIL.
The integrity checks, which do not depend on a fresh session, **PASS**.

Nothing was modified: no file, no setting, no extension state.

---

## 0. Verdict summary

| # | Check | Observed | Verdict |
|---|---|---|---|
| V1 | Shell started **after** the fixed file's mtime | Shell started 08:28:38, file written 09:42:04 → **74 minutes too early** | **FAIL (precondition)** |
| V2 | Extension `Enabled: Yes`, `State: ACTIVE` | `Enabled: Yes`, `State: ACTIVE` | **PASS** |
| V3 | Expected D-Bus name owner exists | `io.github.softdimmer` → `:1.16` → PID 14977 | **PASS** |
| V4 | `GetBrightness` returns the expected double | `TypeError: params is not iterable`, exit 1 | **PENDING** (observed failure — old module) |
| V5 | `GetCapabilities` returns a typed `a{sv}` | `TypeError: … not a subclass of GObject_Struct`, exit 1 | **PENDING** (observed failure — old module) |
| V6 | No `params is not iterable` exception | the exception **is** present | **PENDING** (observed failure — old module) |
| V7 | No `GObject_Struct` exception | the exception **is** present | **PENDING** (observed failure — old module) |
| V8 | `GetEnabled` — real boolean semantics | `(true,)`, type `b`, exit 0 — identical for old and new code | **PENDING** |
| V9 | Brightness unchanged | `1.0` before and after | **PASS** |
| V10 | D-Bus owner stable | `:1.16` before and after | **PASS** |
| V11 | Extension still `ACTIVE` | `Enabled: Yes`, `State: ACTIVE` | **PASS** |
| V12 | No source file or setting changed | tree hash-identical to the pre-call snapshot | **PASS** |

**Overall: PENDING** — blocked by V1. The repair itself is unchanged and
remains verified by the isolated suite (98/98, Phase 2 report §7).

---

## 1. V1 — loaded-session timing — **FAIL (precondition)**

### 1.1 Recorded values

```
$ date '+%Y-%m-%d %H:%M:%S %z'
2026-11-11 10:06:46 +0300

$ ps -eo pid,lstart,etimes,comm | grep -E "gnome-shell"
  14977 Wed Nov 11 08:28:38 2026    5887 gnome-shell
  15079 Wed Nov 11 08:28:40 2026    5885 gnome-shell-cal

$ sha256sum .../soft-dimmer@io.github.softdimmer/dbus.js
f05c167a06b577d4007aef3ed46eb25d83b6e254c98fa85508655f2fbe384669  .../dbus.js
[sha exit=0]

$ stat -c 'mtime=%y' .../dbus.js
mtime=2026-11-11 09:42:04.167957360 +0300
```

| Item | Value |
|---|---|
| Current time | `2026-11-11 10:06:46 +0300` |
| Current GNOME Shell PID | **14977** (`/usr/bin/gnome-shell --mode=user`) |
| Shell start time | **`Wed Nov 11 08:28:38 2026`** (epoch 1794374918) |
| Shell uptime | 5 887 s ≈ 1 h 38 min |
| Installed `dbus.js` SHA-256 | `f05c167a06b577d4007aef3ed46eb25d83b6e254c98fa85508655f2fbe384669` — **matches the Phase 2 fixed hash** |
| `dbus.js` mtime | `2026-11-11 09:42:04 +0300` (epoch 1794379324) |
| Comparison | shell start (1794374918) **<** file mtime (1794379324) — the Shell started **4 406 s (74 min) BEFORE** the fixed file existed |

### 1.2 The timing condition is not satisfied

**The Shell process is the very same one that was running before the fix was
written.** It has not been replaced. Four independent sources agree:

**a) The D-Bus owner is still the old process**

```
$ gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
      --method org.freedesktop.DBus.GetNameOwner io.github.softdimmer
(':1.16',)

$ gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
      --method org.freedesktop.DBus.GetConnectionUnixProcessID io.github.softdimmer
(uint32 14977,)

$ ps -o pid,lstart,etimes,args -p 14977
  14977   14609 Wed Nov 11 08:28:38 2026    5911 /usr/bin/gnome-shell --mode=user
```

A logout/login cycle necessarily kills the old `gnome-shell` and starts a new
one with a **new PID** and a **new unique bus name**. Both are unchanged.

**b) The whole session is from 08:28:38**

```
$ ps -eo pid,ppid,lstart,args | grep -iE "gnome-session|gnome-shell|mutter|Xwayland"
  14645   14269 Wed Nov 11 08:28:38 2026  /usr/libexec/gdm-wayland-session /usr/bin/gnome-session
  14663   14645 Wed Nov 11 08:28:38 2026  /usr/libexec/gnome-session-init-worker gnome
  14942   14609 Wed Nov 11 08:28:38 2026  /usr/libexec/gnome-session-ctl --monitor
  14950   14609 Wed Nov 11 08:28:38 2026  /usr/libexec/gnome-session-service --session=gnome
  14977   14609 Wed Nov 11 08:28:38 2026  /usr/bin/gnome-shell --mode=user
  15470   14977 Wed Nov 11 08:28:40 2026  /usr/bin/Xwayland :0 ...
  16393   14977 Wed Nov 11 08:28:41 2026  /usr/libexec/mutter-x11-frames
```

`gdm-wayland-session`, `gnome-session`, `gnome-shell` and `Xwayland` all carry
the identical start timestamp — one uninterrupted session that began at
08:28:38 and has never been torn down.

**c) logind knows of no later session**

```
$ loginctl list-sessions --no-pager
SESSION  UID USER    SEAT  LEADER CLASS   TTY  IDLE SINCE
      2 1000 mostafa seat0 14269  user    tty2 no   -
      3 1000 mostafa -     14609  manager -    no   -

$ loginctl show-session 2 -p Timestamp -p Type -p Active -p State
Timestamp=Wed 2026-11-11 08:28:38 +03
Type=wayland
Active=yes
State=active
```

Only the two sessions that were created at 08:28:38 exist. A new login would
have added a third session with a later `Timestamp`.

**d) `last` records no new login**

```
$ last -n 5
mostafa  tty2         local            Wed Nov 11 08:28   still logged in
reboot   system boot  7.3.3-300.fc45.* Wed Nov 11 08:28   still running
mostafa  tty2         local            Tue Nov 10 22:32 - down   (00:08)
```

The only login for today is the 08:28 one, and it is still marked
*still logged in*.

**e) The journal records no session event after the fix was written**

```
$ journalctl --user --no-pager --since "2026-11-11 09:40:00" \
    | grep -iE "gnome-session|log out|logout|login|Starting session|shell.*restart"
(no matches)
```

### 1.3 Conclusion of step 1

The instruction was: *"If the timing condition is not satisfied, STOP and
report PENDING."* It is not satisfied, so the verification **stops here** and
is reported as **PENDING**. The read-only calls in §3 were still executed —
they are harmless, and they pin down the current state exactly — but their
results are reported as observations of the **old** module, not as a verdict
on the repair.

### 1.4 What the user should check

The retry was requested on the assumption that a logout/login had happened.
On this machine it has not — the same 08:28:38 session is still running.
Possible causes:

* the logout/login was performed on a **different** machine or a different
  user account;
* the session was **locked** (screen lock / suspend) rather than logged out —
  locking does not restart `gnome-shell`, so the old module stays loaded;
* the logout/login is still in progress, or was done **after** this
  verification ran (the current time, 10:06:46, is only ~1 min 40 s after the
  previous verification at 10:05 — physically too short for a logout/login
  cycle on this system).

Note that even a plain **extension reload inside the same Shell would not
help**: GJS caches modules by URL within the process, so PID 14977 would keep
serving the cached pre-fix module. Only a **new Shell process** loads the new
file.

---

## 2. V2/V3 — extension state — **PASS**

```
$ gnome-extensions info soft-dimmer@io.github.softdimmer
  Name: Soft Dimmer
  Path: /home/mostafa/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer
  Version: 1
  Enabled: Yes
  State: ACTIVE
```

```
$ gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
      --method org.freedesktop.DBus.GetNameOwner io.github.softdimmer
(':1.16',)
```

| Item | Expected | Observed | Verdict |
|---|---|---|---|
| `Enabled` | `Yes` | `Yes` | **PASS** |
| `State` | `ACTIVE` | `ACTIVE` | **PASS** |
| Bus name owner exists | `io.github.softdimmer` | `:1.16` (PID 14977) | **PASS** |

The live object path exposes the interface with the shipped signatures
(unchanged from the previous verification):

```
$ gdbus introspect --session --dest io.github.softdimmer \
      --object-path /io/github/softdimmer/BrightnessControl
  interface io.github.softdimmer.BrightnessControl {
    methods:
      GetBrightness(in  i monitor_index,   out d brightness);
      SetBrightness(in  i monitor_index,   in  d brightness);
      GetEnabled(out b enabled);
      SetEnabled(in  b enabled);
      GetCapabilities(out a{sv} capabilities);
  ...
[introspect exit=0]
```

---

## 3. Read-only live calls (recorded)

`SetBrightness` and `SetEnabled` were **not** called. All commands use the
bus name, object path, interface and signatures taken from the shipped XML
(`dbus/io.github.softdimmer.BrightnessControl.xml`).

### 3.1 `GetBrightness` — in `i monitor_index` → out `d brightness`

```
$ gdbus call --session --dest io.github.softdimmer \
      --object-path /io/github/softdimmer/BrightnessControl \
      --method io.github.softdimmer.BrightnessControl.GetBrightness -- -1
Error: GDBus.Error:org.gnome.gjs.JSError.TypeError: params is not iterable
[exit=1]
```

```
$ gdbus call --session --dest io.github.softdimmer \
      --object-path /io/github/softdimmer/BrightnessControl \
      --method io.github.softdimmer.BrightnessControl.GetBrightness 0
Error: GDBus.Error:org.gnome.gjs.JSError.TypeError: params is not iterable
[exit=1]
```

```
$ busctl --user call io.github.softdimmer /io/github/softdimmer/BrightnessControl \
      io.github.softdimmer.BrightnessControl GetBrightness i -- -1
Call failed: params is not iterable
[exit=1]
```

Returned D-Bus type: **none** (no reply — the call failed).
Error name: `org.gnome.gjs.JSError.TypeError`.

### 3.2 `GetEnabled` — in `()` → out `b enabled`

```
$ gdbus call --session --dest io.github.softdimmer \
      --object-path /io/github/softdimmer/BrightnessControl \
      --method io.github.softdimmer.BrightnessControl.GetEnabled
(true,)
[exit=0]
```

```
$ busctl --user call io.github.softdimmer /io/github/softdimmer/BrightnessControl \
      io.github.softdimmer.BrightnessControl GetEnabled
b true
[exit=0]
```

Returned D-Bus type: **`b`** (a boolean). Error name: none. Exit code: `0`.

### 3.3 `GetCapabilities` — in `()` → out `a{sv} capabilities`

```
$ gdbus call --session --dest io.github.softdimmer \
      --object-path /io/github/softdimmer/BrightnessControl \
      --method io.github.softdimmer.BrightnessControl.GetCapabilities
Error: GDBus.Error:org.gnome.gjs.JSError.TypeError: Object 0x3afb80536c70 is not a subclass of GObject_Struct, it's a Object
[exit=1]
```

```
$ busctl --user call io.github.softdimmer /io/github/softdimmer/BrightnessControl \
      io.github.softdimmer.BrightnessControl GetCapabilities
Call failed: Object 0x3afb80536c70 is not a subclass of GObject_Struct, it's a Object
[exit=1]
```

Returned D-Bus type: **none** (no reply — the call failed).
Error name: `org.gnome.gjs.JSError.TypeError`.

---

## 4. Outcome validation

| Requirement | Observed | Assessment |
|---|---|---|
| `GetBrightness` returns the expected double | no reply; `params is not iterable` | **PENDING** — precondition V1 failed |
| `GetCapabilities` returns a correctly typed `a{sv}` | no reply; `GObject_Struct` error | **PENDING** — precondition V1 failed |
| No `params is not iterable` exception | the exception **is** present (twice, plus in the journal) | **PENDING** — precondition V1 failed |
| No `GObject_Struct` exception | the exception **is** present | **PENDING** — precondition V1 failed |
| `GetEnabled` — real boolean semantics | `(true,)`, type `b` | **PENDING** — see below |

### 4.1 Why `GetEnabled` is PENDING even though it "succeeded"

`GetEnabled` returned `(true,)` with type `b` and exit code 0 — but that
**does not prove** the truthiness bug is fixed:

* The pre-fix code returned `[GLib.Variant.new('b', [true])]`. GJS packs that
  into the same `(b)` out-tuple, so the **wire bytes are identical** to the
  fixed code's `true`.
* The actual pre-fix defect is that a `GLib.Variant` object is *always*
  truthy, so the pre-fix method answers `true` **whatever the real state is**.
* The extension's live `enabled` state is `true`, so old and new code are
  indistinguishable here.

Distinguishing them requires observing the method while the state is `false`,
which means calling `SetEnabled` or writing the GSettings key — a **mutating**
operation, forbidden by the task ("do not modify settings", "do not call
mutating D-Bus methods"). The check is therefore recorded as **PENDING**, not
PASS.

The semantics are already proven correct in the isolated suite (Phase 2 report
§7, suite 7: the reply is a real JS boolean, `true` when the state is `true`
and `false` when it is `false`; 98/98).

### 4.2 The observed errors are the *old* module's, proven by the journal

The Shell's own stack traces again cite the pre-fix line numbers:

```
$ journalctl --user --no-pager --since "-3 min" | grep -E "dbus\.js:[0-9]+"
  GetBrightness@file:///…/soft-dimmer@io.github.softdimmer/dbus.js:86:32
  GetBrightness@file:///…/soft-dimmer@io.github.softdimmer/dbus.js:86:32
  GetCapabilities@file:///…/soft-dimmer@io.github.softdimmer/dbus.js:125:30
  GetCapabilities@file:///…/soft-dimmer@io.github.softdimmer/dbus.js:125:30
  GetBrightness@file:///…/soft-dimmer@io.github.softdimmer/dbus.js:86:32
  GetBrightness@file:///…/soft-dimmer@io.github.softdimmer/dbus.js:86:32
  GetBrightness@file:///…/soft-dimmer@io.github.softdimmer/dbus.js:86:32
  GetCapabilities@file:///…/soft-dimmer@io.github.softdimmer/dbus.js:125:30
```

What those two lines contain in each file version:

```
$ sed -n '86p;125p' <PRE-FIX backup>/extension/dbus.js
        const [monitorIndex] = params;                # line 86  - "params" at column 32
        return [GLib.Variant.new('a{sv}', [caps])];   # line 125 - the a{sv} packing, column 30

$ grep -n "const \[monitorIndex\] = params\|GLib.Variant.new('a{sv}'" <installed>/dbus.js
31://      whose values are GLib.Variant instances.  GLib.Variant.new('a{sv}',
```

The installed file contains **neither** construct — the only occurrence of
`GLib.Variant.new('a{sv}'` is inside a comment (line 31) explaining why it is
not used. The stack traces therefore point at code that exists only in the
pre-fix file, i.e. the running module is still the old one.

---

## 5. Integrity — **PASS**

```
$ gsettings list-recursively org.gnome.shell.extensions.soft-dimmer
org.gnome.shell.extensions.soft-dimmer brightness 1.0
org.gnome.shell.extensions.soft-dimmer enabled true
org.gnome.shell.extensions.soft-dimmer minimum-brightness 0.10000000000000001
org.gnome.shell.extensions.soft-dimmer monitor-selection 'all'
org.gnome.shell.extensions.soft-dimmer restore-brightness true
org.gnome.shell.extensions.soft-dimmer shortcut-decrease @as []
org.gnome.shell.extensions.soft-dimmer shortcut-increase @as []
org.gnome.shell.extensions.soft-dimmer shortcut-toggle @as []

$ gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
      --method org.freedesktop.DBus.GetNameOwner io.github.softdimmer
(':1.16',)

$ gnome-extensions info soft-dimmer@io.github.softdimmer | grep -E "^  (State|Enabled)"
  Enabled: Yes
  State: ACTIVE
```

| Item | Before the calls | After the calls | Verdict |
|---|---|---|---|
| `brightness` | `1.0` | `1.0` | **PASS** |
| `enabled` | `true` | `true` | **PASS** |
| every other `soft-dimmer` key | unchanged | unchanged | **PASS** |
| owner of `io.github.softdimmer` | `:1.16` | `:1.16` | **PASS** |
| extension `State` | `ACTIVE` | `ACTIVE` | **PASS** |
| extension `Enabled` | `Yes` | `Yes` | **PASS** |
| installed file tree | — | hash-identical to the pre-call snapshot | **PASS** |

```
$ cd .../soft-dimmer@io.github.softdimmer \
    && find . -type f -print0 | sort -z | xargs -0 sha256sum | sed 's| \./| |' | sort -k2 > /tmp/lv2-now.sha
$ diff /tmp/lv-now.sha /tmp/lv2-now.sha
  tree identical to the pre-verification snapshot
```

### 5.1 Actions deliberately not taken

* `SetBrightness` / `SetEnabled` — not called.
* No GSettings value was written; the `enabled` state was **not** toggled to
  probe `GetEnabled`.
* The extension was not enabled, disabled, reloaded or restarted; the Shell
  was not restarted; no logout/login was performed.
* No source file was created or modified during the verification (only this
  report was written, in the ASUSCraft working copy).
* No other D-Bus method was called on `io.github.softdimmer`; the only other
  bus touched was `org.freedesktop.DBus` (read-only `GetNameOwner`,
  `GetConnectionUnixProcessID`).

---

## 6. Previous stale-module failure vs. this retry

| | **Previous verification** (10:05) | **This retry** (10:06–10:09) |
|---|---|---|
| Stated premise | "the Shell loaded the old module before the fix was written" | "the user has now performed a NEW normal logout/login" |
| Shell PID | 14977 | **14977 — unchanged** |
| Shell start | 08:28:38 | **08:28:38 — unchanged** |
| New session created? | no | **no** |
| logind sessions | 2 and 3, both 08:28:38 | 2 and 3, both 08:28:38 — unchanged |
| `last` entry for today | 08:28, *still logged in* | 08:28, *still logged in* — no new entry |
| Installed `dbus.js` hash | `f05c167a…` | `f05c167a…` — unchanged, still the fixed file |
| Journal stack trace | `dbus.js:86:32`, `dbus.js:125:30` | `dbus.js:86:32`, `dbus.js:125:30` — identical |
| `GetBrightness` | `params is not iterable` | `params is not iterable` — identical |
| `GetEnabled` | `(true,)` | `(true,)` — identical |
| `GetCapabilities` | `GObject_Struct` error | `GObject_Struct` error — identical |
| Session-dependent verdict | FAIL (stale module) | **PENDING (precondition not met)** |

**The two runs are the same session and therefore the same result.** The
difference is only in how the outcome is *labelled*: the first run had a
verified precondition (the stale module was proven) and could state FAIL; this
retry was supposed to have a fresh session, the precondition is not met, so
the session-dependent checks are PENDING.

What **is** settled by this retry:

* the fixed file is installed and hashes to `f05c167a…` — **PASS**;
* the extension is healthy (`Enabled: Yes`, `State: ACTIVE`, name owned) — **PASS**;
* the session was not disturbed by the verification — **PASS**;
* **no logout/login has occurred on this machine** — the blocking fact.

---

## 7. Next step

The verification can be completed as soon as a **new Shell process** exists.
Concretely, after the user logs out and back in:

```sh
# 1. the new Shell must be newer than the fixed file
ps -o pid,lstart -p "$(pgrep -x gnome-shell | head -1)"
stat -c '%y' ~/.local/share/gnome-shell/extensions/soft-dimmer@io.github.softdimmer/dbus.js

# 2. the three read-only calls
gdbus call --session --dest io.github.softdimmer \
  --object-path /io/github/softdimmer/BrightnessControl \
  --method io.github.softdimmer.BrightnessControl.GetBrightness -- -1
#   expect: (1.0,)                                                          exit 0

gdbus call --session --dest io.github.softdimmer \
  --object-path /io/github/softdimmer/BrightnessControl \
  --method io.github.softdimmer.BrightnessControl.GetEnabled
#   expect: (true,)                                                         exit 0

gdbus call --session --dest io.github.softdimmer \
  --object-path /io/github/softdimmer/BrightnessControl \
  --method io.github.softdimmer.BrightnessControl.GetCapabilities
#   expect: ({'version': <'1.0'>, 'monitor-count': <N>, 'has-dbus': <true>,
#             'supports-per-monitor': <true>},)                            exit 0

# 3. the journal must be clean
journalctl --user --since "-5m" | grep -iE "params is not iterable|GObject_Struct"
#   expect: no output
```

`GetEnabled` remains **PENDING** until it can be observed in **both** states;
that requires a deliberate `SetEnabled` toggle, which is outside this task's
"read-only" constraint and needs the user's explicit go-ahead.

---

## 8. Summary

* **PENDING** — blocked by the timing precondition (V1). The GNOME Shell that
  is running is the same one that started at 08:28:38, before the fix was
  written at 09:42:04; no logout/login has occurred on this machine.
* The fixed file is installed and hashes to
  `f05c167a06b577d4007aef3ed46eb25d83b6e254c98fa85508655f2fbe384669`.
* The extension is `Enabled: Yes` / `State: ACTIVE` and owns
  `io.github.softdimmer` (`:1.16` → PID 14977) — **PASS**.
* The live calls still show the two pre-fix exceptions, and the Shell's own
  stack traces still cite the pre-fix line numbers — the old module is still
  loaded.
* Nothing was modified: brightness `1.0`, `enabled true`, all other keys
  unchanged, owner `:1.16`, extension `ACTIVE`, file tree hash-identical.
* The isolated verification of the repair is unaffected: **98/98** (Phase 2
  report §7).

**Phase 3 was not started.**
