#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <string>
#include <iostream>
#include <fstream>
#include <vector>
#include <sstream>
#include <memory>
#include <regex>
#include <algorithm>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/sysinfo.h>
#endif

// --- Debug & Globals ---
#define LOG(msg) std::cout << "[DEBUG] " << msg << std::endl

// --- Data Structures ---
struct AppSettings {
    std::string home_url = "file://"; 
    std::string settings_url = "file://";
    std::string history_url = "file://";
    std::string search_engine = "https://www.google.com/search?q=";
    std::string suggestion_api = "http://suggestqueries.google.com/complete/search?client=firefox&q=";
    std::string theme = "dark";
} settings;

struct HistoryItem {
    std::string title;
    std::string url;
    std::string time_str;
};

// --- Global State ---
WebKitWebContext* global_context = nullptr; 
GtkWidget* global_window = nullptr;
SoupSession* soup_session = nullptr; 

// In-memory cache for fast access
std::vector<HistoryItem> browsing_history;
std::vector<std::string> search_history;

// --- Forward Declarations ---
void create_window(WebKitWebContext* ctx);
GtkNotebook* get_notebook(GtkWidget* win);
void run_js(WebKitWebView* view, const std::string& script); 
GtkWidget* create_new_tab(GtkWidget* win, const std::string& url, WebKitWebContext* context);
WebKitWebView* get_active_webview(GtkWidget* win);

// --- Path & Time Helpers ---
std::string get_assets_path() {
    char* cwd = g_get_current_dir();
    std::string current(cwd);
    g_free(cwd);

    std::string local_assets = current + "/assets/";
    if (g_file_test(local_assets.c_str(), G_FILE_TEST_IS_DIR)) {
        return local_assets;
    }

    // Check parent directory (e.g. if running from build/)
    size_t last_slash = current.find_last_of('/');
    if (last_slash != std::string::npos) {
        std::string parent = current.substr(0, last_slash);
        std::string parent_assets = parent + "/assets/";
        if (g_file_test(parent_assets.c_str(), G_FILE_TEST_IS_DIR)) {
            return parent_assets;
        }
    }
    return local_assets;
}

std::string get_user_data_dir() {
    const char* home = g_get_home_dir();
    std::string dir = std::string(home) + "/.config/zyro/";
    // Ensure directory exists
    if (g_mkdir_with_parents(dir.c_str(), 0755) == -1) {
        // Handle error if needed
    }
    return dir;
}

std::string get_current_time_str() {
    time_t now = time(0);
    struct tm tstruct;
    char buf[80];
    tstruct = *localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tstruct);
    return std::string(buf);
}

// --- File I/O for History & Settings ---

void save_history_to_disk() {
    std::ofstream f(get_user_data_dir() + "history.txt");
    for (const auto& i : browsing_history) {
        f << i.url << "|" << i.title << "|" << i.time_str << "\n";
    }
}

void save_searches_to_disk() {
    std::ofstream f(get_user_data_dir() + "searches.txt");
    for (const auto& s : search_history) {
        f << s << "\n";
    }
}

void load_data() {
    std::string conf_dir = get_user_data_dir();
    
    // 1. Load Settings
    GKeyFile* key_file = g_key_file_new();
    if (g_key_file_load_from_file(key_file, (conf_dir + "settings.ini").c_str(), G_KEY_FILE_NONE, NULL)) {
        gchar* engine = g_key_file_get_string(key_file, "General", "search_engine", NULL);
        if (engine) { settings.search_engine = engine; g_free(engine); }
        
        gchar* theme = g_key_file_get_string(key_file, "General", "theme", NULL);
        if (theme) { settings.theme = theme; g_free(theme); }
    }
    g_key_file_free(key_file);

    // 2. Load Search History
    std::ifstream s_file(conf_dir + "searches.txt");
    std::string line;
    while (std::getline(s_file, line)) {
        if (!line.empty()) search_history.push_back(line);
    }

    // 3. Load Browsing History
    std::ifstream h_file(conf_dir + "history.txt");
    while (std::getline(h_file, line)) {
        size_t p1 = line.find('|');
        size_t p2 = line.find_last_of('|');
        if (p1 != std::string::npos && p2 != std::string::npos && p1 != p2) {
            browsing_history.push_back({
                line.substr(p1 + 1, p2 - p1 - 1), // Title
                line.substr(0, p1),               // URL
                line.substr(p2 + 1)               // Time
            });
        }
    }

    // 4. Setup Paths
    if(!soup_session) soup_session = soup_session_new();
    std::string base = get_assets_path();
    settings.home_url = "file://" + base + "home.html";
    settings.settings_url = "file://" + base + "settings.html";
    settings.history_url = "file://" + base + "history.html";
    
    LOG("Data Loaded. History items: " << browsing_history.size());
}

