#include "ui/native/app/NativeSearchApp.h"
#include "ui/native/app/NativeSettingsApp.h"
#include "ui/native/common/UITheme.h"
#include "ui/native/graphics/GlassmorphismRenderer.h"
#include "core/utils/WinUtils.h"
#include "core/utils/DpiUtils.h"
#include "core/ipc/MessageBridge.h"
#include <nlohmann/json.hpp>
#include <shellapi.h>
#include <algorithm>
#include <cmath>

namespace tools3000::ui::native {

NativeSearchView::NativeSearchView() {
    buildMockIndex();
    filterResults();
}

NativeSearchView::~NativeSearchView() = default;

void NativeSearchView::setPinned(bool pinned) {
    m_isPinned = pinned;
    if (auto* frameless = dynamic_cast<NativeFramelessWindow*>(m_host)) {
        frameless->setAutoCloseOnBlur(!pinned);
        frameless->setAlwaysOnTop(pinned);
    }
    markNeedsPaint();
}

void NativeSearchView::buildMockIndex() {
    m_allResults = {
        { L"Tools3000.exe", L"C:\\Tools3000\\bin\\Tools3000.exe", L".exe", 1458200, 1757600000, false, IconType::Cpu },
        { L"Tools3000_Guide.md", L"C:\\repo\\easyTools\\docs\\Tools3000_Guide.md", L".md", 15200, 1757550000, false, IconType::FileText },
        { L"Tools3000_Architecture.txt", L"C:\\repo\\easyTools\\docs\\Tools3000_Architecture.txt", L".txt", 8920, 1757540000, false, IconType::FileText },
        { L"README.md", L"C:\\repo\\easyTools\\README.md", L".md", 12450, 1757500000, false, IconType::FileText },
        { L"CMakeLists.txt", L"C:\\repo\\easyTools\\CMakeLists.txt", L".txt", 28175, 1757400000, false, IconType::Code },
        { L"NativeSettingsApp.cpp", L"C:\\repo\\easyTools\\src\\ui\\native\\app\\NativeSettingsApp.cpp", L".cpp", 98839, 1757300000, false, IconType::Code },
        { L"NativeTrayApp.cpp", L"C:\\repo\\easyTools\\src\\ui\\native\\app\\NativeTrayApp.cpp", L".cpp", 18450, 1757200000, false, IconType::Code },
        { L"NativeSearchApp.cpp", L"C:\\repo\\easyTools\\src\\ui\\native\\app\\NativeSearchApp.cpp", L".cpp", 24300, 1757100000, false, IconType::Code },
        { L"NativeQuickLookApp.cpp", L"C:\\repo\\easyTools\\src\\ui\\native\\app\\NativeQuickLookApp.cpp", L".cpp", 21500, 1757000000, false, IconType::Code },
        { L"hero.png", L"C:\\repo\\easyTools\\ui\\src\\assets\\hero.png", L".png", 345020, 1756900000, false, IconType::Crop },
        { L"noto-sans-sc-500.woff2", L"C:\\repo\\easyTools\\ui\\src\\assets\\fonts\\noto-sans-sc-500.woff2", L".woff2", 1150000, 1756800000, false, IconType::FileDigit },
        { L"Tools3000_Installer.exe", L"C:\\repo\\easyTools\\build\\deploy_dist\\Tools3000_Setup.exe", L".exe", 18400500, 1756700000, false, IconType::Cpu },
        { L"config.json", L"C:\\Users\\yuan2\\AppData\\Roaming\\Tools3000\\config.json", L".json", 4096, 1756600000, false, IconType::Code },
        { L"SystemLogs.log", L"C:\\Tools3000\\logs\\tools3000.log", L".log", 84520, 1756500000, false, IconType::FileText },
        { L"easyTools", L"C:\\repo\\easyTools", L"", 0, 1756400000, true, IconType::Folder },
        { L"src", L"C:\\repo\\easyTools\\src", L"", 0, 1756300000, true, IconType::Folder },
        { L"plugins", L"C:\\repo\\easyTools\\build\\bin\\plugins", L"", 0, 1756200000, true, IconType::Folder }
    };
}

std::wstring NativeSearchView::formatFileSize(uint64_t bytes) {
    if (bytes == 0) return L"--";
    if (bytes < 1024) return std::to_wstring(bytes) + L" B";
    if (bytes < 1024 * 1024) {
        wchar_t buf[32];
        swprintf_s(buf, L"%.1f KB", bytes / 1024.0f);
        return buf;
    }
    if (bytes < 1024 * 1024 * 1024) {
        wchar_t buf[32];
        swprintf_s(buf, L"%.1f MB", bytes / (1024.0f * 1024.0f));
        return buf;
    }
    wchar_t buf[32];
    swprintf_s(buf, L"%.2f GB", bytes / (1024.0f * 1024.0f * 1024.0f));
    return buf;
}

IconType NativeSearchView::detectIconForExtension(const std::wstring& ext, bool isDir) {
    if (isDir) return IconType::Folder;
    if (ext == L".cpp" || ext == L".c" || ext == L".h" || ext == L".hpp" || ext == L".ts" ||
        ext == L".js" || ext == L".jsx" || ext == L".tsx" || ext == L".json" || ext == L".py" ||
        ext == L".rs" || ext == L".go" || ext == L".cs" || ext == L".java" || ext == L".html" ||
        ext == L".css" || ext == L".scss" || ext == L".sql" || ext == L".ps1" || ext == L".bat" ||
        ext == L".cmd" || ext == L".sh" || ext == L".xml" || ext == L".yaml" || ext == L".yml" ||
        ext == L".toml" || ext == L".ini") {
        return IconType::Code;
    }
    if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".webp" ||
        ext == L".bmp" || ext == L".gif" || ext == L".ico" || ext == L".svg" || ext == L".psd") {
        return IconType::Crop;
    }
    if (ext == L".mp4" || ext == L".mkv" || ext == L".avi" || ext == L".mov" || ext == L".wmv" || ext == L".flv") {
        return IconType::Video;
    }
    if (ext == L".mp3" || ext == L".wav" || ext == L".flac" || ext == L".aac" || ext == L".ogg" || ext == L".m4a") {
        return IconType::Music;
    }
    if (ext == L".exe" || ext == L".dll" || ext == L".sys" || ext == L".msi") {
        return IconType::Cpu;
    }
    if (ext == L".zip" || ext == L".7z" || ext == L".rar" || ext == L".tar" || ext == L".gz" || ext == L".bz2") {
        return IconType::FolderOpen;
    }
    return IconType::FileText;
}

void NativeSearchView::setQuery(const std::wstring& q) {
    m_query = q;
    m_cursorVisible = true;
    m_cursorBlinkTime = 0.0f;
    m_retryTimer = 0.0f;
    m_retryCount = 0;

    ++m_currentQueryId;

    if (m_query.empty()) {
        m_queryDebounceTimer = 0.0f;
        m_isServiceStarting = false;
        {
            std::lock_guard<std::mutex> lock(m_resultsMutex);
            m_hasBackendResults = false;
            buildMockIndex();
            filterResultsLocked();
        }
        markNeedsPaint();
        return;
    }

    // 先做即时本地过滤，提供键入时的即刻响应反馈
    {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        filterResultsLocked();
    }
    markNeedsPaint();

    // 启用 120ms 防抖计时器，避免高速击键时频繁击穿后端工作线程池
    m_queryDebounceTimer = 0.12f;
}

