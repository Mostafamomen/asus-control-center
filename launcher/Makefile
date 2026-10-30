CXX ?= g++
CXXFLAGS ?= -O2 -g -Wall -Wextra -std=gnu++20
PKGS = gtk4 libadwaita-1

launcher: launcher.cpp
	$(CXX) $(CXXFLAGS) $(shell pkg-config --cflags $(PKGS)) -o $@ $^ $(shell pkg-config --libs $(PKGS))

clean:
	rm -f launcher

.PHONY: clean
