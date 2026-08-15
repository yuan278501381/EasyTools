#include "gesture/RadialMenuOverlay.h"
#include "gesture/BuiltinCommands.h"
#include "core/logger/Logger.h"
#include "core/ipc/MessageBridge.h"
#include "core/utils/WinUtils.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace easy::gesture {

static const wchar_t* CLASS_NAME = L"EasyTools_RadialMenu";
const int WINDOW_SIZE = 400; // 400x400
const UINT_PTR TIMER_ID_ANIMATION = 1001;

// 缓动函数
static float easeOutBack(float x) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    return 1.0f + c3 * std::pow(x - 1.0f, 3) + c1 * std::pow(x - 1.0f, 2);
}

static float easeOutQuad(float x) {
    return 1.0f - (1.0f - x) * (1.0f - x);
}

RadialMenuOverlay& RadialMenuOverlay::instance() {
    static RadialMenuOverlay inst;
    return inst;
}

RadialMenuOverlay::RadialMenuOverlay() {
    registerWindowClass();
}

RadialMenuOverlay::~RadialMenuOverlay() {
    hide();
    discardResources();
    if (m_hBitmap) {
        DeleteObject(m_hBitmap);
    }
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
    }
}

void RadialMenuOverlay::registerWindowClass() {
    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
}

void RadialMenuOverlay::setItems(const std::vector<RadialMenuItem>& items) {
    m_items = items;
    m_hoverRadii.resize(items.size(), 0.0f);
}

void RadialMenuOverlay::show(POINT centerPt) {
    if (m_items.empty()) return;

    m_centerPt = centerPt;
    m_hoverIndex = -1;
    m_popScale = 0.0f;
    m_showStartTime = GetTickCount();
    m_lastTickTime = m_showStartTime;
    std::fill(m_hoverRadii.begin(), m_hoverRadii.end(), 0.0f);

    if (!m_hwnd) {
        m_hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            CLASS_NAME, L"RadialMenu",
            WS_POPUP,
            centerPt.x - WINDOW_SIZE / 2, centerPt.y - WINDOW_SIZE / 2,
            WINDOW_SIZE, WINDOW_SIZE,
            nullptr, nullptr, GetModuleHandle(nullptr), this
        );
    }

    SetWindowPos(m_hwnd, HWND_TOPMOST, 
                 centerPt.x - WINDOW_SIZE / 2, centerPt.y - WINDOW_SIZE / 2, 
                 WINDOW_SIZE, WINDOW_SIZE, SWP_SHOWWINDOW);
    
    // 捕获鼠标
    SetCapture(m_hwnd);
    m_visible = true;
    m_timerId = SetTimer(m_hwnd, TIMER_ID_ANIMATION, 16, nullptr); // ~60 FPS
    
    render();
}

void RadialMenuOverlay::hide() {
    if (m_visible) {
        if (m_timerId) {
            KillTimer(m_hwnd, TIMER_ID_ANIMATION);
            m_timerId = 0;
        }
        ReleaseCapture();
        ShowWindow(m_hwnd, SW_HIDE);
        m_visible = false;
        m_hoverIndex = -1;
        discardResources();
        if (m_hBitmap) {
            DeleteObject(m_hBitmap);
            m_hBitmap = nullptr;
            m_bits = nullptr;
        }
        easy::core::WinUtils::trimWorkingSet();
    }
}

void RadialMenuOverlay::createResources() {
    if (!m_d2dFactory) {
        D2D1_FACTORY_OPTIONS options = {};
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options, (void**)m_d2dFactory.GetAddressOf());
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)m_dwriteFactory.GetAddressOf());
    }

    if (!m_renderTarget) {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            0, 0, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE, D2D1_FEATURE_LEVEL_DEFAULT
        );
        m_d2dFactory->CreateDCRenderTarget(&props, &m_renderTarget);

        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.1f, 0.13f, 0.85f), &m_bgBrush);
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.15f, 0.45f, 0.9f, 0.95f), &m_hoverBrush);
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &m_textBrush);
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.15f), &m_borderBrush);

        m_dwriteFactory->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"en-us", &m_textFormat
        );
        m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    if (!m_hBitmap) {
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = WINDOW_SIZE;
        bmi.bmiHeader.biHeight = -WINDOW_SIZE;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        m_hBitmap = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &m_bits, nullptr, 0);
    }
}

void RadialMenuOverlay::discardResources() {
    m_renderTarget.Reset();
    m_bgBrush.Reset();
    m_hoverBrush.Reset();
    m_textBrush.Reset();
    m_borderBrush.Reset();
    m_textFormat.Reset();
}

