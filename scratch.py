import re

path = 'c:/repo/easyTools/src/main.cpp'
with open(path, 'r', encoding='utf-8') as f:
    lines = f.readlines()

new_lines = []
skip = False
for line in lines:
    if line.startswith('#include "ocr/OcrEngine.h"') or \
       line.startswith('#include "gesture/GestureEngine.h"') or \
       line.startswith('#include "gesture/BuiltinCommands.h"') or \
       line.startswith('#include "capture/ScreenCapture.h"') or \
       line.startswith('#include "capture/ScreenRecorder.h"') or \
       line.startswith('#include "capture/RecordingIndicator.h"') or \
       line.startswith('#include "capture/PinWindow.h"') or \
       line.startswith('#include "capture/ScrollCapture.h"') or \
       line.startswith('#include "capture/CaptureOverlay.h"') or \
       line.startswith('#include "gesture/MouseHook.h"'):
        continue

    if line.startswith('static void triggerScreenshot();'): continue
    if line.startswith('static void toggleRecording();'): continue
    if line.startswith('static void triggerOcrCapture();'): continue

    if line.startswith('// ─────────────────────────────────────────────────────────────────────────────'):
        if len(new_lines) > 0 and '截图 / 录屏 入口' in ''.join(lines[lines.index(line):lines.index(line)+3]):
            skip = True

    if skip:
        if line.startswith('// WinMain — 程序入口'):
            skip = False
        else:
            continue
            
    # Tray bindings
    if 'tray.onScreenshot([]() { triggerScreenshot(); });' in line:
        new_lines.append('    tray.onScreenshot([]() { easy::core::MessageBridge::instance().handleMessage(R"({"method":"capture.triggerScreenshot"})"); });\n')
        continue
    if 'tray.onRecording([]() { toggleRecording(); });' in line:
        new_lines.append('    tray.onRecording([]() { easy::core::MessageBridge::instance().handleMessage(R"({"method":"capture.toggleRecording"})"); });\n')
        continue
    if 'tray.onPauseGesture([]() {' in line:
        new_lines.append('    tray.onPauseGesture([]() { easy::core::MessageBridge::instance().handleMessage(R"({"method":"gesture.togglePause"})"); });\n')
        skip = True
        continue
    if skip and '});' in line: # ends the tray.onPauseGesture block
        skip = False
        continue

    # Hotkeys
    if '    // 3. 注册全局快捷键' in line:
        skip = True
        continue
    if skip and '    // 4. 注册手势引擎状态改变回调' in line:
        skip = False

    # Gesture and OCR and Capture start
    if '    easy::gesture::MouseHook::instance().install();' in line: continue
    if '    easy::gesture::GestureEngine::instance().start();' in line: continue
    if '    // 6. 初始化 OCR' in line: skip = True; continue
    if skip and '    // 7. 加载并初始化插件' in line: skip = False

    if '    // 5. 初始化截图/录屏' in line: skip = True; continue
    if skip and '    // 6. 初始化 OCR' in line: skip = False; continue

    # Shutdown
    if 'easy::capture::PinWindow::closeAll();' in line: continue
    if 'easy::capture::RecordingIndicator::instance().shutdown();' in line: continue
    if 'easy::capture::ScreenRecorder::instance().shutdown();' in line: continue
    if 'easy::capture::ScreenCapture::instance().shutdown();' in line: continue
    if 'easy::gesture::GestureEngine::instance().saveToConfig();' in line: continue
    if 'easy::gesture::GestureEngine::instance().stop();' in line: continue
    if 'easy::gesture::MouseHook::instance().uninstall();' in line: continue
    if 'easy::ocr::OcrEngine::instance().shutdown();' in line: continue

    new_lines.append(line)

with open(path, 'w', encoding='utf-8') as f:
    f.writelines(new_lines)
