// ─────────────────────────────────────────────────────────────────────────────
// I18nLogCatalog.cpp — 世界级 0 内存分配日志多语言模板翻译映射中枢实现
// ─────────────────────────────────────────────────────────────────────────────

#include "core/logger/I18nLogCatalog.h"
#include "core/logger/Logger.h"

#include <unordered_map>
#include <string>

namespace easy::core {

struct LogEntry {
    const char* zhWithTrace;
    const char* enWithTrace;
};

static const std::unordered_map<std::string_view, LogEntry>& getCatalog() {
    static const std::unordered_map<std::string_view, LogEntry> catalog = {
        // ── 基础与生命周期 ──
        {"日志系统初始化完成, 日志目录={}", {
            "[{}] 日志系统初始化完成, 日志目录={}",
            "[{}] Logging system initialized, log directory={}"
        }},
        {"日志系统正在关闭...", {
            "[{}] 日志系统正在关闭...",
            "[{}] Logging system is shutting down..."
        }},
        {"EasyTools v{} 启动", {
            "[{}] EasyTools v{} 启动",
            "[{}] EasyTools v{} started"
        }},
        {"配置管理器初始化, 配置文件路径={}", {
            "[{}] 配置管理器初始化, 配置文件路径={}",
            "[{}] ConfigManager initialized, config path={}"
        }},
        {"配置管理器正在关闭...", {
            "[{}] 配置管理器正在关闭...",
            "[{}] ConfigManager is shutting down..."
        }},
        {"配置文件不存在, 将使用默认配置并创建文件: {}", {
            "[{}] 配置文件不存在, 将使用默认配置并创建文件: {}",
            "[{}] Config file does not exist, creating default config: {}"
        }},
        {"配置文件加载成功, 键数量={}", {
            "[{}] 配置文件加载成功, 键数量={}",
            "[{}] Configuration loaded successfully, key count={}"
        }},
        {"配置文件解析失败: {}, 保留最后一次有效配置", {
            "[{}] 配置文件解析失败: {}, 保留最后一次有效配置",
            "[{}] Config parse failed: {}, retaining last valid config"
        }},
        {"配置文件解析失败: {}, 首次启动使用默认配置", {
            "[{}] 配置文件解析失败: {}, 首次启动使用默认配置",
            "[{}] Config parse failed: {}, fallback to default config"
        }},
        {"配置文件监控已启动: {}", {
            "[{}] 配置文件监控已启动: {}",
            "[{}] Config file watcher started: {}"
        }},
        {"配置文件监控已停止", {
            "[{}] 配置文件监控已停止",
            "[{}] Config file watcher stopped"
        }},
        {"配置管理器初始化失败，应用无法安全启动", {
            "[{}] 配置管理器初始化失败，应用无法安全启动",
            "[{}] ConfigManager initialization failed, application cannot start safely"
        }},
        {"已按设置拉起管理员实例, 当前进程退出", {
            "[{}] 已按设置拉起管理员实例, 当前进程退出",
            "[{}] Elevated admin instance launched per settings, exiting current process"
        }},
        {"PerformanceMonitor: 启动, 采样间隔={}ms", {
            "[{}] PerformanceMonitor: 启动, 采样间隔={}ms",
            "[{}] PerformanceMonitor: started, interval={}ms"
        }},
        {"PerformanceMonitor: 已停止", {
            "[{}] PerformanceMonitor: 已停止",
            "[{}] PerformanceMonitor: stopped"
        }},
        {"系统托盘图标已创建并显示 (cbSize={})", {
            "[{}] 系统托盘图标已创建并显示 (cbSize={})",
            "[{}] System tray icon created and shown (cbSize={})"
        }},
        {"按键统计管理器初始化成功, 日期={}", {
            "[{}] 按键统计管理器初始化成功, 日期={}",
            "[{}] Keycast stats manager initialized, date={}"
        }},
        {"成功从安装包初始配置同步模块开关并写入当前用户配置", {
            "[{}] 成功从安装包初始配置同步模块开关并写入当前用户配置",
            "[{}] Synchronized module flags from initial setup config into user config"
        }},
        {"配置已从 JSON 字符串批量更新", {
            "[{}] 配置已从 JSON 字符串批量更新",
            "[{}] Configuration batch updated from JSON string"
        }},
        {"配置已重置", {
            "[{}] 配置已重置",
            "[{}] Configuration reset"
        }},
        {"配置已导出到: {}", {
            "[{}] 配置已导出到: {}",
            "[{}] Configuration exported to: {}"
        }},

        // ── 插件系统 ──
        {"开始扫描插件目录: {}", {
            "[{}] 开始扫描插件目录: {}",
            "[{}] Scanning plugin directory: {}"
        }},
        {"发现插件 DLL: {}", {
            "[{}] 发现插件 DLL: {}",
            "[{}] Discovered plugin DLL: {}"
        }},
        {"插件已成功加载: {}", {
            "[{}] 插件已成功加载: {}",
            "[{}] Plugin loaded successfully: {}"
        }},
        {"插件已卸载: {}", {
            "[{}] 插件已卸载: {}",
            "[{}] Plugin unloaded: {}"
        }},

        // ── 快捷键与钩子 ──
        {"快捷键管理器已初始化", {
            "[{}] 快捷键管理器已初始化",
            "[{}] Hotkey manager initialized"
        }},
        {"快捷键管理器已关闭", {
            "[{}] 快捷键管理器已关闭",
            "[{}] Hotkey manager closed"
        }},
        {"注册快捷键成功: {}", {
            "[{}] 注册快捷键成功: {}",
            "[{}] Hotkey registered successfully: {}"
        }},
        {"注销快捷键成功: id={}", {
            "[{}] 注销快捷键成功: id={}",
            "[{}] Hotkey unregistered successfully: id={}"
        }},
        {"全局核心鼠标钩子安装成功", {
            "[{}] 全局核心鼠标钩子安装成功",
            "[{}] Global mouse hook installed successfully"
        }},
        {"全局核心键盘钩子安装成功", {
            "[{}] 全局核心键盘钩子安装成功",
            "[{}] Global keyboard hook installed successfully"
        }},
        {"内置核心 IPC 处理器注册完成 (含性能监控、配置管理、系统信息、窗口枚举)", {
            "[{}] 内置核心 IPC 处理器注册完成 (含性能监控、配置管理、系统信息、窗口枚举)",
            "[{}] Core IPC handlers registered (performance, config, system, window enumeration)"
        }},
        {"鼠标演示与特效 Overlay 初始化完成 (Taskbar Safe)", {
            "[{}] 鼠标演示与特效 Overlay 初始化完成 (Taskbar Safe)",
            "[{}] Mouse demonstration & overlay initialized (Taskbar Safe)"
        }},

        // ── 截图与贴图 ──
        {"截图引擎已初始化", {
            "[{}] 截图引擎已初始化",
            "[{}] Screen capture engine initialized"
        }},
        {"截图引擎已关闭", {
            "[{}] 截图引擎已关闭",
            "[{}] Screen capture engine closed"
        }},
        {"截图完成: {}x{}", {
            "[{}] 截图完成: {}x{}",
            "[{}] Screenshot completed: {}x{}"
        }},
        {"截图完成: {}x{}, format={}", {
            "[{}] 截图完成: {}x{}, format={}",
            "[{}] Screenshot completed: {}x{}, format={}"
        }},
        {"截图已保存: path={}, size={}KB", {
            "[{}] 截图已保存: path={}, size={}KB",
            "[{}] Screenshot saved: path={}, size={}KB"
        }},
        {"执行全屏截图", {
            "[{}] 执行全屏截图",
            "[{}] Executing fullscreen capture"
        }},
        {"截取窗口: hwnd={}, region=({},{})x{}x{}", {
            "[{}] 截取窗口: hwnd={}, region=({},{})x{}x{}",
            "[{}] Capturing window: hwnd={}, region=({},{})x{}x{}"
        }},
        {"前台处于全屏独占应用，自动免打扰跳过截图: hwnd=0x{:X}", {
            "[{}] 前台处于全屏独占应用，自动免打扰跳过截图: hwnd=0x{:X}",
            "[{}] Foreground is exclusive fullscreen, skipping capture (Do Not Disturb): hwnd=0x{:X}"
        }},
        {"OCR 提取完成, 行数={}", {
            "[{}] OCR 提取完成, 行数={}",
            "[{}] OCR text extracted, lines={}"
        }},
        {"长截图已启动: mode={}, scrollDelay={}ms, maxFrames={}", {
            "[{}] 长截图已启动: mode={}, scrollDelay={}ms, maxFrames={}",
            "[{}] Scroll capture started: mode={}, scrollDelay={}ms, maxFrames={}"
        }},
        {"长截图拼接完成: {}x{}, 帧数={}", {
            "[{}] 长截图拼接完成: {}x{}, 帧数={}",
            "[{}] Scroll capture stitched: {}x{}, frames={}"
        }},
        {"长截图已保存，共 {} 帧", {
            "[{}] 长截图已保存，共 {} 帧",
            "[{}] Scroll capture saved, total frames={}"
        }},
        {"贴图窗口已创建: {}x{} @ ({},{}), 总数={}", {
            "[{}] 贴图窗口已创建: {}x{} @ ({},{}), 总数={}",
            "[{}] Pin window created: {}x{} @ ({},{}), count={}"
        }},
        {"所有贴图窗口已关闭并释放资源", {
            "[{}] 所有贴图窗口已关闭并释放资源",
            "[{}] All pin windows closed and resources released"
        }},
        {"所有贴图已{}", {
            "[{}] 所有贴图已{}",
            "[{}] All pinned images {}"
        }},
        {"已整理 {} 张贴图", {
            "[{}] 已整理 {} 张贴图",
            "[{}] Arranged {} pinned images"
        }},
        {"贴图鼠标穿透: {}", {
            "[{}] 贴图鼠标穿透: {}",
            "[{}] Pin click-through: {}"
        }},
        {"贴图已保存: {}", {
            "[{}] 贴图已保存: {}",
            "[{}] Pinned image saved: {}"
        }},
        {"剪贴板贴图：剪贴板无可贴内容", {
            "[{}] 剪贴板贴图：剪贴板无可贴内容",
            "[{}] Clipboard pin: No image found in clipboard"
        }},
        {"剪贴板贴图：{}x{} @ ({},{})", {
            "[{}] 剪贴板贴图：{}x{} @ ({},{})",
            "[{}] Clipboard pin: {}x{} @ ({},{})"
        }},
        {"CapturePlugin: 初始化截图/录屏引擎", {
            "[{}] CapturePlugin: 初始化截图/录屏引擎",
            "[{}] CapturePlugin: Initializing capture & recorder engine"
        }},
        {"CapturePlugin: 卸载截图/录屏引擎", {
            "[{}] CapturePlugin: 卸载截图/录屏引擎",
            "[{}] CapturePlugin: Unloading capture & recorder engine"
        }},
        {"录制指示器已显示", {
            "[{}] 录制指示器已显示",
            "[{}] Recording indicator shown"
        }},

        // ── 录屏引擎 ──
        {"屏幕录制引擎已初始化", {
            "[{}] 屏幕录制引擎已初始化",
            "[{}] Screen recording engine initialized"
        }},
        {"屏幕录制引擎已关闭", {
            "[{}] 屏幕录制引擎已关闭",
            "[{}] Screen recording engine closed"
        }},
        {"屏幕录制已准备: {}x{} @ {}fps, countdown={}s, format={}, output={}", {
            "[{}] 屏幕录制已准备: {}x{} @ {}fps, countdown={}s, format={}, output={}",
            "[{}] Screen recording prepared: {}x{} @ {}fps, countdown={}s, format={}, output={}"
        }},
        {"屏幕录制已暂停, frames={}", {
            "[{}] 屏幕录制已暂停, frames={}",
            "[{}] Screen recording paused, frames={}"
        }},
        {"屏幕录制已恢复", {
            "[{}] 屏幕录制已恢复",
            "[{}] Screen recording resumed"
        }},
        {"屏幕录制已停止: frames={}, dropped={}, duration={:.1f}s, output={}", {
            "[{}] 屏幕录制已停止: frames={}, dropped={}, duration={:.1f}s, output={}",
            "[{}] Screen recording stopped: frames={}, dropped={}, duration={:.1f}s, output={}"
        }},
        {"录屏音频已启用: codec={}, rate={}, channels={}", {
            "[{}] 录屏音频已启用: codec={}, rate={}, channels={}",
            "[{}] Screen recording audio enabled: codec={}, rate={}, channels={}"
        }},
        {"录屏捕获后端已切换: {} -> {}", {
            "[{}] 录屏捕获后端已切换: {} -> {}",
            "[{}] Capture backend switched: {} -> {}"
        }},
        {"录屏捕获后端已选择: backend={}, accelerated={}", {
            "[{}] 录屏捕获后端已选择: backend={}, accelerated={}",
            "[{}] Capture backend selected: backend={}, accelerated={}"
        }},
        {"录屏负载已恢复，自适应帧步长调整为 {}", {
            "[{}] 录屏负载已恢复，自适应帧步长调整为 {}",
            "[{}] Recording load recovered, adaptive frame step adjusted to {}"
        }},

        // ── IPC 与窗口交互 ──
        {"收到前端消息: id={}, method={}", {
            "[{}] 收到前端消息: id={}, method={}",
            "[{}] Received frontend IPC message: id={}, method={}"
        }},
        {"IPC 响应完成: method={}, id={}, 耗时={}", {
            "[{}] IPC 响应完成: method={}, id={}, 耗时={}",
            "[{}] IPC response completed: method={}, id={}, duration={}"
        }},
        {"托盘图标被点击: {}", {
            "[{}] 托盘图标被点击: {}",
            "[{}] Tray icon clicked: {}"
        }},
        {"托盘窗口已创建: hwnd=0x{:X}", {
            "[{}] 托盘窗口已创建: hwnd=0x{:X}",
            "[{}] Tray window created: hwnd=0x{:X}"
        }},
        {"托盘窗口已显示: x={}, y={}", {
            "[{}] 托盘窗口已显示: x={}, y={}",
            "[{}] Tray window shown: x={}, y={}"
        }},
        {"托盘窗口已隐藏", {
            "[{}] 托盘窗口已隐藏",
            "[{}] Tray window hidden"
        }},
        {"设置窗口已创建: hwnd=0x{:X}", {
            "[{}] 设置窗口已创建: hwnd=0x{:X}",
            "[{}] Settings window created: hwnd=0x{:X}"
        }},
        {"设置窗口已显示", {
            "[{}] 设置窗口已显示",
            "[{}] Settings window shown"
        }},
        {"设置窗口已隐藏", {
            "[{}] 设置窗口已隐藏",
            "[{}] Settings window hidden"
        }},
        {"搜索窗口已创建: hwnd=0x{:X}", {
            "[{}] 搜索窗口已创建: hwnd=0x{:X}",
            "[{}] Search window created: hwnd=0x{:X}"
        }},
        {"搜索窗口已显示", {
            "[{}] 搜索窗口已显示",
            "[{}] Search window shown"
        }},
        {"搜索窗口已隐藏", {
            "[{}] 搜索窗口已隐藏",
            "[{}] Search window hidden"
        }}
    };
    return catalog;
}

const char* getLocalizedLogFormatWithTrace(std::string_view rawFmt, LogLanguage lang) {
    const auto& catalog = getCatalog();
    auto it = catalog.find(rawFmt);
    if (it != catalog.end()) {
        if (lang == LogLanguage::EnUS) {
            return it->second.enWithTrace;
        } else {
            return it->second.zhWithTrace;
        }
    }
    return nullptr;
}

}  // namespace easy::core
