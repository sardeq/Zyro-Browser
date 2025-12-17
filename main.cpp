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
#include <iomanip>

#include <openssl/evp.h>
#include <openssl/aes.h>
#include <openssl/rand.h>

#ifdef _WIN32
    #include <windows.h>
    #include <wincrypt.h>
    #pragma comment(lib, "crypt32.lib")
#else
    #include <sys/sysinfo.h>
    #include <libsecret/secret.h>
#endif

// --- Security Context ---
struct SecurityContext {
    unsigned char key[32]; // The Raw AES-256 Key
    unsigned char iv[16];  // Initialization Vector
    bool ready = false;
};

SecurityContext global_security;

// --- Forward Declarations ---
std::string get_user_data_dir();
std::string encrypt_string(const std::string& plain);
std::string decrypt_string(const std::string& hex_cipher);
std::string hex_encode(const unsigned char* data, int len);
std::vector<unsigned char> hex_decode(const std::string& input);

// --- OS SPECIFIC ENCRYPTION LOGIC ---

// Helper: Get random bytes
void generate_random_key(unsigned char* buffer, int size) {
    RAND_bytes(buffer, size);
}

#ifdef _WIN32
// --- WINDOWS (DPAPI) ---
void init_security() {
    std::string key_file_path = get_user_data_dir() + "os_key.bin";
    
    // 1. Try to load existing key
    if (std::ifstream in{key_file_path, std::ios::binary | std::ios::ate}) {
        std::streamsize size = in.tellg();
        in.seekg(0, std::ios::beg);
        
        std::vector<char> encrypted_data(size);
        if (in.read(encrypted_data.data(), size)) {
            DATA_BLOB in_blob;
            in_blob.pbData = (BYTE*)encrypted_data.data();
            in_blob.cbData = (DWORD)size;
            
            DATA_BLOB out_blob;
            
            // Decrypt using Windows User Credentials
            if (CryptUnprotectData(&in_blob, NULL, NULL, NULL, NULL, 0, &out_blob)) {
                if (out_blob.cbData == 32) {
                    memcpy(global_security.key, out_blob.pbData, 32);
                    global_security.ready = true;
                    LocalFree(out_blob.pbData);
                }
            }
        }
    }

    // 2. If Failed or Not Found -> Create New
    if (!global_security.ready) {
        generate_random_key(global_security.key, 32);
        
        DATA_BLOB in_blob;
        in_blob.pbData = global_security.key;
        in_blob.cbData = 32;
        
        DATA_BLOB out_blob;
        // Encrypt using Windows User Credentials
        if (CryptProtectData(&in_blob, L"ZyroBrowserMasterKey", NULL, NULL, NULL, 0, &out_blob)) {
            std::ofstream out(key_file_path, std::ios::binary);
            out.write((char*)out_blob.pbData, out_blob.cbData);
            out.close();
            LocalFree(out_blob.pbData);
            global_security.ready = true;
        }
    }
    
    for(int i=0; i<16; i++) global_security.iv[i] = global_security.key[i] ^ 0xAA;
}

#else
// --- LINUX (LibSecret) ---

const SecretSchema * get_zyro_schema(void) {
    static const SecretSchema schema = {
        "org.freedesktop.Secret.Generic", SECRET_SCHEMA_NONE,
        {
            {  "application", SECRET_SCHEMA_ATTRIBUTE_STRING },
            {  NULL, SECRET_SCHEMA_ATTRIBUTE_STRING },
        }
    };
    return &schema;
}

void init_security() {
    GError *error = NULL;

    // 1. Try to fetch existing key from Keyring
    gchar *stored_key_hex = secret_password_lookup_sync(
        get_zyro_schema(),
        NULL, &error,
        "application", "zyro_browser_master_key",
        NULL
    );

    if (stored_key_hex != NULL) {
        std::vector<unsigned char> raw = hex_decode(std::string(stored_key_hex));
        if (raw.size() == 32) {
            memcpy(global_security.key, raw.data(), 32);
            global_security.ready = true;
        }
        secret_password_free(stored_key_hex);
    }

    // 2. If Not Found -> Create New
    if (!global_security.ready) {
        generate_random_key(global_security.key, 32);
        std::string hex_key = hex_encode(global_security.key, 32);
        
        secret_password_store_sync(
            get_zyro_schema(),
            SECRET_COLLECTION_DEFAULT,
            "Zyro Browser Master Key", 
            hex_key.c_str(),           
            NULL, &error,
            "application", "zyro_browser_master_key",
            NULL
        );
        
        if (error != NULL) {
            std::cerr << "LibSecret Error: " << error->message << std::endl;
            g_error_free(error);
        } else {
            global_security.ready = true;
        }
    }

    for(int i=0; i<16; i++) global_security.iv[i] = global_security.key[i] ^ 0xAA;
}
#endif


