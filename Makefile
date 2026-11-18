# edifierctl: one binary, libdbus as the only dependency besides libc.
# Tests link the pure modules only (no Bluetooth, no D-Bus, no I/O).

CC ?= cc
PREFIX ?= /usr/local
CFLAGS ?= -O2
CPPFLAGS += -D_POSIX_C_SOURCE=200809L
WARN = -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wconversion -Wsign-conversion -Werror
PKG_CONFIG ?= pkg-config
DBUS_CFLAGS = $(shell $(PKG_CONFIG) --cflags dbus-1)
DBUS_LIBS = $(shell $(PKG_CONFIG) --libs dbus-1)

CORE = src/frame.c src/model.c src/decode.c
APP = src/dbus.c src/bluez.c src/commands.c src/main.c
TESTS = tests/run.c tests/test_frame.c tests/test_model.c tests/test_decode.c

all: edifierctl

edifierctl: $(CORE) $(APP) src/*.h
	$(CC) $(CPPFLAGS) $(WARN) $(CFLAGS) $(DBUS_CFLAGS) -o $@ $(CORE) $(APP) $(LDFLAGS) $(DBUS_LIBS)

tests/run: $(CORE) $(TESTS) src/*.h tests/*.h
	$(CC) $(CPPFLAGS) $(WARN) $(CFLAGS) -Isrc -o $@ $(CORE) $(TESTS) $(LDFLAGS)

test: tests/run
	./tests/run

install: edifierctl
	install -D -m 0755 edifierctl $(DESTDIR)$(PREFIX)/bin/edifierctl

clean:
	rm -f edifierctl tests/run

.PHONY: all test install clean
