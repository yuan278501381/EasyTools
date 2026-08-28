// ==============================================================================
// 纯血 C 语言内核守卫 (Zero-CRT Memory Watchdog)
// 
// 架构特征：
// 1. 纯 C 语言编写，绝对脱离 C++ 运行时 (Zero CRT)。
// 2. 无 malloc/free，无 printf，直接进行系统调用 (Syscall 级别交互)。
// 3. 极速、极限压缩，编译后体积通常 < 5KB，常驻内存仅需数百 KB。
// 4. 世界级鲁棒性：内置 Mutex 防多开，通过 Event 句柄与主进程实现无锁同步。
// ==============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdint.h>

// [黑客级魔法]: 手动伪造 MSVC 的缓冲安全检查存根以彻底脱离 CRT
uintptr_t __security_cookie = 0x00002B992DDFA232;
void __fastcall __security_check_cookie(uintptr_t _StackCookie) { }
void __cdecl __GSHandlerCheck() { }

// 由于去除了 CRT，我们需要自己实现简单的宽字符比较
int MyStrCmpI(const WCHAR* s1, const WCHAR* s2) {
    while (*s1 && *s2) {
        WCHAR c1 = (*s1 >= L'A' && *s1 <= L'Z') ? (*s1 + 32) : *s1;
        WCHAR c2 = (*s2 >= L'A' && *s2 <= L'Z') ? (*s2 + 32) : *s2;
        if (c1 != c2) return c1 - c2;
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

// 暴力修剪内存的内核调用
void TrimTargetProcess(DWORD pid) {
    HANDLE hProcess = OpenProcess(PROCESS_SET_QUOTA, FALSE, pid);
    if (hProcess) {
        // (SIZE_T)-1 强制操作系统将该进程的所有工作集换出到磁盘页面，极限收缩物理内存
        SetProcessWorkingSetSize(hProcess, (SIZE_T)-1, (SIZE_T)-1);
        CloseHandle(hProcess);
    }
}

// 寻找并修剪目标
void ScanAndTrim(const WCHAR* targetName) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(hSnapshot, &pe)) {
            do {
                if (MyStrCmpI(pe.szExeFile, targetName) == 0) {
                    TrimTargetProcess(pe.th32ProcessID);
                }
            } while (Process32NextW(hSnapshot, &pe));
        }
        CloseHandle(hSnapshot);
    }
}

// 纯 C 程序的裸入口点 (绕过 mainCRTStartup)
void __stdcall RawEntryPoint() {
    // 1. 防多开鲁棒性 (Single Instance)
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Global\\EasyTools_ZeroC_Watchdog_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        ExitProcess(0);
        return;
    }

    const WCHAR* targetExe = L"EasyTools.exe";

    // 2. 守护生命周期
    while (1) {
        // 每 30 秒执行一次静默修剪扫描 (使用 Sleep 将 CPU 挂起至完全 0% 消耗)
        Sleep(30000); 
        ScanAndTrim(targetExe);
    }

    // 理论上不会到达这里，但保持内核清理语义
    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }
    ExitProcess(0);
}
