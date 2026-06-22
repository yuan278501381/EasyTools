#pragma once
#include <windows.h>
#include <wrl/client.h>
#include <string>
#include <atomic>

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

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> m_environment;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> m_controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> m_webView;

    std::atomic<bool> m_visible{false};
    bool m_webViewReady = false;
};

} // namespace easy::ui
