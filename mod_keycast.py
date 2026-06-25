import codecs

with codecs.open('C:/repo/easyTools/src/keycast/KeycastOverlay.cpp', 'r', 'utf-8-sig') as f:
    cpp = f.read()

# Change background color to glassmorphism
old_bg = 'm_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.5f), &m_brushBg);'
new_bg = 'm_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.07f, 0.07f, 0.10f, 0.74f), &m_brushBg);'
cpp = cpp.replace(old_bg, new_bg)

old_border = 'm_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.2f), &m_brushBorder);'
new_border = 'm_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.16f), &m_brushBorder);'
cpp = cpp.replace(old_border, new_border)

# Add border rendering
old_fill = '''        m_brushBg->SetOpacity(currentOpacity);
        m_renderTarget->FillRoundedRectangle(&rrect, m_brushBg.Get());'''

new_fill = '''        m_brushBg->SetOpacity(currentOpacity);
        m_renderTarget->FillRoundedRectangle(&rrect, m_brushBg.Get());
        if (m_brushBorder) {
            m_brushBorder->SetOpacity(currentOpacity);
            m_renderTarget->DrawRoundedRectangle(&rrect, m_brushBorder.Get(), 1.0f);
        }'''

if old_fill in cpp:
    cpp = cpp.replace(old_fill, new_fill)

with codecs.open('C:/repo/easyTools/src/keycast/KeycastOverlay.cpp', 'w', 'utf-8-sig') as f:
    f.write(cpp)

with codecs.open('C:/repo/easyTools/src/gesture/GestureTrailOverlay.cpp', 'r', 'utf-8-sig') as f:
    cpp2 = f.read()

old_bg2 = 'm_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.75f), m_textBgBrush.GetAddressOf());'
new_bg2 = 'm_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.07f, 0.07f, 0.10f, 0.8f), m_textBgBrush.GetAddressOf());'
cpp2 = cpp2.replace(old_bg2, new_bg2)

with codecs.open('C:/repo/easyTools/src/gesture/GestureTrailOverlay.cpp', 'w', 'utf-8-sig') as f:
    f.write(cpp2)

print("Updated plugins")
