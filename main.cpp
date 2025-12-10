#include <iostream>
#include <string>
#include <AppCore/AppCore.h>
#include <AppCore/Window.h>
#include <AppCore/Overlay.h>
#include <Ultralight/platform/Config.h>

using namespace ultralight;

const int UI_HEIGHT = 70; 

// ADDED: Inherit from ViewListener to handle popups/cursors
class ZyroBrowser : public AppListener,
                    public WindowListener,
                    public LoadListener,
                    public ViewListener { 
    RefPtr<App> app_;
    RefPtr<Window> window_;
    
    RefPtr<Overlay> ui_layer_;
    RefPtr<Overlay> web_layer_;

public:
    ZyroBrowser() {
        Config config;
        Settings settings;
        
        // --- FIX 1: MEMORY & PERSISTENCE ---
        // Save cookies/cache to disk so logins are remembered
        config.cache_path = "cache/"; 
        
        // --- FIX 2: GOOGLE COMPATIBILITY ---
        // Pretend to be a standard Chrome browser on Linux
        //settings.user_agent = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/90.0.4430.93 Safari/537.36";

        //settings.force_cpu_renderer = true;
        settings.force_cpu_renderer = false;
        settings.file_system_path = ".";

        app_ = App::Create(settings, config);
        window_ = Window::Create(app_->main_monitor(), 1024, 768, false, kWindowFlags_Titled | kWindowFlags_Resizable);
        window_->SetTitle("Zyro Browser");

        web_layer_ = Overlay::Create(window_, 1024, 768 - UI_HEIGHT, 0, UI_HEIGHT);
        ui_layer_ = Overlay::Create(window_, 1024, UI_HEIGHT, 0, 0);

        String ua = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/90.0.4430.93 Safari/537.36";
        //web_layer_->view()->SetUserAgent(ua);
        
        ui_layer_->view()->LoadURL("file:///assets/app.html");
        web_layer_->view()->LoadURL("https://www.google.com"); 

        // Listeners
        app_->set_listener(this);
        window_->set_listener(this);
        ui_layer_->view()->set_load_listener(this);
        
        // --- FIX 3: ATTACH VIEW LISTENER ---
        // This lets us handle popups and cursors
        web_layer_->view()->set_view_listener(this);
        web_layer_->view()->set_load_listener(this); // Optional: to track URL changes
    }

    void Run() { app_->Run(); }

    virtual void OnResize(ultralight::Window* window, uint32_t width, uint32_t height) override {
        if (ui_layer_) ui_layer_->Resize(width, UI_HEIGHT);
        if (web_layer_) web_layer_->Resize(width, height - UI_HEIGHT);
    }

    // --- POPUP HANDLER (Prevents Freezing) ---
    virtual RefPtr<View> OnCreateChildView(ultralight::View* caller, const String& opener_url, const String& target_url, bool is_popup, const IntRect& popup_rect) override {
        // If Google tries to open a popup, we force it to open in the CURRENT window instead.
        // This isn't perfect (you lose the old page), but it prevents the freeze.
        if (web_layer_) {
            web_layer_->view()->LoadURL(target_url);
        }
        return nullptr; // Return null because we handled it manually
    }

    // --- CURSOR HANDLER (Quality of Life) ---
    virtual void OnChangeCursor(ultralight::View* caller, Cursor cursor) override {
        if (window_) window_->SetCursor(cursor);
    }

    virtual void OnBeginLoading(ultralight::View* caller, uint64_t frame_id, bool is_main_frame, const String& url) override {
        std::string urlStr = url.utf8().data();

        if (caller == ui_layer_->view()) {
            if (urlStr.find("zyro://navigate/") == 0) {
                std::string targetUrl = urlStr.substr(16);
                if (web_layer_) web_layer_->view()->LoadURL(targetUrl.c_str());
                ui_layer_->view()->Stop();
            }
            if (urlStr.find("zyro://action/") == 0) {
                std::string action = urlStr.substr(14);
                if (web_layer_) {
                    if (action == "back") web_layer_->view()->GoBack();
                    if (action == "forward") web_layer_->view()->GoForward();
                    if (action == "refresh") web_layer_->view()->Reload();
                }
                ui_layer_->view()->Stop();
            }
        }
    }

    // Unused
    virtual void OnUpdate() override {}
    virtual void OnClose(ultralight::Window* window) override { app_->Quit(); }
    virtual void OnFinishLoading(ultralight::View* caller, uint64_t frame_id, bool is_main_frame, const String& url) override {}

    virtual void OnDOMReady(ultralight::View* caller, uint64_t frame_id, bool is_main_frame, const String& url) override {
        // Only run this on the Google layer
        if (caller == web_layer_->view() && is_main_frame) {
            // Force Google to think we are Chrome via JavaScript
            caller->EvaluateScript("Object.defineProperty(navigator, 'userAgent', { get: function () { return 'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/90.0.4430.93 Safari/537.36'; } });");
        }
    }
};

int main() {
    ZyroBrowser browser;
    browser.Run();
    return 0;
}