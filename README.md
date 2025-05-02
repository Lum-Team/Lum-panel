--- README.md
+++ README.md
@@ -0,0 +1,138 @@
+# Lum-panel
+
+A lightweight panel for Linux desktop environments, developed by Lum & Bakslerz Team.
+
+![Lum-panel Screenshot](screenshot.png)
+
+## Features
+
+- Clean and minimalist desktop panel
+- System tray for applications
+- Clock with 12/24-hour format toggle
+- Start menu with categorized applications
+- Application search functionality
+- Network status indicator
+- Volume control
+- Battery status indicator
+
+## Dependencies
+
+Before installing Lum-panel, make sure you have the following dependencies installed:
+
+### Debian/Ubuntu-based systems:
+```bash
+sudo apt update
+sudo apt install build-essential pkg-config libgtk-3-dev
+```
+
+### Fedora:
+```bash
+sudo dnf install gcc make pkgconfig gtk3-devel
+```
+
+### Arch Linux:
+```bash
+sudo pacman -S base-devel gtk3
+```
+
+## Installation
+
+### From source
+
+1. Clone the repository:
+```bash
+git clone https://github.com/yourusername/lum-panel.git
+cd lum-panel
+```
+
+2. Build the application:
+```bash
+make
+```
+
+3. Install the application (optional):
+```bash
+sudo make install
+```
+
+By default, this will install Lum-panel to `/usr/local/bin/lum-panel`. You can change the installation prefix by setting the `PREFIX` variable:
+
+```bash
+sudo make PREFIX=/usr install
+```
+
+## Usage
+
+### Running Lum-panel
+
+If you installed Lum-panel:
+```bash
+lum-panel
+```
+
+If you didn't install it, you can run it from the build directory:
+```bash
+./lum-panel
+```
+
+### Adding to startup
+
+To add Lum-panel to your startup applications:
+
+1. Create a desktop entry file:
+```bash
+echo "[Desktop Entry]
+Type=Application
+Name=Lum-panel
+Exec=lum-panel
+Terminal=false
+Categories=Utility;
+Comment=Lightweight panel for Linux desktop environments" > ~/.config/autostart/lum-panel.desktop
+```
+
+2. Make it executable:
+```bash
+chmod +x ~/.config/autostart/lum-panel.desktop
+```
+
+## Features
+
+### Start Menu
+- Click on the "Start" button to open the application menu
+- Applications are organized by categories
+- Use the "Search Applications..." option to find applications quickly
+
+### Clock
+- Click on the clock to toggle between 12-hour and 24-hour formats
+
+### System Tray
+- Shows running applications that support system tray
+
+### Network Status
+- Shows current network connection status
+
+### Volume Control
+- Adjust system volume directly from the panel
+
+### Battery Status
+- Shows current battery level and charging status (for laptops)
+
+## Uninstallation
+
+To uninstall Lum-panel:
+
+```bash
+sudo make uninstall
+```
+
+## License
+
+This project is licensed under the MIT License - see the LICENSE file for details.
+
+## Contributing
+
+Contributions are welcome! Please feel free to submit a Pull Request.
+
+## Acknowledgments
+
+- The Lum & Bakslerz Team for developing and maintaining this project
