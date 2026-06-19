#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// CrashHandler — 崩溃收集 (MiniDump)
//
// 在程序异常终止时自动生成 .dmp 文件，便于事后分析崩溃原因。
// 使用 SetUnhandledExceptionFilter 安装全局异常过滤器。
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_CORE_CRASH_CRASHHANDLER_H
#define EASYTOOLS_CORE_CRASH_CRASHHANDLER_H

#include <windows.h>
#include <dbghelp.h>
#include <string>
#include <filesystem>
#include <chrono>
#include <format>

#pragma comment(lib, "dbghelp.lib")

namespace easy::core {

class CrashHandler {
public:
    /// 安装崩溃处理器
    /// @param dumpDir MiniDump 文件输出目录
    static void install(const std::filesystem::path& dumpDir) {
        s_dumpDir = dumpDir;
        std::filesystem::create_directories(dumpDir);
        s_previousFilter = SetUnhandledExceptionFilter(exceptionFilter);
    }

    /// 卸载崩溃处理器
    static void uninstall() {
        SetUnhandledExceptionFilter(s_previousFilter);
        s_previousFilter = nullptr;
    }

private:
    static LONG WINAPI exceptionFilter(EXCEPTION_POINTERS* exceptionInfo) {
        // 生成带时间戳的 dump 文件名
        auto now = std::chrono::system_clock::now();
        auto timeT = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &timeT);

        auto fileName = std::format(L"EasyTools_crash_{:04d}{:02d}{:02d}_{:02d}{:02d}{:02d}.dmp",
                                    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                                    tm.tm_hour, tm.tm_min, tm.tm_sec);

        auto dumpPath = s_dumpDir / fileName;

        HANDLE hFile = CreateFileW(
            dumpPath.wstring().c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (hFile != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mdei{};
            mdei.ThreadId = GetCurrentThreadId();
            mdei.ExceptionPointers = exceptionInfo;
            mdei.ClientPointers = FALSE;

            // 写入 MiniDump（包含线程、模块、句柄信息）
            MiniDumpWriteDump(
                GetCurrentProcess(),
                GetCurrentProcessId(),
                hFile,
                static_cast<MINIDUMP_TYPE>(
                    MiniDumpWithHandleData |
                    MiniDumpWithThreadInfo |
                    MiniDumpWithUnloadedModules
                ),
                &mdei,
                nullptr,
                nullptr
            );

            CloseHandle(hFile);

            // 弹窗通知用户
            auto msg = std::format(
                L"EasyTools 遇到了意外错误，已生成崩溃转储文件：\n\n{}\n\n请将此文件发送给开发者以帮助修复问题。",
                dumpPath.wstring()
            );
            MessageBoxW(nullptr, msg.c_str(), L"EasyTools 崩溃报告", MB_OK | MB_ICONERROR);
        }

        return EXCEPTION_EXECUTE_HANDLER;
    }

    static inline std::filesystem::path s_dumpDir;
    static inline LPTOP_LEVEL_EXCEPTION_FILTER s_previousFilter = nullptr;
};

}  // namespace easy::core

#endif  // EASYTOOLS_CORE_CRASH_CRASHHANDLER_H