// --- Debug & Globals ---
#define LOG(msg) std::cout << "[DEBUG] " << msg << std::endl

// --- Data Structures ---
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
} settings;

struct HistoryItem {
    std::string title;
    std::string url;
    std::string time_str;
};

struct DownloadItem {
    std::string filename;
    std::string url;
    std::string status;
    double progress;
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
std::vector<BookmarkItem> bookmarks_list;
GtkWidget* global_bookmarks_bar = nullptr;

// --- Global State ---
WebKitWebContext* global_context = nullptr; 
GtkWidget* global_window = nullptr;
SoupSession* soup_session = nullptr; 

// In-memory cache for fast access
std::vector<HistoryItem> browsing_history;
std::vector<std::string> search_history;
std::vector<DownloadItem> downloads_list;
std::vector<PassItem> saved_passwords;

std::string global_master_password = "";

// --- Forward Declarations ---
void create_window(WebKitWebContext* ctx);
GtkNotebook* get_notebook(GtkWidget* win);
void run_js(WebKitWebView* view, const std::string& script); 
GtkWidget* create_new_tab(GtkWidget* win, const std::string& url, WebKitWebContext* context);
WebKitWebView* get_active_webview(GtkWidget* win);

std::string get_self_path() {
    char buff[4096];
    ssize_t len = readlink("/proc/self/exe", buff, sizeof(buff)-1);
    if (len != -1) {
        buff[len] = '\0';
        std::string path(buff);
        return path.substr(0, path.find_last_of('/'));
    }
    return "";
}

std::string get_assets_path() {
    std::string exe_path = get_self_path();
    std::string local_assets = exe_path + "/assets/";
    if (g_file_test(local_assets.c_str(), G_FILE_TEST_IS_DIR)) {
        return local_assets;
    }

    std::string parent_assets = exe_path + "/../assets/";
    if (g_file_test(parent_assets.c_str(), G_FILE_TEST_IS_DIR)) {
        return parent_assets;
    }
    
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
    struct tm tstruct;
    char buf[80];
    tstruct = *localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tstruct);
    return std::string(buf);
}

// --- Encryption Helpers ---

std::string hex_encode(const unsigned char* data, int len) {
    std::stringstream ss;
    for (int i = 0; i < len; ++i) ss << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];
    return ss.str();
}

std::vector<unsigned char> hex_decode(const std::string& input) {
    std::vector<unsigned char> output;
    for (size_t i = 0; i < input.length(); i += 2) {
        std::string byteString = input.substr(i, 2);
        output.push_back((unsigned char)strtol(byteString.c_str(), NULL, 16));
    }
    return output;
}

std::string encrypt_string(const std::string& plain) {
    if (!global_security.ready) return "";

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, global_security.key, global_security.iv);

    std::vector<unsigned char> ciphertext(plain.length() + AES_BLOCK_SIZE);
    int len, ciphertext_len;

    EVP_EncryptUpdate(ctx, ciphertext.data(), &len, (unsigned char*)plain.c_str(), plain.length());
    ciphertext_len = len;

    EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len);
    ciphertext_len += len;
    EVP_CIPHER_CTX_free(ctx);

    return hex_encode(ciphertext.data(), ciphertext_len);
}

std::string decrypt_string(const std::string& hex_cipher) {
    if (!global_security.ready) return "";
    
    std::vector<unsigned char> ciphertext = hex_decode(hex_cipher);
    std::vector<unsigned char> plaintext(ciphertext.size() + AES_BLOCK_SIZE);
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, global_security.key, global_security.iv);

    int len, plaintext_len;
    EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(), ciphertext.size());
    plaintext_len = len;

    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) <= 0) {
        EVP_CIPHER_CTX_free(ctx);
        return ""; // Decrypt failed
    }
    
    EVP_CIPHER_CTX_free(ctx);
    plaintext_len += len;
    return std::string((char*)plaintext.data(), plaintext_len);
}

// --- File I/O ---

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
    for (const auto& b : bookmarks_list) {
        f << b.url << "|" << b.title << "\n";
    }
}

