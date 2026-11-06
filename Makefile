# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026 Hiroki Sato <hrs@allbsd.org>
# All rights reserved.

.POSIX:
CC = cc
PKG_CONFIG = pkg-config
CFLAGS = -O2 -Wall -Wextra -Wpedantic
PREFIX = /usr/local

all: swm

swm: swm.c
	$(CC) $(CPPFLAGS) $(CFLAGS) `$(PKG_CONFIG) --cflags x11` $(LDFLAGS) -o $@ swm.c `$(PKG_CONFIG) --libs x11`

install: swm
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp swm $(DESTDIR)$(PREFIX)/bin/swm

clean:
	rm -f swm tests/client

test: swm tests/client
	sh tests/smoke.sh

tests/client: tests/client.c
	$(CC) $(CPPFLAGS) $(CFLAGS) `$(PKG_CONFIG) --cflags x11` $(LDFLAGS) -o $@ tests/client.c `$(PKG_CONFIG) --libs x11`

.PHONY: all install clean test
