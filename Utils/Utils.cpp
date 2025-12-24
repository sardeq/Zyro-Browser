#include "Utils.h"
#include <iostream>
#include <ctime>
#include <fstream>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
    #include <windows.h>
    #include <sys/types.h>
#else
    #include <unistd.h>
    #include <sys/sysinfo.h>
    #include <dirent.h>
#endif

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
long get_pid_rss_kb(int pid) {
    std::string path = "/proc/" + std::to_string(pid) + "/statm";
    std::ifstream f(path);
    if (!f.is_open()) return 0;
    long size, rss;
    f >> size >> rss; 
    return rss * (sysconf(_SC_PAGESIZE) / 1024); 
}

std::vector<int> get_child_pids() {
    std::vector<int> children;
    int my_pid = getpid();
    
    DIR* proc = opendir("/proc");
    if (!proc) return children;

    struct dirent* entry;
    while ((entry = readdir(proc)) != NULL) {
        if (entry->d_type == DT_DIR && isdigit(entry->d_name[0])) {
            int pid = std::stoi(entry->d_name);
            if (pid == my_pid) continue; // Skip self

            std::string stat_path = "/proc/" + std::string(entry->d_name) + "/stat";
            std::ifstream f(stat_path);
            if (f.is_open()) {
                std::string dummy;
                int ppid;
                std::string line;
                std::getline(f, line);
                size_t last_paren = line.find_last_of(')');
                if (last_paren != std::string::npos && last_paren + 2 < line.length()) {
                    std::stringstream ss(line.substr(last_paren + 2));
                    char state;
                    ss >> state >> ppid;
                    
                    if (ppid == my_pid) {
                        children.push_back(pid);
                    }
                }
            }
        }
    }
    closedir(proc);
    return children;
}
#endif

//idk man
std::string get_total_memory_str() {
    double total_mb = 0;

#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        total_mb = pmc.WorkingSetSize / (1024.0 * 1024.0);
    }
#else
    long rss_kb = get_pid_rss_kb(getpid());
    total_mb = rss_kb / 1024.0; 
#endif

    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << total_mb << " MB (Main)"; 
    return ss.str();
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

std::string get_process_name(int pid) {
#ifdef _WIN32
    return ""; // Windows later
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
    std::string css_data;
    
    if (theme_name == "light") {
        css_data = 
            "window { background-color: #f1f3f4; }"
            "notebook header { background-color: #ffffff; }"
            "notebook tab { background-color: #ffffff; color: #5f6368; }"
            "notebook tab:hover { background-color: #f1f3f4; }"
            "notebook tab:checked { background-color: #e8f0fe; color: #1967d2; }"
            "box.toolbar { background-color: #ffffff; border-bottom: 1px solid #dadce0; }"
            "entry { background-color: #f1f3f4; color: #202124; border: 1px solid transparent; }"
            "entry:focus { background-color: #ffffff; border: 1px solid #1967d2; }"
            "button { color: #5f6368; }"
            "button:hover { background-color: #f1f3f4; color: #202124; }";
    } 
    else if (theme_name == "midnight") {
        css_data = 
            "window { background-color: #000000; }"
            "notebook header { background-color: #111111; }"
            "notebook tab { background-color: #111111; color: #888888; }"
            "notebook tab:hover { background-color: #222222; }"
            "notebook tab:checked { background-color: #333333; color: #00ffcc; }"
            "box.toolbar { background-color: #111111; border-bottom: 1px solid #222; }"
            "entry { background-color: #222222; color: #ffffff; }"
            "entry:focus { border: 1px solid #00ffcc; }"
            "button { color: #888888; }"
            "button:hover { background-color: #333333; color: #00ffcc; }";
    }
    else if (theme_name == "ocean") {
        css_data = 
            "window { background-color: #0f1c2e; }"
            "notebook header { background-color: #162438; }"
            "notebook tab { background-color: #162438; color: #8faecf; }"
            "notebook tab:hover { background-color: #1f304a; }"
            "notebook tab:checked { background-color: #1f304a; color: #64ffda; }"
            "box.toolbar { background-color: #162438; border-bottom: 1px solid #0f1c2e; }"
            "entry { background-color: #1f304a; color: #ccd6f6; }"
            "entry:focus { border: 1px solid #64ffda; }"
            "button { color: #8faecf; }"
            "button:hover { background-color: #1f304a; color: #64ffda; }";
    }
    else if (theme_name == "sunset") {
        css_data = 
            "window { background-color: #2d1b2e; }"
            "notebook header { background-color: #45283c; }"
            "notebook tab { background-color: #45283c; color: #dcb3bc; }"
            "notebook tab:hover { background-color: #58344c; }"
            "notebook tab:checked { background-color: #58344c; color: #ff9e64; }"
            "box.toolbar { background-color: #45283c; border-bottom: 1px solid #2d1b2e; }"
            "entry { background-color: #2d1b2e; color: #fff; }"
            "entry:focus { border: 1px solid #ff9e64; }"
            "button { color: #dcb3bc; }"
            "button:hover { background-color: #58344c; color: #ff9e64; }";
    }
    else {
        css_data = 
            "window { background-color: #1a1b1e; }"
            "notebook header { background-color: #202124; }"
            "notebook tab { background-color: #202124; color: #9aa0a6; }"
            "notebook tab:hover { background-color: #292a2d; }"
            "notebook tab:checked { background-color: #323639; color: #e8eaed; }"
            "box.toolbar { background-color: #202124; border-bottom: 1px solid #1a1b1e; }"
            "entry { background-color: #303134; color: #e8eaed; }"
            "entry:focus { border: 1px solid #8ab4f8; }"
            "button { color: #9aa0a6; }"
            "button:hover { background-color: #3c4043; color: #e8eaed; }";
    }

    std::string style_path = get_assets_path() + "style.css";
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_path(provider, style_path.c_str(), NULL);
    
    GtkCssProvider* color_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(color_provider, css_data.c_str(), -1, NULL);

    GdkScreen* screen = gdk_screen_get_default();
    gtk_style_context_remove_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider));
    
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(color_provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

    g_object_unref(provider);
    g_object_unref(color_provider);
}