void save_settings(const std::string& engine, const std::string& theme) {
    settings.search_engine = engine;
    settings.theme = theme;
    
    GKeyFile* key_file = g_key_file_new();
    g_key_file_set_string(key_file, "General", "search_engine", settings.search_engine.c_str());
    g_key_file_set_string(key_file, "General", "theme", settings.theme.c_str());
    
    std::string path = get_user_data_dir() + "settings.ini";
    g_key_file_save_to_file(key_file, path.c_str(), NULL);
    g_key_file_free(key_file);
}

void add_history_item(const std::string& url, const std::string& title) {
    if (url.find("file://") == 0 || url.empty()) return; 
    
    auto it = std::remove_if(browsing_history.begin(), browsing_history.end(), 
        [&](const HistoryItem& i){ return i.url == url; });
    browsing_history.erase(it, browsing_history.end());

    HistoryItem item = { title.empty() ? url : title, url, get_current_time_str() };
    browsing_history.push_back(item);
    save_history_to_disk();
}

void add_search_query(const std::string& query) {
    if (query.length() < 2) return;
    auto it = std::remove(search_history.begin(), search_history.end(), query);
    search_history.erase(it, search_history.end());
    
    search_history.push_back(query);
    save_searches_to_disk();
}

void clear_all_history() {
    browsing_history.clear();
    search_history.clear();
    save_history_to_disk();
    save_searches_to_disk();
}

// --- Suggestion Logic ---

struct SuggestionRequest {
    bool is_gtk;
    gpointer target;
    std::string query;
};

std::vector<std::string> parse_remote_suggestions(const std::string& json) {
    std::vector<std::string> results;
    try {
        size_t start_arr = json.find('[', 1); 
        size_t end_arr = json.find(']', start_arr);
        if (start_arr == std::string::npos || end_arr == std::string::npos) return results;

        std::string list_content = json.substr(start_arr + 1, end_arr - start_arr - 1);
        std::regex re("\"([^\"]+)\"");
        auto begin = std::sregex_iterator(list_content.begin(), list_content.end(), re);
        auto end = std::sregex_iterator();

        for (auto i = begin; i != end; ++i) {
            results.push_back((*i)[1].str());
        }
    } catch (...) {}
    return results;
}

std::vector<std::string> get_combined_suggestions(const std::string& query, const std::string& remote_json) {
    std::vector<std::string> combined;
    std::string q_lower = query;
    std::transform(q_lower.begin(), q_lower.end(), q_lower.begin(), ::tolower);

    // 1. Check Search History
    int count = 0;
    for (auto it = search_history.rbegin(); it != search_history.rend(); ++it) {
        if (count > 2) break; 
        std::string s_lower = *it;
        std::transform(s_lower.begin(), s_lower.end(), s_lower.begin(), ::tolower);
        
        if (s_lower.find(q_lower) == 0) { 
            combined.push_back("[H] " + *it); 
            count++;
        }
    }

    // 2. Parse and Append Remote
    if (!remote_json.empty()) {
        std::vector<std::string> remote = parse_remote_suggestions(remote_json);
        for (const auto& r : remote) {
            bool exists = false;
            for(const auto& c : combined) {
                if(c.find(r) != std::string::npos) exists = true;
            }
            if(!exists) combined.push_back(r);
        }
    }
    return combined;
}

