#pragma once
#include <string>
#include <gtk/gtk.h>

void load_data();
void save_history_to_disk();
void save_searches_to_disk();
void save_passwords_to_disk();
void save_bookmarks_to_disk();
void save_shortcuts_to_disk();
void save_settings(const std::string& engine, const std::string& theme);
void add_history_item(const std::string& url, const std::string& title);
void add_search_query(const std::string& query);
void clear_all_history();
gboolean auto_save_data(gpointer user_data);