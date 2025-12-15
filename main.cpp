#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <string>
#include <iostream>
#include <fstream>
#include <sys/sysinfo.h>
#include <vector>
#include <sstream>
#include <memory>

// --- Debug & Globals ---
#define LOG(msg) std::cout << "[DEBUG] " << msg << std::endl

struct AppSettings {
    std::string home_url = "file://"; 
    std::string settings_url = "file://";
    std::string search_engine = "https://www.google.com/search?q=";
    std::string theme = "dark";
} settings;

WebKitWebContext* global_context = nullptr; 
GtkWidget* global_window = nullptr; 

// --- Path Helpers ---
std::string get_assets_path() {
    char* cwd = g_get_current_dir();
    std::string current(cwd);
    g_free(cwd);

    // 1. Check if "assets" is in the current folder (running from root)
    if (g_file_test((current + "/assets").c_str(), G_FILE_TEST_IS_DIR)) {
        return current + "/assets/";
    }

    // 2. Check if "assets" is in the parent folder (running from build/)
    size_t last_slash = current.find_last_of('/');
    if (last_slash != std::string::npos) {
        std::string parent = current.substr(0, last_slash);
        if (g_file_test((parent + "/assets").c_str(), G_FILE_TEST_IS_DIR)) {
            return parent + "/assets/";
        }
    }

    // Fallback: return hardcoded assumption or current
    return current + "/assets/";
}

std::string get_config_path() {
    const char* home = g_get_home_dir();
    return std::string(home) + "/.config/zyro/settings.ini";
}

// --- System Stats Helper ---
void get_sys_stats(int& cpu_usage, std::string& ram_usage) {
    // 1. RAM
    struct sysinfo memInfo;
    sysinfo(&memInfo);
    long long totalPhysMem = memInfo.totalram;
    totalPhysMem *= memInfo.mem_unit;
    long long physMemUsed = memInfo.totalram - memInfo.freeram;
    physMemUsed *= memInfo.mem_unit;
    
    std::stringstream ss;
    ss.precision(1);
    ss << std::fixed << (double)physMemUsed / (1024*1024*1024) << " / " 
       << (double)totalPhysMem / (1024*1024*1024) << " GB";
    ram_usage = ss.str();

    // 2. CPU
    static unsigned long long lastTotalUser, lastTotalUserLow, lastTotalSys, lastTotalIdle;
    unsigned long long totalUser, totalUserLow, totalSys, totalIdle, total;
    
    std::ifstream file("/proc/stat");
    std::string line;
    std::getline(file, line);
    std::sscanf(line.c_str(), "cpu %llu %llu %llu %llu", &totalUser, &totalUserLow, &totalSys, &totalIdle);

    if (totalUser < lastTotalUser || totalIdle < lastTotalIdle) {
        cpu_usage = 0;
    } else {
        total = (totalUser - lastTotalUser) + (totalUserLow - lastTotalUserLow) + (totalSys - lastTotalSys);
        unsigned long long percent = total;
        total += (totalIdle - lastTotalIdle);
        cpu_usage = (total > 0) ? (int)((percent * 100) / total) : 0;
    }

    lastTotalUser = totalUser;
    lastTotalUserLow = totalUserLow;
    lastTotalSys = totalSys;
    lastTotalIdle = totalIdle;
}

// --- Settings Management ---
void load_settings() {
    GKeyFile* key_file = g_key_file_new();
    if (g_key_file_load_from_file(key_file, get_config_path().c_str(), G_KEY_FILE_NONE, NULL)) {
        gchar* engine = g_key_file_get_string(key_file, "General", "search_engine", NULL);
        if (engine) { settings.search_engine = engine; g_free(engine); }
        
        gchar* theme = g_key_file_get_string(key_file, "General", "theme", NULL);
        if (theme) { settings.theme = theme; g_free(theme); }
    }
    g_key_file_free(key_file);
    
    // Use the smart path finder
    std::string base = get_assets_path();
    settings.home_url = "file://" + base + "home.html";
    settings.settings_url = "file://" + base + "settings.html";
    LOG("Home URL set to: " + settings.home_url);
}

void save_settings(const std::string& engine, const std::string& theme) {
    settings.search_engine = engine;
    settings.theme = theme;
    
    GKeyFile* key_file = g_key_file_new();
    g_key_file_set_string(key_file, "General", "search_engine", settings.search_engine.c_str());
    g_key_file_set_string(key_file, "General", "theme", settings.theme.c_str());
    
    std::string path = get_config_path();
    std::string dir = path.substr(0, path.find_last_of('/'));
    g_mkdir_with_parents(dir.c_str(), 0755);
    g_key_file_save_to_file(key_file, path.c_str(), NULL);
    g_key_file_free(key_file);
}

// --- Helper: Run JS Safe ---
void run_js(WebKitWebView* view, const std::string& script) {
    webkit_web_view_evaluate_javascript(view, script.c_str(), -1, NULL, NULL, NULL, NULL, NULL);
}

