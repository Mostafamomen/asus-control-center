Name:           asuscraft-agent
Version:        1.2.5
Release:        1%{?dist}
Summary:        The ASUSCraft host agent: lets a sandboxed ASUSCraft reach the hardware

License:        GPL-3.0-or-later
URL:            https://github.com/Mostafamomen/asus-control-center
Source0:        %{url}/archive/v%{version}/asus-control-center-%{version}.tar.gz

BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  pkgconfig(glib-2.0)

# The agent talks to polkit (through pkexec) and, for the bundled GNOME
# Shell extension, to the desktop session.
Requires:       glib2
Requires:       polkit
Recommends:     gnome-shell
Suggests:       polkit-gnome

%description
ASUSCraft can be run as a Flatpak, but a sandbox cannot write to the
laptop's firmware, cannot run pkexec and cannot start gnome-extensions.

This package installs the ASUSCraft host agent: a small, unprivileged,
D-Bus-activated service that runs on the session bus as the ordinary user
and does exactly those things on behalf of the sandboxed app.

The agent holds no privilege of its own. Every privileged change still goes
through the same whitelist-only helper (/usr/libexec/asus-control-center/
acc-helper) under the same polkit action
(io.github.asuscontrolcenter.helper), so the authentication prompt and the
list of allowed changes are unchanged.

The agent is optional: ASUSCraft works without it, and the controls that
need the host say so instead of failing quietly.

%prep
# the source tarball is the app's: both packages are released together
%autosetup -n asus-control-center-%{version}

%build
%set_build_flags
make -C agent

%install
%make_install -C agent

%check
./agent/asuscraft-agent --check

%files
%license LICENSE
%doc README.md
%{_libexecdir}/asuscraft-agent/asuscraft-agent
%{_datadir}/dbus-1/services/io.github.asuscontrolcenter.Agent.service
%{_datadir}/asuscraft-agent/

%changelog
* Thu Nov 12 2026 Mostafa Momen <mostafamomen6521@gmail.com> - 1.2.5-1
- The host agent for ASUSCraft 1.2.5: a thin, unprivileged, D-Bus
  activated session service that lets the Flatpak reach the machine.
- It holds no privilege of its own: every privileged change still goes
  through the whitelist-only helper under the unchanged polkit action
  io.github.asuscontrolcenter.helper.
- The agent's own D-Bus interface version (1.0) is independent of the
  ASUSCraft release version and is only changed when the interface itself
  changes incompatibly.

* Wed Nov 11 2026 Mostafa Momen <mostafamomen6521@gmail.com> - 10.2-1
- The ASUSCraft host agent: a D-Bus activated session service that lets
  the Flatpak build reach the hardware helper, the host commands and the
  desktop session. It holds no privilege of its own: the whitelist-only
  helper and its polkit action are unchanged.
