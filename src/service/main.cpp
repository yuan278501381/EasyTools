#include <windows.h>
#include <iostream>
#include <algorithm>
#include <array>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <sddl.h>
#include <shlobj.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <nlohmann/json.hpp>
#include "MftParser.h"

#define SERVICE_NAME L"EasyTools_SearchService"

SERVICE_STATUS        g_ServiceStatus = {0};
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE                g_ServiceStopEvent = INVALID_HANDLE_VALUE;
std::atomic<bool>     g_IsRunning{false};

std::vector<std::unique_ptr<MftParser>> g_MftParsers;

std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

std::wstring StringToWString(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

void InitLogger() {
    try {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        std::filesystem::path logDir;
        PWSTR programData = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_CREATE, nullptr,
                                           &programData)) && programData) {
            logDir = std::filesystem::path(programData) / L"EasyTools" / L"logs";
            CoTaskMemFree(programData);
        } else {
            logDir = std::filesystem::temp_directory_path() / L"EasyTools" / L"logs";
        }
        std::filesystem::create_directories(logDir);
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            (logDir / L"EasyTools_Service.log").string(), 5 * 1024 * 1024, 3);
        spdlog::sinks_init_list sink_list = { console_sink, file_sink };
        auto logger = std::make_shared<spdlog::logger>("service", sink_list.begin(), sink_list.end());
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::info);
    } catch (const spdlog::spdlog_ex& ex) {
        std::cout << "Log init failed: " << ex.what() << std::endl;
    }
}

// --------------------------------------------------------------------------------------
// Named Pipe Server
// --------------------------------------------------------------------------------------
void IPCServerThread() {
    spdlog::info("IPC Server Thread started.");
    
    // Index every local fixed NTFS volume instead of silently limiting search
    // to C:. Removable/network volumes are excluded because USN semantics and
    // availability are not stable enough for a resident service.
    const DWORD driveMask = GetLogicalDrives();
    for (char drive = 'A'; drive <= 'Z' && g_IsRunning.load(); ++drive) {
        if (!(driveMask & (1u << (drive - 'A')))) continue;
        const std::wstring root{static_cast<wchar_t>(drive), L':', L'\\', L'\0'};
        if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED) continue;
        wchar_t fileSystem[MAX_PATH]{};
        if (!GetVolumeInformationW(root.c_str(), nullptr, 0, nullptr, nullptr, nullptr,
                                   fileSystem, MAX_PATH) || _wcsicmp(fileSystem, L"NTFS") != 0) {
            continue;
        }

        auto parser = std::make_unique<MftParser>();
        if (parser->Initialize(drive)) {
            parser->EnumerateFiles();
            parser->StartListening();
            g_MftParsers.push_back(std::move(parser));
            spdlog::info("Indexed drive {}:", drive);
        }
    }
    spdlog::info("Search index ready for {} volume(s)", g_MftParsers.size());

    PSECURITY_DESCRIPTOR pipeDescriptor = nullptr;
    SECURITY_ATTRIBUTES pipeSecurity{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};
    // LocalSystem/Admin full access; any authenticated desktop user can query.
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;AU)", SDDL_REVISION_1,
            &pipeDescriptor, nullptr)) {
        pipeSecurity.lpSecurityDescriptor = pipeDescriptor;
    } else {
        spdlog::error("Failed to create named pipe security descriptor: {}", GetLastError());
    }
    
    while (g_IsRunning) {
        HANDLE hPipe = CreateNamedPipeA(
            "\\\\.\\pipe\\EasyToolsSearchPipe",
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            4,
            256 * 1024, 4096, 0,
            pipeDescriptor ? &pipeSecurity : nullptr
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            spdlog::error("CreateNamedPipe failed: {}", GetLastError());
            Sleep(1000);
            continue;
        }

        spdlog::debug("Waiting for client connection on named pipe...");
        BOOL connected = ConnectNamedPipe(hPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        
        if (connected) {
            spdlog::info("Client connected to named pipe.");
            std::array<char, 4096> buffer{};
            while (g_IsRunning.load()) {
                std::string request;
                DWORD bytesRead = 0;
                BOOL readOk = FALSE;
                do {
                    readOk = ReadFile(hPipe, buffer.data(), static_cast<DWORD>(buffer.size()),
                                      &bytesRead, nullptr);
                    if (bytesRead > 0 && request.size() < 4096) {
                        request.append(buffer.data(), std::min<size_t>(bytesRead, 4096 - request.size()));
                    }
                } while (!readOk && GetLastError() == ERROR_MORE_DATA && request.size() < 4096);
                if ((!readOk && GetLastError() != ERROR_MORE_DATA) || request.empty()) break;

                std::wstring wQuery = StringToWString(request);
                std::vector<SearchResult> results;
                for (auto& parser : g_MftParsers) {
                    auto volumeResults = parser->Search(wQuery, 50);
                    results.insert(results.end(),
                                   std::make_move_iterator(volumeResults.begin()),
                                   std::make_move_iterator(volumeResults.end()));
                }
                std::stable_sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
                    if (a.fileName.size() != b.fileName.size()) return a.fileName.size() < b.fileName.size();
                    return _wcsicmp(a.fileName.c_str(), b.fileName.c_str()) < 0;
                });
                if (results.size() > 50) results.resize(50);

                nlohmann::json responseJson = {{"results", nlohmann::json::array()}};
                for (const auto& result : results) {
                    responseJson["results"].push_back({
                        {"name", WStringToString(result.fileName)},
                        {"path", WStringToString(result.fullPath)},
                        {"isDirectory", result.isDirectory},
                    });
                }
                const std::string response = responseJson.dump();
                
                DWORD bytesWritten = 0;
                if (!WriteFile(hPipe, response.c_str(), static_cast<DWORD>(response.size()),
                               &bytesWritten, nullptr) ||
                    bytesWritten != static_cast<DWORD>(response.size())) {
                    spdlog::warn("Named pipe response write failed: {}", GetLastError());
                    break;
                }
            }
        }
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }
    for (auto& parser : g_MftParsers) parser->StopListening();
    g_MftParsers.clear();
    if (pipeDescriptor) LocalFree(pipeDescriptor);
    spdlog::info("IPC Server Thread stopped.");
}

