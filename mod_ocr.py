import codecs

with codecs.open('C:/repo/easyTools/src/ocr/OcrResultWindow.h', 'r', 'utf-8-sig') as f:
    h = f.read()

if 'float m_currentAlpha = 0.0f;' not in h:
    h = h.replace('bool m_hoverClose = false;', 'bool m_hoverClose = false;\n    float m_currentAlpha = 0.0f;\n    uint64_t m_showTime = 0;')

with codecs.open('C:/repo/easyTools/src/ocr/OcrResultWindow.h', 'w', 'utf-8-sig') as f:
    f.write(h)

with codecs.open('C:/repo/easyTools/src/ocr/OcrResultWindow.cpp', 'r', 'utf-8-sig') as f:
    cpp = f.read()

# Update brush colors for glassmorphism
cpp = cpp.replace('0.1f, 0.1f, 0.1f, 0.85f', '0.07f, 0.07f, 0.10f, 0.75f')
cpp = cpp.replace('0.2f, 0.5f, 0.9f, 0.8f', '1.0f, 1.0f, 1.0f, 0.12f')
cpp = cpp.replace('0.3f, 0.6f, 1.0f, 1.0f', '1.0f, 1.0f, 1.0f, 0.22f')

# Show Result initialization for animation
old_show = '''    m_copiedTime = 0;

    std::wstring wtext = easy::core::WinUtils::utf8ToWstring(m_text);'''

new_show = '''    m_copiedTime = 0;
    m_showTime = GetTickCount64();
    m_currentAlpha = 0.0f;

    std::wstring wtext = easy::core::WinUtils::utf8ToWstring(m_text);'''

if old_show in cpp:
    cpp = cpp.replace(old_show, new_show)

# Blend alpha
old_blend = '''BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};'''
new_blend = '''BLENDFUNCTION blend = {AC_SRC_OVER, 0, static_cast<BYTE>(m_currentAlpha), AC_SRC_ALPHA};'''
if old_blend in cpp:
    cpp = cpp.replace(old_blend, new_blend)

# Timer animation
old_timer = '''    case WM_TIMER:
        if (wParam == 1 && self.m_copiedTime > 0) {
            if (GetTickCount64() - self.m_copiedTime > 2000) {
                self.m_copiedTime = 0;
                self.render();
            }
        }
        return 0;'''

new_timer = '''    case WM_TIMER:
        if (wParam == 1) {
            bool needsRender = false;
            if (self.m_copiedTime > 0 && GetTickCount64() - self.m_copiedTime > 2000) {
                self.m_copiedTime = 0;
                needsRender = true;
            }
            if (self.m_currentAlpha < 255.0f) {
                float dt = (GetTickCount64() - self.m_showTime) / 150.0f;
                if (dt > 1.0f) dt = 1.0f;
                self.m_currentAlpha = 255.0f * (1.0f - pow(1.0f - dt, 3.0f)); // cubic ease out
                needsRender = true;
            }
            if (needsRender) self.render();
        }
        return 0;'''

if old_timer in cpp:
    cpp = cpp.replace(old_timer, new_timer)

with codecs.open('C:/repo/easyTools/src/ocr/OcrResultWindow.cpp', 'w', 'utf-8-sig') as f:
    f.write(cpp)

print("Updated OcrResultWindow.cpp")
