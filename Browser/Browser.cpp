#include "Browser.h"
#include "Globals.h"
#include "Utils.h"
#include "Storage.h"
#include "Downloads.h"

#include <iostream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <cstring> 

// --- Forward Declarations ---
void show_site_info_popover(GtkEntry* entry, WebKitWebView* view);
void show_menu(GtkButton* btn, gpointer win);
void on_url_activate(GtkEntry* e, gpointer view_ptr);
static void on_url_changed(GtkEditable* editable, gpointer user_data);
static GtkWidget* on_web_view_create(WebKitWebView* web_view, WebKitNavigationAction* navigation_action, gpointer user_data);

// --- Helpers ---
void run_js(WebKitWebView* view, const std::string& script) {
    webkit_web_view_evaluate_javascript(view, script.c_str(), -1, NULL, NULL, NULL, NULL, NULL);
}

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

// --- IPC: C++ <-> JS Communication ---
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
    else if (type == "rename_bookmark") {
        int idx = get_json_int("index");
        std::string new_title = get_json_val("new_title");
        if(idx >= 0 && idx < bookmarks_list.size() && !new_title.empty()) {
            bookmarks_list[idx].title = new_title;
            save_bookmarks_to_disk();
        }
    }
    else if (type == "get_shortcuts") {
        std::stringstream ss; ss << "[";
        for(size_t i=0; i<shortcuts_list.size(); ++i) {
            ss << "{ \"name\": \"" << shortcuts_list[i].name << "\", \"url\": \"" << shortcuts_list[i].url << "\" }";
            if(i < shortcuts_list.size()-1) ss << ",";
        }
        ss << "]";
        run_js(view, "renderShortcuts(" + ss.str() + ");");
    }
    else if (type == "add_shortcut") {
        shortcuts_list.push_back({ get_json_val("name"), get_json_val("url") });
        save_shortcuts_to_disk();
    }
    else if (type == "delete_shortcut") {
        int idx = get_json_int("index");
        if(idx >= 0 && idx < (int)shortcuts_list.size()) {
            shortcuts_list.erase(shortcuts_list.begin() + idx);
            save_shortcuts_to_disk();
        }
    }
    else if (type == "edit_shortcut") {
        int idx = get_json_int("index");
        if(idx >= 0 && idx < (int)shortcuts_list.size()) {
            shortcuts_list[idx].name = get_json_val("name");
            shortcuts_list[idx].url = get_json_val("url");
            save_shortcuts_to_disk();
        }
    }
    else if (type == "get_theme") {
        run_js(view, "setTheme('" + settings.theme + "');");
    }
    else if (type == "get_downloads") {
        std::stringstream ss; ss << "[";
        for(size_t i=0; i<downloads_list.size(); ++i) {
            ss << "{ \"id\": " << downloads_list[i].id << ", "
               << "\"filename\": \"" << downloads_list[i].filename << "\", "
               << "\"url\": \"" << downloads_list[i].url << "\", "
               << "\"status\": \"" << downloads_list[i].status << "\", "
               << "\"progress\": " << downloads_list[i].progress << " }";
            if(i < downloads_list.size()-1) ss << ",";
        }
        ss << "]";
        run_js(view, "renderDownloads('" + ss.str() + "');");
    }
    else if (type == "stop_download") {
        int id = get_json_int("id");
        int idx = find_download_index((guint64)id);
        if (idx != -1 && downloads_list[idx].status == "Downloading") {
            webkit_download_cancel(downloads_list[idx].webkit_download);
        }
    }
    else if (type == "clear_downloads") {
        auto it = std::remove_if(downloads_list.begin(), downloads_list.end(), 
            [](const DownloadItem& i){ return i.status != "Downloading"; });
        downloads_list.erase(it, downloads_list.end());
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
            ss << "{ \"title\": \"" << json_escape(h.title) << "\", "
            << "\"url\": \"" << json_escape(h.url) << "\", "
            << "\"time\": \"" << h.time_str << "\" }";
            if(i < browsing_history.size()-1) ss << ",";
        }
        ss << "]";
        
        // We treat the JSON object as a variable in JS to avoid quote hell
        std::string json_data = ss.str();
        // Escape single quotes just for the JS function call wrapper
        std::string safe_json; 
        for(char c : json_data) {
            if(c == '\'') safe_json += "\\'";
            else safe_json += c;
        }

        std::string script = "renderHistory('" + safe_json + "');";
        run_js(view, script);
    }
    else if (type == "clear_history") {
        clear_all_history();
        run_js(view, "renderHistory('[]');");
    }
}

