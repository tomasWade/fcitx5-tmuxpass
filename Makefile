CXX ?= g++
CXXFLAGS += -std=c++20 -O2 -Wall -fPIC
INC = -I/usr/include/Fcitx5/Core -I/usr/include/Fcitx5/Utils -I/usr/include/Fcitx5/Config
PREFIX ?= /usr
DESTDIR ?=
ADDON_LIB = $(DESTDIR)$(PREFIX)/lib/fcitx5
ADDON_CONF = $(DESTDIR)$(PREFIX)/share/fcitx5/addon

all: libtmuxpass.so

libtmuxpass.so: tmuxpass.cpp
	$(CXX) $(CXXFLAGS) $(INC) -shared $< -o $@

install: libtmuxpass.so
	install -Dm755 libtmuxpass.so -t $(ADDON_LIB)/
	install -Dm644 tmuxpass.conf -t $(ADDON_CONF)/

uninstall:
	rm -f $(ADDON_LIB)/libtmuxpass.so $(ADDON_CONF)/tmuxpass.conf

clean:
	rm -f libtmuxpass.so

.PHONY: all install uninstall clean
