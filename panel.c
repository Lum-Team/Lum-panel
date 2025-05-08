#define _GNU_SOURCE  // Dla strcasestr
#include <gtk/gtk.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PANEL_HEIGHT 30  // Zwiększona wysokość panelu

GtkWidget *clock_label;
GtkWidget *start_menu;
GtkWidget *network_icon;
GtkWidget *volume_button;
GtkWidget *battery_label;
gboolean is_24h = TRUE;

const char *app_dirs[] = {
    "/usr/share/applications",
    "~/.local/share/applications",
    NULL
};

typedef struct {
    char *name;
    char *exec;
    char **categories;
} AppEntry;

// Predefined categories mapping
typedef struct {
    const char *category_id;    // Standard desktop file category
    const char *display_name;   // User-friendly display name in English
} CategoryMapping;

const CategoryMapping predefined_categories[] = {
    {"Game", "Games"},
    {"Network", "Internet"},
    {"Internet", "Internet"},
    {"WebBrowser", "Internet"},
    {"Email", "Internet"},
    {"System", "System"},
    {"Settings", "System"},
    {"Utility", "Utilities"},
    {"Office", "Office"},
    {"Graphics", "Graphics"},
    {"AudioVideo", "Multimedia"},
    {"Audio", "Multimedia"},
    {"Video", "Multimedia"},
    {"Development", "Development"},
    {"Education", "Education"},
    {"Accessories", "Accessories"},
    {NULL, NULL}
};

GHashTable *category_menus;
GtkWidget *all_apps_menu;
GtkWidget *search_results_menu;
GList *all_applications = NULL;  // Lista wszystkich aplikacji do wyszukiwania
GHashTable *category_mapping;

char* get_field(const char *line, const char *key) {
    if (g_str_has_prefix(line, key)) {
        return g_strdup(line + strlen(key));
    }
    return NULL;
}

void launch_app(GtkWidget *item, gpointer user_data) {
    const char *cmd = user_data;
    if (cmd) {
        char command[512];
        snprintf(command, sizeof(command), "%s &", cmd);
        printf("Executing command: %s\n", command);
        system(command);
        
        // Zamknij menu Start po uruchomieniu aplikacji
        if (gtk_widget_get_visible(start_menu)) {
            gtk_widget_hide(start_menu);
        }
    } else {
        printf("Error: No command to execute\n");
    }
}

// Get the display name for a category
const char* get_category_display_name(const char *category_id) {
    // First check if we have a direct mapping
    const char *display_name = g_hash_table_lookup(category_mapping, category_id);
    if (display_name) {
        return display_name;
    }
    
    // If no mapping found, return the original category
    return category_id;
}

void add_app_to_category(AppEntry *entry, const char *category) {
    if (!entry->name || !entry->exec || !category) return;

    // Get the display name for this category
    const char *display_name = get_category_display_name(category);
    
    // Look up the submenu for this category
    GtkWidget *submenu = g_hash_table_lookup(category_menus, display_name);
    if (!submenu) {
        // If no predefined category exists, create a new one
        submenu = gtk_menu_new();
        GtkWidget *cat_item = gtk_menu_item_new_with_label(display_name);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(cat_item), submenu);
        gtk_menu_shell_append(GTK_MENU_SHELL(start_menu), cat_item);
        gtk_widget_show_all(cat_item);
        g_hash_table_insert(category_menus, g_strdup(display_name), submenu);
    }

    GtkWidget *app_item = gtk_menu_item_new_with_label(entry->name);
    g_signal_connect(app_item, "activate", G_CALLBACK(launch_app), g_strdup(entry->exec));
    gtk_menu_shell_append(GTK_MENU_SHELL(submenu), app_item);
    gtk_widget_show(app_item);
}

void add_app_to_all(AppEntry *entry) {
    GtkWidget *app_item = gtk_menu_item_new_with_label(entry->name);
    g_signal_connect(app_item, "activate", G_CALLBACK(launch_app), g_strdup(entry->exec));
    gtk_menu_shell_append(GTK_MENU_SHELL(all_apps_menu), app_item);
    gtk_widget_show(app_item);
    
    // Dodaj aplikację do listy wszystkich aplikacji do wyszukiwania
    AppEntry *app_copy = g_new0(AppEntry, 1);
    app_copy->name = g_strdup(entry->name);
    app_copy->exec = g_strdup(entry->exec);
    all_applications = g_list_append(all_applications, app_copy);
}

// Lista aplikacji do ukrycia
const char *hidden_apps[] = {
    "xwalynad",
    "polkit-gnome-authentication-agent-1",
    "nm-applet",
    "pulseaudio",
    "xfce4-notifyd",
    "xfce4-power-manager",
    "xfce4-screensaver",
    "xfce4-session",
    "xfce4-settings-helper",
    "xfce4-volumed",
    "xfdesktop",
    "xfwm4",
    "xscreensaver",
    "notification-daemon",
    "gnome-keyring-daemon",
    "gvfs-daemon",
    "dbus-daemon",
    "at-spi-bus-launcher",
    "at-spi2-registryd",
    NULL
};

// Sprawdź, czy aplikacja powinna być ukryta
gboolean should_hide_app(const char *exec, const char *name) {
    // Sprawdź, czy nazwa lub komenda wykonawcza zawiera nazwę aplikacji do ukrycia
    for (int i = 0; hidden_apps[i] != NULL; i++) {
        if (exec && strstr(exec, hidden_apps[i])) {
            return TRUE;
        }
        if (name && strcasestr(name, hidden_apps[i])) {
            return TRUE;
        }
    }
    
    // Sprawdź, czy komenda zawiera opcje, które sugerują, że to aplikacja systemowa
    if (exec && (strstr(exec, "--daemon") || 
                strstr(exec, "--no-daemon") || 
                strstr(exec, "--autostart") ||
                strstr(exec, "--sm-client-id"))) {
        return TRUE;
    }
    
    return FALSE;
}

