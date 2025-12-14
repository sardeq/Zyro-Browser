#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <string>

// Global pointers for ease of access in this simple example
WebKitWebView* web_view;
GtkEntry* url_bar;

// --- Helper: Load CSS Styles ---
void load_css() {
    GtkCssProvider* provider = gtk_css_provider_new();
    GFile* css_file = g_file_new_for_path("assets/style.css");
    gtk_css_provider_load_from_file(provider, css_file, NULL);
    
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(css_file);
}

// --- Helper: Update URL Bar when page changes ---
void update_url_bar(WebKitWebView* view, GtkEntry* entry) {
    const char* uri = webkit_web_view_get_uri(view);
    if (uri) gtk_entry_set_text(entry, uri);
}

// --- Callback: User pressed ENTER in URL bar ---
void on_url_activated(GtkEntry* entry, gpointer data) {
    std::string text = gtk_entry_get_text(entry);
    
    // Simple logic: If it has a space or no dot, treat as Google Search
    if (text.find(" ") != std::string::npos || text.find(".") == std::string::npos) {
        std::string search_url = "https://www.google.com/search?q=" + text;
        webkit_web_view_load_uri(web_view, search_url.c_str());
    } else {
        // If missing http/https, add it
        if (text.find("http") != 0) text = "https://" + text;
        webkit_web_view_load_uri(web_view, text.c_str());
    }
    
    // Remove focus from bar so you can scroll the page immediately
    gtk_widget_grab_focus(GTK_WIDGET(web_view));
}

static void activate(GtkApplication* app, gpointer user_data) {
    load_css();

    // 1. Main Window
    GtkWidget* window = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(window), 1200, 800);
    gtk_window_set_title(GTK_WINDOW(window), "Zyro Browser");

    // 2. Layout Containers
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_name(toolbar, "toolbar"); // Matches CSS "box#toolbar" logic
    gtk_style_context_add_class(gtk_widget_get_style_context(toolbar), "toolbar");

    // 3. WebKit View (The Engine)
    // Enable Sandbox & Local Storage
    WebKitWebContext* ctx = webkit_web_context_new_with_website_data_manager(
        webkit_website_data_manager_new(
            "base-cache-directory", "cache",
            "base-data-directory", "data",
            NULL)
    );
    web_view = WEBKIT_WEB_VIEW(webkit_web_view_new_with_context(ctx));

    // 4. Create UI Elements
    GtkWidget* btn_back = gtk_button_new_from_icon_name("go-previous-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget* btn_fwd = gtk_button_new_from_icon_name("go-next-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget* btn_refresh = gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
    url_bar = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(url_bar, "Search Google or type a URL");

    // 5. Pack Toolbar
    gtk_box_pack_start(GTK_BOX(toolbar), btn_back, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_fwd, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), GTK_WIDGET(url_bar), TRUE, TRUE, 5); // URL bar expands

    // 6. Pack Main Layout
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(web_view), TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    // 7. Connect Signals (Interactivity)
    // Connect Back/Forward/Refresh buttons directly to WebKit slots
    g_signal_connect_swapped(btn_back, "clicked", G_CALLBACK(webkit_web_view_go_back), web_view);
    g_signal_connect_swapped(btn_fwd, "clicked", G_CALLBACK(webkit_web_view_go_forward), web_view);
    g_signal_connect_swapped(btn_refresh, "clicked", G_CALLBACK(webkit_web_view_reload), web_view);
    
    // Connect URL Bar
    g_signal_connect(url_bar, "activate", G_CALLBACK(on_url_activated), NULL);

    // Connect Page Load to Update URL Bar
    g_signal_connect(web_view, "load-changed", G_CALLBACK(+[](WebKitWebView* v, WebKitLoadEvent e, gpointer d){
        if(e == WEBKIT_LOAD_COMMITTED) update_url_bar(v, url_bar);
    }), NULL);

    // 8. Launch
    gtk_widget_show_all(window);
    
    // Load Homepage (Your app.html or Google)
    // To load your local HTML file as the "New Tab" page:
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        std::string home_path = std::string("file://") + cwd + "/assets/app.html";
        // Uncomment the line below to use your app.html as home
        // webkit_web_view_load_uri(web_view, home_path.c_str());
        
        // For now, let's load Google to test login/speed immediately:
        webkit_web_view_load_uri(web_view, "https://google.com");
    }
}

int main(int argc, char** argv) {
    GtkApplication* app = gtk_application_new("org.zyro.browser", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}