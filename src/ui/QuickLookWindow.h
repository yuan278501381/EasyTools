#pragma once
#include <windows.h>
#include <wrl/client.h>
#include <string>
#include <atomic>
#include <cstdint>
#include <filesystem>

struct ICoreWebView2;
struct ICoreWebView2Controller;
struct ICoreWebView2Environment;

namespace easy::ui {

class QuickLookWindow {
public:
    static QuickLookWindow& instance();

    void show(const std::wstring& filePath, HINSTANCE hInstance = nullptr);
    void previewFile(const std::wstring& filePath);
    void hide();
    bool isVisible() const;
    void destroy();

    std::wstring currentFilePath() const { return m_currentFilePath; }

private:
    QuickLookWindow() = default;
    ~QuickLookWindow() = default;

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
    std::atomic<uint64_t> m_generation{0};
    std::wstring m_currentFilePath;
    std::wstring m_pendingFilePath;
};

} // namespace easy::ui