void load_data() {
    std::string conf_dir = get_user_data_dir();
    
    GKeyFile* key_file = g_key_file_new();
    if (g_key_file_load_from_file(key_file, (conf_dir + "settings.ini").c_str(), G_KEY_FILE_NONE, NULL)) {
        gchar* engine = g_key_file_get_string(key_file, "General", "search_engine", NULL);
        if (engine) { settings.search_engine = engine; g_free(engine); }
        
        gchar* theme = g_key_file_get_string(key_file, "General", "theme", NULL);
        if (theme) { settings.theme = theme; g_free(theme); }
    }
    g_key_file_free(key_file);

    std::ifstream s_file(conf_dir + "searches.txt");
    std::string line;
    while (std::getline(s_file, line)) {
        if (!line.empty()) search_history.push_back(line);
    }

    std::ifstream h_file(conf_dir + "history.txt");
    while (std::getline(h_file, line)) {
        size_t p1 = line.find('|');
        size_t p2 = line.find_last_of('|');
        if (p1 != std::string::npos && p2 != std::string::npos && p1 != p2) {
            browsing_history.push_back({
                line.substr(p1 + 1, p2 - p1 - 1), 
                line.substr(0, p1),               
                line.substr(p2 + 1)               
            });
        }
    }

    std::ifstream p_file(conf_dir + "passwords.txt");
    while (std::getline(p_file, line)) {
        std::stringstream ss(line);
        std::string segment;
        std::vector<std::string> seglist;
        while(std::getline(ss, segment, '|')) {
            seglist.push_back(segment);
        }
        
        if(seglist.size() >= 3) {
            std::string raw_pass = decrypt_string(seglist[2]); 
            if(!raw_pass.empty()) {
                saved_passwords.push_back({ seglist[0], seglist[1], raw_pass });
            }
        }
    }

    std::ifstream b_file(conf_dir + "bookmarks.txt");
    while (std::getline(b_file, line)) {
        size_t sep = line.find('|');
        if (sep != std::string::npos) {
            bookmarks_list.push_back({ line.substr(sep + 1), line.substr(0, sep) });
        }
    }

    if(!soup_session) soup_session = soup_session_new();
    std::string base = get_assets_path();
    settings.home_url = "file://" + base + "home.html";
    settings.settings_url = "file://" + base + "settings.html";
    settings.history_url = "file://" + base + "history.html";
    settings.downloads_url = "file://" + base + "downloads.html";
    settings.passwords_url = "file://" + base + "passwords.html";
    settings.about_url = "file://" + base + "about.html"; 
    
    LOG("Data Loaded.");
}

void refresh_bookmarks_bar(GtkWidget* bar) {
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(bar));
    for(iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    for (const auto& b : bookmarks_list) {
        GtkWidget* btn = gtk_button_new_with_label(b.title.c_str());
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "bookmark-item");
        
        g_signal_connect(btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer url_ptr){
            std::string* u = (std::string*)url_ptr;
            WebKitWebView* v = get_active_webview(global_window);
            if(v) webkit_web_view_load_uri(v, u->c_str());
        }), new std::string(b.url));

        gtk_box_pack_start(GTK_BOX(bar), btn, FALSE, FALSE, 2);
    }
    gtk_widget_show_all(bar);
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

static void on_download_received_data(WebKitDownload* download, guint64 data_length, gpointer user_data) {
    int* idx_ptr = (int*)user_data;
    if(*idx_ptr >= 0 && *idx_ptr < downloads_list.size()) {
        double p = webkit_download_get_estimated_progress(download);
        downloads_list[*idx_ptr].progress = p * 100.0;
        downloads_list[*idx_ptr].status = "Downloading";
    }
}

static void on_download_finished(WebKitDownload* download, gpointer user_data) {
    int* idx_ptr = (int*)user_data;
    if(*idx_ptr >= 0 && *idx_ptr < downloads_list.size()) {
        downloads_list[*idx_ptr].progress = 100.0;
        downloads_list[*idx_ptr].status = "Completed";
    }
    delete idx_ptr; 
}

static void on_download_failed(WebKitDownload* download, GError* error, gpointer user_data) {
    int* idx_ptr = (int*)user_data;
    if(*idx_ptr >= 0 && *idx_ptr < downloads_list.size()) {
        downloads_list[*idx_ptr].status = "Failed";
    }
}

static gboolean on_decide_destination(WebKitDownload *download, gchar *suggested_filename, gpointer user_data) {
    const char* download_dir = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    if (!download_dir) download_dir = g_get_home_dir();
    
    std::string filename = suggested_filename ? suggested_filename : "download";
    std::string full_path = std::string(download_dir) + "/" + filename;
    std::string file_uri = "file://" + full_path;

    webkit_download_set_destination(download, file_uri.c_str());
    
    DownloadItem item;
    item.filename = filename;
    item.url = webkit_uri_request_get_uri(webkit_download_get_request(download));
    item.progress = 0.0;
    item.status = "Starting...";
    
    downloads_list.push_back(item);
    
    int* idx = new int(downloads_list.size() - 1);
    
    g_signal_connect(download, "received-data", G_CALLBACK(on_download_received_data), idx);
    g_signal_connect(download, "failed", G_CALLBACK(on_download_failed), idx);
    g_signal_connect(download, "finished", G_CALLBACK(on_download_finished), idx);
    
    return TRUE; 
}