void parse_desktop_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return;

    AppEntry entry = {0};
    char line[512];
    gboolean in_entry = FALSE;
    gboolean no_display = FALSE;

    while (fgets(line, sizeof(line), fp)) {
        if (g_str_has_prefix(line, "[Desktop Entry]")) {
            in_entry = TRUE;
            continue;
        }
        if (!in_entry) continue;

        g_strstrip(line);
        if (line[0] == '#' || strlen(line) == 0) continue;

        char *val;
        if ((val = get_field(line, "Name="))) {
            if (!entry.name) entry.name = val; else g_free(val);
        } else if ((val = get_field(line, "Exec="))) {
            if (!entry.exec) entry.exec = val; else g_free(val);
        } else if ((val = get_field(line, "Categories="))) {
            if (!entry.categories) entry.categories = g_strsplit(g_strdup(val), ";", -1);
            g_free(val);
        } else if ((val = get_field(line, "NoDisplay="))) {
            if (g_ascii_strcasecmp(val, "true") == 0) {
                no_display = TRUE;
            }
            g_free(val);
        } else if ((val = get_field(line, "Hidden="))) {
            if (g_ascii_strcasecmp(val, "true") == 0) {
                no_display = TRUE;
            }
            g_free(val);
        } else if ((val = get_field(line, "OnlyShowIn="))) {
            // Jeśli aplikacja jest przeznaczona tylko dla określonego środowiska
            // i nie jest to nasze środowisko, ukryj ją
            if (!strstr(val, "XFCE") && !strstr(val, "X-Generic")) {
                no_display = TRUE;
            }
            g_free(val);
        }
    }

    if (entry.name && entry.exec && !no_display) {
        // Sprawdź, czy aplikacja powinna być ukryta
        if (!should_hide_app(entry.exec, entry.name)) {
            // Add application to "All Applications"
            add_app_to_all(&entry);

            // Add application to defined categories
            if (entry.categories) {
                for (int i = 0; entry.categories[i] != NULL; i++) {
                    if (strlen(entry.categories[i]) > 0)
                        add_app_to_category(&entry, entry.categories[i]);
                }
            }
        }
    }

    g_free(entry.name);
    g_free(entry.exec);
    g_strfreev(entry.categories);
    fclose(fp);
}

// Create predefined category menus
void create_predefined_categories(GtkWidget *menu) {
    // Create menus for predefined categories
    for (int i = 0; predefined_categories[i].category_id != NULL; i++) {
        const char *display_name = predefined_categories[i].display_name;
        
        // Skip if we already created this display category
        if (g_hash_table_lookup(category_menus, display_name)) {
            continue;
        }
        
        GtkWidget *submenu = gtk_menu_new();
        GtkWidget *cat_item = gtk_menu_item_new_with_label(display_name);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(cat_item), submenu);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), cat_item);
        gtk_widget_show_all(cat_item);
        g_hash_table_insert(category_menus, g_strdup(display_name), submenu);
    }
}

// Deklaracja funkcji wyszukiwania aplikacji i About
static void search_applications_callback(GtkWidget *entry, gpointer user_data);
static void open_search_dialog(GtkWidget *widget, gpointer user_data);
static void show_about_dialog(GtkWidget *widget, gpointer user_data);

GtkWidget* create_start_menu_full(void) {
    GtkWidget *menu = gtk_menu_new();
    category_menus = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    
    // Initialize category mapping hash table
    category_mapping = g_hash_table_new(g_str_hash, g_str_equal);
    for (int i = 0; predefined_categories[i].category_id != NULL; i++) {
        g_hash_table_insert(category_mapping, 
                           (gpointer)predefined_categories[i].category_id, 
                           (gpointer)predefined_categories[i].display_name);
    }

    // Dodaj opcję "Search" do menu
    GtkWidget *search_item = gtk_menu_item_new_with_label("Search Applications...");
    g_signal_connect(search_item, "activate", G_CALLBACK(open_search_dialog), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), search_item);
    gtk_widget_show(search_item);
    
    // Zachowaj menu "All Applications" dla kompatybilności, ale nie pokazuj go
    all_apps_menu = gtk_menu_new();
    
    // Create predefined category menus
    create_predefined_categories(menu);

    // Parse desktop files and add applications
    for (int i = 0; app_dirs[i]; i++) {
        const char *dir = app_dirs[i];
        if (dir[0] == '~') dir = g_strconcat(g_get_home_dir(), dir + 1, NULL);

        DIR *d = opendir(dir);
        if (!d) continue;

        struct dirent *entry;
        while ((entry = readdir(d)) != NULL) {
            if (g_str_has_suffix(entry->d_name, ".desktop")) {
                char *fullpath = g_build_filename(dir, entry->d_name, NULL);
                parse_desktop_file(fullpath);
                g_free(fullpath);
            }
        }
        closedir(d);
    }

    // Add separator
    GtkWidget *separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
    gtk_widget_show(separator);
    
    // Add logout/shutdown options
    GtkWidget *logout = gtk_menu_item_new_with_label("Log Out");
    g_signal_connect(logout, "activate", G_CALLBACK(launch_app), g_strdup("pkill -KILL -u $USER"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), logout);
    gtk_widget_show(logout);
    
    GtkWidget *shutdown = gtk_menu_item_new_with_label("Shutdown");
    g_signal_connect(shutdown, "activate", G_CALLBACK(launch_app), g_strdup("shutdown -h now"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), shutdown);
    gtk_widget_show(shutdown);
    
    // Dodaj separator
    GtkWidget *separator2 = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator2);
    gtk_widget_show(separator2);
    
    // Dodaj opcję "About"
    GtkWidget *about_item = gtk_menu_item_new_with_label("About");
    g_signal_connect(about_item, "activate", G_CALLBACK(show_about_dialog), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), about_item);
    gtk_widget_show(about_item);
    
    gtk_widget_show_all(menu);
    return menu;
}

