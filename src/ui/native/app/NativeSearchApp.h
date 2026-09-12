#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// NativeSearchApp.h — Tools3000 纯原生毫秒级全局快速文件搜索中心
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_APP_NATIVESEARCHAPP_H
#define TOOLS3000_UI_NATIVE_APP_NATIVESEARCHAPP_H

#include "ui/native/core/NativeView.h"
#include "ui/native/window/NativeFramelessWindow.h"
#include "ui/native/graphics/VectorIconRenderer.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>

namespace tools3000::ui::native {

struct SearchItemResult {
    std::wstring name;
    std::wstring fullPath;
    std::wstring ext;
    uint64_t sizeBytes = 0;
    uint64_t modifiedTime = 0;
    bool isDirectory = false;
    IconType icon = IconType::FileText;
};

enum class SearchCategory {
    All = 0,
    Documents,
    Folders,
    Images,
    Code,
    Compressed,
    Media,
    Executables
};

class NativeSearchView : public NativeView {
public:
    NativeSearchView();
    virtual ~NativeSearchView() override;

    virtual std::string getViewName() const override { return "SearchView"; }

    /// 设置并触发搜索词
    void setQuery(const std::wstring& q);
    const std::wstring& getQuery() const { return m_query; }

    /// 切换搜索分类
    void setCategory(SearchCategory cat);

    /// 执行搜索检索
    void executeSearch();

    /// 打开当前选中的项目
    void openSelected();

    /// 打开所在文件夹并高亮选定
    void openSelectedFolder();

    /// 复制当前选中项目的绝对路径
    void copySelectedPath();

    /// 设置并获取窗口固定置顶状态
    void setPinned(bool pinned);
    bool isPinned() const { return m_isPinned; }

    // ── UIElement 生命周期 ──────────────────────────────────────────────────
    virtual void onActivated() override;
    virtual Size measure(float availableWidth, float availableHeight, UIRenderContext& ctx) override;
    virtual void layout(const Rect& bounds, UIRenderContext& ctx) override;
    virtual void render(UIRenderContext& ctx) override;
    virtual bool update(float dt) override;

    // ── 键鼠事件与虚拟列表导航 ──────────────────────────────────────────────
    virtual bool onMouseDown(const UIMouseEvent& e) override;
    virtual bool onMouseMove(const UIMouseEvent& e) override;
    virtual bool onMouseWheel(const UIMouseEvent& e) override;
    virtual bool onKeyDown(const UIKeyEvent& e) override;
    virtual bool onChar(const UIKeyEvent& e) override;

    // ── 结果集访问（供单测与诊断使用） ────────────────────────────────────────
    size_t getResultCount() const {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        return m_filteredResults.size();
    }
    int getSelectedIndex() const {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        return m_selectedIndex;
    }
    void setSelectedIndex(int idx) {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        m_selectedIndex = idx;
        markNeedsPaint();
    }

private:
    void filterResults();
    void filterResultsLocked();
    void buildMockIndex();
    static std::wstring formatFileSize(uint64_t bytes);
    static IconType detectIconForExtension(const std::wstring& ext, bool isDir);
    void dispatchSearchQuery();

private:
    std::wstring m_query;
    SearchCategory m_category = SearchCategory::All;
    std::vector<SearchItemResult> m_allResults;
    std::vector<SearchItemResult> m_filteredResults;
    bool m_hasBackendResults = false;
    bool m_isServiceStarting = false;
    float m_queryDebounceTimer = 0.0f;
    float m_retryTimer = 0.0f;
    int m_retryCount = 0;
    uint64_t m_currentQueryId = 0;
    mutable std::mutex m_resultsMutex;

    int m_selectedIndex = 0;
    int m_hoveredIndex = -1;
    float m_scrollOffset = 0.0f;
    float m_targetScrollOffset = 0.0f;
    float m_elapsedMs = 0.8f;

    // 搜索框光标闪烁
    float m_cursorBlinkTime = 0.0f;
    bool m_cursorVisible = true;

    // 右键上下文菜单状态
    bool m_contextMenuVisible = false;
    Point m_contextMenuPos;
    int m_contextMenuHover = -1;

    // 窗口固定置顶状态
    bool m_isPinned = false;
};

class NativeSearchApp {
public:
    static NativeSearchApp& instance();

    /// 预热 Direct2D 搜索宿主环境
    void preload(HINSTANCE hInstance = nullptr);

    /// 居中呼出搜索中心
    void show(HINSTANCE hInstance = nullptr);

    /// 隐藏搜索中心并在冷路径释放工作集内存
    void hide();

    /// 销毁
    void destroy();

    /// 是否可见
    bool isVisible() const;

    /// 设置并获取窗口固定置顶状态
    void setPinned(bool pinned);
    bool isPinned() const;

    /// 激活并聚焦搜索框（若当前窗口可见）
    void focusSearchIfVisible();

    /// 动态设置窗口尺寸
    void setWindowSize(int w, int h, bool forceCenter = false);
    std::pair<int, int> getWindowSize() const;

    HWND hwnd() const { return m_host ? m_host->hwnd() : nullptr; }
    NativeFramelessWindow* getHost() { return m_host.get(); }
    std::shared_ptr<NativeSearchView> getView() const { return m_view; }

private:
    NativeSearchApp();
    ~NativeSearchApp();
    NativeSearchApp(const NativeSearchApp&) = delete;
    NativeSearchApp& operator=(const NativeSearchApp&) = delete;

    void ensureInitialized(HINSTANCE hInstance);

private:
    std::unique_ptr<NativeFramelessWindow> m_host;
    std::shared_ptr<NativeSearchView> m_view;
    int m_width = 860;
    int m_height = 560;
    bool m_isPinned = false;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_APP_NATIVESEARCHAPP_H
