import codecs

with codecs.open('C:/repo/easyTools/src/capture/PinWindow.h', 'r', 'utf-8-sig') as f:
    h = f.read()

new_fields = '''    bool m_focused = false;  // 选中的状态，显示边框，可按 Esc 等
    
    // Hover Toolbar
    bool m_isHovering = false;
    float m_hoverAlpha = 0.0f;
    uint64_t m_hoverTime = 0;
    D2D1_RECT_F m_btnSaveRect = {};
    D2D1_RECT_F m_btnCloseRect = {};
    bool m_hoverSave = false;
    bool m_hoverClose = false;
    
    void updateHoverAnimation();
    void drawHoverToolbar();'''

if 'bool m_isHovering' not in h:
    h = h.replace('bool m_focused = false;  // ѡ̬̽㣩ʾ߿򣬿ɰ Esc ', new_fields)

with codecs.open('C:/repo/easyTools/src/capture/PinWindow.h', 'w', 'utf-8-sig') as f:
    f.write(h)

print("Updated PinWindow.h")