void NativeSearchView::dispatchSearchQuery() {
    std::wstring rawQ = m_query;
    while (!rawQ.empty() && (rawQ.front() == L' ' || rawQ.front() == L'\t' || rawQ.front() == L'　')) {
        rawQ.erase(0, 1);
    }
    if (rawQ.empty()) return;

    // 若带有 '/' 前缀（如 /boost），向后端检索去斜杠后的关键词（boost），兼顾指令与文件检索
    std::wstring effectiveQuery = rawQ;
    if (effectiveQuery.front() == L'/' && effectiveQuery.size() > 1) {
        effectiveQuery.erase(0, 1);
    }

    uint64_t thisQueryId = m_currentQueryId;
    std::wstring querySnapshot = m_query;

    static std::atomic<int> s_queryCounter{1000};
    int reqId = ++s_queryCounter;

    nlohmann::json req;
    req["id"] = reqId;
    req["method"] = "search.query";
    req["params"]["query"] = tools3000::core::WinUtils::wstringToUtf8(effectiveQuery);
    req["params"]["limit"] = 200;
    req["params"]["searchMode"] = "name";

    tools3000::core::MessageBridge::instance().handleMessageAsync(
        req.dump(),
        [this, thisQueryId, querySnapshot](std::string resp) {
            try {
                auto j = nlohmann::json::parse(resp);
                if (j.contains("result")) {
                    const auto& res = j["result"];
                    std::string status = res.value("status", "ready");
                    bool available = res.value("available", true);

                    // 若检索引擎尚在后台冷启动、按需预热或管道握手就绪中
                    if (status == "starting" || !available) {
                        {
                            std::lock_guard<std::mutex> lock(m_resultsMutex);
                            if (thisQueryId != m_currentQueryId || m_query != querySnapshot) {
                                return;
                            }
                            if (m_retryCount < 25) {
                                m_isServiceStarting = true;
                                m_retryTimer = 0.35f;
                                markNeedsPaint();
                                return;
                            }
                            m_isServiceStarting = false;
                        }
                        markNeedsPaint();
                    }

                    // 检索就绪并成功返回结果
                    if (res.contains("results") && res["results"].is_array()) {
                        std::vector<SearchItemResult> items;
                        for (const auto& item : res["results"]) {
                            std::string p = item.value("path", "");
                            if (p.empty()) continue;
                            SearchItemResult r;
                            r.fullPath = tools3000::core::WinUtils::utf8ToWstring(p);
                            std::string nm = item.value("name", "");
                            r.name = nm.empty() ? r.fullPath : tools3000::core::WinUtils::utf8ToWstring(nm);
                            r.sizeBytes = item.value("size", 0ULL);
                            r.modifiedTime = item.value("lastWriteTime", item.value("modified", 0ULL));
                            r.isDirectory = item.value("isDirectory", item.value("isDir", false));
                            size_t dot = r.name.find_last_of(L'.');
                            r.ext = (dot != std::wstring::npos) ? r.name.substr(dot) : L"";
                            r.icon = detectIconForExtension(r.ext, r.isDirectory);
                            items.push_back(std::move(r));
                        }
                        float elapsed = static_cast<float>(res.value("elapsedMs", 0));

                        {
                            std::lock_guard<std::mutex> lock(m_resultsMutex);
                            // 丢弃过时的异步返回
                            if (thisQueryId != m_currentQueryId || m_query != querySnapshot) {
                                return;
                            }
                            m_allResults = std::move(items);
                            m_hasBackendResults = true;
                            m_isServiceStarting = false;
                            m_retryTimer = 0.0f;
                            m_retryCount = 0;
                            m_elapsedMs = elapsed > 0.0f ? elapsed : 0.8f;
                            filterResultsLocked();
                        }
                        markNeedsPaint();
                    }
                }
            } catch (...) {}
        }
    );
}

void NativeSearchView::setCategory(SearchCategory cat) {
    m_category = cat;
    filterResults();
}

void NativeSearchView::executeSearch() {
    filterResults();
}

void NativeSearchView::filterResults() {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    filterResultsLocked();
}

