#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <string>
#include <iostream>
#include <vector>

// --- Global State ---
GtkWidget* notebook;
GtkEntry* url_bar;
GtkWidget* window;

// --- Settings Struct ---
struct AppSettings {
    std::string home_page = "https://google.com";
    std::string search_engine = "https://www.google.com/search?q=";
} settings;

// --- Helper: Get Settings Path ---
std::string get_config_path() {
    const char* home = g_get_home_dir();
    return std::string(home) + "/.config/zyro/settings.ini";
}

// --- Helper: Load/Save Settings ---
void load_settings() {
    GKeyFile* key_file = g_key_file_new();
    if (g_key_file_load_from_file(key_file, get_config_path().c_str(), G_KEY_FILE_NONE, NULL)) {
        gchar* engine = g_key_file_get_string(key_file, "General", "search_engine", NULL);
        if (engine) { settings.search_engine = engine; g_free(engine); }
    }
    g_key_file_free(key_file);
}

void save_settings() {
    GKeyFile* key_file = g_key_file_new();
    g_key_file_set_string(key_file, "General", "search_engine", settings.search_engine.c_str());
    
    // Ensure dir exists
    std::string path = get_config_path();
    std::string dir = path.substr(0, path.find_last_of('/'));
    g_mkdir_with_parents(dir.c_str(), 0755);

    g_key_file_save_to_file(key_file, path.c_str(), NULL);
    g_key_file_free(key_file);
}

// --- Helper: Get Asset Path ---
std::string get_asset_path(const std::string& filename) {
    std::string path = "assets/" + filename;
    if (g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) return path;
    return "../assets/" + filename; // Fallback for build dir
}

// --- Helper: Load CSS ---
void load_css() {
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_path(provider, get_asset_path("style.css").c_str(), NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

// --- Tab Logic ---
WebKitWebView* get_current_webview() {
    int page_num = gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook));
    if (page_num == -1) return NULL;
    return WEBKIT_WEB_VIEW(gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), page_num));
}

void update_url_bar(WebKitWebView* view) {
    if (view == get_current_webview()) {
        const char* uri = webkit_web_view_get_uri(view);
        if (uri) gtk_entry_set_text(url_bar, uri);
    }
}

// --- Tab Creation ---
void create_new_tab(const std::string& url, WebKitWebContext* context = NULL) {
    // 1. Context Management
    if (!context) {
        // Use default persistent context
        context = webkit_web_context_get_default();
    }

    // 2. Create WebView
    GtkWidget* web_view = webkit_web_view_new_with_context(context);
    
    // 3. Create Tab Label with Close Button
    GtkWidget* tab_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget* tab_label = gtk_label_new("New Tab");
    GtkWidget* close_btn = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
    
    // Make close button flat and small
    gtk_widget_set_name(close_btn, "tab-close-btn"); 
    gtk_box_pack_start(GTK_BOX(tab_box), tab_label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(tab_box), close_btn, FALSE, FALSE, 0);
    gtk_widget_show_all(tab_box);

    // 4. Add to Notebook
    int page_num = gtk_notebook_append_page(GTK_NOTEBOOK(notebook), web_view, tab_box);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(notebook), web_view, TRUE);
    gtk_widget_show(web_view);
    
    // 5. Connect Signals
    // Close Tab
    g_signal_connect(close_btn, "clicked", G_CALLBACK(+[](GtkButton* b, GtkWidget* view){
        int page = gtk_notebook_page_num(GTK_NOTEBOOK(notebook), view);
        gtk_notebook_remove_page(GTK_NOTEBOOK(notebook), page);
    }), web_view);

    // Update Title
    g_signal_connect(web_view, "notify::title", G_CALLBACK(+[](WebKitWebView* view, GParamSpec* ps, GtkLabel* label){
        const char* title = webkit_web_view_get_title(view);
        if(title) gtk_label_set_text(label, title);
    }), tab_label);

    // Update URL Bar
    g_signal_connect(web_view, "load-changed", G_CALLBACK(+[](WebKitWebView* v, WebKitLoadEvent e, gpointer d){
        if(e == WEBKIT_LOAD_COMMITTED) update_url_bar(v);
    }), NULL);
    
    // Handle "New Window" links (Open in new tab instead)
    g_signal_connect(web_view, "create", G_CALLBACK(+[](WebKitWebView* v, WebKitNavigationAction* a){
        create_new_tab("");
        return get_current_webview(); // Return the new view
    }), NULL);

    // 6. Switch to this tab
    gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), page_num);
    
    // 7. Load URL
    if (!url.empty()) webkit_web_view_load_uri(WEBKIT_WEB_VIEW(web_view), url.c_str());
}

// --- Navigation Callbacks ---
void on_url_activated(GtkEntry* entry, gpointer data) {
    WebKitWebView* view = get_current_webview();
    if (!view) return;

    std::string text = gtk_entry_get_text(entry);
    if (text.find(" ") != std::string::npos || text.find(".") == std::string::npos) {
        std::string search_url = settings.search_engine + text;
        webkit_web_view_load_uri(view, search_url.c_str());
    } else {
        if (text.find("http") != 0) text = "https://" + text;
        webkit_web_view_load_uri(view, text.c_str());
    }
    gtk_widget_grab_focus(GTK_WIDGET(view));
}

