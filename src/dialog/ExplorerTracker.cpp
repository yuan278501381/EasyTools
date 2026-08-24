/**
 * EasyTools - High Performance Windows Productivity Suite
 * 
 * Copyright (c) 2026 Yy1 (GitHub yuan278501381) <https://github.com/yuan278501381> & EasyTools contributors
 * 
 * Licensed under the MIT License.
 */

#include "ExplorerTracker.h"
#include "core/logger/Logger.h"
#include "core/utils/WinUtils.h"
#include <shlobj.h>
#include <exdisp.h>
#include <shlwapi.h>
#include <wrl/client.h>

#pragma comment(lib, "shlwapi.lib")

using Microsoft::WRL::ComPtr;

namespace easy::dialog {

ExplorerTracker& ExplorerTracker::instance() {
    static ExplorerTracker s_instance;
    return s_instance;
}

std::string ExplorerTracker::normalizeFolderPath(const std::wstring& rawPath) {
    if (rawPath.empty()) return "";

    wchar_t localPath[MAX_PATH] = {0};
    DWORD pathLen = MAX_PATH;

    if (rawPath.rfind(L"file://", 0) == 0) {
        if (SUCCEEDED(PathCreateFromUrlW(rawPath.c_str(), localPath, &pathLen, 0))) {
            return easy::core::WinUtils::wstringToUtf8(localPath);
        }
    }

    // 已经是本地路径
    if (GetFileAttributesW(rawPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return easy::core::WinUtils::wstringToUtf8(rawPath);
    }

    return easy::core::WinUtils::wstringToUtf8(rawPath);
}

std::string ExplorerTracker::getActiveExplorerPath() {
    HWND fgHwnd = GetForegroundWindow();
    HWND activeExplorer = nullptr;

    // 检查前台窗口是否为资源管理器
    if (fgHwnd) {
        wchar_t className[64] = {0};
        GetClassNameW(fgHwnd, className, 64);
        if (wcscmp(className, L"CabinetWClass") == 0 || wcscmp(className, L"ExploreWClass") == 0) {
            activeExplorer = fgHwnd;
        }
    }

    // 通过 IShellWindows 遍历
    ComPtr<IShellWindows> shellWindows;
    HRESULT hr = CoCreateInstance(
        CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&shellWindows));
    if (FAILED(hr) || !shellWindows) {
        return "";
    }

    long count = 0;
    shellWindows->get_Count(&count);
    if (count <= 0) return "";

    std::string fallbackPath;

    for (long i = 0; i < count; ++i) {
        VARIANT vIndex;
        vIndex.vt = VT_I4;
        vIndex.lVal = i;

        ComPtr<IDispatch> disp;
        if (SUCCEEDED(shellWindows->Item(vIndex, &disp)) && disp) {
            ComPtr<IWebBrowserApp> app;
            if (SUCCEEDED(disp.As(&app)) && app) {
                HWND hwnd = nullptr;
                app->get_HWND(reinterpret_cast<SHANDLE_PTR*>(&hwnd));

                BSTR bstrUrl = nullptr;
                if (SUCCEEDED(app->get_LocationURL(&bstrUrl)) && bstrUrl) {
                    std::wstring url(bstrUrl);
                    SysFreeString(bstrUrl);

                    std::string path = normalizeFolderPath(url);
                    if (!path.empty()) {
                        // 如果正是当前激活的 Explorer 窗口，直接返回
                        if (activeExplorer && hwnd == activeExplorer) {
                            return path;
                        }
                        if (fallbackPath.empty()) {
                            fallbackPath = path;
                        }
                    }
                }
            }
        }
    }

    return fallbackPath;
}

std::vector<std::string> ExplorerTracker::getAllOpenExplorerPaths() {
    std::vector<std::string> paths;

    ComPtr<IShellWindows> shellWindows;
    HRESULT hr = CoCreateInstance(
        CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&shellWindows));
    if (FAILED(hr) || !shellWindows) {
        return paths;
    }

    long count = 0;
    shellWindows->get_Count(&count);

    for (long i = 0; i < count; ++i) {
        VARIANT vIndex;
        vIndex.vt = VT_I4;
        vIndex.lVal = i;

        ComPtr<IDispatch> disp;
        if (SUCCEEDED(shellWindows->Item(vIndex, &disp)) && disp) {
            ComPtr<IWebBrowserApp> app;
            if (SUCCEEDED(disp.As(&app)) && app) {
                BSTR bstrUrl = nullptr;
                if (SUCCEEDED(app->get_LocationURL(&bstrUrl)) && bstrUrl) {
                    std::wstring url(bstrUrl);
                    SysFreeString(bstrUrl);

                    std::string path = normalizeFolderPath(url);
                    if (!path.empty() && std::find(paths.begin(), paths.end(), path) == paths.end()) {
                        paths.push_back(path);
                    }
                }
            }
        }
    }

    return paths;
}

} // namespace easy::dialog
