#pragma once

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <windows.h>
#include <lmcons.h>

#include <taskschd.h>
#include <comdef.h>

#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "comsupp.lib")

namespace easy::core::autostart {

inline std::wstring getCurrentUserName() {
    wchar_t username[UNLEN + 1] = {0};
    DWORD size = UNLEN + 1;
    if (GetUserNameW(username, &size) && size > 1) {
        return std::wstring(username);
    }
    wchar_t envBuf[256] = {0};
    if (GetEnvironmentVariableW(L"USERNAME", envBuf, 256) > 0) {
        return std::wstring(envBuf);
    }
    return L"CurrentUser";
}

inline std::wstring getTaskFolderName() {
    return L"\\EasyTools";
}

inline std::wstring getTaskName() {
    return L"Autorun for " + getCurrentUserName();
}

inline std::wstring getFullTaskPath() {
    return L"\\EasyTools\\Autorun for " + getCurrentUserName();
}

inline std::wstring getAutoStartTaskName() {
    return getFullTaskPath();
}

inline constexpr wchar_t LEGACY_AUTOSTART_TASK_NAME[] = L"EasyTools_Autostart";

inline void replaceAll(std::wstring& value, std::wstring_view from, std::wstring_view to);
inline std::wstring normalizeExecutablePath(std::wstring value);

/// 注册 Windows 计划任务（通过 Task Scheduler 2.0 原生 COM 接口，100% 对齐 PixPin 配置）
inline bool registerTaskCOM(const std::wstring& exePath) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool needUninit = SUCCEEDED(hr);

