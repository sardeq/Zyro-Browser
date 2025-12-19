#pragma once
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

void init_blocker();
void toggle_blocker();
void apply_blocker_to_view(WebKitWebView* view);