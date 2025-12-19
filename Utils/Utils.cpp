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
#ifdef _WIN32
    ram_usage = "N/A"; cpu_usage = 0; 
#else
    struct sysinfo memInfo;
    sysinfo(&memInfo);
    long long physMemUsed = (memInfo.totalram - memInfo.freeram) * memInfo.mem_unit;
    long long totalPhysMem = memInfo.totalram * memInfo.mem_unit;
    
    std::stringstream ss; ss.precision(1);
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