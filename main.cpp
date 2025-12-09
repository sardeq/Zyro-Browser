#include <iostream>
#include <filesystem>
#include <string>
#include <AppCore/AppCore.h>
#include <AppCore/Window.h>
#include <AppCore/Overlay.h>
#include <AppCore/JSHelpers.h>
#include <Ultralight/platform/Config.h>

using namespace ultralight;
namespace fs = std::filesystem;

class ZyroBrowser : public AppListener,
                   public WindowListener,
                   public LoadListener {
    RefPtr<App> app_;
    RefPtr<Window> window_;
    RefPtr<Overlay> overlay_;

public:
    ZyroBrowser() {
        Config config;
        
        fs::path cwd = fs::current_path();
        
        std::string path_str = cwd.string();
        if (path_str.back() != '/') {
            path_str += "/";
        }

        path_str += "resources/";
        
        config.resource_path_prefix = path_str.c_str(); 
        
        std::cout << "Root path set to: " << path_str << std::endl;
        std::cout << "Engine will look for: " << path_str << "resources/icudt67l.dat" << std::endl;

        Settings settings;
        app_ = App::Create(settings, config);
        
        window_ = Window::Create(app_->main_monitor(), 800, 600, false, kWindowFlags_Titled);
        window_->SetTitle("Zyro Browser");
        
        overlay_ = Overlay::Create(window_, 800, 600, 0, 0);
        
        app_->set_listener(this);
        window_->set_listener(this);
        overlay_->view()->set_load_listener(this);
        
        overlay_->view()->LoadURL("https://google.com");
    }

    void Run() { app_->Run(); }

    virtual void OnUpdate() override {}
    virtual void OnClose(ultralight::Window* window) override { app_->Quit(); }
    virtual void OnResize(ultralight::Window* window, uint32_t width, uint32_t height) override {
        overlay_->Resize(width, height);
    }
    virtual void OnFinishLoading(ultralight::View* caller, uint64_t frame_id, bool is_main_frame, const String& url) override {}
    virtual void OnDOMReady(ultralight::View* caller, uint64_t frame_id, bool is_main_frame, const String& url) override {}
};

int main() {
    ZyroBrowser browser;
    browser.Run();
    return 0;
}