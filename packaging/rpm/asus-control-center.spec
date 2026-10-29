Name:           asus-control-center
Version:        0.4.0
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
Recommends:     supergfxctl
Recommends:     iw
Suggests:       gnome-shell

%description
ASUS Control Center is a community-built GTK 4 / libadwaita app that brings
the hardware information and controls of an ASUS laptop into one window:
battery charge limits, performance profiles, GPU MUX and iGPU-only modes,
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
%{_metainfodir}/io.github.asuscontrolcenter.AsusControlCenter.metainfo.xml
%{_mandir}/man1/asus-control-center.1*
%{_datadir}/asus-control-center/

%changelog
* Thu Oct 29 2026 Mostafa Momen <mostafamomen6521@gmail.com> - 0.4.0-1
- First packaged release: RPM and Debian packages
- Separate hardware backlight slider next to OLED dimming
- iGPU-only mode via supergfxd, bundled Soft Brightness Plus for OLED dimming
- MyASUS-style layout, colours and fonts
