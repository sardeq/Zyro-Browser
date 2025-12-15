#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <string>
#include <iostream>
#include <sys/stat.h>

// --- Debug Macro ---
#define LOG(msg) std::cout << "[DEBUG] " << msg << std::endl

// --- Global Settings ---
struct AppSettings {
    std::string home_page = "https://google.com";
    std::string search_engine = "https://www.google.com/search?q=";
} settings;

WebKitWebContext* global_context = nullptr; 

// --- Helpers ---
std::string get_config_path() {
    const char* home = g_get_home_dir();
    if (!home) return "";
    return std::string(home) + "/.config/zyro/settings.ini";
}

void ensure_directory_exists(const std::string& path) {
    struct stat st = {0};
    if (stat(path.c_str(), &st) == -1) {
        LOG("Creating directory: " + path);
        mkdir(path.c_str(), 0700);
    }
}

void load_settings() {
    GKeyFile* key_file = g_key_file_new();
    std::string path = get_config_path();
    if (!path.empty() && g_key_file_load_from_file(key_file, path.c_str(), G_KEY_FILE_NONE, NULL)) {
        gchar* engine = g_key_file_get_string(key_file, "General", "search_engine", NULL);
        if (engine) { settings.search_engine = engine; g_free(engine); }
    }
    g_key_file_free(key_file);
}

std::string get_asset_path(const std::string& filename) {
    std::string path = "assets/" + filename;
    if (g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) return path;
    return "../assets/" + filename; 
}

