#pragma once
#include <string>
#include <vector>
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <libsoup/soup.h>

// --- Structs ---
struct AppSettings {
    std::string home_url = "file://"; 
    std::string settings_url = "file://";
    std::string history_url = "file://";
    std::string downloads_url = "file://";
    std::string passwords_url = "file://"; 
    std::string about_url = "file://"; 
    std::string search_engine = "https://www.google.com/search?q=";
    std::string suggestion_api = "http://suggestqueries.google.com/complete/search?client=firefox&q=";
    std::string theme = "dark";
    std::string task_manager_url;
};

struct HistoryItem {
    std::string title;
    std::string url;
    std::string time_str;
};

struct DownloadItem {
    guint64 id;
    std::string filename;
    std::string url;
    std::string status;
    double progress;
    WebKitDownload* webkit_download;
};

struct PassItem {
    std::string site;
    std::string user;
    std::string pass;
};

struct BookmarkItem {
    std::string title;
    std::string url;
};

struct ShortcutItem {
    std::string name;
    std::string url;
};

// --- Security Context ---
struct SecurityContext {
    unsigned char key[32];
    unsigned char iv[16]; 
    bool ready = false;
};

// --- Extern Globals (Declarations) ---
extern AppSettings settings;
extern SecurityContext global_security;
extern WebKitWebContext* global_context;
extern GtkWidget* global_window;
extern SoupSession* soup_session;

extern std::vector<HistoryItem> browsing_history;
extern std::vector<std::string> search_history;
extern std::vector<DownloadItem> downloads_list;
extern std::vector<PassItem> saved_passwords;
extern std::vector<BookmarkItem> bookmarks_list;
extern std::vector<ShortcutItem> shortcuts_list;

extern guint64 global_download_id_counter;

// Global UI pointers for Downloads
extern GtkWidget* global_downloads_popover;
extern GtkWidget* global_downloads_list_box;
extern GtkWidget* global_downloads_btn;


extern bool global_blocker_enabled; 
extern WebKitUserContentFilter* global_filter;