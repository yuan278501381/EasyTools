import codecs

with codecs.open('C:/repo/easyTools/src/capture/CaptureOverlay.h', 'r', 'utf-8-sig') as f:
    h = f.read()

new_fields = '''    // Global Toast
    std::wstring m_toastMessage;
    uint64_t m_toastTime = 0;
    float m_toastAlpha = 0.0f;
    void showToast(const std::wstring& msg);
    void drawToast();
    void updateToastAnimation();

    // 状态标记
    uint32_t m_loupeToastUntil = 0; // 保留旧的或替换
'''

if 'uint32_t m_loupeToastUntil' in h:
    h = h.replace('uint32_t m_loupeToastUntil = 0;', new_fields)

with codecs.open('C:/repo/easyTools/src/capture/CaptureOverlay.h', 'w', 'utf-8-sig') as f:
    f.write(h)

print("Updated CaptureOverlay.h")