static void on_download_started(WebKitWebContext *context, WebKitDownload *download, gpointer user_data) {
    g_signal_connect(download, "decide-destination", G_CALLBACK(on_decide_destination), NULL);
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
        if (std::regex_search(json, match, re)) return match[1].str();
        return "";
    };

    auto get_json_int = [&](std::string key) -> int {
        std::regex re("\"" + key + "\"\\s*:\\s*([0-9]+)");
        std::smatch match;
        if (std::regex_search(json, match, re)) return std::stoi(match[1].str());
        return -1;
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
        fetch_suggestions(get_json_val("query"), false, view);
    }
    else if (type == "save_settings") {
        std::string engine = get_json_val("engine");
        std::string theme = get_json_val("theme");
        if (engine.empty()) engine = settings.search_engine;
        if (theme.empty()) theme = settings.theme;
        save_settings(engine, theme);
        
        if(global_window) {
             GtkNotebook* nb = get_notebook(global_window);
             int pages = gtk_notebook_get_n_pages(nb);
             for(int i=0; i<pages; i++) {
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
    else if (type == "get_bookmarks") {
        std::stringstream ss; ss << "[";
        for(size_t i=0; i<bookmarks_list.size(); ++i) {
            ss << "{ \"title\": \"" << bookmarks_list[i].title << "\", \"url\": \"" << bookmarks_list[i].url << "\" }";
            if(i < bookmarks_list.size()-1) ss << ",";
        }
        ss << "]";
        run_js(view, "renderBookmarks('" + ss.str() + "');");
    }
    else if (type == "delete_bookmark") {
        int idx = get_json_int("index");
        if(idx >= 0 && idx < bookmarks_list.size()) {
            bookmarks_list.erase(bookmarks_list.begin() + idx);
            save_bookmarks_to_disk();
        }
    }
    else if (type == "get_theme") {
        run_js(view, "setTheme('" + settings.theme + "');");
    }
    else if (type == "get_downloads") {
        std::stringstream ss; ss << "[";
        for(size_t i=0; i<downloads_list.size(); ++i) {
            ss << "{ \"filename\": \"" << downloads_list[i].filename << "\", "
               << "\"url\": \"" << downloads_list[i].url << "\", "
               << "\"status\": \"" << downloads_list[i].status << "\", "
               << "\"progress\": " << downloads_list[i].progress << " }";
            if(i < downloads_list.size()-1) ss << ",";
        }
        ss << "]";
        run_js(view, "renderDownloads('" + ss.str() + "');");
    }
    else if (type == "clear_downloads") {
        downloads_list.clear();
    }
    else if (type == "get_passwords") {
        std::stringstream ss; ss << "[";
        for(size_t i=0; i<saved_passwords.size(); ++i) {
            ss << "{ \"site\": \"" << saved_passwords[i].site << "\", "
               << "\"user\": \"" << saved_passwords[i].user << "\", "
               << "\"pass\": \"" << saved_passwords[i].pass << "\" }";
            if(i < saved_passwords.size()-1) ss << ",";
        }
        ss << "]";
        run_js(view, "renderPasswords('" + ss.str() + "');");
    }
    else if (type == "save_password") {
        saved_passwords.push_back({get_json_val("site"), get_json_val("user"), get_json_val("pass")});
        save_passwords_to_disk();
    }
    else if (type == "delete_password") {
        int idx = get_json_int("index");
        if(idx >= 0 && idx < saved_passwords.size()) {
            saved_passwords.erase(saved_passwords.begin() + idx);
            save_passwords_to_disk();
        }
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
        webkit_web_view_load_uri(view, get_json_val("url").c_str());
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

void try_autofill(WebKitWebView* view, const char* uri) {
    if (!global_security.ready) return;
    std::string current_url(uri);

    for (const auto& item : saved_passwords) {
        if (current_url.find(item.site) != std::string::npos) {
            std::string script = 
                "var u = '" + item.user + "';"
                "var p = '" + item.pass + "';"
                "var passInputs = document.querySelectorAll('input[type=\"password\"]');"
                "if (passInputs.length > 0) {"
                "  passInputs[0].value = p;"
                "  passInputs[0].dispatchEvent(new Event('input', { bubbles: true }));"
                "  passInputs[0].dispatchEvent(new Event('change', { bubbles: true }));"
                "  var inputs = document.querySelectorAll('input[type=\"text\"], input[type=\"email\"]');"
                "  for (var i = 0; i < inputs.length; i++) {"
                "    if (inputs[i].compareDocumentPosition(passInputs[0]) & Node.DOCUMENT_POSITION_FOLLOWING) {"
                "       inputs[i].value = u;"
                "       inputs[i].dispatchEvent(new Event('input', { bubbles: true }));"
                "       inputs[i].dispatchEvent(new Event('change', { bubbles: true }));"
                "       break;"
                "    }"
                "  }"
                "}";
            
            run_js(view, script);
            return; 
        }
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
    
    GtkWidget* page = gtk_notebook_get_nth_page(nb, page_num);
    GList* children = gtk_container_get_children(GTK_CONTAINER(page));
    
    WebKitWebView* view = nullptr;
    for (GList* l = children; l != NULL; l = l->next) {
        if (WEBKIT_IS_WEB_VIEW(l->data)) {
            view = WEBKIT_WEB_VIEW(l->data);
            break;
        }
    }
    g_list_free(children);
    return view;
}

static gboolean update_home_stats(gpointer data) {
    if(!global_window) return TRUE;
    GtkNotebook* notebook = get_notebook(global_window);
    if (!notebook) return TRUE;
    
    int pages = gtk_notebook_get_n_pages(notebook);
    for (int i=0; i<pages; i++) {
        GtkWidget* page_box = gtk_notebook_get_nth_page(notebook, i);
        GList* children = gtk_container_get_children(GTK_CONTAINER(page_box));
        
        for (GList* l = children; l != NULL; l = l->next) {
            if (WEBKIT_IS_WEB_VIEW(l->data)) {
                WebKitWebView* view = WEBKIT_WEB_VIEW(l->data);
                const char* uri = webkit_web_view_get_uri(view);
                if (uri && std::string(uri).find("home.html") != std::string::npos) {
                    int cpu; std::string ram;
                    get_sys_stats(cpu, ram);
                    std::string script = "updateStats('" + std::to_string(cpu) + "', '" + ram + "');";
                    run_js(view, script);
                }
                break;
            }
        }
        g_list_free(children);
    }
    return TRUE; 
}

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

// --- Site Info Helper ---

void show_site_info_popover(GtkEntry* entry, WebKitWebView* view) {
    const char* uri = webkit_web_view_get_uri(view);
    bool is_secure = (uri && strncmp(uri, "https://", 8) == 0);

    // Create Popover pointing to the Entry
    GtkWidget* popover = gtk_popover_new(GTK_WIDGET(entry));
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    
    // Attempt to point specifically to the icon area (left side)
    GdkRectangle rect;
    gtk_entry_get_icon_area(entry, GTK_ENTRY_ICON_PRIMARY, &rect);
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    g_object_set(box, "margin", 10, NULL);

    // Security Status Row
    GtkWidget* status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    const char* icon_name = is_secure ? "channel-secure-symbolic" : "channel-insecure-symbolic";
    const char* text = is_secure ? "Connection is secure" : "Connection is not secure";
    
    gtk_box_pack_start(GTK_BOX(status_box), gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_MENU), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(status_box), gtk_label_new(text), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), status_box, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    // Cookies (Placeholder)
    GtkWidget* cookies_btn = gtk_button_new_with_label("Cookies and site data");
    gtk_button_set_relief(GTK_BUTTON(cookies_btn), GTK_RELIEF_NONE);
    // Align text to left
    GtkWidget* cookies_lbl = gtk_bin_get_child(GTK_BIN(cookies_btn));
    gtk_label_set_xalign(GTK_LABEL(cookies_lbl), 0.0);
    gtk_box_pack_start(GTK_BOX(box), cookies_btn, FALSE, FALSE, 0);

    // Site Settings (Placeholder)
    GtkWidget* settings_btn = gtk_button_new_with_label("Site settings");
    gtk_button_set_relief(GTK_BUTTON(settings_btn), GTK_RELIEF_NONE);
    GtkWidget* settings_lbl = gtk_bin_get_child(GTK_BIN(settings_btn));
    gtk_label_set_xalign(GTK_LABEL(settings_lbl), 0.0);
    gtk_box_pack_start(GTK_BOX(box), settings_btn, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(popover), box);
    gtk_widget_show_all(box);
    gtk_popover_popup(GTK_POPOVER(popover));
}

// Callback for Icon Click
void on_icon_press(GtkEntry *entry, GtkEntryIconPosition icon_pos, GdkEvent *event, gpointer user_data) {
    if (icon_pos == GTK_ENTRY_ICON_PRIMARY) {
        WebKitWebView* view = WEBKIT_WEB_VIEW(user_data);
        show_site_info_popover(entry, view);
    }
}

// --- Tab Logic ---

void update_url_bar(WebKitWebView* web_view, GtkEntry* entry) {
    const char* uri = webkit_web_view_get_uri(web_view);
    if (!uri) return;

    std::string u(uri);
    
    if(u.find("file://") == std::string::npos) {
        // Update URL Text - ALWAYS show full URI
        // Note: We use the raw URI here to ensure path is visible
        gtk_entry_set_text(entry, uri); 
        
        // Update Lock Icon based on Scheme
        if(u.find("https://") == 0) {
            gtk_entry_set_icon_from_icon_name(entry, GTK_ENTRY_ICON_PRIMARY, "channel-secure-symbolic");
            gtk_entry_set_icon_tooltip_text(entry, GTK_ENTRY_ICON_PRIMARY, "View site information");
        } else {
            gtk_entry_set_icon_from_icon_name(entry, GTK_ENTRY_ICON_PRIMARY, "channel-insecure-symbolic");
            gtk_entry_set_icon_tooltip_text(entry, GTK_ENTRY_ICON_PRIMARY, "Connection is not secure");
        }
        
        // Make sure icon is clickable
        gtk_entry_set_icon_activatable(entry, GTK_ENTRY_ICON_PRIMARY, TRUE);

        // Update History
        const char* title = webkit_web_view_get_title(web_view);
        add_history_item(u, title ? std::string(title) : "");

    } else {
        // Internal Page
        gtk_entry_set_text(entry, ""); 
        gtk_entry_set_icon_from_icon_name(entry, GTK_ENTRY_ICON_PRIMARY, "open-menu-symbolic"); 
        gtk_entry_set_icon_activatable(entry, GTK_ENTRY_ICON_PRIMARY, FALSE);
    }
}

void on_load_changed(WebKitWebView* web_view, WebKitLoadEvent load_event, gpointer user_data) {
    GtkEntry* entry = GTK_ENTRY(g_object_get_data(G_OBJECT(web_view), "entry"));
    if (!entry) return;

    // Update on COMMITTED (Load started receiving data)
    if (load_event == WEBKIT_LOAD_COMMITTED) {
        update_url_bar(web_view, entry);
    }
    
    // Update on FINISHED (Load complete - handles final redirects)
    if (load_event == WEBKIT_LOAD_FINISHED) {
        const char* uri = webkit_web_view_get_uri(web_view);
        if (uri) {
            update_url_bar(web_view, entry); // Ensure final URL is set
            try_autofill(web_view, uri);
        }
    }
}

static void on_tab_close(GtkButton*, gpointer v_box_widget) { 
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
    
    auto create_base_btn = [&](const char* label, const char* icon_name) {
        GtkWidget* btn = gtk_button_new();
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "menu-item");
        gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
        
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        GtkWidget* img = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_MENU);
        GtkWidget* lbl = gtk_label_new(label);
        
        gtk_box_pack_start(GTK_BOX(box), img, FALSE, FALSE, 10);
        gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(btn), box);
        return btn;
    };

    auto mk_menu_item = [&](const char* label, const char* icon_name, GCallback cb) {
        GtkWidget* b = create_base_btn(label, icon_name);
        if (cb) g_signal_connect(b, "clicked", cb, win);
        return b;
    };

    auto mk_url_item = [&](const char* label, const char* icon, const std::string& url) {
        GtkWidget* b = create_base_btn(label, icon);
        g_signal_connect(b, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data){
            std::string* u = (std::string*)data;
            create_new_tab(global_window, *u, global_context);
            delete u; 
        }), new std::string(url));
        return b;
    };

    auto mk_sep = [&]() {
        GtkWidget* s = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_widget_set_name(s, "menu-separator");
        return s;
    };

    gtk_box_pack_start(GTK_BOX(menu_box), mk_url_item("New Tab", "tab-new-symbolic", settings.home_url), FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(menu_box), mk_sep(), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(menu_box), mk_url_item("Downloads", "folder-download-symbolic", settings.downloads_url), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(menu_box), mk_url_item("Passwords", "dialog-password-symbolic", settings.passwords_url), FALSE, FALSE, 0); 
    gtk_box_pack_start(GTK_BOX(menu_box), mk_url_item("History", "document-open-recent-symbolic", settings.history_url), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(menu_box), mk_url_item("Settings", "preferences-system-symbolic", settings.settings_url), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(menu_box), mk_url_item("About Zyro", "help-about-symbolic", settings.about_url), FALSE, FALSE, 0); 

    gtk_box_pack_start(GTK_BOX(menu_box), mk_sep(), FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(menu_box), mk_menu_item("Exit Zyro", "application-exit-symbolic", G_CALLBACK(gtk_main_quit)), FALSE, FALSE, 0);

    gtk_widget_show_all(menu_box);
    gtk_container_add(GTK_CONTAINER(popover), menu_box);
    gtk_popover_popup(GTK_POPOVER(popover));
}