void load_css() {
    std::string css_path = get_asset_path("style.css");
    if (!g_file_test(css_path.c_str(), G_FILE_TEST_EXISTS)) {
        LOG("WARNING: style.css not found");
        return;
    }

    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_path(provider, css_path.c_str(), NULL);
    GdkScreen* screen = gdk_screen_get_default();
    if (screen) {
        gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    g_object_unref(provider);
}

// --- Window Helpers ---
GtkNotebook* get_notebook(GtkWidget* win) {
    return GTK_NOTEBOOK(g_object_get_data(G_OBJECT(win), "notebook"));
}

GtkEntry* get_url_bar(GtkWidget* win) {
    return GTK_ENTRY(g_object_get_data(G_OBJECT(win), "url_bar"));
}

WebKitWebView* get_current_view(GtkWidget* win) {
    GtkNotebook* nb = get_notebook(win);
    if (!nb) return nullptr;
    int page = gtk_notebook_get_current_page(nb);
    if (page == -1) return nullptr;
    return WEBKIT_WEB_VIEW(gtk_notebook_get_nth_page(nb, page));
}

// --- Forward Declarations ---
GtkWidget* create_new_tab(GtkWidget* win, const std::string& url, WebKitWebContext* context);
void create_browser_window(WebKitWebContext* context, bool is_incognito);

// --- Callbacks ---
static void on_tab_close_clicked(GtkButton* btn, gpointer user_data) {
    GtkWidget* view = GTK_WIDGET(user_data);
    GtkWidget* win = GTK_WIDGET(g_object_get_data(G_OBJECT(view), "window-ptr"));
    GtkNotebook* nb = get_notebook(win);
    int page_num = gtk_notebook_page_num(nb, view);
    if (page_num != -1) gtk_notebook_remove_page(nb, page_num);
}

static void on_title_changed(WebKitWebView* view, GParamSpec* ps, gpointer user_data) {
    GtkLabel* label = GTK_LABEL(user_data);
    const char* title = webkit_web_view_get_title(view);
    if (title) gtk_label_set_text(label, title);
}

static void on_load_changed(WebKitWebView* view, WebKitLoadEvent event, gpointer user_data) {
    if (event == WEBKIT_LOAD_COMMITTED) {
        GtkWidget* win = GTK_WIDGET(g_object_get_data(G_OBJECT(view), "window-ptr"));
        if (win && get_current_view(win) == view) {
            const char* uri = webkit_web_view_get_uri(view);
            if (uri) gtk_entry_set_text(get_url_bar(win), uri);
        }
    }
}

static GtkWidget* on_create_new_window(WebKitWebView* view, WebKitNavigationAction* action, gpointer user_data) {
    GtkWidget* win = GTK_WIDGET(g_object_get_data(G_OBJECT(view), "window-ptr"));
    WebKitWebContext* ctx = webkit_web_view_get_context(view);
    create_new_tab(win, "", ctx);
    return GTK_WIDGET(get_current_view(win));
}

static void on_url_activate(GtkEntry* entry, gpointer user_data) {
    GtkWidget* win = GTK_WIDGET(user_data);
    WebKitWebView* view = get_current_view(win);
    if (!view) return;

    std::string text = gtk_entry_get_text(entry);
    if (text.find(" ") != std::string::npos || text.find(".") == std::string::npos) {
        std::string s = settings.search_engine + text;
        webkit_web_view_load_uri(view, s.c_str());
    } else {
        if (text.find("http") != 0) text = "https://" + text;
        webkit_web_view_load_uri(view, text.c_str());
    }
    gtk_widget_grab_focus(GTK_WIDGET(view));
}

static void on_new_tab_clicked(GtkButton* btn, gpointer user_data) {
    GtkWidget* win = GTK_WIDGET(user_data);
    WebKitWebView* current = get_current_view(win);
    WebKitWebContext* ctx = current ? webkit_web_view_get_context(current) : global_context;
    create_new_tab(win, settings.home_page, ctx);
}

static void on_back_clicked(GtkButton* btn, gpointer user_data) {
    WebKitWebView* v = get_current_view(GTK_WIDGET(user_data));
    if (v) webkit_web_view_go_back(v);
}

static void on_fwd_clicked(GtkButton* btn, gpointer user_data) {
    WebKitWebView* v = get_current_view(GTK_WIDGET(user_data));
    if (v) webkit_web_view_go_forward(v);
}

static void on_refresh_clicked(GtkButton* btn, gpointer user_data) {
    WebKitWebView* v = get_current_view(GTK_WIDGET(user_data));
    if (v) webkit_web_view_reload(v);
}

static void on_incognito_clicked(GtkButton* btn, gpointer user_data) {
    WebKitWebContext* eph = webkit_web_context_new_ephemeral();
    create_browser_window(eph, true);
}

static void on_page_switched(GtkNotebook* nb, GtkWidget* page, guint page_num, gpointer user_data) {
    GtkWidget* win = GTK_WIDGET(user_data);
    WebKitWebView* view = WEBKIT_WEB_VIEW(page);
    const char* uri = webkit_web_view_get_uri(view);
    if (uri) gtk_entry_set_text(get_url_bar(win), uri);
}

static gboolean on_key_press(GtkWidget* win, GdkEventKey* event, gpointer user_data) {
    if (event->state & GDK_CONTROL_MASK) {
        if (event->keyval == GDK_KEY_t) {
            on_new_tab_clicked(NULL, win);
            return TRUE;
        }
        if (event->keyval == GDK_KEY_w) {
            WebKitWebView* v = get_current_view(win);
            if (v) on_tab_close_clicked(NULL, v);
            return TRUE;
        }
    }
    return FALSE;
}

// --- Logic ---

GtkWidget* create_new_tab(GtkWidget* win, const std::string& url, WebKitWebContext* context) {
    GtkNotebook* notebook = get_notebook(win);
    if (!notebook) return NULL;

    GtkWidget* web_view = webkit_web_view_new_with_context(context);
    g_object_set_data(G_OBJECT(web_view), "window-ptr", win);

    GtkWidget* tab_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget* tab_label = gtk_label_new("New Tab");
    GtkWidget* close_btn = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
    gtk_widget_set_name(close_btn, "tab-close-btn"); 
    
    gtk_box_pack_start(GTK_BOX(tab_box), tab_label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(tab_box), close_btn, FALSE, FALSE, 0);
    gtk_widget_show_all(tab_box);

    int page_num = gtk_notebook_append_page(notebook, web_view, tab_box);
    gtk_notebook_set_tab_reorderable(notebook, web_view, TRUE);
    gtk_widget_show(web_view);
    
    g_signal_connect(close_btn, "clicked", G_CALLBACK(on_tab_close_clicked), web_view);
    g_signal_connect(web_view, "notify::title", G_CALLBACK(on_title_changed), tab_label);
    g_signal_connect(web_view, "load-changed", G_CALLBACK(on_load_changed), NULL);
    g_signal_connect(web_view, "create", G_CALLBACK(on_create_new_window), NULL);

    gtk_notebook_set_current_page(notebook, page_num);
    
    if (!url.empty()) webkit_web_view_load_uri(WEBKIT_WEB_VIEW(web_view), url.c_str());

    return web_view;
}

void create_browser_window(WebKitWebContext* context, bool is_incognito) {
    LOG("Creating browser window...");
    GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    
    // [FIX] Attach window to the GtkApplication so it doesn't auto-quit
    GtkApplication* app = GTK_APPLICATION(g_application_get_default());
    gtk_window_set_application(GTK_WINDOW(win), app);

    gtk_window_set_default_size(GTK_WINDOW(win), 1200, 800);
    gtk_window_set_title(GTK_WINDOW(win), is_incognito ? "Zyro - Incognito" : "Zyro Browser");
    
    if (is_incognito) gtk_style_context_add_class(gtk_widget_get_style_context(win), "incognito-window");

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_style_context_add_class(gtk_widget_get_style_context(toolbar), "toolbar");

    GtkWidget* notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(notebook), FALSE);
    g_object_set_data(G_OBJECT(win), "notebook", notebook);

    GtkWidget* btn_back = gtk_button_new_from_icon_name("go-previous-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget* btn_fwd = gtk_button_new_from_icon_name("go-next-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget* btn_refresh = gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget* btn_new_tab = gtk_button_new_from_icon_name("tab-new-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget* url_entry = gtk_entry_new();
    
    g_object_set_data(G_OBJECT(win), "url_bar", url_entry);
    gtk_entry_set_placeholder_text(GTK_ENTRY(url_entry), is_incognito ? "Search (Incognito)" : "Search or type URL");

    GtkWidget* btn_menu = gtk_menu_button_new();
    gtk_button_set_image(GTK_BUTTON(btn_menu), gtk_image_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_BUTTON));
    GtkWidget* popover = gtk_popover_new(btn_menu);
    GtkWidget* menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(menu_box), 10);
    GtkWidget* btn_incog = gtk_button_new_with_label("New Incognito Window");
    gtk_box_pack_start(GTK_BOX(menu_box), btn_incog, FALSE, FALSE, 0);
    gtk_widget_show_all(menu_box);
    gtk_container_add(GTK_CONTAINER(popover), menu_box); 
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(btn_menu), popover);

    gtk_box_pack_start(GTK_BOX(toolbar), btn_back, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_fwd, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), url_entry, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_new_tab, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_menu, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(win), vbox);

    g_signal_connect(btn_new_tab, "clicked", G_CALLBACK(on_new_tab_clicked), win);
    g_signal_connect(url_entry, "activate", G_CALLBACK(on_url_activate), win);
    g_signal_connect(btn_back, "clicked", G_CALLBACK(on_back_clicked), win);
    g_signal_connect(btn_fwd, "clicked", G_CALLBACK(on_fwd_clicked), win);
    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_refresh_clicked), win);
    g_signal_connect(btn_incog, "clicked", G_CALLBACK(on_incognito_clicked), NULL);
    g_signal_connect(notebook, "switch-page", G_CALLBACK(on_page_switched), win);
    g_signal_connect(win, "key-press-event", G_CALLBACK(on_key_press), win);
    
    gtk_widget_show_all(win);
    
    LOG("Opening initial tab...");
    create_new_tab(win, settings.home_page, context);
}

static void activate(GtkApplication* app, gpointer user_data) {
    LOG("App Activate started...");
    load_css();
    load_settings();

    char* cwd = g_get_current_dir();
    std::string base_dir = std::string(cwd);
    g_free(cwd);
    
    std::string data_dir = base_dir + "/data";
    std::string cache_dir = base_dir + "/cache";
    ensure_directory_exists(data_dir);
    ensure_directory_exists(cache_dir);

    LOG("Initializing Data Manager...");
    WebKitWebsiteDataManager* manager = webkit_website_data_manager_new(
        "base-cache-directory", cache_dir.c_str(),
        "base-data-directory", data_dir.c_str(), NULL);
    
    global_context = webkit_web_context_new_with_website_data_manager(manager);
    
    WebKitCookieManager* cm = webkit_web_context_get_cookie_manager(global_context);
    std::string cookie_file = data_dir + "/cookies.sqlite";
    webkit_cookie_manager_set_persistent_storage(cm, cookie_file.c_str(), WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);

    create_browser_window(global_context, false);
    LOG("App Activate finished.");
}

int main(int argc, char** argv) {
    LOG("Main started.");
    GtkApplication* app = gtk_application_new("org.zyro.browser", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    LOG("Main exiting.");
    return status;
}