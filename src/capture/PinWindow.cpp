// ─────────────────────────────────────────────────────────────────────────────
// PinWindow.cpp — 贴图窗口实现
//
// 功能:
//   - 截图后按 "钉住" 将截图贴在屏幕上
//   - 左键拖拽移动, 滚轮缩放, 双击关闭
//   - 支持透明度调节 (右键菜单)
// ─────────────────────────────────────────────────────────────────────────────

#include "capture/PinWindow.h"
#include "capture/CaptureOverlay.h"
#include "capture/CaptureVectorIcons.h"
#include "core/events/EventBus.h"
#include "core/logger/Logger.h"
#include "core/config/ConfigManager.h"
#include "core/utils/ThemeUtils.h"
#include "core/utils/DpiUtils.h"
#include "core/utils/WinUtils.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <shobjidl.h>
#include <windowsx.h>

namespace easy::capture {

using namespace Microsoft::WRL;

static constexpr const wchar_t* PIN_CLASS = L"EasyTools_PinWindow";

// 静态成员
std::vector<std::shared_ptr<PinWindow>> PinWindow::s_instances;
bool PinWindow::s_classRegistered = false;
bool PinWindow::s_allHidden = false;

// ─────────────────────────────────────────────────────────────────────────────
// 创建 / 销毁
// ─────────────────────────────────────────────────────────────────────────────

std::shared_ptr<PinWindow> PinWindow::create(const cv::Mat& image, int x, int y) {
    if (image.empty()) return nullptr;

    auto pin = std::shared_ptr<PinWindow>(new PinWindow());
    pin->m_origWidth = image.cols;
    pin->m_origHeight = image.rows;
    pin->m_sourceImage = image.clone();  // 保存原图，供右键"复制到剪贴板"

    if (!pin->initWindow(GetModuleHandleW(nullptr), x, y, image.cols, image.rows)) {
        LOG_ERROR("贴图窗口创建失败");
        return nullptr;
    }

    if (!pin->createRenderResources(image)) {
        LOG_ERROR("贴图窗口渲染资源创建失败");
        return nullptr;
    }

    ShowWindow(pin->m_hwnd, SW_SHOWNOACTIVATE);
    pin->render();

    s_instances.push_back(pin);
    LOG_INFO("贴图窗口已创建: {}x{} @ ({},{}), 总数={}", image.cols, image.rows, x, y, s_instances.size());

    return pin;
}

void PinWindow::close() {
    // 保活: 下面的 erase 会丢弃 s_instances 中指向自身的最后一个 shared_ptr，
    // 若不先持有自身引用，this 会在本函数执行途中被析构 (use-after-free)。
    // close() 通常从 pinWndProc (双击/右键) 内被调用，UAF 会偶发崩溃。
    std::shared_ptr<PinWindow> keepAlive;
    try {
        keepAlive = shared_from_this();
    } catch (const std::bad_weak_ptr&) {
        // 理论上不会发生 (实例总由 create() 用 shared_ptr 持有)
    }

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    m_sourceImage.release();
    // 从全局列表移除已失效的实例
    s_instances.erase(
        std::remove_if(s_instances.begin(), s_instances.end(),
                       [](const auto& p) { return !p->isAlive(); }),
        s_instances.end()
    );
    if (s_instances.empty()) {
        easy::core::WinUtils::trimWorkingSet();
    }
}

void PinWindow::closeAll() {
    for (auto& pin : s_instances) {
        if (pin) {
            pin->m_sourceImage.release();
            if (pin->m_hwnd) {
                DestroyWindow(pin->m_hwnd);
                pin->m_hwnd = nullptr;
            }
        }
    }
    s_instances.clear();
    s_allHidden = false;
    easy::core::WinUtils::trimWorkingSet();
    LOG_INFO("所有贴图窗口已关闭并释放资源");
}

void PinWindow::toggleHideAll() {
    if (s_instances.empty()) return;
    // 智能切换：只要有任意贴图被隐藏（含被 Esc 单独隐藏的），就全部显示；否则全部隐藏。
    // 这样单独 Esc 隐藏的贴图按一次此键即可找回。
    bool anyHidden = false;
    for (auto& pin : s_instances) {
        if (pin && pin->m_hwnd && !IsWindowVisible(pin->m_hwnd)) { anyHidden = true; break; }
    }
    bool show = anyHidden;
    for (auto& pin : s_instances) {
        if (pin && pin->m_hwnd) {
            ShowWindow(pin->m_hwnd, show ? SW_SHOWNOACTIVATE : SW_HIDE);
        }
    }
    s_allHidden = !show;
    LOG_INFO("所有贴图已{}", show ? "显示" : "隐藏");
}

void PinWindow::arrangeAll() {
    if (s_instances.empty()) return;
    s_allHidden = false;

    RECT wa{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);  // 主屏工作区（排除任务栏）

    const int step = 32;        // 层叠步进
    const int perColumn = 12;   // 每列层叠数，超过换到右侧新列
    int i = 0;
    for (auto& pin : s_instances) {
        if (!pin || !pin->m_hwnd) continue;
        int col = i / perColumn;
        int row = i % perColumn;
        int px = wa.left + 40 + col * (step * 13) + row * step;
        int py = wa.top  + 40 + row * step;
        ShowWindow(pin->m_hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(pin->m_hwnd, HWND_TOPMOST, px, py, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
        ++i;
    }
    LOG_INFO("已整理 {} 张贴图", i);
}

PinWindow::~PinWindow() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 属性
// ─────────────────────────────────────────────────────────────────────────────

void PinWindow::applyLayeredOpacity() {
    if (!m_hwnd) return;
    BYTE alpha = static_cast<BYTE>(std::clamp(m_opacity * 255.0f, 10.0f, 255.0f));
    SetLayeredWindowAttributes(m_hwnd, 0, alpha, LWA_ALPHA);
}

void PinWindow::updateHoverAnimation() {
    bool needsRender = false;
    float dt = (GetTickCount64() - m_hoverTime) / 150.0f;
    if (dt > 1.0f) dt = 1.0f;
    
    float targetAlpha = m_isHovering ? 1.0f : 0.0f;
    float startAlpha = m_isHovering ? 0.0f : 1.0f;
    
    float newAlpha = m_isHovering ? 
        startAlpha + (targetAlpha - startAlpha) * (1.0f - pow(1.0f - dt, 3.0f)) : 
        startAlpha + (targetAlpha - startAlpha) * dt; // linear fade out
        
    if (abs(m_hoverAlpha - newAlpha) > 0.01f) {
        m_hoverAlpha = newAlpha;
        needsRender = true;
    }
    
    if (dt >= 1.0f) {
        m_hoverAlpha = targetAlpha;
        KillTimer(m_hwnd, 1);
        needsRender = true;
    }
    
    if (needsRender) {
        render();
    }
}

void PinWindow::drawHoverToolbar() {
    if (!m_renderTarget) return;
    auto size = m_renderTarget->GetSize();

    const float dpiScale = easy::core::dpi::scaleForWindow(m_hwnd);

    // 悬浮工具栏：编辑、复制、保存、关闭 4 个按钮
    float btnW = 26.0f * dpiScale;
    float btnH = 24.0f * dpiScale;
    float btnPad = 4.0f * dpiScale;
    float tbWidth = btnW * 4.0f + btnPad * 5.0f;
    float tbHeight = 32.0f * dpiScale;
    float tbPadding = 8.0f * dpiScale;
    float tx = size.width - tbWidth - tbPadding;
    float ty = tbPadding;
    
    // 如果窗口太小，放在左上角
    if (tx < 0) tx = tbPadding;
    
    m_toolbarRect = D2D1::RectF(tx, ty, tx + tbWidth, ty + tbHeight);
    
    // 绘制磨砂玻璃背景
    ComPtr<ID2D1SolidColorBrush> bgBrush, borderBrush;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.07f, 0.07f, 0.10f, 0.75f * m_hoverAlpha), bgBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.15f * m_hoverAlpha), borderBrush.GetAddressOf());
    