// --- Autofill ---
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

// --- Bookmarks Bar ---
void refresh_bookmarks_bar(GtkWidget* bar) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(bar));
    for(GList* iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    for (size_t i = 0; i < bookmarks_list.size(); ++i) {
        GtkWidget* btn = gtk_button_new_with_label(bookmarks_list[i].title.c_str());
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "bookmark-item");
        
        g_signal_connect(btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer url_ptr){
            std::string* u = (std::string*)url_ptr;
            WebKitWebView* v = get_active_webview(global_window);
            if(v) webkit_web_view_load_uri(v, u->c_str());
            delete u;
        }), new std::string(bookmarks_list[i].url));

        g_signal_connect(btn, "button-press-event", G_CALLBACK(+[](GtkWidget* widget, GdkEventButton* event, gpointer idx_ptr) -> gboolean {
            if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
                int index = GPOINTER_TO_INT(idx_ptr);
                
                GtkWidget* menu = gtk_menu_new();
                GtkWidget* rename_item = gtk_menu_item_new_with_label("Rename");
                GtkWidget* del_item = gtk_menu_item_new_with_label("Delete Bookmark");
                
                // RENAME
                g_signal_connect(rename_item, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer data){
                    int idx = GPOINTER_TO_INT(data);
                    
                    GtkWidget* dialog = gtk_dialog_new_with_buttons("Rename Bookmark",
                        GTK_WINDOW(global_window), GTK_DIALOG_MODAL,
                        "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_OK, NULL);
                    
                    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
                    GtkWidget* entry = gtk_entry_new();
                    gtk_entry_set_text(GTK_ENTRY(entry), bookmarks_list[idx].title.c_str());
                    gtk_container_add(GTK_CONTAINER(content), entry);
                    gtk_widget_show_all(dialog);

                    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
                        const char* text = gtk_entry_get_text(GTK_ENTRY(entry));
                        if (text && strlen(text) > 0) {
                            bookmarks_list[idx].title = text;
                            save_bookmarks_to_disk();
                            
                            GtkNotebook* nb = get_notebook(global_window);
                            for(int i=0; i<gtk_notebook_get_n_pages(nb); i++){
                                GtkWidget* pb = gtk_notebook_get_nth_page(nb, i);
                                GtkWidget* bb = (GtkWidget*)g_object_get_data(G_OBJECT(pb), "bookmarks_bar");
                                if(bb) refresh_bookmarks_bar(bb);
                            }
                        }
                    }
                    gtk_widget_destroy(dialog);
                }), GINT_TO_POINTER(index));

                // DELETE
                g_signal_connect(del_item, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer data){
                    int idx = GPOINTER_TO_INT(data);
                    bookmarks_list.erase(bookmarks_list.begin() + idx);
                    save_bookmarks_to_disk();
                    
                    GtkNotebook* nb = get_notebook(global_window);
                    for(int i=0; i<gtk_notebook_get_n_pages(nb); i++){
                        GtkWidget* pb = gtk_notebook_get_nth_page(nb, i);
                        GtkWidget* bb = (GtkWidget*)g_object_get_data(G_OBJECT(pb), "bookmarks_bar");
                        if(bb) refresh_bookmarks_bar(bb);
                    }
                }), GINT_TO_POINTER(index));

                gtk_menu_shell_append(GTK_MENU_SHELL(menu), rename_item);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), del_item);
                gtk_widget_show_all(menu);
                gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)event);
                return TRUE;
            }
            return FALSE;
        }), GINT_TO_POINTER(i));

        gtk_box_pack_start(GTK_BOX(bar), btn, FALSE, FALSE, 2);
    }
    gtk_widget_show_all(bar);
}

