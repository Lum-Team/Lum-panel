#include <gtk/gtk.h>
#include <time.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <gdk/gdkx.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <math.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

// Struktura interfejsu sieciowego
typedef struct {
    char *interface;
    char *name;
    gboolean is_wifi;
    gboolean is_connected;
} NetworkInterface;

// Globalne widgety
GtkWidget *window;
GtkWidget *clock_label;
GtkWidget *network_button;
GtkWidget *volume_button;
GtkWidget *power_button;
GtkWidget *battery_label;
GtkWidget *network_menu = NULL;
GtkWidget *main_box; // Główny kontener

// Obszary robocze
#define MAX_WORKSPACES 9
GtkWidget *workspace_buttons[MAX_WORKSPACES];
int current_workspace = 0;
int num_workspaces = 4; // Domyślna liczba obszarów roboczych

// Typy środowisk pulpitu
typedef enum {
    WM_UNKNOWN,
    WM_I3,
    WM_XFWM,
    WM_OPENBOX,
    WM_GNOME,
    WM_KDE,
    WM_OTHER
} WindowManagerType;

WindowManagerType detected_wm = WM_UNKNOWN;

// Struktura dla urządzenia dźwiękowego
typedef struct {
    char *name;
    char *description;
    gboolean is_default;
    gboolean is_input;
} AudioDevice;

// Ustawienia panelu
typedef struct {
    double opacity;           // Przezroczystość panelu (0.0-1.0)
    gboolean auto_hide;       // Czy panel ma się automatycznie ukrywać
    char bg_color[20];        // Kolor tła panelu
    char fg_color[20];        // Kolor ikon i tekstu
} PanelSettings;

PanelSettings panel_settings = {
    .opacity = 0.85,
    .auto_hide = FALSE,
    .bg_color = "#1e1e2e",
    .fg_color = "#cdd6f4"
};

// Deklaracje funkcji
static void update_volume_icon();
static void update_network_icon();
static gboolean is_battery_present();
static gboolean is_battery_charging();
static int get_battery_percentage();
static gboolean update_battery_status(gpointer data);
static gboolean reserve_screen_space(gpointer data);
static void switch_to_workspace(GtkWidget *widget, gpointer workspace_num);
static gboolean update_workspaces(gpointer data);
static WindowManagerType detect_window_manager();
static void apply_panel_settings();

static void load_panel_settings();




// Deklaracje funkcji audio
static void set_volume(gpointer volume_percent_ptr);
static void toggle_mute();
static void open_sound_settings();
static gboolean is_sound_muted();
static int get_current_volume();
static void set_default_audio_device(GtkWidget *widget, gpointer user_data);
static GList* get_audio_devices(gboolean input);
static GtkWidget* create_volume_menu();
static void on_volume_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static gboolean on_volume_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer user_data);



static gboolean update_volume_status(gpointer data);



// Deklaracje funkcji sieci
static GtkWidget* create_network_menu();
static void on_network_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static void toggle_network_interface(GtkWidget *widget, gpointer user_data);
static void connect_to_wifi(GtkWidget *widget, gpointer user_data);
static void refresh_wifi_networks(GtkWidget *widget, gpointer user_data);
static void open_network_settings(GtkWidget *widget, gpointer user_data);
static void style_menu(GtkWidget *menu);
static gboolean update_network_status(gpointer data);

// Deklaracje funkcji zasilania
static void on_power_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static GtkWidget* create_power_menu();
static void power_shutdown();
static void power_reboot();
static void power_suspend();
static void power_hibernate();
static void power_logout();

// Funkcja aktualizująca zegar
static gboolean update_clock(gpointer data) {
    time_t raw_time;
    struct tm *time_info;
    char time_string[64];

    time(&raw_time);
    time_info = localtime(&raw_time);
    strftime(time_string, sizeof(time_string), "%a %d %b  %H:%M", time_info);

    gtk_label_set_text(GTK_LABEL(clock_label), time_string);
    
    return G_SOURCE_CONTINUE;  // Kontynuuj odświeżanie
}

// Struktura informacji o połączeniu sieciowym
typedef struct {
    gboolean has_connection;
    gboolean is_wifi;
    char connected_ssid[256];
    char connected_interface[64];
} NetworkStatus;

// Funkcja sprawdzająca status sieci
static NetworkStatus check_network_status() {
    NetworkStatus status = {FALSE, FALSE, "", ""};
    FILE *fp;
    char line[512];
    
    // Sprawdź aktywne połączenia przez nmcli
    fp = popen("nmcli -t -f DEVICE,TYPE,STATE,CONNECTION device status", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            g_strchomp(line);
            char **parts = g_strsplit(line, ":", 4);
            if (parts && parts[0] && parts[1] && parts[2] && parts[3]) {
                // Sprawdź czy interfejs jest połączony
                if (strcmp(parts[2], "connected") == 0 && strcmp(parts[1], "loopback") != 0) {
                    status.has_connection = TRUE;
                    strncpy(status.connected_interface, parts[0], sizeof(status.connected_interface) - 1);
                    
                    if (strcmp(parts[1], "wifi") == 0) {
                        status.is_wifi = TRUE;
                        strncpy(status.connected_ssid, parts[3], sizeof(status.connected_ssid) - 1);
                        break; // WiFi ma priorytet w wyświetlaniu
                    }
                }
            }
            g_strfreev(parts);
        }
        pclose(fp);
    }
    
    // Jeśli nie znaleziono przez nmcli, sprawdź tradycyjnie
    if (!status.has_connection) {
        struct ifaddrs *ifaddr, *ifa;
        
        if (getifaddrs(&ifaddr) != -1) {
            for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
                if (ifa->ifa_addr == NULL)
                    continue;
                
                // Sprawdź tylko interfejsy IPv4
                if (ifa->ifa_addr->sa_family == AF_INET) {
                    struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(addr->sin_addr), ip, INET_ADDRSTRLEN);
                    
                    // Pomiń interfejs loopback (127.0.0.1)
                    if (strcmp(ip, "127.0.0.1") != 0) {
                        status.has_connection = TRUE;
                        strncpy(status.connected_interface, ifa->ifa_name, sizeof(status.connected_interface) - 1);
                        
                        // Sprawdź czy to WiFi po nazwie interfejsu
                        if (strstr(ifa->ifa_name, "wl") || strstr(ifa->ifa_name, "wifi")) {
                            status.is_wifi = TRUE;
                        }
                        break;
                    }
                }
            }
            freeifaddrs(ifaddr);
        }
    }
    
    return status;
}

// Funkcja do przełączania interfejsu sieciowego
static void toggle_network_interface(GtkWidget *widget, gpointer user_data) {
    NetworkInterface *iface = (NetworkInterface *)user_data;
    char command[256];
    
    if (iface->is_connected) {
        // Odłącz
        if (iface->is_wifi) {
            snprintf(command, sizeof(command), "nmcli connection down id \"%s\" &", iface->name);
        } else {
            snprintf(command, sizeof(command), "nmcli device disconnect %s &", iface->interface);
        }
    } else {
        // Połącz
        if (iface->is_wifi) {
            snprintf(command, sizeof(command), "nmcli connection up id \"%s\" &", iface->name);
        } else {
            snprintf(command, sizeof(command), "nmcli device connect %s &", iface->interface);
        }
    }
    
    system(command);
}