static gboolean on_decide_policy(WebKitWebView* v, WebKitPolicyDecision* decision, WebKitPolicyDecisionType type, gpointer win) {
    if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
        WebKitNavigationAction* action = webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(decision));
        
        if (webkit_navigation_action_get_mouse_button(action) == 2) {
            WebKitURIRequest* req = webkit_navigation_action_get_request(action);
            const char* uri = webkit_uri_request_get_uri(req);
            
            create_new_tab(GTK_WIDGET(win), uri, global_context);
            webkit_policy_decision_ignore(decision);
            return TRUE;
        }
    }
    return FALSE;
}

GtkWidget* create_new_tab(GtkWidget* win, const std::string& url, WebKitWebContext* context) {
    GtkNotebook* notebook = get_notebook(win);
    
    WebKitUserContentManager* ucm = webkit_user_content_manager_new();
    webkit_user_content_manager_register_script_message_handler(ucm, "zyro");
    
    GtkWidget* view = GTK_WIDGET(g_object_new(WEBKIT_TYPE_WEB_VIEW, "web-context", context, "user-content-manager", ucm, NULL));
    g_signal_connect(ucm, "script-message-received::zyro", G_CALLBACK(on_script_message), view);

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
    GtkWidget* b_menu = mkbtn("open-menu-symbolic", "Menu"); 

    GtkWidget* url_entry = gtk_entry_new();
    GtkEntryCompletion* completion = gtk_entry_completion_new();
    GtkListStore* store = gtk_list_store_new(1, G_TYPE_STRING);
    gtk_entry_completion_set_model(completion, GTK_TREE_MODEL(store));
    gtk_entry_completion_set_text_column(completion, 0);
    gtk_entry_set_completion(GTK_ENTRY(url_entry), completion);

    gtk_box_pack_start(GTK_BOX(toolbar), b_back, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), b_fwd, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), b_refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), b_home, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), url_entry, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(toolbar), b_menu, FALSE, FALSE, 0);

    // Bookmarks Bar
    GtkWidget* bookmarks_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_style_context_add_class(gtk_widget_get_style_context(bookmarks_bar), "bookmarks-bar");
    refresh_bookmarks_bar(bookmarks_bar);

    // Main Layout Container
    GtkWidget* page_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    g_object_set_data(G_OBJECT(page_box), "win", win); 
    g_object_set_data(G_OBJECT(page_box), "bookmarks_bar", bookmarks_bar); // Store ref for updating
    
    gtk_box_pack_start(GTK_BOX(page_box), toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page_box), bookmarks_bar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page_box), view, TRUE, TRUE, 0);

    // Tab Header
    GtkWidget* tab_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget* label = gtk_label_new("New Tab");
    GtkWidget* close = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
    gtk_widget_set_name(close, "tab-close-btn");
    gtk_box_pack_start(GTK_BOX(tab_header), label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(tab_header), close, FALSE, FALSE, 0);
    gtk_widget_show_all(tab_header);

    // Signals
    g_signal_connect(b_back, "clicked", G_CALLBACK(+[](GtkButton*, gpointer v){ webkit_web_view_go_back(WEBKIT_WEB_VIEW(v)); }), view);
    g_signal_connect(b_fwd, "clicked", G_CALLBACK(+[](GtkButton*, gpointer v){ webkit_web_view_go_forward(WEBKIT_WEB_VIEW(v)); }), view);
    g_signal_connect(b_refresh, "clicked", G_CALLBACK(+[](GtkButton*, gpointer v){ webkit_web_view_reload(WEBKIT_WEB_VIEW(v)); }), view);
    g_signal_connect(b_home, "clicked", G_CALLBACK(+[](GtkButton*, gpointer v){ webkit_web_view_load_uri(WEBKIT_WEB_VIEW(v), settings.home_url.c_str()); }), view);
    
    g_object_set_data(G_OBJECT(view), "entry", url_entry);
    g_object_set_data(G_OBJECT(view), "page_box", page_box);

    g_signal_connect(url_entry, "changed", G_CALLBACK(on_url_changed), NULL);
    g_signal_connect(url_entry, "activate", G_CALLBACK(on_url_activate), view);
    g_signal_connect(url_entry, "icon-press", G_CALLBACK(on_icon_press), view); 
    g_signal_connect(completion, "match-selected", G_CALLBACK(on_match_selected), url_entry);
    g_signal_connect(view, "load-changed", G_CALLBACK(on_load_changed), NULL);

    // Star Button Logic
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(url_entry), GTK_ENTRY_ICON_SECONDARY, "non-starred-symbolic");
    g_signal_connect(url_entry, "icon-press", G_CALLBACK(+[](GtkEntry* entry, GtkEntryIconPosition pos, GdkEvent* ev, gpointer v_ptr){
        if (pos == GTK_ENTRY_ICON_SECONDARY) {
            WebKitWebView* view = WEBKIT_WEB_VIEW(v_ptr);
            std::string url = webkit_web_view_get_uri(view);
            const char* title = webkit_web_view_get_title(view);
            if(url.empty()) return;

            bookmarks_list.push_back({ title ? title : url, url });
            save_bookmarks_to_disk();
            
            gtk_entry_set_icon_from_icon_name(entry, GTK_ENTRY_ICON_SECONDARY, "starred-symbolic");
            
            // Refresh bar in current tab
            GtkWidget* p_box = (GtkWidget*)g_object_get_data(G_OBJECT(view), "page_box");
            GtkWidget* b_bar = (GtkWidget*)g_object_get_data(G_OBJECT(p_box), "bookmarks_bar");
            refresh_bookmarks_bar(b_bar);
        }
    }), view);

    g_signal_connect(view, "notify::uri", G_CALLBACK(+[](WebKitWebView* v, GParamSpec*, gpointer){ 
        GtkEntry* e = GTK_ENTRY(g_object_get_data(G_OBJECT(v), "entry"));
        update_url_bar(v, e);
    }), NULL);

    g_signal_connect(view, "decide-policy", G_CALLBACK(on_decide_policy), win);
    g_signal_connect(view, "notify::title", G_CALLBACK(+[](WebKitWebView* v, GParamSpec*, GtkLabel* l){ 
        const char* t = webkit_web_view_get_title(v); if(t) gtk_label_set_text(l, t); 
    }), label);

    g_signal_connect(b_menu, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer w){ show_menu(b, w); }), win);
    g_signal_connect(close, "clicked", G_CALLBACK(on_tab_close), page_box);

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