// --- URL & Tab Handlers ---

static void on_url_changed(GtkEditable* editable, gpointer user_data) {
    std::string text = gtk_entry_get_text(GTK_ENTRY(editable));
    if (text.find("://") != std::string::npos || text.find(".") != std::string::npos) return;
    
    GtkEntryCompletion* completion = gtk_entry_get_completion(GTK_ENTRY(editable));
    fetch_suggestions(text, true, completion);
}

void on_url_activate(GtkEntry* e, gpointer view_ptr) {
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
    g_signal_emit_by_name(entry, "activate"); 
    g_free(value);
    return TRUE;
}

void update_url_bar(WebKitWebView* web_view, GtkEntry* entry) {
    const char* uri = webkit_web_view_get_uri(web_view);
    if (!uri) return;

    std::string u(uri);
    
    if(u.find("file://") == std::string::npos) {
        gtk_entry_set_text(entry, uri); 
        
        if(u.find("https://") == 0) {
            gtk_entry_set_icon_from_icon_name(entry, GTK_ENTRY_ICON_PRIMARY, "channel-secure-symbolic");
            gtk_entry_set_icon_tooltip_text(entry, GTK_ENTRY_ICON_PRIMARY, "View site information");
        } else {
            gtk_entry_set_icon_from_icon_name(entry, GTK_ENTRY_ICON_PRIMARY, "channel-insecure-symbolic");
            gtk_entry_set_icon_tooltip_text(entry, GTK_ENTRY_ICON_PRIMARY, "Connection is not secure");
        }
        
        gtk_entry_set_icon_activatable(entry, GTK_ENTRY_ICON_PRIMARY, TRUE);

        const char* title = webkit_web_view_get_title(web_view);
        add_history_item(u, title ? std::string(title) : "");

        auto it = std::find_if(bookmarks_list.begin(), bookmarks_list.end(), 
            [&](const BookmarkItem& b){ return b.url == u; });

        if (it != bookmarks_list.end()) {
            gtk_entry_set_icon_from_icon_name(entry, GTK_ENTRY_ICON_SECONDARY, "starred-symbolic");
        } else {
            gtk_entry_set_icon_from_icon_name(entry, GTK_ENTRY_ICON_SECONDARY, "non-starred-symbolic");
        }

    } else {
        gtk_entry_set_text(entry, ""); 
        gtk_entry_set_icon_from_icon_name(entry, GTK_ENTRY_ICON_PRIMARY, "open-menu-symbolic"); 
        gtk_entry_set_icon_activatable(entry, GTK_ENTRY_ICON_PRIMARY, FALSE);
    }
}