    D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(m_toolbarRect, 6.0f * dpiScale, 6.0f * dpiScale);
    if (bgBrush) m_renderTarget->FillRoundedRectangle(&rrect, bgBrush.Get());
    if (borderBrush) m_renderTarget->DrawRoundedRectangle(&rrect, borderBrush.Get(), 1.0f * dpiScale);
    
    // 布局按钮
    float by = ty + (tbHeight - btnH) / 2.0f;
    float curX = tx + btnPad;
    
    m_btnEditRect = D2D1::RectF(curX, by, curX + btnW, by + btnH); curX += btnW + btnPad;
    m_btnCopyRect = D2D1::RectF(curX, by, curX + btnW, by + btnH); curX += btnW + btnPad;
    m_btnSaveRect = D2D1::RectF(curX, by, curX + btnW, by + btnH); curX += btnW + btnPad;
    m_btnCloseRect = D2D1::RectF(curX, by, curX + btnW, by + btnH);
    
    ComPtr<ID2D1SolidColorBrush> btnHoverBrush, textBrush;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.2f * m_hoverAlpha), btnHoverBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.9f * m_hoverAlpha), textBrush.GetAddressOf());
    
    if (m_hoverEdit && btnHoverBrush) m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(m_btnEditRect, 4.0f * dpiScale, 4.0f * dpiScale), btnHoverBrush.Get());
    if (m_hoverCopy && btnHoverBrush) m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(m_btnCopyRect, 4.0f * dpiScale, 4.0f * dpiScale), btnHoverBrush.Get());
    if (m_hoverSave && btnHoverBrush) m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(m_btnSaveRect, 4.0f * dpiScale, 4.0f * dpiScale), btnHoverBrush.Get());
    if (m_hoverClose && btnHoverBrush) m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(m_btnCloseRect, 4.0f * dpiScale, 4.0f * dpiScale), btnHoverBrush.Get());
    
    // 1. Edit 图标 (Lucide: Pen)
    CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(),
        CaptureIconId::ToolPen, m_btnEditRect, textBrush.Get(), dpiScale);

    // 2. Copy 图标 (Lucide: Copy)
    CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(),
        CaptureIconId::ActionCopy, m_btnCopyRect, textBrush.Get(), dpiScale);
    
    // 3. Save 图标 (Lucide: Download)
    CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(),
        CaptureIconId::ActionSave, m_btnSaveRect, textBrush.Get(), dpiScale);
    
    // 4. Close 图标 (Lucide: X)
    CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(),
        CaptureIconId::ActionCancel, m_btnCloseRect, textBrush.Get(), dpiScale);
}

void PinWindow::setOpacity(float opacity) {
    m_opacity = std::clamp(opacity, 0.1f, 1.0f);
    applyLayeredOpacity();
}

void PinWindow::setClickThrough(bool enable) {
    if (!m_hwnd) return;
    LONG_PTR ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
    if (enable) ex |= WS_EX_TRANSPARENT;
    else        ex &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex);
    m_clickThrough = enable;
    applyLayeredOpacity();
    LOG_INFO("贴图鼠标穿透: {}", enable ? "开启" : "关闭");
}

bool PinWindow::toggleClickThroughUnderCursor() {
    POINT pt;
    GetCursorPos(&pt);
    // 倒序遍历（后创建的视为更上层）：找到光标矩形命中的贴图并切换其穿透态
    for (auto it = s_instances.rbegin(); it != s_instances.rend(); ++it) {
        auto& pin = *it;
        if (!pin || !pin->m_hwnd) continue;
        RECT rc;
        if (GetWindowRect(pin->m_hwnd, &rc) && PtInRect(&rc, pt)) {
            pin->setClickThrough(!pin->m_clickThrough);
            return true;
        }
    }
    return false;
}

