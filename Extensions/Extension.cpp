#include <webkit2/webkit-web-extension.h>
#include <unistd.h> 
#include <string>
#include <malloc.h>

static void send_pid(WebKitWebPage* web_page) {
    pid_t pid = getpid();
    std::string pid_str = std::to_string(pid);
    WebKitUserMessage* message = webkit_user_message_new("pid-update", g_variant_new_string(pid_str.c_str()));
    webkit_web_page_send_message_to_view(web_page, message, NULL, NULL, NULL);
}

static void document_loaded_callback(WebKitWebPage* web_page, gpointer user_data) {
    send_pid(web_page);
}

static void web_page_created_callback(WebKitWebExtension* extension, WebKitWebPage* web_page, gpointer user_data) {
    send_pid(web_page);
    
    g_signal_connect(web_page, "document-loaded", G_CALLBACK(document_loaded_callback), NULL);
}

static gboolean web_process_trim_timer(gpointer) {
    malloc_trim(0);
    return TRUE;
}

extern "C" G_MODULE_EXPORT void webkit_web_extension_initialize(WebKitWebExtension* extension) {
    g_signal_connect(extension, "page-created", G_CALLBACK(web_page_created_callback), NULL);
    g_timeout_add_seconds(10, web_process_trim_timer, NULL);
}