// Funkcja obsługi odpowiedzi w oknie dialogowym hasła WiFi
static void on_wifi_password_response(GtkDialog *dialog, gint response_id, gpointer user_data) {
    if (response_id == GTK_RESPONSE_OK) {
        const char *ssid = (const char *)user_data;
        GtkWidget *entry = GTK_WIDGET(g_object_get_data(G_OBJECT(dialog), "password_entry"));
        const char *password = gtk_entry_get_text(GTK_ENTRY(entry));
        
        char command[512];
        if (strlen(password) > 0) {
            snprintf(command, sizeof(command), "nmcli device wifi connect \"%s\" password \"%s\" &", ssid, password);
        } else {
            snprintf(command, sizeof(command), "nmcli device wifi connect \"%s\" &", ssid);
        }
        system(command);
        
        // Pokazanie komunikatu o próbie połączenia
        g_timeout_add_seconds(2, (GSourceFunc)gtk_widget_destroy, dialog);
        
        GtkWidget *info_dialog = gtk_message_dialog_new(NULL,
                                                       GTK_DIALOG_MODAL,
                                                       GTK_MESSAGE_INFO,
                                                       GTK_BUTTONS_OK,
                                                       "Próba połączenia z siecią %s...", ssid);
        gtk_dialog_run(GTK_DIALOG(info_dialog));
        gtk_widget_destroy(info_dialog);
    }
    
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

// Funkcja do łączenia z siecią WiFi z obsługą hasła
static void connect_to_wifi(GtkWidget *widget, gpointer user_data) {
    const char *ssid = (const char *)user_data;
    char command[256];
    
    // Najpierw sprawdź czy sieć jest już zapisana
    snprintf(command, sizeof(command), "nmcli connection show | grep \"%s\"", ssid);
    if (system(command) == 0) {
        // Sieć jest zapisana, połącz się bezpośrednio
        snprintf(command, sizeof(command), "nmcli connection up id \"%s\" &", ssid);
        system(command);
        return;
    }
    
    // Sieć nie jest zapisana, sprawdź czy ma zabezpieczenie
    FILE *fp = popen("nmcli -t -f SSID,SECURITY device wifi list", "r");
    gboolean has_security = FALSE;
    char line[512];
    
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            g_strchomp(line);
            char **parts = g_strsplit(line, ":", 2);
            if (parts && parts[0] && parts[1]) {
                if (strcmp(parts[0], ssid) == 0 && strlen(parts[1]) > 0 && strcmp(parts[1], "--") != 0) {
                    has_security = TRUE;
                    g_strfreev(parts);
                    break;
                }
            }
            g_strfreev(parts);
        }
        pclose(fp);
    }
    
    if (has_security) {
        // Sieć ma zabezpieczenie, pokaż okno dialogowe do wprowadzenia hasła
        GtkWidget *dialog = gtk_dialog_new_with_buttons("Połącz z siecią WiFi",
                                                        NULL,
                                                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                        "Anuluj", GTK_RESPONSE_CANCEL,
                                                        "Połącz", GTK_RESPONSE_OK,
                                                        NULL);
        
        // Stylizacja okna dialogowego
        GtkCssProvider *dialog_provider = gtk_css_provider_new();
        gtk_css_provider_load_from_data(dialog_provider,
            "dialog { background-color: #1e1e2e; color: #cdd6f4; }"
            "dialog .dialog-vbox { background-color: #1e1e2e; }"
            "dialog .dialog-action-area { background-color: #1e1e2e; }"
            "dialog button { background-color: #313244; color: #cdd6f4; border-radius: 4px; border: none; padding: 8px 16px; margin: 4px; }"
            "dialog button:hover { background-color: #45475a; }"
            "dialog label { color: #cdd6f4; margin: 10px; }"
            "dialog entry { background-color: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 4px; padding: 8px; margin: 5px; }",
            -1, NULL);
        
        GtkStyleContext *dialog_context = gtk_widget_get_style_context(dialog);
        gtk_style_context_add_provider(dialog_context, GTK_STYLE_PROVIDER(dialog_provider), 
                                      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(dialog_provider);
        
        GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        
        char label_text[512];
        snprintf(label_text, sizeof(label_text), "Wprowadź hasło dla sieci:\n<b>%s</b>", ssid);
        GtkWidget *label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(label), label_text);
        gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
        
        GtkWidget *entry = gtk_entry_new();
        gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE); // Ukryj wprowadzane znaki
        gtk_entry_set_input_purpose(GTK_ENTRY(entry), GTK_INPUT_PURPOSE_PASSWORD);
        gtk_widget_set_size_request(entry, 300, -1);
        
        gtk_container_add(GTK_CONTAINER(content_area), label);
        gtk_container_add(GTK_CONTAINER(content_area), entry);
        
        // Zapisz wskaźnik do pola tekstowego w dialogu
        g_object_set_data(G_OBJECT(dialog), "password_entry", entry);
        
        // Podłącz sygnał odpowiedzi
        g_signal_connect(dialog, "response", G_CALLBACK(on_wifi_password_response), g_strdup(ssid));
        
        gtk_widget_show_all(dialog);
        
        // Ustaw fokus na pole tekstowe
        gtk_widget_grab_focus(entry);
        
    } else {
        // Sieć otwarta, połącz się bezpośrednio
        snprintf(command, sizeof(command), "nmcli device wifi connect \"%s\" &", ssid);
        system(command);
    }
}

// Funkcja do odświeżania sieci WiFi
static void refresh_wifi_networks(GtkWidget *widget, gpointer user_data) {
    system("nmcli device wifi rescan &");
    // Poczekaj chwilę na zakończenie skanowania i odtwórz menu
    g_timeout_add_seconds(2, (GSourceFunc)gtk_widget_destroy, network_menu);
}

// Funkcja do otwierania ustawień sieci
static void open_network_settings(GtkWidget *widget, gpointer user_data) {
    // Próba uruchomienia różnych narzędzi do konfiguracji sieci
    if (system("which nm-connection-editor > /dev/null 2>&1") == 0) {
        system("nm-connection-editor &");
    } else if (system("which gnome-control-center > /dev/null 2>&1") == 0) {
        system("gnome-control-center network &");
    } else if (system("which systemsettings5 > /dev/null 2>&1") == 0) {
        system("systemsettings5 kcm_networkmanagement &");
    }
}

// Callback dla rysowania tła menu z przezroczystością
static gboolean on_menu_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    // Wyczyść tło do całkowitej przezroczystości
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    

    
    return FALSE; // Pozwól na dalsze rysowanie
}

// Funkcja do stylizacji menu - przezroczyste z prawdziwą przezroczystością
static void style_menu(GtkWidget *menu) {
    // Ustaw visual dla przezroczystości
    GdkScreen *screen = gtk_widget_get_screen(menu);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    
    if (visual != NULL && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(menu, visual);
        gtk_widget_set_app_paintable(menu, TRUE);
        
        // Podłącz callback do rysowania tła
        g_signal_connect(menu, "draw", G_CALLBACK(on_menu_draw), NULL);
    }
    
    GtkCssProvider *menu_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(menu_provider,
        "menu { "
        "    background: transparent; "
        "    border: none; "
        "    padding: 8px; "
        "}"
        "menuitem { "
        "    color: white; "
        "    background: rgba(255, 255, 255, 0.1); "
        "    padding: 10px 16px; "
        "    margin: 2px; "
        "    border-radius: 8px; "
        "    border: none; "
        "    transition: all 0.2s ease; "
        "    text-shadow: 0 0 8px rgba(0, 0, 0, 0.8), "
        "                 0 1px 2px rgba(0, 0, 0, 0.9); "
        "    box-shadow: 0 2px 8px rgba(0, 0, 0, 0.3); "
        "}"
        "menuitem:hover { "
        "    background: rgba(255, 255, 255, 0.25); "
        "    color: white; "
        "    border: none; "
        "    box-shadow: 0 4px 16px rgba(0, 0, 0, 0.4), "
        "                0 0 20px rgba(255, 255, 255, 0.2); "
        "    transform: translateX(2px); "
        "}"
        "menuitem:disabled { "
        "    color: rgba(255, 255, 255, 0.7); "
        "    background: rgba(255, 255, 255, 0.05); "
        "    font-weight: 700; "
        "    text-shadow: 0 0 10px rgba(0, 0, 0, 0.9); "
        "}"
        "separator { "
        "    background: rgba(255, 255, 255, 0.4); "
        "    margin: 6px 8px; "
        "    min-height: 1px; "
        "    box-shadow: 0 1px 3px rgba(0, 0, 0, 0.3); "
        "}",
        -1, NULL);
    
    GtkStyleContext *context = gtk_widget_get_style_context(menu);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(menu_provider), 
                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(menu_provider);
}

// Funkcja tworząca menu sieci
static GtkWidget* create_network_menu() {
    GtkWidget *menu = gtk_menu_new();
    style_menu(menu);  // Zastosuj styl do menu
    
    FILE *fp;
    char line[512];
    GList *interfaces = NULL;
    gboolean has_wifi = FALSE;
    NetworkStatus current_status = check_network_status();
    
    // Pokaż aktualne połączenie
    if (current_status.has_connection) {
        GtkWidget *current_header = gtk_menu_item_new_with_label("Aktualnie połączono");
        gtk_widget_set_sensitive(current_header, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), current_header);
        gtk_widget_show(current_header);
        
        char current_info[512];
        if (current_status.is_wifi && strlen(current_status.connected_ssid) > 0) {
            snprintf(current_info, sizeof(current_info), "📶 WiFi: %s (%s)", 
                     current_status.connected_ssid, current_status.connected_interface);
        } else if (current_status.is_wifi) {
            snprintf(current_info, sizeof(current_info), "📶 WiFi: %s", current_status.connected_interface);
        } else {
            snprintf(current_info, sizeof(current_info), "🔌 Kabel: %s", current_status.connected_interface);
        }
        
        GtkWidget *current_item = gtk_menu_item_new_with_label(current_info);
        gtk_widget_set_sensitive(current_item, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), current_item);
        gtk_widget_show(current_item);
        
        // Separator
        GtkWidget *separator = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
        gtk_widget_show(separator);
    }
    
    // Pobierz interfejsy sieciowe za pomocą nmcli
    fp = popen("nmcli -t -f DEVICE,TYPE,STATE,CONNECTION device", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            g_strchomp(line);
            char **parts = g_strsplit(line, ":", 4);
            if (parts && parts[0] && parts[1] && parts[2] && parts[3]) {
                // Pomiń loopback
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
    
    // Dodaj interfejsy do menu
    if (interfaces) {
        GtkWidget *header = gtk_menu_item_new_with_label("Interfejsy sieciowe");
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
                         iface->is_wifi ? "WiFi" : "Przewodowa",
                         iface->is_connected ? "Połączono" : "Rozłączono");
            } else {
                snprintf(label, sizeof(label), "%s (%s): %s", 
                         iface->interface, 
                         iface->is_wifi ? "WiFi" : "Przewodowa",
                         iface->is_connected ? "Połączono" : "Rozłączono");
            }
            
            GtkWidget *item = gtk_menu_item_new_with_label(label);
            g_signal_connect(item, "activate", G_CALLBACK(toggle_network_interface), iface);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
            gtk_widget_show(item);
        }
        
        // Dodaj separator
        GtkWidget *separator = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
        gtk_widget_show(separator);
    }
    
    // Jeśli mamy WiFi, dodaj dostępne sieci
    if (has_wifi) {
        // Pobierz dostępne sieci WiFi
        fp = popen("nmcli -t -f SSID,SIGNAL,SECURITY device wifi list", "r");
        if (fp) {
            GtkWidget *wifi_header = gtk_menu_item_new_with_label("Dostępne sieci WiFi");
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
                    const char *security = parts[2] && strlen(parts[2]) > 0 ? "🔒" : "";
                    
                    snprintf(label, sizeof(label), "%s%s (Sygnał: %d%%)", 
                             security, parts[0], signal);
                    
                    GtkWidget *item = gtk_menu_item_new_with_label(label);
                    g_signal_connect(item, "activate", G_CALLBACK(connect_to_wifi), g_strdup(parts[0]));
                    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
                    gtk_widget_show(item);
                    count++;
                }
                g_strfreev(parts);
            }
            pclose(fp);
            
            // Dodaj opcję odświeżania
            GtkWidget *refresh = gtk_menu_item_new_with_label("Odśwież sieci WiFi");
            g_signal_connect(refresh, "activate", G_CALLBACK(refresh_wifi_networks), NULL);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), refresh);
            gtk_widget_show(refresh);
        }
    }
    
    // Dodaj separator
    GtkWidget *separator2 = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator2);
    gtk_widget_show(separator2);
    
    // Dodaj opcję ustawień sieci
    GtkWidget *settings = gtk_menu_item_new_with_label("Ustawienia sieci...");
    g_signal_connect(settings, "activate", G_CALLBACK(open_network_settings), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), settings);
    gtk_widget_show(settings);
    
    return menu;
}