// 将 cv::Mat 以 CF_DIB 写入剪贴板（支持有界重试与完整内存安全保护）
static bool copyImageToClipboard(const cv::Mat& image) {
    if (image.empty()) return false;

    bool opened = false;
    for (int retry = 0; retry < 5; ++retry) {
        if (OpenClipboard(nullptr)) {
            opened = true;
            break;
        }
        Sleep(10);
    }
    if (!opened) return false;

    EmptyClipboard();

    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(bi);
    bi.biWidth = image.cols;
    bi.biHeight = -image.rows;  // top-down
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;
    int rowBytes = image.cols * 3;
    int stride = (rowBytes + 3) & ~3;
    bi.biSizeImage = stride * image.rows;

    size_t totalSize = sizeof(BITMAPINFOHEADER) + bi.biSizeImage;
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, totalSize);
    if (!hGlobal) { CloseClipboard(); return false; }

    auto* pMem = static_cast<uint8_t*>(GlobalLock(hGlobal));
    if (!pMem) { GlobalFree(hGlobal); CloseClipboard(); return false; }
    memcpy(pMem, &bi, sizeof(bi));

    cv::Mat cont;
    if (image.channels() == 4) cv::cvtColor(image, cont, cv::COLOR_BGRA2BGR);
    else cont = image.isContinuous() ? image : image.clone();

    for (int y = 0; y < cont.rows; ++y) {
        memcpy(pMem + sizeof(bi) + y * stride, cont.ptr(y), rowBytes);
    }

    GlobalUnlock(hGlobal);
    if (!SetClipboardData(CF_DIB, hGlobal)) {
        GlobalFree(hGlobal);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

static bool savePinnedImage(HWND owner, const cv::Mat& image) {
    if (image.empty()) return false;

    ComPtr<IFileSaveDialog> dialog;
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(dialog.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR("贴图保存对话框创建失败: hr=0x{:08X}", static_cast<unsigned>(hr));
        easy::core::EventBus::instance().publish(
            easy::core::ShowToastEvent{L"无法打开保存对话框"});
        return false;
    }

    const COMDLG_FILTERSPEC filters[] = {
        {L"PNG 图片 (*.png)", L"*.png"},
        {L"JPEG 图片 (*.jpg;*.jpeg)", L"*.jpg;*.jpeg"},
        {L"WebP 图片 (*.webp)", L"*.webp"},
        {L"BMP 图片 (*.bmp)", L"*.bmp"},
    };
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
    dialog->SetDefaultExtension(L"png");

    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t defaultName[80]{};
    swprintf_s(defaultName, L"EasyTools_%04u%02u%02u_%02u%02u%02u.png",
               now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    dialog->SetFileName(defaultName);

    hr = dialog->Show(owner);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return false;
    if (FAILED(hr)) {
        LOG_ERROR("贴图保存对话框失败: hr=0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }

    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(item.GetAddressOf()))) return false;
    PWSTR rawPath = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath)) || !rawPath) return false;
    std::filesystem::path path(rawPath);
    CoTaskMemFree(rawPath);

    std::wstring ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    if (ext.empty()) {
        path += L".png";
        ext = L".png";
    }
    if (ext == L".jpeg") ext = L".jpg";
    const std::string encodeExt = easy::core::WinUtils::wstringToUtf8(ext);
    const bool supported = encodeExt == ".png" || encodeExt == ".jpg" ||
                           encodeExt == ".webp" || encodeExt == ".bmp";
    if (!supported) {
        easy::core::EventBus::instance().publish(
            easy::core::ShowToastEvent{L"不支持该图片格式"});
        return false;
    }

    try {
        std::vector<uchar> encoded;
        std::vector<int> params;
        if (encodeExt == ".jpg") params = {cv::IMWRITE_JPEG_QUALITY, 95};
        else if (encodeExt == ".webp") params = {cv::IMWRITE_WEBP_QUALITY, 95};
        if (!cv::imencode(encodeExt, image, encoded, params)) return false;

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(encoded.data()),
                   static_cast<std::streamsize>(encoded.size()));
        if (!file) throw std::runtime_error("write failed");
        LOG_INFO("贴图已保存: {}", easy::core::WinUtils::wstringToUtf8(path.wstring()));
        easy::core::EventBus::instance().publish(
            easy::core::ShowToastEvent{L"贴图已保存"});
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("贴图保存失败: {}", e.what());
        easy::core::EventBus::instance().publish(
            easy::core::ShowToastEvent{L"贴图保存失败"});
        return false;
    }
}

void PinWindow::setScale(float scale) {
    m_scale = std::clamp(scale, 0.25f, 4.0f);
    if (m_hwnd) {
        int w = static_cast<int>(m_origWidth * m_scale);
        int h = static_cast<int>(m_origHeight * m_scale);
        SetWindowPos(m_hwnd, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);

        if (m_renderTarget) {
            m_renderTarget->Resize(D2D1::SizeU(w, h));
        }
        render();
    }
}

void PinWindow::rotate(int angleDeg) {
    if (m_sourceImage.empty() || !m_hwnd) return;
    if (angleDeg == 90) {
        cv::rotate(m_sourceImage, m_sourceImage, cv::ROTATE_90_CLOCKWISE);
        std::swap(m_origWidth, m_origHeight);
    } else if (angleDeg == 180) {
        cv::rotate(m_sourceImage, m_sourceImage, cv::ROTATE_180);
    } else if (angleDeg == 270 || angleDeg == -90) {
        cv::rotate(m_sourceImage, m_sourceImage, cv::ROTATE_90_COUNTERCLOCKWISE);
        std::swap(m_origWidth, m_origHeight);
    }
    createRenderResources(m_sourceImage);
    setScale(m_scale);
}

