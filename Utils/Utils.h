#pragma once
#include <string>
#include <algorithm>
#include <gtk/gtk.h>
#include "Globals.h"

std::string get_self_path();

std::string get_user_data_dir();
std::string get_assets_path();
std::string get_current_time_str();
void get_sys_stats(int& cpu_usage, std::string& ram_usage);
std::string get_total_memory_str();
int find_download_index(guint64 id);
std::string json_escape(const std::string& s);
std::string get_process_name(int pid);
std::vector<int> get_child_pids();
long get_pid_rss_kb(int pid);

void apply_browser_theme(const std::string& theme_name);