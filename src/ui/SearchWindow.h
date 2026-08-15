#pragma once
#include <windows.h>
#include <wrl/client.h>
#include <string>
#include <atomic>
#include <cstdint>

struct ICoreWebView2;
struct ICoreWebView2Controller;
struct ICoreWebView2Environment;

namespace easy::ui {

class SearchWindow {
public:
    static SearchWindow& instance();

    void show(HINSTANCE hInstance);
    void hide();
    bool isVisible() const;
    void destroy();

private:
    SearchWindow() = default;
    ~SearchWindow() = default;

    bool createWindow(HINSTANCE hInstance);
    void initializeWebView2();
    void updatePlacement();

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> m_environment;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> m_controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> m_webView;

    std::atomic<bool> m_visible{false};
    bool m_webViewReady = false;
    bool m_updatingPlacement = false;
    std::atomic<uint64_t> m_generation{0};
};

} // namespace easy::ui
