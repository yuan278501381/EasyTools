#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// WinUtils — Windows API 常用操作封装
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_CORE_UTILS_WINUTILS_H
#define EASYTOOLS_CORE_UTILS_WINUTILS_H

#include <windows.h>
#include <string>
#include <filesystem>
#include <optional>
#include <tlhelp32.h>

namespace easy::core {

class WinUtils {
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

    /// 获取前台窗口的进程名
    static std::optional<std::wstring> getForegroundProcessName() {
        HWND hwnd = GetForegroundWindow();
        if (!hwnd) return std::nullopt;

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == 0) return std::nullopt;

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return std::nullopt;

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);

        std::optional<std::wstring> result;
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (entry.th32ProcessID == pid) {
                    result = entry.szExeFile;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return result;
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

    /// UTF-8 string → wstring
    static std::wstring utf8ToWstring(const std::string& str) {
        if (str.empty()) return {};
        int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
        std::wstring result(size, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), result.data(), size);
        return result;
    }
};

}  // namespace easy::core

#endif  // EASYTOOLS_CORE_UTILS_WINUTILS_H
