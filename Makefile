CC = gcc
CFLAGS = -Wall -g `pkg-config --cflags gtk+-3.0`
LIBS = `pkg-config --libs gtk+-3.0` -lX11

# Installation paths
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
DATADIR = $(PREFIX)/share
DESKTOPDIR = $(DATADIR)/applications
AUTOSTART_DIR = $(DATADIR)/autostart

all: lum-panel

lum-panel: lum-panel.c
	$(CC) $(CFLAGS) -o lum-panel lum-panel.c $(LIBS)

clean:
	rm -f lum-panel

install: lum-panel
	# Create directories if they don't exist
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(DESKTOPDIR)
	mkdir -p $(DESTDIR)$(AUTOSTART_DIR)
	
	# Install the executable
	install -m 755 lum-panel $(DESTDIR)$(BINDIR)/lum-panel
	
	# Create and install desktop entry
	@echo "[Desktop Entry]" > lum-panel.desktop
	@echo "Type=Application" >> lum-panel.desktop
	@echo "Name=LUM Panel" >> lum-panel.desktop
	@echo "Comment=Lightweight panel for Linux desktop environments" >> lum-panel.desktop
	@echo "Exec=$(BINDIR)/lum-panel" >> lum-panel.desktop
	@echo "Terminal=false" >> lum-panel.desktop
	@echo "Categories=Utility;System;" >> lum-panel.desktop
	@echo "StartupNotify=false" >> lum-panel.desktop
	@echo "X-GNOME-Autostart-enabled=true" >> lum-panel.desktop
	
	# Install desktop entry to applications and autostart
	install -m 644 lum-panel.desktop $(DESTDIR)$(DESKTOPDIR)/lum-panel.desktop
	install -m 644 lum-panel.desktop $(DESTDIR)$(AUTOSTART_DIR)/lum-panel.desktop
	
	# Clean up temporary desktop file
	rm -f lum-panel.desktop
	
	@echo "Installation complete. LUM Panel has been installed to $(BINDIR)/lum-panel"
	@echo "A desktop entry has been created in $(DESKTOPDIR) and $(AUTOSTART_DIR)"

uninstall:
	# Remove the executable
	rm -f $(DESTDIR)$(BINDIR)/lum-panel
	
	# Remove desktop entries
	rm -f $(DESTDIR)$(DESKTOPDIR)/lum-panel.desktop
	rm -f $(DESTDIR)$(AUTOSTART_DIR)/lum-panel.desktop
	
	@echo "LUM Panel has been uninstalled"

.PHONY: all clean install uninstall