static gboolean update_clock(gpointer data) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char buffer[64];

    if (is_24h) {
        strftime(buffer, sizeof(buffer), "%H:%M:%S", tm_info);
    } else {
        strftime(buffer, sizeof(buffer), "%I:%M:%S %p", tm_info);
    }

    gtk_label_set_text(GTK_LABEL(clock_label), buffer);
    return TRUE;
}

static gboolean on_clock_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        is_24h = !is_24h;
    }
    return FALSE;
}

// Network menu and status functions
GtkWidget *network_menu = NULL;

// Forward declarations
static gboolean update_network_status(gpointer data);

typedef struct {
    char *interface;
    char *name;
    gboolean is_wifi;
    gboolean is_connected;
} NetworkInterface;

// Function to toggle network interface
static void toggle_network_interface(GtkWidget *widget, gpointer user_data) {
    NetworkInterface *iface = (NetworkInterface *)user_data;
    char command[256];
    
    if (iface->is_connected) {
        // Disconnect
        if (iface->is_wifi) {
            snprintf(command, sizeof(command), "nmcli connection down id \"%s\" &", iface->name);
        } else {
            snprintf(command, sizeof(command), "nmcli device disconnect %s &", iface->interface);
        }
    } else {
        // Connect
        if (iface->is_wifi) {
            snprintf(command, sizeof(command), "nmcli connection up id \"%s\" &", iface->name);
        } else {
            snprintf(command, sizeof(command), "nmcli device connect %s &", iface->interface);
        }
    }
    
    system(command);
}

// Function to connect to a WiFi network
static void connect_to_wifi(GtkWidget *widget, gpointer user_data) {
    const char *ssid = (const char *)user_data;
    char command[256];
    
    // Try to connect to the selected WiFi network
    snprintf(command, sizeof(command), "nmcli device wifi connect \"%s\" &", ssid);
    system(command);
}

// Function to refresh WiFi networks
static void refresh_wifi_networks(GtkWidget *widget, gpointer user_data) {
    system("nmcli device wifi rescan &");
    // Wait a bit for the scan to complete and then recreate the menu
    g_timeout_add_seconds(2, (GSourceFunc)gtk_widget_destroy, network_menu);
}

// Function to open network settings
static void open_network_settings(GtkWidget *widget, gpointer user_data) {
    // Try different network configuration tools
    if (system("which nm-connection-editor > /dev/null 2>&1") == 0) {
        system("nm-connection-editor &");
    } else if (system("which gnome-control-center > /dev/null 2>&1") == 0) {
        system("gnome-control-center network &");
    } else if (system("which systemsettings5 > /dev/null 2>&1") == 0) {
        system("systemsettings5 kcm_networkmanagement &");
    }
}

// Network status functions
static gboolean check_network_status() {
    struct ifaddrs *ifaddr, *ifa;
    gboolean has_connection = FALSE;
    
    if (getifaddrs(&ifaddr) == -1) {
        return FALSE;
    }
    
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;
        
        // Check for IPv4 or IPv6 addresses
        if ((ifa->ifa_addr->sa_family == AF_INET || ifa->ifa_addr->sa_family == AF_INET6) &&
            // Skip loopback interfaces
            strcmp(ifa->ifa_name, "lo") != 0) {
            has_connection = TRUE;
            break;
        }
    }
    
    freeifaddrs(ifaddr);
    return has_connection;
}

