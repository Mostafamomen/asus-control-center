# ASUS Control Center
# SPDX-License-Identifier: GPL-3.0-or-later

VERSION  ?= 0.4.0
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
       src/control/brightness.cpp src/control/ctl.cpp src/control/asusd.cpp \
       src/control/gfx.cpp src/control/softdim.cpp src/control/backlight.cpp \
       src/user/user.cpp \
       src/ui/window.cpp src/ui/home.cpp src/ui/theme.cpp src/ui/ring.cpp \
       src/ui/spark.cpp src/ui/pages.cpp src/ui/mcui.cpp src/ui/login.cpp \
       src/ui/specs.cpp

BIN = asus-control-center

$(BIN): $(SRCS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DACC_VERSION='"$(VERSION)"' \
	  $(shell pkg-config --cflags $(PKGS)) -o $@ $^ \
	  $(LDFLAGS) $(shell pkg-config --libs $(PKGS))

test: $(BIN)
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

# Source tarball used by the RPM and Debian builds.
dist:
	git archive --format=tar.gz --prefix=$(BIN)-$(VERSION)/ \
	  -o $(BIN)-$(VERSION).tar.gz HEAD

clean:
	rm -f $(BIN)

.PHONY: test install uninstall dist clean