void on_suggestion_ready(SoupSession* session, SoupMessage* msg, gpointer user_data) {
    SuggestionRequest* req = (SuggestionRequest*)user_data;
    
    std::string body = "";
    if (msg->status_code == 200) {
        body = msg->response_body->data;
    }
    
    std::vector<std::string> final_list = get_combined_suggestions(req->query, body);

    if (req->is_gtk) {
        GtkEntryCompletion* completion = GTK_ENTRY_COMPLETION(req->target);
        GtkListStore* store = GTK_LIST_STORE(gtk_entry_completion_get_model(completion));
        
        gtk_list_store_clear(store);
        GtkTreeIter iter;
        for (const auto& s : final_list) {
            std::string text = s;
            if(text.find("[H] ") == 0) text = text.substr(4);
            
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter, 0, text.c_str(), -1);
        }
        gtk_entry_completion_complete(completion);
    } else {
        std::stringstream js_array;
        js_array << "[";
        for (size_t i = 0; i < final_list.size(); ++i) {
            js_array << "'" << final_list[i] << "'";
            if (i < final_list.size() - 1) js_array << ",";
        }
        js_array << "]";

        std::string script = "renderSuggestions(" + js_array.str() + ");";
        run_js(WEBKIT_WEB_VIEW(req->target), script);
    }
    delete req;
}

void fetch_suggestions(const std::string& query, bool is_gtk, gpointer target) {
    if (query.length() < 2) return;
    
    SuggestionRequest* req = new SuggestionRequest{ is_gtk, target, query };
    std::string url = settings.suggestion_api + query;
    SoupMessage* msg = soup_message_new("GET", url.c_str());
    soup_session_queue_message(soup_session, msg, on_suggestion_ready, req);
}

// --- System Stats Helper ---
void get_sys_stats(int& cpu_usage, std::string& ram_usage) {
#ifdef _WIN32
    ram_usage = "N/A";
    cpu_usage = 0; 
#else
    struct sysinfo memInfo;
    sysinfo(&memInfo);
    long long physMemUsed = (memInfo.totalram - memInfo.freeram) * memInfo.mem_unit;
    long long totalPhysMem = memInfo.totalram * memInfo.mem_unit;
    
    std::stringstream ss;
    ss.precision(1);
    ss << std::fixed << (double)physMemUsed / (1024*1024*1024) << " / " << (double)totalPhysMem / (1024*1024*1024) << " GB";
    ram_usage = ss.str();

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
    lastTotalUser = totalUser; lastTotalUserLow = totalUserLow; lastTotalSys = totalSys; lastTotalIdle = totalIdle;
#endif
}

// --- IPC: C++ <-> JS Communication ---
void run_js(WebKitWebView* view, const std::string& script) {
    webkit_web_view_evaluate_javascript(view, script.c_str(), -1, NULL, NULL, NULL, NULL, NULL);
}

static void on_script_message(WebKitUserContentManager* manager, WebKitJavascriptResult* res, gpointer user_data) {
    JSCValue* value = webkit_javascript_result_get_js_value(res);
    char* json_str = jsc_value_to_string(value);
    std::string json(json_str);
    g_free(json_str);

    auto get_json_val = [&](std::string key) -> std::string {
        std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
        std::smatch match;
        if (std::regex_search(json, match, re)) {
            return match[1].str();
        }
        return "";
    };

    std::string type = get_json_val("type");
    WebKitWebView* view = WEBKIT_WEB_VIEW(user_data);

    if (type == "search") {
        std::string query = get_json_val("query");
        add_search_query(query); 
        std::string url = settings.search_engine + query;
        webkit_web_view_load_uri(view, url.c_str());
    }
    else if (type == "get_suggestions") {
        std::string query = get_json_val("query");
        fetch_suggestions(query, false, view);
    }
    else if (type == "save_settings") {
        std::string engine = get_json_val("engine");
        std::string theme = get_json_val("theme");
        
        if (engine.empty()) engine = settings.search_engine;
        if (theme.empty()) theme = settings.theme;

        save_settings(engine, theme);
        
        // Update all tabs immediately
        if(global_window) {
             GtkNotebook* nb = get_notebook(global_window);
             int pages = gtk_notebook_get_n_pages(nb);
             for(int i=0; i<pages; i++) {
                 // The page is now a VBox [Toolbar, WebView]
                 GtkWidget* page_box = gtk_notebook_get_nth_page(nb, i);
                 GList* children = gtk_container_get_children(GTK_CONTAINER(page_box));
                 if(children && children->next) {
                    WebKitWebView* v = WEBKIT_WEB_VIEW(children->next->data);
                    run_js(v, "setTheme('" + theme + "');");
                 }
                 g_list_free(children);
             }
        }
    }
    else if (type == "get_theme") {
        run_js(view, "setTheme('" + settings.theme + "');");
    }
    else if (type == "get_settings") {
        std::string script = "loadCurrentSettings('" + settings.search_engine + "', '" + settings.theme + "');";
        run_js(view, script);
    }
    else if (type == "clear_cache") {
        WebKitWebsiteDataManager* mgr = webkit_web_context_get_website_data_manager(global_context);
        webkit_website_data_manager_clear(mgr, WEBKIT_WEBSITE_DATA_ALL, 0, NULL, NULL, NULL);
        run_js(view, "showToast('Cache Cleared');"); 
    }
    else if (type == "open_url") {
        std::string url = get_json_val("url");
        webkit_web_view_load_uri(view, url.c_str());
    }
    else if (type == "get_history") {
        std::stringstream ss;
        ss << "[";
        for(size_t i=0; i<browsing_history.size(); ++i) {
            const auto& h = browsing_history[i];
            ss << "{ \"title\": \"" << h.title << "\", \"url\": \"" << h.url << "\", \"time\": \"" << h.time_str << "\" }";
            if(i < browsing_history.size()-1) ss << ",";
        }
        ss << "]";
        std::string script = "renderHistory('" + ss.str() + "');";
        run_js(view, script);
    }
    else if (type == "clear_history") {
        clear_all_history();
        run_js(view, "renderHistory('[]');");
    }
}

