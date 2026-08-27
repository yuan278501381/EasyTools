/**
 * EasyTools - High Performance Windows Productivity Suite
 * 
 * Copyright (c) 2026 Yy1 (GitHub yuan278501381) <https://github.com/yuan278501381> & EasyTools contributors
 * 
 * Licensed under the MIT License.
 */

#include "DialogRibbonOverlay.h"
#include "PathMemoryManager.h"
#include "ExplorerTracker.h"
#include "DialogNavigator.h"
#include "core/logger/Logger.h"
#include "core/utils/WinUtils.h"
#include "core/utils/DpiUtils.h"
#include "core/utils/ThemeUtils.h"
#include "core/config/ConfigManager.h"
#include <algorithm>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;

namespace easy::dialog {

namespace {
const wchar_t* const RIBBON_WINDOW_CLASS = L"EasyTools_DialogRibbonOverlay";
constexpr UINT WM_RIBBON_UPDATE_POS = WM_USER + 101;
constexpr UINT WM_RIBBON_ATTACH     = WM_USER + 102;
constexpr UINT WM_RIBBON_HIDE       = WM_USER + 103;
}

DialogRibbonOverlay& DialogRibbonOverlay::instance() {
    static DialogRibbonOverlay s_instance;
    return s_instance;
}

DialogRibbonOverlay::DialogRibbonOverlay() = default;

DialogRibbonOverlay::~DialogRibbonOverlay() {
    cleanup();
}

bool DialogRibbonOverlay::init() {
    if (m_hwnd && IsWindow(m_hwnd)) return true;

    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = RIBBON_WINDOW_CLASS;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, m_d2dFactory.GetAddressOf());
    DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf())
    );

    if (m_dwriteFactory) {
        m_dwriteFactory->CreateTextFormat(
            L"Microsoft YaHei UI",
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            12.0f,
            L"zh-CN",
            m_textFormat.GetAddressOf()
        );
        m_dwriteFactory->CreateTextFormat(
            L"Microsoft YaHei UI",
            nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            12.0f,
            L"zh-CN",
            m_boldFormat.GetAddressOf()
        );
    }

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        RIBBON_WINDOW_CLASS,
        L"EasyTools Dialog Ribbon",
        WS_POPUP,
        0, 0, m_width, m_height,
        nullptr, nullptr, GetModuleHandleW(nullptr), this
    );

    if (!m_hwnd) {
        LOG_ERROR("创建 DialogRibbonOverlay 窗口失败");
        return false;
    }

    HDC screenDC = GetDC(nullptr);
    m_memDC = CreateCompatibleDC(screenDC);
    ReleaseDC(nullptr, screenDC);

    return true;
}

void DialogRibbonOverlay::cleanup() {
    if (m_hwnd && IsWindow(m_hwnd)) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_memDC) {
        if (m_oldBitmap) {
            SelectObject(m_memDC, m_oldBitmap);
            m_oldBitmap = nullptr;
        }
        if (m_memBitmap) {
            DeleteObject(m_memBitmap);
            m_memBitmap = nullptr;
        }
        DeleteDC(m_memDC);
        m_memDC = nullptr;
    }
    m_renderTarget.Reset();
    m_textFormat.Reset();
    m_boldFormat.Reset();
    m_dwriteFactory.Reset();
    m_d2dFactory.Reset();

    // 触发冷路径物理内存修剪
    easy::core::WinUtils::trimWorkingSet();
}