// --------------------------------------------------------------------------------------
// Service Control
// --------------------------------------------------------------------------------------
void WINAPI ServiceCtrlHandler(DWORD CtrlCode) {
    switch (CtrlCode) {
        case SERVICE_CONTROL_STOP:
            spdlog::info("SERVICE_CONTROL_STOP received.");
            if (g_ServiceStatus.dwCurrentState != SERVICE_RUNNING)
                break;
            g_ServiceStatus.dwControlsAccepted = 0;
            g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
            g_ServiceStatus.dwWin32ExitCode = 0;
            g_ServiceStatus.dwCheckPoint = 4;
            SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
            SetEvent(g_ServiceStopEvent);
            break;
        default:
            break;
    }
}

void WINAPI ServiceMain(DWORD argc, LPTSTR *argv) {
    (void)argc;
    (void)argv;
    g_StatusHandle = RegisterServiceCtrlHandlerW(SERVICE_NAME, ServiceCtrlHandler);
    if (!g_StatusHandle) return;

    ZeroMemory(&g_ServiceStatus, sizeof(g_ServiceStatus));
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwServiceSpecificExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;

    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_ServiceStopEvent) {
        g_ServiceStatus.dwControlsAccepted = 0;
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        g_ServiceStatus.dwWin32ExitCode = GetLastError();
        g_ServiceStatus.dwCheckPoint = 1;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        return;
    }

    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    spdlog::info("Service started successfully.");
    g_IsRunning = true;
    
    // Start IPC Thread
    std::thread ipcThread(IPCServerThread);

    // Wait for stop event
    WaitForSingleObject(g_ServiceStopEvent, INFINITE);

    g_IsRunning = false;
    
    // Break the named pipe wait
    HANDLE hPipe = CreateFileA("\\\\.\\pipe\\EasyToolsSearchPipe", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hPipe != INVALID_HANDLE_VALUE) CloseHandle(hPipe);
    
    if (ipcThread.joinable()) ipcThread.join();

    CloseHandle(g_ServiceStopEvent);
    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 3;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    spdlog::info("Service stopped successfully.");
}

int main(int argc, char** argv) {
    InitLogger();
    if (argc > 1 && std::string(argv[1]) == "--debug") {
        spdlog::info("Running in debug mode (Console).");
        g_IsRunning = true;
        std::thread ipcThread(IPCServerThread);
        std::cout << "Press ENTER to stop..." << std::endl;
        std::cin.get();
        g_IsRunning = false;
        HANDLE hPipe = CreateFileA("\\\\.\\pipe\\EasyToolsSearchPipe", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (hPipe != INVALID_HANDLE_VALUE) CloseHandle(hPipe);
        if (ipcThread.joinable()) ipcThread.join();
        return 0;
    }

    SERVICE_TABLE_ENTRYW ServiceTable[] = {
        {(LPWSTR)SERVICE_NAME, (LPSERVICE_MAIN_FUNCTIONW)ServiceMain},
        {NULL, NULL}
    };
    
    if (!StartServiceCtrlDispatcherW(ServiceTable)) {
        DWORD err = GetLastError();
        if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            // 当非 SCM 服务环境（如便携版、免安装模式或主程序子进程拉起）直接执行时，自动退化为独立后台管道服务进程
            spdlog::info("Running in standalone background mode (named pipe server).");
            g_IsRunning = true;
            IPCServerThread();
            return 0;
        }
        spdlog::error("StartServiceCtrlDispatcher failed: {}.", err);
        return 1;
    }
    return 0;
}