// Create network menu with available interfaces and WiFi networks
static GtkWidget* create_network_menu() {
    GtkWidget *menu = gtk_menu_new();
    FILE *fp;
    char line[512];
    GList *interfaces = NULL;
    GList *wifi_networks = NULL;
    gboolean has_wifi = FALSE;
    
    // Get network interfaces using nmcli
    fp = popen("nmcli -t -f DEVICE,TYPE,STATE,CONNECTION device", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            g_strchomp(line);
            char **parts = g_strsplit(line, ":", 4);
            if (parts && parts[0] && parts[1] && parts[2] && parts[3]) {
                // Skip loopback
                if (strcmp(parts[1], "loopback") != 0) {
                    NetworkInterface *iface = g_new0(NetworkInterface, 1);
                    iface->interface = g_strdup(parts[0]);
                    iface->name = g_strdup(parts[3]);
                    iface->is_wifi = (strcmp(parts[1], "wifi") == 0);
                    iface->is_connected = (strcmp(parts[2], "connected") == 0);
                    
                    interfaces = g_list_append(interfaces, iface);
                    
                    if (iface->is_wifi) {
                        has_wifi = TRUE;
                    }
                }
            }
            g_strfreev(parts);
        }
        pclose(fp);
    }
    
    // Add interfaces to menu
    if (interfaces) {
        GtkWidget *header = gtk_menu_item_new_with_label("Network Interfaces");
        gtk_widget_set_sensitive(header, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), header);
        gtk_widget_show(header);
        
        GList *iter;
        for (iter = interfaces; iter; iter = iter->next) {
            NetworkInterface *iface = (NetworkInterface *)iter->data;
            char label[256];
            
            if (strlen(iface->name) > 0) {
                snprintf(label, sizeof(label), "%s (%s): %s", 
                         iface->interface, 
                         iface->is_wifi ? "WiFi" : "Wired",
                         iface->is_connected ? "Connected" : "Disconnected");
            } else {
                snprintf(label, sizeof(label), "%s (%s): %s", 
                         iface->interface, 
                         iface->is_wifi ? "WiFi" : "Wired",
                         iface->is_connected ? "Connected" : "Disconnected");
            }
            
            GtkWidget *item = gtk_menu_item_new_with_label(label);
            g_signal_connect(item, "activate", G_CALLBACK(toggle_network_interface), iface);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
            gtk_widget_show(item);
        }
        
        // Add separator
        GtkWidget *separator = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
        gtk_widget_show(separator);
    }
    
    // If we have WiFi, add available networks
    if (has_wifi) {
        // Get available WiFi networks
        fp = popen("nmcli -t -f SSID,SIGNAL,SECURITY device wifi list", "r");
        if (fp) {
            GtkWidget *wifi_header = gtk_menu_item_new_with_label("Available WiFi Networks");
            gtk_widget_set_sensitive(wifi_header, FALSE);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), wifi_header);
            gtk_widget_show(wifi_header);
            
            int count = 0;
            while (fgets(line, sizeof(line), fp) && count < 10) {
                g_strchomp(line);
                char **parts = g_strsplit(line, ":", 3);
                if (parts && parts[0] && parts[1] && strlen(parts[0]) > 0) {
                    char label[256];
                    int signal = atoi(parts[1]);
                    const char *security = parts[2] && strlen(parts[2]) > 0 ? parts[2] : "Open";
                    
                    snprintf(label, sizeof(label), "%s (Signal: %d%%, Security: %s)", 
                             parts[0], signal, security);
                    
                    GtkWidget *item = gtk_menu_item_new_with_label(label);
                    g_signal_connect(item, "activate", G_CALLBACK(connect_to_wifi), g_strdup(parts[0]));
                    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
                    gtk_widget_show(item);
                    count++;
                }
                g_strfreev(parts);
            }
            pclose(fp);
            
            // Add refresh option
            GtkWidget *refresh = gtk_menu_item_new_with_label("Refresh WiFi Networks");
            g_signal_connect(refresh, "activate", G_CALLBACK(refresh_wifi_networks), NULL);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), refresh);
            gtk_widget_show(refresh);
        }
    }
    
    // Add separator
    GtkWidget *separator2 = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator2);
    gtk_widget_show(separator2);
    
    // Add network settings option
    GtkWidget *settings = gtk_menu_item_new_with_label("Network Settings...");
    g_signal_connect(settings, "activate", G_CALLBACK(open_network_settings), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), settings);
    gtk_widget_show(settings);
    
    return menu;
}

static void on_network_icon_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        // Create a new menu each time to ensure it's up to date
        if (network_menu) {
            gtk_widget_destroy(network_menu);
        }
        
        network_menu = create_network_menu();
        // Use gtk_menu_popup_at_pointer to prevent menu from disappearing
        gtk_menu_popup_at_pointer(GTK_MENU(network_menu), (GdkEvent*)event);
    }
}

static void update_network_icon() {
    gboolean connected = check_network_status();
    
    if (connected) {
        gtk_image_set_from_icon_name(GTK_IMAGE(network_icon), "network-transmit-receive", GTK_ICON_SIZE_MENU);
        gtk_widget_set_tooltip_text(network_icon, "Network connected - Click for options");
    } else {
        gtk_image_set_from_icon_name(GTK_IMAGE(network_icon), "network-offline", GTK_ICON_SIZE_MENU);
        gtk_widget_set_tooltip_text(network_icon, "Network disconnected - Click for options");
    }
}

static gboolean update_network_status(gpointer data) {
    update_network_icon();
    return TRUE; // Continue the timer
}

// Battery status functions
static gboolean update_battery_status(gpointer data) {
    FILE *capacity_file = fopen("/sys/class/power_supply/BAT0/capacity", "r");
    FILE *status_file = fopen("/sys/class/power_supply/BAT0/status", "r");
    
    if (!capacity_file || !status_file) {
        // No battery found or not accessible
        gtk_label_set_text(GTK_LABEL(battery_label), "");
        if (capacity_file) fclose(capacity_file);
        if (status_file) fclose(status_file);
        return TRUE;
    }
    
    int capacity;
    char status[20];
    char buffer[100];
    
    fscanf(capacity_file, "%d", &capacity);
    fscanf(status_file, "%s", status);
    
    const char *icon;
    if (strcmp(status, "Charging") == 0) {
        icon = "🔌";
    } else if (capacity <= 10) {
        icon = "🪫";
    } else if (capacity <= 25) {
        icon = "🔋";
    } else if (capacity <= 50) {
        icon = "🔋";
    } else if (capacity <= 75) {
        icon = "🔋";
    } else {
        icon = "🔋";
    }
    
    snprintf(buffer, sizeof(buffer), "%s %d%%", icon, capacity);
    gtk_label_set_text(GTK_LABEL(battery_label), buffer);
    
    fclose(capacity_file);
    fclose(status_file);
    return TRUE;
}

// Audio control variables and functions
GtkWidget *audio_menu = NULL;
gboolean is_muted = FALSE;
gboolean is_mic_muted = FALSE;

// Forward declarations for audio functions
static void on_volume_changed(GtkScaleButton *button, gdouble value, gpointer user_data);
static void on_mic_volume_changed(GtkRange *range, gpointer user_data);
static void toggle_mic_mute(GtkWidget *widget, gpointer user_data);