// Funkcja aktualizująca ikonę sieci
static void update_network_icon() {
    NetworkStatus status = check_network_status();
    GtkWidget *image;
    char tooltip[512];
    
    if (status.has_connection) {
        if (status.is_wifi) {
            // Połączenie WiFi
            image = gtk_image_new_from_icon_name("network-wireless-symbolic", GTK_ICON_SIZE_MENU);
            gtk_button_set_image(GTK_BUTTON(network_button), image);
            
            if (strlen(status.connected_ssid) > 0) {
                snprintf(tooltip, sizeof(tooltip), "WiFi połączone: %s\nKliknij, aby zobaczyć opcje", status.connected_ssid);
            } else {
                snprintf(tooltip, sizeof(tooltip), "WiFi połączone (%s)\nKliknij, aby zobaczyć opcje", status.connected_interface);
            }
        } else {
            // Połączenie kablowe
            image = gtk_image_new_from_icon_name("network-wired-symbolic", GTK_ICON_SIZE_MENU);
            gtk_button_set_image(GTK_BUTTON(network_button), image);
            snprintf(tooltip, sizeof(tooltip), "Sieć kablowa połączona (%s)\nKliknij, aby zobaczyć opcje", status.connected_interface);
        }
        gtk_widget_set_tooltip_text(network_button, tooltip);
    } else {
        // Brak połączenia
        image = gtk_image_new_from_icon_name("network-offline-symbolic", GTK_ICON_SIZE_MENU);
        gtk_button_set_image(GTK_BUTTON(network_button), image);
        gtk_widget_set_tooltip_text(network_button, "Sieć rozłączona - Kliknij, aby zobaczyć opcje");
    }
    
    // Upewnij się, że ikona jest biała
    image = gtk_button_get_image(GTK_BUTTON(network_button));
    if (image) {
        GtkStyleContext *img_context = gtk_widget_get_style_context(image);
        gtk_style_context_add_class(img_context, "white-icon");
    }
}

// Funkcja aktualizująca status sieci co 5 sekund
static gboolean update_network_status(gpointer data) {
    update_network_icon();
    return TRUE; // Kontynuuj timer
}

// Funkcja obsługi kliknięcia ikony sieci
static void on_network_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        // Twórz nowe menu za każdym razem, aby było aktualne
        if (network_menu) {
            gtk_widget_destroy(network_menu);
        }
        
        network_menu = create_network_menu();
        // Użyj gtk_menu_popup_at_pointer, aby menu nie znikało
        gtk_menu_popup_at_pointer(GTK_MENU(network_menu), (GdkEvent*)event);
    }
}

// Funkcja do zmiany głośności
static void set_volume(gpointer volume_percent_ptr) {
    int volume_percent = GPOINTER_TO_INT(volume_percent_ptr);
    char command[256];
    snprintf(command, sizeof(command), "pactl set-sink-volume @DEFAULT_SINK@ %d%% &", volume_percent);
    system(command);
    
    // Aktualizuj ikonę głośności po zmianie
    g_timeout_add(200, (GSourceFunc)update_volume_icon, NULL);
}

// Funkcja do wyciszania/odciszania dźwięku
static void toggle_mute() {
    system("pactl set-sink-mute @DEFAULT_SINK@ toggle &");
    
    // Aktualizuj ikonę głośności po zmianie
    g_timeout_add(200, (GSourceFunc)update_volume_icon, NULL);
}

// Funkcja do otwierania ustawień dźwięku
static void open_sound_settings() {
    if (system("which pavucontrol > /dev/null 2>&1") == 0) {
        system("pavucontrol &");
    } else if (system("which gnome-control-center > /dev/null 2>&1") == 0) {
        system("gnome-control-center sound &");
    }
}

// Funkcja do sprawdzania, czy dźwięk jest wyciszony
static gboolean is_sound_muted() {
    FILE *fp;
    char line[256];
    gboolean muted = FALSE;
    
    fp = popen("pactl get-sink-mute @DEFAULT_SINK@", "r");
    if (fp) {
        if (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "yes")) {
                muted = TRUE;
            }
        }
        pclose(fp);
    }
    
    return muted;
}

// Funkcja do pobierania aktualnej głośności
static int get_current_volume() {
    FILE *fp;
    char line[256];
    int volume = 0;
    
    fp = popen("pactl get-sink-volume @DEFAULT_SINK@ | grep -o '[0-9]*%' | head -n 1 | tr -d '%'", "r");
    if (fp) {
        if (fgets(line, sizeof(line), fp)) {
            volume = atoi(line);
        }
        pclose(fp);
    }
    
    return volume;
}

// Funkcja obsługi scrolla myszy na ikonie głośności






// Funkcja do zmiany domyślnego urządzenia dźwiękowego
static void set_default_audio_device(GtkWidget *widget, gpointer user_data) {
    AudioDevice *device = (AudioDevice *)user_data;
    char command[512];
    
    g_print("Zmieniam domyślne urządzenie audio na: %s (%s)\n", device->description, device->name);
    
    if (device->is_input) {
        snprintf(command, sizeof(command), "pactl set-default-source \"%s\"", device->name);
    } else {
        snprintf(command, sizeof(command), "pactl set-default-sink \"%s\"", device->name);
    }
    
    int result = system(command);
    if (result == 0) {
        g_print("Pomyślnie zmieniono urządzenie audio\n");
        
        // Jeśli to urządzenie wyjściowe, ustaw też głośność
        if (!device->is_input) {
            char volume_cmd[256];
            int current_vol = get_current_volume();
            snprintf(volume_cmd, sizeof(volume_cmd), "pactl set-sink-volume \"%s\" %d%%", device->name, current_vol);
            system(volume_cmd);
        }
    } else {
        g_print("Błąd podczas zmiany urządzenia audio\n");
    }
    
    // Aktualizuj ikonę głośności po zmianie
    g_timeout_add(500, (GSourceFunc)update_volume_icon, NULL);
}

// Funkcja do pobierania listy urządzeń dźwiękowych
static GList* get_audio_devices(gboolean input) {
    GList *devices = NULL;
    FILE *fp;
    char line[512];
    char default_device[256] = {0};
    
    // Najpierw pobierz domyślne urządzenie
    if (input) {
        fp = popen("pactl info | grep 'Default Source:' | awk '{print $3}'", "r");
    } else {
        fp = popen("pactl info | grep 'Default Sink:' | awk '{print $3}'", "r");
    }
    
    if (fp) {
        if (fgets(default_device, sizeof(default_device), fp)) {
            g_strchomp(default_device);
        }
        pclose(fp);
    }
    
    // Teraz pobierz listę wszystkich urządzeń
    if (input) {
        fp = popen("pactl list short sources", "r");
    } else {
        fp = popen("pactl list short sinks", "r");
    }
    
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            g_strchomp(line);
            
            // Format linii: ID	NAME	DRIVER	SAMPLE_SPEC	STATE
            char **parts = g_strsplit(line, "\t", 5);
            if (parts && parts[1]) {
                // Pomiń urządzenia monitor dla wejść
                if (input && strstr(parts[1], ".monitor")) {
                    g_strfreev(parts);
                    continue;
                }
                
                AudioDevice *device = g_new0(AudioDevice, 1);
                device->name = g_strdup(parts[1]);
                device->is_input = input;
                device->is_default = (strcmp(parts[1], default_device) == 0);
                
                // Pobierz opisową nazwę urządzenia
                char cmd[512];
                if (input) {
                    snprintf(cmd, sizeof(cmd), "pactl list sources | grep -A 10 'Name: %s' | grep 'Description:' | sed 's/.*Description: //'", parts[1]);
                } else {
                    snprintf(cmd, sizeof(cmd), "pactl list sinks | grep -A 10 'Name: %s' | grep 'Description:' | sed 's/.*Description: //'", parts[1]);
                }
                
                FILE *desc_fp = popen(cmd, "r");
                if (desc_fp) {
                    char desc_line[256];
                    if (fgets(desc_line, sizeof(desc_line), desc_fp)) {
                        g_strchomp(desc_line);
                        device->description = g_strdup(desc_line);
                    } else {
                        device->description = g_strdup(parts[1]); // Fallback na nazwę
                    }
                    pclose(desc_fp);
                } else {
                    device->description = g_strdup(parts[1]); // Fallback na nazwę
                }
                
                devices = g_list_append(devices, device);
            }
            g_strfreev(parts);
        }
        pclose(fp);
    }
    
    return devices;
}

