#!/bin/bash
# Build the .rpm (and .src.rpm) into dist/. Run on Fedora or an EL 10
# clone (AlmaLinux, Rocky, RHEL). As root it installs the build deps first.
set -euo pipefail
cd "$(dirname "$0")/.."

SPEC=packaging/rpm/asus-control-center.spec

if [ "$(id -u)" = 0 ]; then
  if ! grep -q '^ID=fedora' /etc/os-release; then
    # EL: gtk4/libadwaita headers live in CRB, appstream-glib in EPEL
    dnf install -y epel-release dnf-plugins-core
    dnf config-manager --set-enabled crb
  fi
  dnf install -y rpm-build rpmdevtools 'dnf-command(builddep)' tar
  dnf builddep -y "$SPEC"
fi

VERSION=$(rpmspec -q --srpm --qf '%{version}' "$SPEC")
TOP="$PWD/dist/rpmbuild"
mkdir -p "$TOP/SOURCES"
tar --exclude=./dist --exclude=./.git --exclude=./asus-control-center \
    --transform "s,^\.,asus-control-center-$VERSION," \
    -czf "$TOP/SOURCES/asus-control-center-$VERSION.tar.gz" .

rpmbuild -ba --define "_topdir $TOP" "$SPEC"
cp "$TOP"/RPMS/*/*.rpm "$TOP"/SRPMS/*.rpm dist/
ls -l dist/*.rpm