void DialogRibbonOverlay::attachToDialog(HWND dialogHwnd, const std::string& processName) {
    if (!dialogHwnd || !IsWindow(dialogHwnd)) return;
    if (!PathMemoryManager::instance().isRibbonEnabled()) return;

    wchar_t className[64]{};
    if (GetClassNameW(dialogHwnd, className, static_cast<int>(std::size(className))) <= 0 ||
        wcscmp(className, L"#32770") != 0) {
        return;
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(dialogHwnd, &processId);
    if (processId == 0) return;

    if (!m_hwnd || !IsWindow(m_hwnd)) {
        if (!init()) return;
    }

    {
        std::lock_guard lock(m_mutex);
        m_targetDialog = dialogHwnd;
        m_targetProcessId = processId;
        m_targetProcess = processName;
    }

    if (m_hwnd && IsWindow(m_hwnd)) {
        PostMessageW(m_hwnd, WM_RIBBON_ATTACH, 0, 0);
    }
}

void DialogRibbonOverlay::updatePosition() {
    if (m_hwnd && IsWindow(m_hwnd)) {
        PostMessageW(m_hwnd, WM_RIBBON_UPDATE_POS, 0, 0);
    }
}

void DialogRibbonOverlay::hide() {
    if (m_hwnd && IsWindow(m_hwnd)) {
        PostMessageW(m_hwnd, WM_RIBBON_HIDE, 0, 0);
    }
}

void DialogRibbonOverlay::doAttachToDialog() {
    // doUpdatePosition is the single authority that may show the overlay.
    // A queued stale ATTACH must never re-show it after validation failed.
    if (doUpdatePosition()) {
        if (m_hwnd && IsWindow(m_hwnd)) {
            SetTimer(m_hwnd, 1001, 80, nullptr);
        }
    }
}

void DialogRibbonOverlay::doHide() {
    std::lock_guard lock(m_mutex);
    doHideLocked();
}

void DialogRibbonOverlay::doHideLocked() {
    if (m_hwnd && IsWindow(m_hwnd)) {
        KillTimer(m_hwnd, 1001);
        ShowWindow(m_hwnd, SW_HIDE);
    }
    m_targetDialog = nullptr;
    m_targetProcessId = 0;
    m_targetProcess.clear();
    m_activeExplorerPath.clear();
    m_menuOpen = false;
}

bool DialogRibbonOverlay::doUpdatePosition() {
    std::lock_guard lock(m_mutex);
    try {
        if (!m_targetDialog || !IsWindow(m_targetDialog) || !m_hwnd ||
            m_targetProcessId == 0) {
            doHideLocked();
            return false;
        }

        wchar_t className[64]{};
        DWORD currentProcessId = 0;
        GetWindowThreadProcessId(m_targetDialog, &currentProcessId);
        if (currentProcessId != m_targetProcessId ||
            GetClassNameW(m_targetDialog, className, static_cast<int>(std::size(className))) <= 0 ||
            wcscmp(className, L"#32770") != 0) {
            // HWND values can be reused after a dialog is destroyed. PID plus
            // dialog class validation prevents following the replacement window.
            doHideLocked();
            return false;
        }
        const std::string currentProcess =
            easy::core::WinUtils::getProcessNameFromWindow(m_targetDialog);
        if (!m_targetProcess.empty() &&
            (currentProcess.empty() ||
             _stricmp(currentProcess.c_str(), m_targetProcess.c_str()) != 0)) {
            doHideLocked();
            return false;
        }

        // 如果下拉菜单正在弹出交互中，始终保持展示
        if (m_menuOpen) {
            return true;
        }

        // 1. 检查目标对话框是否最小化或已被隐藏
        if (IsIconic(m_targetDialog) || !IsWindowVisible(m_targetDialog)) {
            if (IsWindowVisible(m_hwnd)) ShowWindow(m_hwnd, SW_HIDE);
            return false;
        }

        // 检查前台激活状态：仅在用户切换到完全不相关的其他进程时隐藏，
        // 对话框内部子控件获取焦点、切换子窗口均稳定保持展示
        const HWND foreground = GetForegroundWindow();
        if (foreground) {
            DWORD fgPid = 0;
            GetWindowThreadProcessId(foreground, &fgPid);
            if (fgPid != m_targetProcessId && fgPid != GetCurrentProcessId()) {
                if (IsWindowVisible(m_hwnd)) ShowWindow(m_hwnd, SW_HIDE);
                return false;
            }
        }

        RECT dlgRect;
        if (!GetWindowRect(m_targetDialog, &dlgRect)) {
            doHideLocked();
            return false;
        }

        m_dpiScale = easy::core::dpi::scaleForWindow(m_targetDialog);
        updateButtonsLayout(m_dpiScale);

        int posX = 0;
        int posY = 0;
        std::string posConfig = PathMemoryManager::instance().getRibbonPosition();

        if (posConfig == "top-center") {
            posX = dlgRect.left + ((dlgRect.right - dlgRect.left) - m_width) / 2;
            posY = dlgRect.top + static_cast<int>(5.0f * m_dpiScale);
        } else {
            // top-right: 贴合在对话框右上角标题栏（位于系统最小化/关闭按钮左侧）
            posX = dlgRect.right - m_width - static_cast<int>(145.0f * m_dpiScale);
            posY = dlgRect.top + static_cast<int>(5.0f * m_dpiScale);
        }

        SetWindowPos(m_hwnd, HWND_TOPMOST, posX, posY, m_width, m_height,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        render();
        return true;
    } catch (const std::exception& e) {
        LOG_WARN("doUpdatePosition 异常: {}", e.what());
    } catch (...) {
        LOG_WARN("doUpdatePosition 未知异常");
    }
    doHideLocked();
    return false;
}

void DialogRibbonOverlay::updateButtonsLayout(float dpiScale) {
    m_buttons.clear();
    try {
        m_activeExplorerPath = ExplorerTracker::instance().getActiveExplorerPath();
    } catch (...) {
        m_activeExplorerPath.clear();
    }

    float paddingX = 8.0f * dpiScale;
    float currentX = paddingX;
    float btnH = 26.0f * dpiScale;
    float btnY = ((float)m_height - btnH) * 0.5f;

    // 1. 资源管理器同频胶囊
    if (PathMemoryManager::instance().isQuickSwitchEnabled() && !m_activeExplorerPath.empty()) {
        std::string folderName = m_activeExplorerPath;
        size_t lastSlash = folderName.find_last_of("\\/");
        if (lastSlash != std::string::npos && lastSlash + 1 < folderName.length()) {
            folderName = folderName.substr(lastSlash + 1);
        }
        if (folderName.empty()) folderName = m_activeExplorerPath;

        std::string label = folderName;
        float btnW = std::clamp(static_cast<float>(label.length()) * 8.5f * dpiScale + 24.0f * dpiScale,
                                80.0f * dpiScale, 180.0f * dpiScale);

        m_buttons.push_back({
            RibbonButtonRect::Type::ExplorerSync,
            D2D1::RectF(currentX, btnY, currentX + btnW, btnY + btnH),
            label,
            m_activeExplorerPath,
            false
        });
        currentX += btnW + 6.0f * dpiScale;
    }

    // 2. 最近使用
    try {
        auto recentList = PathMemoryManager::instance().getRecentPaths(5);
        std::string recentLabel = "最近(" + std::to_string(recentList.size()) + ") ▾";
        float recentW = 76.0f * dpiScale;
        m_buttons.push_back({
            RibbonButtonRect::Type::Recent,
            D2D1::RectF(currentX, btnY, currentX + recentW, btnY + btnH),
            recentLabel,
            "",
            false
        });
        currentX += recentW + 6.0f * dpiScale;
    } catch (...) {
    }

    // 3. 常用收藏
    std::string favLabel = "工作区 ▾";
    float favW = 68.0f * dpiScale;
    m_buttons.push_back({
        RibbonButtonRect::Type::Favorites,
        D2D1::RectF(currentX, btnY, currentX + favW, btnY + btnH),
        favLabel,
        "",
        false
    });
    currentX += favW + paddingX;

    m_width = static_cast<int>(currentX);
}

void DialogRibbonOverlay::render() {
    if (!m_hwnd || !m_memDC || m_width <= 0 || m_height <= 0) return;

    if (m_memBitmap && (m_renderedWidth != m_width || m_renderedHeight != m_height)) {
        if (m_oldBitmap) {
            SelectObject(m_memDC, m_oldBitmap);
            m_oldBitmap = nullptr;
        }
        DeleteObject(m_memBitmap);
        m_memBitmap = nullptr;
    }

    if (!m_memBitmap) {
        HDC screenDC = GetDC(nullptr);
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = m_width;
        bmi.bmiHeader.biHeight = -m_height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        m_memBitmap = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, screenDC);
        m_oldBitmap = static_cast<HBITMAP>(SelectObject(m_memDC, m_memBitmap));
        m_renderedWidth = m_width;
        m_renderedHeight = m_height;
    }

    if (!m_renderTarget && m_d2dFactory) {
        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            0.0f, 0.0f,
            D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE
        );
        m_d2dFactory->CreateDCRenderTarget(&rtProps, m_renderTarget.GetAddressOf());
    }

    if (!m_renderTarget) return;

    RECT rc = {0, 0, m_width, m_height};
    m_renderTarget->BindDC(m_memDC, &rc);
    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));

    // 绘制主体毛玻璃背景胶囊
    ComPtr<ID2D1SolidColorBrush> bgBrush;
    ComPtr<ID2D1SolidColorBrush> borderBrush;
    ComPtr<ID2D1SolidColorBrush> textBrush;
    ComPtr<ID2D1SolidColorBrush> highlightBrush;
    ComPtr<ID2D1SolidColorBrush> syncBgBrush;

    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.14f, 0.18f, 0.92f), bgBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.18f), borderBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.95f, 0.95f, 1.0f), textBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f), highlightBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.23f, 0.51f, 0.96f, 0.88f), syncBgBrush.GetAddressOf());

    D2D1_ROUNDED_RECT mainRRect = D2D1::RoundedRect(
        D2D1::RectF(0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height)),
        10.0f * m_dpiScale, 10.0f * m_dpiScale
    );

    if (bgBrush) m_renderTarget->FillRoundedRectangle(&mainRRect, bgBrush.Get());
    if (borderBrush) m_renderTarget->DrawRoundedRectangle(&mainRRect, borderBrush.Get(), 1.0f * m_dpiScale);

    // 绘制各个按钮
    for (const auto& btn : m_buttons) {
        D2D1_ROUNDED_RECT btnRRect = D2D1::RoundedRect(btn.rect, 6.0f * m_dpiScale, 6.0f * m_dpiScale);

        if (btn.type == RibbonButtonRect::Type::ExplorerSync && syncBgBrush) {
            syncBgBrush->SetOpacity(btn.hovered ? 1.0f : 0.85f);
            m_renderTarget->FillRoundedRectangle(&btnRRect, syncBgBrush.Get());
        } else if (btn.hovered && highlightBrush) {
            m_renderTarget->FillRoundedRectangle(&btnRRect, highlightBrush.Get());
        }

        std::wstring wText = easy::core::WinUtils::utf8ToWstring(btn.text);
        if (textBrush && m_textFormat) {
            auto format = (btn.type == RibbonButtonRect::Type::ExplorerSync && m_boldFormat)
                ? m_boldFormat.Get() : m_textFormat.Get();
            format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            m_renderTarget->DrawText(
                wText.c_str(), static_cast<UINT32>(wText.length()),
                format, btn.rect, textBrush.Get()
            );
        }
    }

    m_renderTarget->EndDraw();

    // 提交分层窗口
    HDC screenDC = GetDC(nullptr);
    POINT ptSrc = {0, 0};
    RECT winRect;
    GetWindowRect(m_hwnd, &winRect);
    POINT ptDst = {winRect.left, winRect.top};
    SIZE size = {m_width, m_height};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

    UpdateLayeredWindow(m_hwnd, screenDC, &ptDst, &size, m_memDC, &ptSrc, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screenDC);
}

