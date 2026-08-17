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

    /// 预热搜索窗口 WebView2 渲染环境（后台静默就绪，使用户按下快捷键时 0 毫秒瞬间呼出）
    void preload(HINSTANCE hInstance);

    void show(HINSTANCE hInstance);
    void hide();
    bool isVisible() const;
    void destroy();
    void setWindowSize(int baseWidth, int baseHeight);
    std::pair<int, int> getWindowSize() const;

private:
    SearchWindow() = default;
    ~SearchWindow() { destroy(); }
    SearchWindow(const SearchWindow&) = delete;
    SearchWindow& operator=(const SearchWindow&) = delete;

    bool createWindow(HINSTANCE hInstance);
    void initializeWebView2();
    void updatePlacement();

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> m_environment;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> m_controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> m_webView;

    std::atomic<bool> m_visible{false};
    std::atomic<bool> m_webViewReady{false};
    bool m_updatingPlacement = false;
    uint64_t m_showTimeTick{0};
    std::atomic<uint64_t> m_generation{0};
};

} // namespace easy::ui
