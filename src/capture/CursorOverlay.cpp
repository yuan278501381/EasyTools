#include "capture/CursorOverlay.h"

#include "core/logger/Logger.h"
#include "core/utils/DpiUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace easy::capture {
namespace {

constexpr int MaxCursorDimension = 512;
constexpr auto ClickDuration = std::chrono::milliseconds(420);

int bytesPerPixel(CapturePixelFormat format) {
    return format == CapturePixelFormat::Bgra32 ? 4 : 3;
}

struct Bounds {
    int left = std::numeric_limits<int>::max();
    int top = std::numeric_limits<int>::max();
    int right = std::numeric_limits<int>::min();
    int bottom = std::numeric_limits<int>::min();

    void include(int x, int y, int width, int height) {
        if (width <= 0 || height <= 0) return;
        left = std::min(left, x);
        top = std::min(top, y);
        right = std::max(right, x + width);
        bottom = std::max(bottom, y + height);
    }

    bool clip(int width, int height) {
        left = std::clamp(left, 0, width);
        top = std::clamp(top, 0, height);
        right = std::clamp(right, 0, width);
        bottom = std::clamp(bottom, 0, height);
        return left < right && top < bottom;
    }
};

}  // namespace

CursorOverlay::Patch::Patch(Patch&& other) noexcept {
    *this = std::move(other);
}

CursorOverlay::Patch& CursorOverlay::Patch::operator=(Patch&& other) noexcept {
    if (this == &other) return *this;
    restore();
    m_frameData = std::exchange(other.m_frameData, nullptr);
    m_stride = other.m_stride;
    m_bytesPerPixel = other.m_bytesPerPixel;
    m_x = other.m_x;
    m_y = other.m_y;
    m_width = other.m_width;
    m_height = other.m_height;
    m_original = std::move(other.m_original);
    return *this;
}

void CursorOverlay::Patch::restore() noexcept {
    if (!m_frameData) return;
    const std::size_t rowBytes = static_cast<std::size_t>(m_width) * m_bytesPerPixel;
    for (int row = 0; row < m_height; ++row) {
        auto* destination = m_frameData +
            static_cast<std::size_t>(m_y + row) * m_stride +
            static_cast<std::size_t>(m_x) * m_bytesPerPixel;
        std::memcpy(destination, m_original.data() + static_cast<std::size_t>(row) * rowBytes,
                    rowBytes);
    }
    m_frameData = nullptr;
    m_original.clear();
}

CursorOverlay::~CursorOverlay() {
    destroySurfaces();
}

bool CursorOverlay::ensureSurfaces(int width, int height) {
    if (width == m_surfaceWidth && height == m_surfaceHeight &&
        m_blackDc && m_whiteDc && m_blackBits && m_whiteBits) {
        return true;
    }
    destroySurfaces();
    if (width <= 0 || height <= 0 || width > MaxCursorDimension || height > MaxCursorDimension) {
        return false;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    m_blackDc = CreateCompatibleDC(nullptr);
    m_whiteDc = CreateCompatibleDC(nullptr);
    m_blackBitmap = CreateDIBSection(
        m_blackDc, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&m_blackBits), nullptr, 0);
    m_whiteBitmap = CreateDIBSection(
        m_whiteDc, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&m_whiteBits), nullptr, 0);
    if (!m_blackDc || !m_whiteDc || !m_blackBitmap || !m_whiteBitmap ||
        !m_blackBits || !m_whiteBits) {
        destroySurfaces();
        return false;
    }
    m_previousBlackBitmap = SelectObject(m_blackDc, m_blackBitmap);
    m_previousWhiteBitmap = SelectObject(m_whiteDc, m_whiteBitmap);
    if (!m_previousBlackBitmap || m_previousBlackBitmap == HGDI_ERROR ||
        !m_previousWhiteBitmap || m_previousWhiteBitmap == HGDI_ERROR) {
        if (m_previousBlackBitmap == HGDI_ERROR) m_previousBlackBitmap = nullptr;
        if (m_previousWhiteBitmap == HGDI_ERROR) m_previousWhiteBitmap = nullptr;
        destroySurfaces();
        return false;
    }
    m_surfaceWidth = width;
    m_surfaceHeight = height;
    return true;
}

void CursorOverlay::destroySurfaces() noexcept {
    if (m_blackDc && m_previousBlackBitmap) {
        SelectObject(m_blackDc, m_previousBlackBitmap);
        m_previousBlackBitmap = nullptr;
    }
    if (m_whiteDc && m_previousWhiteBitmap) {
        SelectObject(m_whiteDc, m_previousWhiteBitmap);
        m_previousWhiteBitmap = nullptr;
    }
    if (m_blackBitmap) DeleteObject(m_blackBitmap);
    if (m_whiteBitmap) DeleteObject(m_whiteBitmap);
    if (m_blackDc) DeleteDC(m_blackDc);
    if (m_whiteDc) DeleteDC(m_whiteDc);
    m_blackDc = nullptr;
    m_whiteDc = nullptr;
    m_blackBitmap = nullptr;
    m_whiteBitmap = nullptr;
    m_blackBits = nullptr;
    m_whiteBits = nullptr;
    m_surfaceWidth = 0;
    m_surfaceHeight = 0;
}