void RadialMenuOverlay::render() {
    if (!m_hwnd) return;
    createResources();

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HGDIOBJ hOldBmp = SelectObject(hdcMem, m_hBitmap);

    RECT rc = {0, 0, WINDOW_SIZE, WINDOW_SIZE};
    m_renderTarget->BindDC(hdcMem, &rc);
    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));

    // 缩放矩阵 (出场动画)
    float cx = WINDOW_SIZE / 2.0f;
    float cy = WINDOW_SIZE / 2.0f;
    m_renderTarget->SetTransform(D2D1::Matrix3x2F::Scale(m_popScale, m_popScale, D2D1::Point2F(cx, cy)));

    // 中心取消区域反馈
    if (m_hoverIndex == -1) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> centerBrush;
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.4f, 0.4f, 0.5f), &centerBrush); // 淡红取消区
        m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), m_radiusInner, m_radiusInner), centerBrush.Get());
    }

    // 绘制轮盘
    int numItems = static_cast<int>(m_items.size());
    if (numItems > 0) {
        float angleStep = 360.0f / numItems;
        float offsetAngle = -90.0f - (angleStep / 2.0f);

        for (int i = 0; i < numItems; ++i) {
            float startAngle = offsetAngle + i * angleStep;
            float endAngle = startAngle + angleStep;
            
            float startRad = startAngle * M_PI / 180.0f;
            float endRad = endAngle * M_PI / 180.0f;

            // 应用当前扇区的额外扩展半径 (悬停动画)
            float outerR = m_radiusOuter + m_hoverRadii[i];

            Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry;
            m_d2dFactory->CreatePathGeometry(&geometry);
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
            geometry->Open(&sink);

            D2D1_POINT_2F ptInnerStart = D2D1::Point2F(cx + m_radiusInner * cos(startRad), cy + m_radiusInner * sin(startRad));
            D2D1_POINT_2F ptOuterStart = D2D1::Point2F(cx + outerR * cos(startRad), cy + outerR * sin(startRad));
            D2D1_POINT_2F ptOuterEnd = D2D1::Point2F(cx + outerR * cos(endRad), cy + outerR * sin(endRad));
            D2D1_POINT_2F ptInnerEnd = D2D1::Point2F(cx + m_radiusInner * cos(endRad), cy + m_radiusInner * sin(endRad));

            sink->BeginFigure(ptInnerStart, D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(ptOuterStart);
            sink->AddArc(D2D1::ArcSegment(ptOuterEnd, D2D1::SizeF(outerR, outerR), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            sink->AddLine(ptInnerEnd);
            sink->AddArc(D2D1::ArcSegment(ptInnerStart, D2D1::SizeF(m_radiusInner, m_radiusInner), 0.0f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();

            // 填充背景
            if (i == m_hoverIndex) {
                m_renderTarget->FillGeometry(geometry.Get(), m_hoverBrush.Get());
            } else {
                m_renderTarget->FillGeometry(geometry.Get(), m_bgBrush.Get());
            }
            m_renderTarget->DrawGeometry(geometry.Get(), m_borderBrush.Get(), 1.5f);

            // 绘制文字
            float midRad = (startRad + endRad) / 2.0f;
            float textRadius = m_radiusInner + (outerR - m_radiusInner) / 2.0f;
            D2D1_RECT_F textRect = D2D1::RectF(
                cx + textRadius * cos(midRad) - 50,
                cy + textRadius * sin(midRad) - 20,
                cx + textRadius * cos(midRad) + 50,
                cy + textRadius * sin(midRad) + 20
            );
            
            std::wstring wLabel(m_items[i].label.begin(), m_items[i].label.end());
            m_renderTarget->DrawTextW(wLabel.c_str(), wLabel.length(), m_textFormat.Get(), textRect, m_textBrush.Get());
        }
    }

    m_renderTarget->EndDraw();

    updateLayeredWindow();

    SelectObject(hdcMem, hOldBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

void RadialMenuOverlay::updateLayeredWindow() {
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HGDIOBJ hOldBmp = SelectObject(hdcMem, m_hBitmap);

    POINT ptSrc = {0, 0};
    SIZE size = {WINDOW_SIZE, WINDOW_SIZE};
    
    // 透明度由 m_popScale 控制，产生淡入效果
    BYTE alpha = static_cast<BYTE>(std::clamp(m_popScale * 255.0f, 0.0f, 255.0f));
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};

    UpdateLayeredWindow(m_hwnd, hdcScreen, nullptr, &size, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(hdcMem, hOldBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

int RadialMenuOverlay::hitTest(POINT pt) {
    if (m_items.empty()) return -1;
    
    float dx = pt.x - (WINDOW_SIZE / 2.0f);
    float dy = pt.y - (WINDOW_SIZE / 2.0f);
    float dist = sqrt(dx * dx + dy * dy);

    // 如果鼠标位于内圆中心区，视为取消 (不选中任何菜单)
    if (dist <= m_radiusInner) {
        return -1;
    }

    // 容错度升级: 即使外圈无限长，只要超出了内圈，就锁定对应角度的扇区
    // 不再限制 dist > m_radiusOuter 会取消选中。
    float angle = atan2(dy, dx) * 180.0f / M_PI; // -180 ~ 180
    
    int numItems = static_cast<int>(m_items.size());
    float angleStep = 360.0f / numItems;
    float offsetAngle = -90.0f - (angleStep / 2.0f);

    float normAngle = fmod(angle - offsetAngle + 360.0f * 2.0f, 360.0f);
    int index = static_cast<int>(normAngle / angleStep) % numItems;
    return index;
}

void RadialMenuOverlay::executeAction(int index) {
    if (index >= 0 && index < static_cast<int>(m_items.size())) {
        const std::string cmd = m_items[index].command;
        LOG_INFO("RadialMenu: Execute command '{}'", cmd);

        // 兼容早期的可读字符串，并以数值索引作为当前稳定格式。
        static const std::unordered_map<std::string, BuiltinCommand> legacyCommands = {
            {"capture", BuiltinCommand::TakeScreenshot},
            {"search", BuiltinCommand::ToggleSearch},
            {"lock", BuiltinCommand::LockScreen},
            {"pin", BuiltinCommand::PasteAsPin},
            {"record", BuiltinCommand::StartRecording},
        };
        if (const auto legacy = legacyCommands.find(cmd); legacy != legacyCommands.end()) {
            BuiltinCommandDispatcher::instance().execute(legacy->second);
            return;
        }
        try {
            size_t consumed = 0;
            const int commandIndex = std::stoi(cmd, &consumed);
            if (consumed == cmd.size() && commandIndex >= 0 &&
                commandIndex <= static_cast<int>(BuiltinCommand::PasteAsPin)) {
                BuiltinCommandDispatcher::instance().execute(
                    static_cast<BuiltinCommand>(commandIndex));
                return;
            }
        } catch (...) {}

        // 仅为旧配置保留结构化 IPC 消息兼容；普通文本不再交给 JSON 解析器。
        if (!cmd.empty() && cmd.front() == '{') {
            easy::core::MessageBridge::instance().handleMessage(cmd);
        } else {
            LOG_WARN("RadialMenu: 忽略未知命令 '{}'", cmd);
        }
    }
}

LRESULT CALLBACK RadialMenuOverlay::windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    RadialMenuOverlay* pThis = nullptr;
    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<RadialMenuOverlay*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<RadialMenuOverlay*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (pThis) {
        return pThis->handleMessage(uMsg, wParam, lParam);
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT RadialMenuOverlay::handleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_TIMER: {
            if (wParam == TIMER_ID_ANIMATION) {
                bool needsRender = false;
                DWORD now = GetTickCount();
                float dt = (now - m_lastTickTime) / 1000.0f;
                m_lastTickTime = now;

                // 1. 出场动画 (0.0 -> 1.0)
                float elapsedShow = (now - m_showStartTime) / 1000.0f;
                if (elapsedShow < 0.2f) { // 200ms
                    float progress = std::clamp(elapsedShow / 0.2f, 0.0f, 1.0f);
                    m_popScale = easeOutBack(progress);
                    needsRender = true;
                } else if (m_popScale != 1.0f) {
                    m_popScale = 1.0f;
                    needsRender = true;
                }

                // 2. 悬停动画 (半径伸缩)
                for (size_t i = 0; i < m_items.size(); ++i) {
                    float targetRadii = (i == m_hoverIndex) ? 15.0f : 0.0f; // 悬停增加 15px 半径
                    if (std::abs(m_hoverRadii[i] - targetRadii) > 0.5f) {
                        m_hoverRadii[i] += (targetRadii - m_hoverRadii[i]) * 15.0f * dt; // 平滑差值
                        needsRender = true;
                    } else {
                        m_hoverRadii[i] = targetRadii;
                    }
                }

                if (needsRender) {
                    render();
                }
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            POINT pt;
            pt.x = static_cast<short>(LOWORD(lParam));
            pt.y = static_cast<short>(HIWORD(lParam));
            int newHover = hitTest(pt);
            if (newHover != m_hoverIndex) {
                m_hoverIndex = newHover;
                // 注意这里不需要立刻 render()，因为 WM_TIMER 的 60FPS 会自动拾取 hoverIndex 并平滑动画过去
            }
            return 0;
        }
        case WM_MBUTTONUP:
        case WM_LBUTTONUP:
        case WM_RBUTTONUP: {
            int finalIndex = m_hoverIndex;
            hide();
            if (finalIndex != -1) {
                executeAction(finalIndex);
            }
            return 0;
        }
        case WM_KILLFOCUS: {
            hide();
            return 0;
        }
    }
    return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
}

} // namespace easy::gesture
