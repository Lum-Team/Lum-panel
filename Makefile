# Compiler and flags
CC = gcc
CFLAGS = -Wall -g
LDFLAGS = `pkg-config --cflags --libs gtk+-3.0`

# Installation paths
PREFIX = /usr/local

# Default target
all: lum-panel

# Build target
lum-panel: panel.c
	$(CC) $(CFLAGS) -o lum-panel panel.c $(LDFLAGS)

# Installation target
install: lum-panel
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install -m 755 lum-panel $(DESTDIR)$(PREFIX)/bin/lum-panel

# Uninstallation target
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/lum-panel

# Clean target
clean:
	rm -f lum-panel panel_simple *.o

# Phony targets
.PHONY: all install uninstall clean
