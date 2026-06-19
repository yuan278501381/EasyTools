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

# ── 收集源文件 ────────────────────────────────────────────────────────────────
file(GLOB_RECURSE EASYTOOLS_SOURCES
    "src/*.cpp"
    "src/*.h"
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

    # Windows SDK
    d2d1 dwrite d3d11 dxgi
    dwmapi mfplat mfreadwrite mfuuid
    shlwapi shell32 user32 gdi32
    ole32 oleaut32 advapi32 comctl32 dbghelp
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
