# ASUSCraft host agent
# SPDX-License-Identifier: GPL-3.0-or-later
#
# A small, unprivileged, D-Bus-activated service that runs on the HOST so a
# Flatpak build of ASUSCraft can still reach the machine's hardware and its
# desktop session. It is installed as its own package; the app works without
# it.
#
#   make            build
#   make check      the pure-logic self test (no bus, no hardware)
#   make install    install the binary, the D-Bus service file and the
#                   bundled Soft Brightness Plus

TOPDIR     ?= ..
CXX        ?= g++
CXXFLAGS   ?= -O2 -g -Wall -Wextra
CXXFLAGS   += -std=gnu++20
PKGS       = gio-2.0 glib-2.0

PREFIX     ?= /usr
LIBEXECDIR ?= $(PREFIX)/libexec/asuscraft-agent
DBUSDIR    ?= $(PREFIX)/share/dbus-1/services
AGENTDIR   ?= $(PREFIX)/share/asuscraft-agent

BIN  = asuscraft-agent
SERVICE = io.github.asuscontrolcenter.Agent

SRCS = main.cpp \
       $(TOPDIR)/src/control/commands.cpp \
       $(TOPDIR)/src/control/nlsettings.cpp \
       $(TOPDIR)/src/control/sbp_local.cpp

$(BIN): $(SRCS) $(TOPDIR)/src/control/commands.h \
        $(TOPDIR)/src/control/sbp_local.h $(TOPDIR)/src/control/agentproto.h \
        $(TOPDIR)/src/control/nlsettings.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I$(TOPDIR)/src/control \
	  -DACC_SBP_SYSTEM_DIR='"$(AGENTDIR)/soft-brightness-plus"' \
	  $(shell pkg-config --cflags $(PKGS)) -o $@ $(SRCS) \
	  $(LDFLAGS) $(shell pkg-config --libs $(PKGS))

check: $(BIN)
	./$(BIN) --check

install: $(BIN)
	install -Dm0755 $(BIN) $(DESTDIR)$(LIBEXECDIR)/$(BIN)
	install -d $(DESTDIR)$(DBUSDIR)
	sed 's|@EXEC@|$(LIBEXECDIR)/$(BIN)|' $(SERVICE).service.in \
	  > $(DESTDIR)$(DBUSDIR)/$(SERVICE).service
	chmod 0644 $(DESTDIR)$(DBUSDIR)/$(SERVICE).service
	install -d $(DESTDIR)$(AGENTDIR)
	cd $(TOPDIR)/data && find soft-brightness-plus -type f -exec \
	  install -Dm0644 {} $(DESTDIR)$(AGENTDIR)/{} \;

uninstall:
	rm -f $(DESTDIR)$(LIBEXECDIR)/$(BIN) \
	      $(DESTDIR)$(DBUSDIR)/$(SERVICE).service
	rm -rf $(DESTDIR)$(AGENTDIR)

clean:
	rm -f $(BIN)

.PHONY: check install uninstall clean
