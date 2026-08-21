#include "ui/WebViewSuspend.h"

#include "core/logger/Logger.h"

#include <windows.h>
#include <wrl/client.h>
#include <wrl/event.h>

// WebView2's MIDL-generated C++ declarations require the COM interface
// macros supplied by WRL/unknwn before the SDK header is parsed.
#include <WebView2.h>

#include <atomic>
#include <string>

namespace easy::ui {

struct WebViewSuspendController::State {
    std::atomic<bool> suspendDesired{false};
    std::atomic<bool> abandoned{false};
};

WebViewSuspendController::WebViewSuspendController()
    : m_state(std::make_shared<State>()) {}

WebViewSuspendController::~WebViewSuspendController() {
    abandon();
}

HRESULT WebViewSuspendController::requestSuspend(ICoreWebView2* webView,
                                                  const char* surface) noexcept {
    if (!webView) return E_POINTER;
    if (!m_state) return E_UNEXPECTED;
    m_state->suspendDesired.store(true, std::memory_order_release);
    Microsoft::WRL::ComPtr<ICoreWebView2_3> webView3;
    const HRESULT query = webView->QueryInterface(IID_PPV_ARGS(&webView3));
    if (FAILED(query) || !webView3) return query;
    const std::string surfaceName = surface ? surface : "unknown";
    const std::shared_ptr<State> state = m_state;
    const HRESULT result = webView3->TrySuspend(
        Microsoft::WRL::Callback<ICoreWebView2TrySuspendCompletedHandler>(
            [state, webView3, surfaceName](HRESULT error, BOOL successful) -> HRESULT {
                const auto outcome = classifyWebViewSuspendCompletion(error, successful);
                switch (outcome) {
                    case WebViewSuspendOutcome::Suspended:
                        LOG_DEBUG("WebView suspended: {}", surfaceName);
                        break;
                    case WebViewSuspendOutcome::Refused:
                        LOG_DEBUG("WebView suspend was declined by runtime: {}", surfaceName);
                        break;
                    case WebViewSuspendOutcome::Failed:
                        LOG_WARN("WebView suspend failed: {}, hr=0x{:08X}", surfaceName,
                                 static_cast<unsigned long>(error));
                        break;
                }
                // The WebView runtime may complete TrySuspend after the owner
                // has already shown the surface again. Resume in that exact
                // case so visible content cannot remain frozen. `webView3` is
                // retained only until this callback returns; no HWND/window is
                // captured, so destruction remains safe.
                if (shouldResumeAfterSuspendCompletion(
                        outcome,
                        state->suspendDesired.load(std::memory_order_acquire),
                        state->abandoned.load(std::memory_order_acquire))) {
                    const HRESULT resumeResult = webView3->Resume();
                    if (FAILED(resumeResult)) {
                        LOG_WARN("WebView late Resume failed: {}, hr=0x{:08X}", surfaceName,
                                 static_cast<unsigned long>(resumeResult));
                    }
                }
                return S_OK;
            }).Get());
    if (FAILED(result)) {
        LOG_WARN("WebView TrySuspend request failed: {}, hr=0x{:08X}", surfaceName,
                 static_cast<unsigned long>(result));
    }
    return result;
}

HRESULT WebViewSuspendController::resume(ICoreWebView2* webView, const char* surface) noexcept {
    if (!webView) return E_POINTER;
    if (!m_state) return E_UNEXPECTED;
    m_state->suspendDesired.store(false, std::memory_order_release);
    Microsoft::WRL::ComPtr<ICoreWebView2_3> webView3;
    const HRESULT query = webView->QueryInterface(IID_PPV_ARGS(&webView3));
    if (FAILED(query) || !webView3) return query;
    const HRESULT result = webView3->Resume();
    if (FAILED(result)) {
        LOG_WARN("WebView Resume failed: {}, hr=0x{:08X}", surface ? surface : "unknown",
                 static_cast<unsigned long>(result));
    }
    return result;
}

void WebViewSuspendController::reset() noexcept {
    try {
        m_state = std::make_shared<State>();
    } catch (...) {
        // Keep the old state abandoned if allocating a replacement fails. The
        // WebView remains usable; only best-effort deep suspension is skipped.
    }
}

void WebViewSuspendController::abandon() noexcept {
    if (!m_state) return;
    m_state->abandoned.store(true, std::memory_order_release);
    m_state->suspendDesired.store(false, std::memory_order_release);
}

}  // namespace easy::ui
