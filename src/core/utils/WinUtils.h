#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// WinUtils — Windows API 常用操作封装
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_CORE_UTILS_WINUTILS_H
#define EASYTOOLS_CORE_UTILS_WINUTILS_H

#include "core/utils/Export.h"
#include "../../common/AtomicFile.h"

#include <windows.h>
#include <shellapi.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <shldisp.h>
#include <exdisp.h>
#include <dwmapi.h>
#include <wrl/client.h>
#include <string>
#include <filesystem>
#include <optional>
#include <tlhelp32.h>
#include <algorithm>
#include <cctype>
#include <vector>
#include <thread>

namespace easy::core {

class EASYCORE_API WinUtils {
public:
    /// 获取可执行文件所在目录
    static std::filesystem::path getExeDirectory() {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        return std::filesystem::path(path).parent_path();
    }

    /// 判断是否处于绿色便携模式 (Portable Mode)
    static bool isPortableMode() {
        const auto exeDir = getExeDirectory();
        std::error_code ec;
        if (std::filesystem::is_directory(exeDir / L".easytools", ec)) return true;
        if (std::filesystem::is_directory(exeDir / L"data", ec)) return true;
        if (std::filesystem::is_directory(exeDir / L"portable_data", ec)) return true;
        return false;
    }

    /// 获取应用数据根目录（优先使用主程序目录下 .easytools / data 便携目录，否则使用 LocalAppData）
    static std::filesystem::path getAppDataDirectory() {
        const auto exeDir = getExeDirectory();
        std::error_code ec;
        
        // 1. 便携模式检测：若主程序同级目录下存在 .easytools 或 data 目录，优先作为数据根目录
        if (std::filesystem::is_directory(exeDir / L".easytools", ec)) {
            return exeDir / L".easytools";
        }
        if (std::filesystem::is_directory(exeDir / L"data", ec)) {
            return exeDir / L"data";
        }
        if (std::filesystem::is_directory(exeDir / L"portable_data", ec)) {
            return exeDir / L"portable_data";
        }

        // 2. 标准系统模式：回退到 %LOCALAPPDATA%\EasyTools
        wchar_t path[MAX_PATH] = {};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path))) {
            auto dir = std::filesystem::path(path) / L"EasyTools";
            std::filesystem::create_directories(dir, ec);
            return dir;
        }
        return exeDir / L"data";
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

    /// 进程级工作集修剪。仅允许在真正的冷路径调用（插件停用、长截图管线关闭、全部贴图关闭）。
    /// 禁止在设置/搜索/托盘/预览/手势轨迹等可反复打开的 UI 路径调用，以免软缺页抵消预热。
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

    /// 判断窗口是否为桌面背景窗口 (Progman / WorkerW)
    static bool isDesktopWindow(HWND hwnd) {
        if (!hwnd || !IsWindow(hwnd)) return false;
        std::wstring cls = getWindowClassName(hwnd);
        if (cls == L"Progman" || cls == L"WorkerW") return true;
        HWND parent = GetParent(hwnd);
        if (parent) {
            std::wstring pCls = getWindowClassName(parent);
            if (pCls == L"Progman" || pCls == L"WorkerW") return true;
        }
        return false;
    }

    /// 判断窗口是否为任务栏窗口 (Shell_TrayWnd / Shell_SecondaryTrayWnd)
    static bool isTaskbarWindow(HWND hwnd) {
        if (!hwnd || !IsWindow(hwnd)) return false;
        HWND root = GetAncestor(hwnd, GA_ROOT);
        if (!root) root = hwnd;
        std::wstring cls = getWindowClassName(root);
        return (cls == L"Shell_TrayWnd" || cls == L"Shell_SecondaryTrayWnd");
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
        HWND root = GetAncestor(hwnd, GA_ROOT);
        if (root) hwnd = root;

        // 排除桌面和 Shell 窗口
        if (hwnd == GetDesktopWindow() || hwnd == GetShellWindow()) return false;
        std::wstring className = getWindowClassName(hwnd);
        if (className == L"Progman" || className == L"WorkerW" || className == L"Shell_TrayWnd") {
            return false;
        }

        // 若具有标准标题栏（WS_CAPTION），说明是普通窗口或常规最大化窗口，非全屏独占模式
        LONG style = GetWindowLongW(hwnd, GWL_STYLE);
        if ((style & WS_CAPTION) == WS_CAPTION) {
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

        // 判定无边框窗口是否覆盖整个物理显示器区域（如 3D 游戏、F11全屏、全屏视频）
        return (rcWindow.left <= mi.rcMonitor.left &&
                rcWindow.top <= mi.rcMonitor.top &&
                rcWindow.right >= mi.rcMonitor.right &&
                rcWindow.bottom >= mi.rcMonitor.bottom);
    }

    static bool queryProcessElevated(HANDLE process) {
        if (!process) return false;
        HANDLE token = nullptr;
        if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return false;
        TOKEN_ELEVATION elev{};
        DWORD ret = 0;
        const bool ok = GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &ret) != FALSE;
        CloseHandle(token);
        return ok && elev.TokenIsElevated != 0;
    }

    static bool isCurrentProcessElevated() {
        return queryProcessElevated(GetCurrentProcess());
    }

    struct WindowProcessQuery {
        bool queryLimitedOk = false;
        bool queryInformationOk = false;
        bool tokenQueryOk = false;
        bool tokenElevated = false;
    };

    /// 探测对本窗口进程的查询权限。勿在 WH_MOUSE_LL 热路径调用。
    static WindowProcessQuery queryWindowProcessAccess(HWND hwnd) {
        WindowProcessQuery q;
        if (!hwnd || !IsWindow(hwnd)) return q;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (!pid) return q;

        HANDLE limited = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        q.queryLimitedOk = limited != nullptr;

        HANDLE full = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
        q.queryInformationOk = full != nullptr;

        HANDLE tokenSource = full ? full : limited;
        if (tokenSource) {
            HANDLE token = nullptr;
            if (OpenProcessToken(tokenSource, TOKEN_QUERY, &token)) {
                q.tokenQueryOk = true;
                TOKEN_ELEVATION elev{};
                DWORD ret = 0;
                if (GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &ret)) {
                    q.tokenElevated = elev.TokenIsElevated != 0;
                }
                CloseHandle(token);
            }
        }
        if (full) CloseHandle(full);
        if (limited) CloseHandle(limited);
        return q;
    }

    static bool isWindowProcessElevated(HWND hwnd) {
        return queryWindowProcessAccess(hwnd).tokenElevated;
    }

    /// 目标完整性更高时，Medium IL 的 WH_MOUSE_LL 收不到事件，PostMessage / SendInput 会被 UIPI 丢掉。
    /// QUERY_LIMITED 成功但 TOKEN_QUERY 被拒，也视为更高完整性（管理员窗常见）。
    static bool isWindowHigherIntegrity(HWND hwnd) {
        const auto q = queryWindowProcessAccess(hwnd);
        if (!q.queryLimitedOk) return false;
        if (!q.queryInformationOk || !q.tokenQueryOk) {
            return !isCurrentProcessElevated();
        }
        return q.tokenElevated && !isCurrentProcessElevated();
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
                    const bool hasPath = SHGetPathFromIDListW(pidlFull, path) != FALSE;
                    CoTaskMemFree(pidlFull);
                    if (hasPath) {
                        std::error_code ec;
                        if (std::filesystem::exists(path, ec)) {
                            return std::wstring(path);
                        }
                    }
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

    struct SystemDriveInfo {
        char letter = 0;
        std::wstring path;
        std::wstring volumeLabel;
        std::wstring fileSystem;
        std::wstring typeStr; // "fixed", "remote", "removable", "cdrom", "ramdisk", "unknown"
        uint64_t totalBytes = 0;
        uint64_t freeBytes = 0;
    };

    /// 枚举当前系统所有驱动器（包括本地磁盘、映射网络驱动器、U盘/移动硬盘等）
    static std::vector<SystemDriveInfo> getSystemDrives() {
        std::vector<SystemDriveInfo> drives;
        DWORD driveMask = GetLogicalDrives();
        for (char letter = 'A'; letter <= 'Z'; ++letter) {
            if (!(driveMask & (1u << (letter - 'A')))) continue;

            SystemDriveInfo info;
            info.letter = letter;
            info.path = std::wstring{static_cast<wchar_t>(letter), L':', L'\\'};

            UINT driveType = GetDriveTypeW(info.path.c_str());
            switch (driveType) {
                case DRIVE_FIXED:     info.typeStr = L"fixed"; break;
                case DRIVE_REMOTE:    info.typeStr = L"remote"; break;
                case DRIVE_REMOVABLE: info.typeStr = L"removable"; break;
                case DRIVE_CDROM:     info.typeStr = L"cdrom"; break;
                case DRIVE_RAMDISK:   info.typeStr = L"ramdisk"; break;
                default:              info.typeStr = L"unknown"; break;
            }

            wchar_t volumeName[MAX_PATH + 1] = {};
            wchar_t fileSystemName[MAX_PATH + 1] = {};
            DWORD serialNumber = 0, maxComponentLen = 0, flags = 0;
            if (GetVolumeInformationW(info.path.c_str(), volumeName, MAX_PATH + 1,
                                     &serialNumber, &maxComponentLen, &flags,
                                     fileSystemName, MAX_PATH + 1)) {
                info.volumeLabel = volumeName;
                info.fileSystem = fileSystemName;
            }

            ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes, totalNumberOfFreeBytes;
            if (GetDiskFreeSpaceExW(info.path.c_str(), &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
                info.totalBytes = totalNumberOfBytes.QuadPart;
                info.freeBytes = totalNumberOfFreeBytes.QuadPart;
            }

            drives.push_back(std::move(info));
        }
        return drives;
    }

    /// 在 Windows 资源管理器中定位并高亮选中文件或目录。
    /// 采用独立 STA 线程异步解耦与三级降级容灾链 (Tier 1: SHOpenFolderAndSelectItems -> Tier 2: explorer /select -> Tier 3: 打开父目录)
    /// 配合异常隔离，彻底杜绝主 UI 线程 / IPC 线程在 Windows Explorer 繁忙时出现卡死 (AppHang) 或崩溃。
    static bool openFolderAndSelectItem(const std::wstring& filePath) {
        if (filePath.empty()) return false;

        // 异步派发到独立 STA 工作线程中执行，彻底解耦调用方线程（UI/IPC）与 Shell 唤醒阻塞
        std::thread([path = filePath]() {
            try {
                const HRESULT hrOle = OleInitialize(nullptr);

                bool located = false;
                PIDLIST_ABSOLUTE pidl = nullptr;
                // Tier 1: 原生 PIDL 定位高亮
                if (SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr)) && pidl) {
                    const HRESULT hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
                    ILFree(pidl);
                    if (SUCCEEDED(hr)) {
                        located = true;
                    }
                }

                if (SUCCEEDED(hrOle)) {
                    OleUninitialize();
                }

                if (located) return;

                // Tier 2: 降级通过 explorer.exe /select,"path" 调起
                const std::wstring args = L"/select,\"" + path + L"\"";
                const auto ret = reinterpret_cast<INT_PTR>(
                    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL));
                if (ret > 32) return;

                // Tier 3: 兜底直接打开父文件夹
                std::filesystem::path fsPath(path);
                std::error_code ec;
                auto parent = fsPath.parent_path();
                if (!parent.empty() && std::filesystem::exists(parent, ec)) {
                    ShellExecuteW(nullptr, L"open", parent.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
            } catch (...) {
                // 异常隔离
            }
        }).detach();

        return true;
    }

    /// 非阻塞启动/打开指定文件或应用程序
    static bool openFile(const std::wstring& filePath) {
        if (filePath.empty()) return false;
        return reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, L"open", filePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
    }

    /// 非阻塞在记事本中打开指定文件
    static bool openWithNotepad(const std::wstring& filePath) {
        if (filePath.empty()) return false;
        const std::wstring args = L"\"" + filePath + L"\"";
        return reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, L"open", L"notepad.exe", args.c_str(), nullptr, SW_SHOWNORMAL)) > 32;
    }

    /// 非阻塞以管理员身份启动程序
    static bool openFileAsAdmin(const std::wstring& filePath) {
        if (filePath.empty()) return false;
        return reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, L"runas", filePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
    }

    /// 非阻塞弹出 Windows 原生文件属性对话框
    static bool showFileProperties(const std::wstring& filePath) {
        if (filePath.empty()) return false;
        std::thread([path = filePath]() {
            try {
                const HRESULT hrOle = OleInitialize(nullptr);
                SHELLEXECUTEINFOW sei{};
                sei.cbSize = sizeof(sei);
                sei.fMask = SEE_MASK_INVOKEIDLIST;
                sei.lpVerb = L"properties";
                sei.lpFile = path.c_str();
                sei.nShow = SW_SHOWNORMAL;
                ShellExecuteExW(&sei);
                if (SUCCEEDED(hrOle)) OleUninitialize();
            } catch (...) {
            }
        }).detach();
        return true;
    }

    /// 判断 Windows 系统任务栏是否为深色模式 (用于自适应托盘图标与浮层明暗)
    static bool isSystemTaskbarDark() {
        DWORD data = 0;
        DWORD dataSize = sizeof(data);
        // 读取 Windows 10 (1903+) / Windows 11 系统任务栏主题设置 (0: Dark, 1: Light)
        LONG res = RegGetValueW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"SystemUsesLightTheme",
            RRF_RT_REG_DWORD,
            nullptr,
            &data,
            &dataSize
        );
        if (res == ERROR_SUCCESS) {
            return (data == 0); // 0 为深色任务栏，1 为浅色任务栏
        }

        // 若不存在，尝试读取应用级主题 AppsUseLightTheme 回退
        res = RegGetValueW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme",
            RRF_RT_REG_DWORD,
            nullptr,
            &data,
            &dataSize
        );
        if (res == ERROR_SUCCESS) {
            return (data == 0);
        }

        // 默认回退深色
        return true;
    }

    /// 判断 Windows 应用是否为深色模式 (AppsUseLightTheme, 0: Dark, 1: Light)
    static bool isSystemDarkMode() {
        DWORD data = 0;
        DWORD dataSize = sizeof(data);
        LONG res = RegGetValueW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme",
            RRF_RT_REG_DWORD,
            nullptr,
            &data,
            &dataSize
        );
        if (res == ERROR_SUCCESS) {
            return (data == 0);
        }
        return isSystemTaskbarDark();
    }

    /// 获取/初始化进程级 Job Object (带 JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE)
    static HANDLE getProcessJobObject() {
        static HANDLE s_job = []() -> HANDLE {
            HANDLE job = CreateJobObjectW(nullptr, nullptr);
            if (!job) return nullptr;

            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
            jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
            if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli))) {
                CloseHandle(job);
                return nullptr;
            }
            // 将当前主进程绑定到 Job Object
            AssignProcessToJobObject(job, GetCurrentProcess());
            return job;
        }();
        return s_job;
    }

    /// 将派生子进程安全加入到当前进程树的 Job Object 中
    static bool assignProcessToCurrentJob(HANDLE hProcess) {
        if (!hProcess) return false;
        HANDLE job = getProcessJobObject();
        if (!job) return false;
        return AssignProcessToJobObject(job, hProcess) != FALSE;
    }

    /// 强制将文件句柄对应的未写缓冲刷入物理存储硬件 (FlushFileBuffers)
    static bool flushFileToPhysicalDisk(HANDLE hFile) {
        if (!hFile || hFile == INVALID_HANDLE_VALUE) return false;
        return FlushFileBuffers(hFile) != FALSE;
    }

    /// 原子级写入文件 (写临时文件 + 物理硬件落盘 + ReplaceFile/MoveFileEx 原子替换)
    static bool atomicWriteFileWithFlush(const std::wstring& targetPath, const std::string& data) {
        return easy::common::atomicWriteFileWithFlush(targetPath, data);
    }

    /// 创建系统低物理内存状态事件通知句柄
    static HANDLE createLowMemoryNotification() {
        return CreateMemoryResourceNotification(LowMemoryResourceNotification);
    }

    /// 创建轻量级不可见辅助宿主窗口，用于隔绝 Overlay 窗口在 Windows 任务栏与通知区域产生图标
    static HWND createOverlayHelperOwner(HINSTANCE hInstance, const wchar_t* name = L"EasyTools_OverlayHelperOwner") {
        return CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"STATIC",
            name,
            WS_POPUP,
            0, 0, 0, 0,
            nullptr, nullptr, hInstance, nullptr
        );
    }

    /// 将窗口样式标准化为绝对不污染任务栏与 Alt+Tab 的零泄漏 Overlay 窗口
    static void applyTaskbarSafeOverlayStyle(HWND hwnd, bool excludeFromCapture = true) {
        if (!hwnd || !IsWindow(hwnd)) return;
        LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        constexpr LONG_PTR required = WS_EX_LAYERED | WS_EX_TRANSPARENT |
                                      WS_EX_TOPMOST | WS_EX_NOACTIVATE |
                                      WS_EX_TOOLWINDOW;
        exStyle = (exStyle | required) & ~static_cast<LONG_PTR>(WS_EX_APPWINDOW);
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);

        const DWM_WINDOW_CORNER_PREFERENCE noCorners = DWMWCP_DONOTROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                              &noCorners, sizeof(noCorners));
        const BOOL disableTransitions = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED,
                              &disableTransitions, sizeof(disableTransitions));
        if (excludeFromCapture) {
            excludeWindowFromCapture(hwnd);
        } else {
            SetWindowDisplayAffinity(hwnd, WDA_NONE);
        }
    }

    /// 为任意 Win32 / WebView2 宿主窗口赋予跨平台通用圆角 (全兼容 Windows 10/11/Server 2019/2022/2025)
    static void applyUniversalRoundedCorners(HWND hwnd, int width, int height, int radius) {
        if (!hwnd || !IsWindow(hwnd) || width <= 0 || height <= 0 || radius <= 0) return;

        // 1. Windows 11 DWM 硬件级超平滑圆角首选
        const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

        // 2. Win32 硬件级 RGN 裁剪兜底 (确保在 Win10、Windows Server 2022/2025、虚拟机与 RDP 下 100% 绝对圆角)
        const HRGN hRgn = CreateRoundRectRgn(0, 0, width + 1, height + 1, radius * 2, radius * 2);
        if (hRgn) {
            SetWindowRgn(hwnd, hRgn, TRUE);
            // SetWindowRgn 成功后系统接管 hRgn 句柄的所有权，无需手动 DeleteObject
        }
    }
};

}  // namespace easy::core

#endif  // EASYTOOLS_CORE_UTILS_WINUTILS_H