// Funkcja tworząca menu dźwięku
static GtkWidget* create_volume_menu() {
    GtkWidget *menu = gtk_menu_new();
    style_menu(menu);
    GtkWidget *item;
    
    // Pobierz aktualną głośność i stan wyciszenia
    int current_volume = get_current_volume();
    gboolean is_muted = is_sound_muted();
    
    // Nagłówek z aktualną głośnością
    char volume_label[64];
    if (is_muted) {
        snprintf(volume_label, sizeof(volume_label), "Głośność: %d%% (Wyciszone)", current_volume);
    } else {
        snprintf(volume_label, sizeof(volume_label), "Głośność: %d%%", current_volume);
    }
    item = gtk_menu_item_new_with_label(volume_label);
    gtk_widget_set_sensitive(item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show(item);
    
    // Wskazówka o scroll'u
    item = gtk_menu_item_new_with_label("💡 Użyj scroll'a na ikonie do zmiany głośności");
    gtk_widget_set_sensitive(item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show(item);
    
    // Opcje głośności
    item = gtk_menu_item_new_with_label("🔇 Wycisz/Włącz");
    g_signal_connect(item, "activate", G_CALLBACK(toggle_mute), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show(item);
    
    // Separator
    GtkWidget *separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
    gtk_widget_show(separator);
    
    // Submenu dla urządzeń wyjściowych
    GList *output_devices = get_audio_devices(FALSE);
    if (output_devices) {
        item = gtk_menu_item_new_with_label("Urządzenia wyjściowe >");
        GtkWidget *output_submenu = gtk_menu_new();
        style_menu(output_submenu);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), output_submenu);
        
        GList *iter;
        for (iter = output_devices; iter; iter = iter->next) {
            AudioDevice *device = (AudioDevice *)iter->data;
            char label[256];
            
            if (device->is_default) {
                snprintf(label, sizeof(label), "• %s", device->description);
            } else {
                snprintf(label, sizeof(label), "  %s", device->description);
            }
            
            GtkWidget *subitem = gtk_menu_item_new_with_label(label);
            g_signal_connect(subitem, "activate", G_CALLBACK(set_default_audio_device), device);
            gtk_menu_shell_append(GTK_MENU_SHELL(output_submenu), subitem);
            gtk_widget_show(subitem);
        }
        
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        gtk_widget_show(item);
    }
    
    // Submenu dla urządzeń wejściowych
    GList *input_devices = get_audio_devices(TRUE);
    if (input_devices) {
        item = gtk_menu_item_new_with_label("Urządzenia wejściowe >");
        GtkWidget *input_submenu = gtk_menu_new();
        style_menu(input_submenu);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), input_submenu);
        
        GList *iter;
        for (iter = input_devices; iter; iter = iter->next) {
            AudioDevice *device = (AudioDevice *)iter->data;
            char label[256];
            
            if (device->is_default) {
                snprintf(label, sizeof(label), "• %s", device->description);
            } else {
                snprintf(label, sizeof(label), "  %s", device->description);
            }
            
            GtkWidget *subitem = gtk_menu_item_new_with_label(label);
            g_signal_connect(subitem, "activate", G_CALLBACK(set_default_audio_device), device);
            gtk_menu_shell_append(GTK_MENU_SHELL(input_submenu), subitem);
            gtk_widget_show(subitem);
        }
        
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        gtk_widget_show(item);
    }
    
    // Separator
    separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
    gtk_widget_show(separator);
    
    // Opcja ustawień dźwięku
    item = gtk_menu_item_new_with_label("Ustawienia dźwięku...");
    g_signal_connect(item, "activate", G_CALLBACK(open_sound_settings), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show(item);
    
    return menu;
}

// Funkcja aktualizująca ikonę dźwięku
static void update_volume_icon() {
    int volume = get_current_volume();
    gboolean is_muted = is_sound_muted();
    
    char tooltip[64];
    GtkWidget *image;
    
    if (is_muted) {
        image = gtk_image_new_from_icon_name("audio-volume-muted-symbolic", GTK_ICON_SIZE_MENU);
        gtk_button_set_image(GTK_BUTTON(volume_button), image);
        gtk_widget_set_tooltip_text(volume_button, "Dźwięk wyciszony");
    } else if (volume == 0) {
        image = gtk_image_new_from_icon_name("audio-volume-muted-symbolic", GTK_ICON_SIZE_MENU);
        gtk_button_set_image(GTK_BUTTON(volume_button), image);
        gtk_widget_set_tooltip_text(volume_button, "Głośność: 0%");
    } else if (volume < 33) {
        image = gtk_image_new_from_icon_name("audio-volume-low-symbolic", GTK_ICON_SIZE_MENU);
        gtk_button_set_image(GTK_BUTTON(volume_button), image);
        snprintf(tooltip, sizeof(tooltip), "Głośność: %d%%", volume);
        gtk_widget_set_tooltip_text(volume_button, tooltip);
    } else if (volume < 66) {
        image = gtk_image_new_from_icon_name("audio-volume-medium-symbolic", GTK_ICON_SIZE_MENU);
        gtk_button_set_image(GTK_BUTTON(volume_button), image);
        snprintf(tooltip, sizeof(tooltip), "Głośność: %d%%", volume);
        gtk_widget_set_tooltip_text(volume_button, tooltip);
    } else {
        image = gtk_image_new_from_icon_name("audio-volume-high-symbolic", GTK_ICON_SIZE_MENU);
        gtk_button_set_image(GTK_BUTTON(volume_button), image);
        snprintf(tooltip, sizeof(tooltip), "Głośność: %d%%", volume);
        gtk_widget_set_tooltip_text(volume_button, tooltip);
    }
    
    // Upewnij się, że ikona jest biała
    image = gtk_button_get_image(GTK_BUTTON(volume_button));
    if (image) {
        GtkStyleContext *img_context = gtk_widget_get_style_context(image);
        gtk_style_context_add_class(img_context, "white-icon");
    }
}

// Funkcja aktualizująca status dźwięku co 2 sekundy
static gboolean update_volume_status(gpointer data) {
    update_volume_icon();
    return TRUE; // Kontynuuj timer
}

// Funkcja obsługi kliknięcia ikony dźwięku
static void on_volume_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    if (event->type == GDK_BUTTON_PRESS) {
        if (event->button == 1) {
            // Lewy przycisk myszy - pokaż menu dźwięku
            GtkWidget *menu = create_volume_menu();
            gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)event);
        } else if (event->button == 3) {
            // Prawy przycisk myszy - otwórz pavucontrol
            if (system("which pavucontrol > /dev/null 2>&1") == 0) {
                system("pavucontrol &");
            } else if (system("which gnome-sound-settings > /dev/null 2>&1") == 0) {
                system("gnome-sound-settings &");
            } else if (system("which alsamixer > /dev/null 2>&1") == 0) {
                system("x-terminal-emulator -e alsamixer &");
            }
        }
    }
}

// Funkcja obsługi scroll wheel na volume (ikona lub suwak)
static gboolean on_volume_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer user_data) {
    int current_volume = get_current_volume();
    int new_volume = current_volume;
    
    if (event->direction == GDK_SCROLL_UP) {
        // Scroll w górę - zwiększ głośność o 5%
        new_volume = MIN(100, current_volume + 5);
    } else if (event->direction == GDK_SCROLL_DOWN) {
        // Scroll w dół - zmniejsz głośność o 5%
        new_volume = MAX(0, current_volume - 5);
    }
    
    if (new_volume != current_volume) {
        // Ustaw nową głośność
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "amixer -D pulse sset Master %d%%", new_volume);
        system(cmd);
        
        // Alternatywnie dla pipewire/pulseaudio
        snprintf(cmd, sizeof(cmd), "pactl set-sink-volume @DEFAULT_SINK@ %d%%", new_volume);
        system(cmd);
        
        // Jeśli to suwak w menu, aktualizuj jego wartość i label
        if (GTK_IS_SCALE(widget)) {
            gtk_range_set_value(GTK_RANGE(widget), (double)new_volume);
            
            // Aktualizuj label jeśli został przekazany jako user_data
            if (user_data && GTK_IS_LABEL(user_data)) {
                char volume_text[16];
                snprintf(volume_text, sizeof(volume_text), "%d%%", new_volume);
                gtk_label_set_text(GTK_LABEL(user_data), volume_text);
            }
        }
        
        // Aktualizuj ikonę
        update_volume_icon();
    }
    
    return TRUE; // Event został obsłużony
}







// Funkcje obsługi zasilania
static void power_shutdown() {
    system("systemctl poweroff");
}

static void power_reboot() {
    system("systemctl reboot");
}

static void power_suspend() {
    system("systemctl suspend");
}

static void power_hibernate() {
    system("systemctl hibernate");
}

static void power_logout() {
    // Próba wylogowania z różnych środowisk
    if (system("which gnome-session-quit > /dev/null 2>&1") == 0) {
        system("gnome-session-quit --logout --no-prompt &");
    } else if (system("which xfce4-session-logout > /dev/null 2>&1") == 0) {
        system("xfce4-session-logout --logout &");
    } else {
        system("pkill -SIGTERM -u $USER &");
    }
}

