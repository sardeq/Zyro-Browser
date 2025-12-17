#include "Downloads.h"
#include "Globals.h"
#include "Utils.h"
#include <algorithm>
#include <cstring> // Added for strlen

void update_downloads_popup() {
    if (!global_downloads_list_box) return;
    GList* children = gtk_container_get_children(GTK_CONTAINER(global_downloads_list_box));
    for (GList* iter = children; iter != NULL; iter = g_list_next(iter)) gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    if (downloads_list.empty()) {
        GtkWidget* lbl = gtk_label_new("No recent downloads");
        gtk_widget_set_opacity(lbl, 0.6);
        gtk_box_pack_start(GTK_BOX(global_downloads_list_box), lbl, FALSE, FALSE, 10);
    }

    int start = std::max(0, (int)downloads_list.size() - 5);
    for (int i = (int)downloads_list.size() - 1; i >= start; --i) {
        const auto& item = downloads_list[i];
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        GtkWidget* top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        GtkWidget* name = gtk_label_new(item.filename.c_str());
        gtk_label_set_xalign(GTK_LABEL(name), 0); gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
        
        GtkWidget* status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        GtkWidget* status = gtk_label_new(item.status.c_str());
        gtk_widget_set_opacity(status, 0.7);
        gtk_box_pack_start(GTK_BOX(status_box), status, FALSE, FALSE, 0);

        if (item.status == "Downloading") {
            GtkWidget* stop_btn = gtk_button_new_from_icon_name("process-stop-symbolic", GTK_ICON_SIZE_MENU);
            g_signal_connect(stop_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer id_ptr){
                int idx = find_download_index(GPOINTER_TO_UINT(id_ptr));
                if(idx != -1) webkit_download_cancel(downloads_list[idx].webkit_download);
            }), GUINT_TO_POINTER(item.id));
            gtk_box_pack_start(GTK_BOX(status_box), stop_btn, FALSE, FALSE, 0);
        }
        gtk_box_pack_start(GTK_BOX(top), name, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(top), status_box, FALSE, FALSE, 0);
        GtkWidget* pb = gtk_progress_bar_new();
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pb), item.progress / 100.0);
        if (item.status == "Cancelled" || item.status == "Failed") gtk_style_context_add_class(gtk_widget_get_style_context(pb), "error");
        
        gtk_box_pack_start(GTK_BOX(row), top, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), pb, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(global_downloads_list_box), row, FALSE, FALSE, 5);
        if(i > start) gtk_box_pack_start(GTK_BOX(global_downloads_list_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 5);
    }
    gtk_widget_show_all(global_downloads_list_box);
}

static void on_download_received_data(WebKitDownload* download, guint64 data_length, gpointer user_data) {
    int idx = find_download_index(GPOINTER_TO_UINT(user_data));
    if(idx != -1) {
        downloads_list[idx].progress = webkit_download_get_estimated_progress(download) * 100.0;
        downloads_list[idx].status = "Downloading";
    }
}
static void on_download_finished(WebKitDownload* download, gpointer user_data) {
    int idx = find_download_index(GPOINTER_TO_UINT(user_data));
    if(idx != -1 && downloads_list[idx].status != "Cancelled" && downloads_list[idx].status != "Failed") {
        downloads_list[idx].progress = 100.0; downloads_list[idx].status = "Completed";
    }
}
static void on_download_failed(WebKitDownload* download, GError* error, gpointer user_data) {
    int idx = find_download_index(GPOINTER_TO_UINT(user_data));
    if(idx != -1) downloads_list[idx].status = g_error_matches(error, WEBKIT_DOWNLOAD_ERROR, WEBKIT_DOWNLOAD_ERROR_CANCELLED_BY_USER) ? "Cancelled" : "Failed";
}

void on_download_started(WebKitWebContext *context, WebKitDownload *download, gpointer user_data) {
    const char* download_dir = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    if (!download_dir) download_dir = g_get_home_dir();
    
    // FIX: Use webkit_uri_response_get_suggested_filename to get the actual filename (e.g., "file.zip")
    // instead of the full destination URI, and use 'const gchar*' to match the return type.
    WebKitURIResponse* resp = webkit_download_get_response(download);
    const gchar* suggested = webkit_uri_response_get_suggested_filename(resp);
    
    std::string filename = (suggested && strlen(suggested) > 0) ? suggested : "download";
    std::string full_path = std::string(download_dir) + "/" + filename;
    std::string file_uri = "file://" + full_path;
    
    webkit_download_set_destination(download, file_uri.c_str());
    
    DownloadItem item;
    item.id = global_download_id_counter++;
    item.filename = filename;
    item.url = webkit_uri_request_get_uri(webkit_download_get_request(download));
    item.progress = 0.0; item.status = "Starting..."; item.webkit_download = download;
    downloads_list.push_back(item);
    
    g_signal_connect(download, "received-data", G_CALLBACK(on_download_received_data), GUINT_TO_POINTER(item.id));
    g_signal_connect(download, "failed", G_CALLBACK(on_download_failed), GUINT_TO_POINTER(item.id));
    g_signal_connect(download, "finished", G_CALLBACK(on_download_finished), GUINT_TO_POINTER(item.id));
    
    if (global_downloads_btn) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(global_downloads_btn), TRUE);
    update_downloads_popup();
}