#include "Utils.h"
#include "Globals.h" 
#include <iostream>
#include <ctime>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <map>
#include <set>
#include <numeric>
#include <vector>
#include <algorithm>

#ifdef _WIN32
    #include <windows.h>
    #include <psapi.h>
#else
    #include <unistd.h>
    #include <sys/sysinfo.h>
    #include <dirent.h>
    #include <sys/resource.h>
#endif

static std::map<int, unsigned long long> last_proc_times;
static unsigned long long last_sys_time = 0;

std::string get_self_path() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::string::size_type pos = std::string(buffer).find_last_of("\\/");
    return std::string(buffer).substr(0, pos);
#else
    char buff[4096];
    ssize_t len = readlink("/proc/self/exe", buff, sizeof(buff)-1);
    if (len != -1) {
        buff[len] = '\0';
        std::string path(buff);
        return path.substr(0, path.find_last_of('/'));
    }
    return "";
#endif
}

#ifndef _WIN32
unsigned long long get_proc_total_time(int pid) {
    std::string path = "/proc/" + std::to_string(pid) + "/stat";
    std::ifstream f(path);
    if (!f.is_open()) return 0;
    
    std::string line;
    std::getline(f, line);
    
    size_t last_paren = line.find_last_of(')');
    if (last_paren == std::string::npos || last_paren + 2 >= line.length()) return 0;

    std::stringstream ss(line.substr(last_paren + 2));
    
    std::string ignore;
    unsigned long long utime = 0, stime = 0;
    
    for(int i=0; i<11; i++) ss >> ignore;
    
    ss >> utime >> stime;
    return utime + stime;
}

long get_pid_rss_kb(int pid) {
    std::string path = "/proc/" + std::to_string(pid) + "/statm";
    std::ifstream f(path);
    if (!f.is_open()) return 0;
    long size, rss;
    f >> size >> rss; 
    return rss * (sysconf(_SC_PAGESIZE) / 1024); 
}

void collect_descendants(int pid, std::set<int>& pids) {
    DIR* proc = opendir("/proc");
    if (!proc) return;

    struct dirent* entry;
    while ((entry = readdir(proc)) != NULL) {
        if (entry->d_type == DT_DIR && isdigit(entry->d_name[0])) {
            int current_pid = std::stoi(entry->d_name);
            if (pids.find(current_pid) != pids.end()) continue;

            std::string stat_path = "/proc/" + std::string(entry->d_name) + "/stat";
            std::ifstream f(stat_path);
            if (f.is_open()) {
                std::string line;
                std::getline(f, line);
                size_t last_paren = line.find_last_of(')');
                if (last_paren != std::string::npos && last_paren + 2 < line.length()) {
                    std::stringstream ss(line.substr(last_paren + 2));
                    char state;
                    int ppid;
                    ss >> state >> ppid;
                    
                    if (ppid == pid) {
                        pids.insert(current_pid);
                        collect_descendants(current_pid, pids);
                    }
                }
            }
        }
    }
    closedir(proc);
}

std::vector<int> get_child_pids() {
    std::set<int> pids;
    collect_descendants(getpid(), pids);
    return std::vector<int>(pids.begin(), pids.end());
}
#endif

std::string get_total_memory_str() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        double total_mb = pmc.WorkingSetSize / (1024.0 * 1024.0);
        std::stringstream ss;
        ss << std::fixed << std::setprecision(1) << total_mb << " MB"; 
        return ss.str();
    }
    return "0 MB";
#else
    long total_rss_kb = 0;
    
    total_rss_kb += get_pid_rss_kb(getpid());
    
    std::vector<int> children = get_child_pids();
    for (int pid : children) {
        total_rss_kb += get_pid_rss_kb(pid);
    }

    double total_mb = total_rss_kb / 1024.0;
    
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << total_mb << " MB"; 
    return ss.str();
#endif
}

std::string get_assets_path() {
    std::string exe_path = get_self_path();
    std::string local_assets = exe_path + "/assets/";
    if (g_file_test(local_assets.c_str(), G_FILE_TEST_IS_DIR)) return local_assets;
    std::string parent_assets = exe_path + "/../assets/";
    if (g_file_test(parent_assets.c_str(), G_FILE_TEST_IS_DIR)) return parent_assets;
    return local_assets; 
}

std::string get_user_data_dir() {
    const char* home = g_get_home_dir();
    std::string dir = std::string(home) + "/.config/zyro/";
    if (g_mkdir_with_parents(dir.c_str(), 0755) == -1) { }
    return dir;
}

std::string get_current_time_str() {
    time_t now = time(0);
    struct tm tstruct = *localtime(&now);
    char buf[80];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tstruct);
    return std::string(buf);
}

int find_download_index(guint64 id) {
    for(size_t i=0; i<downloads_list.size(); ++i) {
        if (downloads_list[i].id == id) return i;
    }
    return -1;
}

std::string json_escape(const std::string& s) {
    std::stringstream ss;
    for (char c : s) {
        switch (c) {
            case '"': ss << "\\\""; break;
            case '\\': ss << "\\\\"; break;
            case '\b': ss << "\\b"; break;
            case '\f': ss << "\\f"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            default:
                if ('\x00' <= c && c <= '\x1f') {
                    ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c;
                } else {
                    ss << c;
                }
        }
    }
    return ss.str();
}

void get_sys_stats(int& cpu_usage, std::string& ram_usage) {
    ram_usage = get_total_memory_str();

#ifdef _WIN32
    cpu_usage = 0;
#else
    std::ifstream file("/proc/stat");
    std::string line;
    std::getline(file, line);
    
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    std::sscanf(line.c_str(), "cpu %llu %llu %llu %llu %llu %llu %llu %llu", 
        &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
        
    unsigned long long total_sys_time = user + nice + system + idle + iowait + irq + softirq + steal;

    std::vector<int> pids = get_child_pids();
    pids.push_back(getpid());
    
    unsigned long long total_proc_time = 0;
    std::map<int, unsigned long long> current_proc_times;
    
    unsigned long long proc_delta_sum = 0;

    for (int pid : pids) {
        unsigned long long t = get_proc_total_time(pid);
        if (t == 0) continue;
        
        current_proc_times[pid] = t;
        
        if (last_proc_times.count(pid)) {
            if (t >= last_proc_times[pid]) {
                proc_delta_sum += (t - last_proc_times[pid]);
            }
        }
    }
    
    if (last_sys_time > 0 && total_sys_time > last_sys_time) {
        unsigned long long sys_delta = total_sys_time - last_sys_time;
        
        if (sys_delta > 0) {
            double pct = (double)proc_delta_sum / (double)sys_delta * 100.0;
            cpu_usage = (int)pct; 
        }
    }

    last_sys_time = total_sys_time;
    last_proc_times = current_proc_times;
#endif
}

std::string get_process_name(int pid) {
#ifdef _WIN32
    return ""; 
#else
    std::string path = "/proc/" + std::to_string(pid) + "/comm";
    std::ifstream f(path);
    std::string name;
    if (f.is_open()) {
        std::getline(f, name);
        name.erase(std::remove(name.begin(), name.end(), '\n'), name.end());
    }
    return name;
#endif
}

void apply_browser_theme(const std::string& theme_name) {
    GtkSettings* settings = gtk_settings_get_default();
    if (settings) {
        gboolean prefer_dark = (theme_name != "light"); 
        g_object_set(settings, "gtk-application-prefer-dark-theme", prefer_dark, NULL);
    }
}