// Function to toggle mute
static void toggle_mute(GtkWidget *widget, gpointer user_data) {
    is_muted = !is_muted;
    
    // Toggle mute using amixer
    if (is_muted) {
        system("amixer -q set Master mute");
        gtk_scale_button_set_value(GTK_SCALE_BUTTON(volume_button), 0.0);
    } else {
        system("amixer -q set Master unmute");
        // Get current volume and set the button value
        FILE *fp = popen("amixer get Master | grep -o '[0-9]*%' | head -1 | tr -d '%'", "r");
        if (fp) {
            char vol_str[10];
            if (fgets(vol_str, sizeof(vol_str), fp)) {
                int vol = atoi(vol_str);
                gtk_scale_button_set_value(GTK_SCALE_BUTTON(volume_button), vol / 100.0);
            }
            pclose(fp);
        }
    }
}

// Function to open audio settings
static void open_audio_settings(GtkWidget *widget, gpointer user_data) {
    // Try different audio configuration tools
    if (system("which pavucontrol > /dev/null 2>&1") == 0) {
        system("pavucontrol &");
    } else if (system("which gnome-control-center > /dev/null 2>&1") == 0) {
        system("gnome-control-center sound &");
    } else if (system("which alsamixer > /dev/null 2>&1") == 0) {
        system("x-terminal-emulator -e alsamixer &");
    }
}

// Function to select audio output device
static void select_audio_device(GtkWidget *widget, gpointer user_data) {
    const char *device = (const char *)user_data;
    char cmd[256];
    
    // Set default sink using pactl (PulseAudio)
    snprintf(cmd, sizeof(cmd), "pactl set-default-sink \"%s\"", device);
    system(cmd);
}

// Function to select audio input device
static void select_audio_input(GtkWidget *widget, gpointer user_data) {
    const char *device = (const char *)user_data;
    char cmd[256];
    
    // Set default source using pactl (PulseAudio)
    snprintf(cmd, sizeof(cmd), "pactl set-default-source \"%s\"", device);
    system(cmd);
}

static void update_volume_icon() {
    // Get the current volume and mute state
    FILE *fp = popen("pactl get-sink-mute @DEFAULT_SINK@ | grep -q yes && echo muted || echo unmuted", "r");
    char mute_status[10] = {0};
    if (fp && fgets(mute_status, sizeof(mute_status), fp)) {
        g_strstrip(mute_status);
    }
    if (fp) pclose(fp);

    fp = popen("pactl get-sink-volume @DEFAULT_SINK@ | grep -o '[0-9]\\+%' | head -1 | tr -d '%'", "r");
    int volume = 0;
    if (fp && fscanf(fp, "%d", &volume) == 1) {
        pclose(fp);
    } else if (fp) {
        pclose(fp);
    }

    // Set the icon based on volume and mute state
    const char *icon_name = "audio-volume-muted";
    if (g_strcmp0(mute_status, "muted") == 0) {
        icon_name = "audio-volume-muted";
    } else if (volume == 0) {
        icon_name = "audio-volume-muted";
    } else if (volume <= 33) {
        icon_name = "audio-volume-low";
    } else if (volume <= 66) {
        icon_name = "audio-volume-medium";
    } else {
        icon_name = "audio-volume-high";
    }

    gtk_image_set_from_icon_name(GTK_IMAGE(volume_button), icon_name, GTK_ICON_SIZE_MENU);
}

static GtkWidget* create_audio_menu() {
    GtkWidget *menu = gtk_menu_new();
    FILE *fp;
    char line[512];

    // Add volume slider
    GtkWidget *volume_item = gtk_menu_item_new();
    GtkWidget *volume_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_widget_set_size_request(volume_scale, 150, -1);

    // Get current volume
    fp = popen("pactl get-sink-volume @DEFAULT_SINK@ | grep -o '[0-9]\\+%' | head -1 | tr -d '%'", "r");
    if (fp) {
        char vol_str[10];
        if (fgets(vol_str, sizeof(vol_str), fp)) {
            int vol = atoi(vol_str);
            gtk_range_set_value(GTK_RANGE(volume_scale), vol);
        }
        pclose(fp);
    }

    // Connect volume change signal
    g_signal_connect(volume_scale, "value-changed", G_CALLBACK(on_volume_changed), NULL);

    gtk_container_add(GTK_CONTAINER(volume_item), volume_scale);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), volume_item);
    gtk_widget_show_all(volume_item);

    // Add mute toggle
    GtkWidget *mute_item = gtk_check_menu_item_new_with_label("Mute");
    fp = popen("pactl get-sink-mute @DEFAULT_SINK@ | grep -q yes && echo muted || echo unmuted", "r");
    if (fp) {
        char mute_status[10];
        if (fgets(mute_status, sizeof(mute_status), fp)) {
            if (g_str_has_prefix(mute_status, "muted")) {
                gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mute_item), TRUE);
            }
        }
        pclose(fp);
    }
    g_signal_connect(mute_item, "toggled", G_CALLBACK(toggle_mute), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mute_item);
    gtk_widget_show(mute_item);

    // Add separator
    GtkWidget *separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
    gtk_widget_show(separator);

    // Add output devices if PipeWire is available
    if (system("which pw-cli > /dev/null 2>&1") == 0) {
        GtkWidget *devices_header = gtk_menu_item_new_with_label("Output Devices");
        gtk_widget_set_sensitive(devices_header, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), devices_header);
        gtk_widget_show(devices_header);

        fp = popen("pw-cli ls Node | grep -E 'node.id|node.description' | sed 'N;s/\\n/ /'", "r");
        if (fp) {
            char node_id[64] = "";
            char node_desc[256] = "";
            while (fgets(line, sizeof(line), fp)) {
                sscanf(line, "node.id = %s node.description = \"%[^\"]\"", node_id, node_desc);
                GtkWidget *device_item = gtk_menu_item_new_with_label(node_desc);
                g_signal_connect(device_item, "activate", G_CALLBACK(select_audio_device), g_strdup(node_id));
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), device_item);
                gtk_widget_show(device_item);
            }
            pclose(fp);
        }
    }

    return menu;
}

