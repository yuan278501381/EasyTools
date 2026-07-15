cmake_minimum_required(VERSION 3.25)

# ─────────────────────────────────────────────────────────────────────────────
# EasyTools — 高性能桌面效率工具 (Microkernel Architecture)
# ─────────────────────────────────────────────────────────────────────────────

project(EasyTools
    VERSION 1.0.0
    DESCRIPTION "High-performance desktop productivity toolkit"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

configure_file(
    "${CMAKE_SOURCE_DIR}/src/core/Version.h.in"
    "${CMAKE_BINARY_DIR}/generated/EasyToolsVersion.h"
    @ONLY
)

# Generate Windows VERSIONINFO from the same project() version used by the UI,
# native APIs and installer. Keeping this as a function lets every shipped
# binary expose an accurate description and original filename in Explorer.
string(TIMESTAMP EASYTOOLS_COPYRIGHT_YEAR "%Y" UTC)
function(easytools_add_version_resource TARGET DESCRIPTION ORIGINAL_FILENAME FILE_TYPE)
    set(EASYTOOLS_FILE_DESCRIPTION "${DESCRIPTION}")
    set(EASYTOOLS_ORIGINAL_FILENAME "${ORIGINAL_FILENAME}")
    set(EASYTOOLS_FILE_TYPE "${FILE_TYPE}")
    set(EASYTOOLS_VERSION_RESOURCE
        "${CMAKE_BINARY_DIR}/generated/${TARGET}_version.rc")
    configure_file(
        "${CMAKE_SOURCE_DIR}/src/VersionInfo.rc.in"
        "${EASYTOOLS_VERSION_RESOURCE}"
        @ONLY
    )
    target_sources(${TARGET} PRIVATE "${EASYTOOLS_VERSION_RESOURCE}")
endfunction()

# ── 输出目录 ──────────────────────────────────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ── 编译选项 ──────────────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(
        /utf-8 /W4 /WX- /permissive- /Zc:__cplusplus /MP
    )
    add_compile_definitions(
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
    )
endif()

# ── 依赖库 ───────────────────────────────────────────────────────────────────
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(sol2 CONFIG REQUIRED)
find_package(Lua REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(OpenCV CONFIG REQUIRED)

find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h)
find_library(AVCODEC_LIBRARY avcodec)
find_library(AVFORMAT_LIBRARY avformat)
find_library(AVUTIL_LIBRARY avutil)
find_library(SWSCALE_LIBRARY swscale)
find_library(SWRESAMPLE_LIBRARY swresample)

set(WEBVIEW2_VERSION "1.0.4022.49" CACHE STRING "Pinned Microsoft.Web.WebView2 SDK version")
set(WEBVIEW2_DIR "${CMAKE_SOURCE_DIR}/packages/Microsoft.Web.WebView2.${WEBVIEW2_VERSION}")
if(NOT EXISTS "${WEBVIEW2_DIR}")
    find_program(NUGET_EXE nuget PATHS "${CMAKE_SOURCE_DIR}/packages")
    if(NUGET_EXE)
        execute_process(
            COMMAND ${NUGET_EXE} install Microsoft.Web.WebView2 -Version ${WEBVIEW2_VERSION}
                    -OutputDirectory "${CMAKE_SOURCE_DIR}/packages" -NonInteractive
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE WEBVIEW2_INSTALL_RESULT
        )
        if(NOT WEBVIEW2_INSTALL_RESULT EQUAL 0)
            message(FATAL_ERROR "NuGet failed to install Microsoft.Web.WebView2 ${WEBVIEW2_VERSION}")
        endif()
    endif()
endif()
if(NOT EXISTS "${WEBVIEW2_DIR}/build/native/include/WebView2.h")
    message(FATAL_ERROR
        "Microsoft.Web.WebView2 ${WEBVIEW2_VERSION} is missing. Run deploy.ps1 or place nuget.exe in packages/.")
endif()

