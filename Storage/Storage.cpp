#include "Storage.h"
#include "Globals.h"
#include "Utils.h"
#include "Security.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>


static bool history_dirty = false;
static bool searches_dirty = false;

void save_history_to_disk() {
    std::ofstream f(get_user_data_dir() + "history.txt");
    for (const auto& i : browsing_history) f << i.url << "|" << i.title << "|" << i.time_str << "\n";
}

void save_searches_to_disk() {
    std::ofstream f(get_user_data_dir() + "searches.txt");
    for (const auto& s : search_history) f << s << "\n";
}

void save_passwords_to_disk() {
    if (!global_security.ready) return;
    std::ofstream f(get_user_data_dir() + "passwords.txt");
    for (const auto& p : saved_passwords) {
        std::string encrypted_pass = encrypt_string(p.pass);
        f << p.site << "|" << p.user << "|" << encrypted_pass << "\n";
    }
}
void save_bookmarks_to_disk() {
    std::ofstream f(get_user_data_dir() + "bookmarks.txt");
    for (const auto& b : bookmarks_list) f << b.url << "|" << b.title << "\n";
}
void save_shortcuts_to_disk() {
    std::ofstream f(get_user_data_dir() + "shortcuts.txt");
    for (const auto& s : shortcuts_list) f << s.name << "|" << s.url << "\n";
}

void save_settings(const std::string& engine, const std::string& theme) {
    settings.search_engine = engine;
    settings.theme = theme;
    
    GKeyFile* key_file = g_key_file_new();
    
    g_key_file_set_string(key_file, "General", "search_engine", settings.search_engine.c_str());
    g_key_file_set_string(key_file, "General", "theme", settings.theme.c_str());
    
    g_key_file_set_boolean(key_file, "Home", "show_cpu", settings.show_cpu);
    g_key_file_set_boolean(key_file, "Home", "show_ram", settings.show_ram);
    g_key_file_set_boolean(key_file, "Home", "show_shortcuts", settings.show_shortcuts);
    g_key_file_set_string(key_file, "Home", "bg_type", settings.bg_type.c_str());

    g_key_file_save_to_file(key_file, (get_user_data_dir() + "settings.ini").c_str(), NULL);
    g_key_file_free(key_file);
}

gboolean auto_save_data(gpointer user_data) {
    if (history_dirty) {
        save_history_to_disk();
        history_dirty = false;
        // std::cout << "Auto-saved history." << std::endl; // debugging
    }
    if (searches_dirty) {
        save_searches_to_disk();
        searches_dirty = false;
    }
    return TRUE;
}


void add_history_item(const std::string& url, const std::string& title) {
    if (url.find("file://") == 0 || url.empty()) return; 
    auto it = std::remove_if(browsing_history.begin(), browsing_history.end(), [&](const HistoryItem& i){ return i.url == url; });
    browsing_history.erase(it, browsing_history.end());
    HistoryItem item = { title.empty() ? url : title, url, get_current_time_str() };
    browsing_history.push_back(item);
    history_dirty = true;
    //save_history_to_disk();
}


void add_search_query(const std::string& query) {
    if (query.length() < 2) return;
    auto it = std::remove(search_history.begin(), search_history.end(), query);
    search_history.erase(it, search_history.end());
    search_history.push_back(query);
    searches_dirty = true;
    //save_searches_to_disk();
}

void clear_all_history() {
    browsing_history.clear(); search_history.clear();
    save_history_to_disk(); save_searches_to_disk();
}

void load_data() {
    std::string conf_dir = get_user_data_dir();
    
    GKeyFile* key_file = g_key_file_new();
    if (g_key_file_load_from_file(key_file, (conf_dir + "settings.ini").c_str(), G_KEY_FILE_NONE, NULL)) {
        gchar* engine = g_key_file_get_string(key_file, "General", "search_engine", NULL);
        if (engine) { settings.search_engine = engine; g_free(engine); }
        
        gchar* theme = g_key_file_get_string(key_file, "General", "theme", NULL);
        if (theme) { settings.theme = theme; g_free(theme); }

        if (g_key_file_has_key(key_file, "Home", "show_cpu", NULL))
            settings.show_cpu = g_key_file_get_boolean(key_file, "Home", "show_cpu", NULL);
        
        if (g_key_file_has_key(key_file, "Home", "show_ram", NULL))
            settings.show_ram = g_key_file_get_boolean(key_file, "Home", "show_ram", NULL);
            
        if (g_key_file_has_key(key_file, "Home", "show_shortcuts", NULL))
            settings.show_shortcuts = g_key_file_get_boolean(key_file, "Home", "show_shortcuts", NULL);

        gchar* bg = g_key_file_get_string(key_file, "Home", "bg_type", NULL);
        if (bg) { settings.bg_type = bg; g_free(bg); }
    }
    g_key_file_free(key_file);
    
    apply_browser_theme(settings.theme);

    std::ifstream s_file(conf_dir + "searches.txt");
    std::string line;
    while (std::getline(s_file, line)) if (!line.empty()) search_history.push_back(line);

    std::ifstream h_file(conf_dir + "history.txt");
    while (std::getline(h_file, line)) {
        size_t p1 = line.find('|'); size_t p2 = line.find_last_of('|');
        if (p1 != std::string::npos && p2 != std::string::npos && p1 != p2) {
            browsing_history.push_back({ line.substr(p1 + 1, p2 - p1 - 1), line.substr(0, p1), line.substr(p2 + 1) });
        }
    }
    
    std::ifstream p_file(conf_dir + "passwords.txt");
    while (std::getline(p_file, line)) {
        std::stringstream ss(line); std::string segment; std::vector<std::string> seglist;
        while(std::getline(ss, segment, '|')) seglist.push_back(segment);
        if(seglist.size() >= 3) {
            std::string raw_pass = decrypt_string(seglist[2]);
            if(!raw_pass.empty()) saved_passwords.push_back({ seglist[0], seglist[1], raw_pass });
        }
    }

    std::ifstream b_file(conf_dir + "bookmarks.txt");
    while (std::getline(b_file, line)) {
        size_t sep = line.find('|');
        if (sep != std::string::npos) bookmarks_list.push_back({ line.substr(sep + 1), line.substr(0, sep) });
    }

    std::ifstream shortcut_file(conf_dir + "shortcuts.txt");
    while (std::getline(shortcut_file, line)) {
        size_t sep = line.find('|');
        if (sep != std::string::npos) shortcuts_list.push_back({ line.substr(0, sep), line.substr(sep + 1) });
    }

    if(!soup_session) soup_session = soup_session_new();
    std::string base = get_assets_path();
    settings.home_url = "file://" + base + "home.html";
    settings.settings_url = "file://" + base + "settings.html";
    settings.history_url = "file://" + base + "history.html";
    settings.downloads_url = "file://" + base + "downloads.html";
    settings.passwords_url = "file://" + base + "passwords.html";
    settings.about_url = "file://" + base + "about.html";
    settings.task_manager_url = "file://" + base + "task_manager.html";
}