// Funkcja tworząca menu zasilania
static GtkWidget* create_power_menu() {
    GtkWidget *menu = gtk_menu_new();
    style_menu(menu);  // Zastosuj styl do menu
    GtkWidget *item;
    
    // Nagłówek
    item = gtk_menu_item_new_with_label("Opcje zasilania");
    gtk_widget_set_sensitive(item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show(item);
    
    // Informacje o baterii
    if (is_battery_present()) {
        int percentage = get_battery_percentage();
        gboolean charging = is_battery_charging();
        
        char battery_info[64];
        if (charging) {
            snprintf(battery_info, sizeof(battery_info), "Bateria: %d%% (ładowanie)", percentage);
        } else {
            snprintf(battery_info, sizeof(battery_info), "Bateria: %d%%", percentage);
        }
        
        item = gtk_menu_item_new_with_label(battery_info);
        gtk_widget_set_sensitive(item, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        gtk_widget_show(item);
    }
    
    // Separator
    GtkWidget *separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
    gtk_widget_show(separator);
    
    // Opcje zasilania
    item = gtk_menu_item_new_with_label("Wyloguj");
    g_signal_connect(item, "activate", G_CALLBACK(power_logout), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show(item);
    
    item = gtk_menu_item_new_with_label("Uśpij");
    g_signal_connect(item, "activate", G_CALLBACK(power_suspend), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show(item);
    
    item = gtk_menu_item_new_with_label("Hibernacja");
    g_signal_connect(item, "activate", G_CALLBACK(power_hibernate), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show(item);
    
    item = gtk_menu_item_new_with_label("Uruchom ponownie");
    g_signal_connect(item, "activate", G_CALLBACK(power_reboot), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show(item);
    
    item = gtk_menu_item_new_with_label("Wyłącz");
    g_signal_connect(item, "activate", G_CALLBACK(power_shutdown), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show(item);
    
    return menu;
}

// Funkcja sprawdzająca, czy bateria jest podłączona
static gboolean is_battery_present() {
    return g_file_test("/sys/class/power_supply/BAT0", G_FILE_TEST_EXISTS) ||
           g_file_test("/sys/class/power_supply/BAT1", G_FILE_TEST_EXISTS);
}

// Funkcja sprawdzająca, czy bateria jest ładowana
static gboolean is_battery_charging() {
    FILE *fp;
    char line[256];
    gboolean charging = FALSE;
    
    // Sprawdź BAT0
    if (g_file_test("/sys/class/power_supply/BAT0/status", G_FILE_TEST_EXISTS)) {
        fp = fopen("/sys/class/power_supply/BAT0/status", "r");
        if (fp) {
            if (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "Charging") || strstr(line, "Full")) {
                    charging = TRUE;
                }
            }
            fclose(fp);
        }
    }
    
    // Jeśli BAT0 nie jest ładowana, sprawdź BAT1
    if (!charging && g_file_test("/sys/class/power_supply/BAT1/status", G_FILE_TEST_EXISTS)) {
        fp = fopen("/sys/class/power_supply/BAT1/status", "r");
        if (fp) {
            if (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "Charging") || strstr(line, "Full")) {
                    charging = TRUE;
                }
            }
            fclose(fp);
        }
    }
    
    return charging;
}

// Funkcja pobierająca poziom naładowania baterii
static int get_battery_percentage() {
    FILE *fp;
    char line[256];
    int capacity = 0;
    
    // Sprawdź BAT0
    if (g_file_test("/sys/class/power_supply/BAT0/capacity", G_FILE_TEST_EXISTS)) {
        fp = fopen("/sys/class/power_supply/BAT0/capacity", "r");
        if (fp) {
            if (fgets(line, sizeof(line), fp)) {
                capacity = atoi(line);
            }
            fclose(fp);
        }
    }
    
    // Jeśli BAT0 nie ma, sprawdź BAT1
    if (capacity == 0 && g_file_test("/sys/class/power_supply/BAT1/capacity", G_FILE_TEST_EXISTS)) {
        fp = fopen("/sys/class/power_supply/BAT1/capacity", "r");
        if (fp) {
            if (fgets(line, sizeof(line), fp)) {
                capacity = atoi(line);
            }
            fclose(fp);
        }
    }
    
    return capacity;
}

// Funkcja aktualizująca status baterii
static gboolean update_battery_status(gpointer data) {
    if (!is_battery_present()) {
        // Brak baterii - ukryj etykietę
        gtk_widget_hide(battery_label);
        
        // Ustaw ikonę zasilania sieciowego
        GtkWidget *image = gtk_image_new_from_icon_name("system-shutdown-symbolic", GTK_ICON_SIZE_MENU);
        gtk_button_set_image(GTK_BUTTON(power_button), image);
        
        // Upewnij się, że ikona jest biała
        image = gtk_button_get_image(GTK_BUTTON(power_button));
        if (image) {
            GtkStyleContext *img_context = gtk_widget_get_style_context(image);
            gtk_style_context_add_class(img_context, "white-icon");
        }
        
        return G_SOURCE_CONTINUE;
    }
    
    // Pokaż etykietę baterii
    gtk_widget_show(battery_label);
    
    // Pobierz informacje o baterii
    int percentage = get_battery_percentage();
    gboolean charging = is_battery_charging();
    
    // Aktualizuj etykietę
    char battery_text[32];
    snprintf(battery_text, sizeof(battery_text), "%d%%", percentage);
    gtk_label_set_text(GTK_LABEL(battery_label), battery_text);
    
    // Aktualizuj ikonę baterii
    const char *icon_name;
    
    if (charging) {
        if (percentage <= 20) {
            icon_name = "battery-caution-charging-symbolic";
        } else if (percentage <= 40) {
            icon_name = "battery-low-charging-symbolic";
        } else if (percentage <= 60) {
            icon_name = "battery-medium-charging-symbolic";
        } else if (percentage <= 80) {
            icon_name = "battery-good-charging-symbolic";
        } else {
            icon_name = "battery-full-charging-symbolic";
        }
    } else {
        if (percentage <= 20) {
            icon_name = "battery-caution-symbolic";
        } else if (percentage <= 40) {
            icon_name = "battery-low-symbolic";
        } else if (percentage <= 60) {
            icon_name = "battery-medium-symbolic";
        } else if (percentage <= 80) {
            icon_name = "battery-good-symbolic";
        } else {
            icon_name = "battery-full-symbolic";
        }
    }
    
    GtkWidget *image = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_MENU);
    gtk_button_set_image(GTK_BUTTON(power_button), image);
    
    // Upewnij się, że ikona jest biała
    image = gtk_button_get_image(GTK_BUTTON(power_button));
    if (image) {
        GtkStyleContext *img_context = gtk_widget_get_style_context(image);
        gtk_style_context_add_class(img_context, "white-icon");
    }
    
    // Ustaw podpowiedź
    char tooltip[64];
    if (charging) {
        snprintf(tooltip, sizeof(tooltip), "Bateria: %d%% (ładowanie)", percentage);
    } else {
        snprintf(tooltip, sizeof(tooltip), "Bateria: %d%%", percentage);
    }
    gtk_widget_set_tooltip_text(power_button, tooltip);
    
    return G_SOURCE_CONTINUE;
}

// Funkcja obsługi kliknięcia ikony zasilania
static void on_power_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        GtkWidget *menu = create_power_menu();
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)event);
    }
}

// Funkcja rezerwująca przestrzeń na ekranie dla panelu
static gboolean reserve_screen_space(gpointer data) {
    GtkWidget *window = GTK_WIDGET(data);
    GdkWindow *gdk_window = gtk_widget_get_window(window);
    
    if (!gdk_window) {
        return G_SOURCE_CONTINUE; // Spróbuj ponownie później
    }
    
    // Pobierz rozmiar monitora
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor = gdk_display_get_primary_monitor(display);
    GdkRectangle geometry;
    gdk_monitor_get_geometry(monitor, &geometry);
    
    // Pobierz wysokość panelu
    gint panel_height;
    gtk_window_get_size(GTK_WINDOW(window), NULL, &panel_height);
    
    // Ustaw właściwość _NET_WM_STRUT, aby zarezerwować miejsce na górze ekranu
    GdkAtom atom = gdk_atom_intern("_NET_WM_STRUT", FALSE);
    gulong struts[12] = {0};
    
    // Ustaw tylko górną krawędź (indeks 2)
    struts[2] = panel_height;  // Górna krawędź
    
    gdk_property_change(gdk_window, atom, gdk_atom_intern("CARDINAL", FALSE),
                       32, GDK_PROP_MODE_REPLACE, (guchar*)struts, 4);
                       
    // Ustaw również _NET_WM_STRUT_PARTIAL dla lepszej kompatybilności
    atom = gdk_atom_intern("_NET_WM_STRUT_PARTIAL", FALSE);
    
    // Wyczyść wszystkie wartości
    memset(struts, 0, sizeof(struts));
    
    // Ustaw tylko górną krawędź i jej zakres X
    struts[2] = panel_height;       // Górna krawędź
    struts[8] = 0;                  // początek_x dla górnej krawędzi
    struts[9] = geometry.width - 1; // koniec_x dla górnej krawędzi
    
    gdk_property_change(gdk_window, atom, gdk_atom_intern("CARDINAL", FALSE),
                       32, GDK_PROP_MODE_REPLACE, (guchar*)struts, 12);
    
    return G_SOURCE_REMOVE; // Wykonaj tylko raz
}

// Funkcja do wykrywania typu menedżera okien
static WindowManagerType detect_window_manager() {
    FILE *fp;
    char line[1024];
    
    // Sprawdź zmienną środowiskową XDG_CURRENT_DESKTOP
    const char *desktop = getenv("XDG_CURRENT_DESKTOP");
    if (desktop) {
        if (strstr(desktop, "GNOME"))
            return WM_GNOME;
        if (strstr(desktop, "KDE"))
            return WM_KDE;
    }
    
    // Sprawdź zmienną środowiskową DESKTOP_SESSION
    const char *session = getenv("DESKTOP_SESSION");
    if (session) {
        if (strstr(session, "gnome"))
            return WM_GNOME;
        if (strstr(session, "kde"))
            return WM_KDE;
    }
    
    // Sprawdź, czy działa GNOME Shell
    fp = popen("ps -e | grep -q \"gnome-shell\" && echo 'gnome'", "r");
    if (fp) {
        if (fgets(line, sizeof(line), fp) && strstr(line, "gnome")) {
            pclose(fp);
            return WM_GNOME;
        }
        pclose(fp);
    }
    
    // Sprawdź, czy działa i3
    fp = popen("command -v i3-msg >/dev/null && i3-msg -t get_version 2>/dev/null", "r");
    if (fp) {
        if (fgets(line, sizeof(line), fp) && strstr(line, "i3")) {
            pclose(fp);
            return WM_I3;
        }
        pclose(fp);
    }
    
    // Sprawdź, czy działa xfwm4
    fp = popen("ps -e | grep -q xfwm4 && echo 'xfwm4'", "r");
    if (fp) {
        if (fgets(line, sizeof(line), fp) && strstr(line, "xfwm4")) {
            pclose(fp);
            return WM_XFWM;
        }
        pclose(fp);
    }
    
    // Sprawdź, czy działa openbox
    fp = popen("ps -e | grep -q openbox && echo 'openbox'", "r");
    if (fp) {
        if (fgets(line, sizeof(line), fp) && strstr(line, "openbox")) {
            pclose(fp);
            return WM_OPENBOX;
        }
        pclose(fp);
    }
    
    // Sprawdź, czy wmctrl jest dostępny
    fp = popen("command -v wmctrl >/dev/null && echo 'wmctrl'", "r");
    if (fp) {
        if (fgets(line, sizeof(line), fp) && strstr(line, "wmctrl")) {
            pclose(fp);
            return WM_OTHER; // Inny menedżer okien, ale wmctrl jest dostępny
        }
        pclose(fp);
    }
    
    return WM_UNKNOWN;
}