void PinWindow::flip(bool horizontal) {
    if (m_sourceImage.empty() || !m_hwnd) return;
    cv::flip(m_sourceImage, m_sourceImage, horizontal ? 1 : 0);
    createRenderResources(m_sourceImage);
    render();
}

void PinWindow::editMarkup() {
    if (!m_hwnd || m_sourceImage.empty()) return;
    
    RECT rc{};
    GetWindowRect(m_hwnd, &rc);
    CaptureRegion reg{ rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top };

    ShowWindow(m_hwnd, SW_HIDE);
    std::weak_ptr<PinWindow> weakSelf = shared_from_this();
    CaptureOverlay::instance().startEditPinned(m_sourceImage, reg, [weakSelf](const cv::Mat& newImage) {
        if (auto self = weakSelf.lock()) {
            self->updateImage(newImage);
            ShowWindow(self->m_hwnd, SW_SHOW);
        }
    });
}

void PinWindow::updateImage(const cv::Mat& newImage) {
    if (newImage.empty() || !m_hwnd) return;
    m_sourceImage = newImage.clone();
    m_origWidth = newImage.cols;
    m_origHeight = newImage.rows;

    const int newW = std::max(1, static_cast<int>(m_origWidth * m_scale));
    const int newH = std::max(1, static_cast<int>(m_origHeight * m_scale));
    SetWindowPos(m_hwnd, nullptr, 0, 0, newW, newH, SWP_NOMOVE | SWP_NOZORDER);

    createRenderResources(m_sourceImage);
    render();
}

POINT PinWindow::calculateMagneticSnap(HWND currentHwnd, POINT targetPos, int curW, int curH) {
    POINT res = targetPos;
    const HMONITOR monitor = MonitorFromPoint(targetPos, MONITOR_DEFAULTTONEAREST);
    const RECT work = easy::core::dpi::workArea(monitor);
    const float scale = easy::core::dpi::scaleForMonitor(monitor);
    const int snapDist = easy::core::dpi::scaleMetric(12, scale);

    // 1. 吸附到当前显示器工作区四边
    if (std::abs(res.x - work.left) <= snapDist) res.x = work.left;
    else if (std::abs(res.x + curW - work.right) <= snapDist) res.x = work.right - curW;

    if (std::abs(res.y - work.top) <= snapDist) res.y = work.top;
    else if (std::abs(res.y + curH - work.bottom) <= snapDist) res.y = work.bottom - curH;

    // 2. 吸附到其他贴图窗口边缘
    for (const auto& pin : s_instances) {
        if (!pin || !pin->isAlive() || pin->m_hwnd == currentHwnd) continue;
        if (!IsWindowVisible(pin->m_hwnd)) continue;

        RECT otherRc{};
        GetWindowRect(pin->m_hwnd, &otherRc);

        // X 轴吸附：左对齐、右对齐、左贴右、右贴左
        if (std::abs(res.x - otherRc.left) <= snapDist) {
            res.x = otherRc.left;
        } else if (std::abs(res.x + curW - otherRc.right) <= snapDist) {
            res.x = otherRc.right - curW;
        } else if (std::abs(res.x - otherRc.right) <= snapDist) {
            res.x = otherRc.right;
        } else if (std::abs(res.x + curW - otherRc.left) <= snapDist) {
            res.x = otherRc.left - curW;
        }

        // Y 轴吸附：顶对齐、底对齐、顶贴底、底贴顶
        if (std::abs(res.y - otherRc.top) <= snapDist) {
            res.y = otherRc.top;
        } else if (std::abs(res.y + curH - otherRc.bottom) <= snapDist) {
            res.y = otherRc.bottom - curH;
        } else if (std::abs(res.y - otherRc.bottom) <= snapDist) {
            res.y = otherRc.bottom;
        } else if (std::abs(res.y + curH - otherRc.top) <= snapDist) {
            res.y = otherRc.top - curH;
        }
    }

    return res;
}

// ─────────────────────────────────────────────────────────────────────────────
// 剪贴板贴图（图像 / 文本 / 颜色 → 浮空贴图）
// ─────────────────────────────────────────────────────────────────────────────