# ── 文件收集 ──────────────────────────────────────────────────────────────────
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS "src/core/*.cpp" "src/core/*.h")
file(GLOB_RECURSE GESTURE_SOURCES CONFIGURE_DEPENDS "src/gesture/*.cpp" "src/gesture/*.h")
file(GLOB_RECURSE CAPTURE_SOURCES CONFIGURE_DEPENDS "src/capture/*.cpp" "src/capture/*.h")
file(GLOB_RECURSE KEYCAST_SOURCES CONFIGURE_DEPENDS "src/keycast/*.cpp" "src/keycast/*.h")
file(GLOB_RECURSE TRAY_SOURCES CONFIGURE_DEPENDS "src/tray/*.cpp" "src/tray/*.h")
file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS "src/ui/*.cpp" "src/ui/*.h")
file(GLOB_RECURSE OCR_SOURCES CONFIGURE_DEPENDS "src/ocr/*.cpp" "src/ocr/*.h")
file(GLOB_RECURSE SEARCH_SOURCES CONFIGURE_DEPENDS "src/search/*.cpp" "src/search/*.h")
file(GLOB_RECURSE SERVICE_SOURCES CONFIGURE_DEPENDS "src/service/*.cpp" "src/service/*.h")

set(HOST_SOURCES src/main.cpp ${TRAY_SOURCES} ${UI_SOURCES} src/EasyTools.rc)

# ── 1. EasyCore (动态库) ───────────────────────────────────────────────────────
add_library(EasyCore SHARED ${CORE_SOURCES})
easytools_add_version_resource(EasyCore "EasyTools Core Runtime" "EasyCore.dll" VFT_DLL)
target_compile_definitions(EasyCore PRIVATE EASYCORE_EXPORTS)
target_include_directories(EasyCore PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
    ${LUA_INCLUDE_DIR}
)
target_link_libraries(EasyCore PUBLIC
    spdlog::spdlog nlohmann_json::nlohmann_json sol2::sol2 ${LUA_LIBRARIES} fmt::fmt
    user32 shell32 advapi32 dbghelp psapi shlwapi winhttp crypt32
)

