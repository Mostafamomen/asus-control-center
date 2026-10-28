# ASUS Control Center
# SPDX-License-Identifier: GPL-3.0-or-later

CXX      ?= g++
CXXFLAGS ?= -O2 -g -Wall -Wextra -std=gnu++20
PKGS      = gtk4 libadwaita-1

SRCS = src/main.cpp \
       src/i18n/i18n.cpp \
       src/probe/probe.cpp src/probe/battery.cpp src/probe/dmi.cpp \
       src/probe/hwmon.cpp src/probe/gpu.cpp src/probe/display.cpp \
       src/probe/sysinfo.cpp src/probe/load.cpp src/probe/devs.cpp \
       src/control/brightness.cpp src/control/ctl.cpp src/control/asusd.cpp \
       src/user/user.cpp \
       src/ui/window.cpp src/ui/home.cpp src/ui/theme.cpp src/ui/ring.cpp \
       src/ui/spark.cpp src/ui/pages.cpp src/ui/mcui.cpp src/ui/login.cpp \
       src/ui/specs.cpp

BIN = asus-control-center

$(BIN): $(SRCS)
	$(CXX) $(CXXFLAGS) $(shell pkg-config --cflags $(PKGS)) -o $@ $^ $(shell pkg-config --libs $(PKGS))

test: $(BIN)
	./$(BIN) --self-test

clean:
	rm -f $(BIN)

.PHONY: test clean