void NativeSearchView::filterResultsLocked() {
    m_filteredResults.clear();
    std::wstring lq = m_query;
    std::transform(lq.begin(), lq.end(), lq.begin(), ::towlower);

    // 1. 提取去斜杠与去空格关键词
    std::wstring cleanQ = lq;
    while (!cleanQ.empty() && (cleanQ.front() == L'/' || cleanQ.front() == L' ' || cleanQ.front() == L'\t' || cleanQ.front() == L'　')) {
        cleanQ.erase(0, 1);
    }
    while (!cleanQ.empty() && (cleanQ.back() == L' ' || cleanQ.back() == L'\t' || cleanQ.back() == L'　')) {
        cleanQ.pop_back();
    }

    // 2. 匹配内置快捷动作与模块命令 (Builtin Commands)
    // 在分类为「全部」、「程序」，或者用户明确输入以 '/' 开头的指令时，展示系统内置工具指令
    if (m_category == SearchCategory::All || m_category == SearchCategory::Executables || lq.starts_with(L'/')) {
        static const struct BuiltinCmdDef {
            const wchar_t* name;
            const wchar_t* target;
            const wchar_t* desc;
            const wchar_t* alias;
            IconType icon;
        } s_builtinCmds[] = {
            { L"远控加速 (Remote Boost)", L"tools3000://settings/remote_boost",
              L"主控端沉浸式热键直通、修饰键卡死急救与输入法智能脱敏 · 回车直达配置中心",
              L"boost 远控 remote remoteboost remote_boost /boost", IconType::Cpu },
            { L"超级截图 (Capture)", L"tools3000://action/capture",
              L"智能选区吸附、图像美化外壳与贴图 · 回车立即唤起截图",
              L"capture screenshot 截图 截屏 /capture /screenshot", IconType::Crop },
            { L"屏幕录制 (Screen Record)", L"tools3000://action/record",
              L"高清硬件加速选区录屏与音视频捕获 · 回车立即开始录制",
              L"record 录屏 录像 /record", IconType::Video },
            { L"按键统计与热力图 (Keycast Stats)", L"tools3000://settings/stats",
              L"104/87/60 物理键盘击键频次热力学高斯渲染与全景数据洞察 · 回车打开",
              L"stats 热力图 键盘热力图 按键统计 统计 /stats /heatmap", IconType::BarChart3 },
            { L"离线文字识别 (Offline OCR)", L"tools3000://settings/ocr",
              L"基于 Windows 原生 OCR 引擎纯本地安全识别 · 回车打开",
              L"ocr 文字识别 提取文字 /ocr", IconType::FileText },
            { L"按键播报 (Keycast)", L"tools3000://settings/keycast",
              L"屏幕实时回显击键与组合快捷键，演示录屏利器 · 回车打开",
              L"keycast 按键播报 快捷键显示 播报 /keycast", IconType::Keyboard },
            { L"聚合高亮与特效 (Spotlight)", L"tools3000://settings/spotlight",
              L"光标聚光灯聚焦、点击水波纹与移动流光轨迹 · 回车打开",
              L"spotlight 聚光灯 水波纹 流光 高亮 /spotlight", IconType::Sparkles },
            { L"对话框增强 (Dialog Enhancer)", L"tools3000://settings/dialog_enhancer",
              L"文件对话框智能跳跃、快捷固定与路径记忆 · 回车打开",
              L"dialog 对话框增强 快速跳转 对话框 /dialog", IconType::Folder },
            { L"设置中心 (Settings)", L"tools3000://settings/general",
              L"Tools3000 原生极速配置中心 · 回车打开",
              L"settings 设置 偏好设置 配置 通用 /settings /set /config", IconType::Sliders },
            { L"屏幕拾色器 (Color Picker)", L"tools3000://settings/color_picker",
              L"多格式色值复制 (HEX / RGB / HSL) 与像素级放大镜 · 回车打开",
              L"color 拾色器 取色器 吸管 颜色 /color", IconType::Palette },
            { L"剪贴板历史 (Clipboard)", L"tools3000://settings/clipboard_manager",
              L"本地安全存储剪贴板历史，支持文本、代码与图片 · 回车打开",
              L"clipboard 剪贴板 历史记录 剪切板 /clipboard /clip", IconType::Copy },
            { L"Markdown 预览 (Markdown Preview)", L"tools3000://settings/markdown_preview",
              L"空格极速预览 Markdown 文档与代码高亮 · 回车打开",
              L"markdown md 预览 标记 /md /markdown", IconType::FileText },
            { L"关于产品 (About)", L"tools3000://settings/about",
              L"Tools3000 版本信息、开源许可与架构技术栈 · 回车打开",
              L"about 关于 版本 版权 /about /version", IconType::Info },
        };

        for (const auto& cmd : s_builtinCmds) {
            bool matches = false;
            if (lq.empty()) {
                matches = false;
            } else if (lq == L"/") {
                matches = true;
            } else {
                std::wstring alias = cmd.alias;
                std::wstring name = cmd.name;
                std::transform(name.begin(), name.end(), name.begin(), ::towlower);
                if (alias.find(lq) != std::wstring::npos || name.find(lq) != std::wstring::npos) {
                    matches = true;
                } else if (!cleanQ.empty() && (alias.find(cleanQ) != std::wstring::npos || name.find(cleanQ) != std::wstring::npos)) {
                    matches = true;
                }
            }

            if (matches) {
                SearchItemResult r;
                r.name = cmd.name;
                r.fullPath = cmd.target;
                r.ext = L"命令";
                r.sizeBytes = 0;
                r.modifiedTime = 0;
                r.isDirectory = false;
                r.icon = cmd.icon;
                m_filteredResults.push_back(std::move(r));
            }
        }
    }

    // 若输入单个 '/'，专用于唤起效率指令总览面板，不再混入磁盘文件
    if (lq == L"/") {
        if (m_selectedIndex >= static_cast<int>(m_filteredResults.size())) {
            m_selectedIndex = std::max(0, static_cast<int>(m_filteredResults.size()) - 1);
        }
        markNeedsPaint();
        return;
    }

    // 3. 拆分多关键词并过滤磁盘检索文件
    std::vector<std::wstring> queryTokens;
    if (!cleanQ.empty()) {
        size_t start = 0;
        while (start < cleanQ.size()) {
            while (start < cleanQ.size() && (cleanQ[start] == L' ' || cleanQ[start] == L'\t' || cleanQ[start] == L'　')) ++start;
            if (start >= cleanQ.size()) break;
            size_t end = start;
            while (end < cleanQ.size() && cleanQ[end] != L' ' && cleanQ[end] != L'\t' && cleanQ[end] != L'　') ++end;
            queryTokens.push_back(cleanQ.substr(start, end - start));
            start = end;
        }
    }

    for (const auto& item : m_allResults) {
        // 分类过滤
        if (m_category == SearchCategory::Folders && !item.isDirectory) continue;
        if (m_category == SearchCategory::Documents && (
            item.ext != L".txt" && item.ext != L".md" && item.ext != L".pdf" &&
            item.ext != L".doc" && item.ext != L".docx" && item.ext != L".xls" &&
            item.ext != L".xlsx" && item.ext != L".ppt" && item.ext != L".pptx" &&
            item.ext != L".rtf" && item.ext != L".csv")) continue;
        if (m_category == SearchCategory::Images && (
            item.ext != L".png" && item.ext != L".jpg" && item.ext != L".jpeg" &&
            item.ext != L".webp" && item.ext != L".gif" && item.ext != L".bmp" &&
            item.ext != L".ico" && item.ext != L".svg")) continue;
        if (m_category == SearchCategory::Code && (
            item.ext != L".cpp" && item.ext != L".h" && item.ext != L".c" &&
            item.ext != L".hpp" && item.ext != L".ts" && item.ext != L".js" &&
            item.ext != L".json" && item.ext != L".py" && item.ext != L".rs" &&
            item.ext != L".go" && item.ext != L".cs" && item.ext != L".java" &&
            item.ext != L".html" && item.ext != L".css" && item.ext != L".sql" &&
            item.ext != L".ps1" && item.ext != L".bat" && item.ext != L".cmd")) continue;
        if (m_category == SearchCategory::Executables && (
            item.ext != L".exe" && item.ext != L".msi" && item.ext != L".bat" &&
            item.ext != L".cmd")) continue;
        if (m_category == SearchCategory::Compressed && (
            item.ext != L".zip" && item.ext != L".7z" && item.ext != L".rar" &&
            item.ext != L".tar" && item.ext != L".gz")) continue;
        if (m_category == SearchCategory::Media && (
            item.ext != L".mp4" && item.ext != L".mkv" && item.ext != L".avi" &&
            item.ext != L".mov" && item.ext != L".mp3" && item.ext != L".wav" &&
            item.ext != L".flac")) continue;

        // 若结果来自后端检索，后端已完成匹配（支持拼音、路径、分词、模糊），无需二次硬过滤；
        // 若来自本地模拟数据或离线列表，则执行多关键词匹配
        if (!m_hasBackendResults && !queryTokens.empty()) {
            std::wstring lname = item.name;
            std::transform(lname.begin(), lname.end(), lname.begin(), ::towlower);
            std::wstring lpath = item.fullPath;
            std::transform(lpath.begin(), lpath.end(), lpath.begin(), ::towlower);

            bool matchAll = true;
            bool hasMultipleTokens = (queryTokens.size() > 1);
            for (const auto& token : queryTokens) {
                bool tokenMatched = false;
                if (lname.find(token) != std::wstring::npos) {
                    tokenMatched = true;
                } else if (hasMultipleTokens || token.find(L'\\') != std::wstring::npos || token.find(L'/') != std::wstring::npos) {
                    if (lpath.find(token) != std::wstring::npos) {
                        tokenMatched = true;
                    }
                }
                if (!tokenMatched) {
                    matchAll = false;
                    break;
                }
            }
            if (!matchAll) continue;
        }

        m_filteredResults.push_back(item);
    }

    if (m_selectedIndex >= static_cast<int>(m_filteredResults.size())) {
        m_selectedIndex = std::max(0, static_cast<int>(m_filteredResults.size()) - 1);
    }
    markNeedsPaint();
}

void NativeSearchView::openSelected() {
    std::wstring path;
    {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_filteredResults.size())) {
            path = m_filteredResults[m_selectedIndex].fullPath;
        }
    }
    if (!path.empty()) {
        if (m_host) m_host->hide();
        if (path.starts_with(L"tools3000://settings/")) {
            std::wstring pageW = path.substr(21);
            std::string pageId = tools3000::core::WinUtils::wstringToUtf8(pageW);
            tools3000::ui::native::NativeSettingsApp::instance().show(GetModuleHandleW(nullptr));
            tools3000::ui::native::NativeSettingsApp::instance().navigateTo(pageId);
        } else if (path == L"tools3000://action/capture") {
            tools3000::core::MessageBridge::instance().handleMessageAsync(
                "{\"id\":\"search_shot\",\"method\":\"capture.start\",\"params\":{}}", [](std::string){});
        } else if (path == L"tools3000://action/record") {
            tools3000::core::MessageBridge::instance().handleMessageAsync(
                "{\"id\":\"search_rec\",\"method\":\"capture.startRecording\",\"params\":{}}", [](std::string){});
        } else if (path == L"tools3000://action/boost") {
            tools3000::core::MessageBridge::instance().handleMessageAsync(
                "{\"id\":\"search_flush\",\"method\":\"remote.emergencyFlush\",\"params\":{}}", [](std::string){});
        } else {
            ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    }
}

void NativeSearchView::openSelectedFolder() {
    std::wstring path;
    {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_filteredResults.size())) {
            path = m_filteredResults[m_selectedIndex].fullPath;
        }
    }
    if (!path.empty()) {
        if (m_host) m_host->hide();
        if (path.starts_with(L"tools3000://")) {
            tools3000::ui::native::NativeSettingsApp::instance().show(GetModuleHandleW(nullptr));
            return;
        }
        std::wstring param = L"/select,\"" + path + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", param.c_str(), nullptr, SW_SHOWNORMAL);
    }
}

void NativeSearchView::copySelectedPath() {
    std::wstring path;
    {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_filteredResults.size())) {
            path = m_filteredResults[m_selectedIndex].fullPath;
        }
    }
    if (!path.empty()) {
        if (OpenClipboard(nullptr)) {
            EmptyClipboard();
            size_t bytes = (path.size() + 1) * sizeof(wchar_t);
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (hMem) {
                void* p = GlobalLock(hMem);
                if (p) {
                    memcpy(p, path.c_str(), bytes);
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_UNICODETEXT, hMem);
                }
            }
            CloseClipboard();
        }
    }
}