# ── 2. Plugin_Gesture (动态库) ────────────────────────────────────────────────
add_library(Plugin_Gesture SHARED ${GESTURE_SOURCES})
easytools_add_version_resource(Plugin_Gesture "EasyTools Gesture Plugin" "Plugin_Gesture.dll" VFT_DLL)
target_compile_definitions(Plugin_Gesture PRIVATE PLUGIN_EXPORTS)
target_include_directories(Plugin_Gesture PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(Plugin_Gesture PRIVATE EasyCore
    d2d1 dwrite windowscodecs user32 shell32
)
set_target_properties(Plugin_Gesture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugins")

# ── 3. Plugin_Capture (动态库) ────────────────────────────────────────────────
add_library(Plugin_Capture SHARED ${CAPTURE_SOURCES} ${OCR_SOURCES})
easytools_add_version_resource(Plugin_Capture "EasyTools Capture Plugin" "Plugin_Capture.dll" VFT_DLL)
target_compile_definitions(Plugin_Capture PRIVATE PLUGIN_EXPORTS)
target_include_directories(Plugin_Capture PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(Plugin_Capture PRIVATE EasyCore
    ${OpenCV_LIBS}
    ${AVCODEC_LIBRARY} ${AVFORMAT_LIBRARY} ${AVUTIL_LIBRARY} ${SWSCALE_LIBRARY} ${SWRESAMPLE_LIBRARY}
    d2d1 dwrite d3d11 dxgi dxguid dwmapi mfplat mfreadwrite mfuuid
    shlwapi shell32 user32 gdi32 ole32 oleaut32 advapi32 comctl32 windowscodecs winhttp windowsapp Shcore imm32
)
if(FFMPEG_INCLUDE_DIR)
    target_include_directories(Plugin_Capture PRIVATE ${FFMPEG_INCLUDE_DIR})
endif()
if(OpenCV_FOUND)
    target_include_directories(Plugin_Capture PRIVATE ${OpenCV_INCLUDE_DIRS})
endif()
set_target_properties(Plugin_Capture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugins")

# ── 3.5. Plugin_Keycast (动态库) ────────────────────────────────────────────────
add_library(Plugin_Keycast SHARED ${KEYCAST_SOURCES})
easytools_add_version_resource(Plugin_Keycast "EasyTools Keycast Plugin" "Plugin_Keycast.dll" VFT_DLL)
target_compile_definitions(Plugin_Keycast PRIVATE PLUGIN_EXPORTS)
target_include_directories(Plugin_Keycast PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(Plugin_Keycast PRIVATE EasyCore
    d2d1 dwrite msimg32 user32 shell32
)
set_target_properties(Plugin_Keycast PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugins")

# ── 3.6. Plugin_Search (动态库) ────────────────────────────────────────────────
add_library(Plugin_Search SHARED ${SEARCH_SOURCES})
easytools_add_version_resource(Plugin_Search "EasyTools Search Plugin" "Plugin_Search.dll" VFT_DLL)
target_compile_definitions(Plugin_Search PRIVATE PLUGIN_EXPORTS)
target_include_directories(Plugin_Search PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(Plugin_Search PRIVATE EasyCore nlohmann_json::nlohmann_json spdlog::spdlog)
set_target_properties(Plugin_Search PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugins")

# ── 4. EasyTools Host (可执行文件) ─────────────────────────────────────────────
# 这里移除没有扩展名的 rc 防止出问题，rc通常会被收集进去
add_executable(EasyTools WIN32 ${HOST_SOURCES})
easytools_add_version_resource(EasyTools "EasyTools Desktop Productivity Toolkit" "EasyTools.exe" VFT_APP)
target_include_directories(EasyTools PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(EasyTools PRIVATE EasyCore
    shlwapi shell32 user32 gdi32 ole32 oleaut32 advapi32 comctl32
)

if(EXISTS "${WEBVIEW2_DIR}")
    target_include_directories(EasyTools PRIVATE "${WEBVIEW2_DIR}/build/native/include")
    if("${CMAKE_GENERATOR_PLATFORM}" STREQUAL "ARM64" OR "${CMAKE_SYSTEM_PROCESSOR}" MATCHES "^(ARM64|aarch64|arm64)$")
        target_link_libraries(EasyTools PRIVATE "${WEBVIEW2_DIR}/build/native/arm64/WebView2LoaderStatic.lib")
    else()
        target_link_libraries(EasyTools PRIVATE "${WEBVIEW2_DIR}/build/native/x64/WebView2LoaderStatic.lib")
    endif()
endif()

# ── 5. EasyTools_Service (后台高权限服务) ─────────────────────────────────────────────
add_executable(EasyTools_Service ${SERVICE_SOURCES})
easytools_add_version_resource(EasyTools_Service "EasyTools File Index Service" "EasyTools_Service.exe" VFT_APP)
target_compile_definitions(EasyTools_Service PRIVATE UNICODE _UNICODE)
target_link_libraries(EasyTools_Service PRIVATE spdlog::spdlog fmt::fmt nlohmann_json::nlohmann_json advapi32 user32 shell32 ole32)

# ── 6. 自动化测试 ────────────────────────────────────────────────────────────
include(CTest)
if(BUILD_TESTING)
    add_executable(EasyToolsTests
        tests/unit/test_main.cpp
        src/gesture/GestureRecognizer.cpp
        src/gesture/ScopeRule.cpp
    )
    target_include_directories(EasyToolsTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(EasyToolsTests PRIVATE EasyCore user32 shell32)
    add_test(NAME EasyTools.Unit COMMAND EasyToolsTests)
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/ui/dist")
    add_custom_command(TARGET EasyTools POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/ui/dist"
            "$<TARGET_FILE_DIR:EasyTools>/ui"
        COMMENT "Copying WebView2 UI assets..."
    )
endif()

# ── 安装规则 ──────────────────────────────────────────────────────────────────
install(TARGETS EasyTools EasyTools_Service EasyCore RUNTIME DESTINATION bin)
install(TARGETS Plugin_Gesture Plugin_Capture Plugin_Keycast Plugin_Search RUNTIME DESTINATION bin/plugins)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources/" DESTINATION bin/resources)