    ITaskService* pService = NULL;
    hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&pService);
    if (FAILED(hr)) {
        if (needUninit) CoUninitialize();
        return false;
    }

    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    if (FAILED(hr)) {
        pService->Release();
        if (needUninit) CoUninitialize();
        return false;
    }

    // 1. 获取根目录文件夹
    ITaskFolder* pRootFolder = NULL;
    hr = pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
    if (FAILED(hr)) {
        pService->Release();
        if (needUninit) CoUninitialize();
        return false;
    }

    // 2. 创建或获取 \EasyTools 文件夹
    ITaskFolder* pEasyToolsFolder = NULL;
    hr = pRootFolder->GetFolder(_bstr_t(L"EasyTools"), &pEasyToolsFolder);
    if (FAILED(hr)) {
        hr = pRootFolder->CreateFolder(_bstr_t(L"EasyTools"), _variant_t(), &pEasyToolsFolder);
    }
    pRootFolder->Release();

    if (FAILED(hr) || !pEasyToolsFolder) {
        pService->Release();
        if (needUninit) CoUninitialize();
        return false;
    }

    // 3. 创建任务定义
    ITaskDefinition* pTask = NULL;
    hr = pService->NewTask(0, &pTask);
    if (FAILED(hr)) {
        pEasyToolsFolder->Release();
        pService->Release();
        if (needUninit) CoUninitialize();
        return false;
    }

    // 注册信息 (RegistrationInfo)
    IRegistrationInfo* pRegInfo = NULL;
    if (SUCCEEDED(pTask->get_RegistrationInfo(&pRegInfo)) && pRegInfo) {
        pRegInfo->put_Author(_bstr_t(getCurrentUserName().c_str()));
        pRegInfo->put_Description(_bstr_t(L"EasyTools 桌面效率工具开机自启动任务"));
        pRegInfo->Release();
    }

    // 主体与最高权限 (Principal: HighestAvailable, InteractiveToken)
    IPrincipal* pPrincipal = NULL;
    if (SUCCEEDED(pTask->get_Principal(&pPrincipal)) && pPrincipal) {
        pPrincipal->put_Id(_bstr_t(L"Principal1"));
        pPrincipal->put_UserId(_bstr_t(getCurrentUserName().c_str()));
        pPrincipal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
        pPrincipal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
        pPrincipal->Release();
    }

    // 设置 (Settings: PT0S 无限生命周期, 允许电池启动, IgnoreNew 单实例)
    ITaskSettings* pSettings = NULL;
    if (SUCCEEDED(pTask->get_Settings(&pSettings)) && pSettings) {
        pSettings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW);
        pSettings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
        pSettings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
        pSettings->put_ExecutionTimeLimit(_bstr_t(L"PT0S"));
        pSettings->put_AllowHardTerminate(VARIANT_TRUE);
        pSettings->put_StartWhenAvailable(VARIANT_FALSE);
        pSettings->put_AllowDemandStart(VARIANT_TRUE);
        pSettings->put_Enabled(VARIANT_TRUE);
        pSettings->put_Hidden(VARIANT_FALSE);
        pSettings->put_Priority(4);
        pSettings->Release();
    }

    // 触发器 (Trigger: LogonTrigger with 3s delay)
    ITriggerCollection* pTriggerCollection = NULL;
    if (SUCCEEDED(pTask->get_Triggers(&pTriggerCollection)) && pTriggerCollection) {
        ITrigger* pTrigger = NULL;
        if (SUCCEEDED(pTriggerCollection->Create(TASK_TRIGGER_LOGON, &pTrigger)) && pTrigger) {
            ILogonTrigger* pLogonTrigger = NULL;
            if (SUCCEEDED(pTrigger->QueryInterface(IID_ILogonTrigger, (void**)&pLogonTrigger)) && pLogonTrigger) {
                pLogonTrigger->put_Id(_bstr_t(L"Trigger1"));
                pLogonTrigger->put_UserId(_bstr_t(getCurrentUserName().c_str()));
                pLogonTrigger->put_Delay(_bstr_t(L"PT3S"));
                pLogonTrigger->Release();
            }
            pTrigger->Release();
        }
        pTriggerCollection->Release();
    }

    // 操作 (Action: Exec)
    IActionCollection* pActionCollection = NULL;
    if (SUCCEEDED(pTask->get_Actions(&pActionCollection)) && pActionCollection) {
        IAction* pAction = NULL;
        if (SUCCEEDED(pActionCollection->Create(TASK_ACTION_EXEC, &pAction)) && pAction) {
            IExecAction* pExecAction = NULL;
            if (SUCCEEDED(pAction->QueryInterface(IID_IExecAction, (void**)&pExecAction)) && pExecAction) {
                pExecAction->put_Path(_bstr_t(exePath.c_str()));
                pExecAction->put_Arguments(_bstr_t(L"--silent"));
                pExecAction->Release();
            }
            pAction->Release();
        }
        pActionCollection->Release();
    }

    // 注册任务 (TASK_CREATE_OR_UPDATE)
    IRegisteredTask* pRegisteredTask = NULL;
    hr = pEasyToolsFolder->RegisterTaskDefinition(
        _bstr_t(getTaskName().c_str()),
        pTask,
        TASK_CREATE_OR_UPDATE,
        _variant_t(),
        _variant_t(),
        TASK_LOGON_INTERACTIVE_TOKEN,
        _variant_t(L"D:(A;;FA;;;WD)"),
        &pRegisteredTask
    );

    if (FAILED(hr)) {
        hr = pEasyToolsFolder->RegisterTaskDefinition(
            _bstr_t(getTaskName().c_str()),
            pTask,
            TASK_CREATE_OR_UPDATE,
            _variant_t(),
            _variant_t(),
            TASK_LOGON_INTERACTIVE_TOKEN,
            _variant_t(),
            &pRegisteredTask
        );
    }

    bool success = SUCCEEDED(hr);
    if (pRegisteredTask) pRegisteredTask->Release();
    pTask->Release();
    pEasyToolsFolder->Release();
    pService->Release();
    if (needUninit) CoUninitialize();

    return success;
}

/// 删除 Windows 计划任务
inline bool unregisterTaskCOM() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool needUninit = SUCCEEDED(hr);

    ITaskService* pService = NULL;
    hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&pService);
    if (FAILED(hr)) {
        if (needUninit) CoUninitialize();
        return false;
    }

    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    if (FAILED(hr)) {
        pService->Release();
        if (needUninit) CoUninitialize();
        return false;
    }

    ITaskFolder* pFolder = NULL;
    hr = pService->GetFolder(_bstr_t(getTaskFolderName().c_str()), &pFolder);
    if (SUCCEEDED(hr) && pFolder) {
        pFolder->DeleteTask(_bstr_t(getTaskName().c_str()), 0);
        pFolder->Release();
    }

    ITaskFolder* pRoot = NULL;
    if (SUCCEEDED(pService->GetFolder(_bstr_t(L"\\"), &pRoot)) && pRoot) {
        pRoot->DeleteTask(_bstr_t(LEGACY_AUTOSTART_TASK_NAME), 0);
        pRoot->Release();
    }

    pService->Release();
    if (needUninit) CoUninitialize();
    return true;
}