void NativeSearchView::onActivated() {
    NativeView::onActivated();
    m_selectedIndex = 0;
    m_scrollOffset = 0.0f;
    m_targetScrollOffset = 0.0f;
    m_cursorVisible = true;
    m_cursorBlinkTime = 0.0f;
    if (m_query.empty()) {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        m_hasBackendResults = false;
        buildMockIndex();
        filterResultsLocked();
    } else {
        setQuery(m_query);
    }
    markNeedsPaint();
}

Size NativeSearchView::measure(float availableWidth, float availableHeight, UIRenderContext& ctx) {
    (void)ctx;
    float w = availableWidth > 0 ? availableWidth : 860.0f;
    float h = availableHeight > 0 ? availableHeight : 560.0f;
    m_desiredSize = Size(w, h);
    return m_desiredSize;
}

void NativeSearchView::layout(const Rect& bounds, UIRenderContext& ctx) {
    UIElement::layout(bounds, ctx);
}

bool NativeSearchView::update(float dt) {
    bool keepAlive = false;

    // 1. 防抖计时器推进
    if (m_queryDebounceTimer > 0.0f) {
        m_queryDebounceTimer -= dt;
        keepAlive = true;
        if (m_queryDebounceTimer <= 0.0f) {
            m_queryDebounceTimer = 0.0f;
            dispatchSearchQuery();
        }
    }

    // 2. 服务启动重试计时器推进
    if (m_retryTimer > 0.0f) {
        m_retryTimer -= dt;
        keepAlive = true;
        if (m_retryTimer <= 0.0f) {
            m_retryTimer = 0.0f;
            if (m_isServiceStarting && !m_query.empty() && m_retryCount < 25) {
                m_retryCount++;
                dispatchSearchQuery();
            }
        }
    }

    // 3. 搜索框光标闪烁
    m_cursorBlinkTime += dt;
    if (m_cursorBlinkTime >= 0.5f) {
        m_cursorBlinkTime = 0.0f;
        m_cursorVisible = !m_cursorVisible;
        markNeedsPaint();
    }

    // 4. 虚拟列表惯性与平滑滚动
    if (std::abs(m_targetScrollOffset - m_scrollOffset) > 0.5f) {
        m_scrollOffset += (m_targetScrollOffset - m_scrollOffset) * std::clamp(dt * 18.0f, 0.05f, 1.0f);
        markNeedsPaint();
        return true;
    }

    return keepAlive;
}

bool NativeSearchView::onKeyDown(const UIKeyEvent& e) {
    if (e.virtualKey == VK_ESCAPE) {
        if (m_host) m_host->hide();
        return true;
    }
    if (e.virtualKey == VK_UP) {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        if (m_selectedIndex > 0) {
            m_selectedIndex--;
            float itemTop = m_selectedIndex * 40.0f;
            if (itemTop < m_targetScrollOffset) {
                m_targetScrollOffset = itemTop;
            }
            markNeedsPaint();
        }
        return true;
    }
    if (e.virtualKey == VK_DOWN) {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        if (m_selectedIndex < static_cast<int>(m_filteredResults.size()) - 1) {
            m_selectedIndex++;
            float itemBottom = (m_selectedIndex + 1) * 40.0f;
            float viewH = std::max(100.0f, m_bounds.height() - 134.0f);
            if (itemBottom > m_targetScrollOffset + viewH) {
                m_targetScrollOffset = itemBottom - viewH;
            }
            markNeedsPaint();
        }
        return true;
    }
    if (e.virtualKey == VK_TAB) {
        static const SearchCategory s_cats[] = {
            SearchCategory::All,
            SearchCategory::Documents,
            SearchCategory::Folders,
            SearchCategory::Images,
            SearchCategory::Code,
            SearchCategory::Executables
        };
        constexpr int numCats = static_cast<int>(sizeof(s_cats) / sizeof(s_cats[0]));
        int curIdx = 0;
        for (int i = 0; i < numCats; ++i) {
            if (m_category == s_cats[i]) { curIdx = i; break; }
        }
        int nextIdx = e.modifiers.shift ? (curIdx + numCats - 1) % numCats : (curIdx + 1) % numCats;
        setCategory(s_cats[nextIdx]);
        return true;
    }
    if (e.virtualKey == VK_RETURN) {
        if (e.modifiers.ctrl) {
            openSelectedFolder();
        } else {
            openSelected();
        }
        return true;
    }
    if (e.virtualKey == 'C' && e.modifiers.ctrl) {
        copySelectedPath();
        return true;
    }
    if (e.virtualKey == 'V' && e.modifiers.ctrl) {
        if (OpenClipboard(nullptr)) {
            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
            if (hData) {
                wchar_t* pText = static_cast<wchar_t*>(GlobalLock(hData));
                if (pText) {
                    m_query += pText;
                    GlobalUnlock(hData);
                    setQuery(m_query);
                }
            }
            CloseClipboard();
        }
        return true;
    }
    if (e.virtualKey == 'A' && e.modifiers.ctrl) {
        setQuery(L"");
        return true;
    }
    if (e.virtualKey == VK_BACK) {
        if (e.modifiers.ctrl) {
            setQuery(L"");
        } else if (!m_query.empty()) {
            m_query.pop_back();
            setQuery(m_query);
        }
        return true;
    }
    if (e.virtualKey == VK_DELETE) {
        if (!m_query.empty()) {
            setQuery(L"");
        }
        return true;
    }

    return false;
}

bool NativeSearchView::onChar(const UIKeyEvent& e) {
    if (e.charCode >= 32) {
        m_query.push_back(static_cast<wchar_t>(e.charCode));
        setQuery(m_query);
        return true;
    }
    return false;
}

bool NativeSearchView::onMouseWheel(const UIMouseEvent& e) {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    float viewH = std::max(100.0f, m_bounds.height() - 134.0f);
    float maxScroll = std::max(0.0f, m_filteredResults.size() * 40.0f - viewH);
    m_targetScrollOffset = std::clamp(m_targetScrollOffset - e.wheelDelta * 0.4f, 0.0f, maxScroll);
    markNeedsPaint();
    return true;
}

bool NativeSearchView::onMouseMove(const UIMouseEvent& e) {
    if (m_contextMenuVisible) {
        Rect menuRect(m_contextMenuPos.x, m_contextMenuPos.y, m_contextMenuPos.x + 190.0f, m_contextMenuPos.y + 132.0f);
        if (menuRect.right > m_bounds.right - 10.0f) {
            menuRect = menuRect.offset(m_bounds.right - 10.0f - menuRect.right, 0.0f);
        }
        if (menuRect.bottom > m_bounds.bottom - 10.0f) {
            menuRect = menuRect.offset(0.0f, m_bounds.bottom - 10.0f - menuRect.bottom);
        }
        int prevHover = m_contextMenuHover;
        if (menuRect.contains(e.position)) {
            m_contextMenuHover = static_cast<int>((e.position.y - (menuRect.top + 6.0f)) / 30.0f);
            if (m_contextMenuHover < 0 || m_contextMenuHover > 3) m_contextMenuHover = -1;
        } else {
            m_contextMenuHover = -1;
        }
        if (m_contextMenuHover != prevHover) {
            markNeedsPaint();
        }
        return true;
    }

    int prevHover = m_hoveredIndex;
    m_hoveredIndex = -1;

    float listTop = m_bounds.top + 92.0f;
    float listBottom = m_bounds.bottom - 38.0f;

    if (e.position.y >= listTop && e.position.y <= listBottom) {
        float relY = e.position.y - listTop + m_scrollOffset;
        int idx = static_cast<int>(relY / 40.0f);
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        if (idx >= 0 && idx < static_cast<int>(m_filteredResults.size())) {
            m_hoveredIndex = idx;
        }
    }

    if (m_hoveredIndex != prevHover) {
        markNeedsPaint();
    }
    return true;
}