void DialogRibbonOverlay::onMouseMove(int x, int y) {
    if (!m_trackingMouse) {
        TRACKMOUSEEVENT tme = {sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_hwnd, 0};
        TrackMouseEvent(&tme);
        m_trackingMouse = true;
    }

    bool needRepaint = false;
    for (auto& btn : m_buttons) {
        bool inside = (x >= btn.rect.left && x <= btn.rect.right &&
                       y >= btn.rect.top && y <= btn.rect.bottom);
        if (btn.hovered != inside) {
            btn.hovered = inside;
            needRepaint = true;
        }
    }

    if (needRepaint) {
        render();
    }
}

void DialogRibbonOverlay::onMouseLeave() {
    m_trackingMouse = false;
    bool needRepaint = false;
    for (auto& btn : m_buttons) {
        if (btn.hovered) {
            btn.hovered = false;
            needRepaint = true;
        }
    }
    if (needRepaint) render();
}

void DialogRibbonOverlay::onLButtonDown(int x, int y) {
    for (const auto& btn : m_buttons) {
        if (x >= btn.rect.left && x <= btn.rect.right &&
            y >= btn.rect.top && y <= btn.rect.bottom) {

            POINT screenPt = {static_cast<int>(btn.rect.left), static_cast<int>(btn.rect.bottom)};
            ClientToScreen(m_hwnd, &screenPt);

            if (btn.type == RibbonButtonRect::Type::ExplorerSync) {
                const HWND targetDialog = getTargetDialog();
                if (!btn.extraData.empty() && targetDialog) {
                    DialogNavigator::instance().navigateToFolder(targetDialog, btn.extraData);
                    updateButtonsLayout(m_dpiScale);
                    render();
                }
            } else if (btn.type == RibbonButtonRect::Type::Recent) {
                showRecentMenu(screenPt.x, screenPt.y + 4);
            } else if (btn.type == RibbonButtonRect::Type::Favorites) {
                showFavoritesMenu(screenPt.x, screenPt.y + 4);
            }
            break;
        }
    }
}