void on_load_changed(WebKitWebView* web_view, WebKitLoadEvent load_event, gpointer user_data) {
    GtkEntry* entry = GTK_ENTRY(g_object_get_data(G_OBJECT(web_view), "entry"));
    if (!entry) return;

    if (load_event == WEBKIT_LOAD_COMMITTED) {
        update_url_bar(web_view, entry);
    }
    
    if (load_event == WEBKIT_LOAD_FINISHED) {
        const char* uri = webkit_web_view_get_uri(web_view);
        if (uri) {
            update_url_bar(web_view, entry); 
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

static gboolean on_decide_policy(WebKitWebView* v, WebKitPolicyDecision* decision, WebKitPolicyDecisionType type, gpointer win) {
    switch (type) {
        case WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION: {
            WebKitNavigationAction* action = webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(decision));
            if (webkit_navigation_action_get_mouse_button(action) == 2) {
                WebKitURIRequest* req = webkit_navigation_action_get_request(action);
                create_new_tab(GTK_WIDGET(win), webkit_uri_request_get_uri(req), global_context);
                webkit_policy_decision_ignore(decision);
                return TRUE;
            }
            break;
        }
        case WEBKIT_POLICY_DECISION_TYPE_RESPONSE: {
            WebKitResponsePolicyDecision* r_decision = WEBKIT_RESPONSE_POLICY_DECISION(decision);
            if (!webkit_response_policy_decision_is_mime_type_supported(r_decision)) {
                webkit_policy_decision_download(decision);
                return TRUE;
            }
            break;
        }
        default: break;
    }
    return FALSE;
}

// --- Handler for create signal (target="_blank") ---
static GtkWidget* on_web_view_create(WebKitWebView* web_view, WebKitNavigationAction* navigation_action, gpointer user_data) {
    GtkWidget* win = GTK_WIDGET(user_data);
    // FIX: Pass 'web_view' as the related view to avoid assertion failure
    return create_new_tab(win, "", global_context, web_view);
}

// --- Site Info Popover ---
void show_site_info_popover(GtkEntry* entry, WebKitWebView* view) {
    const char* uri = webkit_web_view_get_uri(view);
    bool is_secure = (uri && strncmp(uri, "https://", 8) == 0);

    GtkWidget* popover = gtk_popover_new(GTK_WIDGET(entry));
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    
    GdkRectangle rect;
    gtk_entry_get_icon_area(entry, GTK_ENTRY_ICON_PRIMARY, &rect);
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    g_object_set(box, "margin", 10, NULL);

    GtkWidget* status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    const char* icon_name = is_secure ? "channel-secure-symbolic" : "channel-insecure-symbolic";
    const char* text = is_secure ? "Connection is secure" : "Connection is not secure";
    
    gtk_box_pack_start(GTK_BOX(status_box), gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_MENU), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(status_box), gtk_label_new(text), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), status_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
    
    GtkWidget* cookies_btn = gtk_button_new_with_label("Cookies and site data");
    gtk_button_set_relief(GTK_BUTTON(cookies_btn), GTK_RELIEF_NONE);
    gtk_box_pack_start(GTK_BOX(box), cookies_btn, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(popover), box);
    gtk_widget_show_all(box);
    gtk_popover_popup(GTK_POPOVER(popover));
}

void on_icon_press(GtkEntry *entry, GtkEntryIconPosition icon_pos, GdkEvent *event, gpointer user_data) {
    if (icon_pos == GTK_ENTRY_ICON_PRIMARY) {
        WebKitWebView* view = WEBKIT_WEB_VIEW(user_data);
        show_site_info_popover(entry, view);
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

    auto mk_url_item = [&](const char* label, const char* icon, const std::string& url) {
        GtkWidget* b = create_base_btn(label, icon);
        g_signal_connect(b, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data){
            std::string* u = (std::string*)data;
            create_new_tab(global_window, *u, global_context);
            delete u; 
        }), new std::string(url));
        return b;
    };

    gtk_box_pack_start(GTK_BOX(menu_box), mk_url_item("New Tab", "tab-new-symbolic", settings.home_url), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(menu_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(menu_box), mk_url_item("Downloads", "folder-download-symbolic", settings.downloads_url), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(menu_box), mk_url_item("Passwords", "dialog-password-symbolic", settings.passwords_url), FALSE, FALSE, 0); 
    gtk_box_pack_start(GTK_BOX(menu_box), mk_url_item("History", "document-open-recent-symbolic", settings.history_url), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(menu_box), mk_url_item("Settings", "preferences-system-symbolic", settings.settings_url), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(menu_box), mk_url_item("About Zyro", "help-about-symbolic", settings.about_url), FALSE, FALSE, 0); 
    gtk_box_pack_start(GTK_BOX(menu_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 6);
    
    GtkWidget* exit_btn = create_base_btn("Exit Zyro", "application-exit-symbolic");
    g_signal_connect(exit_btn, "clicked", G_CALLBACK(gtk_main_quit), NULL);
    gtk_box_pack_start(GTK_BOX(menu_box), exit_btn, FALSE, FALSE, 0);

    gtk_widget_show_all(menu_box);
    gtk_container_add(GTK_CONTAINER(popover), menu_box);
    gtk_popover_popup(GTK_POPOVER(popover));
}

// --- CREATE NEW TAB (Main UI Builder) ---
GtkWidget* create_new_tab(GtkWidget* win, const std::string& url, WebKitWebContext* context, WebKitWebView* related_view) {
    GtkNotebook* notebook = get_notebook(win);
    
    WebKitUserContentManager* ucm = webkit_user_content_manager_new();
    webkit_user_content_manager_register_script_message_handler(ucm, "zyro");
    
    // FIX: Remove "web-context" when "related-view" is present
    GtkWidget* view;
    if (related_view) {
        view = GTK_WIDGET(g_object_new(WEBKIT_TYPE_WEB_VIEW, 
            "user-content-manager", ucm, 
            "related-view", related_view,
            NULL));
    } else {
        view = GTK_WIDGET(g_object_new(WEBKIT_TYPE_WEB_VIEW, 
            "web-context", context, 
            "user-content-manager", ucm, 
            NULL));
    }

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

    // Downloads Button & Popover
    GtkWidget* b_downloads = gtk_toggle_button_new(); 
    GtkWidget* dl_icon = gtk_image_new_from_icon_name("folder-download-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_container_add(GTK_CONTAINER(b_downloads), dl_icon);
    gtk_widget_set_tooltip_text(b_downloads, "Downloads");
    
    GtkWidget* dl_popover = gtk_popover_new(b_downloads);
    gtk_popover_set_position(GTK_POPOVER(dl_popover), GTK_POS_BOTTOM);
    
    GtkWidget* pop_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    g_object_set(pop_box, "margin", 10, "width-request", 300, NULL);
    
    GtkWidget* pop_header = gtk_label_new("<b>Downloads</b>");
    gtk_label_set_use_markup(GTK_LABEL(pop_header), TRUE);
    gtk_label_set_xalign(GTK_LABEL(pop_header), 0);
    
    GtkWidget* list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* full_page_link = gtk_button_new_with_label("View All Downloads");
    gtk_button_set_relief(GTK_BUTTON(full_page_link), GTK_RELIEF_NONE);
    g_signal_connect(full_page_link, "clicked", G_CALLBACK(+[](GtkButton*, gpointer){
        if(global_window && global_context) {
            create_new_tab(global_window, settings.downloads_url, global_context);
            if(global_downloads_popover) gtk_popover_popdown(GTK_POPOVER(global_downloads_popover));
        }
    }), NULL);

    gtk_box_pack_start(GTK_BOX(pop_box), pop_header, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(pop_box), list_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(pop_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(pop_box), full_page_link, FALSE, FALSE, 0);
    
    gtk_container_add(GTK_CONTAINER(dl_popover), pop_box);
    gtk_widget_show_all(pop_box);

    g_signal_connect(b_downloads, "toggled", G_CALLBACK(+[](GtkToggleButton* btn, gpointer pop){
        if (gtk_toggle_button_get_active(btn)) {
            update_downloads_popup(); 
            gtk_popover_popup(GTK_POPOVER(pop));
        } else {
            gtk_popover_popdown(GTK_POPOVER(pop));
        }
    }), dl_popover);
    
    g_signal_connect(dl_popover, "closed", G_CALLBACK(+[](GtkPopover*, gpointer btn){
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), FALSE);
    }), b_downloads);

    // Set Global Pointers
    global_downloads_btn = b_downloads;
    global_downloads_popover = dl_popover;
    global_downloads_list_box = list_box;

    g_signal_connect(dl_popover, "destroy", G_CALLBACK(+[](GtkWidget* widget, gpointer){
        if (global_downloads_popover == widget) {
            global_downloads_popover = nullptr;
            global_downloads_list_box = nullptr;
            global_downloads_btn = nullptr;
        }
    }), NULL);

    // URL Bar
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
    gtk_box_pack_start(GTK_BOX(toolbar), b_downloads, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), b_menu, FALSE, FALSE, 0);

    // Bookmarks Bar
    GtkWidget* bookmarks_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_style_context_add_class(gtk_widget_get_style_context(bookmarks_bar), "bookmarks-bar");
    refresh_bookmarks_bar(bookmarks_bar);

    // Main Layout Container
    GtkWidget* page_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    g_object_set_data(G_OBJECT(page_box), "win", win); 
    g_object_set_data(G_OBJECT(page_box), "bookmarks_bar", bookmarks_bar); 
    
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

            auto it = std::find_if(bookmarks_list.begin(), bookmarks_list.end(), 
                [&](const BookmarkItem& b){ return b.url == url; });
            
            if (it != bookmarks_list.end()) {
                return; 
            }

            bookmarks_list.push_back({ title ? title : url, url });
            save_bookmarks_to_disk();
            
            gtk_entry_set_icon_from_icon_name(entry, GTK_ENTRY_ICON_SECONDARY, "starred-symbolic");
            
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
    // Connect to the create signal to handle new windows/tabs (e.g. target="_blank")
    g_signal_connect(view, "create", G_CALLBACK(on_web_view_create), win);
    
    g_signal_connect(view, "notify::title", G_CALLBACK(+[](WebKitWebView* v, GParamSpec*, GtkLabel* l){ 
        const char* t = webkit_web_view_get_title(v); if(t) gtk_label_set_text(l, t); 
    }), label);

    g_signal_connect(b_menu, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer w){ show_menu(b, w); }), win);
    g_signal_connect(close, "clicked", G_CALLBACK(on_tab_close), page_box);

    int page = gtk_notebook_append_page(notebook, page_box, tab_header);
    gtk_notebook_set_tab_reorderable(notebook, page_box, TRUE);
    gtk_widget_show_all(page_box);
    gtk_notebook_set_current_page(notebook, page);
    
    // Only load if a URL is provided. WebKit handles loading for "create" signals automatically.
    if (!url.empty()) {
        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(view), url.c_str());
    }
    
    return view;
}