// --- UI Helpers ---
GtkNotebook* get_notebook(GtkWidget* win) {
    return GTK_NOTEBOOK(g_object_get_data(G_OBJECT(win), "notebook"));
}

WebKitWebView* get_active_webview(GtkWidget* win) {
    GtkNotebook* nb = get_notebook(win);
    int page_num = gtk_notebook_get_current_page(nb);
    if (page_num == -1) return nullptr;
    
    // Page is now VBox -> [Toolbar, WebView]
    GtkWidget* page = gtk_notebook_get_nth_page(nb, page_num);
    GList* children = gtk_container_get_children(GTK_CONTAINER(page));
    
    // The webview is the second item (index 1) in the VBox
    WebKitWebView* view = nullptr;
    if (children && children->next) {
        view = WEBKIT_WEB_VIEW(children->next->data);
    }
    g_list_free(children);
    return view;
}

// --- Forward declaration for Tab Logic ---
void on_incognito_clicked(GtkButton*, gpointer);

// --- Toolbar Events (Per Tab) ---

static void on_url_changed(GtkEditable* editable, gpointer user_data) {
    std::string text = gtk_entry_get_text(GTK_ENTRY(editable));
    if (text.find("://") != std::string::npos || text.find(".") != std::string::npos) return;
    
    GtkEntryCompletion* completion = gtk_entry_get_completion(GTK_ENTRY(editable));
    fetch_suggestions(text, true, completion);
}

static void on_url_activate(GtkEntry* e, gpointer view_ptr) {
    WebKitWebView* v = WEBKIT_WEB_VIEW(view_ptr);
    if (!v) return;
    std::string t = gtk_entry_get_text(e);
    
    if(t.find("://") == std::string::npos && t.find(".") == std::string::npos) {
        add_search_query(t);
        t = settings.search_engine + t;
    } 
    else if(t.find("://") == std::string::npos) {
        t = "https://" + t;
    }
    webkit_web_view_load_uri(v, t.c_str());
    gtk_widget_grab_focus(GTK_WIDGET(v));
}

static gboolean on_match_selected(GtkEntryCompletion* widget, GtkTreeModel* model, GtkTreeIter* iter, gpointer entry) {
    gchar* value;
    gtk_tree_model_get(model, iter, 0, &value, -1);
    gtk_entry_set_text(GTK_ENTRY(entry), value);
    g_signal_emit_by_name(entry, "activate"); // Trigger load
    g_free(value);
    return TRUE;
}

// --- Tab Logic ---