bool NativeSearchView::onMouseDown(const UIMouseEvent& e) {
    // 检查右键上下文菜单响应
    if (m_contextMenuVisible) {
        Rect menuRect(m_contextMenuPos.x, m_contextMenuPos.y, m_contextMenuPos.x + 190.0f, m_contextMenuPos.y + 132.0f);
        if (menuRect.right > m_bounds.right - 10.0f) {
            menuRect = menuRect.offset(m_bounds.right - 10.0f - menuRect.right, 0.0f);
        }
        if (menuRect.bottom > m_bounds.bottom - 10.0f) {
            menuRect = menuRect.offset(0.0f, m_bounds.bottom - 10.0f - menuRect.bottom);
        }
        if (menuRect.contains(e.position)) {
            int itemIdx = static_cast<int>((e.position.y - (menuRect.top + 6.0f)) / 30.0f);
            if (itemIdx == 0) {
                openSelected();
            } else if (itemIdx == 1) {
                openSelectedFolder();
            } else if (itemIdx == 2) {
                copySelectedPath();
            } else if (itemIdx == 3) {
                std::wstring path;
                {
                    std::lock_guard<std::mutex> lock(m_resultsMutex);
                    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_filteredResults.size())) {
                        path = m_filteredResults[m_selectedIndex].fullPath;
                    }
                }
                if (!path.empty()) {
                    ShellExecuteW(nullptr, L"runas", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    if (m_host) m_host->close();
                }
            }
        }
        m_contextMenuVisible = false;
        m_contextMenuHover = -1;
        markNeedsPaint();
        return true;
    }

    // 检查右键呼出上下文菜单
    if (e.button == MouseButton::Right) {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        if (m_hoveredIndex >= 0 && m_hoveredIndex < static_cast<int>(m_filteredResults.size())) {
            m_selectedIndex = m_hoveredIndex;
            m_contextMenuVisible = true;
            m_contextMenuPos = e.position;
            m_contextMenuHover = -1;
            markNeedsPaint();
            return true;
        }
    }

    // 检查 Pin 固定置顶胶囊点击
    Rect pinBtn(m_bounds.right - 46.0f, m_bounds.top + 12.0f, m_bounds.right - 16.0f, m_bounds.top + 48.0f);
    if (pinBtn.contains(e.position)) {
        setPinned(!m_isPinned);
        return true;
    }

    // 检查分类 Tab 栏点击
    float tabY = m_bounds.top + 56.0f;
    if (e.position.y >= tabY && e.position.y <= tabY + 28.0f) {
        static const SearchCategory cats[] = {
            SearchCategory::All, SearchCategory::Documents, SearchCategory::Folders,
            SearchCategory::Images, SearchCategory::Code, SearchCategory::Executables
        };
        float tx = m_bounds.left + 16.0f;
        for (int i = 0; i < 6; ++i) {
            Rect tr(tx, tabY, tx + 64.0f, tabY + 26.0f);
            if (tr.contains(e.position)) {
                setCategory(cats[i]);
                return true;
            }
            tx += 70.0f;
        }
    }

    // 检查搜索框清空按钮 (X)
    Rect searchBar(m_bounds.left + 16.0f, m_bounds.top + 12.0f, m_bounds.right - 54.0f, m_bounds.top + 48.0f);
    Rect clearBtn(searchBar.right - 30.0f, searchBar.top + 8.0f, searchBar.right - 8.0f, searchBar.top + 30.0f);
    if (!m_query.empty() && clearBtn.contains(e.position)) {
        m_query.clear();
        filterResults();
        return true;
    }

    // 检查搜索框点击 (激活光标并阻止误识别为拖拽)
    if (searchBar.contains(e.position)) {
        m_cursorVisible = true;
        m_cursorBlinkTime = 0.0f;
        markNeedsPaint();
        return true;
    }

    // 点击列表行
    bool shouldOpen = false;
    {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        if (m_hoveredIndex >= 0 && m_hoveredIndex < static_cast<int>(m_filteredResults.size())) {
            m_selectedIndex = m_hoveredIndex;
            markNeedsPaint();
            if (e.clickCount >= 2) {
                shouldOpen = true;
            }
        }
    }
    if (shouldOpen) {
        openSelected();
        return true;
    }
    if (m_hoveredIndex >= 0) {
        return true;
    }

    // 点击空闲底板区域支持按住拖动窗口
    if (e.button == MouseButton::Left && m_host && m_host->hwnd()) {
        ReleaseCapture();
        SendMessageW(m_host->hwnd(), WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return true;
    }

    return false;
}

