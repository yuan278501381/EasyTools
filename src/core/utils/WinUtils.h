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
#include <shldisp.h>
#include <exdisp.h>
#include <wrl/client.h>
#include <string>
#include <filesystem>
#include <optional>
#include <tlhelp32.h>
#include <algorithm>
#include <cctype>
#include <vector>

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
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path))) {
            auto dir = std::filesystem::path(path) / L"EasyTools";
            std::filesystem::create_directories(dir);
            return dir;
        }
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

    /// 主动释放进程未使用的物理内存并修剪工作集 (Working Set)
    static void trimWorkingSet() {
        SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
    }

    /// 由 PID 取得进程可执行文件名 (仅文件名, 如 "chrome.exe")。
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

    /// 复制宽文本到剪贴板（支持剪贴板占用重试与异常内存释放保护）
    static bool copyToClipboard(const std::wstring& wtext, HWND owner = nullptr) {
        if (wtext.empty()) return false;

        // 剪贴板可能被其他进程瞬时独占（如剪贴板管理器、Office），进行有界重试
        bool opened = false;
        for (int retry = 0; retry < 5; ++retry) {
            if (OpenClipboard(owner)) {
                opened = true;
                break;
            }
            Sleep(10);
        }
        if (!opened) return false;

        EmptyClipboard();
        const size_t byteCount = (wtext.length() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, byteCount);
        if (!hMem) {
            CloseClipboard();
            return false;
        }

        void* dest = GlobalLock(hMem);
        if (!dest) {
            GlobalFree(hMem);
            CloseClipboard();
            return false;
        }
        memcpy(dest, wtext.c_str(), byteCount);
        GlobalUnlock(hMem);

        if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
            GlobalFree(hMem);
            CloseClipboard();
            return false;
        }
        CloseClipboard();
        return true;
    }

    /// 复制 UTF-8 文本到剪贴板
    static bool copyToClipboard(const std::string& text, HWND owner = nullptr) {
        return copyToClipboard(utf8ToWstring(text), owner);
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

    /// Prevent a top-level EasyTools control window from appearing in screenshots
    /// and recordings. This intentionally does not fall back to WDA_MONITOR,
    /// which would leave an opaque rectangle in captured output on older systems.
    static bool excludeWindowFromCapture(HWND hwnd) {
        if (!hwnd || !IsWindow(hwnd)) return false;
        SetLastError(ERROR_SUCCESS);
        return SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE) != FALSE;
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

    /// 获取当前活动资源管理器（Explorer）或桌面所选中的文件/文件夹完整物理路径
    static std::optional<std::wstring> getSelectedExplorerFile() {
        HWND foregroundWnd = GetForegroundWindow();
        if (!foregroundWnd) return std::nullopt;

        wchar_t className[256] = {0};
        GetClassNameW(foregroundWnd, className, 256);
        std::wstring cls = className;

        bool isExplorer = (cls == L"CabinetWClass" || cls == L"ExploreWClass");
        bool isDesktop = (cls == L"Progman" || cls == L"WorkerW");

        // 检查是否为通用文件选择窗口
        if (!isExplorer && !isDesktop) {
            DWORD pid = 0;
            GetWindowThreadProcessId(foregroundWnd, &pid);
            std::wstring proc = processNameFromPid(pid);
            if (toLower(wstringToUtf8(proc)) == "explorer.exe") {
                isExplorer = true;
            }
        }

        if (!isExplorer && !isDesktop) {
            return std::nullopt;
        }

        // 检查焦点控件是否处于重命名编辑状态 (Edit 控件)，防止打字误触发
        DWORD threadId = GetWindowThreadProcessId(foregroundWnd, nullptr);
        GUITHREADINFO gti = { sizeof(GUITHREADINFO) };
        if (GetGUIThreadInfo(threadId, &gti)) {
            if (gti.hwndFocus) {
                wchar_t focusClass[256] = {0};
                GetClassNameW(gti.hwndFocus, focusClass, 256);
                if (_wcsicmp(focusClass, L"Edit") == 0) {
                    return std::nullopt;
                }
            }
        }

        // COM 遍历 IShellWindows 取得选中项
        Microsoft::WRL::ComPtr<IShellWindows> shellWindows;
        HRESULT hr = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&shellWindows));
        if (FAILED(hr) || !shellWindows) return std::nullopt;

        long count = 0;
        shellWindows->get_Count(&count);

        for (long i = 0; i < count; ++i) {
            VARIANT vi;
            VariantInit(&vi);
            vi.vt = VT_I4;
            vi.lVal = i;
            Microsoft::WRL::ComPtr<IDispatch> disp;
            HRESULT itemHr = shellWindows->Item(vi, &disp);
            VariantClear(&vi);
            if (FAILED(itemHr) || !disp) continue;

            Microsoft::WRL::ComPtr<IWebBrowserApp> app;
            if (FAILED(disp.As(&app)) || !app) continue;

            HWND hwnd = nullptr;
            app->get_HWND(reinterpret_cast<SHANDLE_PTR*>(&hwnd));
            if (!hwnd) continue;

            // 匹配顶层窗口或其父/子窗口句柄（兼容 Win11 多标签页 Explorer）
            if (hwnd != foregroundWnd && !isDesktop) {
                if (!IsChild(hwnd, foregroundWnd) && !IsChild(foregroundWnd, hwnd)) {
                    continue;
                }
            }

            Microsoft::WRL::ComPtr<IServiceProvider> sp;
            if (FAILED(disp.As(&sp)) || !sp) continue;

            Microsoft::WRL::ComPtr<IShellBrowser> sb;
            if (FAILED(sp->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&sb))) || !sb) continue;

            Microsoft::WRL::ComPtr<IShellView> sv;
            if (FAILED(sb->QueryActiveShellView(&sv)) || !sv) continue;

            Microsoft::WRL::ComPtr<IFolderView> fv;
            if (FAILED(sv.As(&fv)) || !fv) continue;

            int focusIndex = -1;
            if (FAILED(fv->GetFocusedItem(&focusIndex)) || focusIndex < 0) {
                if (FAILED(fv->GetSelectionMarkedItem(&focusIndex)) || focusIndex < 0) {
                    continue;
                }
            }

            Microsoft::WRL::ComPtr<IPersistFolder2> pf;
            if (FAILED(fv->GetFolder(IID_PPV_ARGS(&pf))) || !pf) continue;

            PIDLIST_ABSOLUTE pidlFolder = nullptr;
            if (FAILED(pf->GetCurFolder(&pidlFolder)) || !pidlFolder) continue;

            PITEMID_CHILD pidlItem = nullptr;
            if (SUCCEEDED(fv->Item(focusIndex, &pidlItem)) && pidlItem) {
                PIDLIST_ABSOLUTE pidlFull = ILCombine(pidlFolder, pidlItem);
                CoTaskMemFree(pidlItem);
                CoTaskMemFree(pidlFolder);

                if (pidlFull) {
                    wchar_t path[MAX_PATH] = {0};
                    if (SHGetPathFromIDListW(pidlFull, path)) {
                        CoTaskMemFree(pidlFull);
                        std::error_code ec;
                        if (std::filesystem::exists(path, ec)) {
                            return std::wstring(path);
                        }
                    }
                    CoTaskMemFree(pidlFull);
                }
            } else {
                CoTaskMemFree(pidlFolder);
            }
        }

        return std::nullopt;
    }

    /// 自动捕获当前选中的文本（通过发送 Ctrl+C 并读取剪贴板），若无选中则返回剪贴板现有文本
    static std::string captureSelectedText() {
        // 先尝试获取当前剪贴板
        std::string initialText;
        if (OpenClipboard(nullptr)) {
            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
            if (hData) {
                auto* pszText = static_cast<wchar_t*>(GlobalLock(hData));
                if (pszText) {
                    initialText = wstringToUtf8(pszText);
                    GlobalUnlock(hData);
                }
            }
            CloseClipboard();
        }

        // 模拟 Ctrl+C 复制选中文本
        INPUT inputs[4] = {};
        inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_CONTROL;
        inputs[1].type = INPUT_KEYBOARD; inputs[1].ki.wVk = 'C';
        inputs[2].type = INPUT_KEYBOARD; inputs[2].ki.wVk = 'C'; inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[3].type = INPUT_KEYBOARD; inputs[3].ki.wVk = VK_CONTROL; inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(4, inputs, sizeof(INPUT));

        Sleep(50); // 给系统一点时间完成复制

        // 再次读取剪贴板
        if (OpenClipboard(nullptr)) {
            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
            if (hData) {
                auto* pszText = static_cast<wchar_t*>(GlobalLock(hData));
                if (pszText) {
                    std::string newText = wstringToUtf8(pszText);
                    GlobalUnlock(hData);
                    CloseClipboard();
                    return newText;
                }
            }
            CloseClipboard();
        }

        return initialText;
    }
};

}  // namespace easy::core

#endif  // EASYTOOLS_CORE_UTILS_WINUTILS_H
