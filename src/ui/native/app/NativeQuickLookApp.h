#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// NativeQuickLookApp.h — Tools3000 纯原生空格极速文件预览器应用中枢
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_APP_NATIVEQUICKLOOKAPP_H
#define TOOLS3000_UI_NATIVE_APP_NATIVEQUICKLOOKAPP_H

#include "ui/native/core/NativeView.h"
#include "ui/native/window/NativeFramelessWindow.h"
#include "ui/native/graphics/VectorIconRenderer.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <memory>

namespace tools3000::ui::native {

enum class QuickLookFileType {
    Text = 0,
    Code,
    Markdown,
    Image,
    Folder,
    Binary,
    Media
};

struct FolderItemEntry {
    std::wstring name;
    bool isDir = false;
    uint64_t sizeBytes = 0;
};

class NativeQuickLookView : public NativeView {
public:
    NativeQuickLookView();
    virtual ~NativeQuickLookView() override;

    virtual std::string getViewName() const override { return "QuickLookView"; }

    /// 加载并预览指定物理文件路径
    void previewFile(const std::wstring& filePath);

    /// 获取当前预览文件路径
    const std::wstring& getFilePath() const { return m_filePath; }

    /// 打开外部程序
    void openExternal();

    /// 打开所在文件夹
    void locateInFolder();

    /// 复制绝对路径
    void copyPath();

    // ── UIElement 生命周期 ──────────────────────────────────────────────────
    virtual Size measure(float availableWidth, float availableHeight, UIRenderContext& ctx) override;
    virtual void layout(const Rect& bounds, UIRenderContext& ctx) override;
    virtual void render(UIRenderContext& ctx) override;
    virtual bool update(float dt) override;

    // ── 键鼠交互 ────────────────────────────────────────────────────────────
    virtual bool onKeyDown(const UIKeyEvent& e) override;
    virtual bool onMouseWheel(const UIMouseEvent& e) override;
    virtual bool onMouseDown(const UIMouseEvent& e) override;

    // ── 诊断与单测接口 ──────────────────────────────────────────────────────
    QuickLookFileType getFileType() const { return m_fileType; }
    size_t getLineCount() const { return m_textLines.size(); }
    float getImageScale() const { return m_imageScale; }
    bool hasError() const { return m_hasError; }
    const std::wstring& getErrorMessage() const { return m_errorMessage; }

private:
    void loadFileInfo();
    void loadTextContent();
    void loadImageContent();
    void loadFolderContent();
    void loadBinaryHexDump();

private:
    std::wstring m_filePath;
    std::wstring m_fileName;
    uint64_t m_fileSize = 0;
    QuickLookFileType m_fileType = QuickLookFileType::Text;

    // 文本与代码预览数据
    std::vector<std::wstring> m_textLines;
    float m_scrollOffset = 0.0f;
    float m_fontSize = 13.0f;

    // 图像预览数据
    Microsoft::WRL::ComPtr<IWICImagingFactory> m_wicFactory;
    Microsoft::WRL::ComPtr<IWICFormatConverter> m_wicConverter;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_d2dBitmap;
    UINT m_imageWidth = 0;
    UINT m_imageHeight = 0;
    float m_imageScale = 1.0f;

    // 异常与空状态
    bool m_hasError = false;
    std::wstring m_errorMessage;

    // 文件夹预览数据
    std::vector<FolderItemEntry> m_folderEntries;

    // 二进制 Hex Dump 数据
    std::vector<std::wstring> m_hexLines;
};

class NativeQuickLookApp {
public:
    static NativeQuickLookApp& instance();

    /// 呼出预览窗口并预览文件
    void show(const std::wstring& filePath, HINSTANCE hInstance = nullptr);

    /// 切换预览目标文件
    void previewFile(const std::wstring& filePath);

    /// 隐藏
    void hide();

    /// 销毁
    void destroy();

    /// 是否可见
    bool isVisible() const;

    std::wstring currentFilePath() const;

    HWND hwnd() const { return m_host ? m_host->hwnd() : nullptr; }
    NativeFramelessWindow* getHost() { return m_host.get(); }
    std::shared_ptr<NativeQuickLookView> getView() const { return m_view; }

private:
    NativeQuickLookApp();
    ~NativeQuickLookApp();
    NativeQuickLookApp(const NativeQuickLookApp&) = delete;
    NativeQuickLookApp& operator=(const NativeQuickLookApp&) = delete;

    void ensureInitialized(HINSTANCE hInstance);

private:
    std::unique_ptr<NativeFramelessWindow> m_host;
    std::shared_ptr<NativeQuickLookView> m_view;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_APP_NATIVEQUICKLOOKAPP_H
