#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// WinUtils — Windows API 常用操作封装
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_CORE_UTILS_WINUTILS_H
#define EASYTOOLS_CORE_UTILS_WINUTILS_H

#include "core/utils/Export.h"

#include <windows.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <string>
#include <filesystem>
#include <optional>
#include <tlhelp32.h>
#include <algorithm>
#include <cctype>

namespace easy::core {

class EASYCORE_API WinUtils {
public:
    /// 获取可执行文件所在目录
    static std::filesystem::path getExeDirectory() {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        return std::filesystem::path(path).parent_path();
    }

    /// 获取 AppData/Local/EasyTools 目录（自动创建）
    static std::filesystem::path getAppDataDirectory() {
        wchar_t path[MAX_PATH] = {};
        // 使用 FOLDERID_LocalAppData
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path))) {
            auto dir = std::filesystem::path(path) / L"EasyTools";
            std::filesystem::create_directories(dir);
            return dir;
        }
        // fallback: 使用 exe 目录
        return getExeDirectory() / L"data";
    }

    /// 获取日志目录
    static std::filesystem::path getLogDirectory() {
        auto dir = getAppDataDirectory() / L"logs";
        std::filesystem::create_directories(dir);
        return dir;
    }

    /// 获取配置目录
    static std::filesystem::path getConfigDirectory() {
        auto dir = getAppDataDirectory() / L"config";
        std::filesystem::create_directories(dir);
        return dir;
    }

    /// 由 PID 取得进程可执行文件名 (仅文件名, 如 "chrome.exe")。
    /// 用 QueryFullProcessImageNameW 直接查询, 避免 Toolhelp 快照枚举全部进程
    /// (后者在低级鼠标钩子热路径上代价过高)。
    static std::wstring processNameFromPid(DWORD pid) {
        if (pid == 0) return {};
        HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!proc) return {};

        wchar_t path[MAX_PATH] = {};
        DWORD size = MAX_PATH;
        std::wstring result;
        if (QueryFullProcessImageNameW(proc, 0, path, &size)) {
            std::wstring full(path, size);
            size_t slash = full.find_last_of(L"\\/");
            result = (slash == std::wstring::npos) ? full : full.substr(slash + 1);
        }
        CloseHandle(proc);
        return result;
    }

    /// 获取前台窗口的进程名
    static std::optional<std::wstring> getForegroundProcessName() {
        HWND hwnd = GetForegroundWindow();
        if (!hwnd) return std::nullopt;

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        std::wstring name = processNameFromPid(pid);
        if (name.empty()) return std::nullopt;
        return name;
    }

    /// 获取窗口类名
    static std::wstring getWindowClassName(HWND hwnd) {
        wchar_t className[256] = {};
        GetClassNameW(hwnd, className, 256);
        return className;
    }

    /// 获取窗口标题
    static std::wstring getWindowTitle(HWND hwnd) {
        int len = GetWindowTextLengthW(hwnd);
        if (len == 0) return L"";
        std::wstring title(len + 1, L'\0');
        GetWindowTextW(hwnd, title.data(), len + 1);
        title.resize(len);
        return title;
    }

    /// wstring → UTF-8 string
    static std::string wstringToUtf8(const std::wstring& wstr) {
        if (wstr.empty()) return {};
        int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
        std::string result(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    /// UTF-8 string 到 wstring
    static std::wstring utf8ToWstring(const std::string& str) {
        if (str.empty()) return {};
        int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
        std::wstring result(size, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), result.data(), size);
        return result;
    }

    /// 获取窗口的进程名 (UTF-8)
    static std::string getProcessNameFromWindow(HWND hwnd) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        return wstringToUtf8(processNameFromPid(pid));
    }

    /// Base64 编码
    static std::string base64Encode(const std::vector<uint8_t>& data) {
        if (data.empty()) return "";
        DWORD len = 0;
        CryptBinaryToStringA(data.data(), static_cast<DWORD>(data.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &len);
        if (len == 0) return "";
        std::string result(len, '\0');
        CryptBinaryToStringA(data.data(), static_cast<DWORD>(data.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, result.data(), &len);
        // 去除尾部的 null 字符
        if (!result.empty() && result.back() == '\0') {
            result.pop_back();
        }
        return result;
    }

    /// 字符串转小写
    static std::string toLower(std::string str) {
        std::transform(str.begin(), str.end(), str.begin(),
            [](unsigned char c){ return std::tolower(c); });
        return str;
    }

    /// 复制文本到剪贴板
    static bool copyToClipboard(const std::string& text) {
        if (!OpenClipboard(nullptr)) return false;
        EmptyClipboard();
        std::wstring wtext = utf8ToWstring(text);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (wtext.length() + 1) * sizeof(wchar_t));
        if (!hMem) {
            CloseClipboard();
            return false;
        }
        memcpy(GlobalLock(hMem), wtext.c_str(), (wtext.length() + 1) * sizeof(wchar_t));
        GlobalUnlock(hMem);
        SetClipboardData(CF_UNICODETEXT, hMem);
        CloseClipboard();
        return true;
    }

    /// 启用全局高分屏 (DPI) 感知
    /// 解决高分屏下截屏、鼠标手势坐标以及窗口渲染产生的偏移问题
    static void enableHighDpiSupport() {
        // 启用 Per-Monitor V2 DPI 感知，确保在高分屏下物理像素和逻辑像素一比一映射
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    /// 获取全部多显示器合并后的真实物理边界
    static RECT getVirtualScreenPhysicalBounds() {
        RECT virtualBounds = { 0, 0, 0, 0 };
        EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hMonitor, HDC, LPRECT, LPARAM dwData) -> BOOL {
            MONITORINFO info;
            info.cbSize = sizeof(MONITORINFO);
            if (GetMonitorInfoW(hMonitor, &info)) {
                RECT* bounds = reinterpret_cast<RECT*>(dwData);
                bounds->left = std::min(bounds->left, info.rcMonitor.left);
                bounds->top = std::min(bounds->top, info.rcMonitor.top);
                bounds->right = std::max(bounds->right, info.rcMonitor.right);
                bounds->bottom = std::max(bounds->bottom, info.rcMonitor.bottom);
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&virtualBounds));
        
        // 如果没有获取到(比如失败了)，回退到 GetSystemMetrics
        if (virtualBounds.left == 0 && virtualBounds.right == 0 && 
            virtualBounds.top == 0 && virtualBounds.bottom == 0) {
            virtualBounds.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
            virtualBounds.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
            virtualBounds.right = virtualBounds.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
            virtualBounds.bottom = virtualBounds.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        }
        return virtualBounds;
    }

    /// 获取指定窗口的 DPI 缩放比例 (例如 125% DPI 时返回 1.25)
    static float getDpiScale(HWND hwnd = nullptr) {
        UINT dpi = hwnd ? GetDpiForWindow(hwnd) : GetDpiForSystem();
        return dpi / 96.0f;
    }

    /// 判断系统界面语言是否为中文
    static bool isSystemLanguageChinese() {
        LANGID langId = GetUserDefaultUILanguage();
        return PRIMARYLANGID(langId) == LANG_CHINESE;
    }

    /// 判断指定窗口是否处于全屏独占模式（如 3D 游戏、全屏播放等）
    /// 自动排除桌面、任务栏等系统特殊窗口
    static bool isWindowFullscreen(HWND hwnd) {
        if (!hwnd || !IsWindow(hwnd)) return false;

        // 排除桌面和 Shell 窗口
        if (hwnd == GetDesktopWindow() || hwnd == GetShellWindow()) return false;
        std::wstring className = getWindowClassName(hwnd);
        if (className == L"Progman" || className == L"WorkerW" || className == L"Shell_TrayWnd") {
            return false;
        }

        // 获取窗口所在的显示器
        HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);
        if (!hMon) return false;

        MONITORINFO mi{};
        mi.cbSize = sizeof(MONITORINFO);
        if (!GetMonitorInfoW(hMon, &mi)) return false;

        RECT rcWindow{};
        if (!GetWindowRect(hwnd, &rcWindow)) return false;

        // 判定窗口是否覆盖整个物理显示器区域
        return (rcWindow.left <= mi.rcMonitor.left &&
                rcWindow.top <= mi.rcMonitor.top &&
                rcWindow.right >= mi.rcMonitor.right &&
                rcWindow.bottom >= mi.rcMonitor.bottom);
    }
};

}  // namespace easy::core

#endif  // EASYTOOLS_CORE_UTILS_WINUTILS_H
