#include <windows.h>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "MftParser.h"

#define SERVICE_NAME L"EasyTools_SearchService"

SERVICE_STATUS        g_ServiceStatus = {0};
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE                g_ServiceStopEvent = INVALID_HANDLE_VALUE;
std::atomic<bool>     g_IsRunning{false};

MftParser g_MftParser;

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
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("EasyTools_Service.log", true);
        spdlog::sinks_init_list sink_list = { console_sink, file_sink };
        auto logger = std::make_shared<spdlog::logger>("service", sink_list.begin(), sink_list.end());
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::debug);
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
    
    // Initialize MFT
    if (g_MftParser.Initialize('C')) {
        g_MftParser.EnumerateFiles();
        g_MftParser.StartListening();
    }
    
    while (g_IsRunning) {
        HANDLE hPipe = CreateNamedPipeA(
            "\\\\.\\pipe\\EasyToolsSearchPipe",
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            65536, 65536, 0, NULL
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
            char buffer[1024];
            DWORD bytesRead;
            while (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                spdlog::debug("Received query: {}", buffer);
                
                std::wstring wQuery = StringToWString(buffer);
                auto results = g_MftParser.Search(wQuery, 50);

                std::string response = "{\"results\":[";
                for (size_t i = 0; i < results.size(); ++i) {
                    std::string u8Name = WStringToString(results[i]->fileName);
                    // simple JSON escaping for quotes
                    size_t pos = 0;
                    while ((pos = u8Name.find('"', pos)) != std::string::npos) {
                        u8Name.replace(pos, 1, "\\\"");
                        pos += 2;
                    }
                    response += "\"" + u8Name + "\"";
                    if (i < results.size() - 1) response += ",";
                }
                response += "]}";
                
                DWORD bytesWritten;
                WriteFile(hPipe, response.c_str(), response.size(), &bytesWritten, NULL);
            }
        }
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }
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
        spdlog::error("StartServiceCtrlDispatcher failed: {}. Use --debug to run in console.", GetLastError());
        return 1;
    }
    return 0;
}
