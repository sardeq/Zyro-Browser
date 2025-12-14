#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <string>
#include <iostream>

// Global pointers
WebKitWebView* web_view;
GtkEntry* url_bar;

// --- Helper: Find Assets (Fixes the "No such file" error) ---
// Checks current dir, then parent dir (for when running from /build)
std::string get_asset_path(const std::string& filename) {
    std::string path = "assets/" + filename;
    if (g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) {
        return path;
    }
    
    // Check parent directory (common when running from build/)
    path = "../assets/" + filename;
    if (g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) {
        return path;
    }

    // Fallback: Return absolute path logging for debugging
    g_warning("Could not find asset: %s. Checked ./assets and ../assets", filename.c_str());
    return filename; 
}

// --- Helper: Load CSS ---
void load_css() {
    GtkCssProvider* provider = gtk_css_provider_new();
    std::string css_path = get_asset_path("style.css");
    
    GError* error = NULL;
    GFile* css_file = g_file_new_for_path(css_path.c_str());
    
    // Use load_from_path directly for better error handling
    if(!gtk_css_provider_load_from_file(provider, css_file, &error)) {
        g_warning("Failed to load CSS: %s", error->message);
        g_error_free(error);
    } else {
        gtk_style_context_add_provider_for_screen(
            gdk_screen_get_default(),
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
        );
    }
    g_object_unref(css_file);
}

// --- Helper: Update URL Bar ---
void update_url_bar(WebKitWebView* view, GtkEntry* entry) {
    const char* uri = webkit_web_view_get_uri(view);
    if (uri) gtk_entry_set_text(entry, uri);
}

// --- Callback: Enter in URL bar ---
void on_url_activated(GtkEntry* entry, gpointer data) {
    std::string text = gtk_entry_get_text(entry);
    if (text.find(" ") != std::string::npos || text.find(".") == std::string::npos) {
        std::string search_url = "https://www.google.com/search?q=" + text;
        webkit_web_view_load_uri(web_view, search_url.c_str());
    } else {
        if (text.find("http") != 0) text = "https://" + text;
        webkit_web_view_load_uri(web_view, text.c_str());
    }
    gtk_widget_grab_focus(GTK_WIDGET(web_view));
}

static void activate(GtkApplication* app, gpointer user_data) {
    load_css();

    // 1. Setup Persistent Data Managers (Fixes Logins/Languages)
    // We get the current working directory to make absolute paths
    char* cwd = g_get_current_dir();
    std::string cache_dir = std::string(cwd) + "/cache";
    std::string data_dir = std::string(cwd) + "/data";
    g_free(cwd);

    WebKitWebsiteDataManager* manager = webkit_website_data_manager_new(
        "base-cache-directory", cache_dir.c_str(),
        "base-data-directory", data_dir.c_str(),
        NULL);

    WebKitWebContext* context = webkit_web_context_new_with_website_data_manager(manager);

    // *** CRITICAL FIX FOR LOGINS ***
    // Explicitly tell the cookie manager to save to a database file
    WebKitCookieManager* cookie_manager = webkit_web_context_get_cookie_manager(context);
    std::string cookie_file = data_dir + "/cookies.sqlite";
    webkit_cookie_manager_set_persistent_storage(
        cookie_manager, 
        cookie_file.c_str(), 
        WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE
    );
    // *******************************

    // 2. Create Window & Layout
    GtkWidget* window = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(window), 1200, 800);
    gtk_window_set_title(GTK_WINDOW(window), "Zyro Browser");

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_name(toolbar, "toolbar");
    gtk_style_context_add_class(gtk_widget_get_style_context(toolbar), "toolbar");

    // 3. Create Web View
    web_view = WEBKIT_WEB_VIEW(webkit_web_view_new_with_context(context));

    // 4. Create UI Elements
    GtkWidget* btn_back = gtk_button_new_from_icon_name("go-previous-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget* btn_fwd = gtk_button_new_from_icon_name("go-next-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget* btn_refresh = gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
    url_bar = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(url_bar, "Search Google or type a URL");

    // 5. Pack UI
    gtk_box_pack_start(GTK_BOX(toolbar), btn_back, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_fwd, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), GTK_WIDGET(url_bar), TRUE, TRUE, 5);
    
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(web_view), TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    // 6. Signals
    g_signal_connect_swapped(btn_back, "clicked", G_CALLBACK(webkit_web_view_go_back), web_view);
    g_signal_connect_swapped(btn_fwd, "clicked", G_CALLBACK(webkit_web_view_go_forward), web_view);
    g_signal_connect_swapped(btn_refresh, "clicked", G_CALLBACK(webkit_web_view_reload), web_view);
    g_signal_connect(url_bar, "activate", G_CALLBACK(on_url_activated), NULL);
    g_signal_connect(web_view, "load-changed", G_CALLBACK(+[](WebKitWebView* v, WebKitLoadEvent e, gpointer d){
        if(e == WEBKIT_LOAD_COMMITTED) update_url_bar(v, url_bar);
    }), NULL);

    gtk_widget_show_all(window);
    
    // Load Homepage (Your local app.html if found, else Google)
    // Using the new helper to find app.html
    std::string home_path = "file://" + get_asset_path("app.html");
    
    webkit_web_view_load_uri(web_view, "https://google.com");
}

int main(int argc, char** argv) {
    GtkApplication* app = gtk_application_new("org.zyro.browser", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}