// 剪贴板位图(HBITMAP) → BGR cv::Mat。用 GetDIBits 统一转 32 位, 兼容任意源格式。
static cv::Mat hbitmapToMat(HBITMAP hbm) {
    BITMAP bm{};
    if (!GetObject(hbm, sizeof(bm), &bm)) return {};
    int w = bm.bmWidth, h = std::abs(bm.bmHeight);
    if (w <= 0 || h <= 0) return {};

    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(bi);
    bi.biWidth = w;
    bi.biHeight = -h;  // top-down
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    cv::Mat bgra(h, w, CV_8UC4);
    HDC dc = GetDC(nullptr);
    int got = GetDIBits(dc, hbm, 0, h, bgra.data, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (got == 0) return {};

    // GetDIBits 32 位 BI_RGB 不写 alpha, 直接丢弃 alpha 得到不透明 BGR
    cv::Mat bgr;
    cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
    return bgr;
}

// 解析 #RGB / #RRGGBB 文本为 BGR 颜色
static bool parseHexColor(const std::wstring& in, cv::Scalar& bgr, std::wstring& label) {
    size_t a = in.find_first_not_of(L" \t\r\n");
    size_t b = in.find_last_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return false;
    std::wstring s = in.substr(a, b - a + 1);
    if (s.size() < 4 || s[0] != L'#') return false;
    std::wstring hex = s.substr(1);
    if (hex.size() != 3 && hex.size() != 6) return false;
    for (wchar_t c : hex) if (!std::iswxdigit(c)) return false;

    auto hv = [](wchar_t c) -> int {
        c = std::towlower(c);
        return (c <= L'9') ? (c - L'0') : (c - L'a' + 10);
    };
    int r, g, bl;
    if (hex.size() == 3) { r = hv(hex[0]) * 17; g = hv(hex[1]) * 17; bl = hv(hex[2]) * 17; }
    else { r = hv(hex[0]) * 16 + hv(hex[1]); g = hv(hex[2]) * 16 + hv(hex[3]); bl = hv(hex[4]) * 16 + hv(hex[5]); }
    bgr = cv::Scalar(bl, g, r);
    label = s;
    return true;
}

// 颜色 → 色卡贴图（色块 + 文字按亮度自动取黑/白以保证对比）
static cv::Mat renderColorSwatch(const cv::Scalar& bgr, const std::wstring& label) {
    const int w = 180, h = 130;
    cv::Mat img(h, w, CV_8UC3, bgr);
    double lum = 0.114 * bgr[0] + 0.587 * bgr[1] + 0.299 * bgr[2];
    cv::Scalar tc = lum > 140 ? cv::Scalar(20, 20, 20) : cv::Scalar(240, 240, 240);
    std::string lbl;
    for(auto c : label) lbl.push_back((char)c);  // hex 为 ASCII，可安全窄化
    cv::putText(img, lbl, cv::Point(14, h - 18), cv::FONT_HERSHEY_SIMPLEX, 0.7, tc, 1, cv::LINE_AA);
    return img;
}

// 文本/代码 → Mac 极客风卡片贴图（含红黄绿三色窗口点、代码行号与深邃极客磨砂卡片）
static cv::Mat renderTextToImage(const std::wstring& text) {
    HDC screen = GetDC(nullptr);
    HDC memdc = CreateCompatibleDC(screen);

    // 智能检测是否为代码块（包含换行、花括号、缩进、常见代码关键字）
    bool isCode = (text.find(L'\n') != std::wstring::npos) ||
                  (text.find(L'{') != std::wstring::npos) ||
                  (text.find(L"def ") != std::wstring::npos) ||
                  (text.find(L"const ") != std::wstring::npos) ||
                  (text.find(L"function") != std::wstring::npos) ||
                  (text.find(L"#include") != std::wstring::npos) ||
                  (text.find(L"import ") != std::wstring::npos) ||
                  (text.find(L"class ") != std::wstring::npos);

    LPCWSTR fontFace = isCode ? L"Cascadia Code" : L"Segoe UI Variable Text";
    HFONT font = CreateFontW(-19, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, fontFace);
    HGDIOBJ oldFont = SelectObject(memdc, font);

    const int headerH = 28;     // 顶部 Mac 窗口控制栏高度
    const int padX = 18;        // 水平内边距
    const int padY = 14;        // 垂直内边距
    const int maxW = 720;
    
    RECT rc{0, 0, maxW, 0};
    DrawTextW(memdc, text.c_str(), -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    int contentW = std::min<LONG>(rc.right, maxW);
    int contentH = rc.bottom;

    int w = std::clamp(contentW + padX * 2 + 10, 160, maxW + padX * 2);
    int h = std::clamp(headerH + contentH + padY * 2, 64, 2400);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(memdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    cv::Mat result;
    if (dib && bits) {
        HGDIOBJ oldBm = SelectObject(memdc, dib);
        
        // 1. 卡片深邃背景 (#18181B - Zinc 900)
        RECT full{0, 0, w, h};
        HBRUSH bg = CreateSolidBrush(RGB(24, 24, 27));
        FillRect(memdc, &full, bg);
        DeleteObject(bg);

        // 2. 顶部 Mac 红黄绿三色微圆点 (10px 直径)
        HBRUSH redDot = CreateSolidBrush(RGB(255, 95, 86));
        HBRUSH yellowDot = CreateSolidBrush(RGB(255, 189, 46));
        HBRUSH greenDot = CreateSolidBrush(RGB(39, 201, 63));
        HPEN nullPen = CreatePen(PS_NULL, 0, 0);
        HGDIOBJ oldPen = SelectObject(memdc, nullPen);

        SelectObject(memdc, redDot);
        Ellipse(memdc, 14, 10, 24, 20);
        SelectObject(memdc, yellowDot);
        Ellipse(memdc, 30, 10, 40, 20);
        SelectObject(memdc, greenDot);
        Ellipse(memdc, 46, 10, 56, 20);

        DeleteObject(redDot);
        DeleteObject(yellowDot);
        DeleteObject(greenDot);

        // 3. 顶部微弱分割线 (#27272A)
        HPEN divPen = CreatePen(PS_SOLID, 1, RGB(39, 39, 42));
        SelectObject(memdc, divPen);
        MoveToEx(memdc, 0, headerH, nullptr);
        LineTo(memdc, w, headerH);
        DeleteObject(divPen);

        // 4. 外边框微高光 (#3F3F46)
        HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(63, 63, 70));
        SelectObject(memdc, borderPen);
        MoveToEx(memdc, 0, 0, nullptr);
        LineTo(memdc, w - 1, 0);
        LineTo(memdc, w - 1, h - 1);
        LineTo(memdc, 0, h - 1);
        LineTo(memdc, 0, 0);
        DeleteObject(borderPen);

        SelectObject(memdc, oldPen);
        DeleteObject(nullPen);

        // 5. 文本与代码渲染
        SetBkMode(memdc, TRANSPARENT);
        SetTextColor(memdc, RGB(244, 244, 245));
        RECT tr{padX, headerH + padY, w - padX, h - padY};
        DrawTextW(memdc, text.c_str(), -1, &tr, DT_WORDBREAK | DT_NOPREFIX);
        GdiFlush();

        cv::Mat bgra(h, w, CV_8UC4, bits);
        cv::cvtColor(bgra, result, cv::COLOR_BGRA2BGR);  // 独立内存输出

        SelectObject(memdc, oldBm);
        DeleteObject(dib);
    }

    SelectObject(memdc, oldFont);
    DeleteObject(font);
    DeleteDC(memdc);
    ReleaseDC(nullptr, screen);
    return result;
}

std::shared_ptr<PinWindow> PinWindow::createFromClipboard() {
    POINT pt;
    GetCursorPos(&pt);

    cv::Mat img;
    std::wstring text;
    if (OpenClipboard(nullptr)) {
        if (IsClipboardFormatAvailable(CF_BITMAP)) {
            if (HBITMAP hbm = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP))) {
                img = hbitmapToMat(hbm);  // 句柄归剪贴板所有，期间使用、勿释放
            }
        }
        if (img.empty() && IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
                if (auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
                    text = p;
                    GlobalUnlock(h);
                }
            }
        }
        CloseClipboard();
    }

    if (img.empty() && !text.empty()) {
        cv::Scalar c;
        std::wstring hexLabel;
        if (parseHexColor(text, c, hexLabel)) img = renderColorSwatch(c, hexLabel);
        else img = renderTextToImage(text);
    }

    if (img.empty()) {
        LOG_INFO("剪贴板贴图：剪贴板无可贴内容");
        return nullptr;
    }

    LOG_INFO("剪贴板贴图：{}x{} @ ({},{})", img.cols, img.rows, pt.x, pt.y);
    return create(img, pt.x, pt.y);
}

