#include "Globals.h"

AppSettings settings;
SecurityContext global_security;
WebKitWebContext* global_context = nullptr;
GtkWidget* global_window = nullptr;
SoupSession* soup_session = nullptr;

std::vector<HistoryItem> browsing_history;
std::vector<std::string> search_history;
std::vector<DownloadItem> downloads_list;
std::vector<PassItem> saved_passwords;
std::vector<BookmarkItem> bookmarks_list;
std::vector<ShortcutItem> shortcuts_list;

guint64 global_download_id_counter = 1;

GtkWidget* global_downloads_popover = nullptr;
GtkWidget* global_downloads_list_box = nullptr;
GtkWidget* global_downloads_btn = nullptr;

bool global_blocker_enabled = true; // Default to ON
WebKitUserContentFilter* global_filter = nullptr;