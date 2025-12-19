#pragma once
#include <string>
#include <gtk/gtk.h>
#include "Globals.h" // For DownloadItem check

std::string get_user_data_dir();
std::string get_assets_path();
std::string get_current_time_str();
void get_sys_stats(int& cpu_usage, std::string& ram_usage);
int find_download_index(guint64 id);
std::string json_escape(const std::string& s);