// ─────────────────────────────────────────────────────────────────────────────
// 窗口初始化
// ─────────────────────────────────────────────────────────────────────────────

bool PinWindow::initWindow(HINSTANCE hInstance, int x, int y, int w, int h) {
    if (!s_classRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS | 0x00020000 /*CS_DROPSHADOW*/;
        wc.lpfnWndProc = pinWndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(nullptr, IDC_SIZEALL);
        wc.lpszClassName = PIN_CLASS;
        RegisterClassExW(&wc);
        s_classRegistered = true;
    }

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        PIN_CLASS, L"",
        WS_POPUP,
        x, y, w, h,
        nullptr, nullptr, hInstance, this
    );

    if (!m_hwnd) return false;

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);

    return true;
}

bool PinWindow::createRenderResources(const cv::Mat& image) {
    HRESULT hr;

    if (!m_d2dFactory) {
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf());
        if (FAILED(hr)) return false;
    }

    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    const UINT targetW = static_cast<UINT>((std::max)(1L, rc.right - rc.left));
    const UINT targetH = static_cast<UINT>((std::max)(1L, rc.bottom - rc.top));

    if (!m_renderTarget) {
        auto rtProps = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        auto hwndProps = D2D1::HwndRenderTargetProperties(
            m_hwnd, D2D1::SizeU(targetW, targetH),
            D2D1_PRESENT_OPTIONS_IMMEDIATELY
        );

        hr = m_d2dFactory->CreateHwndRenderTarget(rtProps, hwndProps, m_renderTarget.GetAddressOf());
        if (FAILED(hr)) return false;

        // 禁用 D2D 的自动 DPI 缩放
        m_renderTarget->SetDpi(96.0f, 96.0f);
    } else {
        m_renderTarget->Resize(D2D1::SizeU(targetW, targetH));
    }

    // 转为 BGRA 并创建 D2D Bitmap
    cv::Mat bgra;
    if (image.channels() == 3) {
        cv::cvtColor(image, bgra, cv::COLOR_BGR2BGRA);
    } else if (image.channels() == 4) {
        bgra = image;
    } else {
        return false;
    }

    D2D1_BITMAP_PROPERTIES bitmapProps = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    m_bitmap.Reset();
    hr = m_renderTarget->CreateBitmap(
        D2D1::SizeU(bgra.cols, bgra.rows),
        bgra.data, bgra.cols * 4,
        bitmapProps, m_bitmap.GetAddressOf()
    );

    return SUCCEEDED(hr);
}

void PinWindow::render() {
    if (!m_renderTarget || !m_bitmap) return;

    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));

    auto size = m_renderTarget->GetSize();
    m_renderTarget->DrawBitmap(m_bitmap.Get(),
                               D2D1::RectF(0, 0, size.width, size.height));

    if (m_hoverAlpha > 0.01f) {
        drawHoverToolbar();
    }

    // 边框：选中态用主题强调色 2px 内描边，否则 1px 半透明中性灰
    ComPtr<ID2D1SolidColorBrush> borderBrush;
    if (m_focused) {
        auto& cfg = easy::core::ConfigManager::instance();
        const std::string accent = cfg.get<std::string>("/general/accentColor", "blue");
        const easy::core::AccentColorRGB themeRgb = easy::core::getAccentColorRGB(accent);
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(themeRgb.r, themeRgb.g, themeRgb.b, 0.95f), borderBrush.GetAddressOf());
        m_renderTarget->DrawRectangle(D2D1::RectF(1, 1, size.width - 1, size.height - 1), borderBrush.Get(), 2.0f);
    } else {
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.5f), borderBrush.GetAddressOf());
        m_renderTarget->DrawRectangle(D2D1::RectF(0, 0, size.width, size.height), borderBrush.Get(), 1.0f);
    }

    m_renderTarget->EndDraw();
}

// ─────────────────────────────────────────────────────────────────────────────
// 窗口过程
// ─────────────────────────────────────────────────────────────────────────────