// --- Key Press Handler ---
gboolean on_key_press(GtkWidget* widget, GdkEventKey* event, gpointer user_data) {
    WebKitWebView* view = get_active_webview(widget);
    GtkNotebook* nb = get_notebook(widget);
    GtkEntry* url_entry = nullptr;
    if (view) url_entry = GTK_ENTRY(g_object_get_data(G_OBJECT(view), "entry"));

    // CTRL Shortcuts
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
            case GDK_KEY_r: 
            case GDK_KEY_F5:
                if (view) webkit_web_view_reload(view);
                return TRUE;
            case GDK_KEY_l: 
                if (url_entry) gtk_widget_grab_focus(GTK_WIDGET(url_entry));
                return TRUE;
            case GDK_KEY_Tab: 
            case GDK_KEY_Page_Down: 
                if (nb) {
                    int curr = gtk_notebook_get_current_page(nb);
                    int last = gtk_notebook_get_n_pages(nb) - 1;
                    gtk_notebook_set_current_page(nb, (curr == last) ? 0 : curr + 1);
                }
                return TRUE;
            case GDK_KEY_Page_Up: 
                if (nb) {
                    int curr = gtk_notebook_get_current_page(nb);
                    int last = gtk_notebook_get_n_pages(nb) - 1;
                    gtk_notebook_set_current_page(nb, (curr == 0) ? last : curr - 1);
                }
                return TRUE;
        }
    }

    // ALT Shortcuts
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
    
    if (event->keyval == GDK_KEY_F5 && view) {
        webkit_web_view_reload(view);
        return TRUE;
    }

    return FALSE; 
}

// --- Create Window ---
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

    GtkWidget* add_tab_btn = gtk_button_new_from_icon_name("list-add-symbolic", GTK_ICON_SIZE_MENU);
    gtk_widget_set_tooltip_text(add_tab_btn, "New Tab");
    gtk_widget_show(add_tab_btn);

    gtk_notebook_set_action_widget(GTK_NOTEBOOK(nb), add_tab_btn, GTK_PACK_END);

    g_signal_connect(add_tab_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer win){
        create_new_tab(GTK_WIDGET(win), settings.home_url, global_context);
    }), win);

    gtk_box_pack_start(GTK_BOX(box), nb, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(win), box);

    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    
    gtk_widget_show_all(win);
    
    create_new_tab(win, settings.home_url, ctx);
}