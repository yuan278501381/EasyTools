#include "capture/CaptureBackend.h"

#include "core/logger/Logger.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <array>
#include <cstdio>
#include <limits>
#include <memory>
#include <utility>

namespace easy::capture {
namespace {

using Microsoft::WRL::ComPtr;

std::string hresultText(HRESULT result) {
    char buffer[16]{};
    sprintf_s(buffer, "0x%08X", static_cast<unsigned>(result));
    return buffer;
}

bool containsRegion(const RECT& output, const CaptureRegion& region) {
    const auto right = static_cast<long long>(region.x) + region.width;
    const auto bottom = static_cast<long long>(region.y) + region.height;
    return region.x >= output.left && region.y >= output.top &&
           right <= output.right && bottom <= output.bottom;
}

HRESULT createDevice(IDXGIAdapter* adapter, ComPtr<ID3D11Device>& device,
                     ComPtr<ID3D11DeviceContext>& context) {
    constexpr std::array featureLevels{
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL selected{};
    HRESULT result = D3D11CreateDevice(
        adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        featureLevels.data(), static_cast<UINT>(featureLevels.size()), D3D11_SDK_VERSION,
        &device, &selected, &context);
    if (result == E_INVALIDARG) {
        result = D3D11CreateDevice(
            adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            featureLevels.data() + 1, static_cast<UINT>(featureLevels.size() - 1),
            D3D11_SDK_VERSION, &device, &selected, &context);
    }
    return result;
}

class GdiCaptureBackend final : public ICaptureBackend {
public:
    ~GdiCaptureBackend() override { shutdown(); }

    const CaptureBackendInfo& info() const noexcept override { return m_info; }

    bool initialize(const CaptureRegion& region, std::string& error) override {
        shutdown();
        if (!region.isValid() || region.width > 32768 || region.height > 32768 ||
            region.width > (std::numeric_limits<int>::max() - 3) / 3) {
            error = "invalid GDI capture region";
            return false;
        }
        m_region = region;
        m_screenDc = GetDC(nullptr);
        if (!m_screenDc) {
            error = "GetDC failed (" + std::to_string(GetLastError()) + ")";
            return false;
        }
        m_memoryDc = CreateCompatibleDC(m_screenDc);
        if (!m_memoryDc) {
            error = "CreateCompatibleDC failed (" + std::to_string(GetLastError()) + ")";
            shutdown();
            return false;
        }

        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = region.width;
        bitmapInfo.bmiHeader.biHeight = -region.height;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 24;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;
        m_bitmap = CreateDIBSection(m_screenDc, &bitmapInfo, DIB_RGB_COLORS,
                                    &m_bits, nullptr, 0);
        if (!m_bitmap || !m_bits) {
            error = "CreateDIBSection failed (" + std::to_string(GetLastError()) + ")";
            shutdown();
            return false;
        }
        m_previousBitmap = SelectObject(m_memoryDc, m_bitmap);
        if (!m_previousBitmap || m_previousBitmap == HGDI_ERROR) {
            error = "SelectObject failed (" + std::to_string(GetLastError()) + ")";
            m_previousBitmap = nullptr;
            shutdown();
            return false;
        }
        m_stride = (region.width * 3 + 3) & ~3;
        return true;
    }

    bool capture(CaptureFrameView& frame, std::string& error) override {
        if (!m_screenDc || !m_memoryDc || !m_bits) {
            error = "GDI capture backend is not initialized";
            return false;
        }
        if (!BitBlt(m_memoryDc, 0, 0, m_region.width, m_region.height,
                    m_screenDc, m_region.x, m_region.y, SRCCOPY | CAPTUREBLT)) {
            error = "BitBlt failed (" + std::to_string(GetLastError()) + ")";
            return false;
        }
        frame = {
            static_cast<std::uint8_t*>(m_bits), m_stride,
            m_region.width, m_region.height, CapturePixelFormat::Bgr24
        };
        return true;
    }

    void releaseFrame() noexcept override {}

    void shutdown() noexcept override {
        if (m_memoryDc && m_previousBitmap) {
            SelectObject(m_memoryDc, m_previousBitmap);
            m_previousBitmap = nullptr;
        }
        if (m_bitmap) {
            DeleteObject(m_bitmap);
            m_bitmap = nullptr;
        }
        m_bits = nullptr;
        if (m_memoryDc) {
            DeleteDC(m_memoryDc);
            m_memoryDc = nullptr;
        }
        if (m_screenDc) {
            ReleaseDC(nullptr, m_screenDc);
            m_screenDc = nullptr;
        }
        m_stride = 0;
        m_region = {};
    }

private:
    CaptureBackendInfo m_info{"gdi-bitblt", "GDI BitBlt", true, false, {}};
    CaptureRegion m_region;
    HDC m_screenDc = nullptr;
    HDC m_memoryDc = nullptr;
    HBITMAP m_bitmap = nullptr;
    HGDIOBJ m_previousBitmap = nullptr;
    void* m_bits = nullptr;
    int m_stride = 0;
};

class DesktopDuplicationBackend final : public ICaptureBackend {
public:
    ~DesktopDuplicationBackend() override { shutdown(); }

    const CaptureBackendInfo& info() const noexcept override { return m_info; }

    bool initialize(const CaptureRegion& region, std::string& error) override {
        shutdown();
        if (!region.isValid() || region.width > 32768 || region.height > 32768) {
            error = "invalid Desktop Duplication capture region";
            return false;
        }

        ComPtr<IDXGIFactory1> factory;
        HRESULT result = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if (FAILED(result)) {
            error = "CreateDXGIFactory1 failed (" + hresultText(result) + ")";
            return false;
        }

        ComPtr<IDXGIAdapter1> selectedAdapter;
        ComPtr<IDXGIOutput> selectedOutput;
        DXGI_OUTPUT_DESC selectedDesc{};
        for (UINT adapterIndex = 0; ; ++adapterIndex) {
            ComPtr<IDXGIAdapter1> adapter;
            const HRESULT adapterResult = factory->EnumAdapters1(adapterIndex, &adapter);
            if (adapterResult == DXGI_ERROR_NOT_FOUND) break;
            if (FAILED(adapterResult) || !adapter) continue;
            for (UINT outputIndex = 0; ; ++outputIndex) {
                ComPtr<IDXGIOutput> output;
                const HRESULT outputResult = adapter->EnumOutputs(outputIndex, &output);
                if (outputResult == DXGI_ERROR_NOT_FOUND) break;
                if (FAILED(outputResult) || !output) continue;
                DXGI_OUTPUT_DESC desc{};
                if (SUCCEEDED(output->GetDesc(&desc)) && desc.AttachedToDesktop &&
                    containsRegion(desc.DesktopCoordinates, region)) {
                    selectedAdapter = adapter;
                    selectedOutput = output;
                    selectedDesc = desc;
                    break;
                }
            }
            if (selectedOutput) break;
        }
        if (!selectedOutput) {
            error = "capture region spans displays or has no attached DXGI output";
            return false;
        }
        // Duplication returns an unrotated surface with rotated pixels. Until a
        // GPU rotation pass exists, prefer the compatibility backend for accuracy.
        if (selectedDesc.Rotation != DXGI_MODE_ROTATION_IDENTITY &&
            selectedDesc.Rotation != DXGI_MODE_ROTATION_UNSPECIFIED) {
            error = "rotated display requires the compatibility backend";
            return false;
        }

        result = createDevice(selectedAdapter.Get(), m_device, m_context);
        if (FAILED(result)) {
            error = "D3D11CreateDevice failed (" + hresultText(result) + ")";
            shutdown();
            return false;
        }
        ComPtr<IDXGIOutput1> output1;
        result = selectedOutput.As(&output1);
        if (FAILED(result)) {
            error = "IDXGIOutput1 is unavailable (" + hresultText(result) + ")";
            shutdown();
            return false;
        }
        result = output1->DuplicateOutput(m_device.Get(), &m_duplication);
        if (FAILED(result)) {
            error = "DuplicateOutput failed (" + hresultText(result) + ")";
            shutdown();
            return false;
        }

        D3D11_TEXTURE2D_DESC textureDesc{};
        textureDesc.Width = static_cast<UINT>(region.width);
        textureDesc.Height = static_cast<UINT>(region.height);
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 1;
        textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Usage = D3D11_USAGE_STAGING;
        textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        result = m_device->CreateTexture2D(&textureDesc, nullptr, &m_stagingTexture);
        if (FAILED(result)) {
            error = "CreateTexture2D staging surface failed (" + hresultText(result) + ")";
            shutdown();
            return false;
        }

        m_region = region;
        m_outputCoordinates = selectedDesc.DesktopCoordinates;
        return true;
    }

    bool capture(CaptureFrameView& frame, std::string& error) override {
        releaseFrame();
        if (!m_duplication || !m_context || !m_stagingTexture) {
            error = "Desktop Duplication backend is not initialized";
            return false;
        }

        DXGI_OUTDUPL_FRAME_INFO frameInfo{};
        ComPtr<IDXGIResource> desktopResource;
        const HRESULT acquireResult = m_duplication->AcquireNextFrame(
            m_hasFrame ? 0u : 250u, &frameInfo, &desktopResource);
        if (acquireResult == DXGI_ERROR_ACCESS_LOST) {
            error = "desktop duplication access was lost";
            return false;
        }
        if (acquireResult != DXGI_ERROR_WAIT_TIMEOUT && FAILED(acquireResult)) {
            error = "AcquireNextFrame failed (" + hresultText(acquireResult) + ")";
            return false;
        }

        if (SUCCEEDED(acquireResult)) {
            ComPtr<ID3D11Texture2D> desktopTexture;
            const HRESULT textureResult = desktopResource.As(&desktopTexture);
            if (FAILED(textureResult)) {
                m_duplication->ReleaseFrame();
                error = "desktop resource is not a D3D11 texture (" +
                    hresultText(textureResult) + ")";
                return false;
            }
            const UINT left = static_cast<UINT>(m_region.x - m_outputCoordinates.left);
            const UINT top = static_cast<UINT>(m_region.y - m_outputCoordinates.top);
            const D3D11_BOX sourceBox{
                left, top, 0, left + static_cast<UINT>(m_region.width),
                top + static_cast<UINT>(m_region.height), 1
            };
            m_context->CopySubresourceRegion(
                m_stagingTexture.Get(), 0, 0, 0, 0,
                desktopTexture.Get(), 0, &sourceBox);
            m_duplication->ReleaseFrame();
            m_hasFrame = true;
        } else if (!m_hasFrame) {
            error = "timed out waiting for the first desktop frame";
            return false;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT mapResult = m_context->Map(
            m_stagingTexture.Get(), 0, D3D11_MAP_READ_WRITE, 0, &mapped);
        if (FAILED(mapResult)) {
            error = "Map staging surface failed (" + hresultText(mapResult) + ")";
            return false;
        }
        m_mapped = true;
        frame = {
            static_cast<std::uint8_t*>(mapped.pData),
            static_cast<int>(mapped.RowPitch), m_region.width, m_region.height,
            CapturePixelFormat::Bgra32
        };
        return true;
    }

    void releaseFrame() noexcept override {
        if (m_mapped && m_context && m_stagingTexture) {
            m_context->Unmap(m_stagingTexture.Get(), 0);
            m_mapped = false;
        }
    }

    void shutdown() noexcept override {
        releaseFrame();
        m_stagingTexture.Reset();
        m_duplication.Reset();
        m_context.Reset();
        m_device.Reset();
        m_region = {};
        m_outputCoordinates = {};
        m_hasFrame = false;
    }

private:
    CaptureBackendInfo m_info{
        "dxgi-desktop-duplication", "DXGI Desktop Duplication", true, true, {}
    };
    CaptureRegion m_region;
    RECT m_outputCoordinates{};
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGIOutputDuplication> m_duplication;
    ComPtr<ID3D11Texture2D> m_stagingTexture;
    bool m_hasFrame = false;
    bool m_mapped = false;
};

class AutomaticCaptureBackend final : public ICaptureBackend {
public:
    ~AutomaticCaptureBackend() override { shutdown(); }

    const CaptureBackendInfo& info() const noexcept override {
        return m_active ? m_active->info() : m_unavailableInfo;
    }

    bool initialize(const CaptureRegion& region, std::string& error) override {
        shutdown();
        m_region = region;
        auto accelerated = std::make_unique<DesktopDuplicationBackend>();
        std::string acceleratedError;
        if (accelerated->initialize(region, acceleratedError)) {
            m_active = std::move(accelerated);
            return true;
        }
        LOG_WARN("DXGI 捕获不可用，回退 GDI: {}", acceleratedError);
        return switchToGdi(error);
    }

    bool capture(CaptureFrameView& frame, std::string& error) override {
        if (!m_active) {
            error = "no capture backend is active";
            return false;
        }
        if (m_active->capture(frame, error)) return true;
        if (!m_active->info().accelerated) return false;

        const std::string acceleratedError = error;
        m_active->releaseFrame();
        m_active->shutdown();
        m_active.reset();

        std::string fallbackError;
        if (!switchToGdi(fallbackError)) {
            error = acceleratedError + "; GDI fallback failed: " + fallbackError;
            return false;
        }
        LOG_WARN("DXGI 捕获运行时失败，已切换 GDI: {}", acceleratedError);
        return m_active->capture(frame, error);
    }

    void releaseFrame() noexcept override {
        if (m_active) m_active->releaseFrame();
    }

    void shutdown() noexcept override {
        if (m_active) {
            m_active->releaseFrame();
            m_active->shutdown();
            m_active.reset();
        }
        m_region = {};
    }

private:
    bool switchToGdi(std::string& error) {
        auto fallback = std::make_unique<GdiCaptureBackend>();
        if (!fallback->initialize(m_region, error)) return false;
        m_active = std::move(fallback);
        return true;
    }

    CaptureBackendInfo m_unavailableInfo{
        "unavailable", "No capture backend", false, false, "not initialized"
    };
    CaptureRegion m_region;
    std::unique_ptr<ICaptureBackend> m_active;
};

bool hasAttachedDxgiOutput() {
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;
    for (UINT adapterIndex = 0; ; ++adapterIndex) {
        ComPtr<IDXGIAdapter1> adapter;
        const HRESULT adapterResult = factory->EnumAdapters1(adapterIndex, &adapter);
        if (adapterResult == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(adapterResult) || !adapter) continue;
        for (UINT outputIndex = 0; ; ++outputIndex) {
            ComPtr<IDXGIOutput> output;
            const HRESULT outputResult = adapter->EnumOutputs(outputIndex, &output);
            if (outputResult == DXGI_ERROR_NOT_FOUND) break;
            if (FAILED(outputResult) || !output) continue;
            DXGI_OUTPUT_DESC desc{};
            if (SUCCEEDED(output->GetDesc(&desc)) && desc.AttachedToDesktop) return true;
        }
    }
    return false;
}

}  // namespace

std::vector<CaptureBackendInfo> captureBackendCapabilities() {
    const bool dxgiAvailable = hasAttachedDxgiOutput();
    return {
        {"dxgi-desktop-duplication", "DXGI Desktop Duplication", dxgiAvailable, true,
         dxgiAvailable ? "" : "no attached DXGI output"},
        {"gdi-bitblt", "GDI BitBlt", true, false, {}}
    };
}

std::unique_ptr<ICaptureBackend> createCaptureBackend() {
    return std::make_unique<AutomaticCaptureBackend>();
}

}  // namespace easy::capture
