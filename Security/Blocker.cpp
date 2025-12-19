#include "Blocker.h"
#include "Globals.h"
#include "Utils.h"
#include "Browser.h"

static int session_blocked_count = 0;

const char* BLOCK_RULES_JSON = R"([
    { "trigger": { "url-filter": ".*doubleclick\\.net.*" }, "action": { "type": "block" } },
    { "trigger": { "url-filter": ".*googlesyndication\\.com.*" }, "action": { "type": "block" } },
    { "trigger": { "url-filter": ".*google-analytics\\.com.*" }, "action": { "type": "block" } },
    { "trigger": { "url-filter": ".*adservice\\.google\\.com.*" }, "action": { "type": "block" } },
    { "trigger": { "url-filter": ".*facebook\\.net.*" }, "action": { "type": "block" } },
    { "trigger": { "url-filter": ".*scorecardresearch\\.com.*" }, "action": { "type": "block" } },
    { "trigger": { "url-filter": ".*adnxs\\.com.*" }, "action": { "type": "block" } },
    { "trigger": { "url-filter": ".*adsrvr\\.org.*" }, "action": { "type": "block" } },
    { "trigger": { "url-filter": ".*bluekai\\.com.*" }, "action": { "type": "block" } },
    { "trigger": { "url-filter": ".*criteo\\.com.*" }, "action": { "type": "block" } },
    { "trigger": { "url-filter": "/ad_status\\.js" }, "action": { "type": "block" } },
    { "trigger": { "url-filter": "/ads/" }, "action": { "type": "block" } }
])";

static void on_filter_saved(WebKitUserContentFilterStore *store, GAsyncResult *res, gpointer user_data) {
    GError* error = nullptr;
    global_filter = webkit_user_content_filter_store_save_finish(store, res, &error);
    if (error) {
        g_error_free(error);
    } else if (global_filter && global_blocker_enabled) {
        if (global_window) {
            GtkNotebook* nb = get_notebook(global_window);
            int pages = gtk_notebook_get_n_pages(nb);
            for(int i=0; i<pages; i++) {
                GtkWidget* page = gtk_notebook_get_nth_page(nb, i);
                GList* children = gtk_container_get_children(GTK_CONTAINER(page));
                for (GList* l = children; l != NULL; l = l->next) {
                    if (WEBKIT_IS_WEB_VIEW(l->data)) {
                        apply_blocker_to_view(WEBKIT_WEB_VIEW(l->data));
                        break;
                    }
                }
                g_list_free(children);
            }
        }
    }
}

void init_blocker() {
    std::string store_path = get_user_data_dir() + "adblock_store";
    WebKitUserContentFilterStore* store = webkit_user_content_filter_store_new(store_path.c_str());
    
    GBytes* rules = g_bytes_new_static(BLOCK_RULES_JSON, strlen(BLOCK_RULES_JSON));
    webkit_user_content_filter_store_save(store, "zyro_basic_blocklist", rules, NULL, (GAsyncReadyCallback)on_filter_saved, NULL);
    g_bytes_unref(rules);
    g_object_unref(store);
}

void apply_blocker_to_view(WebKitWebView* view) {
    WebKitUserContentManager* ucm = webkit_web_view_get_user_content_manager(view);
    webkit_user_content_manager_remove_all_filters(ucm);
    
    if (global_blocker_enabled && global_filter) {
        webkit_user_content_manager_add_filter(ucm, global_filter);
        session_blocked_count++; // Simplified counting
    }
}

void toggle_blocker() {
    global_blocker_enabled = !global_blocker_enabled;

    if (!global_window) return;
    GtkNotebook* nb = get_notebook(global_window);
    int pages = gtk_notebook_get_n_pages(nb);

    for(int i=0; i<pages; i++) {
        GtkWidget* page = gtk_notebook_get_nth_page(nb, i);
        GList* children = gtk_container_get_children(GTK_CONTAINER(page));
        
        WebKitWebView* view = nullptr;
        // Search for view
        for (GList* l = children; l != NULL; l = l->next) {
            if (WEBKIT_IS_WEB_VIEW(l->data)) {
                view = WEBKIT_WEB_VIEW(l->data);
                break;
            }
        }
        
        if (view) {
            apply_blocker_to_view(view);
            // Refreshing the page is often good practice when toggling adblock 
            // to immediately show/hide elements, but we'll leave it to the user.
            // webkit_web_view_reload(view); 
        }
        g_list_free(children);
    }
}

int get_blocked_count() {
    return session_blocked_count;
}