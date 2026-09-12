#include "capture/ClipboardUtils.h"
#include "core/logger/Logger.h"
#include "core/utils/WinUtils.h"
#include <shlobj.h>
#include <shellapi.h>
#include <vector>
#include <filesystem>
#include <chrono>
#include <algorithm>

namespace tools3000::capture {

bool ClipboardUtils::copyImageToClipboard(const cv::Mat& image,
                                         const std::wstring& preferredFilePath,
                                         HWND ownerHwnd) {
    if (image.empty() || image.cols <= 0 || image.rows <= 0) {
        LOG_WARN("ClipboardUtils: image buffer is empty, aborting clipboard write");
        return false;
    }

    // 1. 剪贴板宿主窗口保障：若外部传入合法窗口则绑定，否则传入 nullptr 直接与当前任务(Current Task)绑定。
    // 严禁创建临时窗口又在退出时 DestroyWindow，避免破坏 Windows 剪贴板底层句柄所有权。
    HWND safeOwner = (ownerHwnd && IsWindow(ownerHwnd)) ? ownerHwnd : nullptr;

    // 2. 剪贴板锁定：针对系统剪贴板监听器 (如 Win+V 剪贴板历史、微信、Office) 瞬时占用的有界退避重试 (最大约 380ms)
    bool opened = false;
    for (int retry = 0; retry < 20; ++retry) {
        if (OpenClipboard(safeOwner)) {
            opened = true;
            break;
        }
        Sleep(10 + retry * 5);
    }

    if (!opened) {
        LOG_WARN("ClipboardUtils: failed to open clipboard after 20 retries, error={}", GetLastError());
        return false;
    }

    struct ClipboardGuard {
        ~ClipboardGuard() {
            CloseClipboard();
        }
    } clipGuard;

    if (!EmptyClipboard()) {
        LOG_WARN("ClipboardUtils: EmptyClipboard failed, error={}", GetLastError());
        return false;
    }

    // 3. 格式 1: 写入 PNG 格式 (保留透明通道与最高画质，现代社交与协同软件首选)
    UINT formatPng = RegisterClipboardFormatW(L"PNG");
    if (formatPng != 0) {
        std::vector<uint8_t> pngBytes;
        if (cv::imencode(".png", image, pngBytes) && !pngBytes.empty()) {
            HGLOBAL hPngGlobal = GlobalAlloc(GMEM_MOVEABLE, pngBytes.size());
            if (hPngGlobal) {
                void* pPngMem = GlobalLock(hPngGlobal);
                if (pPngMem) {
                    memcpy(pPngMem, pngBytes.data(), pngBytes.size());
                    GlobalUnlock(hPngGlobal);
                    if (!SetClipboardData(formatPng, hPngGlobal)) {
                        GlobalFree(hPngGlobal);
                    }
                } else {
                    GlobalFree(hPngGlobal);
                }
            }
        }
    }

    // 4. 格式 2: 写入 CF_DIBV5 (BITMAPV5HEADER 32位 BGRA 带 Alpha，支持现代 Office/GDI+ 透明图形)
    {
        BITMAPV5HEADER bi5{};
        bi5.bV5Size = sizeof(BITMAPV5HEADER);
        bi5.bV5Width = image.cols;
        bi5.bV5Height = image.rows; // 正数 = 规范底向上 (Bottom-Up)
        bi5.bV5Planes = 1;
        bi5.bV5BitCount = 32;
        bi5.bV5Compression = BI_BITFIELDS;
        bi5.bV5RedMask   = 0x00FF0000;
        bi5.bV5GreenMask = 0x0000FF00;
        bi5.bV5BlueMask  = 0x000000FF;
        bi5.bV5AlphaMask = 0xFF000000;
        bi5.bV5CSType    = LCS_sRGB;
        bi5.bV5Intent    = LCS_GM_IMAGES;

        const int stride32 = image.cols * 4;
        bi5.bV5SizeImage = stride32 * image.rows;
        const size_t totalSize5 = sizeof(BITMAPV5HEADER) + bi5.bV5SizeImage;

        HGLOBAL hGlobal5 = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, totalSize5);
        if (hGlobal5) {
            auto* pMem5 = static_cast<uint8_t*>(GlobalLock(hGlobal5));
            if (pMem5) {
                memcpy(pMem5, &bi5, sizeof(bi5));
                cv::Mat bgraMat;
                if (image.channels() == 4) {
                    bgraMat = image.isContinuous() ? image : image.clone();
                } else {
                    cv::cvtColor(image, bgraMat, cv::COLOR_BGR2BGRA);
                }

                for (int y = 0; y < bgraMat.rows; ++y) {
                    const int srcY = bgraMat.rows - 1 - y;
                    memcpy(pMem5 + sizeof(bi5) + y * stride32, bgraMat.ptr(srcY), stride32);
                }

                GlobalUnlock(hGlobal5);
                if (!SetClipboardData(CF_DIBV5, hGlobal5)) {
                    GlobalFree(hGlobal5);
                }
            } else {
                GlobalFree(hGlobal5);
            }
        }
    }

