import codecs

# Fix PinWindow.h
with codecs.open('C:/repo/easyTools/src/capture/PinWindow.h', 'r', 'utf-8-sig') as f:
    h = f.read()

if 'D2D1_RECT_F m_toolbarRect;' not in h:
    h = h.replace('D2D1_RECT_F m_btnSaveRect = {};', 'D2D1_RECT_F m_toolbarRect = {};\n    D2D1_RECT_F m_btnSaveRect = {};')

with codecs.open('C:/repo/easyTools/src/capture/PinWindow.h', 'w', 'utf-8-sig') as f:
    f.write(h)

# Fix PinWindow.cpp
with codecs.open('C:/repo/easyTools/src/capture/PinWindow.cpp', 'r', 'utf-8-sig') as f:
    cpp = f.read()

cpp = cpp.replace('easy::core::WinUtils::copyToClipboard(self->m_sourceImage);', 'copyImageToClipboard(self->m_sourceImage);')

with codecs.open('C:/repo/easyTools/src/capture/PinWindow.cpp', 'w', 'utf-8-sig') as f:
    f.write(cpp)

print("Fixed compile errors")