// Volume button click handler
static gboolean on_volume_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        // Create a new menu each time to ensure it's up to date
        if (audio_menu) {
            gtk_widget_destroy(audio_menu);
        }
        
        audio_menu = create_audio_menu();
        // Use gtk_menu_popup_at_pointer to prevent menu from disappearing
        gtk_menu_popup_at_pointer(GTK_MENU(audio_menu), (GdkEvent*)event);
        return TRUE; // Stop event propagation
    }
    return FALSE;
}

// Function to toggle microphone mute
static void toggle_mic_mute(GtkWidget *widget, gpointer user_data) {
    is_mic_muted = !is_mic_muted;
    
    // Toggle mute using amixer
    if (is_mic_muted) {
        system("amixer -q set Capture mute");
    } else {
        system("amixer -q set Capture unmute");
    }
}

// Microphone volume change callback
static void on_mic_volume_changed(GtkRange *range, gpointer user_data) {
    int vol_percent = (int)gtk_range_get_value(range);
    char cmd[100];
    
    // Set microphone volume using amixer
    snprintf(cmd, sizeof(cmd), "amixer -q set Capture %d%%", vol_percent);
    system(cmd);
    
    // Unmute if volume is increased
    if (vol_percent > 0) {
        is_mic_muted = FALSE;
        system("amixer -q set Capture unmute");
    }
}

// Volume change callback
static void on_volume_changed(GtkScaleButton *button, gdouble value, gpointer user_data) {
    char cmd[100];
    int vol_percent = (int)(value * 100);
    
    // Set volume using amixer and unmute if needed
    snprintf(cmd, sizeof(cmd), "amixer -q set Master %d%% unmute", vol_percent);
    system(cmd);
    
    // Update mute state
    if (vol_percent > 0) {
        is_muted = FALSE;
    }
}

static void on_start_button_clicked(GtkWidget *widget, gpointer data) {
    if (gtk_widget_get_visible(start_menu)) {
        gtk_widget_hide(start_menu);
    } else {
        // Use gtk_menu_popup_at_pointer to prevent menu from disappearing
        gtk_menu_popup_at_pointer(GTK_MENU(start_menu), NULL);
    }
}

static void on_browser_button_clicked(GtkWidget *widget, gpointer data) {
    system("xdg-open https://www.google.com &");
}

static void on_file_manager_button_clicked(GtkWidget *widget, gpointer data) {
    system("xdg-open ~ &");
}

static void on_terminal_button_clicked(GtkWidget *widget, gpointer data) {
    // Try to launch a terminal - try different common terminals
    if (system("which x-terminal-emulator > /dev/null 2>&1") == 0) {
        system("x-terminal-emulator &");
    } else if (system("which gnome-terminal > /dev/null 2>&1") == 0) {
        system("gnome-terminal &");
    } else if (system("which konsole > /dev/null 2>&1") == 0) {
        system("konsole &");
    } else if (system("which xterm > /dev/null 2>&1") == 0) {
        system("xterm &");
    }
}