// --- JS Communication Handler ---
static void on_script_message(WebKitUserContentManager* manager, WebKitJavascriptResult* res, gpointer user_data) {
    JSCValue* value = webkit_javascript_result_get_js_value(res);
    char* json_str = jsc_value_to_string(value);
    std::string json(json_str);
    g_free(json_str);
    
    if (json.find("search") != std::string::npos) {
        size_t q_pos = json.find("query\":\"");
        if (q_pos != std::string::npos) {
            std::string query = json.substr(q_pos + 8);
            query = query.substr(0, query.find("\""));
            WebKitWebView* view = WEBKIT_WEB_VIEW(user_data);
            std::string url = settings.search_engine + query;
            webkit_web_view_load_uri(view, url.c_str());
        }
    } 
    else if (json.find("save_settings") != std::string::npos) {
        std::string engine = "https://www.google.com/search?q=";
        if (json.find("duckduckgo") != std::string::npos) engine = "https://duckduckgo.com/?q=";
        if (json.find("bing") != std::string::npos) engine = "https://www.bing.com/search?q=";
        
        std::string theme = (json.find("light") != std::string::npos) ? "light" : "dark";
        save_settings(engine, theme);
    }
    else if (json.find("get_theme") != std::string::npos) {
        WebKitWebView* view = WEBKIT_WEB_VIEW(user_data);
        run_js(view, "setTheme('" + settings.theme + "');");
    }
}

// --- UI Helpers ---
GtkNotebook* get_notebook(GtkWidget* win) {
    return GTK_NOTEBOOK(g_object_get_data(G_OBJECT(win), "notebook"));
}

GtkEntry* get_url_bar(GtkWidget* win) {
    return GTK_ENTRY(g_object_get_data(G_OBJECT(win), "url_bar"));
}

WebKitWebView* get_active_webview(GtkWidget* win) {
    GtkNotebook* nb = get_notebook(win);
    int page = gtk_notebook_get_current_page(nb);
    if (page == -1) return nullptr;
    return WEBKIT_WEB_VIEW(gtk_notebook_get_nth_page(nb, page));
}

// --- Widget Updater ---
static gboolean update_home_stats(gpointer data) {
    GtkNotebook* notebook = get_notebook(GTK_WIDGET(data));
    if (!notebook) return TRUE;

    int pages = gtk_notebook_get_n_pages(notebook);
    for (int i=0; i<pages; i++) {
        WebKitWebView* view = WEBKIT_WEB_VIEW(gtk_notebook_get_nth_page(notebook, i));
        const char* uri = webkit_web_view_get_uri(view);
        
        if (uri && std::string(uri).find("home.html") != std::string::npos) {
            int cpu; std::string ram;
            get_sys_stats(cpu, ram);
            std::string script = "updateStats('" + std::to_string(cpu) + "', '" + ram + "');";
            run_js(view, script);
        }
    }
    return TRUE; 
}

// --- Tab Logic ---
static void on_tab_close(GtkButton*, gpointer v) { 
    GtkWidget* view = GTK_WIDGET(v);
    GtkWidget* win = GTK_WIDGET(g_object_get_data(G_OBJECT(view), "win"));
    gtk_notebook_remove_page(get_notebook(win), gtk_notebook_page_num(get_notebook(win), view));
}

GtkWidget* create_new_tab(GtkWidget* win, const std::string& url, WebKitWebContext* context) {
    GtkNotebook* notebook = get_notebook(win);
    
    WebKitUserContentManager* ucm = webkit_user_content_manager_new();
    webkit_user_content_manager_register_script_message_handler(ucm, "zyro");
    
    GtkWidget* view = GTK_WIDGET(g_object_new(WEBKIT_TYPE_WEB_VIEW,
        "web-context", context,
        "user-content-manager", ucm,
        NULL));
        
    g_object_set_data(G_OBJECT(view), "win", win);

    g_signal_connect(ucm, "script-message-received::zyro", G_CALLBACK(on_script_message), view);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget* label = gtk_label_new("New Tab");
    GtkWidget* close = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
    gtk_widget_set_name(close, "tab-close-btn");
    gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), close, FALSE, FALSE, 0);
    gtk_widget_show_all(box);

    int page = gtk_notebook_append_page(notebook, view, box);
    gtk_notebook_set_tab_reorderable(notebook, view, TRUE);
    gtk_widget_show(view);
    
    g_signal_connect(close, "clicked", G_CALLBACK(on_tab_close), view);
    
    g_signal_connect(view, "notify::title", G_CALLBACK(+[](WebKitWebView* v, GParamSpec*, GtkLabel* l){ 
        const char* t = webkit_web_view_get_title(v); if(t) gtk_label_set_text(l, t); 
    }), label);
    
    g_signal_connect(view, "load-changed", G_CALLBACK(+[](WebKitWebView* v, WebKitLoadEvent e, gpointer w){
        if(e == WEBKIT_LOAD_COMMITTED) {
             const char* u = webkit_web_view_get_uri(v);
             if(u && std::string(u).find("file://") == std::string::npos) 
                gtk_entry_set_text(get_url_bar(GTK_WIDGET(w)), u);
             else gtk_entry_set_text(get_url_bar(GTK_WIDGET(w)), ""); 
        }
    }), win);

    gtk_notebook_set_current_page(notebook, page);
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(view), url.c_str());
    return view;
}

