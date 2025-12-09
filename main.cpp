#include <iostream>
#include <filesystem>
#include <string>
#include <AppCore/AppCore.h>
#include <AppCore/Window.h>
#include <AppCore/Overlay.h>
#include <AppCore/JSHelpers.h>
#include <Ultralight/platform/Config.h>

using namespace ultralight;

class ZyroBrowser : public AppListener,
                   public WindowListener,
                   public LoadListener {
    RefPtr<App> app_;
    RefPtr<Window> window_;
    RefPtr<Overlay> overlay_;

public:
    ZyroBrowser() {
        std::cout << "[1] Initializing Settings..." << std::endl;
        Config config;
        Settings settings;
        
        // Use the GPU renderer by default, but we might need to disable it if this crashes
        //config.use_gpu_driver = false; 

        std::cout << "[2] Creating App..." << std::endl;
        app_ = App::Create(settings, config);
        
        if (!app_) {
            std::cerr << "[CRITICAL] App::Create returned null! Check your GPU drivers or assets." << std::endl;
            exit(-1);
        }

        std::cout << "[3] Creating Window..." << std::endl;
        window_ = Window::Create(app_->main_monitor(), 800, 600, false, kWindowFlags_Titled);
        window_->SetTitle("Zyro Browser");
        
        std::cout << "[4] Creating Overlay..." << std::endl;
        overlay_ = Overlay::Create(window_, 800, 600, 0, 0);
        
        std::cout << "[5] Setting Listeners..." << std::endl;
        app_->set_listener(this);
        window_->set_listener(this);
        overlay_->view()->set_load_listener(this);
        
        std::cout << "[6] Loading URL..." << std::endl;
        overlay_->view()->LoadURL("https://google.com");
        
        std::cout << "[7] Initialization Complete!" << std::endl;
    }

    void Run() { 
        std::cout << "[8] Entering Run Loop..." << std::endl;
        app_->Run(); 
    }

    virtual void OnUpdate() override {}
    virtual void OnClose(ultralight::Window* window) override { app_->Quit(); }
    virtual void OnResize(ultralight::Window* window, uint32_t width, uint32_t height) override {
        if (overlay_) overlay_->Resize(width, height);
    }
    virtual void OnFinishLoading(ultralight::View* caller, uint64_t frame_id, bool is_main_frame, const String& url) override {}
    virtual void OnDOMReady(ultralight::View* caller, uint64_t frame_id, bool is_main_frame, const String& url) override {}
};

int main() {
    std::cout << "--- STARTING ZYRO BROWSER ---" << std::endl;
    ZyroBrowser browser;
    browser.Run();
    return 0;
}