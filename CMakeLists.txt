cmake_minimum_required(VERSION 3.25)

# ─────────────────────────────────────────────────────────────────────────────
# EasyTools — 高性能桌面效率工具
# 技术栈: C++20 + WebView2 + Direct2D + Lua
# ─────────────────────────────────────────────────────────────────────────────

project(EasyTools
    VERSION 0.1.0
    DESCRIPTION "High-performance desktop productivity toolkit"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── 输出目录 ──────────────────────────────────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ── 编译选项 ──────────────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(
        /utf-8          # 源码编码 UTF-8
        /W4             # 高警告级别
        /WX-            # 警告不视为错误（开发阶段）
        /permissive-    # 严格标准一致性
        /Zc:__cplusplus # 正确报告 __cplusplus 宏
        /MP             # 多处理器并行编译
    )
    add_compile_definitions(
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# ── vcpkg 依赖发现 ───────────────────────────────────────────────────────────
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(sol2 CONFIG REQUIRED)
find_package(Lua REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(OpenCV CONFIG REQUIRED)

# FFmpeg (通过 vcpkg 管理，需要按库分别查找)
find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h)
find_library(AVCODEC_LIBRARY avcodec)
find_library(AVFORMAT_LIBRARY avformat)
find_library(AVUTIL_LIBRARY avutil)
find_library(SWSCALE_LIBRARY swscale)
find_library(SWRESAMPLE_LIBRARY swresample)

# WebView2 (NuGet 包，通过 nuget.config 或手动下载)
# 先检查 packages 目录下是否有 WebView2 SDK
file(GLOB WEBVIEW2_DIRS "${CMAKE_SOURCE_DIR}/packages/Microsoft.Web.WebView2.*")
if(WEBVIEW2_DIRS)
    list(GET WEBVIEW2_DIRS 0 WEBVIEW2_DIR)
else()
    set(WEBVIEW2_DIR "${CMAKE_SOURCE_DIR}/packages/Microsoft.Web.WebView2")
endif()

if(NOT EXISTS "${WEBVIEW2_DIR}")
    message(STATUS "WebView2 SDK not found, will try to download via nuget...")
    # 尝试通过 nuget 下载
    find_program(NUGET_EXE nuget PATHS "${CMAKE_SOURCE_DIR}/packages")
    if(NUGET_EXE)
        execute_process(
            COMMAND ${NUGET_EXE} install Microsoft.Web.WebView2 -OutputDirectory "${CMAKE_SOURCE_DIR}/packages"
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        file(GLOB WEBVIEW2_DIRS "${CMAKE_SOURCE_DIR}/packages/Microsoft.Web.WebView2.*")
        if(WEBVIEW2_DIRS)
            list(GET WEBVIEW2_DIRS 0 WEBVIEW2_DIR)
        endif()
    endif()
endif()

# ── 收集源文件 ────────────────────────────────────────────────────────────────
file(GLOB_RECURSE EASYTOOLS_SOURCES
    "src/*.cpp"
    "src/*.h"
    "src/*.rc"
)

# ── 主可执行目标 ──────────────────────────────────────────────────────────────
add_executable(EasyTools WIN32 ${EASYTOOLS_SOURCES})

target_include_directories(EasyTools PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${LUA_INCLUDE_DIR}
)

target_link_libraries(EasyTools PRIVATE
    # 第三方库
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    sol2::sol2
    ${LUA_LIBRARIES}
    fmt::fmt
    ${OpenCV_LIBS}

    # FFmpeg
    ${AVCODEC_LIBRARY}
    ${AVFORMAT_LIBRARY}
    ${AVUTIL_LIBRARY}
    ${SWSCALE_LIBRARY}
    ${SWRESAMPLE_LIBRARY}

    # Windows SDK
    d2d1 dwrite d3d11 dxgi dxguid
    dwmapi mfplat mfreadwrite mfuuid
    shlwapi shell32 user32 gdi32
    ole32 oleaut32 advapi32 comctl32 dbghelp
    windowscodecs winhttp runtimeobject
)

# WebView2 头文件和加载器库
if(EXISTS "${WEBVIEW2_DIR}")
    target_include_directories(EasyTools PRIVATE
        "${WEBVIEW2_DIR}/build/native/include"
    )
    target_link_libraries(EasyTools PRIVATE
        "${WEBVIEW2_DIR}/build/native/x64/WebView2LoaderStatic.lib"
    )
    message(STATUS "WebView2 SDK found: ${WEBVIEW2_DIR}")
else()
    message(WARNING "WebView2 SDK not found. SettingsWindow will not compile.")
endif()

# OpenCV 头文件
if(OpenCV_FOUND)
    target_include_directories(EasyTools PRIVATE ${OpenCV_INCLUDE_DIRS})
endif()

# FFmpeg 头文件
if(FFMPEG_INCLUDE_DIR)
    target_include_directories(EasyTools PRIVATE ${FFMPEG_INCLUDE_DIR})
endif()

# ── 单元测试 ──────────────────────────────────────────────────────────────────
# 轻量、零测试框架依赖。覆盖纯逻辑 (手势识别 / 作用域规则匹配)。
# 默认随构建一起编译; deploy.ps1 在构建后运行它, 失败则使 CI 失败。
add_executable(EasyToolsTests
    tests/unit/test_main.cpp
    src/gesture/GestureRecognizer.cpp
    src/gesture/ScopeRule.cpp
    src/core/logger/Logger.cpp
)
target_include_directories(EasyToolsTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(EasyToolsTests PRIVATE
    spdlog::spdlog
    fmt::fmt
    nlohmann_json::nlohmann_json
    user32 shell32  # ScopeRule/WinUtils/Logger 引用的 Win32 符号
)

# ── 复制前端资源到输出目录 ─────────────────────────────────────────────────────
if(EXISTS "${CMAKE_SOURCE_DIR}/ui/dist")
    add_custom_command(TARGET EasyTools POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/ui/dist"
            "$<TARGET_FILE_DIR:EasyTools>/ui"
        COMMENT "Copying WebView2 UI assets..."
    )
endif()

# ── 安装规则 ──────────────────────────────────────────────────────────────────
install(TARGETS EasyTools RUNTIME DESTINATION bin)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources/" DESTINATION bin/resources)
