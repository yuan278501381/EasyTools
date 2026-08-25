/**
 * EasyTools - High Performance Windows Productivity Suite
 *
 * Copyright (c) 2026 Yy1 (GitHub yuan278501381) <https://github.com/yuan278501381> & EasyTools contributors
 *
 * Licensed under the MIT License.
 */

#include "DialogNavigator.h"
#include "core/logger/Logger.h"
#include "core/utils/WinUtils.h"

#include <commdlg.h>
#include <dlgs.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <uiautomation.h>
#include <algorithm>
#include <vector>
#include <thread>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <wrl/client.h>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "UIAutomationCore.lib")
#pragma comment(lib, "ole32.lib")

#ifndef CDM_FIRST
#define CDM_FIRST (WM_USER + 100)
#endif
#ifndef CDM_GETFOLDERPATH
#define CDM_GETFOLDERPATH (CDM_FIRST + 2)
#endif
#ifndef CDM_GETFILEPATH
#define CDM_GETFILEPATH (CDM_FIRST + 1)
#endif

namespace easy::dialog {

using Microsoft::WRL::ComPtr;

DialogNavigator& DialogNavigator::instance() {
    static DialogNavigator s_instance;
    return s_instance;
}

// ============================================================
// 内部工具命名空间
// ============================================================
namespace {

bool sendMessageWithTimeout(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                            LRESULT& result, UINT timeoutMs = 250) {
    DWORD_PTR rawResult = 0;
    if (!hwnd || !SendMessageTimeoutW(hwnd, message, wParam, lParam,
                                      SMTO_ABORTIFHUNG | SMTO_BLOCK,
                                      timeoutMs, &rawResult)) {
        result = 0;
        return false;
    }
    result = static_cast<LRESULT>(rawResult);
    return true;
}

std::wstring getWindowTextWithTimeout(HWND hwnd, size_t maxChars = MAX_PATH * 2) {
    if (!hwnd || maxChars < 2) return {};
    std::wstring value(maxChars, L'\0');
    LRESULT copied = 0;
    if (!sendMessageWithTimeout(hwnd, WM_GETTEXT, static_cast<WPARAM>(maxChars),
                                reinterpret_cast<LPARAM>(value.data()), copied)) {
        return {};
    }
    if (copied <= 0) return {};
    value.resize(std::min<size_t>(static_cast<size_t>(copied), maxChars - 1));
    return value;
}

bool isVisibleAddressControl(HWND hwnd) {
    if (!hwnd || !IsWindowVisible(hwnd)) return false;
    RECT bounds{};
    return GetWindowRect(hwnd, &bounds) && bounds.right > bounds.left &&
           bounds.bottom > bounds.top;
}

bool isDirectionalMark(wchar_t value) {
    return value == 0x200E || value == 0x200F ||
           (value >= 0x202A && value <= 0x202E) ||
           (value >= 0x2066 && value <= 0x2069);
}

std::wstring sanitizeShellText(std::wstring value) {
    value.erase(std::remove_if(value.begin(), value.end(), isDirectionalMark), value.end());
    while (!value.empty() && iswspace(value.back())) value.pop_back();
    return value;
}

std::wstring extractExistingDirectory(const std::wstring& shellText) {
    const std::wstring text = sanitizeShellText(shellText);
    auto isDirectory = [](const std::wstring& candidate) {
        const DWORD attributes = GetFileAttributesW(candidate.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES &&
               (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    };

    for (size_t index = 0; index + 2 < text.size(); ++index) {
        const bool driveRoot = iswalpha(text[index]) && text[index + 1] == L':' &&
                               (text[index + 2] == L'\\' || text[index + 2] == L'/');
        const bool uncRoot = text[index] == L'\\' && text[index + 1] == L'\\' &&
                             index + 3 < text.size();
        if (!driveRoot && !uncRoot) continue;
        std::wstring candidate = text.substr(index);
        if (isDirectory(candidate)) return candidate;
    }
    return {};
}

bool isTargetForeground(HWND dialogHwnd) {
    const HWND foreground = GetForegroundWindow();
    return foreground == dialogHwnd ||
           (foreground && GetAncestor(foreground, GA_ROOT) == dialogHwnd);
}

bool ensureTargetForeground(HWND dialogHwnd) {
    if (isTargetForeground(dialogHwnd)) return true;
    SetForegroundWindow(dialogHwnd);
    for (int retry = 0; retry < 10; ++retry) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (isTargetForeground(dialogHwnd)) return true;
    }
    return false;
}

bool sendKeyChord(HWND dialogHwnd, WORD modifier, WORD key) {
    if (!ensureTargetForeground(dialogHwnd)) return false;
    INPUT input[4]{};
    UINT count = 0;
    if (modifier != 0) {
        input[count].type = INPUT_KEYBOARD;
        input[count++].ki.wVk = modifier;
    }
    input[count].type = INPUT_KEYBOARD;
    input[count++].ki.wVk = key;
    input[count].type = INPUT_KEYBOARD;
    input[count].ki.wVk = key;
    input[count++].ki.dwFlags = KEYEVENTF_KEYUP;
    if (modifier != 0) {
        input[count].type = INPUT_KEYBOARD;
        input[count].ki.wVk = modifier;
        input[count++].ki.dwFlags = KEYEVENTF_KEYUP;
    }
    return SendInput(count, input, sizeof(INPUT)) == count;
}

// 每个调用线程拥有独立的 UI Automation 对象和匹配的 COM apartment
// 生命周期。RPC_E_CHANGED_MODE 表示线程已经由宿主初始化为另一种
// apartment；此时可以沿用既有 apartment，但不能替宿主 CoUninitialize。
class UiaThreadContext final {
public:
    UiaThreadContext() noexcept {
        m_initializeResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(m_initializeResult) && m_initializeResult != RPC_E_CHANGED_MODE) return;

        const HRESULT createResult = CoCreateInstance(
            CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
            IID_IUIAutomation, reinterpret_cast<void**>(m_uia.GetAddressOf()));
        if (FAILED(createResult)) m_uia.Reset();
    }

    ~UiaThreadContext() noexcept {
        // Apartment-bound interfaces must be released before COM is balanced.
        m_uia.Reset();
        if (SUCCEEDED(m_initializeResult)) CoUninitialize();
    }

    UiaThreadContext(const UiaThreadContext&) = delete;
    UiaThreadContext& operator=(const UiaThreadContext&) = delete;

    IUIAutomation* get() const noexcept { return m_uia.Get(); }

private:
    HRESULT m_initializeResult{E_UNEXPECTED};
    ComPtr<IUIAutomation> m_uia;
};

IUIAutomation* getUIA() noexcept {
    static thread_local UiaThreadContext context;
    return context.get();
}

// 安全读取 UIA ValuePattern 值
std::wstring uiaGetValue(IUIAutomationElement* elem) {
    if (!elem) return L"";
    ComPtr<IUIAutomationValuePattern> vp;
    if (FAILED(elem->GetCurrentPatternAs(UIA_ValuePatternId, IID_IUIAutomationValuePattern,
                                          reinterpret_cast<void**>(vp.GetAddressOf()))) || !vp) {
        return L"";
    }
    BSTR val = nullptr;
    if (FAILED(vp->get_CurrentValue(&val)) || !val) return L"";
    std::wstring result(val);
    SysFreeString(val);
    return result;
}

// 枚举子控件上下文结构体
struct EnumChildContext {
    HWND editHwnd{nullptr};
    HWND comboBoxHwnd{nullptr};
    HWND addressBandHwnd{nullptr};
    HWND shellViewHwnd{nullptr};
    HWND namespaceTreeHwnd{nullptr};
    HWND okButtonHwnd{nullptr};
    bool hasTabControl{false};
    bool hasDefView{false};
};

BOOL CALLBACK EnumFileDialogChildren(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<EnumChildContext*>(lParam);
    if (!ctx) return FALSE;

    wchar_t className[64] = {0};
    GetClassNameW(hwnd, className, 64);
    int ctrlId = GetDlgCtrlID(hwnd);

    if (wcscmp(className, L"SHELLDLL_DefView") == 0) {
        ctx->hasDefView = true;
        if (!ctx->shellViewHwnd) ctx->shellViewHwnd = hwnd;
    } else if (wcscmp(className, L"NamespaceTreeControl") == 0) {
        ctx->namespaceTreeHwnd = hwnd;
    } else if (wcscmp(className, L"SysTabControl32") == 0) {
        ctx->hasTabControl = true;
    }
    
    if (wcscmp(className, L"ComboBoxEx32") == 0) {
        ctx->comboBoxHwnd = hwnd;
    } else if (wcscmp(className, L"Edit") == 0) {
        if (ctrlId == 0x047C || ctrlId == 0x0442 || ctrlId == 1152 || !ctx->editHwnd) {
            ctx->editHwnd = hwnd;
        }
    } else if (wcscmp(className, L"ToolbarWindow32") == 0 || wcscmp(className, L"Breadcrumb Parent") == 0) {
        HWND parent = GetParent(hwnd);
        if (parent) {
            wchar_t pClass[64] = {0};
            GetClassNameW(parent, pClass, 64);
            if (wcsstr(pClass, L"Address") || wcsstr(pClass, L"Breadcrumb") || wcsstr(pClass, L"ReBar")) {
                ctx->addressBandHwnd = hwnd;
            }
        }
    } else if (wcscmp(className, L"Button") == 0 && (ctrlId == IDOK || ctrlId == 1 || ctrlId == 0x0400)) {
        ctx->okButtonHwnd = hwnd;
    }
    return TRUE;
}

} // namespace

// ============================================================
// 对话框类型检测：Modern(IFileOpenDialog) vs Legacy(OPENFILENAME)
// ============================================================
DialogType DialogNavigator::detectDialogType(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return DialogType::Unknown;

    // 用 CDM_GETFOLDERPATH 探测：Legacy 对话框必定响应且返回 > 0
    // Modern IFileOpenDialog 的 #32770 宿主不支持 CDM 消息，返回 0
    wchar_t cdmBuf[MAX_PATH] = {0};
    LRESULT lr = 0;
    sendMessageWithTimeout(hwnd, CDM_GETFOLDERPATH, MAX_PATH,
                           reinterpret_cast<LPARAM>(cdmBuf), lr);
    if (lr > 0 && cdmBuf[0] != L'\0') {
        return DialogType::Legacy;
    }
    return DialogType::Modern;
}

// ============================================================
// isFileDialog
// ============================================================
bool DialogNavigator::isFileDialog(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return false;

    wchar_t className[64] = {0};
    GetClassNameW(hwnd, className, 64);
    if (wcscmp(className, L"#32770") != 0) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == GetCurrentProcessId()) return false;

