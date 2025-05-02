CC = gcc
CFLAGS = -Wall -g
LDFLAGS = `pkg-config --cflags --libs gtk+-3.0`
PREFIX = /usr/local

all: lum-panel panel_simple

lum-panel: panel.c
	$(CC) $(CFLAGS) -o lum-panel panel.c $(LDFLAGS)

panel_simple: panel_simple.c
	$(CC) $(CFLAGS) -o panel_simple panel_simple.c $(LDFLAGS)

install: lum-panel
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install -m 755 lum-panel $(DESTDIR)$(PREFIX)/bin/lum-panel

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/lum-panel

clean:
	rm -f lum-panel panel_simple *.o

.PHONY: all install uninstall clean