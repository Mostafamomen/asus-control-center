# ASUSCraft
# SPDX-License-Identifier: GPL-3.0-or-later

VERSION  ?= 1.2.5
CXX      ?= g++
CXXFLAGS ?= -O2 -g -Wall -Wextra
CXXFLAGS += -std=gnu++20
PKGS      = gtk4 libadwaita-1

# The helper path and polkit policy location are fixed system paths that the
# code relies on, so packages always install under /usr.
PREFIX     ?= /usr
BINDIR     ?= $(PREFIX)/bin
DATADIR    ?= $(PREFIX)/share
LIBEXECDIR ?= /usr/libexec
POLKITDIR  ?= /usr/share/polkit-1/actions
MANDIR     ?= $(DATADIR)/man
APP_ID      = io.github.asuscontrolcenter.AsusControlCenter

SRCS = src/main.cpp \
       src/i18n/i18n.cpp \
       src/probe/probe.cpp src/probe/battery.cpp src/probe/dmi.cpp \
       src/probe/hwmon.cpp src/probe/gpu.cpp src/probe/display.cpp \
       src/probe/sysinfo.cpp src/probe/load.cpp src/probe/devs.cpp \
       src/probe/desktop.cpp \
       src/control/brightness.cpp src/control/ctl.cpp src/control/asusd.cpp \
       src/control/gfx.cpp src/control/muxreq.cpp src/control/softdim.cpp src/control/backlight.cpp \
       src/control/nightlight.cpp src/control/touchpadctl.cpp \
       src/control/commands.cpp src/control/agent.cpp src/control/hostcmd.cpp \
       src/control/sbp_local.cpp src/control/nlsettings.cpp src/control/portal.cpp \
       src/user/user.cpp \
       src/ui/window.cpp src/ui/home.cpp src/ui/theme.cpp src/ui/ring.cpp \
       src/ui/spark.cpp src/ui/pages.cpp src/ui/hubs.cpp src/ui/sysdiag.cpp \
       src/ui/sysmgr.cpp src/ui/softcenter.cpp src/ui/pcmanager.cpp src/ui/systest.cpp src/ui/mcui.cpp src/ui/login.cpp src/ui/specs.cpp

BIN = asus-control-center

