#include <windows.h>
#include <shellapi.h>
#include <iostream>

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_USER + 100) {
        std::cout << "Tray message received: " << lParam << std::endl;
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int main() {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"TrayTestClass";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, L"TrayTestClass", L"TrayTest", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        std::cout << "CreateWindow failed: " << GetLastError() << std::endl;
        return 1;
    }

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER + 100;
    nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"Test Tray");

    BOOL ok = Shell_NotifyIconW(NIM_ADD, &nid);
    std::cout << "NIM_ADD with IDI_APPLICATION: " << (ok ? "SUCCESS" : "FAILED") << " error=" << GetLastError() << std::endl;

    if (!ok) {
        nid.cbSize = NOTIFYICONDATAW_V3_SIZE;
        ok = Shell_NotifyIconW(NIM_ADD, &nid);
        std::cout << "NIM_ADD with V3_SIZE: " << (ok ? "SUCCESS" : "FAILED") << " error=" << GetLastError() << std::endl;
    }

    Shell_NotifyIconW(NIM_DELETE, &nid);
    DestroyWindow(hwnd);
    return 0;
}