// Funkcja do przełączania obszarów roboczych
static void switch_to_workspace(GtkWidget *widget, gpointer workspace_num) {
    int workspace = GPOINTER_TO_INT(workspace_num);
    char command[128];
    
    // Natychmiast zaktualizuj wygląd przycisków
    for (int i = 0; i < num_workspaces; i++) {
        GtkStyleContext *context = gtk_widget_get_style_context(workspace_buttons[i]);
        
        if (i == workspace) {
            // Aktywny obszar roboczy
            gtk_style_context_add_class(context, "active-workspace");
            current_workspace = i;
        } else {
            // Nieaktywny obszar roboczy
            gtk_style_context_remove_class(context, "active-workspace");
        }
    }
    
    // Przełącz obszar roboczy w zależności od wykrytego menedżera okien
    switch (detected_wm) {
        case WM_I3:
            // i3 numeruje obszary robocze od 1, więc dodajemy 1 do indeksu
            snprintf(command, sizeof(command), "i3-msg workspace number %d", workspace + 1);
            system(command);
            break;
            
        case WM_GNOME:
            {
                // GNOME Shell - użyj większego bufora i prostszej metody
                char gnome_cmd[256];
                
                // Metoda 1: Użyj wmctrl, który działa w GNOME
                snprintf(gnome_cmd, sizeof(gnome_cmd), "wmctrl -s %d", workspace);
                system(gnome_cmd);
                
                // Metoda 2: Alternatywna metoda przez D-Bus (jeśli wmctrl nie zadziała)
                if (system("which gdbus >/dev/null 2>&1") == 0) {
                    snprintf(gnome_cmd, sizeof(gnome_cmd), 
                        "gdbus call --session --dest org.gnome.Shell "
                        "--object-path /org/gnome/Shell "
                        "--method org.gnome.Shell.Eval "
                        "'Meta.workspace_manager_get_workspace_by_index(%d).activate(global.get_current_time())'",
                        workspace);
                    system(gnome_cmd);
                }
            }
            break;
            
        case WM_KDE:
            // KDE/KWin
            snprintf(command, sizeof(command), "qdbus org.kde.KWin /KWin setCurrentDesktop %d", 
                    workspace + 1);
            system(command);
            break;
            
        case WM_XFWM:
        case WM_OPENBOX:
        case WM_OTHER:
            // Użyj wmctrl dla innych menedżerów okien
            snprintf(command, sizeof(command), "wmctrl -s %d", workspace);
            system(command);
            break;
            
        case WM_UNKNOWN:
        default:
            // Próbuj użyć wmctrl jako ostateczność
            snprintf(command, sizeof(command), "wmctrl -s %d 2>/dev/null", workspace);
            system(command);
            break;
    }
}

// Funkcja aktualizująca stan obszarów roboczych
static gboolean update_workspaces(gpointer data) {
    FILE *fp;
    char line[1024];
    int active_workspace = -1;
    
    // Pobierz aktualny obszar roboczy w zależności od menedżera okien
    switch (detected_wm) {
        case WM_I3:
            // Pobierz aktualny obszar roboczy za pomocą i3-msg
            fp = popen("i3-msg -t get_workspaces | grep '\"focused\":true' | sed -E 's/.*\"num\":([0-9]+).*/\\1/' 2>/dev/null", "r");
            if (fp) {
                if (fgets(line, sizeof(line), fp)) {
                    active_workspace = atoi(line) - 1; // i3 numeruje od 1, my od 0
                }
                pclose(fp);
            }
            break;
            
        case WM_GNOME:
            // GNOME Shell - najpierw próbuj wmctrl, który jest bardziej niezawodny
            fp = popen("wmctrl -d | grep '*' | cut -d ' ' -f1 2>/dev/null", "r");
            if (fp) {
                if (fgets(line, sizeof(line), fp)) {
                    active_workspace = atoi(line);
                    pclose(fp);
                    break; // Jeśli udało się pobrać, zakończ
                }
                pclose(fp);
            }
            
            // Jeśli wmctrl nie zadziałał, spróbuj przez gdbus
            if (system("which gdbus >/dev/null 2>&1") == 0) {
                fp = popen("gdbus call --session --dest org.gnome.Shell "
                          "--object-path /org/gnome/Shell "
                          "--method org.gnome.Shell.Eval 'global.workspace_manager.get_active_workspace_index()' "
                          "| grep -o '[0-9]\\+' | head -1", "r");
                if (fp) {
                    if (fgets(line, sizeof(line), fp)) {
                        active_workspace = atoi(line);
                    }
                    pclose(fp);
                }
            }
            break;
            
        case WM_KDE:
            // KDE/KWin
            fp = popen("qdbus org.kde.KWin /KWin currentDesktop 2>/dev/null", "r");
            if (fp) {
                if (fgets(line, sizeof(line), fp)) {
                    active_workspace = atoi(line) - 1; // KDE numeruje od 1
                }
                pclose(fp);
            }
            break;
            
        case WM_XFWM:
        case WM_OPENBOX:
        case WM_OTHER:
            // Pobierz aktualny obszar roboczy za pomocą wmctrl (dla innych WM)
            fp = popen("wmctrl -d | grep '*' | cut -d ' ' -f1 2>/dev/null", "r");
            if (fp) {
                if (fgets(line, sizeof(line), fp)) {
                    active_workspace = atoi(line);
                }
                pclose(fp);
            }
            break;
            
        case WM_UNKNOWN:
        default:
            // Próbuj użyć wmctrl jako ostateczność
            fp = popen("wmctrl -d | grep '*' | cut -d ' ' -f1 2>/dev/null", "r");
            if (fp) {
                if (fgets(line, sizeof(line), fp)) {
                    active_workspace = atoi(line);
                }
                pclose(fp);
            }
            break;
    }
    
    // Jeśli nie udało się pobrać, użyj domyślnej wartości
    if (active_workspace < 0) {
        active_workspace = current_workspace;
    }
    
    // Aktualizuj stan przycisków
    for (int i = 0; i < num_workspaces; i++) {
        GtkStyleContext *context = gtk_widget_get_style_context(workspace_buttons[i]);
        
        if (i == active_workspace) {
            // Aktywny obszar roboczy
            gtk_style_context_add_class(context, "active-workspace");
            current_workspace = i;
        } else {
            // Nieaktywny obszar roboczy
            gtk_style_context_remove_class(context, "active-workspace");
        }
    }
    
    return G_SOURCE_CONTINUE;
}



// Callback dla rysowania tła okna z przezroczystością
static gboolean on_window_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    // Wyczyść tło do całkowitej przezroczystości
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    

    
    return FALSE; // Pozwól na dalsze rysowanie widgetów
}

// Funkcja ustawiająca blur dla okna
static gboolean set_window_blur(GtkWidget *widget) {
    GdkWindow *gdk_window = gtk_widget_get_window(widget);
    if (gdk_window) {
        Display *display = GDK_WINDOW_XDISPLAY(gdk_window);
        Window xid = GDK_WINDOW_XID(gdk_window);
        
        // Ustaw blur dla picom/compton
        Atom blur_atom = XInternAtom(display, "_NET_WM_WINDOW_BLUR", False);
        if (blur_atom != None) {
            unsigned long blur_value = 1;
            XChangeProperty(display, xid, blur_atom, XA_CARDINAL, 32,
                          PropModeReplace, (unsigned char*)&blur_value, 1);
        }
        
        // Ustaw blur region dla KWin
        Atom blur_region_atom = XInternAtom(display, "_KDE_NET_WM_BLUR_BEHIND_REGION", False);
        if (blur_region_atom != None) {
            // Ustaw całe okno jako blur region
            unsigned long region[4] = {0, 0, 0, 0}; // cały obszar
            XChangeProperty(display, xid, blur_region_atom, XA_CARDINAL, 32,
                          PropModeReplace, (unsigned char*)region, 4);
        }
        
        // Dodatkowe właściwości blur dla różnych compositorów
        Atom blur_rounded_atom = XInternAtom(display, "_NET_WM_WINDOW_BLUR_ROUNDED", False);
        if (blur_rounded_atom != None) {
            unsigned long blur_radius = 15;
            XChangeProperty(display, xid, blur_rounded_atom, XA_CARDINAL, 32,
                          PropModeReplace, (unsigned char*)&blur_radius, 1);
        }
        
        XFlush(display);
        g_print("Ustawiono blur dla okna\n");
    }
    return FALSE; // Wyłącz timer
}

