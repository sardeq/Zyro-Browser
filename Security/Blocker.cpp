#include "Blocker.h"
#include "Globals.h"
#include "Utils.h" // for get_user_data_dir if needed
#include "Browser.h" // needed to access get_notebook

// Basic list of heavy tracking/ad domains.
// This is formatted as a JSON array for WebKit's Content Blocker API.
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
        // If filter is ready and enabled, apply it to all existing tabs immediately
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
    }
}

void toggle_blocker() {
    global_blocker_enabled = !global_blocker_enabled;

    if (!global_window) return;
    GtkNotebook* nb = get_notebook(global_window);
    int pages = gtk_notebook_get_n_pages(nb);

    // 1. Update Logic (Apply/Remove filter)
    for(int i=0; i<pages; i++) {
        GtkWidget* page = gtk_notebook_get_nth_page(nb, i);
        GList* children = gtk_container_get_children(GTK_CONTAINER(page));
        
        WebKitWebView* view = nullptr;
        GtkWidget* toolbar = nullptr;
        
        for (GList* l = children; l != NULL; l = l->next) {
            if (WEBKIT_IS_WEB_VIEW(l->data)) view = WEBKIT_WEB_VIEW(l->data);
            if (GTK_IS_BOX(l->data) && !view) toolbar = GTK_WIDGET(l->data); // Assuming toolbar is before view
        }
        
        if (view) apply_blocker_to_view(view);

        // 2. Update UI (Icon)
        if (toolbar) {
            GList* tb_children = gtk_container_get_children(GTK_CONTAINER(toolbar));
            for (GList* t = tb_children; t != NULL; t = t->next) {
                GtkWidget* widget = GTK_WIDGET(t->data);
                // We identify the button by checking if it has the specific data tag we set in Browser.cpp
                const char* type = (const char*)g_object_get_data(G_OBJECT(widget), "type");
                if (type && strcmp(type, "blocker_btn") == 0) {
                    GtkWidget* img = gtk_bin_get_child(GTK_BIN(widget));
                    if(global_blocker_enabled) 
                        gtk_image_set_from_icon_name(GTK_IMAGE(img), "security-high-symbolic", GTK_ICON_SIZE_BUTTON);
                    else 
                        gtk_image_set_from_icon_name(GTK_IMAGE(img), "security-low-symbolic", GTK_ICON_SIZE_BUTTON);
                }
            }
            g_list_free(tb_children);
        }
        g_list_free(children);
    }
}