// Hook to capture URL visits and update URL bar
void on_load_changed(WebKitWebView* web_view, WebKitLoadEvent load_event, gpointer user_data) {
    if (load_event == WEBKIT_LOAD_COMMITTED) {
        const char* uri = webkit_web_view_get_uri(web_view);
        // We stored the pointer to the entry in the webview's data
        GtkEntry* entry = GTK_ENTRY(g_object_get_data(G_OBJECT(web_view), "entry"));

        if (uri && entry) {
            std::string u(uri);
            if(u.find("file://") == std::string::npos) {
                gtk_entry_set_text(entry, uri);
                const char* title = webkit_web_view_get_title(web_view);
                add_history_item(u, title ? std::string(title) : "");
            } else {
                gtk_entry_set_text(entry, ""); // Clear for internal pages
            }
        }
    }
}

static void on_tab_close(GtkButton*, gpointer v_box_widget) { 
    // v_box_widget is the main content box of the tab
    GtkWidget* win = GTK_WIDGET(g_object_get_data(G_OBJECT(v_box_widget), "win"));
    GtkNotebook* nb = get_notebook(win);
    int page_num = gtk_notebook_page_num(nb, GTK_WIDGET(v_box_widget));
    if (page_num != -1) {
        gtk_notebook_remove_page(nb, page_num);
    }
}

// --- Menu Helper ---
void show_menu(GtkButton* btn, gpointer win) {
    GtkWidget* popover = gtk_popover_new(GTK_WIDGET(btn));
    gtk_style_context_add_class(gtk_widget_get_style_context(popover), "menu-popover");
    
    GtkWidget* menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    
    auto mk_menu_item = [&](const char* label, const char* icon_name, GCallback cb) {
        GtkWidget* btn = gtk_button_new();
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "menu-item");
        gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
        
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        GtkWidget* img = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_MENU);
        GtkWidget* lbl = gtk_label_new(label);
        
        gtk_box_pack_start(GTK_BOX(box), img, FALSE, FALSE, 10);
        gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(btn), box);
        
        if (cb) g_signal_connect(btn, "clicked", cb, win);
        return btn;
    };

    auto mk_sep = [&]() {
        GtkWidget* s = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_widget_set_name(s, "menu-separator");
        return s;
    };

    gtk_box_pack_start(GTK_BOX(menu_box), mk_menu_item("New Tab", "tab-new-symbolic", G_CALLBACK(+[](GtkButton*, gpointer w){ 
        create_new_tab(GTK_WIDGET(w), settings.home_url, global_context); 
    })), FALSE, FALSE, 0);
    
    // Note: Incognito needs a forward declaration or move, implemented below
    // gtk_box_pack_start(GTK_BOX(menu_box), mk_menu_item("New Incognito", "user-trash-symbolic", ...), ...);

    gtk_box_pack_start(GTK_BOX(menu_box), mk_sep(), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(menu_box), mk_menu_item("History", "document-open-recent-symbolic", G_CALLBACK(+[](GtkButton*, gpointer w){
        create_new_tab(GTK_WIDGET(w), settings.history_url, global_context); 
    })), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(menu_box), mk_menu_item("Settings", "preferences-system-symbolic", G_CALLBACK(+[](GtkButton*, gpointer w){
        create_new_tab(GTK_WIDGET(w), settings.settings_url, global_context); 
    })), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(menu_box), mk_sep(), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(menu_box), mk_menu_item("Exit Zyro", "application-exit-symbolic", G_CALLBACK(gtk_main_quit)), FALSE, FALSE, 0);

    gtk_widget_show_all(menu_box);
    gtk_container_add(GTK_CONTAINER(popover), menu_box);
    gtk_popover_popup(GTK_POPOVER(popover));
}

