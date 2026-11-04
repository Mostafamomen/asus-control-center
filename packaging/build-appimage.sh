#!/bin/bash
# Build a portable AppImage into dist/ using linuxdeploy + the GTK plugin,
# which bundle GTK 4, libadwaita, the gdk-pixbuf loaders, GSettings schemas
# and the Adwaita icons.
#
# Best-effort / portable build: the privileged pkexec helper and the polkit
# policy are NOT bundled (an AppImage is read-only and unprivileged), so the
# AppImage gives the full monitoring UI while the system-level hardware
# controls (charge limit, performance profile, GPU MUX, keyboard light)
# need the .deb/.rpm, which install the helper. The app greys those out
# cleanly when the helper is absent.
#
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."

ARCH="${ARCH:-x86_64}"
export APPIMAGE_EXTRACT_AND_RUN=1   # CI containers usually have no FUSE

if [ "$(id -u)" = 0 ]; then
  export DEBIAN_FRONTEND=noninteractive
  apt-get update
  apt-get install -y --no-install-recommends \
    build-essential pkg-config libgtk-4-dev libadwaita-1-dev \
    wget file desktop-file-utils librsvg2-common libglib2.0-bin \
    libgdk-pixbuf-2.0-0 patchelf ca-certificates
fi

VERSION="${VERSION:-$(sed -n 's/^VERSION[[:space:]]*?\?=[[:space:]]*//p' Makefile | head -1)}"
APPID=io.github.asuscontrolcenter.AsusControlCenter

make VERSION="$VERSION"
rm -rf AppDir
make install DESTDIR="$PWD/AppDir" PREFIX=/usr VERSION="$VERSION"

# AppImage runs from a mounted AppDir, so the launcher must be found on the
# bundled PATH, not an absolute system path.
sed -i 's|^Exec=/usr/bin/|Exec=|' \
  "AppDir/usr/share/applications/$APPID.desktop"

fetch() { wget -q "$1" -O "$2"; chmod +x "$2"; }
fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage" linuxdeploy
fetch "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh" linuxdeploy-plugin-gtk.sh

mkdir -p dist
rm -f dist/*.AppImage
export OUTPUT="dist/asus-control-center-$VERSION-$ARCH.AppImage"
./linuxdeploy --appdir AppDir --plugin gtk \
  -d "AppDir/usr/share/applications/$APPID.desktop" \
  -i "AppDir/usr/share/icons/hicolor/256x256/apps/$APPID.png" \
  --output appimage

ls -l dist/*.AppImage