    EnumChildContext ctx;
    EnumChildWindows(hwnd, EnumFileDialogChildren, reinterpret_cast<LPARAM>(&ctx));

    // 1. 属性页对话框（包含 SysTabControl32）绝对不是文件对话框
    if (ctx.hasTabControl) return false;

    // 2. Modern 文件对话框判定：必须有核心文件列表视图 SHELLDLL_DefView，或者包含左侧导航树 NamespaceTreeControl
    if (ctx.hasDefView) return true;
    if (ctx.namespaceTreeHwnd && (ctx.addressBandHwnd || ctx.okButtonHwnd)) return true;

    // 3. Legacy 传统文件对话框判定：必须能成功响应 CDM_GETFOLDERPATH 消息
    wchar_t cdmBuf[MAX_PATH] = {0};
    LRESULT lr = 0;
    if (sendMessageWithTimeout(hwnd, CDM_GETFOLDERPATH, MAX_PATH,
                               reinterpret_cast<LPARAM>(cdmBuf), lr, 100) && lr > 0) {
        return true;
    }

    return false;
}

// ============================================================
// 子控件查找辅助（Legacy 模式专用）
// ============================================================
HWND DialogNavigator::findPathEditControl(HWND dialogHwnd) {
    if (!dialogHwnd || !IsWindow(dialogHwnd)) return nullptr;

    HWND bestEdit = nullptr;
    int maxTop = -1;
    struct Ctx { HWND* pBest; int* pMaxTop; } sCtx = { &bestEdit, &maxTop };

    EnumChildWindows(dialogHwnd, [](HWND hwnd, LPARAM lParam) -> BOOL {
        auto* ctx = reinterpret_cast<Ctx*>(lParam);
        wchar_t cls[64] = {0};
        GetClassNameW(hwnd, cls, 64);
        if (wcscmp(cls, L"Edit") == 0) {
            RECT rc = {}; GetWindowRect(hwnd, &rc);
            if (rc.top > *(ctx->pMaxTop)) { *(ctx->pMaxTop) = rc.top; *(ctx->pBest) = hwnd; }
        } else if (wcscmp(cls, L"ComboBoxEx32") == 0 || wcscmp(cls, L"ComboBox") == 0) {
            HWND ce = FindWindowExW(hwnd, nullptr, L"Edit", nullptr);
            if (ce) {
                RECT rc = {}; GetWindowRect(ce, &rc);
                if (rc.top > *(ctx->pMaxTop)) { *(ctx->pMaxTop) = rc.top; *(ctx->pBest) = ce; }
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&sCtx));

    return bestEdit;
}

HWND DialogNavigator::findAddressBandControl(HWND dialogHwnd) {
    if (!dialogHwnd || !IsWindow(dialogHwnd)) return nullptr;
    EnumChildContext ctx;
    EnumChildWindows(dialogHwnd, EnumFileDialogChildren, reinterpret_cast<LPARAM>(&ctx));
    return ctx.addressBandHwnd;
}

HWND DialogNavigator::findShellViewControl(HWND dialogHwnd) {
    if (!dialogHwnd || !IsWindow(dialogHwnd)) return nullptr;
    EnumChildContext ctx;
    EnumChildWindows(dialogHwnd, EnumFileDialogChildren, reinterpret_cast<LPARAM>(&ctx));
    return ctx.shellViewHwnd;
}

HWND DialogNavigator::findOkButtonControl(HWND dialogHwnd) {
    if (!dialogHwnd || !IsWindow(dialogHwnd)) return nullptr;
    EnumChildContext ctx;
    EnumChildWindows(dialogHwnd, EnumFileDialogChildren, reinterpret_cast<LPARAM>(&ctx));
    return ctx.okButtonHwnd;
}

// ============================================================
// UIAutomation 私有实现
// ============================================================

// Modern IFileDialog: 优先读取当前可见的 Address Band 面包屑；仅在用户
// 正在编辑地址时读取可见 Edit。只识别盘符/UNC 语法并验证目录存在，
// 不依赖“地址:”等任何本地化文本，也不接受隐藏控件里的历史值。
std::string DialogNavigator::uiaGetAddressBarPath(HWND dialogHwnd) {
    // Breadcrumb mode is authoritative. Windows can retain the address Edit
    // used by a previous programmatic navigation after the user has moved to
    // another folder; reading that hidden Edit first returns a stale path.
    struct NativeToolbarPath { std::wstring path; } toolbarPath;
    EnumChildWindows(dialogHwnd, [](HWND child, LPARAM parameter) -> BOOL {
        wchar_t className[32]{};
        GetClassNameW(child, className, static_cast<int>(std::size(className)));
        if (wcscmp(className, L"ToolbarWindow32") != 0 ||
            !isVisibleAddressControl(child)) return TRUE;
        auto* result = reinterpret_cast<NativeToolbarPath*>(parameter);
        result->path = extractExistingDirectory(getWindowTextWithTimeout(child));
        return result->path.empty() ? TRUE : FALSE;
    }, reinterpret_cast<LPARAM>(&toolbarPath));
    if (!toolbarPath.path.empty()) {
        return easy::core::WinUtils::wstringToUtf8(toolbarPath.path);
    }

    IUIAutomation* uia = getUIA();
    if (!uia) return "";

    ComPtr<IUIAutomationElement> dlgElem;
    if (FAILED(uia->ElementFromHandle(dialogHwnd, dlgElem.GetAddressOf())) || !dlgElem) return "";

    // Windows 11 exposes the breadcrumb address bar differently depending on
    // its current mode.  While the user is typing it is an Edit with id 41477;
    // after Enter it becomes an Address Band Root Pane (also id 41477) whose
    // child Pane (normally id 1001) owns a name such as "Address: D:\\Work".
    // Restrict the scan to that exact address-band subtree so localized labels,
    // search text and file-list cells can never be mistaken for a folder path.
    VARIANT addressId{};
    addressId.vt = VT_BSTR;
    addressId.bstrVal = SysAllocString(L"41477");
    ComPtr<IUIAutomationCondition> addressCondition;
    const HRESULT addressConditionResult = uia->CreatePropertyCondition(
        UIA_AutomationIdPropertyId, addressId, addressCondition.GetAddressOf());
    VariantClear(&addressId);
    if (SUCCEEDED(addressConditionResult) && addressCondition) {
        ComPtr<IUIAutomationElementArray> addressRoots;
        if (SUCCEEDED(dlgElem->FindAll(TreeScope_Descendants, addressCondition.Get(),
                                      addressRoots.GetAddressOf())) && addressRoots) {
            ComPtr<IUIAutomationCondition> trueCondition;
            if (SUCCEEDED(uia->CreateTrueCondition(trueCondition.GetAddressOf())) && trueCondition) {
                int rootCount = 0;
                addressRoots->get_Length(&rootCount);
                for (int rootIndex = 0; rootIndex < rootCount; ++rootIndex) {
                    ComPtr<IUIAutomationElement> addressRoot;
                    if (FAILED(addressRoots->GetElement(rootIndex, addressRoot.GetAddressOf())) ||
                        !addressRoot) continue;
                    BOOL offscreen = TRUE;
                    RECT bounds{};
                    if (FAILED(addressRoot->get_CurrentIsOffscreen(&offscreen)) || offscreen ||
                        FAILED(addressRoot->get_CurrentBoundingRectangle(&bounds)) ||
                        bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
                        continue;
                    }

                    ComPtr<IUIAutomationElementArray> addressElements;
                    if (FAILED(addressRoot->FindAll(TreeScope_Subtree, trueCondition.Get(),
                                                    addressElements.GetAddressOf())) ||
                        !addressElements) continue;
                    int count = 0;
                    addressElements->get_Length(&count);
                    for (int index = 0; index < count; ++index) {
                        ComPtr<IUIAutomationElement> element;
                        if (FAILED(addressElements->GetElement(index, element.GetAddressOf())) ||
                            !element) continue;
                        BSTR rawName = nullptr;
                        if (FAILED(element->get_CurrentName(&rawName)) || !rawName) continue;
                        const std::wstring path = extractExistingDirectory(rawName);
                        SysFreeString(rawName);
                        if (!path.empty()) {
                            return easy::core::WinUtils::wstringToUtf8(path);
                        }
                    }
                }
            }
        }
    }

    // The native Edit is valid only while it is genuinely visible (for
    // example while the user is typing a path). A hidden zero-sized Edit may
    // retain the last path EasyTools submitted and must never win over the
    // live breadcrumb above.
    struct NativeAddress { HWND hwnd; } native{};
    EnumChildWindows(dialogHwnd, [](HWND child, LPARAM parameter) -> BOOL {
        auto* result = reinterpret_cast<NativeAddress*>(parameter);
        if (GetDlgCtrlID(child) != 41477 || !isVisibleAddressControl(child)) return TRUE;
        wchar_t className[32]{};
        GetClassNameW(child, className, static_cast<int>(std::size(className)));
        if (wcscmp(className, L"Edit") == 0) {
            result->hwnd = child;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&native));
    if (native.hwnd) {
        const std::wstring path = extractExistingDirectory(getWindowTextWithTimeout(native.hwnd));
        if (!path.empty()) return easy::core::WinUtils::wstringToUtf8(path);
    }
    VARIANT toolbarType{};
    toolbarType.vt = VT_I4;
    toolbarType.lVal = UIA_ToolBarControlTypeId;
    ComPtr<IUIAutomationCondition> toolbarCondition;
    ComPtr<IUIAutomationElementArray> toolbars;
    if (FAILED(uia->CreatePropertyCondition(UIA_ControlTypePropertyId, toolbarType,
                                            toolbarCondition.GetAddressOf())) || !toolbarCondition ||
        FAILED(dlgElem->FindAll(TreeScope_Descendants, toolbarCondition.Get(),
                                toolbars.GetAddressOf())) || !toolbars) return "";

    int count = 0;
    toolbars->get_Length(&count);
    for (int index = 0; index < count; ++index) {
        ComPtr<IUIAutomationElement> toolbar;
        if (FAILED(toolbars->GetElement(index, toolbar.GetAddressOf())) || !toolbar) continue;
        BSTR name = nullptr;
        if (FAILED(toolbar->get_CurrentName(&name)) || !name) continue;
        const std::wstring path = extractExistingDirectory(name);
        SysFreeString(name);
        if (!path.empty()) return easy::core::WinUtils::wstringToUtf8(path);
    }
    return "";
}

// Modern IFileDialog: 读取底部文件名输入框。优先使用稳定控件 ID 1152，
// 避免枚举文件列表中数十个虚拟 Edit 单元格。
std::string DialogNavigator::uiaGetFileNameText(HWND dialogHwnd, bool* controlFound) {
    if (controlFound) *controlFound = false;
    struct NativeFileName { HWND hwnd; } native{};
    EnumChildWindows(dialogHwnd, [](HWND child, LPARAM parameter) -> BOOL {
        auto* result = reinterpret_cast<NativeFileName*>(parameter);
        if (GetDlgCtrlID(child) != 1152) return TRUE;
        wchar_t className[32]{};
        GetClassNameW(child, className, static_cast<int>(std::size(className)));
        if (wcscmp(className, L"Edit") == 0) {
            result->hwnd = child;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&native));
    if (native.hwnd) {
        if (controlFound) *controlFound = true;
        const std::wstring text = getWindowTextWithTimeout(native.hwnd);
        if (!text.empty()) return easy::core::WinUtils::wstringToUtf8(text);
    }

    IUIAutomation* uia = getUIA();
    if (!uia) return "";

    ComPtr<IUIAutomationElement> dlgElem;
    if (FAILED(uia->ElementFromHandle(dialogHwnd, dlgElem.GetAddressOf())) || !dlgElem) return "";

    VARIANT id{};
    id.vt = VT_BSTR;
    id.bstrVal = SysAllocString(L"1152");
    ComPtr<IUIAutomationCondition> idCondition;
    const HRESULT conditionResult = uia->CreatePropertyCondition(
        UIA_AutomationIdPropertyId, id, idCondition.GetAddressOf());
    VariantClear(&id);
    if (FAILED(conditionResult) || !idCondition) return "";

    ComPtr<IUIAutomationElement> fileNameElement;
    if (FAILED(dlgElem->FindFirst(TreeScope_Descendants, idCondition.Get(),
                                  fileNameElement.GetAddressOf())) || !fileNameElement) return "";
    if (controlFound) *controlFound = true;
    const std::wstring value = uiaGetValue(fileNameElement.Get());
    return value.empty() ? "" : easy::core::WinUtils::wstringToUtf8(value);
}

namespace {

// FOS_PICKFOLDERS dialogs normally have no file-name Edit. In that mode the
// authoritative user intent is the selected child in the Shell Items View,
// while the address bar still represents only its parent folder.
std::string uiaGetSelectedShellChild(HWND dialogHwnd, const std::string& currentFolder) {
    if (currentFolder.empty()) return {};

    IUIAutomation* uia = getUIA();
    if (!uia) return {};

    ComPtr<IUIAutomationElement> dialogElement;
    if (FAILED(uia->ElementFromHandle(dialogHwnd, dialogElement.GetAddressOf())) ||
        !dialogElement) {
        return {};
    }

    VARIANT selectedValue{};
    selectedValue.vt = VT_BOOL;
    selectedValue.boolVal = VARIANT_TRUE;
    ComPtr<IUIAutomationCondition> selectedCondition;
    if (FAILED(uia->CreatePropertyCondition(
            UIA_SelectionItemIsSelectedPropertyId, selectedValue,
            selectedCondition.GetAddressOf())) || !selectedCondition) {
        return {};
    }

    VARIANT listItemType{};
    listItemType.vt = VT_I4;
    listItemType.lVal = UIA_ListItemControlTypeId;
    VARIANT dataItemType{};
    dataItemType.vt = VT_I4;
    dataItemType.lVal = UIA_DataItemControlTypeId;
    ComPtr<IUIAutomationCondition> listItemCondition;
    ComPtr<IUIAutomationCondition> dataItemCondition;
    ComPtr<IUIAutomationCondition> shellItemTypeCondition;
    if (FAILED(uia->CreatePropertyCondition(
            UIA_ControlTypePropertyId, listItemType,
            listItemCondition.GetAddressOf())) || !listItemCondition ||
        FAILED(uia->CreatePropertyCondition(
            UIA_ControlTypePropertyId, dataItemType,
            dataItemCondition.GetAddressOf())) || !dataItemCondition ||
        FAILED(uia->CreateOrCondition(
            listItemCondition.Get(), dataItemCondition.Get(),
            shellItemTypeCondition.GetAddressOf())) || !shellItemTypeCondition) {
        return {};
    }

    ComPtr<IUIAutomationCondition> selectedShellItemCondition;
    if (FAILED(uia->CreateAndCondition(
            selectedCondition.Get(), shellItemTypeCondition.Get(),
            selectedShellItemCondition.GetAddressOf())) || !selectedShellItemCondition) {
        return {};
    }

    ComPtr<IUIAutomationElement> selectedElement;
    if (FAILED(dialogElement->FindFirst(
            TreeScope_Descendants, selectedShellItemCondition.Get(),
            selectedElement.GetAddressOf())) || !selectedElement) {
        return {};
    }

    BSTR rawName = nullptr;
    if (FAILED(selectedElement->get_CurrentName(&rawName)) || !rawName) return {};
    const std::wstring itemName = sanitizeShellText(rawName);
    SysFreeString(rawName);
    if (itemName.empty() || itemName == L"." || itemName == L".." ||
        itemName.find_first_of(L"\\/") != std::wstring::npos) {
        return {};
    }

    const std::filesystem::path parent(
        easy::core::WinUtils::utf8ToWstring(currentFolder));
    const std::filesystem::path candidate = (parent / itemName).lexically_normal();
    const DWORD attributes = GetFileAttributesW(candidate.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) return {};
    return easy::core::WinUtils::wstringToUtf8(candidate.native());
}

} // namespace

// Modern IFileDialog: UIA 地址栏导航核心。
// Windows Shell 为地址编辑框暴露稳定、非本地化的 AutomationId 41477；
// 底部文件名框是 1152，搜索框是 SearchEditBox。只有同时通过 ID、类型、
// 窗口归属和几何校验的控件才允许写入。
bool DialogNavigator::uiaNavigate(HWND dialogHwnd, const std::wstring& wPath) {
    RECT dialogRect{};
    if (!GetWindowRect(dialogHwnd, &dialogRect)) return false;
    const LONG dialogWidth = dialogRect.right - dialogRect.left;
    const LONG dialogHeight = dialogRect.bottom - dialogRect.top;
    if (dialogWidth <= 0 || dialogHeight <= 0) return false;

    auto isSafeAddressHwnd = [&](HWND editHwnd) -> bool {
        if (!editHwnd || !IsWindow(editHwnd) || GetDlgCtrlID(editHwnd) != 41477) return false;
        wchar_t className[32]{};
        GetClassNameW(editHwnd, className, static_cast<int>(std::size(className)));
        if (wcscmp(className, L"Edit") != 0) return false;
        RECT editRect{};
        if (!GetWindowRect(editHwnd, &editRect)) return false;
        const LONG width = editRect.right - editRect.left;
        const LONG height = editRect.bottom - editRect.top;
        const LONG centerX = editRect.left + width / 2;
        return (GetAncestor(editHwnd, GA_ROOT) == dialogHwnd || IsChild(dialogHwnd, editHwnd)) &&
               editRect.top >= dialogRect.top &&
               editRect.bottom <= dialogRect.top + dialogHeight * 45 / 100 &&
               centerX <= dialogRect.left + dialogWidth * 70 / 100 &&
               width >= 120 && height >= 12;
    };

    auto submitNativeAddress = [&](HWND editHwnd, const char* source) -> bool {
        if (!isSafeAddressHwnd(editHwnd)) return false;
        LRESULT ignored = 0;
        if (!sendMessageWithTimeout(editHwnd, WM_SETTEXT, 0,
                                    reinterpret_cast<LPARAM>(wPath.c_str()), ignored, 250)) {
            return false;
        }
        const LPARAM keyDown = 1 | (static_cast<LPARAM>(MapVirtualKeyW(VK_RETURN, MAPVK_VK_TO_VSC)) << 16);
        const LPARAM keyUp = keyDown | (static_cast<LPARAM>(1) << 30) | (static_cast<LPARAM>(1) << 31);
        if (!sendMessageWithTimeout(editHwnd, WM_KEYDOWN, VK_RETURN, keyDown, ignored, 250) ||
            !sendMessageWithTimeout(editHwnd, WM_KEYUP, VK_RETURN, keyUp, ignored, 250)) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(180));
        if (!IsWindow(dialogHwnd)) {
            LOG_ERROR("UIA: 地址栏回车后对话框意外关闭");
            return false;
        }
        LOG_INFO("UIA: {} 已向地址栏安全提交导航: {}", source,
                 easy::core::WinUtils::wstringToUtf8(wPath));
        return true;
    };

    // 首选原生 HWND：不依赖前台焦点，也不会触发 UIA 大树遍历。41477 是
    // Shell 地址 Edit 的控件 ID；底部输入框 1152 在入口处即被排除。
    struct AddressSearch { HWND result; } addressSearch{};
    for (int retry = 0; retry < 20 && !addressSearch.result && IsWindow(dialogHwnd); ++retry) {
        EnumChildWindows(dialogHwnd, [](HWND child, LPARAM parameter) -> BOOL {
            auto* search = reinterpret_cast<AddressSearch*>(parameter);
            if (GetDlgCtrlID(child) != 41477) return TRUE;
            wchar_t className[32]{};
            GetClassNameW(child, className, static_cast<int>(std::size(className)));
            if (wcscmp(className, L"Edit") == 0) {
                search->result = child;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&addressSearch));
        if (!addressSearch.result) std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    if (addressSearch.result && submitNativeAddress(addressSearch.result, "native id=41477")) {
        return true;
    }

    IUIAutomation* uia = getUIA();
    if (!uia) {
        LOG_WARN("UIA: IUIAutomation 接口不可用");
        return false;
    }
    ComPtr<IUIAutomationElement> dialogElement;
    if (FAILED(uia->ElementFromHandle(dialogHwnd, dialogElement.GetAddressOf())) || !dialogElement) {
        return false;
    }

    auto commitAddress = [&](IUIAutomationElement* element, const char* source) -> bool {
        if (!element || !IsWindow(dialogHwnd)) return false;

        CONTROLTYPEID controlType = 0;
        UIA_HWND nativeHandle{};
        RECT editRect{};
        BSTR automationId = nullptr;
        if (FAILED(element->get_CurrentControlType(&controlType)) ||
            FAILED(element->get_CurrentNativeWindowHandle(&nativeHandle)) || !nativeHandle ||
            FAILED(element->get_CurrentBoundingRectangle(&editRect)) ||
            FAILED(element->get_CurrentAutomationId(&automationId))) {
            SysFreeString(automationId);
            return false;
        }

        const bool exactAddressId = automationId && wcscmp(automationId, L"41477") == 0;
        SysFreeString(automationId);
        const HWND editHwnd = reinterpret_cast<HWND>(nativeHandle);
        const LONG editWidth = editRect.right - editRect.left;
        const LONG editHeight = editRect.bottom - editRect.top;
        const LONG editCenterX = editRect.left + editWidth / 2;
        const bool belongsToDialog = GetAncestor(editHwnd, GA_ROOT) == dialogHwnd ||
                                     IsChild(dialogHwnd, editHwnd);
        const bool inUpperBand = editRect.top >= dialogRect.top &&
                                 editRect.bottom <= dialogRect.top + dialogHeight * 45 / 100;
        const bool notSearchBox = editCenterX <= dialogRect.left + dialogWidth * 70 / 100;
        const bool usableSize = editWidth >= 120 && editHeight >= 12;
        if (controlType != UIA_EditControlTypeId || !exactAddressId || !belongsToDialog ||
            !inUpperBand || !notSearchBox || !usableSize) {
            LOG_WARN("UIA: {} 候选未通过地址栏硬校验 id={}, belongs={}, upper={}, "
                     "nonSearch={}, size={}", source, exactAddressId, belongsToDialog,
                     inUpperBand, notSearchBox, usableSize);
            return false;
        }

        ComPtr<IUIAutomationValuePattern> valuePattern;
        if (FAILED(element->GetCurrentPatternAs(
                UIA_ValuePatternId, IID_IUIAutomationValuePattern,
                reinterpret_cast<void**>(valuePattern.GetAddressOf()))) || !valuePattern) {
            return false;
        }
        BSTR pathValue = SysAllocString(wPath.c_str());
        const HRESULT setResult = valuePattern->SetValue(pathValue);
        SysFreeString(pathValue);
        if (FAILED(setResult)) {
            LOG_WARN("UIA: 地址栏 ValuePattern::SetValue 失败 hr=0x{:X}",
                     static_cast<unsigned>(setResult));
            return false;
        }

        // 回车直接投递给已严格识别的地址 Edit。相比全局 SendInput，
        // 不依赖前台抢焦点，也不可能触发底部默认确认按钮。
        element->SetFocus();
        return submitNativeAddress(editHwnd, source);
    };

    VARIANT addressId{};
    addressId.vt = VT_BSTR;
    addressId.bstrVal = SysAllocString(L"41477");
    ComPtr<IUIAutomationCondition> addressCondition;
    const HRESULT conditionResult = uia->CreatePropertyCondition(
        UIA_AutomationIdPropertyId, addressId, addressCondition.GetAddressOf());
    VariantClear(&addressId);
    VARIANT editType{};
    editType.vt = VT_I4;
    editType.lVal = UIA_EditControlTypeId;
    ComPtr<IUIAutomationCondition> editCondition;
    ComPtr<IUIAutomationCondition> exactAddressCondition;
    if (SUCCEEDED(conditionResult) && addressCondition &&
        SUCCEEDED(uia->CreatePropertyCondition(UIA_ControlTypePropertyId, editType,
                                               editCondition.GetAddressOf())) && editCondition) {
        uia->CreateAndCondition(addressCondition.Get(), editCondition.Get(),
                                exactAddressCondition.GetAddressOf());
    }
    if (exactAddressCondition) {
        ComPtr<IUIAutomationElement> addressElement;
        if (SUCCEEDED(dialogElement->FindFirst(TreeScope_Descendants, exactAddressCondition.Get(),
                                               addressElement.GetAddressOf())) && addressElement &&
            commitAddress(addressElement.Get(), "AutomationId=41477")) {
            return true;
        }
    }

    struct Activator { WORD modifier; WORD key; const char* name; };
    constexpr Activator activators[] = {
        {VK_MENU, 'D', "Alt+D"},
        {VK_CONTROL, 'L', "Ctrl+L"},
        {0, VK_F4, "F4"},
    };

    for (const auto& activator : activators) {
        if (!sendKeyChord(dialogHwnd, activator.modifier, activator.key)) {
            LOG_WARN("UIA: {} 未能发送到目标对话框", activator.name);
            continue;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(90));
        if (!IsWindow(dialogHwnd) || !isTargetForeground(dialogHwnd)) return false;

        ComPtr<IUIAutomationElement> focusedElem;
        HRESULT hr = uia->GetFocusedElement(focusedElem.GetAddressOf());
        if (FAILED(hr) || !focusedElem) continue;

        if (commitAddress(focusedElem.Get(), activator.name)) return true;
    }

    LOG_WARN("UIA: 所有地址栏激活方式均未通过安全校验，本次不导航");
    return false;
}


// ============================================================
// getCurrentDialogFolder — Modern + Legacy 双模
// ============================================================
std::string DialogNavigator::getCurrentDialogFolder(HWND dialogHwnd) {
    if (!dialogHwnd || !IsWindow(dialogHwnd)) return "";

    DialogType dtype = detectDialogType(dialogHwnd);

    if (dtype == DialogType::Modern) {
        // Modern: UIA 读取地址栏路径
        std::string uiaPath = uiaGetAddressBarPath(dialogHwnd);
        if (!uiaPath.empty()) {
            return uiaPath;
        }
        // UIA 降级：枚举子窗口文字找绝对路径
        std::string foundPath;
        EnumChildWindows(dialogHwnd, [](HWND hwnd, LPARAM lParam) -> BOOL {
            auto* outStr = reinterpret_cast<std::string*>(lParam);
            wchar_t buf[MAX_PATH * 2] = {0};
            GetWindowTextW(hwnd, buf, MAX_PATH * 2);
            std::wstring text = buf;
            if (text.size() >= 3 && iswalpha(text[0]) && text[1] == L':' && text[2] == L'\\') {
                if (GetFileAttributesW(text.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    *outStr = easy::core::WinUtils::wstringToUtf8(text);
                    return FALSE;
                }
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&foundPath));
        return foundPath;
    }

    // Legacy: CDM_GETFOLDERPATH
    wchar_t cdmBuffer[MAX_PATH] = {0};
    LRESULT lr = 0;
    sendMessageWithTimeout(dialogHwnd, CDM_GETFOLDERPATH, MAX_PATH,
                           reinterpret_cast<LPARAM>(cdmBuffer), lr);
    if (lr > 0 && cdmBuffer[0] != L'\0') {
        std::wstring raw(cdmBuffer);
        if (GetFileAttributesW(raw.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return easy::core::WinUtils::wstringToUtf8(raw);
        }
    }
    return "";
}

// ============================================================
// getSelectedPath — Modern + Legacy 双模
// ============================================================
std::string DialogNavigator::getSelectedPath(HWND dialogHwnd) {
    if (!dialogHwnd || !IsWindow(dialogHwnd)) return "";

    DialogType dtype = detectDialogType(dialogHwnd);

    if (dtype == DialogType::Modern) {
        // Modern: UIA 读取底部文件名框
        bool fileNameControlFound = false;
        std::string fileName = uiaGetFileNameText(dialogHwnd, &fileNameControlFound);
        if (!fileName.empty()) {
            std::wstring wFileName = easy::core::WinUtils::utf8ToWstring(fileName);
            if (GetFileAttributesW(wFileName.c_str()) != INVALID_FILE_ATTRIBUTES) {
                return fileName;
            }
            std::string curFolder = getCurrentDialogFolder(dialogHwnd);
            if (!curFolder.empty()) {
                const std::filesystem::path cand =
                    std::filesystem::path(easy::core::WinUtils::utf8ToWstring(curFolder)) /
                    easy::core::WinUtils::utf8ToWstring(fileName);
                std::error_code ec;
                if (std::filesystem::exists(cand, ec)) {
                    return easy::core::WinUtils::wstringToUtf8(cand.native());
                }
            }
        }
        // 选择文件夹对话框通常没有文件名输入框。此时必须读取 Items View
        // 中真正选中的子目录，不能把地址栏的当前父目录误当成用户选择。
        if (!fileNameControlFound) {
            const std::string currentFolder = getCurrentDialogFolder(dialogHwnd);
            const std::string selectedChild =
                uiaGetSelectedShellChild(dialogHwnd, currentFolder);
            if (!selectedChild.empty()) return selectedChild;
        }
        // 没有明确选择时必须返回空。当前目录不等于“用户已选择”，否则取消
        // 对话框或自动回位都会污染按 EXE 的记忆。
        return "";
    }

    // Legacy: CDM_GETFILEPATH
    wchar_t cdmBuffer[MAX_PATH] = {0};
    LRESULT lr = 0;
    sendMessageWithTimeout(dialogHwnd, CDM_GETFILEPATH, MAX_PATH,
                           reinterpret_cast<LPARAM>(cdmBuffer), lr);
    if (lr > 0 && cdmBuffer[0] != L'\0') {
        std::wstring raw(cdmBuffer);
        if (GetFileAttributesW(raw.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return easy::core::WinUtils::wstringToUtf8(raw);
        }
    }
    // Legacy 降级：底部 Edit 拼接
    HWND editHwnd = findPathEditControl(dialogHwnd);
    if (editHwnd && IsWindow(editHwnd)) {
        std::wstring editStr = getWindowTextWithTimeout(editHwnd, MAX_PATH);
        if (!editStr.empty()) {
            if (GetFileAttributesW(editStr.c_str()) != INVALID_FILE_ATTRIBUTES) {
                return easy::core::WinUtils::wstringToUtf8(editStr);
            }
            std::string curFolder = getCurrentDialogFolder(dialogHwnd);
            if (!curFolder.empty()) {
                const std::filesystem::path cand =
                    std::filesystem::path(easy::core::WinUtils::utf8ToWstring(curFolder)) /
                    editStr;
                std::error_code ec;
                if (std::filesystem::exists(cand, ec)) {
                    return easy::core::WinUtils::wstringToUtf8(cand.native());
                }
            }
        }
    }
    return "";
}

// ============================================================
// navigateToFolder — Modern UIAutomation + Legacy 容灾双模
// ============================================================
bool DialogNavigator::navigateToFolder(HWND dialogHwnd, const std::string& folderPath) {
    if (!dialogHwnd || !IsWindow(dialogHwnd) || folderPath.empty()) return false;

    std::wstring wPath = easy::core::WinUtils::utf8ToWstring(folderPath);
    if (wPath.empty()) return false;
    DWORD attrs = GetFileAttributesW(wPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        LOG_WARN("拒绝导航到不存在或非目录路径: {}", folderPath);
        return false;
    }

    DialogType dtype = detectDialogType(dialogHwnd);
    LOG_INFO("执行文件对话框导航: hwnd=0x{:X}, type={}, targetPath={}",
             reinterpret_cast<uintptr_t>(dialogHwnd),
             (dtype == DialogType::Modern ? "Modern(UIA)" : "Legacy(CDM)"),
             folderPath);

    if (dtype == DialogType::Modern) {
        // Modern: UIAutomation 地址栏导航（唯一可靠方案）
        bool ok = uiaNavigate(dialogHwnd, wPath);
        if (!ok) {
            LOG_WARN("UIA: Modern 对话框导航失败，目标路径={}", folderPath);
        }
        return ok;
    }

    // Legacy 对话框没有跨进程可用的 SetFolder API。采用 Windows 传统的
    // “目录尾随反斜杠 + Enter”语义，但保存并恢复原输入内容，避免目录名
    // 留在底部输入框形成预选或误提交。
    HWND editHwnd = findPathEditControl(dialogHwnd);
    if (!editHwnd) {
        LOG_WARN("Legacy: 未找到底部输入控件, hwnd=0x{:X}", reinterpret_cast<uintptr_t>(dialogHwnd));
        return false;
    }

    std::wstring wPathDir = wPath;
    if (wPathDir.back() != L'\\' && wPathDir.back() != L'/') wPathDir.push_back(L'\\');

    const int oldLength = GetWindowTextLengthW(editHwnd);
    std::wstring oldText(static_cast<size_t>(std::max(0, oldLength)), L'\0');
    if (oldLength > 0) {
        GetWindowTextW(editHwnd, oldText.data(), oldLength + 1);
    }

    DWORD_PTR ignored = 0;
    if (!SendMessageTimeoutW(editHwnd, WM_SETTEXT, 0,
                             reinterpret_cast<LPARAM>(wPathDir.c_str()),
                             SMTO_ABORTIFHUNG | SMTO_BLOCK, 250, &ignored)) {
        return false;
    }
    HWND parent = GetParent(editHwnd);
    if (parent) {
        SendMessageTimeoutW(parent, WM_COMMAND,
                            MAKEWPARAM(GetDlgCtrlID(editHwnd), EN_CHANGE),
                            reinterpret_cast<LPARAM>(editHwnd),
                            SMTO_ABORTIFHUNG | SMTO_BLOCK, 250, &ignored);
    }
    SendMessageTimeoutW(editHwnd, WM_KEYDOWN, VK_RETURN, 0x001C0001,
                        SMTO_ABORTIFHUNG | SMTO_BLOCK, 250, &ignored);
    SendMessageTimeoutW(editHwnd, WM_KEYUP, VK_RETURN, 0xC01C0001,
                        SMTO_ABORTIFHUNG | SMTO_BLOCK, 250, &ignored);

    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    if (!IsWindow(dialogHwnd)) {
        LOG_ERROR("Legacy 导航导致对话框意外关闭，已停止后续操作: {}", folderPath);
        return false;
    }

    SendMessageTimeoutW(editHwnd, WM_SETTEXT, 0,
                        reinterpret_cast<LPARAM>(oldText.c_str()),
                        SMTO_ABORTIFHUNG | SMTO_BLOCK, 250, &ignored);
    LOG_INFO("Legacy: 已提交目录导航并恢复底部输入内容: {}", folderPath);
    return true;
}

} // namespace easy::dialog

