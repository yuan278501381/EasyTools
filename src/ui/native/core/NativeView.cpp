#include "ui/native/core/NativeView.h"
#include "ui/native/window/NativeWindowHost.h"

namespace tools3000::ui::native {

NativeView::NativeView() = default;
NativeView::~NativeView() = default;

void NativeView::onAttachedToHost(NativeWindowHost* host) {
    m_host = host;
    m_isAttached = (host != nullptr);
    setWindowHost(host);
}

void NativeView::onDetachedFromHost() {
    m_host = nullptr;
    m_isAttached = false;
    setWindowHost(nullptr);
}

void NativeView::onActivated() {}
void NativeView::onDeactivated() {}

void NativeView::onWindowSizeChanged(int w, int h, bool isMaximized) {
    (void)w;
    (void)h;
    (void)isMaximized;
}

bool NativeView::handleWindowMessage(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT& result) {
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)result;
    return false;
}

void NativeView::requestHostLayout() {
    if (m_host) {
        m_host->requestLayout();
    } else {
        markNeedsLayout();
    }
}

void NativeView::requestHostPaint() {
    if (m_host) {
        m_host->requestPaint();
    } else {
        markNeedsPaint();
    }
}

void NativeView::closeHostWindow() {
    if (m_host) {
        m_host->close();
    }
}

} // namespace tools3000::ui::native
