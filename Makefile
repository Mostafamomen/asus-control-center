# ASUSCraft
# SPDX-License-Identifier: GPL-3.0-or-later

VERSION  ?= 1.2.4
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
       src/user/user.cpp \
       src/ui/window.cpp src/ui/home.cpp src/ui/theme.cpp src/ui/ring.cpp \
       src/ui/spark.cpp src/ui/pages.cpp src/ui/hubs.cpp src/ui/sysdiag.cpp \
       src/ui/sysmgr.cpp src/ui/softcenter.cpp src/ui/pcmanager.cpp src/ui/systest.cpp src/ui/mcui.cpp src/ui/login.cpp src/ui/specs.cpp src/ui/softdimmer_page.cpp \
       src/control/softdimmer/monitor_manager.cpp \
       src/control/softdimmer/brightness_engine.cpp \
       src/control/softdimmer/transition_engine.cpp \
       src/control/softdimmer/softdimmer_service.cpp

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

	# ── Soft Dimmer GNOME Shell extension ──
	install -d $(DESTDIR)$(SD_EXT_DIR)
	for f in $(SD_EXT_SRCS); do \
	  install -Dm0644 $$f $(DESTDIR)$(SD_EXT_DIR)/; \
	done
	# Compile GSettings schemas for the extension (if schemas dir exists)
	if [ -d gnome-extension/soft-dimmer/schemas ]; then \
	  install -d $(DESTDIR)$(SD_EXT_DIR)/schemas; \
	  find gnome-extension/soft-dimmer/schemas -name '*.gschema.xml' -exec \
	    install -Dm0644 {} $(DESTDIR)$(SD_EXT_DIR)/schemas/ \; ; \
	  if [ -x $(COMPILE_SCHEMAS) ]; then \
	    $(COMPILE_SCHEMAS) $(DESTDIR)$(SD_EXT_DIR)/schemas || true; \
	  fi; \
	fi
	# Enable the extension for the current user (best-effort, non-fatal)
	if [ -x $(EXT_BIN) ]; then \
	  $(EXT_BIN) enable $(SD_EXT_UUID) 2>/dev/null || true; \
	fi

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

	# ── Soft Dimmer extension removal ──
	if [ -x $(EXT_BIN) ]; then \
	  $(EXT_BIN) disable $(SD_EXT_UUID) 2>/dev/null || true; \
	fi
	rm -rf $(DESTDIR)$(SD_EXT_DIR)

# Soft Dimmer (ASUSCraft integrated) GNOME Shell extension
SD_EXT_UUID   = soft-dimmer-asc@io.github.asuscraft
SD_EXT_DIR    = $(DATADIR)/gnome-shell/extensions/$(SD_EXT_UUID)
SD_EXT_SRCS   = gnome-extension/soft-dimmer/extension.js \
                gnome-extension/soft-dimmer/dimmer.js \
                gnome-extension/soft-dimmer/indicator.js \
                gnome-extension/soft-dimmer/dbus_client.js \
                gnome-extension/soft-dimmer/metadata.json
EXT_BIN       ?= /usr/bin/gnome-extensions
COMPILE_SCHEMAS ?= /usr/bin/glib-compile-schemas
dist:
	git archive --format=tar.gz --prefix=$(BIN)-$(VERSION)/ \
	  -o $(BIN)-$(VERSION).tar.gz HEAD

clean:
	rm -f $(BIN)

.PHONY: test install uninstall dist clean
