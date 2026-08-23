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