// Funkcja do stosowania ustawień panelu
static void apply_panel_settings() {
    // Sprawdź czy compositor obsługuje przezroczystość
    GdkScreen *screen = gtk_widget_get_screen(window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    
    if (visual != NULL && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(window, visual);
        gtk_widget_set_app_paintable(window, TRUE);
        
        // Podłącz callback do rysowania tła
        g_signal_connect(window, "draw", G_CALLBACK(on_window_draw), NULL);
        

        
        g_print("Ustawiono przezroczystość i blur dla panelu\n");
    } else {
        // Fallback dla systemów bez compositora
        g_print("Compositor nie obsługuje przezroczystości, używam solidnego tła\n");
    }
    
    // Tworzenie CSS - przezroczysty panel z blur
    char css[2048];
    snprintf(css, sizeof(css),
        "window { "
        "    background: transparent; "
        "} "
        "label { "
        "    color: white; "
        "    text-shadow: 0 0 10px rgba(0, 0, 0, 0.8), "
        "                 0 1px 3px rgba(0, 0, 0, 0.9); "
        "    font-weight: 600; "
        "} "
        "button { "
        "    color: white; "
        "    background: rgba(255, 255, 255, 0.15); "
        "    border: none; "
        "    border-radius: 8px; "
        "    margin: 2px; "
        "    padding: 4px 8px; "
        "    transition: all 0.2s ease; "
        "    box-shadow: 0 2px 10px rgba(0, 0, 0, 0.4); "
        "} "
        "button:hover { "
        "    background: rgba(255, 255, 255, 0.25); "
        "    box-shadow: 0 4px 20px rgba(0, 0, 0, 0.5), "
        "                0 0 30px rgba(255, 255, 255, 0.2); "
        "    transform: translateY(-1px); "
        "} "
        "button.workspace { "
        "    color: white; "
        "    background: rgba(255, 255, 255, 0.1); "
        "    border-radius: 6px; "
        "    min-width: 32px; "
        "    min-height: 24px; "
        "} "
        "button.active-workspace { "
        "    background: rgba(100, 149, 237, 0.8); "
        "    color: white; "
        "    border: none; "
        "    box-shadow: 0 0 20px rgba(100, 149, 237, 0.8), "
        "                0 2px 10px rgba(0, 0, 0, 0.4); "
        "    text-shadow: 0 0 10px rgba(0, 0, 0, 0.8); "
        "} "
        "button image { "
        "    color: white; "
        "}");
    
    // Zastosowanie CSS
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                             GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
    
    // Aktualizacja zegara
    GtkStyleContext *clock_context = gtk_widget_get_style_context(clock_label);
    gtk_style_context_add_class(clock_context, "clock-label");
    

}



// Funkcja do wczytywania ustawień panelu
static void load_panel_settings() {
    char config_path[256];
    snprintf(config_path, sizeof(config_path), "%s/.config/lum-panel/settings.conf", getenv("HOME"));
    
    FILE *fp = fopen(config_path, "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            char key[64], value[64];
            if (sscanf(line, "%63[^=]=%63s", key, value) == 2) {
                if (strcmp(key, "opacity") == 0) {
                    panel_settings.opacity = atof(value);
                } else if (strcmp(key, "auto_hide") == 0) {
                    panel_settings.auto_hide = atoi(value);
                } else if (strcmp(key, "bg_color") == 0) {
                    strncpy(panel_settings.bg_color, value, sizeof(panel_settings.bg_color) - 1);
                } else if (strcmp(key, "fg_color") == 0) {
                    strncpy(panel_settings.fg_color, value, sizeof(panel_settings.fg_color) - 1);
                }
            }
        }
        fclose(fp);
    }
}

// Funkcja obsługi zmiany przezroczystości


// Funkcja otwierająca okno ustawień


// Funkcja obsługi kliknięcia prawym przyciskiem myszy na panelu