GtkWidget* create_new_tab(GtkWidget* win, const std::string& url, WebKitWebContext* context) {
    GtkNotebook* notebook = get_notebook(win);
    
    // 1. Create Web View
    WebKitUserContentManager* ucm = webkit_user_content_manager_new();
    webkit_user_content_manager_register_script_message_handler(ucm, "zyro");
    
    GtkWidget* view = GTK_WIDGET(g_object_new(WEBKIT_TYPE_WEB_VIEW, "web-context", context, "user-content-manager", ucm, NULL));
    g_signal_connect(ucm, "script-message-received::zyro", G_CALLBACK(on_script_message), view);

    // 2. Create Toolbar (Now Local to the Tab)
    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_style_context_add_class(gtk_widget_get_style_context(toolbar), "toolbar");

    auto mkbtn = [](const char* icon, const char* tooltip) { 
        GtkWidget* b = gtk_button_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON); 
        gtk_widget_set_tooltip_text(b, tooltip); return b; 
    };
    
    GtkWidget* b_back = mkbtn("go-previous-symbolic", "Back");
    GtkWidget* b_fwd = mkbtn("go-next-symbolic", "Forward");
    GtkWidget* b_refresh = mkbtn("view-refresh-symbolic", "Reload");
    GtkWidget* b_home = mkbtn("go-home-symbolic", "Home");
    GtkWidget* b_menu = mkbtn("open-menu-symbolic", "Menu"); // Menu is now per-tab

    // URL Entry
    GtkWidget* url_entry = gtk_entry_new();
    GtkEntryCompletion* completion = gtk_entry_completion_new();
    GtkListStore* store = gtk_list_store_new(1, G_TYPE_STRING);
    gtk_entry_completion_set_model(completion, GTK_TREE_MODEL(store));
    gtk_entry_completion_set_text_column(completion, 0);
    gtk_entry_set_completion(GTK_ENTRY(url_entry), completion);

    // Pack Toolbar
    gtk_box_pack_start(GTK_BOX(toolbar), b_back, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), b_fwd, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), b_refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), b_home, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), url_entry, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(toolbar), b_menu, FALSE, FALSE, 0);

    // 3. Create Container (Vertical Box: Toolbar + WebView)
    GtkWidget* page_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    g_object_set_data(G_OBJECT(page_box), "win", win); // Link back to window
    
    gtk_box_pack_start(GTK_BOX(page_box), toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page_box), view, TRUE, TRUE, 0);

    // 4. Create Tab Header (Label + Close Button)
    GtkWidget* tab_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget* label = gtk_label_new("New Tab");
    GtkWidget* close = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
    gtk_widget_set_name(close, "tab-close-btn");
    
    gtk_box_pack_start(GTK_BOX(tab_header), label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(tab_header), close, FALSE, FALSE, 0);
    gtk_widget_show_all(tab_header);

    // 5. Connect Signals
    
    // Web Navigation
    g_signal_connect(b_back, "clicked", G_CALLBACK(+[](GtkButton*, gpointer v){ webkit_web_view_go_back(WEBKIT_WEB_VIEW(v)); }), view);
    g_signal_connect(b_fwd, "clicked", G_CALLBACK(+[](GtkButton*, gpointer v){ webkit_web_view_go_forward(WEBKIT_WEB_VIEW(v)); }), view);
    g_signal_connect(b_refresh, "clicked", G_CALLBACK(+[](GtkButton*, gpointer v){ webkit_web_view_reload(WEBKIT_WEB_VIEW(v)); }), view);
    g_signal_connect(b_home, "clicked", G_CALLBACK(+[](GtkButton*, gpointer v){ webkit_web_view_load_uri(WEBKIT_WEB_VIEW(v), settings.home_url.c_str()); }), view);
    
    // URL Bar
    g_object_set_data(G_OBJECT(view), "entry", url_entry); // Save entry pointer in view
    g_signal_connect(url_entry, "changed", G_CALLBACK(on_url_changed), NULL);
    g_signal_connect(url_entry, "activate", G_CALLBACK(on_url_activate), view);
    g_signal_connect(completion, "match-selected", G_CALLBACK(on_match_selected), url_entry);
    
    // Load Events (Update URL bar & History)
    g_signal_connect(view, "load-changed", G_CALLBACK(on_load_changed), NULL);
    
    // Title Update
    g_signal_connect(view, "notify::title", G_CALLBACK(+[](WebKitWebView* v, GParamSpec*, GtkLabel* l){ 
        const char* t = webkit_web_view_get_title(v); if(t) gtk_label_set_text(l, t); 
    }), label);

    // Menu
    g_signal_connect(b_menu, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer w){ show_menu(b, w); }), win);

    // Close Tab
    g_signal_connect(close, "clicked", G_CALLBACK(on_tab_close), page_box);

    // Add to Notebook
    int page = gtk_notebook_append_page(notebook, page_box, tab_header);
    gtk_notebook_set_tab_reorderable(notebook, page_box, TRUE);
    gtk_widget_show_all(page_box);
    
    gtk_notebook_set_current_page(notebook, page);
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(view), url.c_str());
    
    return view;
}

