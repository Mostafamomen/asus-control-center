#!/bin/bash
# Build the .rpm files (the app and the host agent) into dist/. Run on
# Fedora or an EL 10 clone (AlmaLinux, Rocky, RHEL). As root it installs the
# build deps first.
#
# The two packages are released together and share one source tarball: the
# app and the agent are two halves of the same release.
set -euo pipefail
cd "$(dirname "$0")/.."

SPEC=packaging/rpm/asus-control-center.spec
AGENT_SPEC=packaging/rpm/asuscraft-agent.spec

if [ "$(id -u)" = 0 ]; then
  if ! grep -q '^ID=fedora' /etc/os-release; then
    # EL: gtk4/libadwaita headers live in CRB, appstream-glib in EPEL
    dnf install -y epel-release dnf-plugins-core
    dnf config-manager --set-enabled crb
  fi
  dnf install -y rpm-build rpmdevtools 'dnf-command(builddep)' tar
  dnf builddep -y "$SPEC"
  dnf builddep -y "$AGENT_SPEC"
fi

VERSION=$(rpmspec -q --srpm --qf '%{version}' "$SPEC")
TOP="$PWD/dist/rpmbuild"
mkdir -p "$TOP/SOURCES"
tar --exclude=./dist --exclude=./.git --exclude=./asus-control-center \
    --exclude=./agent/asuscraft-agent \
    --transform "s,^\.,asus-control-center-$VERSION," \
    -czf "$TOP/SOURCES/asus-control-center-$VERSION.tar.gz" .

rpmbuild -ba --define "_topdir $TOP" "$SPEC"
rpmbuild -ba --define "_topdir $TOP" "$AGENT_SPEC"
cp "$TOP"/RPMS/*/*.rpm "$TOP"/SRPMS/*.rpm dist/
ls -l dist/*.rpm