int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    
    // Tworzenie głównego okna
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Lum-panel");
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);  // Bez dekoracji okna
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);  // Nie pokazuj na pasku zadań
    gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);  // Nie pokazuj w przełączniku okien
    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);  // Zawsze na wierzchu
    gtk_window_stick(GTK_WINDOW(window));  // Pokazuj panel na wszystkich obszarach roboczych
    gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_DOCK);  // Zachowuj się jak panel w docku
    gtk_window_set_role(GTK_WINDOW(window), "Lum-panel");  // Ustaw rolę okna
    
    // Ustaw przezroczystość okna
    GdkScreen *screen = gtk_window_get_screen(GTK_WINDOW(window));
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual != NULL && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(window, visual);
    }
    gtk_widget_set_app_paintable(window, TRUE);
    
    // Ustawienie pozycji i rozmiaru
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor = gdk_display_get_primary_monitor(display);
    GdkRectangle geometry;
    gdk_monitor_get_geometry(monitor, &geometry);
    
    gint panel_height = 20;  // Bardzo niski panel
    
    // Ustawienie panelu na całą szerokość ekranu, na górze
    gtk_window_set_default_size(GTK_WINDOW(window), geometry.width, panel_height);
    gtk_window_move(GTK_WINDOW(window), 0, 0);  // Ustawienie panelu na górze ekranu
    
    // Ustaw stałą wielkość panelu - nie pozwól na zmianę rozmiaru
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    
    // Ustaw nowoczesny styl inspirowany macOS i nowoczesnych interfejsów
    GtkCssProvider *main_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(main_provider,
        /* Główne okno panelu */
        "window { "
        "    background: linear-gradient(180deg, rgba(30, 30, 46, 0.95) 0%, rgba(24, 24, 37, 0.95) 100%); "
        "    border-radius: 0; "
        "    box-shadow: 0 4px 20px rgba(0, 0, 0, 0.3); "
        "    border-bottom: 1px solid rgba(205, 214, 244, 0.1); "
        "}"
        
        /* Transparentne tło dla boxów */
        "box { background-color: transparent; }"
        
        /* Domyślne style dla ikon */
        "image { "
        "    color: #cdd6f4; "
        "    -gtk-icon-effect: none; "
        "    -gtk-icon-palette: default #cdd6f4; "
        "}"
        
        "button image { "
        "    color: #cdd6f4; "
        "    -gtk-icon-effect: none; "
        "    -gtk-icon-palette: default #cdd6f4; "
        "}"
        
        /* Globalne style przycisków */
        "button { "
        "    background: transparent; "
        "    border: none; "
        "    border-radius: 6px; "
        "    padding: 4px 8px; "
        "    margin: 2px; "
        "    transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1); "
        "    min-height: 16px; "
        "}"
        
        "button:hover { "
        "    background: rgba(137, 180, 250, 0.15); "
        "    box-shadow: 0 2px 8px rgba(137, 180, 250, 0.2); "
        "    transform: translateY(-1px); "
        "}"
        
        "button:active { "
        "    background: rgba(137, 180, 250, 0.25); "
        "    transform: translateY(0px); "
        "    transition: all 0.1s ease; "
        "}",
        -1, NULL);
    
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                             GTK_STYLE_PROVIDER(main_provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(main_provider);
    
    // Główny kontener
    main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(window), main_box);
    
    // Wczytaj ustawienia
    load_panel_settings();
    
    // Zastosuj ustawienia
    apply_panel_settings();
    
    // Dodaj obsługę kliknięcia prawym przyciskiem myszy

    
    // Kontener na ikony statusu (po lewej)
    GtkWidget *left_status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(main_box), left_status_box, FALSE, FALSE, 2);
    
    // Dodaj przyciski obszarów roboczych z nowoczesnym stylem
    GtkCssProvider *workspace_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(workspace_provider,
        /* Nieaktywne obszary robocze */
        "button.workspace { "
        "    min-width: 28px; "
        "    min-height: 20px; "
        "    padding: 2px 6px; "
        "    margin: 1px 2px; "
        "    border-radius: 8px; "
        "    background: rgba(49, 50, 68, 0.4); "
        "    color: #bac2de; "
        "    font-size: 11px; "
        "    font-weight: 500; "
        "    border: 1px solid rgba(205, 214, 244, 0.1); "
        "    transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1); "
        "}"
        
        "button.workspace:hover { "
        "    background: rgba(137, 180, 250, 0.2); "
        "    border-color: rgba(137, 180, 250, 0.3); "
        "    color: #cdd6f4; "
        "    transform: scale(1.05); "
        "    box-shadow: 0 2px 8px rgba(137, 180, 250, 0.2); "
        "}"
        
        /* Aktywny obszar roboczy */
        "button.active-workspace { "
        "    background: linear-gradient(45deg, rgba(137, 180, 250, 0.9), rgba(116, 199, 236, 0.9)) !important; "
        "    color: #1e1e2e !important; "
        "    font-weight: bold !important; "
        "    border-color: rgba(137, 180, 250, 0.6) !important; "
        "    box-shadow: 0 3px 12px rgba(137, 180, 250, 0.4); "
        "    transform: scale(1.1); "
        "}"
        
        "button.active-workspace:hover { "
        "    background: linear-gradient(45deg, rgba(137, 180, 250, 1.0), rgba(116, 199, 236, 1.0)) !important; "
        "    box-shadow: 0 4px 16px rgba(137, 180, 250, 0.5); "
        "}",
        -1, NULL);
    
    // Wykryj typ menedżera okien
    detected_wm = detect_window_manager();
    FILE *fp;
    char line[1024];
    
    // Pobierz liczbę obszarów roboczych w zależności od menedżera okien
    switch (detected_wm) {
        case WM_I3:
            // Dla i3 pobierz liczbę obszarów roboczych z i3-msg
            fp = popen("i3-msg -t get_workspaces | grep -o '\"num\"' | wc -l 2>/dev/null", "r");
            if (fp) {
                if (fgets(line, sizeof(line), fp)) {
                    num_workspaces = atoi(line);
                    // Sprawdź, czy mamy przynajmniej 4 obszary robocze (domyślnie)
                    if (num_workspaces < 4) {
                        num_workspaces = 4;
                    }
                }
                pclose(fp);
            }
            break;
            
        case WM_GNOME:
            // GNOME Shell - najpierw próbuj wmctrl, który jest bardziej niezawodny
            fp = popen("wmctrl -d | wc -l 2>/dev/null", "r");
            if (fp) {
                if (fgets(line, sizeof(line), fp)) {
                    num_workspaces = atoi(line);
                    if (num_workspaces > 0) {
                        pclose(fp);
                        break; // Jeśli udało się pobrać, zakończ
                    }
                }
                pclose(fp);
            }
            
            // Jeśli wmctrl nie zadziałał, spróbuj przez gdbus
            if (system("which gdbus >/dev/null 2>&1") == 0) {
                fp = popen("gdbus call --session --dest org.gnome.Shell "
                          "--object-path /org/gnome/Shell "
                          "--method org.gnome.Shell.Eval 'global.workspace_manager.n_workspaces' "
                          "| grep -o '[0-9]\\+' | head -1", "r");
                if (fp) {
                    if (fgets(line, sizeof(line), fp)) {
                        num_workspaces = atoi(line);
                    }
                    pclose(fp);
                }
            }
            
            // Jeśli nadal nie mamy wartości, użyj domyślnej
            if (num_workspaces < 1) {
                num_workspaces = 4;
            }
            break;
            
        case WM_KDE:
            // KDE/KWin
            fp = popen("qdbus org.kde.KWin /KWin numberOfDesktops 2>/dev/null", "r");
            if (fp) {
                if (fgets(line, sizeof(line), fp)) {
                    num_workspaces = atoi(line);
                }
                pclose(fp);
            }
            break;
            
        case WM_XFWM:
        case WM_OPENBOX:
        case WM_OTHER:
            // Dla innych WM użyj wmctrl
            fp = popen("wmctrl -d | wc -l 2>/dev/null", "r");
            if (fp) {
                if (fgets(line, sizeof(line), fp)) {
                    num_workspaces = atoi(line);
                }
                pclose(fp);
            }
            break;
            
        case WM_UNKNOWN:
        default:
            // Próbuj użyć wmctrl jako ostateczność
            fp = popen("wmctrl -d | wc -l 2>/dev/null", "r");
            if (fp) {
                if (fgets(line, sizeof(line), fp)) {
                    num_workspaces = atoi(line);
                }
                pclose(fp);
            }
            break;
    }
    
    // Sprawdź limity
    if (num_workspaces > MAX_WORKSPACES) {
        num_workspaces = MAX_WORKSPACES;
    }
    if (num_workspaces < 1) {
        num_workspaces = 4; // Domyślnie 4 obszary robocze
    }
    
    // Utwórz przyciski obszarów roboczych
    for (int i = 0; i < num_workspaces; i++) {
        char label[8];
        snprintf(label, sizeof(label), "%d", i + 1);
        
        workspace_buttons[i] = gtk_button_new_with_label(label);
        gtk_button_set_relief(GTK_BUTTON(workspace_buttons[i]), GTK_RELIEF_NONE);
        
        GtkStyleContext *context = gtk_widget_get_style_context(workspace_buttons[i]);
        gtk_style_context_add_class(context, "workspace");
        gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(workspace_provider), 
                                      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        
        g_signal_connect(workspace_buttons[i], "clicked", G_CALLBACK(switch_to_workspace), GINT_TO_POINTER(i));
        
        gtk_box_pack_start(GTK_BOX(left_status_box), workspace_buttons[i], FALSE, FALSE, 0);
    }
    
    g_object_unref(workspace_provider);
    
    // Uruchom timer do aktualizacji stanu obszarów roboczych
    g_timeout_add(1000, update_workspaces, NULL);
    
    
    // Środkowy zegar
    clock_label = gtk_label_new("");
    gtk_widget_set_name(clock_label, "clock_label");  // Dodaj identyfikator dla CSS
    PangoAttrList *attr_list = pango_attr_list_new();
    PangoAttribute *attr_weight = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    pango_attr_list_insert(attr_list, attr_weight);
    gtk_label_set_attributes(GTK_LABEL(clock_label), attr_list);
    pango_attr_list_unref(attr_list);
    
    // Prosty styl zegara
    GtkCssProvider *clock_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(clock_provider,
        "label { "
        "    color: white; "
        "    font-size: 14px; "
        "    font-weight: bold; "
        "}",
        -1, NULL);
    
    GtkStyleContext *clock_context = gtk_widget_get_style_context(clock_label);
    gtk_style_context_add_provider(clock_context, GTK_STYLE_PROVIDER(clock_provider), 
                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(clock_provider);
    
    // Dodaj zegar bezpośrednio do głównego kontenera, ale wycentrowany
    gtk_widget_set_halign(clock_label, GTK_ALIGN_CENTER);
    gtk_box_set_center_widget(GTK_BOX(main_box), clock_label);
    
    
    // Kontener na ikony statusu (po prawej)
    GtkWidget *right_status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_end(GTK_BOX(main_box), right_status_box, FALSE, FALSE, 2);
    
    // Ikony statusu
    network_button = gtk_button_new();
    volume_button = gtk_button_new(); // Przywracamy jako przycisk
    power_button = gtk_button_new();
    

    
    // Etykieta baterii
    battery_label = gtk_label_new("");
    
    // Style dla przycisków z efektem blur
    GtkCssProvider *icon_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(icon_provider,
        "button { "
        "    background: rgba(255, 255, 255, 0.08); "
        "    border: none; "
        "    border-radius: 8px; "
        "    padding: 6px 10px; "
        "    margin: 3px; "
        "    backdrop-filter: blur(10px); "
        "    transition: all 0.2s ease; "
        "}"
        "button:hover { "
        "    background: rgba(255, 255, 255, 0.2); "
        "    box-shadow: 0 4px 16px rgba(255, 255, 255, 0.15); "
        "    transform: translateY(-1px); "
        "}"
        "scale { "
        "    background: rgba(255, 255, 255, 0.08); "
        "    border-radius: 8px; "
        "    padding: 6px 10px; "
        "    margin: 3px; "
        "    min-height: 20px; "
        "}"
        "scale trough { "
        "    background: rgba(255, 255, 255, 0.15); "
        "    border-radius: 10px; "
        "    min-height: 4px; "
        "    border: none; "
        "}"
        "scale highlight { "
        "    background: rgba(100, 149, 237, 0.8); "
        "    border-radius: 10px; "
        "}"
        "scale slider { "
        "    background: white; "
        "    border-radius: 50%; "
        "    min-width: 12px; "
        "    min-height: 12px; "
        "    border: none; "
        "    box-shadow: 0 2px 6px rgba(0, 0, 0, 0.4); "
        "}"
        "scale slider:hover { "
        "    background: rgba(255, 255, 255, 0.9); "
        "    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.5); "
        "}"
        "label { "
        "    color: rgba(255, 255, 255, 0.95); "
        "    font-size: 12px; "
        "    font-weight: 500; "
        "    text-shadow: 0 1px 2px rgba(0, 0, 0, 0.3); "
        "}",
        -1, NULL);
    
    // Zastosuj styl do przycisków i etykiety
    GtkStyleContext *btn_context;
    btn_context = gtk_widget_get_style_context(network_button);
    gtk_style_context_add_provider(btn_context, GTK_STYLE_PROVIDER(icon_provider), 
                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    
    btn_context = gtk_widget_get_style_context(volume_button);
    gtk_style_context_add_provider(btn_context, GTK_STYLE_PROVIDER(icon_provider), 
                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    

    
    btn_context = gtk_widget_get_style_context(power_button);
    gtk_style_context_add_provider(btn_context, GTK_STYLE_PROVIDER(icon_provider), 
                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    
    // Zastosuj styl do etykiety baterii
    GtkStyleContext *label_context = gtk_widget_get_style_context(battery_label);
    gtk_style_context_add_provider(label_context, GTK_STYLE_PROVIDER(icon_provider), 
                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    
    g_object_unref(icon_provider);
    
    // Ustaw ikony
    gtk_button_set_relief(GTK_BUTTON(network_button), GTK_RELIEF_NONE);
    gtk_button_set_relief(GTK_BUTTON(volume_button), GTK_RELIEF_NONE);
    gtk_button_set_relief(GTK_BUTTON(power_button), GTK_RELIEF_NONE);
    
    // Ustaw domyślne ikony
    gtk_button_set_image(GTK_BUTTON(network_button), 
                        gtk_image_new_from_icon_name("network-offline-symbolic", GTK_ICON_SIZE_MENU));
    gtk_button_set_image(GTK_BUTTON(volume_button), 
                        gtk_image_new_from_icon_name("audio-volume-high-symbolic", GTK_ICON_SIZE_MENU));
    gtk_button_set_image(GTK_BUTTON(power_button), 
                        gtk_image_new_from_icon_name("system-shutdown-symbolic", GTK_ICON_SIZE_MENU));
    
    // Włącz maski zdarzeń dla scroll
    gtk_widget_add_events(volume_button, GDK_SCROLL_MASK);
    
    // Podłącz sygnały
    g_signal_connect(network_button, "button-press-event", G_CALLBACK(on_network_clicked), NULL);
    g_signal_connect(volume_button, "button-press-event", G_CALLBACK(on_volume_clicked), NULL);
    g_signal_connect(volume_button, "scroll-event", G_CALLBACK(on_volume_scroll), NULL);
    g_signal_connect(power_button, "button-press-event", G_CALLBACK(on_power_clicked), NULL);
    
    // Dodaj przyciski i etykietę do panelu
    gtk_box_pack_start(GTK_BOX(right_status_box), network_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_status_box), volume_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_status_box), battery_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_status_box), power_button, FALSE, FALSE, 0);
    
    // Aktualizacja ikon
    update_network_icon();
    update_volume_icon();
    update_battery_status(NULL);
    
    // Dodaj timery do okresowego sprawdzania statusu
    g_timeout_add_seconds(5, update_network_status, NULL);
    g_timeout_add_seconds(2, update_volume_status, NULL);
    g_timeout_add_seconds(10, update_battery_status, NULL);
    
    // Uruchomienie timera do aktualizacji zegara
    g_timeout_add_seconds(1, update_clock, NULL);
    update_clock(NULL);  // Natychmiastowa aktualizacja
    
    // Sygnał zamknięcia
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    
    // Wyświetlenie wszystkiego
    gtk_widget_show_all(window);
    
    // Ustaw blur po pokazaniu okna
    g_timeout_add(100, (GSourceFunc)set_window_blur, window);
    
    // Zarezerwuj przestrzeń na ekranie dla panelu (po pokazaniu okna)
    g_timeout_add(500, reserve_screen_space, window);
    
    // Pętla główna
    gtk_main();
    
    return 0;
}
