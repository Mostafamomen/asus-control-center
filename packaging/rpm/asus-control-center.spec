Name:           asus-control-center
Version:        1.2.5
Release:        1%{?dist}
Summary:        Battery, performance, GPU and display controls for ASUS laptops

License:        GPL-3.0-or-later
URL:            https://github.com/Mostafamomen/asus-control-center
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  pkgconfig(gtk4)
BuildRequires:  pkgconfig(libadwaita-1) >= 1.5
BuildRequires:  desktop-file-utils
BuildRequires:  libappstream-glib

Requires:       polkit
Requires:       hicolor-icon-theme
# Optional ASUS services (asus-linux COPR: lukenukem/asus-linux). The app
# works without them and greys out the controls they provide.
Recommends:     asusctl
Recommends:     iw
Suggests:       gnome-shell

%description
ASUSCraft is a community-built GTK 4 / libadwaita app that brings
the hardware information and controls of an ASUS laptop into one window:
battery charge limits, performance profiles, GPU MUX switching (Hybrid/Ultimate),
keyboard lighting, fan and temperature monitoring, and OLED-friendly
display dimming. It is not affiliated with or endorsed by ASUS.

Privileged changes go through a small whitelist-only helper run with
pkexec, so no part of the app runs as root.

%prep
%autosetup

%build
%set_build_flags
%make_build VERSION=%{version}

%install
%make_install VERSION=%{version}

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/io.github.asuscontrolcenter.AsusControlCenter.desktop
appstream-util validate-relax --nonet %{buildroot}%{_metainfodir}/io.github.asuscontrolcenter.AsusControlCenter.metainfo.xml

%files
%license LICENSE
%doc README.md
%{_bindir}/asus-control-center
%dir %{_libexecdir}/asus-control-center
%{_libexecdir}/asus-control-center/acc-helper
%{_datadir}/polkit-1/actions/io.github.asuscontrolcenter.AsusControlCenter.policy
%{_datadir}/applications/io.github.asuscontrolcenter.AsusControlCenter.desktop
%{_datadir}/icons/hicolor/scalable/apps/io.github.asuscontrolcenter.AsusControlCenter.svg
%{_datadir}/icons/hicolor/*/apps/io.github.asuscontrolcenter.AsusControlCenter.png
%{_metainfodir}/io.github.asuscontrolcenter.AsusControlCenter.metainfo.xml
%{_mandir}/man1/asus-control-center.1*
%{_datadir}/asus-control-center/

%changelog
* Thu Nov 12 2026 Mostafa Momen <mostafamomen6521@gmail.com> - 1.2.5-1
- ASUSCraft 1.2.5: the Flatpak distribution and the separately packaged
  host agent (asuscraft-agent).
- The GUI, the read-only hardware probes and the app's own settings run in
  a Flatpak; everything that needs the machine itself goes through the
  host agent.
- The host agent is a thin, unprivileged, D-Bus-activated session service.
  It holds no privilege of its own: the whitelist-only helper and the
  polkit action io.github.asuscontrolcenter.helper are unchanged.
- The night light now reads and writes the GNOME settings-daemon colour
  schema (enabled, colour temperature, schedule) and keeps the KDE path.
- The touchpad uses the desktop's own setting; the obsolete xinput path
  is gone.
- The Flatpak asks for six named D-Bus services and one runtime directory:
  no host filesystem, no device access, no unrestricted bus.
- Fix a double free in the System Diagnosis SMART check (it aborted the
  application on start) and one in the night light's error path.

* Mon Nov 03 2026 Mostafa Momen <mostafamomen6521@gmail.com> - 1.0.0-1
- System Manager: Smart Recommendations, CPU/RAM monitoring, health history
- System Manager: Quick Actions, improved proc_timer lifecycle
- Software Center: installed apps, update, driver and diagnostics overview
- Fix memory leak in health-history directory creation
- Fix stale s_last_exit on helper spawn failure

* Sun Nov 02 2026 Mostafa Momen <mostafamomen6521@gmail.com> - 0.9.0-1
- System Manager and Software Center pages
- System Diagnosis: Hardware and Software check groups expanded

* Mon Nov 02 2026 Mostafa Momen <mostafamomen6521@gmail.com> - 0.6.0-1
- GPU: fix MUX double-write, verify switch after reboot
- Integrated (Intel Arc) shown unavailable with the reason

* Thu Oct 29 2026 Mostafa Momen <mostafamomen6521@gmail.com> - 0.4.0-1
- First packaged release: RPM and Debian packages
- Separate hardware backlight slider next to OLED dimming
- Bundled Soft Brightness Plus GNOME extension for OLED dimming
- MyASUS-style layout, colours and fonts