$(BIN): $(SRCS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DACC_VERSION='"$(VERSION)"' \
	  $(shell pkg-config --cflags $(PKGS)) -o $@ $^ \
	  $(LDFLAGS) $(shell pkg-config --libs $(PKGS))

launcher-bin:
	$(MAKE) -C launcher

test: $(BIN) launcher-bin
	./$(BIN) --check
	bash -n data/acc-helper
	./$(BIN) --self-test

install: $(BIN)
	install -Dm0755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	install -Dm0755 data/acc-helper $(DESTDIR)$(LIBEXECDIR)/asus-control-center/acc-helper
	install -Dm0644 data/$(APP_ID).policy $(DESTDIR)$(POLKITDIR)/$(APP_ID).policy
	install -d $(DESTDIR)$(DATADIR)/applications
	sed 's|@BIN@|$(BINDIR)|' data/$(APP_ID).desktop.in \
	  > $(DESTDIR)$(DATADIR)/applications/$(APP_ID).desktop
	install -Dm0644 data/$(APP_ID).svg \
	  $(DESTDIR)$(DATADIR)/icons/hicolor/scalable/apps/$(APP_ID).svg
	for size in 16x16 24x24 32x32 48x48 64x64 128x128 256x256 512x512; do \
	  install -Dm0644 data/icons/hicolor/$$size/apps/$(APP_ID).png \
	    $(DESTDIR)$(DATADIR)/icons/hicolor/$$size/apps/$(APP_ID).png; \
	done
	install -Dm0644 data/$(APP_ID).metainfo.xml \
	  $(DESTDIR)$(DATADIR)/metainfo/$(APP_ID).metainfo.xml
	install -Dm0644 docs/$(BIN).1 $(DESTDIR)$(MANDIR)/man1/$(BIN).1
	install -d $(DESTDIR)$(DATADIR)/asus-control-center
	install -m0644 src/ui/theme.css src/ui/theme-dark.css assets/*.png \
	  $(DESTDIR)$(DATADIR)/asus-control-center/
	cd data && find soft-brightness-plus -type f -exec \
	  install -Dm0644 {} $(DESTDIR)$(DATADIR)/asus-control-center/{} \;

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN) \
	      $(DESTDIR)$(LIBEXECDIR)/asus-control-center/acc-helper \
	      $(DESTDIR)$(POLKITDIR)/$(APP_ID).policy \
	      $(DESTDIR)$(DATADIR)/applications/$(APP_ID).desktop \
	      $(DESTDIR)$(DATADIR)/icons/hicolor/scalable/apps/$(APP_ID).svg \
	      $(DESTDIR)$(DATADIR)/metainfo/$(APP_ID).metainfo.xml \
	      $(DESTDIR)$(MANDIR)/man1/$(BIN).1
	rm -rf $(DESTDIR)$(DATADIR)/asus-control-center
	-rmdir $(DESTDIR)$(LIBEXECDIR)/asus-control-center

# ---------------------------------------------------------------------------
# The Flatpak build.
#
# The Flatpak has its OWN application id: a Flatpak id may not contain
# capitals, and the native id (io.github.asuscontrolcenter.AsusControlCenter)
# is what the polkit action, the desktop file and the dconf path already
# use. Both are shipped: the native one unchanged, the Flatpak one lower
# case, side by side.
#
# The helper and the polkit policy are NOT installed here: they belong to
# the host, and the host agent invokes them.
FLATPAK_PREFIX ?= /app
FLATPAK_APP_ID = io.github.asuscontrolcenter.asuscraft

install-flatpak: $(BIN)
	install -Dm0755 $(BIN) $(DESTDIR)$(FLATPAK_PREFIX)/bin/$(BIN)
	install -d $(DESTDIR)$(FLATPAK_PREFIX)/share/applications
	sed -e 's|Exec=@BIN@/$(BIN)|Exec=$(BIN)|' \
	    -e 's|^Icon=.*|Icon=$(FLATPAK_APP_ID)|' \
	    data/$(APP_ID).desktop.in \
	  > $(DESTDIR)$(FLATPAK_PREFIX)/share/applications/$(FLATPAK_APP_ID).desktop
	install -Dm0644 data/$(APP_ID).svg \
	  $(DESTDIR)$(FLATPAK_PREFIX)/share/icons/hicolor/scalable/apps/$(FLATPAK_APP_ID).svg
	for size in 16x16 24x24 32x32 48x48 64x64 128x128 256x256 512x512; do \
	  install -Dm0644 data/icons/hicolor/$$size/apps/$(APP_ID).png \
	    $(DESTDIR)$(FLATPAK_PREFIX)/share/icons/hicolor/$$size/apps/$(FLATPAK_APP_ID).png; \
	done
	install -Dm0644 data/$(APP_ID).svg \
	  $(DESTDIR)$(FLATPAK_PREFIX)/share/icons/hicolor/scalable/apps/$(APP_ID).svg
	install -d $(DESTDIR)$(FLATPAK_PREFIX)/share/metainfo
	sed -e 's|<id>.*</id>|<id>$(FLATPAK_APP_ID)</id>|' \
	    -e 's|<launchable type="desktop-id">.*</launchable>|<launchable type="desktop-id">$(FLATPAK_APP_ID).desktop</launchable>|' \
	    data/$(APP_ID).metainfo.xml \
	  > $(DESTDIR)$(FLATPAK_PREFIX)/share/metainfo/$(FLATPAK_APP_ID).metainfo.xml
	install -Dm0644 docs/$(BIN).1 $(DESTDIR)$(FLATPAK_PREFIX)/share/man/man1/$(BIN).1
	install -d $(DESTDIR)$(FLATPAK_PREFIX)/share/asus-control-center
	install -m0644 src/ui/theme.css src/ui/theme-dark.css assets/*.png \
	  $(DESTDIR)$(FLATPAK_PREFIX)/share/asus-control-center/
	cd data && find soft-brightness-plus -type f -exec \
	  install -Dm0644 {} $(DESTDIR)$(FLATPAK_PREFIX)/share/asus-control-center/{} \;
	# the Soft Brightness Plus schema has to be inside the Flatpak: the
	# dimming VALUE is written through GSettings, and GSettings writes
	# reach the host's dconf as long as the schema is known locally.
	install -d $(DESTDIR)$(FLATPAK_PREFIX)/share/glib-2.0/schemas
	install -m0644 data/soft-brightness-plus/schemas/*.gschema.xml \
	  $(DESTDIR)$(FLATPAK_PREFIX)/share/glib-2.0/schemas/
	glib-compile-schemas $(DESTDIR)$(FLATPAK_PREFIX)/share/glib-2.0/schemas/

flatpak-uninstall:
	rm -rf $(DESTDIR)$(FLATPAK_PREFIX)

# Source tarball used by the RPM and Debian builds.
dist:
	git archive --format=tar.gz --prefix=$(BIN)-$(VERSION)/ \
	  -o $(BIN)-$(VERSION).tar.gz HEAD

clean:
	rm -f $(BIN)

.PHONY: test install uninstall install-flatpak flatpak-uninstall dist clean
