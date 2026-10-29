#!/bin/bash
# Build the .deb into dist/. Run on Ubuntu 24.04+ or Debian 13+ (the oldest
# releases with libadwaita 1.5). As root it installs the build deps first.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ "$(id -u)" = 0 ]; then
  export DEBIAN_FRONTEND=noninteractive
  apt-get update
  apt-get install -y --no-install-recommends build-essential devscripts equivs
  mk-build-deps -i -r -t 'apt-get -y --no-install-recommends' debian/control
fi

dpkg-buildpackage -us -uc -b
mkdir -p dist
mv ../asus-control-center_*.deb dist/
rm -f ../asus-control-center_*.buildinfo ../asus-control-center_*.changes
ls -l dist/*.deb
