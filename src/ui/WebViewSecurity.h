#pragma once

#include "core/logger/Logger.h"
#include "ui/WebViewOriginPolicy.h"

#include <string>
#include <string_view>

#include <windows.h>
#include <shellapi.h>
#include <WebView2.h>
#include <wrl.h>
#include <nlohmann/json.hpp>

namespace easy::ui::web_security {

inline bool isTrustedMessageSource(ICoreWebView2WebMessageReceivedEventArgs* args) {
    if (!args) return false;
    LPWSTR sourceRaw = nullptr;
    if (FAILED(args->get_Source(&sourceRaw)) || !sourceRaw) return false;
    const std::wstring source(sourceRaw);
    CoTaskMemFree(sourceRaw);
    const bool trusted = isTrustedUri(source);
    if (!trusted) LOG_WARN("已拒绝非可信 WebView 消息来源");
    return trusted;
}

inline bool isBridgeMethodAllowed(std::string_view message, Surface surface) {
    if (!isBridgeMessageSizeAcceptable(message.size())) {
        LOG_WARN("已拒绝过大的 WebView bridge 消息: {} bytes", message.size());
        return false;
    }
    if (surface == Surface::Settings) return true;
    try {
        const auto request = nlohmann::json::parse(message);
        const std::string method = request.value("method", "");
        const auto starts = [&method](std::string_view prefix) {
            return method.starts_with(prefix);
        };
        bool allowed = false;
        switch (surface) {
            case Surface::Search:
                allowed = starts("search.") || starts("system.") ||
                          method == "capture.pinImageFile";
                break;
            case Surface::Tray:
                allowed = starts("tray.") || method == "plugins.getAll" ||
                          method == "gesture.getState";
                break;
            case Surface::QuickLook:
                allowed = starts("quicklook.");
                break;
            case Surface::Settings:
                allowed = true;
                break;
        }
        if (!allowed) LOG_WARN("已拒绝 WebView surface 越权调用: {}", method);
        return allowed;
    } catch (...) {
        return false;
    }
}

inline void applyNavigationPolicy(ICoreWebView2* webView) {
    if (!webView) return;
    using Microsoft::WRL::Callback;

    webView->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                LPWSTR uriRaw = nullptr;
                if (FAILED(args->get_Uri(&uriRaw)) || !uriRaw) {
                    args->put_Cancel(TRUE);
                    return S_OK;
                }
                const std::wstring uri(uriRaw);
                CoTaskMemFree(uriRaw);
                if (!isTrustedUri(uri)) {
                    LOG_WARN("已阻止 WebView 导航到非可信来源");
                    args->put_Cancel(TRUE);
                }
                return S_OK;
            }).Get(), nullptr);

    webView->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                LPWSTR uriRaw = nullptr;
                if (SUCCEEDED(args->get_Uri(&uriRaw)) && uriRaw) {
                    const std::wstring uri(uriRaw);
                    CoTaskMemFree(uriRaw);
                    // 新窗口一律交给系统浏览器；特权 WebView 不承载外部内容。
                    if (startsWithInsensitive(uri, L"https://") ||
                        startsWithInsensitive(uri, L"http://")) {
                        ShellExecuteW(nullptr, L"open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    }
                }
                args->put_Handled(TRUE);
                return S_OK;
            }).Get(), nullptr);
}

}  // namespace easy::ui::web_security
