#pragma once
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <string>

void create_window(WebKitWebContext* ctx);
GtkWidget* create_new_tab(GtkWidget* win, const std::string& url, WebKitWebContext* context, WebKitWebView* related_view = nullptr, bool switch_to = true);
GtkNotebook* get_notebook(GtkWidget* win);
WebKitWebView* get_active_webview(GtkWidget* win);
void run_js(WebKitWebView* view, const std::string& script);
void refresh_bookmarks_bar(GtkWidget* bar);
void show_search_overlay(GtkWidget* parent_win);