static void on_url_activate(GtkEntry* e, gpointer win) {
    WebKitWebView* v = get_active_webview(GTK_WIDGET(win));
    if (!v) return;
    std::string t = gtk_entry_get_text(e);
    if(t.find("://") == std::string::npos && t.find(".") == std::string::npos) t = settings.search_engine + t;
    else if(t.find("://") == std::string::npos) t = "https://" + t;
    webkit_web_view_load_uri(v, t.c_str());
    gtk_widget_grab_focus(GTK_WIDGET(v));
}

void create_window(WebKitWebContext* ctx) {
    GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    global_window = win;
    gtk_window_set_default_size(GTK_WINDOW(win), 1200, 800);
    gtk_window_set_title(GTK_WINDOW(win), "Zyro");
    
    std::string style_path = get_assets_path() + "style.css";
    LOG("Loading CSS from: " + style_path);
    
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_path(provider, style_path.c_str(), NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_style_context_add_class(gtk_widget_get_style_context(toolbar), "toolbar");
    
    GtkWidget* nb = gtk_notebook_new();
    gtk_notebook_set_show_border(GTK_NOTEBOOK(nb), FALSE);
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(nb), TRUE);
    g_object_set_data(G_OBJECT(win), "notebook", nb);

    auto mkbtn = [](const char* icon, const char* tooltip) { 
        GtkWidget* b = gtk_button_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON); 
        gtk_widget_set_tooltip_text(b, tooltip);
        return b; 
    };
    
    GtkWidget* b_back = mkbtn("go-previous-symbolic", "Back");
    GtkWidget* b_fwd = mkbtn("go-next-symbolic", "Forward");
    GtkWidget* b_refresh = mkbtn("view-refresh-symbolic", "Reload");
    GtkWidget* b_home = mkbtn("go-home-symbolic", "Home");
    GtkWidget* url = gtk_entry_new();
    g_object_set_data(G_OBJECT(win), "url_bar", url);
    GtkWidget* b_add = mkbtn("tab-new-symbolic", "New Tab");
    GtkWidget* b_sett = mkbtn("emblem-system-symbolic", "Settings");

    gtk_box_pack_start(GTK_BOX(toolbar), b_back, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), b_fwd, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), b_refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), b_home, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), url, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(toolbar), b_add, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), b_sett, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), nb, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(win), box);

    g_signal_connect(b_back, "clicked", G_CALLBACK(+[](GtkButton*, gpointer w){ 
        WebKitWebView* v = get_active_webview(GTK_WIDGET(w)); if(v) webkit_web_view_go_back(v); 
    }), win);
    
    g_signal_connect(b_fwd, "clicked", G_CALLBACK(+[](GtkButton*, gpointer w){ 
        WebKitWebView* v = get_active_webview(GTK_WIDGET(w)); if(v) webkit_web_view_go_forward(v); 
    }), win);
    
    g_signal_connect(b_refresh, "clicked", G_CALLBACK(+[](GtkButton*, gpointer w){ 
        WebKitWebView* v = get_active_webview(GTK_WIDGET(w)); if(v) webkit_web_view_reload(v); 
    }), win);
    
    g_signal_connect(b_home, "clicked", G_CALLBACK(+[](GtkButton*, gpointer w){ 
        WebKitWebView* v = get_active_webview(GTK_WIDGET(w)); if(v) webkit_web_view_load_uri(v, settings.home_url.c_str()); 
    }), win);
    
    g_signal_connect(b_sett, "clicked", G_CALLBACK(+[](GtkButton*, gpointer w){ 
        create_new_tab(GTK_WIDGET(w), settings.settings_url, global_context); 
    }), win);

    g_signal_connect(b_add, "clicked", G_CALLBACK(+[](GtkButton*, gpointer w){ create_new_tab(GTK_WIDGET(w), settings.home_url, global_context); }), win);
    g_signal_connect(url, "activate", G_CALLBACK(on_url_activate), win);

    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_show_all(win);

    create_new_tab(win, settings.home_url, ctx);
    
    g_timeout_add(1000, update_home_stats, win);
}

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    load_settings();
    
    // We can use the current dir for data/cache, 
    // or properly use standard paths. For simplicity, we keep them local to execution.
    char* cwd = g_get_current_dir();
    std::string current(cwd);
    g_free(cwd);
    
    std::string cache = current + "/cache";
    std::string data = current + "/data";
    g_mkdir_with_parents(cache.c_str(), 0755);
    g_mkdir_with_parents(data.c_str(), 0755);

    WebKitWebsiteDataManager* mgr = webkit_website_data_manager_new("base-cache-directory", cache.c_str(), "base-data-directory", data.c_str(), NULL);
    global_context = webkit_web_context_new_with_website_data_manager(mgr);
    webkit_cookie_manager_set_persistent_storage(webkit_web_context_get_cookie_manager(global_context), (data+"/cookies.sqlite").c_str(), WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);

    create_window(global_context);
    gtk_main();
    return 0;
}