static void build_panel(GtkWidget *panel) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(panel), box);

    // CSS dla usunięcia obramowań przycisków
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "button { border: none; background: none; padding: 2px; }"
        "button:hover { background-color: rgba(255,255,255,0.1); }", -1, NULL);
    GtkStyleContext *context;
    GdkScreen *screen = gdk_screen_get_default();
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    // Start button bez obramowania
    GtkWidget *start_button = gtk_button_new_with_label("Start");
    context = gtk_widget_get_style_context(start_button);
    gtk_style_context_add_class(context, "flat");
    gtk_box_pack_start(GTK_BOX(box), start_button, FALSE, FALSE, 2);

    start_menu = create_start_menu_full();
    g_signal_connect(start_button, "clicked", G_CALLBACK(on_start_button_clicked), NULL);
    
    // Add a separator
    GtkWidget *separator1 = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(box), separator1, FALSE, FALSE, 4);
    
    // Quick launch buttons bez obramowań
    // Web browser button
    GtkWidget *browser_button = gtk_button_new();
    GtkWidget *browser_icon = gtk_image_new_from_icon_name("web-browser", GTK_ICON_SIZE_MENU);
    gtk_button_set_image(GTK_BUTTON(browser_button), browser_icon);
    gtk_widget_set_tooltip_text(browser_button, "Web Browser");
    context = gtk_widget_get_style_context(browser_button);
    gtk_style_context_add_class(context, "flat");
    g_signal_connect(browser_button, "clicked", G_CALLBACK(on_browser_button_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(box), browser_button, FALSE, FALSE, 0);
    
    // File manager button
    GtkWidget *file_button = gtk_button_new();
    GtkWidget *file_icon = gtk_image_new_from_icon_name("system-file-manager", GTK_ICON_SIZE_MENU);
    gtk_button_set_image(GTK_BUTTON(file_button), file_icon);
    gtk_widget_set_tooltip_text(file_button, "File Manager");
    context = gtk_widget_get_style_context(file_button);
    gtk_style_context_add_class(context, "flat");
    g_signal_connect(file_button, "clicked", G_CALLBACK(on_file_manager_button_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(box), file_button, FALSE, FALSE, 0);
    
    // Terminal button
    GtkWidget *terminal_button = gtk_button_new();
    GtkWidget *terminal_icon = gtk_image_new_from_icon_name("utilities-terminal", GTK_ICON_SIZE_MENU);
    gtk_button_set_image(GTK_BUTTON(terminal_button), terminal_icon);
    gtk_widget_set_tooltip_text(terminal_button, "Terminal");
    context = gtk_widget_get_style_context(terminal_button);
    gtk_style_context_add_class(context, "flat");
    g_signal_connect(terminal_button, "clicked", G_CALLBACK(on_terminal_button_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(box), terminal_button, FALSE, FALSE, 0);

    // Add a spacer to push everything else to the right
    GtkWidget *spacer = gtk_label_new(NULL);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_pack_start(GTK_BOX(box), spacer, TRUE, TRUE, 0);

    // Clock - umieszczony przed ikoną sieci
    clock_label = gtk_label_new("");
    // Ustawiamy większą czcionkę dla zegara
    PangoAttrList *attr_list = pango_attr_list_new();
    pango_attr_list_insert(attr_list, pango_attr_scale_new(1.2));  // Zwiększamy rozmiar czcionki o 20%
    pango_attr_list_insert(attr_list, pango_attr_weight_new(PANGO_WEIGHT_BOLD));  // Pogrubiamy czcionkę
    gtk_label_set_attributes(GTK_LABEL(clock_label), attr_list);
    pango_attr_list_unref(attr_list);
    
    gtk_box_pack_end(GTK_BOX(box), clock_label, FALSE, FALSE, 10);
    g_signal_connect(clock_label, "button-press-event", G_CALLBACK(on_clock_button_press), NULL);
    g_timeout_add_seconds(1, update_clock, NULL);

    // Sound applet
    GtkWidget *volume_event_box = gtk_event_box_new();
    volume_button = gtk_image_new_from_icon_name("audio-volume-medium", GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(volume_event_box), volume_button);
    gtk_box_pack_end(GTK_BOX(box), volume_event_box, FALSE, FALSE, 4);
    g_signal_connect(volume_event_box, "button-press-event", G_CALLBACK(on_volume_button_press), NULL);
    g_timeout_add_seconds(1, (GSourceFunc)update_volume_icon, NULL);

    // Network status icon with click event
    GtkWidget *network_event_box = gtk_event_box_new();
    network_icon = gtk_image_new_from_icon_name("network-offline", GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(network_event_box), network_icon);
    gtk_box_pack_end(GTK_BOX(box), network_event_box, FALSE, FALSE, 4);
    g_signal_connect(network_event_box, "button-press-event", G_CALLBACK(on_network_icon_clicked), NULL);
    update_network_icon();
    g_timeout_add_seconds(5, update_network_status, NULL);
    
    // Battery status
    battery_label = gtk_label_new("");
    gtk_box_pack_end(GTK_BOX(box), battery_label, FALSE, FALSE, 4);
    update_battery_status(NULL);
    g_timeout_add_seconds(30, update_battery_status, NULL);
}

// Funkcja do otwierania okna wyszukiwania
static void open_search_dialog(GtkWidget *widget, gpointer user_data) {
    // Zamknij menu Start
    if (gtk_widget_get_visible(start_menu)) {
        gtk_widget_hide(start_menu);
    }
    
    // Utwórz okno dialogowe
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Search Applications",
        NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Close", GTK_RESPONSE_CLOSE,
        NULL
    );
    
    // Ustaw rozmiar okna
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 300);
    
    // Pobierz obszar zawartości okna dialogowego
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 10);
    
    // Dodaj pole wyszukiwania
    GtkWidget *search_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(content_area), search_box);
    
    GtkWidget *search_label = gtk_label_new("Enter application name:");
    gtk_box_pack_start(GTK_BOX(search_box), search_label, FALSE, FALSE, 0);
    
    GtkWidget *search_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry), "Search...");
    gtk_box_pack_start(GTK_BOX(search_box), search_entry, FALSE, FALSE, 0);
    
    // Dodaj kontener na wyniki wyszukiwania
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled_window, -1, 200);
    gtk_box_pack_start(GTK_BOX(search_box), scrolled_window, TRUE, TRUE, 0);
    
    GtkWidget *results_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(scrolled_window), results_box);
    
    // Funkcja do wyszukiwania aplikacji
    GtkWidget *info_label = gtk_label_new("Type to search for applications");
    gtk_box_pack_start(GTK_BOX(results_box), info_label, FALSE, FALSE, 0);
    
    // Funkcja obsługująca wyszukiwanie
    g_signal_connect(search_entry, "changed", G_CALLBACK(search_applications_callback), results_box);
    
    // Pokaż wszystkie widgety
    gtk_widget_show_all(dialog);
    
    // Uruchom okno dialogowe
    gtk_dialog_run(GTK_DIALOG(dialog));
    
    // Zniszcz okno dialogowe po zamknięciu
    gtk_widget_destroy(dialog);
}