void DialogRibbonOverlay::showRecentMenu(int screenX, int screenY) {
    auto recentList = PathMemoryManager::instance().getRecentPaths(10);
    if (recentList.empty()) return;

    HMENU hMenu = CreatePopupMenu();
    for (size_t i = 0; i < recentList.size(); ++i) {
        std::wstring itemText = easy::core::WinUtils::utf8ToWstring(recentList[i]);
        AppendMenuW(hMenu, MF_STRING, static_cast<UINT_PTR>(1000 + i), itemText.c_str());
    }

    m_menuOpen = true;
    SetForegroundWindow(m_hwnd);
    int cmd = TrackPopupMenu(
        hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
        screenX, screenY, 0, m_hwnd, nullptr
    );
    DestroyMenu(hMenu);
    m_menuOpen = false;

    if (cmd >= 1000 && static_cast<size_t>(cmd - 1000) < recentList.size()) {
        std::string chosenPath = recentList[cmd - 1000];
        const HWND targetDialog = getTargetDialog();
        if (targetDialog) {
            DialogNavigator::instance().navigateToFolder(targetDialog, chosenPath);
            updateButtonsLayout(m_dpiScale);
            render();
        }
    }
}

void DialogRibbonOverlay::showFavoritesMenu(int screenX, int screenY) {
    auto favList = PathMemoryManager::instance().getFavorites();
    if (favList.empty()) return;

    HMENU hMenu = CreatePopupMenu();
    for (size_t i = 0; i < favList.size(); ++i) {
        std::wstring itemText = easy::core::WinUtils::utf8ToWstring(favList[i]);
        AppendMenuW(hMenu, MF_STRING, static_cast<UINT_PTR>(2000 + i), itemText.c_str());
    }

    m_menuOpen = true;
    SetForegroundWindow(m_hwnd);
    int cmd = TrackPopupMenu(
        hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
        screenX, screenY, 0, m_hwnd, nullptr
    );
    DestroyMenu(hMenu);
    m_menuOpen = false;

    if (cmd >= 2000 && static_cast<size_t>(cmd - 2000) < favList.size()) {
        std::string chosenPath = favList[cmd - 2000];
        const HWND targetDialog = getTargetDialog();
        if (targetDialog) {
            DialogNavigator::instance().navigateToFolder(targetDialog, chosenPath);
            updateButtonsLayout(m_dpiScale);
            render();
        }
    }
}

LRESULT CALLBACK DialogRibbonOverlay::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DialogRibbonOverlay* self = nullptr;
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<DialogRibbonOverlay*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<DialogRibbonOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        switch (msg) {
            case WM_RIBBON_UPDATE_POS:
            case WM_TIMER:
                self->doUpdatePosition();
                return 0;
            case WM_RIBBON_ATTACH:
                self->doAttachToDialog();
                return 0;
            case WM_RIBBON_HIDE:
                self->doHide();
                return 0;
            case WM_MOUSEMOVE:
                self->onMouseMove(LOWORD(lParam), HIWORD(lParam));
                return 0;
            case WM_MOUSELEAVE:
                self->onMouseLeave();
                return 0;
            case WM_LBUTTONDOWN:
                self->onLButtonDown(LOWORD(lParam), HIWORD(lParam));
                return 0;
            case WM_SETCURSOR:
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace easy::dialog
