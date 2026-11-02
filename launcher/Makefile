CC       ?= gcc
CXX      ?= g++
CFLAGS   ?= -O2 -g -Wall
CXXFLAGS ?= -O2 -g -Wall -Wextra -std=gnu++20
PKGS      = gtk4 libadwaita-1
PKG_CFLAGS = $(shell pkg-config --cflags $(PKGS))
PKG_LIBS   = $(shell pkg-config --libs $(PKGS))

all: launcher

resources.c: launcher.gresource.xml style.css data/io.github.asuscontrolcenter.Launcher.svg data/io.github.asuscontrolcenter.Launcher.desktop.in
	glib-compile-resources --target=$@ --generate-source $<

%.o: %.c
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(PKG_CFLAGS) -c -o $@ $<

launcher: launcher.o resources.o
	$(CXX) -o $@ $^ $(PKG_LIBS)

clean:
	rm -f launcher *.o resources.c

.PHONY: all clean
