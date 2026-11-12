#!/bin/bash
# Verify that the Flatpak manifest asks for EXACTLY the permissions the
# project intends - no more. This is the guard that keeps the sandbox
# narrow: every D-Bus name and every filesystem entry is listed here.
#
# Run it locally:   flatpak/verify-permissions.sh
# Run it in CI:     the "flatpak" workflow runs it on every push.
#
# Exit 0 = the manifest matches. Exit 1 = a permission was added, removed
# or changed without the review that a security change needs.
set -u

DIR="$(cd "$(dirname "$0")" && pwd)"
MANIFEST="$DIR/io.github.asuscontrolcenter.asuscraft.yml"

# The permissions the manifest is allowed to ask for.
EXPECTED_SESSION="ca.desrt.dconf
io.github.asuscontrolcenter.Agent
org.freedesktop.portal.Desktop
org.gnome.shell.extensions.soft-brightness-plus"

EXPECTED_SYSTEM="org.freedesktop.login1
xyz.ljones.Asusd"

EXPECTED_FILESYSTEMS="xdg-run/dconf"

fail () { echo "FAIL: $*" >&2; exit 1; }

[ -f "$MANIFEST" ] || fail "the manifest is missing: $MANIFEST"

actual_session=$(sed -n 's/^[[:space:]]*-[[:space:]]*--talk-name=\(.*\)$/\1/p' "$MANIFEST" | sort)
actual_system=$(sed -n 's/^[[:space:]]*-[[:space:]]*--system-talk-name=\(.*\)$/\1/p' "$MANIFEST" | sort)
actual_files=$(sed -n 's/^[[:space:]]*-[[:space:]]*--filesystem=\(.*\)$/\1/p' "$MANIFEST" | sort)

# Only the list items of finish-args count - the comments around them
# document what is deliberately NOT asked for.
finish_args=$(awk '
  /^finish-args:/ { inblock = 1; next }
  inblock && /^[a-zA-Z]/ { inblock = 0 }
  inblock && /^[[:space:]]*-[[:space:]]/ { print }
' "$MANIFEST")

# A permission the project never asked for.
for bad in --device=all --filesystem=host --allow=devel --talk-name=org.freedesktop.DBus \
           --system-talk-name=org.freedesktop.DBus --system-talk-name=org.freedesktop.systemd1 \
           --socket=x11 --socket=wayland --device=dri; do
  if echo "$finish_args" | grep -q -- "$bad"; then
    fail "the manifest asks for '$bad' - that needs a separate review"
  fi
done

if [ "$actual_session" != "$EXPECTED_SESSION" ]; then
  echo "the session bus permissions differ:" >&2
  diff <(echo "$EXPECTED_SESSION") <(echo "$actual_session") >&2 || true
  fail "the session bus permissions are not the reviewed set"
fi

if [ "$actual_system" != "$EXPECTED_SYSTEM" ]; then
  echo "the system bus permissions differ:" >&2
  diff <(echo "$EXPECTED_SYSTEM") <(echo "$actual_system") >&2 || true
  fail "the system bus permissions are not the reviewed set"
fi

if [ "$actual_files" != "$EXPECTED_FILESYSTEMS" ]; then
  echo "the filesystem permissions differ:" >&2
  diff <(echo "$EXPECTED_FILESYSTEMS") <(echo "$actual_files") >&2 || true
  fail "the filesystem permissions are not the reviewed set"
fi

echo "ok: the manifest asks for exactly the reviewed permissions"
echo "  session bus:  $(echo "$actual_session" | tr '\n' ' ')"
echo "  system bus:   $(echo "$actual_system" | tr '\n' ' ')"
echo "  filesystem:   $(echo "$actual_files" | tr '\n' ' ')"
exit 0