LRESULT CALLBACK PinWindow::pinWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<PinWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_LBUTTONDOWN: {
            if (!self) break;
            if (self->m_hoverAlpha > 0.0f) {
                if (self->m_hoverEdit) {
                    self->editMarkup();
                    return 0;
                }
                if (self->m_hoverCopy) {
                    copyImageToClipboard(self->m_sourceImage);
                    easy::core::EventBus::instance().publish(
                        easy::core::ShowToastEvent{L"已复制贴图到剪贴板"});
                    return 0;
                }
                if (self->m_hoverSave) {
                    savePinnedImage(hwnd, self->m_sourceImage);
                    return 0;
                }
                if (self->m_hoverClose) {
                    self->close();
                    return 0;
                }
            }
            SetForegroundWindow(hwnd);  // 选中此贴图（取得键盘焦点，使其能响应 Esc）
            self->m_isDragging = true;
            self->m_dragOffset = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            SetCapture(hwnd);
            return 0;
        }

        case WM_SETFOCUS: {
            if (self) { self->m_focused = true; InvalidateRect(hwnd, nullptr, FALSE); }
            return 0;
        }

        case WM_KILLFOCUS: {
            if (self) { self->m_focused = false; InvalidateRect(hwnd, nullptr, FALSE); }
            return 0;
        }

        case WM_KEYDOWN: {
            if (!self) break;
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

            // Esc / Ctrl+W: 优雅关闭当前贴图
            if (wParam == VK_ESCAPE || (ctrl && wParam == 'W')) {
                self->close();
                return 0;
            }
            // E: 重新进入标注编辑模式
            if (wParam == 'E' && !ctrl) {
                self->editMarkup();
                return 0;
            }
            // T: 切换鼠标穿透
            if (wParam == 'T' && !ctrl) {
                self->setClickThrough(!self->m_clickThrough);
                return 0;
            }
            // 1~9: 快速设置透明度 10%~90%
            if (wParam >= '1' && wParam <= '9' && !ctrl) {
                self->setOpacity(static_cast<float>(wParam - '0') / 10.0f);
                return 0;
            }
            // R / Shift+R: 顺时针 / 逆时针旋转 90 度
            if (wParam == 'R' && !ctrl) {
                self->rotate(shift ? 270 : 90);
                return 0;
            }
            // H: 水平镜像翻转
            if (wParam == 'H' && !ctrl) {
                self->flip(true);
                return 0;
            }
            // V: 垂直镜像翻转
            if (wParam == 'V' && !ctrl) {
                self->flip(false);
                return 0;
            }
            // Ctrl+C: 复制原图到剪贴板
            if (ctrl && wParam == 'C') {
                copyImageToClipboard(self->m_sourceImage);
                return 0;
            }
            // Ctrl+S: 保存贴图图片
            if (ctrl && wParam == 'S') {
                savePinnedImage(hwnd, self->m_sourceImage);
                return 0;
            }
            // +/- 键: 缩放
            if (wParam == VK_OEM_PLUS || wParam == VK_ADD || wParam == '=') {
                self->setScale(self->m_scale * 1.1f);
                return 0;
            }
            if (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT || wParam == '-') {
                self->setScale(self->m_scale * 0.9f);
                return 0;
            }
            // [/] 键: 透明度微调
            if (wParam == VK_OEM_4) { // '['
                self->setOpacity((std::max)(0.1f, self->m_opacity - 0.1f));
                return 0;
            }
            if (wParam == VK_OEM_6) { // ']'
                self->setOpacity((std::min)(1.0f, self->m_opacity + 0.1f));
                return 0;
            }
            // 0 或 1: 恢复 100% 原始尺寸
            if (wParam == '0' || wParam == VK_NUMPAD0) {
                self->setScale(1.0f);
                return 0;
            }
            break;
        }

        case WM_MOUSEMOVE: {
            if (!self) break;
            if (!self->m_isHovering) {
                self->m_isHovering = true;
                self->m_hoverTime = GetTickCount64();
                SetTimer(hwnd, 1, 16, nullptr);
                
                TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, HOVER_DEFAULT };
                TrackMouseEvent(&tme);
            }
            
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            bool hoverEdit = (x >= self->m_btnEditRect.left && x <= self->m_btnEditRect.right &&
                              y >= self->m_btnEditRect.top && y <= self->m_btnEditRect.bottom);
            bool hoverCopy = (x >= self->m_btnCopyRect.left && x <= self->m_btnCopyRect.right &&
                              y >= self->m_btnCopyRect.top && y <= self->m_btnCopyRect.bottom);
            bool hoverSave = (x >= self->m_btnSaveRect.left && x <= self->m_btnSaveRect.right &&
                              y >= self->m_btnSaveRect.top && y <= self->m_btnSaveRect.bottom);
            bool hoverClose = (x >= self->m_btnCloseRect.left && x <= self->m_btnCloseRect.right &&
                               y >= self->m_btnCloseRect.top && y <= self->m_btnCloseRect.bottom);
            
            if (hoverEdit != self->m_hoverEdit || hoverCopy != self->m_hoverCopy ||
                hoverSave != self->m_hoverSave || hoverClose != self->m_hoverClose) {
                self->m_hoverEdit = hoverEdit;
                self->m_hoverCopy = hoverCopy;
                self->m_hoverSave = hoverSave;
                self->m_hoverClose = hoverClose;
                self->render();
            }

            if (self->m_isDragging) {
                POINT cursor;
                GetCursorPos(&cursor);
                int curW = static_cast<int>(self->m_origWidth * self->m_scale);
                int curH = static_cast<int>(self->m_origHeight * self->m_scale);
                POINT targetPos = { cursor.x - self->m_dragOffset.x, cursor.y - self->m_dragOffset.y };
                targetPos = calculateMagneticSnap(hwnd, targetPos, curW, curH);
                SetWindowPos(hwnd, nullptr,
                             targetPos.x,
                             targetPos.y,
                             0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }
            return 0;
        }

        case WM_MOUSELEAVE: {
            if (!self) break;
            self->m_isHovering = false;
            self->m_hoverTime = GetTickCount64();
            self->m_hoverEdit = false;
            self->m_hoverCopy = false;
            self->m_hoverSave = false;
            self->m_hoverClose = false;
            SetTimer(hwnd, 1, 16, nullptr);
            return 0;
        }

        case WM_TIMER: {
            if (self && wParam == 1) {
                self->updateHoverAnimation();
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (self) {
                self->m_isDragging = false;
                ReleaseCapture();
            }
            return 0;
        }

        case WM_LBUTTONDBLCLK: {
            // 双击默认进入标注编辑模式，若已在编辑或不可编辑则关闭
            if (self) {
                self->editMarkup();
            }
            return 0;
        }

        case WM_MBUTTONUP: {
            // 鼠标中键单击：快速恢复 1:1 原始物理像素尺寸
            if (self) {
                self->setScale(1.0f);
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            if (!self) break;
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            const bool ctrl = (LOWORD(wParam) & MK_CONTROL) != 0 ||
                              ((GetKeyState(VK_CONTROL) & 0x8000) != 0);

            if (ctrl) {
                // Ctrl + 滚轮：平滑微调透明度 (10% ~ 100%, 步进 5%)
                float newOpacity = self->m_opacity + (delta > 0 ? 0.05f : -0.05f);
                self->setOpacity(newOpacity);
            } else {
                // 滚轮：以当前鼠标光标为锚点平滑缩放 (0.1x ~ 5.0x)
                POINT cursor;
                GetCursorPos(&cursor);
                RECT rc;
                GetWindowRect(hwnd, &rc);

                float oldScale = self->m_scale;
                float zoomFactor = (delta > 0 ? 1.1f : 0.9f);
                float newScale = std::clamp(oldScale * zoomFactor, 0.1f, 5.0f);

                if (std::abs(newScale - oldScale) > 0.001f) {
                    float relX = (cursor.x - rc.left) / static_cast<float>((std::max)(1L, rc.right - rc.left));
                    float relY = (cursor.y - rc.top) / static_cast<float>((std::max)(1L, rc.bottom - rc.top));

                    int newW = static_cast<int>(self->m_origWidth * newScale);
                    int newH = static_cast<int>(self->m_origHeight * newScale);

                    int newLeft = cursor.x - static_cast<int>(newW * relX);
                    int newTop = cursor.y - static_cast<int>(newH * relY);

                    self->m_scale = newScale;
                    SetWindowPos(hwnd, nullptr, newLeft, newTop, newW, newH, SWP_NOZORDER | SWP_NOACTIVATE);
                    if (self->m_renderTarget) {
                        self->m_renderTarget->Resize(D2D1::SizeU(newW, newH));
                    }
                    self->render();
                }
            }
            return 0;
        }

        case WM_RBUTTONUP: {
            // 右键菜单
            if (!self) break;
            HMENU menu = CreatePopupMenu();
            bool isZh = easy::core::WinUtils::isSystemLanguageChinese();
            AppendMenuW(menu, MF_STRING, 16, isZh ? L"编辑 / 二次标注  (E / 双击)" : L"Edit / Annotate  (E / Double Click)");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 1, isZh ? L"复制到剪贴板  (Ctrl+C)" : L"Copy to Clipboard  (Ctrl+C)");
            AppendMenuW(menu, MF_STRING, 10, isZh ? L"保存图片...  (Ctrl+S)" : L"Save Image...  (Ctrl+S)");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 15, isZh ? L"1:1 原始尺寸  (0 或鼠标中键)" : L"1:1 Original Scale  (0 or MButton)");
            AppendMenuW(menu, MF_STRING, 11, isZh ? L"顺时针旋转 90°  (R)" : L"Rotate Right 90°  (R)");
            AppendMenuW(menu, MF_STRING, 12, isZh ? L"逆时针旋转 90°  (Shift+R)" : L"Rotate Left 90°  (Shift+R)");
            AppendMenuW(menu, MF_STRING, 13, isZh ? L"水平翻转  (H)" : L"Flip Horizontal  (H)");
            AppendMenuW(menu, MF_STRING, 14, isZh ? L"垂直翻转  (V)" : L"Flip Vertical  (V)");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 2, isZh ? L"透明度 100%  (数字 0 或 Ctrl+滚轮)" : L"Opacity 100%  (Key 0 or Ctrl+Wheel)");
            AppendMenuW(menu, MF_STRING, 3, isZh ? L"透明度 75%  (数字 7)" : L"Opacity 75%  (Key 7)");
            AppendMenuW(menu, MF_STRING, 4, isZh ? L"透明度 50%  (数字 5)" : L"Opacity 50%  (Key 5)");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 7, isZh ? L"鼠标穿透  (T 或 Ctrl+Alt+Shift+X 切回)"
                                               : L"Click-through  (T or Ctrl+Alt+Shift+X toggles)");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 9, isZh ? L"整理全部贴图" : L"Arrange All Pins");
            AppendMenuW(menu, MF_STRING, 8, isZh ? L"隐藏全部贴图  (Ctrl+Alt+Shift+H 显示)"
                                               : L"Hide All Pins  (Ctrl+Alt+Shift+H shows)");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 5, isZh ? L"关闭  (Esc / Ctrl+W)" : L"Close  (Esc / Ctrl+W)");
            AppendMenuW(menu, MF_STRING, 6, isZh ? L"关闭全部贴图" : L"Close All Pins");

            POINT pt;
            GetCursorPos(&pt);
            int cmd = TrackPopupMenu(menu, TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);

            switch (cmd) {
                case 16: self->editMarkup(); break;
                case 1: copyImageToClipboard(self->m_sourceImage); break;
                case 10: savePinnedImage(hwnd, self->m_sourceImage); break;
                case 15: self->setScale(1.0f); break;
                case 11: self->rotate(90); break;
                case 12: self->rotate(270); break;
                case 13: self->flip(true); break;
                case 14: self->flip(false); break;
                case 2: self->setOpacity(1.0f); break;
                case 3: self->setOpacity(0.75f); break;
                case 4: self->setOpacity(0.5f); break;
                case 5: self->close(); break;
                case 6: PinWindow::closeAll(); break;
                case 7: self->setClickThrough(true); break;
                case 8: PinWindow::toggleHideAll(); break;
                case 9: PinWindow::arrangeAll(); break;
            }
            return 0;
        }

        case WM_PAINT: {
            if (self) self->render();
            ValidateRect(hwnd, nullptr);
            return 0;
        }

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace easy::capture