    // 5. 格式 3 & 4: 写入标准底向上 CF_DIB (24位 BGR) 与 CF_BITMAP (HBITMAP)
    cv::Mat bgrMat;
    if (image.channels() == 4) {
        // 对于带透明通道的截图 (圆角/阴影/外壳)，合并不透明纯白底消除黑边
        cv::Mat bg(image.size(), CV_8UC3, cv::Scalar(255, 255, 255));
        for (int y = 0; y < image.rows; ++y) {
            const auto* srcPtr = image.ptr<cv::Vec4b>(y);
            auto* dstPtr = bg.ptr<cv::Vec3b>(y);
            for (int x = 0; x < image.cols; ++x) {
                const float alpha = srcPtr[x][3] / 255.0f;
                dstPtr[x][0] = static_cast<uint8_t>(srcPtr[x][0] * alpha + 255.0f * (1.0f - alpha));
                dstPtr[x][1] = static_cast<uint8_t>(srcPtr[x][1] * alpha + 255.0f * (1.0f - alpha));
                dstPtr[x][2] = static_cast<uint8_t>(srcPtr[x][2] * alpha + 255.0f * (1.0f - alpha));
            }
        }
        bgrMat = bg;
    } else {
        bgrMat = image.isContinuous() ? image : image.clone();
    }

    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = image.cols;
    bi.biHeight = image.rows; // 正数 = 规范底向上 (Bottom-Up)
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;
    const int rowBytes24 = image.cols * 3;
    const int stride24 = (rowBytes24 + 3) & ~3; // 4 字节边界对齐
    bi.biSizeImage = stride24 * image.rows;

    const size_t totalSizeDib = sizeof(BITMAPINFOHEADER) + bi.biSizeImage;
    HGLOBAL hGlobalDib = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, totalSizeDib);
    if (hGlobalDib) {
        auto* pMemDib = static_cast<uint8_t*>(GlobalLock(hGlobalDib));
        if (pMemDib) {
            memcpy(pMemDib, &bi, sizeof(bi));
            for (int y = 0; y < bgrMat.rows; ++y) {
                const int srcY = bgrMat.rows - 1 - y;
                memcpy(pMemDib + sizeof(bi) + y * stride24, bgrMat.ptr(srcY), rowBytes24);
            }

            // 格式 4: CF_BITMAP (HBITMAP)
            HDC screenDc = GetDC(nullptr);
            if (screenDc) {
                HBITMAP hBmp = CreateDIBitmap(
                    screenDc,
                    &bi,
                    CBM_INIT,
                    pMemDib + sizeof(bi),
                    reinterpret_cast<const BITMAPINFO*>(&bi),
                    DIB_RGB_COLORS
                );
                ReleaseDC(nullptr, screenDc);
                if (hBmp) {
                    if (!SetClipboardData(CF_BITMAP, hBmp)) {
                        DeleteObject(hBmp);
                    }
                }
            }

            GlobalUnlock(hGlobalDib);
            if (!SetClipboardData(CF_DIB, hGlobalDib)) {
                GlobalFree(hGlobalDib);
            }
        } else {
            GlobalFree(hGlobalDib);
        }
    }

    // 6. 格式 5: 写入 CF_HDROP (文件拖拽句柄，赋予 Windows 桌面与资源管理器直接 Ctrl+V 粘贴 PNG 图片文件的原生能力)
    std::wstring dropFilePath = preferredFilePath;
    std::error_code ec;
    if (dropFilePath.empty() || !std::filesystem::exists(dropFilePath, ec)) {
        // 若外部未显式持久化文件，自动在应用临时目录输出高可用截图副本
        auto tempDir = tools3000::core::WinUtils::getAppDataDirectory() / L"temp";
        std::filesystem::create_directories(tempDir, ec);
        auto tempFile = tempDir / L"clipboard_screenshot.png";
        if (cv::imwrite(tools3000::core::WinUtils::wstringToUtf8(tempFile.wstring()), image)) {
            dropFilePath = tempFile.wstring();
        }
    }

    if (!dropFilePath.empty() && std::filesystem::exists(dropFilePath, ec)) {
        DROPFILES df{};
        df.pFiles = sizeof(DROPFILES);
        df.pt = {0, 0};
        df.fNC = FALSE;
        df.fWide = TRUE; // 采用 UTF-16 宽字符编码

        const size_t pathChars = dropFilePath.length() + 1; // 包含单个终止 \0
        const size_t stringBytes = (pathChars + 1) * sizeof(wchar_t); // 规范双 \0\0 终结
        const size_t totalBytes = sizeof(DROPFILES) + stringBytes;

        HGLOBAL hGlobalDrop = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, totalBytes);
        if (hGlobalDrop) {
            auto* pMemDrop = static_cast<uint8_t*>(GlobalLock(hGlobalDrop));
            if (pMemDrop) {
                memcpy(pMemDrop, &df, sizeof(DROPFILES));
                memcpy(pMemDrop + sizeof(DROPFILES), dropFilePath.c_str(), pathChars * sizeof(wchar_t));
                // GMEM_ZEROINIT 自动保障第二个 \0 结尾
                GlobalUnlock(hGlobalDrop);
                if (!SetClipboardData(CF_HDROP, hGlobalDrop)) {
                    GlobalFree(hGlobalDrop);
                }
            } else {
                GlobalFree(hGlobalDrop);
            }
        }
    }

    LOG_INFO("ClipboardUtils: image copied to clipboard successfully, size={}x{}, file={}",
             image.cols, image.rows, tools3000::core::WinUtils::wstringToUtf8(dropFilePath));
    return true;
}

} // namespace tools3000::capture