/// 检查 Windows 计划任务是否存在且指向当前 exe
inline bool isTaskRegisteredCOM(const std::wstring& targetExePath) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool needUninit = SUCCEEDED(hr);

    ITaskService* pService = NULL;
    hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&pService);
    if (FAILED(hr)) {
        if (needUninit) CoUninitialize();
        return false;
    }

    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    if (FAILED(hr)) {
        pService->Release();
        if (needUninit) CoUninitialize();
        return false;
    }

    ITaskFolder* pFolder = NULL;
    hr = pService->GetFolder(_bstr_t(getTaskFolderName().c_str()), &pFolder);
    if (FAILED(hr)) {
        pService->Release();
        if (needUninit) CoUninitialize();
        return false;
    }

    IRegisteredTask* pTask = NULL;
    hr = pFolder->GetTask(_bstr_t(getTaskName().c_str()), &pTask);
    bool exists = false;
    if (SUCCEEDED(hr) && pTask) {
        ITaskDefinition* pTaskDef = NULL;
        if (SUCCEEDED(pTask->get_Definition(&pTaskDef)) && pTaskDef) {
            IActionCollection* pActionCol = NULL;
            if (SUCCEEDED(pTaskDef->get_Actions(&pActionCol)) && pActionCol) {
                IAction* pAction = NULL;
                if (SUCCEEDED(pActionCol->get_Item(1, &pAction)) && pAction) {
                    IExecAction* pExecAction = NULL;
                    if (SUCCEEDED(pAction->QueryInterface(IID_IExecAction, (void**)&pExecAction)) && pExecAction) {
                        BSTR bstrPath = NULL;
                        if (SUCCEEDED(pExecAction->get_Path(&bstrPath)) && bstrPath) {
                            std::wstring exe(bstrPath);
                            SysFreeString(bstrPath);
                            exists = (normalizeExecutablePath(exe) == normalizeExecutablePath(targetExePath));
                        }
                        pExecAction->Release();
                    }
                    pAction->Release();
                }
                pActionCol->Release();
            }
            pTaskDef->Release();
        }
        pTask->Release();
    }

    pFolder->Release();
    pService->Release();
    if (needUninit) CoUninitialize();
    return exists;
}

inline void replaceAll(std::wstring& value, std::wstring_view from, std::wstring_view to) {
    size_t pos = 0;
    while ((pos = value.find(from, pos)) != std::wstring::npos) {
        value.replace(pos, from.size(), to);
        pos += to.size();
    }
}

inline std::wstring decodeXml(std::wstring value) {
    replaceAll(value, L"&quot;", L"\"");
    replaceAll(value, L"&apos;", L"'");
    replaceAll(value, L"&lt;", L"<");
    replaceAll(value, L"&gt;", L">");
    replaceAll(value, L"&amp;", L"&");
    return value;
}

inline std::optional<std::wstring> xmlElement(std::wstring_view xml, std::wstring_view name) {
    const std::wstring open = L"<" + std::wstring(name) + L">";
    const std::wstring close = L"</" + std::wstring(name) + L">";
    const size_t begin = xml.find(open);
    if (begin == std::wstring_view::npos) return std::nullopt;
    const size_t valueBegin = begin + open.size();
    const size_t end = xml.find(close, valueBegin);
    if (end == std::wstring_view::npos) return std::nullopt;
    return decodeXml(std::wstring(xml.substr(valueBegin, end - valueBegin)));
}

inline std::wstring normalizeExecutablePath(std::wstring value) {
    while (!value.empty() && std::iswspace(value.front())) value.erase(value.begin());
    while (!value.empty() && std::iswspace(value.back())) value.pop_back();
    if (value.size() >= 2 && value.front() == L'\"' && value.back() == L'\"') {
        value = value.substr(1, value.size() - 2);
    }
    value = std::filesystem::path(value).lexically_normal().wstring();
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}

inline bool taskTargetsExecutable(std::wstring_view taskXml,
                                  const std::filesystem::path& executable) {
    const auto command = xmlElement(taskXml, L"Command");
    return command && normalizeExecutablePath(*command) ==
                          normalizeExecutablePath(executable.wstring());
}

} // namespace easy::core::autostart