bool CursorOverlay::updateCursorImage(HCURSOR cursor) {
    if (!cursor) return false;
    if (cursor != m_cachedCursor) {
        ICONINFO iconInfo{};
        if (!GetIconInfo(cursor, &iconInfo)) return false;
        BITMAP bitmap{};
        bool haveBitmap = false;
        if (iconInfo.hbmColor) {
            haveBitmap = GetObjectW(iconInfo.hbmColor, sizeof(bitmap), &bitmap) == sizeof(bitmap);
        } else if (iconInfo.hbmMask) {
            haveBitmap = GetObjectW(iconInfo.hbmMask, sizeof(bitmap), &bitmap) == sizeof(bitmap);
            if (haveBitmap) bitmap.bmHeight /= 2;
        }
        if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
        if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);
        if (!haveBitmap || bitmap.bmWidth <= 0 || bitmap.bmHeight <= 0 ||
            bitmap.bmWidth > MaxCursorDimension || bitmap.bmHeight > MaxCursorDimension) {
            return false;
        }
        m_cachedCursor = cursor;
        m_cursorWidth = bitmap.bmWidth;
        m_cursorHeight = bitmap.bmHeight;
        m_hotspotX = static_cast<int>(iconInfo.xHotspot);
        m_hotspotY = static_cast<int>(iconInfo.yHotspot);
        m_cursorBgra.resize(
            static_cast<std::size_t>(m_cursorWidth) * m_cursorHeight * 4);
    }
    if (!ensureSurfaces(m_cursorWidth, m_cursorHeight)) return false;

    const std::size_t pixelCount = static_cast<std::size_t>(m_cursorWidth) * m_cursorHeight;
    std::fill_n(m_blackBits, pixelCount, 0x00000000u);
    std::fill_n(m_whiteBits, pixelCount, 0x00FFFFFFu);
    if (!DrawIconEx(m_blackDc, 0, 0, cursor, m_cursorWidth, m_cursorHeight,
                    0, nullptr, DI_NORMAL) ||
        !DrawIconEx(m_whiteDc, 0, 0, cursor, m_cursorWidth, m_cursorHeight,
                    0, nullptr, DI_NORMAL)) {
        return false;
    }

    for (std::size_t index = 0; index < pixelCount; ++index) {
        const auto black = m_blackBits[index];
        const auto white = m_whiteBits[index];
        const int blackBlue = black & 0xFF;
        const int blackGreen = (black >> 8) & 0xFF;
        const int blackRed = (black >> 16) & 0xFF;
        const int delta = std::max({
            static_cast<int>(white & 0xFF) - blackBlue,
            static_cast<int>((white >> 8) & 0xFF) - blackGreen,
            static_cast<int>((white >> 16) & 0xFF) - blackRed
        });
        const int alpha = std::clamp(255 - delta, 0, 255);
        auto* destination = m_cursorBgra.data() + index * 4;
        destination[3] = static_cast<std::uint8_t>(alpha);
        if (alpha == 0) {
            destination[0] = destination[1] = destination[2] = 0;
        } else {
            destination[0] = static_cast<std::uint8_t>(
                std::clamp(blackBlue * 255 / alpha, 0, 255));
            destination[1] = static_cast<std::uint8_t>(
                std::clamp(blackGreen * 255 / alpha, 0, 255));
            destination[2] = static_cast<std::uint8_t>(
                std::clamp(blackRed * 255 / alpha, 0, 255));
        }
    }
    return true;
}

void CursorOverlay::blendPixel(CaptureFrameView& frame, int x, int y,
                               std::uint8_t red, std::uint8_t green,
                               std::uint8_t blue, std::uint8_t alpha) {
    if (alpha == 0 || x < 0 || y < 0 || x >= frame.width || y >= frame.height) return;
    const int pixelBytes = bytesPerPixel(frame.format);
    auto* pixel = frame.data + static_cast<std::size_t>(y) * frame.stride +
        static_cast<std::size_t>(x) * pixelBytes;
    const unsigned inverse = 255u - alpha;
    pixel[0] = static_cast<std::uint8_t>((blue * alpha + pixel[0] * inverse + 127) / 255);
    pixel[1] = static_cast<std::uint8_t>((green * alpha + pixel[1] * inverse + 127) / 255);
    pixel[2] = static_cast<std::uint8_t>((red * alpha + pixel[2] * inverse + 127) / 255);
    if (pixelBytes == 4) pixel[3] = 255;
}