// --- Key Press Handler ---
gboolean on_key_press(GtkWidget* widget, GdkEventKey* event, gpointer user_data) {
    // Get Active Tab Data
    WebKitWebView* view = get_active_webview(widget);
    GtkNotebook* nb = get_notebook(widget);
    GtkEntry* url_entry = nullptr;
    if (view) url_entry = GTK_ENTRY(g_object_get_data(G_OBJECT(view), "entry"));

    // --- CTRL Shortcuts ---
    if (event->state & GDK_CONTROL_MASK) {
        switch (event->keyval) {
            case GDK_KEY_t: 
                create_new_tab(widget, settings.home_url, global_context); 
                return TRUE;
            case GDK_KEY_w: {
                int page = gtk_notebook_get_current_page(nb);
                if (page != -1) {
                    gtk_notebook_remove_page(nb, page);
                    if (gtk_notebook_get_n_pages(nb) == 0) gtk_widget_destroy(widget);
                }
                return TRUE;
            }
            case GDK_KEY_r: // Reload
            case GDK_KEY_F5:
                if (view) webkit_web_view_reload(view);
                return TRUE;
            case GDK_KEY_l: // Focus URL Bar
                if (url_entry) gtk_widget_grab_focus(GTK_WIDGET(url_entry));
                return TRUE;
            case GDK_KEY_Tab: 
            case GDK_KEY_Page_Down: // Next Tab
                if (nb) {
                    int curr = gtk_notebook_get_current_page(nb);
                    int last = gtk_notebook_get_n_pages(nb) - 1;
                    gtk_notebook_set_current_page(nb, (curr == last) ? 0 : curr + 1);
                }
                return TRUE;
            case GDK_KEY_Page_Up: // Prev Tab
                if (nb) {
                    int curr = gtk_notebook_get_current_page(nb);
                    int last = gtk_notebook_get_n_pages(nb) - 1;
                    gtk_notebook_set_current_page(nb, (curr == 0) ? last : curr - 1);
                }
                return TRUE;
        }
    }

    // --- ALT Shortcuts ---
    if (event->state & GDK_MOD1_MASK) {
        switch (event->keyval) {
            case GDK_KEY_Left:
                if (view && webkit_web_view_can_go_back(view)) webkit_web_view_go_back(view);
                return TRUE;
            case GDK_KEY_Right:
                if (view && webkit_web_view_can_go_forward(view)) webkit_web_view_go_forward(view);
                return TRUE;
        }
    }
    
    // --- F5 (No Modifier) ---
    if (event->keyval == GDK_KEY_F5 && view) {
        webkit_web_view_reload(view);
        return TRUE;
    }

    return FALSE; 
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
    
    g_signal_connect(win, "key-press-event", G_CALLBACK(on_key_press), NULL);
    
    std::string style_path = get_assets_path() + "style.css";
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_path(provider, style_path.c_str(), NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    
    GtkWidget* nb = gtk_notebook_new();
    gtk_notebook_set_show_border(GTK_NOTEBOOK(nb), FALSE);
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(nb), TRUE);
    g_object_set_data(G_OBJECT(win), "notebook", nb);

    gtk_box_pack_start(GTK_BOX(box), nb, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(win), box);

    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    
    gtk_widget_show_all(win);
    
    create_new_tab(win, settings.home_url, ctx);
}


void init_security();

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    
    std::string user_dir = get_user_data_dir();
    std::string cache = user_dir + "cache";
    std::string data = user_dir + "data";

    WebKitWebsiteDataManager* mgr = webkit_website_data_manager_new("base-cache-directory", cache.c_str(), "base-data-directory", data.c_str(), NULL);
    global_context = webkit_web_context_new_with_website_data_manager(mgr);
    webkit_cookie_manager_set_persistent_storage(webkit_web_context_get_cookie_manager(global_context), (data+"/cookies.sqlite").c_str(), WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);

    init_security(); // Auto-unlocks using OS Keyring
    load_data();
    create_window(global_context);

    g_signal_connect(global_context, "download-started", G_CALLBACK(on_download_started), NULL);
    g_timeout_add(1000, update_home_stats, NULL);
    
    gtk_main();
    return 0;
}