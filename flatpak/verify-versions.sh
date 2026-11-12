#!/bin/bash
# Verify that every version source in the project agrees, and that the
# host agent's D-Bus interface version stays independent of the release.
#
# Run it locally:   flatpak/verify-versions.sh [expected-version]
# Run it in CI:     the "ci" workflow runs it on every push.
set -u

DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$DIR" || exit 1

EXPECTED="${1:-1.2.5}"
fail () { echo "FAIL: $*" >&2; exit 1; }

get () { # file, sed-expression
  sed -n "$2" "$1" 2>/dev/null | head -1
}

makefile=$(get Makefile 's/^VERSION[[:space:]]*?=[[:space:]]*//p')
spec=$(get packaging/rpm/asus-control-center.spec 's/^Version:[[:space:]]*//p')
agent_spec=$(get packaging/rpm/asuscraft-agent.spec 's/^Version:[[:space:]]*//p')
debian=$(get debian/changelog '1s/.*(\([^)-]*\)).*/\1/p')
metainfo=$(get data/io.github.asuscontrolcenter.AsusControlCenter.metainfo.xml 's/.*<release version="\([^"]*\)".*/\1/p')

echo "the version sources:"
echo "  the Makefile            $makefile"
echo "  the app RPM spec        $spec"
echo "  the agent RPM spec      $agent_spec"
echo "  debian/changelog        $debian"
echo "  the AppStream metainfo  $metainfo"

for pair in "Makefile:$makefile" "the app RPM spec:$spec" \
            "the agent RPM spec:$agent_spec" "debian/changelog:$debian" \
            "the AppStream metainfo:$metainfo"; do
  what="${pair%%:*}"; value="${pair##*:}"
  [ "$value" = "$EXPECTED" ] || fail "$what says '$value', expected '$EXPECTED'"
done

# The agent's D-Bus interface version is independent of the release: it
# describes the protocol, not the release. It must be defined and must
# not simply mirror the release number.
iface=$(get src/control/agentproto.h 's/.*#define ACC_AGENT_VERSION[[:space:]]*"\([^"]*\)".*/\1/p')
[ -n "$iface" ] || fail "the agent's D-Bus interface version is not defined"
echo "  the agent's interface   $iface  (independent of the release ✓)"
case "$iface" in
  "$EXPECTED") fail "the agent's interface version must not mirror the release version" ;;
esac

# The Flatpak carries its own application id and inherits the version from
# the metainfo: check that the id is the lower-case one.
appid=$(get flatpak/io.github.asuscontrolcenter.asuscraft.yml 's/^app-id:[[:space:]]*//p')
case "$appid" in
  *[!a-z0-9.-]*) fail "the Flatpak application id '$appid' must be lower case" ;;
esac
echo "  the Flatpak app-id      $appid ✓"

echo "ok: every version source agrees on $EXPECTED"
exit 0