// --- Settings Dialog ---
void show_settings_dialog() {
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Settings", GTK_WINDOW(window), 
        GTK_DIALOG_MODAL, "Save", GTK_RESPONSE_ACCEPT, "Cancel", GTK_RESPONSE_CANCEL, NULL);
    
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 20);

    // Search Engine Input
    GtkWidget* label = gtk_label_new("Search Engine URL:");
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), settings.search_engine.c_str());
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_widget_set_size_request(entry, 300, -1);

    gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry, 1, 0, 1, 1);
    
    gtk_container_add(GTK_CONTAINER(content), grid);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        settings.search_engine = gtk_entry_get_text(GTK_ENTRY(entry));
        save_settings();
    }
    gtk_widget_destroy(dialog);
}

// --- Incognito Mode ---
void open_incognito_window() {
    // For true isolation, we spawn a new window with an ephemeral context
    GtkWidget* incog_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(incog_window), "Zyro - Incognito");
    gtk_window_set_default_size(GTK_WINDOW(incog_window), 1000, 700);

    WebKitWebContext* ephemeral_ctx = webkit_web_context_new_ephemeral();
    GtkWidget* incog_view = webkit_web_view_new_with_context(ephemeral_ctx);
    
    gtk_container_add(GTK_CONTAINER(incog_window), incog_view);
    gtk_widget_show_all(incog_window);
    
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(incog_view), "https://duckduckgo.com");
}

static void activate(GtkApplication* app, gpointer user_data) {
    load_css();
    load_settings();

    // 1. Setup Persistent Data for Main Session
    char* cwd = g_get_current_dir();
    std::string data_dir = std::string(cwd) + "/data";
    std::string cache_dir = std::string(cwd) + "/cache";
    g_free(cwd);

    WebKitWebsiteDataManager* manager = webkit_website_data_manager_new(
        "base-cache-directory", cache_dir.c_str(),
        "base-data-directory", data_dir.c_str(), NULL);
    
    WebKitWebContext* context = webkit_web_context_new_with_website_data_manager(manager);
    
    // Cookies Persistence
    WebKitCookieManager* cookie_manager = webkit_web_context_get_cookie_manager(context);
    std::string cookie_file = data_dir + "/cookies.sqlite";
    webkit_cookie_manager_set_persistent_storage(cookie_manager, cookie_file.c_str(), WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);

    // 2. Main Window
    window = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(window), 1200, 800);
    gtk_window_set_title(GTK_WINDOW(window), "Zyro Browser");

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_style_context_add_class(gtk_widget_get_style_context(toolbar), "toolbar");

    // 3. Tab Container (Notebook)
    notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(notebook), FALSE);

    // Update URL bar when tab changes
    g_signal_connect(notebook, "switch-page", G_CALLBACK(+[](GtkNotebook* nb, GtkWidget* page, guint num, gpointer d){
         update_url_bar(WEBKIT_WEB_VIEW(page));
    }), NULL);

    // 4. UI Elements
    GtkWidget* btn_back = gtk_button_new_from_icon_name("go-previous-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget* btn_fwd = gtk_button_new_from_icon_name("go-next-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget* btn_refresh = gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget* btn_new_tab = gtk_button_new_from_icon_name("tab-new-symbolic", GTK_ICON_SIZE_BUTTON);
    
    url_bar = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(url_bar, "Search or type URL");

    // Settings Menu Button
    GtkWidget* btn_menu = gtk_menu_button_new();
    GtkWidget* menu_icon = gtk_image_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(GTK_BUTTON(btn_menu), menu_icon);
    
    // Create Popover Menu
    GtkWidget* popover = gtk_popover_new(btn_menu);
    GtkWidget* menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(menu_box), 10);
    
    GtkWidget* btn_incognito = gtk_button_new_with_label("New Incognito Window");
    GtkWidget* btn_settings = gtk_button_new_with_label("Settings");
    
    gtk_box_pack_start(GTK_BOX(menu_box), btn_incognito, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(menu_box), btn_settings, FALSE, FALSE, 0);
    gtk_widget_show_all(menu_box);
    
    // *** FIX: Use gtk_container_add for GTK 3 ***
    gtk_container_add(GTK_CONTAINER(popover), menu_box); 
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(btn_menu), popover);

    // 5. Pack UI
    gtk_box_pack_start(GTK_BOX(toolbar), btn_back, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_fwd, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), GTK_WIDGET(url_bar), TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_new_tab, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_menu, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    // 6. Global Signals
    g_signal_connect(btn_new_tab, "clicked", G_CALLBACK(+[](GtkButton* b){ create_new_tab(settings.home_page); }), NULL);
    g_signal_connect(url_bar, "activate", G_CALLBACK(on_url_activated), NULL);
    
    // Navigation Signals (Active Tab Only)
    g_signal_connect(btn_back, "clicked", G_CALLBACK(+[]{ 
        WebKitWebView* v = get_current_webview(); if(v) webkit_web_view_go_back(v); 
    }), NULL);
    g_signal_connect(btn_fwd, "clicked", G_CALLBACK(+[]{ 
        WebKitWebView* v = get_current_webview(); if(v) webkit_web_view_go_forward(v); 
    }), NULL);
    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(+[]{ 
        WebKitWebView* v = get_current_webview(); if(v) webkit_web_view_reload(v); 
    }), NULL);

    // Menu Signals
    g_signal_connect(btn_incognito, "clicked", G_CALLBACK(open_incognito_window), NULL);
    g_signal_connect(btn_settings, "clicked", G_CALLBACK(show_settings_dialog), NULL);

    // 7. Launch
    gtk_widget_show_all(window);
    
    // Open initial tab
    create_new_tab(settings.home_page, context);
}

int main(int argc, char** argv) {
    GtkApplication* app = gtk_application_new("org.zyro.browser", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}