import codecs

with codecs.open('C:/repo/easyTools/src/capture/CaptureOverlay.cpp', 'r', 'utf-8-sig') as f:
    cpp = f.read()

# 1. Update the 'C' key behavior
old_c_key = '''            if (wParam == 'C' && self->m_state == OverlayState::Selecting) {
                int cr, cg, cb;
                if (self->sampleScreenColor(self->m_currentCursor.x, self->m_currentCursor.y, cr, cg, cb)) {
                    easy::core::WinUtils::copyToClipboard(std::format("#{:02X}{:02X}{:02X}", cr, cg, cb));
                    self->m_loupeToastUntil = GetTickCount() + 1200;
                    self->invalidate();
                }
                return 0;
            }'''

new_c_key = '''            if (wParam == 'C' && self->m_state == OverlayState::Selecting) {
                int cr, cg, cb;
                if (self->sampleScreenColor(self->m_currentCursor.x, self->m_currentCursor.y, cr, cg, cb)) {
                    easy::core::WinUtils::copyToClipboard(std::format("#{:02X}{:02X}{:02X}", cr, cg, cb));
                    self->showToast(std::format(L"已复制颜色 #{:02X}{:02X}{:02X}", cr, cg, cb));
                }
                return 0;
            }'''

cpp = cpp.replace(old_c_key, new_c_key)

# 2. Add drawToast() inside render() before toolbar
cpp = cpp.replace('    // 工具栏（在选区底部中心）', '    // Global Toast\n    drawToast();\n\n    // 工具栏（在选区底部中心）')

# 3. Handle WM_TIMER
old_timer = '''        case WM_TIMER: {
            if (self && wParam == RENDER_TIMER_ID) {
                if (self->m_isFadingOut) {'''

new_timer = '''        case WM_TIMER: {
            if (!self) break;
            if (wParam == 1) { // Toast animation timer
                self->updateToastAnimation();
            }
            if (wParam == RENDER_TIMER_ID) {
                if (self->m_isFadingOut) {'''

cpp = cpp.replace(old_timer, new_timer)

# 4. Add the Toast implementation functions at the very end
new_funcs = '''
void CaptureOverlay::showToast(const std::wstring& msg) {
    m_toastMessage = msg;
    m_toastTime = GetTickCount64();
    m_toastAlpha = 0.0f;
    SetTimer(m_hwnd, 1, 16, nullptr);
    m_needsRender = true;
}

void CaptureOverlay::updateToastAnimation() {
    if (m_toastTime == 0) return;
    
    uint64_t elapsed = GetTickCount64() - m_toastTime;
    float newAlpha = 0.0f;
    
    if (elapsed < 200) { // Fade in
        newAlpha = elapsed / 200.0f;
    } else if (elapsed < 1500) { // Hold
        newAlpha = 1.0f;
    } else if (elapsed < 1800) { // Fade out
        newAlpha = 1.0f - (elapsed - 1500) / 300.0f;
    } else { // Done
        m_toastTime = 0;
        m_toastAlpha = 0.0f;
        KillTimer(m_hwnd, 1);
        m_needsRender = true;
        return;
    }
    
    // easeOutCubic
    if (elapsed < 200) {
        newAlpha = 1.0f - pow(1.0f - newAlpha, 3.0f);
    }
    
    if (abs(m_toastAlpha - newAlpha) > 0.01f) {
        m_toastAlpha = newAlpha;
        m_needsRender = true;
    }
}

void CaptureOverlay::drawToast() {
    if (m_toastAlpha <= 0.01f || m_toastMessage.empty() || !m_renderTarget) return;

    auto size = m_renderTarget->GetSize();
    
    // Float up animation
    uint64_t elapsed = GetTickCount64() - m_toastTime;
    float yOffset = 0.0f;
    if (elapsed < 200) {
        float t = elapsed / 200.0f;
        yOffset = 20.0f * (1.0f - (1.0f - pow(1.0f - t, 3.0f))); // easeOut float up
    }
    
    ComPtr<IDWriteTextLayout> tl;
    m_dwriteFactory->CreateTextLayout(m_toastMessage.c_str(), (UINT32)m_toastMessage.length(), m_infoTextFormat.Get(), size.width, 100, &tl);
    
    DWRITE_TEXT_METRICS metrics;
    tl->GetMetrics(&metrics);
    
    float paddingX = 24.0f;
    float paddingY = 12.0f;
    float tw = metrics.width + paddingX * 2;
    float th = metrics.height + paddingY * 2;
    
    float tx = (size.width - tw) / 2.0f;
    float ty = size.height - 100.0f - th + yOffset;
    
    D2D1_RECT_F bgRect = D2D1::RectF(tx, ty, tx + tw, ty + th);
    
    // Glass Background
    ComPtr<ID2D1SolidColorBrush> bgBrush, borderBrush;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.07f, 0.07f, 0.10f, 0.75f * m_toastAlpha), bgBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.15f * m_toastAlpha), borderBrush.GetAddressOf());
    
    D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(bgRect, th/2.0f, th/2.0f);
    if (bgBrush) m_renderTarget->FillRoundedRectangle(&rrect, bgBrush.Get());
    if (borderBrush) m_renderTarget->DrawRoundedRectangle(&rrect, borderBrush.Get(), 1.0f);
    
    ComPtr<ID2D1SolidColorBrush> textBrush;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, m_toastAlpha), textBrush.GetAddressOf());
    
    if (textBrush) {
        m_renderTarget->DrawTextLayout(D2D1::Point2F(tx + paddingX, ty + paddingY), tl.Get(), textBrush.Get());
    }
}
'''

cpp = cpp.replace('} // namespace easy::capture', new_funcs + '\n} // namespace easy::capture')

with codecs.open('C:/repo/easyTools/src/capture/CaptureOverlay.cpp', 'w', 'utf-8-sig') as f:
    f.write(cpp)

print("Updated CaptureOverlay.cpp")
