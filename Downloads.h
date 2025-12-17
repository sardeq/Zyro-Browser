#pragma once
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

void update_downloads_popup();
void on_download_started(WebKitWebContext *context, WebKitDownload *download, gpointer user_data);