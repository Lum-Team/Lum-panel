# LUM Panel

Lightweight panel for Linux desktop environments with support for various window managers including i3.

## Features

- Clock with date and time display
- Network status and management
- Volume control
- Battery status indicator
- Workspace switcher (compatible with multiple window managers including i3, GNOME, KDE, XFWM, and others)
- Customizable appearance (opacity, colors, auto-hide)

## Dependencies

- GTK+ 3.0
- wmctrl (required for most window managers including GNOME)
- i3-msg (optional, for i3 window manager support)
- gdbus or dbus-send (optional, alternative method for GNOME Shell support)
- qdbus (optional, for KDE/KWin support)

The panel will automatically detect your window manager and use the appropriate method for workspace switching.

## Building

To build the panel, simply run:

```bash
make
```

## Installation

### System-wide installation

To install LUM Panel system-wide (requires root privileges):

```bash
sudo make install
```

This will:
- Install the executable to `/usr/local/bin/lum-panel`
- Create desktop entries in `/usr/local/share/applications/` and `/usr/local/share/autostart/`

### Custom installation path

You can specify a custom installation prefix:

```bash
sudo make install PREFIX=/usr
```

### User installation (no root required)

To install for the current user only:

```bash
make install PREFIX=$HOME/.local
```

## Uninstallation

To uninstall LUM Panel:

```bash
sudo make uninstall
```

If you installed with a custom prefix, specify the same prefix:

```bash
sudo make uninstall PREFIX=/usr
```

Or for user installation:

```bash
make uninstall PREFIX=$HOME/.local
```

## Usage

After installation, LUM Panel can be started from your application menu or will start automatically on login.

To start it manually:

```bash
lum-panel
```