CursorOverlay::Patch CursorOverlay::apply(CaptureFrameView& frame,
                                          const CaptureRegion& screenRegion,
                                          bool includeCursor,
                                          bool showClickEffects) {
    Patch patch;
    if (!frame.data || frame.width <= 0 || frame.height <= 0 || frame.stride <= 0) return patch;

    CURSORINFO cursorInfo{sizeof(CURSORINFO)};
    const bool haveCursor = GetCursorInfo(&cursorInfo) != FALSE;
    POINT pointer = haveCursor ? cursorInfo.ptScreenPos : POINT{};

    const bool leftButtonDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool rightButtonDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    const auto now = std::chrono::steady_clock::now();
    if (showClickEffects && leftButtonDown && !m_leftButtonDown) {
        if (!haveCursor) GetCursorPos(&pointer);
        m_clickPoint = pointer;
        m_clickStarted = now;
        m_clickActive = true;
        m_clickIsRight = false;
    } else if (showClickEffects && rightButtonDown && !m_rightButtonDown) {
        if (!haveCursor) GetCursorPos(&pointer);
        m_clickPoint = pointer;
        m_clickStarted = now;
        m_clickActive = true;
        m_clickIsRight = true;
    }
    m_leftButtonDown = leftButtonDown;
    m_rightButtonDown = rightButtonDown;

    bool drawCursor = includeCursor && haveCursor &&
        (cursorInfo.flags & CURSOR_SHOWING) != 0 && updateCursorImage(cursorInfo.hCursor);
    const int cursorX = pointer.x - screenRegion.x - m_hotspotX;
    const int cursorY = pointer.y - screenRegion.y - m_hotspotY;

    double clickProgress = 1.0;
    if (m_clickActive) {
        clickProgress = std::chrono::duration<double>(now - m_clickStarted).count() /
            std::chrono::duration<double>(ClickDuration).count();
        if (clickProgress >= 1.0 || !showClickEffects) m_clickActive = false;
    }
    const bool drawClick = m_clickActive && showClickEffects;
    const int clickX = m_clickPoint.x - screenRegion.x;
    const int clickY = m_clickPoint.y - screenRegion.y;
    const float dpiScale = easy::core::dpi::scaleAtPoint(m_clickPoint);
    const double radius = (8.0 + clickProgress * 24.0) * static_cast<double>(dpiScale);
    const double ringThickness = 3.6 * static_cast<double>(dpiScale);

    Bounds bounds;
    if (drawCursor) bounds.include(cursorX, cursorY, m_cursorWidth, m_cursorHeight);
    if (drawClick) {
        const int extent = static_cast<int>(std::ceil(radius + ringThickness + 1));
        bounds.include(clickX - extent, clickY - extent, extent * 2 + 1, extent * 2 + 1);
    }
    if (!bounds.clip(frame.width, frame.height)) return patch;

    patch.m_frameData = frame.data;
    patch.m_stride = frame.stride;
    patch.m_bytesPerPixel = bytesPerPixel(frame.format);
    patch.m_x = bounds.left;
    patch.m_y = bounds.top;
    patch.m_width = bounds.right - bounds.left;
    patch.m_height = bounds.bottom - bounds.top;
    const std::size_t rowBytes = static_cast<std::size_t>(patch.m_width) * patch.m_bytesPerPixel;
    patch.m_original.resize(rowBytes * patch.m_height);
    for (int row = 0; row < patch.m_height; ++row) {
        const auto* source = frame.data +
            static_cast<std::size_t>(patch.m_y + row) * frame.stride +
            static_cast<std::size_t>(patch.m_x) * patch.m_bytesPerPixel;
        std::memcpy(patch.m_original.data() + static_cast<std::size_t>(row) * rowBytes,
                    source, rowBytes);
    }

    if (drawCursor) {
        for (int y = 0; y < m_cursorHeight; ++y) {
            for (int x = 0; x < m_cursorWidth; ++x) {
                const auto* pixel = m_cursorBgra.data() +
                    (static_cast<std::size_t>(y) * m_cursorWidth + x) * 4;
                blendPixel(frame, cursorX + x, cursorY + y,
                           pixel[2], pixel[1], pixel[0], pixel[3]);
            }
        }
    }
    if (drawClick) {
        const int extent = static_cast<int>(std::ceil(radius + ringThickness + 1));
        const double baseAlpha = 220.0 * (1.0 - clickProgress);
        const std::uint8_t ringR = m_clickIsRight ? 255 : 26;
        const std::uint8_t ringG = m_clickIsRight ? 125 : 122;
        const std::uint8_t ringB = m_clickIsRight ? 0   : 255;

        for (int y = -extent; y <= extent; ++y) {
            for (int x = -extent; x <= extent; ++x) {
                const double distance = std::sqrt(static_cast<double>(x * x + y * y));
                const double edgeDistance = std::abs(distance - radius);
                if (edgeDistance > ringThickness) continue;
                const auto alpha = static_cast<std::uint8_t>(std::clamp(
                    baseAlpha * (1.0 - edgeDistance / ringThickness), 0.0, 255.0));
                blendPixel(frame, clickX + x, clickY + y, ringR, ringG, ringB, alpha);
            }
        }
    }
    return patch;
}

}  // namespace easy::capture