void on_incognito_clicked(GtkButton*, gpointer) {
    WebKitWebContext* ephemeral_ctx = webkit_web_context_new_ephemeral();
    create_window(ephemeral_ctx);
}

// --- Key Press Handler (Hotkeys) ---
gboolean on_key_press(GtkWidget* widget, GdkEventKey* event, gpointer user_data) {
    // Check for Control Key mask
    if (event->state & GDK_CONTROL_MASK) {
        if (event->keyval == GDK_KEY_t) {
            // Ctrl + T : New Tab
            create_new_tab(widget, settings.home_url, global_context);
            return TRUE; // Event handled
        }
        if (event->keyval == GDK_KEY_w) {
            // Ctrl + W : Close Current Tab
            GtkNotebook* nb = get_notebook(widget);
            int page = gtk_notebook_get_current_page(nb);
            if (page != -1) {
                gtk_notebook_remove_page(nb, page);
                // If no tabs left, close window? Optional.
                if (gtk_notebook_get_n_pages(nb) == 0) gtk_widget_destroy(widget);
            }
            return TRUE; // Event handled
        }
    }
    return FALSE; // Propagate event
}

// --- Window Creation ---

void create_window(WebKitWebContext* ctx) {
    GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    if (!webkit_web_context_is_ephemeral(ctx)) {
        global_window = win;
    }
    
    if (webkit_web_context_is_ephemeral(ctx)) {
        gtk_window_set_title(GTK_WINDOW(win), "Zyro (Incognito)");
        gtk_style_context_add_class(gtk_widget_get_style_context(win), "incognito");
    } else {
        gtk_window_set_title(GTK_WINDOW(win), "Zyro");
    }

    gtk_window_set_default_size(GTK_WINDOW(win), 1200, 800);
    
    // Register Hotkeys
    g_signal_connect(win, "key-press-event", G_CALLBACK(on_key_press), NULL);
    
    // Load CSS
    std::string style_path = get_assets_path() + "style.css";
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_path(provider, style_path.c_str(), NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    // Main Container
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    
    // Notebook (Tabs)
    GtkWidget* nb = gtk_notebook_new();
    // We remove the border to make it blend with the toolbar (which is now inside the pages)
    gtk_notebook_set_show_border(GTK_NOTEBOOK(nb), FALSE);
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(nb), TRUE);
    g_object_set_data(G_OBJECT(win), "notebook", nb);

    gtk_box_pack_start(GTK_BOX(box), nb, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(win), box);

    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    
    gtk_widget_show_all(win);
    
    // Open Initial Tab
    create_new_tab(win, settings.home_url, ctx);
}

static gboolean update_home_stats(gpointer data) {
    if(!global_window) return TRUE;
    GtkNotebook* notebook = get_notebook(global_window);
    if (!notebook) return TRUE;
    
    int pages = gtk_notebook_get_n_pages(notebook);
    for (int i=0; i<pages; i++) {
        GtkWidget* page_box = gtk_notebook_get_nth_page(notebook, i);
        GList* children = gtk_container_get_children(GTK_CONTAINER(page_box));
        
        if (children && children->next) {
            WebKitWebView* view = WEBKIT_WEB_VIEW(children->next->data);
            const char* uri = webkit_web_view_get_uri(view);
            if (uri && std::string(uri).find("home.html") != std::string::npos) {
                int cpu; std::string ram;
                get_sys_stats(cpu, ram);
                std::string script = "updateStats('" + std::to_string(cpu) + "', '" + ram + "');";
                run_js(view, script);
            }
        }
        g_list_free(children);
    }
    return TRUE; 
}

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    load_data();
    
    std::string user_dir = get_user_data_dir();
    std::string cache = user_dir + "cache";
    std::string data = user_dir + "data";

    WebKitWebsiteDataManager* mgr = webkit_website_data_manager_new("base-cache-directory", cache.c_str(), "base-data-directory", data.c_str(), NULL);
    global_context = webkit_web_context_new_with_website_data_manager(mgr);
    webkit_cookie_manager_set_persistent_storage(webkit_web_context_get_cookie_manager(global_context), (data+"/cookies.sqlite").c_str(), WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);

    create_window(global_context); 
    g_timeout_add(1000, update_home_stats, NULL);
    gtk_main();
    return 0;
}