void NativeSearchView::render(UIRenderContext& ctx) {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    const auto& theme = UITheme::instance();
    const auto& p = theme.palette();
    bool isDark = theme.isEffectiveDark();

    // ── 0. 微晶拟态亚克力主体背景面板 (Glass Acrylic Base Panel) ──────────────
    // 采用双层抗锯齿微晶渐变底板 + 物理向下柔和阴影，彻底根除透明穿透与直角底衬
    GlassmorphismRenderer::drawGlassPanel(ctx, m_bounds, 12.0f, isDark, 3);

    // ── 1. 顶部搜索框 (Search Bar) ──────────────────────────────────────────
    Rect searchBar(m_bounds.left + 16.0f, m_bounds.top + 12.0f, m_bounds.right - 54.0f, m_bounds.top + 48.0f);
    
    // 物理下沉触感微阴影
    ctx.drawRoundedRect(
        Rect(searchBar.left, searchBar.top + 1.0f, searchBar.right, searchBar.bottom + 1.0f),
        8.0f, Color(0.0f, 0.0f, 0.0f, isDark ? 0.30f : 0.05f), 1.0f
    );

    Color barBg = isDark ? Color::fromHex(0x13121d, 0.95f) : Color::fromHex(0xf8fafc, 0.98f);
    Color barBorder = m_query.empty()
        ? (isDark ? Color(1.0f, 1.0f, 1.0f, 0.14f) : Color(0.06f, 0.09f, 0.16f, 0.12f))
        : p.primary.withAlpha(0.65f);

    ctx.fillRoundedRect(searchBar, 8.0f, barBg);

    // 顶部 1px 极细微晶反光线条
    Color barSheen = isDark ? Color(1.0f, 1.0f, 1.0f, 0.12f) : Color(1.0f, 1.0f, 1.0f, 0.92f);
    ctx.drawLine(
        Point(searchBar.left + 8.0f, searchBar.top + 0.8f),
        Point(searchBar.right - 8.0f, searchBar.top + 0.8f),
        barSheen, 1.0f
    );

    ctx.drawRoundedRect(searchBar, 8.0f, barBorder, 1.5f);

    // 搜索矢量图标 (输入内容时高亮强调色)
    Color searchIconColor = m_query.empty() ? p.textSecondary : p.primary;
    VectorIconRenderer::drawIcon(
        ctx, IconType::Search,
        Rect(searchBar.left + 12.0f, searchBar.top + 9.0f, searchBar.left + 30.0f, searchBar.top + 27.0f),
        searchIconColor, 1.9f
    );

    // 搜索文本 / 占位提示
    if (m_query.empty()) {
        ctx.drawText(L"键入文件名或路径进行全系统毫秒级闪电搜索...",
                     Point(searchBar.left + 42.0f, searchBar.top + 8.0f),
                     FontToken::Base, FontWeight::Regular, p.textMuted);

        // 搜索框光标闪烁
        if (m_cursorVisible) {
            ctx.drawLine(Point(searchBar.left + 38.0f, searchBar.top + 8.0f),
                         Point(searchBar.left + 38.0f, searchBar.bottom - 8.0f),
                         p.primary, 2.0f);
        }
    } else {
        ctx.drawText(m_query, Point(searchBar.left + 38.0f, searchBar.top + 7.0f),
                     FontToken::Lg, FontWeight::SemiBold, p.textPrimary);

        // 闪烁光标 (基于实际字体度量精准定位，杜绝中英混排与等宽错位)
        if (m_cursorVisible) {
            Size textSz = ctx.measureText(m_query, FontToken::Lg, FontWeight::SemiBold);
            float cx = searchBar.left + 38.0f + textSz.width + 2.0f;
            ctx.drawLine(Point(cx, searchBar.top + 8.0f), Point(cx, searchBar.bottom - 8.0f), p.primary, 2.0f);
        }

        // 清空 X 按钮
        Rect clearBtn(searchBar.right - 30.0f, searchBar.top + 10.0f, searchBar.right - 12.0f, searchBar.top + 28.0f);
        VectorIconRenderer::drawIcon(ctx, IconType::X, clearBtn, p.textSecondary, 1.6f);
    }

    // 窗口 Pin 固定置顶按钮 (右上角 [right - 46, top + 12, right - 16, top + 48])
    Rect pinBtn(m_bounds.right - 46.0f, m_bounds.top + 12.0f, m_bounds.right - 16.0f, m_bounds.top + 48.0f);
    if (m_isPinned) {
        ctx.fillRoundedRect(pinBtn, 7.0f, p.primary.withAlpha(isDark ? 0.35f : 0.20f));
        ctx.drawRoundedRect(pinBtn, 7.0f, p.primary, 1.5f);
        VectorIconRenderer::drawIcon(ctx, IconType::Pin, Rect(pinBtn.left + 6.0f, pinBtn.top + 6.0f, pinBtn.right - 6.0f, pinBtn.bottom - 6.0f), p.primary, 1.8f);
    } else {
        ctx.fillRoundedRect(pinBtn, 7.0f, isDark ? Color(1.0f, 1.0f, 1.0f, 0.05f) : Color(0.0f, 0.0f, 0.0f, 0.04f));
        ctx.drawRoundedRect(pinBtn, 7.0f, isDark ? Color(1.0f, 1.0f, 1.0f, 0.12f) : Color(0.06f, 0.09f, 0.16f, 0.10f), 1.0f);
        VectorIconRenderer::drawIcon(ctx, IconType::Pin, Rect(pinBtn.left + 6.0f, pinBtn.top + 6.0f, pinBtn.right - 6.0f, pinBtn.bottom - 6.0f), p.textMuted, 1.5f);
    }

    // ── 2. 分类过滤标签胶囊 (Category Tabs) ──────────────────────────────────
    float tabY = m_bounds.top + 56.0f;
    static const struct {
        SearchCategory cat;
        const wchar_t* label;
    } catTabs[] = {
        { SearchCategory::All, L"全部" },
        { SearchCategory::Documents, L"文档" },
        { SearchCategory::Folders, L"文件夹" },
        { SearchCategory::Images, L"图片" },
        { SearchCategory::Code, L"代码" },
        { SearchCategory::Executables, L"程序" }
    };

    float tx = m_bounds.left + 16.0f;
    for (const auto& tab : catTabs) {
        bool isAct = (m_category == tab.cat);
        Rect tr(tx, tabY, tx + 64.0f, tabY + 26.0f);
        const float pillRadius = 13.0f;

        if (isAct) {
            ctx.fillRoundedRect(tr, pillRadius, p.primary.withAlpha(isDark ? 0.28f : 0.16f));
            ctx.drawRoundedRect(tr, pillRadius, p.primary.withAlpha(isDark ? 0.70f : 0.50f), 1.2f);
            // 顶部高光
            ctx.drawLine(Point(tr.left + 10.0f, tr.top + 0.8f), Point(tr.right - 10.0f, tr.top + 0.8f), Color(1.0f, 1.0f, 1.0f, 0.35f), 1.0f);
            ctx.drawText(tab.label, tr, p.primary, FontToken::Xs, FontWeight::SemiBold, FontFamilyType::Sans, TextAlignmentH::Center, TextAlignmentV::Center);
        } else {
            ctx.fillRoundedRect(tr, pillRadius, isDark ? Color(1.0f, 1.0f, 1.0f, 0.04f) : Color(0.0f, 0.0f, 0.0f, 0.03f));
            ctx.drawRoundedRect(tr, pillRadius, isDark ? Color(1.0f, 1.0f, 1.0f, 0.09f) : Color(0.06f, 0.09f, 0.16f, 0.08f), 1.0f);
            ctx.drawText(tab.label, tr, p.textMuted, FontToken::Xs, FontWeight::Medium, FontFamilyType::Sans, TextAlignmentH::Center, TextAlignmentV::Center);
        }
        tx += 70.0f;
    }

    // ── 3. 虚拟列表结果展示 (Virtual Results List) ──────────────────────────
    float listTop = m_bounds.top + 92.0f;
    float listBottom = m_bounds.bottom - 40.0f;
    Rect listClip(m_bounds.left + 14.0f, listTop, m_bounds.right - 14.0f, listBottom);

    const float rowH = 40.0f;
    int startRow = std::max(0, static_cast<int>(m_scrollOffset / rowH));
    int endRow = std::min(static_cast<int>(m_filteredResults.size()),
                          static_cast<int>((m_scrollOffset + listClip.height()) / rowH) + 1);

    ctx.pushClip(listClip);
    if (m_filteredResults.empty()) {
        float centerX = (listClip.left + listClip.right) * 0.5f;
        float centerY = (listClip.top + listClip.bottom) * 0.5f - 18.0f;

        // 微晶拟态居中空状态徽章 (64x64 圆角容器)
        Rect badgeRect(centerX - 32.0f, centerY - 38.0f, centerX + 32.0f, centerY + 26.0f);
        Color badgeBg = isDark ? Color::fromHex(0x1a1d29, 0.70f) : Color::fromHex(0xedf2f7, 0.85f);
        Color badgeBorder = isDark ? Color(1.0f, 1.0f, 1.0f, 0.12f) : Color(0.06f, 0.09f, 0.16f, 0.10f);
        ctx.fillRoundedRect(badgeRect, 18.0f, badgeBg);
        ctx.drawRoundedRect(badgeRect, 18.0f, badgeBorder, 1.2f);

        // 徽章顶部微晶高光
        ctx.drawLine(
            Point(badgeRect.left + 12.0f, badgeRect.top + 0.8f),
            Point(badgeRect.right - 12.0f, badgeRect.top + 0.8f),
            Color(1.0f, 1.0f, 1.0f, isDark ? 0.25f : 0.60f), 1.0f
        );

        // 矢量大图标
        IconType iconType = m_isServiceStarting ? IconType::RefreshCw : IconType::Search;
        Rect iconRect(badgeRect.left + 16.0f, badgeRect.top + 16.0f, badgeRect.right - 16.0f, badgeRect.bottom - 16.0f);
        VectorIconRenderer::drawIcon(ctx, iconType, iconRect, p.primary, 2.2f);

        if (m_isServiceStarting) {
            ctx.drawText(
                L"正在启动极速文件检索引擎...",
                Rect(listClip.left + 20.0f, centerY + 36.0f, listClip.right - 20.0f, centerY + 58.0f),
                p.textPrimary, FontToken::Base, FontWeight::SemiBold, FontFamilyType::Sans,
                TextAlignmentH::Center, TextAlignmentV::Center
            );
            ctx.drawText(
                L"首次唤起正在按需装载磁盘索引并连接服务管道，稍候片刻即将呈现检索结果",
                Rect(listClip.left + 20.0f, centerY + 58.0f, listClip.right - 20.0f, centerY + 78.0f),
                p.textMuted, FontToken::Xs, FontWeight::Regular, FontFamilyType::Sans,
                TextAlignmentH::Center, TextAlignmentV::Center
            );
        } else if (!m_query.empty()) {
            std::wstring mainTip = L"未找到与 “" + m_query + L"” 匹配的项目";
            if (mainTip.size() > 40) {
                mainTip = mainTip.substr(0, 37) + L"...” 匹配的项目";
            }
            ctx.drawText(
                mainTip,
                Rect(listClip.left + 20.0f, centerY + 36.0f, listClip.right - 20.0f, centerY + 58.0f),
                p.textPrimary, FontToken::Base, FontWeight::SemiBold, FontFamilyType::Sans,
                TextAlignmentH::Center, TextAlignmentV::Center
            );
            ctx.drawText(
                L"建议：检查关键词拼写、尝试缩短搜索词，或切换至「全部」分类再次检索",
                Rect(listClip.left + 20.0f, centerY + 58.0f, listClip.right - 20.0f, centerY + 78.0f),
                p.textMuted, FontToken::Xs, FontWeight::Regular, FontFamilyType::Sans,
                TextAlignmentH::Center, TextAlignmentV::Center
            );
        } else {
            ctx.drawText(
                L"全系统闪电极速搜索就绪",
                Rect(listClip.left + 20.0f, centerY + 36.0f, listClip.right - 20.0f, centerY + 58.0f),
                p.textPrimary, FontToken::Base, FontWeight::SemiBold, FontFamilyType::Sans,
                TextAlignmentH::Center, TextAlignmentV::Center
            );
            ctx.drawText(
                L"键入文件名、路径首字母或拼音缩写，毫秒级快速定位目标",
                Rect(listClip.left + 20.0f, centerY + 58.0f, listClip.right - 20.0f, centerY + 78.0f),
                p.textMuted, FontToken::Xs, FontWeight::Regular, FontFamilyType::Sans,
                TextAlignmentH::Center, TextAlignmentV::Center
            );
        }
    } else {
        for (int i = startRow; i < endRow; ++i) {
            const auto& item = m_filteredResults[i];
            float ry = listTop + i * rowH - m_scrollOffset;
            Rect rowRect(listClip.left, ry, listClip.right, ry + rowH);

            bool isSelected = (m_selectedIndex == i);
            bool isHovered = (m_hoveredIndex == i);

            if (isSelected) {
                Color selBg = isDark ? p.primary.withAlpha(0.24f) : p.primary.withAlpha(0.12f);
                ctx.fillRoundedRect(rowRect, 7.0f, selBg);
                ctx.drawRoundedRect(rowRect, 7.0f, p.primary.withAlpha(isDark ? 0.45f : 0.30f), 1.0f);

                // 左侧 3.5px 强调色指示条
                Rect barRect(rowRect.left + 2.0f, rowRect.top + 7.0f, rowRect.left + 5.5f, rowRect.bottom - 7.0f);
                ctx.fillRoundedRect(barRect, 1.75f, p.primary);
            } else if (isHovered) {
                Color hovBg = isDark ? Color(1.0f, 1.0f, 1.0f, 0.06f) : Color(0.0f, 0.0f, 0.0f, 0.04f);
                ctx.fillRoundedRect(rowRect, 7.0f, hovBg);
            }

            // 微晶图标容器小方块 (28x28 微晶底板)
            Rect iconBox(rowRect.left + 10.0f, rowRect.top + 6.0f, rowRect.left + 38.0f, rowRect.top + 34.0f);
            Color boxBg = isDark
                ? Color(1.0f, 1.0f, 1.0f, isSelected ? 0.12f : 0.05f)
                : Color(0.0f, 0.0f, 0.0f, isSelected ? 0.08f : 0.04f);
            ctx.fillRoundedRect(iconBox, 6.0f, boxBg);
            ctx.drawRoundedRect(iconBox, 6.0f, isSelected ? p.primary.withAlpha(0.35f) : Color(1.0f, 1.0f, 1.0f, 0.08f), 1.0f);

            // 居中矢量文件图标
            Rect iconInner(iconBox.left + 6.0f, iconBox.top + 6.0f, iconBox.right - 6.0f, iconBox.bottom - 6.0f);
            VectorIconRenderer::drawIcon(
                ctx, item.icon, iconInner,
                isSelected ? p.primary : p.textSecondary, 1.6f
            );

            // 双行层级排版：文件名
            Color nameColor = isSelected
                ? (isDark ? Color(1.0f, 1.0f, 1.0f, 1.0f) : p.primary)
                : p.textPrimary;
            ctx.drawText(item.name, Point(rowRect.left + 46.0f, rowRect.top + 5.0f),
                         FontToken::Sm, isSelected ? FontWeight::Bold : FontWeight::SemiBold, nameColor);

            // 路径 (父目录)
            std::wstring parentDir = item.fullPath;
            if (parentDir.starts_with(L"tools3000://")) {
                if (parentDir.find(L"remote_boost") != std::wstring::npos) {
                    parentDir = L"主控端热键直通、修饰键卡死急救与输入法智能脱敏 · 回车直达配置中心";
                } else if (parentDir.find(L"capture") != std::wstring::npos) {
                    parentDir = L"智能选区吸附、图像美化外壳与贴图 · 回车立即唤起截图";
                } else if (parentDir.find(L"record") != std::wstring::npos) {
                    parentDir = L"高清硬件加速选区录屏与音视频捕获 · 回车立即开始录制";
                } else if (parentDir.find(L"stats") != std::wstring::npos) {
                    parentDir = L"全景物理键盘热力分布与击键频次统计 · 回车打开查看";
                } else if (parentDir.find(L"ocr") != std::wstring::npos) {
                    parentDir = L"基于 Windows 原生 OCR 引擎纯本地安全识别 · 回车打开";
                } else {
                    parentDir = L"Tools3000 原生效率扩展指令 · 按 Enter 执行";
                }
            } else {
                size_t lastSlash = parentDir.find_last_of(L"\\/");
                if (lastSlash != std::wstring::npos) parentDir = parentDir.substr(0, lastSlash);
            }
            ctx.drawText(parentDir, Point(rowRect.left + 46.0f, rowRect.top + 21.0f),
                         FontToken::Xs, FontWeight::Regular, p.textMuted);

            // 右侧微晶角标 (文件大小或效率指令)
            bool isCmd = item.fullPath.starts_with(L"tools3000://");
            std::wstring szStr = isCmd ? L"效率指令" : formatFileSize(item.sizeBytes);
            Rect sizeBadgeRect(rowRect.right - 92.0f, rowRect.top + 10.0f, rowRect.right - 10.0f, rowRect.top + 30.0f);
            Color badgeBg = isDark ? Color::fromHex(0x0f0f19, 0.65f) : Color::fromHex(0xf1f5f9, 0.85f);
            Color badgeBorder = isDark ? Color(1.0f, 1.0f, 1.0f, 0.10f) : Color(0.06f, 0.09f, 0.16f, 0.08f);
            ctx.fillRoundedRect(sizeBadgeRect, 4.0f, badgeBg);
            ctx.drawRoundedRect(sizeBadgeRect, 4.0f, badgeBorder, 1.0f);
            ctx.drawText(szStr, sizeBadgeRect, isSelected ? p.primary : p.textSecondary,
                         FontToken::Xs, FontWeight::Medium, isCmd ? FontFamilyType::Sans : FontFamilyType::Mono,
                         TextAlignmentH::Center, TextAlignmentV::Center);
        }
    }
    ctx.popClip();

    // 绘制虚拟列表超细平滑微晶滚动条
    float totalListHeight = m_filteredResults.size() * rowH;
    if (totalListHeight > listClip.height()) {
        float trackH = listClip.height() - 8.0f;
        float thumbH = std::max(28.0f, (listClip.height() / totalListHeight) * trackH);
        float maxScroll = totalListHeight - listClip.height();
        float scrollRatio = std::clamp(m_scrollOffset / maxScroll, 0.0f, 1.0f);
        float thumbY = listClip.top + 4.0f + scrollRatio * (trackH - thumbH);
        Rect thumbRect(m_bounds.right - 18.0f, thumbY, m_bounds.right - 14.0f, thumbY + thumbH);
        Color thumbColor = isDark ? Color(1.0f, 1.0f, 1.0f, 0.25f) : Color(0.0f, 0.0f, 0.0f, 0.20f);
        ctx.fillRoundedRect(thumbRect, 2.0f, thumbColor);
    }

    // ── 4. 底部状态栏 (Search Footer) ────────────────────────────────────────
    Rect footer(m_bounds.left + 16.0f, m_bounds.bottom - 34.0f, m_bounds.right - 16.0f, m_bounds.bottom);
    // 顶部 1px 细分割线
    Color footerLine = isDark ? Color(1.0f, 1.0f, 1.0f, 0.07f) : Color(0.06f, 0.09f, 0.16f, 0.07f);
    ctx.drawLine(Point(footer.left, footer.top), Point(footer.right, footer.top), footerLine, 1.0f);

    // 状态指示微晶圆点
    Color dotColor = m_isServiceStarting ? p.warning : p.success;
    ctx.fillCircle(Point(footer.left + 6.0f, footer.top + 17.0f), 2.8f, dotColor.withAlpha(0.35f));
    ctx.fillCircle(Point(footer.left + 6.0f, footer.top + 17.0f), 1.8f, dotColor);

    wchar_t statBuf[128];
    if (m_isServiceStarting) {
        swprintf_s(statBuf, L"检索引擎启动连接中 (重试 %d/25)...", m_retryCount);
    } else {
        swprintf_s(statBuf, L"找到 %zu 个项目 · 检索耗时 %.1f ms", m_filteredResults.size(), m_elapsedMs);
    }
    ctx.drawText(statBuf, Point(footer.left + 14.0f, footer.top + 8.0f), FontToken::Xs, FontWeight::Medium, p.textMuted);

    ctx.drawText(L"Enter 打开 · Ctrl+Enter 定位目录 · Ctrl+C 复制路径 · Tab 切换分类 · Esc 隐藏",
                 Point(footer.right - 448.0f, footer.top + 8.0f),
                 FontToken::Xs, FontWeight::Regular, p.textSecondary);

    // ── 5. 右键上下文菜单浮层 ────────────────────────────────────────────────
    if (m_contextMenuVisible) {
        Rect menuRect(m_contextMenuPos.x, m_contextMenuPos.y, m_contextMenuPos.x + 190.0f, m_contextMenuPos.y + 132.0f);
        if (menuRect.right > m_bounds.right - 10.0f) {
            menuRect = menuRect.offset(m_bounds.right - 10.0f - menuRect.right, 0.0f);
        }
        if (menuRect.bottom > m_bounds.bottom - 10.0f) {
            menuRect = menuRect.offset(0.0f, m_bounds.bottom - 10.0f - menuRect.bottom);
        }

        // 下沉投影与微晶底板
        GlassmorphismRenderer::drawGlassPanel(ctx, menuRect, 8.0f, isDark, 2);

        static const struct {
            IconType icon;
            const wchar_t* label;
        } menuItems[] = {
            { IconType::ExternalLink, L"打开文件" },
            { IconType::FolderOpen, L"打开所在文件夹" },
            { IconType::Copy, L"复制绝对路径" },
            { IconType::Shield, L"以管理员身份运行" }
        };

        float iy = menuRect.top + 6.0f;
        for (int i = 0; i < 4; ++i) {
            Rect itemR(menuRect.left + 6.0f, iy, menuRect.right - 6.0f, iy + 28.0f);
            if (m_contextMenuHover == i) {
                Color hov = isDark ? Color(1.0f, 1.0f, 1.0f, 0.08f) : Color(0.0f, 0.0f, 0.0f, 0.06f);
                ctx.fillRoundedRect(itemR, 5.0f, hov);
            }
            VectorIconRenderer::drawIcon(ctx, menuItems[i].icon, Rect(itemR.left + 6.0f, itemR.top + 6.0f, itemR.left + 22.0f, itemR.top + 22.0f), (m_contextMenuHover == i) ? p.primary : p.textSecondary, 1.5f);
            ctx.drawText(menuItems[i].label, Point(itemR.left + 28.0f, itemR.top + 5.0f), FontToken::Xs, FontWeight::Medium, (m_contextMenuHover == i) ? p.primary : p.textPrimary);
            iy += 30.0f;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// NativeSearchApp 单例宿主实现
// ─────────────────────────────────────────────────────────────────────────────

NativeSearchApp& NativeSearchApp::instance() {
    static NativeSearchApp s_app;
    return s_app;
}

NativeSearchApp::NativeSearchApp() = default;
NativeSearchApp::~NativeSearchApp() {
    destroy();
}

void NativeSearchApp::ensureInitialized(HINSTANCE hInstance) {
    if (m_host) return;

    m_host = std::make_unique<NativeFramelessWindow>();
    NativeWindowConfig config;
    config.className = L"Tools3000_NativeSearchWindow";
    config.title = L"Tools3000 全局搜索中心";
    config.width = m_width;
    config.height = m_height;
    config.minWidth = 600;
    config.minHeight = 400;
    config.centerOnScreen = true;
    config.resizable = true;
    config.seamlessTitlebar = false;
    config.isPopup = true;
    config.isToolWindow = true;
    config.alwaysOnTop = true;

    if (m_host->create(hInstance, config)) {
        m_host->setFramelessMode(FramelessMode::SpotlightCenter);

        if (m_host->hwnd()) {
            BOOL useDarkMode = UITheme::instance().isEffectiveDark() ? TRUE : FALSE;
            DwmSetWindowAttribute(m_host->hwnd(), 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &useDarkMode, sizeof(useDarkMode));
        }

        m_view = std::make_shared<NativeSearchView>();
        m_view->onAttachedToHost(m_host.get());
        if (m_isPinned) {
            m_view->setPinned(true);
        }
        m_host->setRootElement(m_view);

        m_host->setOnFocusLost([this]() {
            if (!isPinned()) {
                hide();
            }
        });
    }
}

void NativeSearchApp::preload(HINSTANCE hInstance) {
    ensureInitialized(hInstance);
}

void NativeSearchApp::show(HINSTANCE hInstance) {
    ensureInitialized(hInstance);
    if (!m_host) return;

    // 1. 同步显式标记搜索窗已呼出，严格遵循 DEMAND_START 生命周期契约，保障后续查询必达
    tools3000::core::MessageBridge::instance().handleMessage(
        R"({"id":0,"method":"search.windowShown","params":{}})"
    );
    // 2. 异步触发极速搜索服务预热/启动
    tools3000::core::MessageBridge::instance().handleMessageAsync(
        R"({"id":0,"method":"search.warmup","params":{}})",
        [](std::string) {}
    );

    m_host->positionCentered(m_width, m_height);
    m_host->show(SW_SHOW);
    SetForegroundWindow(m_host->hwnd());
    SetFocus(m_host->hwnd());

    if (m_view) {
        m_host->setFocusedElement(m_view);
        m_view->onActivated();
    }
    m_host->requestPaint();
}

void NativeSearchApp::hide() {
    if (m_host && m_host->isVisible()) {
        m_host->hide();
        tools3000::core::MessageBridge::instance().handleMessageAsync(
            R"({"id":0,"method":"search.windowHidden","params":{}})", [](std::string) {});
    }
}

void NativeSearchApp::destroy() {
    if (m_host) {
        m_host->destroy();
        m_host.reset();
    }
}

bool NativeSearchApp::isVisible() const {
    return m_host && m_host->isVisible();
}

void NativeSearchApp::setPinned(bool pinned) {
    m_isPinned = pinned;
    if (m_view) {
        m_view->setPinned(pinned);
    }
}

bool NativeSearchApp::isPinned() const {
    return m_view ? m_view->isPinned() : m_isPinned;
}

void NativeSearchApp::focusSearchIfVisible() {
    if (isVisible() && m_host) {
        SetForegroundWindow(m_host->hwnd());
        SetFocus(m_host->hwnd());
        if (m_view) {
            m_host->setFocusedElement(m_view);
            m_view->onActivated();
        }
        m_host->requestPaint();
    }
}

void NativeSearchApp::setWindowSize(int w, int h, bool forceCenter) {
    m_width = w;
    m_height = h;
    if (m_host) {
        if (forceCenter) {
            m_host->positionCentered(w, h);
        } else {
            m_host->setLogicalSize(w, h);
        }
    }
}

std::pair<int, int> NativeSearchApp::getWindowSize() const {
    return { m_width, m_height };
}

} // namespace tools3000::ui::native
