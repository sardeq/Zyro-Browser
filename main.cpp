#include <iostream>
#include <string>
#include <filesystem> 
#include <AppCore/AppCore.h>
#include <AppCore/Window.h>
#include <AppCore/Overlay.h>
#include <Ultralight/platform/Config.h>

using namespace ultralight;
namespace fs = std::filesystem;

const int UI_HEIGHT = 44; 

class ZyroBrowser : public AppListener,
                    public WindowListener,
                    public LoadListener,
                    public ViewListener { 
    RefPtr<App> app_;
    RefPtr<Window> window_;
    RefPtr<Overlay> ui_layer_;
    RefPtr<Overlay> web_layer_;

    double current_zoom_ = 1.0;

public:
    ZyroBrowser() {
        // --- STEP 1: CALCULATE ABSOLUTE PATHS ---
        fs::path basePath = fs::current_path();
        fs::path cacheDir = basePath / "cache";
        
        // Ensure cache exists
        if (!fs::exists(cacheDir)) fs::create_directories(cacheDir);

        std::cout << "--- PATH INFO ---" << std::endl;
        std::cout << "Cache Path: " << cacheDir << std::endl;

        Config config;
        Settings settings;
        
        // --- STEP 2: STORAGE CONFIG ---
        // We use the absolute path. On Linux, this is often required for SQLite to lock the database file.
        config.cache_path = cacheDir.string().c_str();

        // 3. File system path
        settings.file_system_path = ".";

        // Performance
        settings.force_cpu_renderer = false; 

        app_ = App::Create(settings, config);
        window_ = Window::Create(app_->main_monitor(), 1100, 800, false, kWindowFlags_Titled | kWindowFlags_Resizable);
        window_->SetTitle("Zyro Browser");

        web_layer_ = Overlay::Create(window_, 1100, 800 - UI_HEIGHT, 0, UI_HEIGHT);
        ui_layer_ = Overlay::Create(window_, 1100, UI_HEIGHT, 0, 0);

        ui_layer_->view()->LoadURL("file:///assets/app.html");
        
        // Load Google Login directly to test
        web_layer_->view()->LoadURL("https://accounts.google.com"); 

        app_->set_listener(this);
        window_->set_listener(this);
        ui_layer_->view()->set_load_listener(this);
        ui_layer_->view()->set_view_listener(this);
        web_layer_->view()->set_view_listener(this); 
        web_layer_->view()->set_load_listener(this);
    }

    void Run() { app_->Run(); }

    virtual void OnResize(ultralight::Window* window, uint32_t width, uint32_t height) override {
        if (ui_layer_) ui_layer_->Resize(width, UI_HEIGHT);
        if (web_layer_) web_layer_->Resize(width, height - UI_HEIGHT);
    }

    virtual RefPtr<View> OnCreateChildView(ultralight::View* caller, const String& opener_url, const String& target_url, bool is_popup, const IntRect& popup_rect) override {
        if (web_layer_) web_layer_->view()->LoadURL(target_url);
        return nullptr;
    }

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
                    if (action == "zoomIn" || action == "zoomOut" || action == "zoomReset") {
                        if (action == "zoomIn") current_zoom_ += 0.2;
                        if (action == "zoomOut") current_zoom_ -= 0.2;
                        if (action == "zoomReset") current_zoom_ = 1.0;
                        std::string zoomScript = "document.body.style.zoom = '" + std::to_string(current_zoom_) + "';";
                        web_layer_->view()->EvaluateScript(zoomScript.c_str());
                    }
                }
                ui_layer_->view()->Stop();
            }
        }
    }

    virtual void OnChangeURL(ultralight::View* caller, const String& url) override {
        if (caller == web_layer_->view()) {
            String js = "window.updateAddressBar('" + url + "');";
            ui_layer_->view()->EvaluateScript(js);
        }
    }

    virtual void OnFinishLoading(ultralight::View* caller, uint64_t frame_id, bool is_main_frame, const String& url) override {
        if (caller == web_layer_->view() && is_main_frame) {
            String js = "window.updateAddressBar('" + url + "');";
            ui_layer_->view()->EvaluateScript(js);
        }
    }

    virtual void OnUpdate() override {}
    virtual void OnClose(ultralight::Window* window) override { app_->Quit(); }
    
    virtual void OnDOMReady(ultralight::View* caller, uint64_t frame_id, bool is_main_frame, const String& url) override {
        if (caller == web_layer_->view() && is_main_frame) {
            
            // 1. Hide Automation (CRITICAL)
            caller->EvaluateScript("Object.defineProperty(navigator, 'webdriver', {get: () => undefined});");

            // 2. Linux Chrome User Agent (Matches your Arch Linux OS)
            // This is safer than Safari because it matches your system fonts and rendering behavior.
            caller->EvaluateScript("Object.defineProperty(navigator, 'userAgent', { get: function () { return 'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/114.0.0.0 Safari/537.36'; } });");

            // 3. Mock Plugins & Languages (Passes Google's 'Bot vs Human' heuristic)
            // Bots often have empty plugin lists. We inject fake ones.
            caller->EvaluateScript("Object.defineProperty(navigator, 'plugins', { get: () => [1, 2, 3] });");
            caller->EvaluateScript("Object.defineProperty(navigator, 'languages', { get: () => ['en-US', 'en', 'ar'] });");

            // 4. Font Fix (Arabic support)
            String fontFix = "var style = document.createElement('style');"
                             "style.innerHTML = 'body, p, span, div, input, textarea, h1, h2, h3 { font-family: system-ui, \"Noto Sans\", \"Noto Sans Arabic\", \"Arial\", sans-serif !important; }';"
                             "document.head.appendChild(style);";
            caller->EvaluateScript(fontFix);
        }
    }
};

int main() {
    ZyroBrowser browser;
    browser.Run();
    return 0;
}