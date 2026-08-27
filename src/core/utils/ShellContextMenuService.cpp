#include "core/utils/ShellContextMenuService.h"

#include "core/logger/Logger.h"

#include <chrono>

#include <shellapi.h>
#include <shlobj.h>

namespace easy::core {
namespace {

constexpr wchar_t HelperClassName[] = L"EasyTools_ShellMenuWorker";
thread_local IContextMenu2* t_contextMenu2 = nullptr;
thread_local IContextMenu3* t_contextMenu3 = nullptr;

LRESULT CALLBACK helperWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (t_contextMenu3) {
        LRESULT result = 0;
        if (SUCCEEDED(t_contextMenu3->HandleMenuMsg2(message, wParam, lParam, &result))) {
            return result;
        }
    }
    if (t_contextMenu2 && SUCCEEDED(t_contextMenu2->HandleMenuMsg(message, wParam, lParam))) {
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

ShellContextMenuService& ShellContextMenuService::instance() {
    static ShellContextMenuService service;
    return service;
}

bool ShellContextMenuService::showAsync(std::wstring path) {
    if (path.empty() || m_stopping.load(std::memory_order_acquire)) return false;
    std::lock_guard lock(m_mutex);
    if (m_busy.exchange(true, std::memory_order_acq_rel)) return false;
    if (m_worker.joinable()) m_worker.join();
    m_worker = std::jthread(
        [this, path = std::move(path)](std::stop_token stop) mutable {
            run(std::move(path), stop);
            m_busy.store(false, std::memory_order_release);
        });
    return true;
}

void ShellContextMenuService::shutdown() {
    if (m_stopping.exchange(true, std::memory_order_acq_rel)) return;
    std::jthread worker;
    {
        std::lock_guard lock(m_mutex);
        if (m_worker.joinable()) m_worker.request_stop();
        if (const HWND helper = m_helperWindow.load(std::memory_order_acquire); helper && IsWindow(helper)) {
            PostMessageW(helper, WM_CANCELMODE, 0, 0);
        }
        worker = std::move(m_worker);
    }
    // 析构 worker 时在锁外等待，避免与 showAsync 形成锁反转。
    m_busy.store(false, std::memory_order_release);
}

ShellContextMenuService::~ShellContextMenuService() {
    shutdown();
}

void ShellContextMenuService::run(std::wstring path, std::stop_token stop) {
    const HWND searchWindow = FindWindowW(L"EasyTools_SearchWindow", nullptr);
    if (searchWindow) SetPropW(searchWindow, L"EasyTools_ShellMenuActive", reinterpret_cast<HANDLE>(1));
    auto clearActive = [&]() {
        if (searchWindow && IsWindow(searchWindow)) {
            RemovePropW(searchWindow, L"EasyTools_ShellMenuActive");
        }
    };

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    PIDLIST_ABSOLUTE itemId = nullptr;
    IShellFolder* parent = nullptr;
    IContextMenu* contextMenu = nullptr;
    HMENU menu = nullptr;
    HWND helper = nullptr;

    const auto cleanup = [&]() {
        t_contextMenu3 = nullptr;
        t_contextMenu2 = nullptr;
        if (helper && IsWindow(helper)) DestroyWindow(helper);
        m_helperWindow.store(nullptr, std::memory_order_release);
        if (menu) DestroyMenu(menu);
        if (contextMenu) contextMenu->Release();
        if (parent) parent->Release();
        if (itemId) CoTaskMemFree(itemId);
        clearActive();
        if (SUCCEEDED(comResult)) CoUninitialize();
    };

    if (stop.stop_requested() || FAILED(SHParseDisplayName(path.c_str(), nullptr, &itemId, 0, nullptr))) {
        cleanup();
        return;
    }
    PCUITEMID_CHILD child = nullptr;
    if (FAILED(SHBindToParent(itemId, IID_IShellFolder, reinterpret_cast<void**>(&parent), &child)) || !parent) {
        cleanup();
        return;
    }
    if (FAILED(parent->GetUIObjectOf(nullptr, 1, &child, IID_IContextMenu, nullptr,
                                     reinterpret_cast<void**>(&contextMenu))) || !contextMenu) {
        cleanup();
        return;
    }

    menu = CreatePopupMenu();
    if (!menu || FAILED(contextMenu->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL | CMF_EXPLORE))) {
        cleanup();
        return;
    }
    contextMenu->QueryInterface(IID_IContextMenu2, reinterpret_cast<void**>(&t_contextMenu2));
    contextMenu->QueryInterface(IID_IContextMenu3, reinterpret_cast<void**>(&t_contextMenu3));

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = helperWindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = HelperClassName;
    RegisterClassExW(&windowClass);

    POINT cursor{};
    GetCursorPos(&cursor);
    helper = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, HelperClassName, L"", WS_POPUP,
                             cursor.x, cursor.y, 1, 1, nullptr, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    m_helperWindow.store(helper, std::memory_order_release);
    if (helper) {
        ShowWindow(helper, SW_SHOWNOACTIVATE);
        SetForegroundWindow(helper);
    }

    const UINT command = TrackPopupMenuEx(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
        cursor.x, cursor.y, helper ? helper : GetForegroundWindow(), nullptr);
    if (command >= 1 && !stop.stop_requested()) {
        CMINVOKECOMMANDINFOEX info{};
        info.cbSize = sizeof(info);
        info.fMask = CMIC_MASK_UNICODE;
        info.hwnd = helper;
        info.lpVerb = reinterpret_cast<LPCSTR>(MAKEINTRESOURCEA(command - 1));
        info.lpVerbW = reinterpret_cast<LPCWSTR>(MAKEINTRESOURCEW(command - 1));
        info.nShow = SW_SHOWNORMAL;
        contextMenu->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&info));
    }

    if (t_contextMenu3) { t_contextMenu3->Release(); t_contextMenu3 = nullptr; }
    if (t_contextMenu2) { t_contextMenu2->Release(); t_contextMenu2 = nullptr; }
    if (!stop.stop_requested() && searchWindow && IsWindow(searchWindow)) {
        SetForegroundWindow(searchWindow);
        SetFocus(searchWindow);
    }
    cleanup();
}

}  // namespace easy::core
