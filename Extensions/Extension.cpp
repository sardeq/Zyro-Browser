#include <webkit2/webkit-web-extension.h>
#include <unistd.h> 
#include <string>

static void web_page_created_callback(WebKitWebExtension* extension, WebKitWebPage* web_page, gpointer user_data) {
    pid_t pid = getpid();
    
    std::string pid_str = std::to_string(pid);
    
    WebKitUserMessage* message = webkit_user_message_new("pid-update", g_variant_new_string(pid_str.c_str()));
    webkit_web_page_send_message_to_view(web_page, message, NULL, NULL, NULL);
}

extern "C" G_MODULE_EXPORT void webkit_web_extension_initialize(WebKitWebExtension* extension) {
    g_signal_connect(extension, "page-created", G_CALLBACK(web_page_created_callback), NULL);
}