// Funkcja do wyszukiwania aplikacji (callback)
static void search_applications_callback(GtkWidget *entry, gpointer user_data) {
    const gchar *search_text = gtk_entry_get_text(GTK_ENTRY(entry));
    GtkWidget *results_box = GTK_WIDGET(user_data);
    
    // Usuń poprzednie wyniki wyszukiwania
    GList *children = gtk_container_get_children(GTK_CONTAINER(results_box));
    for (GList *l = children; l != NULL; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);
    
    // Jeśli pole wyszukiwania jest puste, pokaż informację
    if (strlen(search_text) == 0) {
        GtkWidget *info_label = gtk_label_new("Type to search for applications");
        gtk_box_pack_start(GTK_BOX(results_box), info_label, FALSE, FALSE, 0);
        gtk_widget_show(info_label);
        return;
    }
    
    // Wyszukaj aplikacje pasujące do zapytania
    gchar *search_lower = g_utf8_strdown(search_text, -1);
    int results_count = 0;
    
    for (GList *l = all_applications; l != NULL; l = l->next) {
        AppEntry *app = (AppEntry *)l->data;
        gchar *name_lower = g_utf8_strdown(app->name, -1);
        
        if (strstr(name_lower, search_lower) != NULL) {
            // Found a match
            GtkWidget *app_button = gtk_button_new_with_label(app->name);
            
            // Use g_strdup to pass the exec command correctly
            g_signal_connect(app_button, "clicked", G_CALLBACK(launch_app), g_strdup(app->exec));
            
            // Close the parent dialog when the button is clicked
            g_signal_connect_swapped(app_button, "clicked", G_CALLBACK(gtk_widget_destroy), gtk_widget_get_toplevel(results_box));
            
            gtk_box_pack_start(GTK_BOX(results_box), app_button, FALSE, FALSE, 2);
            gtk_widget_show(app_button);
            
            results_count++;
        }
        
        g_free(name_lower);
    }
    
    // Jeśli nie znaleziono wyników, pokaż komunikat
    if (results_count == 0) {
        GtkWidget *no_results = gtk_label_new("wtf u searching for?");
        gtk_box_pack_start(GTK_BOX(results_box), no_results, FALSE, FALSE, 2);
        gtk_widget_show(no_results);
    }
    
    g_free(search_lower);
}

// Funkcja wyświetlająca okno dialogowe "About"
static void show_about_dialog(GtkWidget *widget, gpointer user_data) {
    // Zamknij menu Start
    if (gtk_widget_get_visible(start_menu)) {
        gtk_widget_hide(start_menu);
    }
    
    GtkWidget *dialog = gtk_about_dialog_new();
    
    // Ustaw właściwości okna dialogowego
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dialog), "Lum Desktop");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), "0.1");
    gtk_about_dialog_set_copyright(GTK_ABOUT_DIALOG(dialog), "© 2025 Lum & Bakslerz Team");
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog), 
        "A lightweight panel for Linux desktop environments.");
    
    // Możesz dodać więcej informacji, takich jak autorzy, licencja, strona internetowa itp.
    const gchar *authors[] = {"Lum & Bakslerz Team", NULL};
    gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(dialog), authors);
    
    // Wyświetl okno dialogowe
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void cleanup_resources() {
    if (category_mapping) {
        g_hash_table_destroy(category_mapping);
        category_mapping = NULL;
    }
    
    if (network_menu) {
        gtk_widget_destroy(network_menu);
        network_menu = NULL;
    }
    
    if (audio_menu) {
        gtk_widget_destroy(audio_menu);
        audio_menu = NULL;
    }
    
    // Zwolnij pamięć listy aplikacji
    for (GList *l = all_applications; l != NULL; l = l->next) {
        AppEntry *app = (AppEntry *)l->data;
        g_free(app->name);
        g_free(app->exec);
        g_free(app);
    }
    g_list_free(all_applications);
}

// Callback dla zdarzenia realizacji okna
static gboolean on_window_realized(GtkWidget *window, gpointer user_data) {
    GdkWindow *gdk_window = gtk_widget_get_window(window);
    if (gdk_window) {
        GdkScreen *screen = gdk_screen_get_default();
        gint width = gdk_screen_get_width(screen);
        
        // Dodajemy dodatkowy margines bezpieczeństwa
        const gulong reserved_height = PANEL_HEIGHT + 2;
        
        // Ustaw struts, aby zarezerwować miejsce na górze ekranu
        // Kolejność w tablicy: lewo, prawo, góra, dół
        gulong struts[4] = {0};
        struts[2] = reserved_height;  // Indeks 2 to góra - rezerwuje miejsce na górze ekranu
        
        // Ustaw podstawowe struts
        gdk_property_change(
            gdk_window,
            gdk_atom_intern("_NET_WM_STRUT", FALSE),
            gdk_atom_intern("CARDINAL", FALSE),
            32,
            GDK_PROP_MODE_REPLACE,
            (guchar *)struts,
            4
        );
        
        // Ustaw rozszerzone struts
        // Kolejność: lewo, prwo, góra, dół, lewo_start, lewo_koniec, prawo_start, prawo_koniec, 
        //            góra_start, góra_koniec, dół_start, dół_koniec
        gulong struts_partial[12] = {0};
        struts_partial[2] = reserved_height;  // Góra
        struts_partial[8] = 0;                // góra_start - początek obszaru zarezerwowanego na górze
        struts_partial[9] = width - 1;        // góra_koniec - koniec obszaru zarezerwowanego na górze
        
        gdk_property_change(
            gdk_window,
            gdk_atom_intern("_NET_WM_STRUT_PARTIAL", FALSE),
            gdk_atom_intern("CARDINAL", FALSE),
            32,
            GDK_PROP_MODE_REPLACE,
            (guchar *)struts_partial,
            12
        );
        
        // Upewnij się, że okno jest na górze ekranu
        gdk_window_move(gdk_window, 0, 0);
        
        // Wydrukuj informacje debugowe
        g_print("Panel ustawiony na górze ekranu, rezerwuje %lu pikseli wysokości\n", reserved_height);
    }
    return FALSE;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_DOCK);
    gtk_window_stick(GTK_WINDOW(window));
    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);

    GdkScreen *screen = gdk_screen_get_default();
    gint width = gdk_screen_get_width(screen);
    gtk_window_set_default_size(GTK_WINDOW(window), width, PANEL_HEIGHT);
    gtk_window_move(GTK_WINDOW(window), 0, 0);

    build_panel(window);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(window, "realize", G_CALLBACK(on_window_realized), NULL);
    atexit(cleanup_resources);

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
} 

