/**
 * EasyTools - High Performance Windows Productivity Suite
 * 
 * Copyright (c) 2026 Yy1 (GitHub yuan278501381) <https://github.com/yuan278501381> & EasyTools contributors
 * 
 * Licensed under the MIT License.
 */

#pragma once

#include <string>
#include <vector>
#include <windows.h>

namespace easy::dialog {

class ExplorerTracker {
public:
    static ExplorerTracker& instance();

    // 获取当前前台或最近活跃的资源管理器文件夹路径 (UTF-8)
    std::string getActiveExplorerPath();

    // 获取所有当前打开的资源管理器文件夹路径列表 (UTF-8)
    std::vector<std::string> getAllOpenExplorerPaths();

    // 从 URL 或 PIDL 提取本地规范路径
    static std::string normalizeFolderPath(const std::wstring& rawPath);

private:
    ExplorerTracker() = default;
    ~ExplorerTracker() = default;
};

} // namespace easy::dialog
