# ASUSCraft 1.0.0-rc1 — First Release Candidate

ASUSCraft is a native Linux laptop management application for ASUS laptops.

## Assets
- Fedora RPM: validated on Fedora 44 x86_64.
- Debian DEB: built and metadata-inspected; runtime testing pending.
- AppImage: built and structurally inspected; runtime testing pending.

## Installation
Fedora:
sudo dnf install ./asus-control-center-1.0.0-1.fc44.x86_64.rpm

Debian/Ubuntu:
sudo dpkg -i ./asus-control-center_1.0.0_amd64.deb

AppImage:
chmod +x ./ASUSCraft-1.0.0-x86_64.AppImage
./ASUSCraft-1.0.0-x86_64.AppImage

## Known limitations
- No true interactive security scan cancellation.
- Some legacy untranslated strings may remain.
- No dedicated automated unit-test framework.
- iGPU-only mode unsupported on ASUS N6506CU.