// ─────────────────────────────────────────────────────────────────────────────
// I18nLogCatalog.cpp — 世界级 0 内存分配日志多语言模板翻译映射中枢实现 (全量自动对齐)
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
        {"EasyTools v{} 启动", {
            "[{}] EasyTools v{} 启动",
            "[{}] EasyTools v{} started"
        }},
        {"按键回显初始化失败: 无法创建渲染表面", {
            "[{}] 按键回显初始化失败: 无法创建渲染表面",
            "[{}] Keycast overlay initialization failed: unable to create render surface"
        }},
        {"按键统计管理器初始化成功, 日期={}", {
            "[{}] 按键统计管理器初始化成功, 日期={}",
            "[{}] Keycast stats manager initialized, date={}"
        }},
        {"按键统计管理器已关闭", {
            "[{}] 按键统计管理器已关闭",
            "[{}] Keycast stats manager closed"
        }},
        {"损坏配置备份失败: {}", {
            "[{}] 损坏配置备份失败: {}",
            "[{}] Failed to backup corrupted config: {}"
        }},
        {"损坏配置已保留到: {}", {
            "[{}] 损坏配置已保留到: {}",
            "[{}] Corrupted config preserved at: {}"
        }},
        {"推送事件到前端: event={}", {
            "[{}] 推送事件到前端: event={}",
            "[{}] Push event to frontend: event={}"
        }},
        {"插件 ABI 校验失败: {}, manifest={}, binary={}", {
            "[{}] 插件 ABI 校验失败: {}, manifest={}, binary={}",
            "[{}] Plugin ABI verification failed: {}, manifest={}, binary={}"
        }},
        {"插件 DLL 未导出 ABI 握手函数: {}", {
            "[{}] 插件 DLL 未导出 ABI 握手函数: {}",
            "[{}] Plugin DLL does not export ABI handshake function: {}"
        }},
        {"插件 DLL 未导出 CreatePlugin: {}", {
            "[{}] 插件 DLL 未导出 CreatePlugin: {}",
            "[{}] Plugin DLL does not export CreatePlugin: {}"
        }},
        {"插件 ID 重复，已拒绝加载: {}", {
            "[{}] 插件 ID 重复，已拒绝加载: {}",
            "[{}] Duplicate plugin ID, load rejected: {}"
        }},
        {"插件元数据异常: {}, error={}", {
            "[{}] 插件元数据异常: {}, error={}",
            "[{}] Plugin metadata error: {}, error={}"
        }},
        {"插件元数据未知异常: {}", {
            "[{}] 插件元数据未知异常: {}",
            "[{}] Unknown exception reading plugin metadata: {}"
        }},
        {"插件关闭异常: {}, error={}", {
            "[{}] 插件关闭异常: {}, error={}",
            "[{}] Plugin shutdown exception: {}, error={}"
        }},
        {"插件关闭未知异常: {}", {
            "[{}] 插件关闭未知异常: {}",
            "[{}] Unknown exception shutting down plugin: {}"
        }},
        {"插件初始化失败: {}", {
            "[{}] 插件初始化失败: {}",
            "[{}] Plugin initialization failed: {}"
        }},
        {"插件初始化异常: {}, error={}", {
            "[{}] 插件初始化异常: {}, error={}",
            "[{}] Plugin initialization exception: {}, error={}"
        }},
        {"插件初始化未知异常: {}", {
            "[{}] 插件初始化未知异常: {}",
            "[{}] Unknown exception initializing plugin: {}"
        }},
        {"插件名称重复，已拒绝加载: {}", {
            "[{}] 插件名称重复，已拒绝加载: {}",
            "[{}] Duplicate plugin name, load rejected: {}"
        }},
        {"插件已加载，拒绝重复扫描", {
            "[{}] 插件已加载，拒绝重复扫描",
            "[{}] Plugins already loaded, skipping duplicate scan"
        }},
        {"插件已成功加载: {} (v{})", {
            "[{}] 插件已成功加载: {} (v{})",
            "[{}] Plugin loaded successfully: {} (v{})"
        }},
        {"插件已成功加载: {}", {
            "[{}] 插件已成功加载: {}",
            "[{}] Plugin loaded successfully: {}"
        }},
        {"插件已禁用，完全跳过 DLL 加载: {}", {
            "[{}] 插件已禁用，完全跳过 DLL 加载: {}",
            "[{}] Plugin disabled, skipping DLL load: {}"
        }},
        {"插件清单校验失败: {}, error={}", {
            "[{}] 插件清单校验失败: {}, error={}",
            "[{}] Plugin manifest validation failed: {}, error={}"
        }},
        {"插件目录不存在: {}", {
            "[{}] 插件目录不存在: {}",
            "[{}] Plugin directory does not exist: {}"
        }},
        {"收到前端消息: id={}, method={}", {
            "[{}] 收到前端消息: id={}, method={}",
            "[{}] Received frontend IPC message: id={}, method={}"
        }},
        {"收到退出消息，准备清理", {
            "[{}] 收到退出消息，准备清理",
            "[{}] Received exit message, preparing cleanup"
        }},
        {"无外部目标窗口，放弃按键注入: keys={}, fg={}", {
            "[{}] 无外部目标窗口，放弃按键注入: keys={}, fg={}",
            "[{}] No external target window, aborting key injection: keys={}, fg={}"
        }},
        {"无手势, 补发{}键点击以还原正常操作", {
            "[{}] 无手势, 补发{}键点击以还原正常操作",
            "[{}] No gesture recognized, dispatching {} click to restore normal action"
        }},
        {"无效的虚拟屏幕尺寸: {}x{}", {
            "[{}] 无效的虚拟屏幕尺寸: {}x{}",
            "[{}] Invalid virtual screen size: {}x{}"
        }},
        {"无法从录屏中排除快捷键提示层: error={}", {
            "[{}] 无法从录屏中排除快捷键提示层: error={}",
            "[{}] Failed to exclude shortcut hint overlay from recording: error={}"
        }},
        {"无法创建录屏输出目录: path={}, error={}", {
            "[{}] 无法创建录屏输出目录: path={}, error={}",
            "[{}] Failed to create recording output directory: path={}, error={}"
        }},
        {"无法创建消息窗口", {
            "[{}] 无法创建消息窗口",
            "[{}] Failed to create message window"
        }},
        {"无法创建配置文件监控事件: error={}", {
            "[{}] 无法创建配置文件监控事件: error={}",
            "[{}] Failed to create config file watch event: error={}"
        }},
        {"无法创建配置监控停止事件: error={}", {
            "[{}] 无法创建配置监控停止事件: error={}",
            "[{}] Failed to create config watch stop event: error={}"
        }},
        {"无法创建配置目录: path={}, error={}", {
            "[{}] 无法创建配置目录: path={}, error={}",
            "[{}] Failed to create config directory: path={}, error={}"
        }},
        {"无法删除长截图临时文件: {}", {
            "[{}] 无法删除长截图临时文件: {}",
            "[{}] Failed to delete scroll capture temporary file: {}"
        }},
        {"无法加载插件 DLL: {}, error={}", {
            "[{}] 无法加载插件 DLL: {}, error={}",
            "[{}] Failed to load plugin DLL: {}, error={}"
        }},
        {"无法启动截图: 桌面底图上传失败", {
            "[{}] 无法启动截图: 桌面底图上传失败",
            "[{}] Failed to start screenshot: desktop backdrop upload failed"
        }},
        {"无法启动截图: 桌面底图捕获或上传失败", {
            "[{}] 无法启动截图: 桌面底图捕获或上传失败",
            "[{}] Failed to start screenshot: backdrop capture or upload failed"
        }},
        {"无法启动截图: 覆盖层窗口初始化失败", {
            "[{}] 无法启动截图: 覆盖层窗口初始化失败",
            "[{}] Failed to start screenshot: overlay window initialization failed"
        }},
        {"无法启动贴图编辑: 覆盖层窗口初始化失败", {
            "[{}] 无法启动贴图编辑: 覆盖层窗口初始化失败",
            "[{}] Failed to start pin edit: overlay window initialization failed"
        }},
        {"无法启动配置监控线程: {}", {
            "[{}] 无法启动配置监控线程: {}",
            "[{}] Failed to start config watcher thread: {}"
        }},
        {"无法在主线程安装前台窗口监视", {
            "[{}] 无法在主线程安装前台窗口监视",
            "[{}] Failed to install foreground window monitor on main thread"
        }},
        {"无法打开剪贴板, error={}", {
            "[{}] 无法打开剪贴板, error={}",
            "[{}] Failed to open clipboard, error={}"
        }},
        {"无法打开文件: {}", {
            "[{}] 无法打开文件: {}",
            "[{}] Failed to open file: {}"
        }},
        {"无法打开配置导出临时文件: {}", {
            "[{}] 无法打开配置导出临时文件: {}",
            "[{}] Failed to open config export temp file: {}"
        }},
        {"无法捕获屏幕底图用于贴图编辑", {
            "[{}] 无法捕获屏幕底图用于贴图编辑",
            "[{}] Failed to capture screen backdrop for pin edit"
        }},
        {"无法查询录屏目录剩余空间: path={}, error={}", {
            "[{}] 无法查询录屏目录剩余空间: path={}, error={}",
            "[{}] Failed to query recording directory free space: path={}, error={}"
        }},
        {"无法监控配置文件目录: {}", {
            "[{}] 无法监控配置文件目录: {}",
            "[{}] Failed to watch config directory: {}"
        }},
        {"无法监视前台窗口，高完整性窗口将不会提示手势不可用", {
            "[{}] 无法监视前台窗口，高完整性窗口将不会提示手势不可用",
            "[{}] Unable to monitor foreground window, high integrity windows will not prompt gesture status"
        }},
        {"日志系统初始化完成, 日志目录={}", {
            "[{}] 日志系统初始化完成, 日志目录={}",
            "[{}] Logging system initialized, log directory={}"
        }},
        {"日志系统正在关闭...", {
            "[{}] 日志系统正在关闭...",
            "[{}] Logging system is shutting down..."
        }},
        {"更新响应解析失败: {}", {
            "[{}] 更新响应解析失败: {}",
            "[{}] Failed to parse update response: {}"
        }},
        {"更新检查完成: {}", {
            "[{}] 更新检查完成: {}",
            "[{}] Update check completed: {}"
        }},
        {"未找到手势映射: fullCode={}, bareCode={}", {
            "[{}] 未找到手势映射: fullCode={}, bareCode={}",
            "[{}] Gesture mapping not found: fullCode={}, bareCode={}"
        }},
        {"未找到本地 UI 文件及动态端口文件, 尝试连接默认开发服务器 http://localhost:5173", {
            "[{}] 未找到本地 UI 文件及动态端口文件, 尝试连接默认开发服务器 http://localhost:5173",
            "[{}] Local UI files not found, connecting to dev server http://localhost:5173"
        }},
        {"未找到编码器: format={}", {
            "[{}] 未找到编码器: format={}",
            "[{}] Encoder not found for format: format={}"
        }},
        {"未检测到 OCR 语言包，OCR 功能不可用。可在系统设置中添加语言的“可选功能 → 光学字符识别”", {
            "[{}] 未检测到 OCR 语言包，OCR 功能不可用。可在系统设置中添加语言的“可选功能 → 光学字符识别”",
            "[{}] No OCR language pack detected, OCR unavailable. Install OCR pack in Windows Settings -> Optional Features"
        }},
        {"未知的 IPC 方法: {}", {
            "[{}] 未知的 IPC 方法: {}",
            "[{}] Unknown IPC method: {}"
        }},
        {"未能激活目标窗口，仅保留已投递的关闭消息: target={}", {
            "[{}] 未能激活目标窗口，仅保留已投递的关闭消息: target={}",
            "[{}] Failed to activate target window, keeping dispatched close message: target={}"
        }},
        {"未能激活目标窗口，放弃注入: keys={}, target={}, fg={}", {
            "[{}] 未能激活目标窗口，放弃注入: keys={}, target={}, fg={}",
            "[{}] Failed to activate target window, aborting key injection: keys={}, target={}, fg={}"
        }},
        {"标注引擎: 撤销, 剩余元素数={}", {
            "[{}] 标注引擎: 撤销, 剩余元素数={}",
            "[{}] Annotation engine: undo, remaining elements={}"
        }},
        {"标注引擎: 添加智能消除 ({},{})→({},{}) radius={}", {
            "[{}] 标注引擎: 添加智能消除 ({},{})→({},{}) radius={}",
            "[{}] Annotation engine: add smart inpaint ({},{})→({},{}) radius={}"
        }},
        {"标注引擎: 添加水印 ({},{})→({},{}) text='{}' opacity={:.2f} angle={:.1f}", {
            "[{}] 标注引擎: 添加水印 ({},{})→({},{}) text='{}' opacity={:.2f} angle={:.1f}",
            "[{}] Annotation engine: add watermark ({},{})→({},{}) text='{}' opacity={:.2f} angle={:.1f}"
        }},
        {"标注引擎: 添加聚光灯 ({},{})→({},{}) dimAlpha={:.2f} ellipse={}", {
            "[{}] 标注引擎: 添加聚光灯 ({},{})→({},{}) dimAlpha={:.2f} ellipse={}",
            "[{}] Annotation engine: add spotlight ({},{})→({},{}) dimAlpha={:.2f} ellipse={}"
        }},
        {"标注引擎: 渲染智能消除 radius={}", {
            "[{}] 标注引擎: 渲染智能消除 radius={}",
            "[{}] Annotation engine: render smart inpaint radius={}"
        }},
        {"标注引擎: 渲染水印 text='{}' opacity={:.2f} angle={:.1f} spacing={}", {
            "[{}] 标注引擎: 渲染水印 text='{}' opacity={:.2f} angle={:.1f} spacing={}",
            "[{}] Annotation engine: render watermark text='{}' opacity={:.2f} angle={:.1f} spacing={}"
        }},
        {"标注引擎: 渲染聚光灯 dimAlpha={:.2f} ellipse={}", {
            "[{}] 标注引擎: 渲染聚光灯 dimAlpha={:.2f} ellipse={}",
            "[{}] Annotation engine: render spotlight dimAlpha={:.2f} ellipse={}"
        }},
        {"标注引擎: 设置底图 {}x{}", {
            "[{}] 标注引擎: 设置底图 {}x{}",
            "[{}] Annotation engine: set backdrop {}x{}"
        }},
        {"标注引擎: 重做, 剩余元素数={}", {
            "[{}] 标注引擎: 重做, 剩余元素数={}",
            "[{}] Annotation engine: redo, remaining elements={}"
        }},
        {"检测到 Windows 系统会话解锁，恢复正常工作状态", {
            "[{}] 检测到 Windows 系统会话解锁，恢复正常工作状态",
            "[{}] Windows session unlocked, resuming normal operations"
        }},
        {"检测到 Windows 系统会话锁屏或注销 (0x{:X})，主动挂起渲染并释放物理内存", {
            "[{}] 检测到 Windows 系统会话锁屏或注销 (0x{:X})，主动挂起渲染并释放物理内存",
            "[{}] Windows session locked/logged off (0x{:X}), suspending rendering and trimming memory"
        }},
        {"检测到滚动到底部, frame={}", {
            "[{}] 检测到滚动到底部, frame={}",
            "[{}] Detected scroll to bottom, frame={}"
        }},
        {"检测到系统任务栏重建 (TaskbarCreated)，重新注册托盘图标", {
            "[{}] 检测到系统任务栏重建 (TaskbarCreated)，重新注册托盘图标",
            "[{}] Taskbar recreated (TaskbarCreated), re-registering tray icon"
        }},
        {"检测到配置文件变更, 正在热加载...", {
            "[{}] 检测到配置文件变更, 正在热加载...",
            "[{}] Config file change detected, hot reloading..."
        }},
        {"模板匹配: confidence={:.3f}, offset={}", {
            "[{}] 模板匹配: confidence={:.3f}, offset={}",
            "[{}] Template match: confidence={:.3f}, offset={}"
        }},
        {"模板匹配置信度过低: {:.3f}", {
            "[{}] 模板匹配置信度过低: {:.3f}",
            "[{}] Template match confidence too low: {:.3f}"
        }},
        {"注册 IPC 处理器: method={}", {
            "[{}] 注册 IPC 处理器: method={}",
            "[{}] Register IPC handler: method={}"
        }},
        {"注册快捷键失败: name={}, def={}, error={}", {
            "[{}] 注册快捷键失败: name={}, def={}, error={}",
            "[{}] Failed to register hotkey: name={}, def={}, error={}"
        }},
        {"注册快捷键成功: name={}, def={}, id={}", {
            "[{}] 注册快捷键成功: name={}, def={}, id={}",
            "[{}] Hotkey registered successfully: name={}, def={}, id={}"
        }},
        {"注销 IPC 命名空间: prefix={}, count={}", {
            "[{}] 注销 IPC 命名空间: prefix={}, count={}",
            "[{}] Unregister IPC namespace: prefix={}, count={}"
        }},
        {"注销快捷键: name={}, def={}", {
            "[{}] 注销快捷键: name={}, def={}",
            "[{}] Unregister hotkey: name={}, def={}"
        }},
        {"消息循环失败, error={}", {
            "[{}] 消息循环失败, error={}",
            "[{}] Message loop failure, error={}"
        }},
        {"添加作用域规则: id={}, name={}, process={}, class={}", {
            "[{}] 添加作用域规则: id={}, name={}, process={}, class={}",
            "[{}] Add scope rule: id={}, name={}, process={}, class={}"
        }},
        {"添加手势映射: profile={}, code={}, action={}", {
            "[{}] 添加手势映射: profile={}, code={}, action={}",
            "[{}] Add gesture mapping: profile={}, code={}, action={}"
        }},
        {"用户已明确授权脚本 {} 权限", {
            "[{}] 用户已明确授权脚本 {} 权限",
            "[{}] User explicitly granted permission to script {}"
        }},
        {"用户拒绝授予脚本 {} 敏感权限，终止执行", {
            "[{}] 用户拒绝授予脚本 {} 敏感权限，终止执行",
            "[{}] User denied sensitive permission to script {}, execution aborted"
        }},
        {"用户按 Esc 结束长截图, frame={}", {
            "[{}] 用户按 Esc 结束长截图, frame={}",
            "[{}] User pressed Esc to finish scroll capture, frame={}"
        }},
        {"目标窗口完整性更高，关闭可能被 UIPI 拦截: {}", {
            "[{}] 目标窗口完整性更高，关闭可能被 UIPI 拦截: {}",
            "[{}] Target window has higher integrity, close may be blocked by UIPI: {}"
        }},
        {"目标窗口完整性更高，按键注入可能被 UIPI 拦截: keys={}, target={}", {
            "[{}] 目标窗口完整性更高，按键注入可能被 UIPI 拦截: keys={}, target={}",
            "[{}] Target window has higher integrity, key injection may be blocked by UIPI: keys={}, target={}"
        }},
        {"程序启动完成，进入消息循环", {
            "[{}] 程序启动完成，进入消息循环",
            "[{}] Application startup complete, entering message loop"
        }},
        {"窗口在手势黑名单, 不拦截: process='{}', class='{}'", {
            "[{}] 窗口在手势黑名单, 不拦截: process='{}', class='{}'",
            "[{}] Window in gesture blacklist, skipping: process='{}', class='{}'"
        }},
        {"窗口置顶切换: {} -> {}", {
            "[{}] 窗口置顶切换: {} -> {}",
            "[{}] Window top-most toggled: {} -> {}"
        }},
        {"窗口透明度切换: 关闭 (不透明)", {
            "[{}] 窗口透明度切换: 关闭 (不透明)",
            "[{}] Window opacity toggled: Off (Opaque)"
        }},
        {"窗口透明度切换: 开启 (70%)", {
            "[{}] 窗口透明度切换: 开启 (70%)",
            "[{}] Window opacity toggled: On (70%)"
        }},
        {"系统托盘图标已成功创建并显示 (cbSize={})", {
            "[{}] 系统托盘图标已成功创建并显示 (cbSize={})",
            "[{}] System tray icon created and shown (cbSize={})"
        }},
        {"系统托盘图标已销毁", {
            "[{}] 系统托盘图标已销毁",
            "[{}] System tray icon destroyed"
        }},
        {"系统通知 Overlay 初始化完成 (Taskbar Safe)", {
            "[{}] 系统通知 Overlay 初始化完成 (Taskbar Safe)",
            "[{}] System notification overlay initialized (Taskbar Safe)"
        }},
        {"编码器不可用: codec={}, pixelFormat={}, error={} ({})", {
            "[{}] 编码器不可用: codec={}, pixelFormat={}, error={} ({})",
            "[{}] Encoder unavailable: codec={}, pixelFormat={}, error={} ({})"
        }},
        {"缩小模板匹配置信度过低: {:.3f}", {
            "[{}] 缩小模板匹配置信度过低: {:.3f}",
            "[{}] Scaled template match confidence too low: {:.3f}"
        }},
        {"脚本 {} 未经用户预授权，已自动限制在安全只读沙箱执行", {
            "[{}] 脚本 {} 未经用户预授权，已自动限制在安全只读沙箱执行",
            "[{}] Script {} not pre-authorized, executing in safe read-only sandbox"
        }},
        {"自动提权被取消或失败，以普通权限继续运行", {
            "[{}] 自动提权被取消或失败，以普通权限继续运行",
            "[{}] Elevation canceled or failed, continuing as standard user"
        }},
        {"获取屏幕 DC 失败, error={}", {
            "[{}] 获取屏幕 DC 失败, error={}",
            "[{}] Failed to get screen DC, error={}"
        }},
        {"补发{}键点击失败: sent={}, error={}", {
            "[{}] 补发{}键点击失败: sent={}, error={}",
            "[{}] Failed to replay {} key click: sent={}, error={}"
        }},
        {"覆盖手势映射: profile={}, code={}, action={}", {
            "[{}] 覆盖手势映射: profile={}, code={}, action={}",
            "[{}] Override gesture mapping: profile={}, code={}, action={}"
        }},
        {"解析或应用 initial_modules.json 失败: {}", {
            "[{}] 解析或应用 initial_modules.json 失败: {}",
            "[{}] Failed to parse or apply initial_modules.json: {}"
        }},
        {"触发键按下: trigger={}, pos=({},{}), hwnd=0x{:X}", {
            "[{}] 触发键按下: trigger={}, pos=({},{}), hwnd=0x{:X}",
            "[{}] Trigger key down: trigger={}, pos=({},{}), hwnd=0x{:X}"
        }},
        {"设置手势状态: profile={}, code={}, enabled={}", {
            "[{}] 设置手势状态: profile={}, code={}, enabled={}",
            "[{}] Set gesture state: profile={}, code={}, enabled={}"
        }},
        {"设置手势配置集: name={}, 手势数={}", {
            "[{}] 设置手势配置集: name={}, 手势数={}",
            "[{}] Set gesture profile: name={}, gesturesCount={}"
        }},
        {"设置窗口后台静默预热失败", {
            "[{}] 设置窗口后台静默预热失败",
            "[{}] Settings window background warmup failed"
        }},
        {"设置窗口后台静默预热完成", {
            "[{}] 设置窗口后台静默预热完成",
            "[{}] Settings window background warmup completed"
        }},
        {"设置窗口已创建并显示", {
            "[{}] 设置窗口已创建并显示",
            "[{}] Settings window created and shown"
        }},
        {"设置窗口已激活（复用已有窗口）", {
            "[{}] 设置窗口已激活（复用已有窗口）",
            "[{}] Settings window activated (reused existing)"
        }},
        {"设置窗口已销毁", {
            "[{}] 设置窗口已销毁",
            "[{}] Settings window destroyed"
        }},
        {"设置窗口已隐藏", {
            "[{}] 设置窗口已隐藏",
            "[{}] Settings window hidden"
        }},
        {"设置触发方式状态: profile={}, trigger={}, state={}", {
            "[{}] 设置触发方式状态: profile={}, trigger={}, state={}",
            "[{}] Set trigger state: profile={}, trigger={}, state={}"
        }},
        {"设置配置项失败: key={}, error={}", {
            "[{}] 设置配置项失败: key={}, error={}",
            "[{}] Failed to set config item: key={}, error={}"
        }},
        {"请求的编码器在当前系统不可用，已回退兼容编码器: codec={}", {
            "[{}] 请求的编码器在当前系统不可用，已回退兼容编码器: codec={}",
            "[{}] Requested encoder unavailable, fallback to compatible encoder: codec={}"
        }},
        {"读取配置目录变更失败: error={}", {
            "[{}] 读取配置目录变更失败: error={}",
            "[{}] Failed to read config directory changes: error={}"
        }},
        {"贴图保存失败: {}", {
            "[{}] 贴图保存失败: {}",
            "[{}] Pin save failed: {}"
        }},
        {"贴图保存对话框创建失败: hr=0x{:08X}", {
            "[{}] 贴图保存对话框创建失败: hr=0x{:08X}",
            "[{}] Pin save dialog creation failed: hr=0x{:08X}"
        }},
        {"贴图保存对话框失败: hr=0x{:08X}", {
            "[{}] 贴图保存对话框失败: hr=0x{:08X}",
            "[{}] Pin save dialog failed: hr=0x{:08X}"
        }},
        {"贴图已保存: {}", {
            "[{}] 贴图已保存: {}",
            "[{}] Pinned image saved: {}"
        }},
        {"贴图文件加载失败: {}", {
            "[{}] 贴图文件加载失败: {}",
            "[{}] Failed to load pin image file: {}"
        }},
        {"贴图窗口创建失败", {
            "[{}] 贴图窗口创建失败",
            "[{}] Failed to create pin window"
        }},
        {"贴图窗口已创建: {}x{} @ ({},{}), 总数={}", {
            "[{}] 贴图窗口已创建: {}x{} @ ({},{}), 总数={}",
            "[{}] Pin window created: {}x{} @ ({},{}), count={}"
        }},
        {"贴图窗口渲染资源创建失败", {
            "[{}] 贴图窗口渲染资源创建失败",
            "[{}] Failed to create pin window render resources"
        }},
        {"贴图编辑底图上传失败", {
            "[{}] 贴图编辑底图上传失败",
            "[{}] Pin edit backdrop upload failed"
        }},
        {"贴图鼠标穿透: {}", {
            "[{}] 贴图鼠标穿透: {}",
            "[{}] Pin click-through: {}"
        }},
        {"迁移旧统计文件失败: {}", {
            "[{}] 迁移旧统计文件失败: {}",
            "[{}] Failed to migrate legacy stats file: {}"
        }},
        {"选择截图位图失败, error={}", {
            "[{}] 选择截图位图失败, error={}",
            "[{}] Failed to select screenshot bitmap, error={}"
        }},
        {"配置保存失败: {}", {
            "[{}] 配置保存失败: {}",
            "[{}] Failed to save configuration: {}"
        }},
        {"配置原子硬件刷盘失败: {}", {
            "[{}] 配置原子硬件刷盘失败: {}",
            "[{}] Config hardware atomic flush failed: {}"
        }},
        {"配置变更回调执行异常: callbackId={}, error={}", {
            "[{}] 配置变更回调执行异常: callbackId={}, error={}",
            "[{}] Config change callback exception: callbackId={}, error={}"
        }},
        {"配置合并失败: patch 根节点不是对象", {
            "[{}] 配置合并失败: patch 根节点不是对象",
            "[{}] Config merge failed: patch root is not an object"
        }},
        {"配置合并失败: {}", {
            "[{}] 配置合并失败: {}",
            "[{}] Config merge failed: {}"
        }},
        {"配置导入失败: {}", {
            "[{}] 配置导入失败: {}",
            "[{}] Config import failed: {}"
        }},
        {"配置导出失败: {}", {
            "[{}] 配置导出失败: {}",
            "[{}] Config export failed: {}"
        }},
        {"配置已从 JSON 字符串批量更新", {
            "[{}] 配置已从 JSON 字符串批量更新",
            "[{}] Configuration batch updated from JSON string"
        }},
        {"配置已从文件导入(合并模式): {}", {
            "[{}] 配置已从文件导入(合并模式): {}",
            "[{}] Config imported from file (merge mode): {}"
        }},
        {"配置已导出到: {}", {
            "[{}] 配置已导出到: {}",
            "[{}] Configuration exported to: {}"
        }},
        {"配置已通过硬件原子刷盘保存到文件: {}", {
            "[{}] 配置已通过硬件原子刷盘保存到文件: {}",
            "[{}] Configuration saved to file via atomic flush: {}"
        }},
        {"配置已重置", {
            "[{}] 配置已重置",
            "[{}] Configuration reset"
        }},
        {"配置已重置为默认值", {
            "[{}] 配置已重置为默认值",
            "[{}] Configuration reset to default values"
        }},
        {"配置文件不存在, 将使用默认配置并创建文件: {}", {
            "[{}] 配置文件不存在, 将使用默认配置并创建文件: {}",
            "[{}] Config file does not exist, creating default config: {}"
        }},
        {"配置文件加载成功, 键数量={}", {
            "[{}] 配置文件加载成功, 键数量={}",
            "[{}] Configuration loaded successfully, key count={}"
        }},
        {"配置文件监控已停止", {
            "[{}] 配置文件监控已停止",
            "[{}] Config file watcher stopped"
        }},
        {"配置文件监控已启动: {}", {
            "[{}] 配置文件监控已启动: {}",
            "[{}] Config file watcher started: {}"
        }},
        {"配置文件解析失败: {}, 保留最后一次有效配置", {
            "[{}] 配置文件解析失败: {}, 保留最后一次有效配置",
            "[{}] Config parse failed: {}, retaining last valid config"
        }},
        {"配置文件解析失败: {}, 首次启动使用默认配置", {
            "[{}] 配置文件解析失败: {}, 首次启动使用默认配置",
            "[{}] Config parse failed: {}, fallback to default config"
        }},
        {"配置管理器关闭时保存失败", {
            "[{}] 配置管理器关闭时保存失败",
            "[{}] Failed to save configuration on ConfigManager shutdown"
        }},
        {"配置管理器初始化, 配置文件路径={}", {
            "[{}] 配置管理器初始化, 配置文件路径={}",
            "[{}] ConfigManager initialized, config path={}"
        }},
        {"配置管理器初始化失败，应用无法安全启动", {
            "[{}] 配置管理器初始化失败，应用无法安全启动",
            "[{}] ConfigManager initialization failed, application cannot start safely"
        }},
        {"配置管理器正在关闭...", {
            "[{}] 配置管理器正在关闭...",
            "[{}] ConfigManager is shutting down..."
        }},
        {"重新占用快捷键失败: name={}, def={}, error={}", {
            "[{}] 重新占用快捷键失败: name={}, def={}, error={}",
            "[{}] Failed to reclaim hotkey: name={}, def={}, error={}"
        }},
        {"重绑定快捷键失败: name={}, newDef={}", {
            "[{}] 重绑定快捷键失败: name={}, newDef={}",
            "[{}] Failed to rebind hotkey: name={}, newDef={}"
        }},
        {"钩子安装失败, errorCode={}", {
            "[{}] 钩子安装失败, errorCode={}",
            "[{}] Hook installation failed, errorCode={}"
        }},
        {"长截图失败: {}", {
            "[{}] 长截图失败: {}",
            "[{}] Scroll capture failed: {}"
        }},
        {"长截图已保存，共 {} 帧", {
            "[{}] 长截图已保存，共 {} 帧",
            "[{}] Scroll capture saved, total frames={}"
        }},
        {"长截图已启动: mode={}, scrollDelay={}ms, maxFrames={}", {
            "[{}] 长截图已启动: mode={}, scrollDelay={}ms, maxFrames={}",
            "[{}] Scroll capture started: mode={}, scrollDelay={}ms, maxFrames={}"
        }},
        {"长截图已在运行中", {
            "[{}] 长截图已在运行中",
            "[{}] Scroll capture already in progress"
        }},
        {"长截图帧捕获失败: backend={}, error={}", {
            "[{}] 长截图帧捕获失败: backend={}, error={}",
            "[{}] Scroll capture frame grab failed: backend={}, error={}"
        }},
        {"长截图拼接完成: {}x{}, 帧数={}", {
            "[{}] 长截图拼接完成: {}x{}, 帧数={}",
            "[{}] Scroll capture stitched: {}x{}, frames={}"
        }},
        {"长截图捕获后端初始化失败: {}", {
            "[{}] 长截图捕获后端初始化失败: {}",
            "[{}] Scroll capture backend initialization failed: {}"
        }},
        {"长截图滚轮输入失败: error={}", {
            "[{}] 长截图滚轮输入失败: error={}",
            "[{}] Scroll capture wheel input failed: error={}"
        }},
        {"长截图线程异常, 已停止: {}", {
            "[{}] 长截图线程异常, 已停止: {}",
            "[{}] Scroll capture thread exception, stopped: {}"
        }},
        {"长截图线程未知异常, 已停止", {
            "[{}] 长截图线程未知异常, 已停止",
            "[{}] Scroll capture thread unknown exception, stopped"
        }},
        {"长截图达到字节预算: current={} MiB, next={} MiB, limit={} MiB", {
            "[{}] 长截图达到字节预算: current={} MiB, next={} MiB, limit={} MiB",
            "[{}] Scroll capture reached byte budget: current={} MiB, next={} MiB, limit={} MiB"
        }},
        {"音频编码器不可用, 已降级为仅视频录制", {
            "[{}] 音频编码器不可用, 已降级为仅视频录制",
            "[{}] Audio encoder unavailable, degraded to video-only recording"
        }},
        {"音频编码器初始化失败: codec={}, error={}", {
            "[{}] 音频编码器初始化失败: codec={}, error={}",
            "[{}] Audio encoder initialization failed: codec={}, error={}"
        }},
        {"音频编码失败, 已降级为仅视频录制", {
            "[{}] 音频编码失败, 已降级为仅视频录制",
            "[{}] Audio encoding failed, degraded to video-only recording"
        }},
        {"鼠标手势触发模式已更新: mode={}", {
            "[{}] 鼠标手势触发模式已更新: mode={}",
            "[{}] Mouse gesture trigger mode updated: mode={}"
        }},
        {"鼠标演示与特效 Overlay 初始化完成 (Taskbar Safe)", {
            "[{}] 鼠标演示与特效 Overlay 初始化完成 (Taskbar Safe)",
            "[{}] Mouse demonstration & overlay initialized (Taskbar Safe)"
        }},
        {"鼠标钩子回调执行耗时过长: {} ms ({} us) type={}", {
            "[{}] 鼠标钩子回调执行耗时过长: {} ms ({} us) type={}",
            "[{}] Mouse hook callback execution exceeded budget: {} ms ({} us) type={}"
        }},
        {"鼠标钩子已安装, 跳过重复安装", {
            "[{}] 鼠标钩子已安装, 跳过重复安装",
            "[{}] Mouse hook already installed, skipping duplicate installation"
        }},
        {"鼠标钩子暂停状态: paused={}", {
            "[{}] 鼠标钩子暂停状态: paused={}",
            "[{}] Mouse hook paused state: paused={}"
        }},
        {"鼠标钩子熔断冷却期满，自动尝试恢复工作状态", {
            "[{}] 鼠标钩子熔断冷却期满，自动尝试恢复工作状态",
            "[{}] Mouse hook breaker cooldown elapsed, restoring active state"
        }},
        {"鼠标钩子熔断复位回调异常", {
            "[{}] 鼠标钩子熔断复位回调异常",
            "[{}] Mouse hook breaker reset callback exception"
        }},
        {"已按设置拉起管理员实例，当前进程退出", {
            "[{}] 已按设置拉起管理员实例，当前进程退出",
            "[{}] Elevated admin instance launched per settings, exiting current process"
        }},
        {"已按设置拉起管理员实例, 当前进程退出", {
            "[{}] 已按设置拉起管理员实例, 当前进程退出",
            "[{}] Elevated admin instance launched per settings, exiting current process"
        }},
        {"内置核心 IPC 处理器注册完成 (含性能监控、配置管理、系统信息、窗口枚举)", {
            "[{}] 内置核心 IPC 处理器注册完成 (含性能监控、配置管理、系统信息、窗口枚举)",
            "[{}] Core IPC handlers registered (performance, config, system, window enumeration)"
        }},
        {"内置核心 IPC 处理器注册完成（含性能监控、配置管理、系统信息、窗口枚举）", {
            "[{}] 内置核心 IPC 处理器注册完成（含性能监控、配置管理、系统信息、窗口枚举）",
            "[{}] Core IPC handlers registered (performance, config, system, window enumeration)"
        }},
        {"IPC 响应完成: method={}, id={}, 耗时={}us", {
            "[{}] IPC 响应完成: method={}, id={}, 耗时={}us",
            "[{}] IPC response completed: method={}, id={}, duration={}us"
        }},
        {"全局核心鼠标钩子安装成功", {
            "[{}] 全局核心鼠标钩子安装成功",
            "[{}] Global mouse hook installed successfully"
        }},
        {"全局核心键盘钩子安装成功", {
            "[{}] 全局核心键盘钩子安装成功",
            "[{}] Global keyboard hook installed successfully"
        }},
        {"开始扫描插件目录: {}", {
            "[{}] 开始扫描插件目录: {}",
            "[{}] Scanning plugin directory: {}"
        }},
        {"发现插件 DLL: {}", {
            "[{}] 发现插件 DLL: {}",
            "[{}] Discovered plugin DLL: {}"
        }},
        {"快捷键管理器已初始化", {
            "[{}] 快捷键管理器已初始化",
            "[{}] Hotkey manager initialized"
        }},
        {"快捷键管理器已关闭", {
            "[{}] 快捷键管理器已关闭",
            "[{}] Hotkey manager closed"
        }},
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
        {"OCR 提取完成, 行数={}", {
            "[{}] OCR 提取完成, 行数={}",
            "[{}] OCR text extracted, lines={}"
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
        {"前台处于全屏独占应用，自动免打扰跳过截图: hwnd=0x{:X}", {
            "[{}] 前台处于全屏独占应用，自动免打扰跳过截图: hwnd=0x{:X}",
            "[{}] Foreground is exclusive fullscreen, skipping capture (Do Not Disturb): hwnd=0x{:X}"
        }},
        {"执行全屏截图", {
            "[{}] 执行全屏截图",
            "[{}] Executing fullscreen capture"
        }},
        {"截取窗口: hwnd={}, region=({},{})x{}x{}", {
            "[{}] 截取窗口: hwnd={}, region=({},{})x{}x{}",
            "[{}] Capturing window: hwnd={}, region=({},{})x{}x{}"
        }},
        {"DXGI 捕获不可用，回退 GDI: {}", {
            "[{}] DXGI 捕获不可用，回退 GDI: {}",
            "[{}] DXGI 捕获不可用，回退 GDI: {}"
        }},
        {"DXGI 捕获运行时失败，已切换 GDI: {}", {
            "[{}] DXGI 捕获运行时失败，已切换 GDI: {}",
            "[{}] DXGI 捕获运行时失败，已切换 GDI: {}"
        }},
        {"CaptureHistory: 忽略空图像", {
            "[{}] CaptureHistory: 忽略空图像",
            "[{}] CaptureHistory: 忽略空图像"
        }},
        {"CaptureHistory: 推入新截图, 尺寸={}x{}, 当前历史数={}", {
            "[{}] CaptureHistory: 推入新截图, 尺寸={}x{}, 当前历史数={}",
            "[{}] CaptureHistory: 推入新截图, 尺寸={}x{}, 当前历史数={}"
        }},
        {"CaptureHistory: 已清空所有历史记录", {
            "[{}] CaptureHistory: 已清空所有历史记录",
            "[{}] CaptureHistory: 已清空所有历史记录"
        }},
        {"CaptureHistory: 设置最大历史数={}", {
            "[{}] CaptureHistory: 设置最大历史数={}",
            "[{}] CaptureHistory: 设置最大历史数={}"
        }},
        {"截图覆盖层 DPI 文本资源更新失败: dpi={}", {
            "[{}] 截图覆盖层 DPI 文本资源更新失败: dpi={}",
            "[{}] 截图覆盖层 DPI 文本资源更新失败: dpi={}"
        }},
        {"CaptureOverlay 窗口过程异常: {}", {
            "[{}] CaptureOverlay 窗口过程异常: {}",
            "[{}] CaptureOverlay 窗口过程异常: {}"
        }},
        {"CaptureOverlay 窗口过程未知异常", {
            "[{}] CaptureOverlay 窗口过程未知异常",
            "[{}] CaptureOverlay 窗口过程未知异常"
        }},
        {"截图覆盖层窗口创建失败", {
            "[{}] 截图覆盖层窗口创建失败",
            "[{}] 截图覆盖层窗口创建失败"
        }},
        {"截图覆盖层 Direct2D 初始化失败", {
            "[{}] 截图覆盖层 Direct2D 初始化失败",
            "[{}] 截图覆盖层 Direct2D 初始化失败"
        }},
        {"GetDC(nullptr) 失败, error={}", {
            "[{}] GetDC(nullptr) 失败, error={}",
            "[{}] GetDC(nullptr) 失败, error={}"
        }},
        {"CreateCompatibleDC 失败, error={}", {
            "[{}] CreateCompatibleDC 失败, error={}",
            "[{}] CreateCompatibleDC 失败, error={}"
        }},
        {"CreateDIBSection 失败, error={}", {
            "[{}] CreateDIBSection 失败, error={}",
            "[{}] CreateDIBSection 失败, error={}"
        }},
        {"截图覆盖层桌面底图捕获成功: {}x{} (坐标=[{}, {}])", {
            "[{}] 截图覆盖层桌面底图捕获成功: {}x{} (坐标=[{}, {}])",
            "[{}] 截图覆盖层桌面底图捕获成功: {}x{} (坐标=[{}, {}])"
        }},
        {"截图覆盖层 BitBlt 失败, error={}", {
            "[{}] 截图覆盖层 BitBlt 失败, error={}",
            "[{}] 截图覆盖层 BitBlt 失败, error={}"
        }},
        {"截图底图上传失败: 渲染目标或图像为空", {
            "[{}] 截图底图上传失败: 渲染目标或图像为空",
            "[{}] 截图底图上传失败: 渲染目标或图像为空"
        }},
        {"截图底图上传失败: 不支持的通道数={}", {
            "[{}] 截图底图上传失败: 不支持的通道数={}",
            "[{}] 截图底图上传失败: 不支持的通道数={}"
        }},
        {"截图底图上传 Direct2D 失败, hr=0x{:08X}", {
            "[{}] 截图底图上传 Direct2D 失败, hr=0x{:08X}",
            "[{}] 截图底图上传 Direct2D 失败, hr=0x{:08X}"
        }},
        {"GDI+ 文本渲染器初始化失败, status={}", {
            "[{}] GDI+ 文本渲染器初始化失败, status={}",
            "[{}] GDI+ 文本渲染器初始化失败, status={}"
        }},
        {"GDI+ 文本渲染器已初始化", {
            "[{}] GDI+ 文本渲染器已初始化",
            "[{}] GDI+ 文本渲染器已初始化"
        }},
        {"GDI+ 文本渲染器已关闭", {
            "[{}] GDI+ 文本渲染器已关闭",
            "[{}] GDI+ 文本渲染器已关闭"
        }},
        {"录屏已保存: {}", {
            "[{}] 录屏已保存: {}",
            "[{}] 录屏已保存: {}"
        }},
        {"OCR 后台线程异常: {}", {
            "[{}] OCR 后台线程异常: {}",
            "[{}] OCR 后台线程异常: {}"
        }},
        {"OCR 后台线程未知异常", {
            "[{}] OCR 后台线程未知异常",
            "[{}] OCR 后台线程未知异常"
        }},
        {"录制指示器按需初始化失败", {
            "[{}] 录制指示器按需初始化失败",
            "[{}] 录制指示器按需初始化失败"
        }},
        {"录制指示器按需初始化成功", {
            "[{}] 录制指示器按需初始化成功",
            "[{}] 录制指示器按需初始化成功"
        }},
        {"录制指示器已隐藏", {
            "[{}] 录制指示器已隐藏",
            "[{}] 录制指示器已隐藏"
        }},
        {"当前 Windows 版本无法从捕获中排除录制悬浮条: error={}", {
            "[{}] 当前 Windows 版本无法从捕获中排除录制悬浮条: error={}",
            "[{}] 当前 Windows 版本无法从捕获中排除录制悬浮条: error={}"
        }},
        {"OCR 未提取到文字", {
            "[{}] OCR 未提取到文字",
            "[{}] OCR 未提取到文字"
        }},
        {"OCR(覆盖层) 异常: {}", {
            "[{}] OCR(覆盖层) 异常: {}",
            "[{}] OCR(覆盖层) 异常: {}"
        }},
        {"OCR(覆盖层) 未知异常", {
            "[{}] OCR(覆盖层) 未知异常",
            "[{}] OCR(覆盖层) 未知异常"
        }},
        {"截图已在进行中", {
            "[{}] 截图已在进行中",
            "[{}] 截图已在进行中"
        }},
        {"截图失败: {}", {
            "[{}] 截图失败: {}",
            "[{}] 截图失败: {}"
        }},
        {"截图已复制到剪贴板", {
            "[{}] 截图已复制到剪贴板",
            "[{}] 截图已复制到剪贴板"
        }},
        {"复制到剪贴板失败", {
            "[{}] 复制到剪贴板失败",
            "[{}] 复制到剪贴板失败"
        }},
        {"创建截图内存 DC 失败, error={}", {
            "[{}] 创建截图内存 DC 失败, error={}",
            "[{}] 创建截图内存 DC 失败, error={}"
        }},
        {"创建截图 DIBSection 失败, error={}", {
            "[{}] 创建截图 DIBSection 失败, error={}",
            "[{}] 创建截图 DIBSection 失败, error={}"
        }},
        {"BitBlt 失败, error={}", {
            "[{}] BitBlt 失败, error={}",
            "[{}] BitBlt 失败, error={}"
        }},
        {"保存截图失败: {}", {
            "[{}] 保存截图失败: {}",
            "[{}] 保存截图失败: {}"
        }},
        {"已在录制中, 请先停止当前录制", {
            "[{}] 已在录制中, 请先停止当前录制",
            "[{}] 已在录制中, 请先停止当前录制"
        }},
        {"录屏参数无效: fps={}, bitrate={}, countdown={}", {
            "[{}] 录屏参数无效: fps={}, bitrate={}, countdown={}",
            "[{}] 录屏参数无效: fps={}, bitrate={}, countdown={}"
        }},
        {"录屏区域无效: {}x{}", {
            "[{}] 录屏区域无效: {}x{}",
            "[{}] 录屏区域无效: {}x{}"
        }},
        {"FFmpeg 编码器初始化失败", {
            "[{}] FFmpeg 编码器初始化失败",
            "[{}] FFmpeg 编码器初始化失败"
        }},
        {"录屏捕获资源初始化失败", {
            "[{}] 录屏捕获资源初始化失败",
            "[{}] 录屏捕获资源初始化失败"
        }},
        {"帧捕获/编码失败, 停止录制", {
            "[{}] 帧捕获/编码失败, 停止录制",
            "[{}] 帧捕获/编码失败, 停止录制"
        }},
        {"录屏持续过载，自适应帧步长调整为 {}", {
            "[{}] 录屏持续过载，自适应帧步长调整为 {}",
            "[{}] 录屏持续过载，自适应帧步长调整为 {}"
        }},
        {"录屏因磁盘安全余量不足而停止", {
            "[{}] 录屏因磁盘安全余量不足而停止",
            "[{}] 录屏因磁盘安全余量不足而停止"
        }},
        {"录制线程异常, 已停止录制: {}", {
            "[{}] 录制线程异常, 已停止录制: {}",
            "[{}] 录制线程异常, 已停止录制: {}"
        }},
        {"录制线程未知异常, 已停止录制", {
            "[{}] 录制线程未知异常, 已停止录制",
            "[{}] 录制线程未知异常, 已停止录制"
        }},
        {"录屏临时文件不存在, 无法提交: {}", {
            "[{}] 录屏临时文件不存在, 无法提交: {}",
            "[{}] 录屏临时文件不存在, 无法提交: {}"
        }},
        {"录屏文件原子提交失败: error={}, temporary={}", {
            "[{}] 录屏文件原子提交失败: error={}, temporary={}",
            "[{}] 录屏文件原子提交失败: error={}, temporary={}"
        }},
        {"录屏输出目录不可写: path={}, error={}", {
            "[{}] 录屏输出目录不可写: path={}, error={}",
            "[{}] 录屏输出目录不可写: path={}, error={}"
        }},
        {"录屏剩余空间不足: available={}, required={}", {
            "[{}] 录屏剩余空间不足: available={}, required={}",
            "[{}] 录屏剩余空间不足: available={}, required={}"
        }},
        {"avformat_alloc_output_context2 失败", {
            "[{}] avformat_alloc_output_context2 失败",
            "[{}] avformat_alloc_output_context2 失败"
        }},
        {"所有候选编码器均初始化失败: format={}", {
            "[{}] 所有候选编码器均初始化失败: format={}",
            "[{}] 所有候选编码器均初始化失败: format={}"
        }},
        {"avformat_new_stream 失败", {
            "[{}] avformat_new_stream 失败",
            "[{}] avformat_new_stream 失败"
        }},
        {"avio_open 失败: {}", {
            "[{}] avio_open 失败: {}",
            "[{}] avio_open 失败: {}"
        }},
        {"avformat_write_header 失败", {
            "[{}] avformat_write_header 失败",
            "[{}] avformat_write_header 失败"
        }},
        {"av_frame_alloc 失败", {
            "[{}] av_frame_alloc 失败",
            "[{}] av_frame_alloc 失败"
        }},
        {"av_frame_get_buffer 失败", {
            "[{}] av_frame_get_buffer 失败",
            "[{}] av_frame_get_buffer 失败"
        }},
        {"av_packet_alloc 失败", {
            "[{}] av_packet_alloc 失败",
            "[{}] av_packet_alloc 失败"
        }},
        {"FFmpeg 编码器初始化成功: codec={}, {}x{} @ {}fps", {
            "[{}] FFmpeg 编码器初始化成功: codec={}, {}x{} @ {}fps",
            "[{}] FFmpeg 编码器初始化成功: codec={}, {}x{} @ {}fps"
        }},
        {"录屏帧捕获失败: backend={}, error={}", {
            "[{}] 录屏帧捕获失败: backend={}, error={}",
            "[{}] 录屏帧捕获失败: backend={}, error={}"
        }},
        {"sws_getContext 失败: sourceFormat={}", {
            "[{}] sws_getContext 失败: sourceFormat={}",
            "[{}] sws_getContext 失败: sourceFormat={}"
        }},
        {"录屏捕获后端初始化失败: error={}", {
            "[{}] 录屏捕获后端初始化失败: error={}",
            "[{}] 录屏捕获后端初始化失败: error={}"
        }},
        {"拼接帧 {}/{}: offset={}, 结果高度={}", {
            "[{}] 拼接帧 {}/{}: offset={}, 结果高度={}",
            "[{}] 拼接帧 {}/{}: offset={}, 结果高度={}"
        }},
        {"当前 Windows 版本无法从捕获中排除长截图预览: error={}", {
            "[{}] 当前 Windows 版本无法从捕获中排除长截图预览: error={}",
            "[{}] 当前 Windows 版本无法从捕获中排除长截图预览: error={}"
        }},
        {"删除配置项失败: key={}, error={}", {
            "[{}] 删除配置项失败: key={}, error={}",
            "[{}] 删除配置项失败: key={}, error={}"
        }},
        {"JSON 字符串解析失败: {}", {
            "[{}] JSON 字符串解析失败: {}",
            "[{}] JSON 字符串解析失败: {}"
        }},
        {"EventBus: 取消订阅, subId={}", {
            "[{}] EventBus: 取消订阅, subId={}",
            "[{}] EventBus: 取消订阅, subId={}"
        }},
        {"EventBus: 同步取消订阅, subId={}", {
            "[{}] EventBus: 同步取消订阅, subId={}",
            "[{}] EventBus: 同步取消订阅, subId={}"
        }},
        {"EventBus: 清除所有订阅", {
            "[{}] EventBus: 清除所有订阅",
            "[{}] EventBus: 清除所有订阅"
        }},
        {"EventBus: 事件类型转换失败: {}", {
            "[{}] EventBus: 事件类型转换失败: {}",
            "[{}] EventBus: 事件类型转换失败: {}"
        }},
        {"EventBus: 事件处理器异常: {}", {
            "[{}] EventBus: 事件处理器异常: {}",
            "[{}] EventBus: 事件处理器异常: {}"
        }},
        {"EventBus: 事件处理器未知异常", {
            "[{}] EventBus: 事件处理器未知异常",
            "[{}] EventBus: 事件处理器未知异常"
        }},
        {"EventBus: 订阅事件 [{}], subId={}", {
            "[{}] EventBus: 订阅事件 [{}], subId={}",
            "[{}] EventBus: 订阅事件 [{}], subId={}"
        }},
        {"EventBus: 发布事件 [{}], 订阅者数={}", {
            "[{}] EventBus: 发布事件 [{}], 订阅者数={}",
            "[{}] EventBus: 发布事件 [{}], 订阅者数={}"
        }},
        {"快捷键管理器已关闭, 所有快捷键已注销", {
            "[{}] 快捷键管理器已关闭, 所有快捷键已注销",
            "[{}] 快捷键管理器已关闭, 所有快捷键已注销"
        }},
        {"快捷键名称已存在, 将先注销旧绑定: name={}", {
            "[{}] 快捷键名称已存在, 将先注销旧绑定: name={}",
            "[{}] 快捷键名称已存在, 将先注销旧绑定: name={}"
        }},
        {"快捷键已禁用: name={}", {
            "[{}] 快捷键已禁用: name={}",
            "[{}] 快捷键已禁用: name={}"
        }},
        {"快捷键 {} 已被其他程序占用", {
            "[{}] 快捷键 {} 已被其他程序占用",
            "[{}] 快捷键 {} 已被其他程序占用"
        }},
        {"尝试注销不存在的快捷键: name={}", {
            "[{}] 尝试注销不存在的快捷键: name={}",
            "[{}] 尝试注销不存在的快捷键: name={}"
        }},
        {"已注销快捷键: name={}", {
            "[{}] 已注销快捷键: name={}",
            "[{}] 已注销快捷键: name={}"
        }},
        {"尝试重绑定不存在的快捷键: name={}", {
            "[{}] 尝试重绑定不存在的快捷键: name={}",
            "[{}] 尝试重绑定不存在的快捷键: name={}"
        }},
        {"快捷键重绑定成功（会话外不占用）: name={}, {} → {}", {
            "[{}] 快捷键重绑定成功（会话外不占用）: name={}, {} → {}",
            "[{}] 快捷键重绑定成功（会话外不占用）: name={}, {} → {}"
        }},
        {"快捷键重绑定成功: name={}, {} → {}", {
            "[{}] 快捷键重绑定成功: name={}, {} → {}",
            "[{}] 快捷键重绑定成功: name={}, {} → {}"
        }},
        {"尝试武装不存在的快捷键: name={}", {
            "[{}] 尝试武装不存在的快捷键: name={}",
            "[{}] 尝试武装不存在的快捷键: name={}"
        }},
        {"快捷键已卸下，交还给前台应用: name={}, def={}", {
            "[{}] 快捷键已卸下，交还给前台应用: name={}, def={}",
            "[{}] 快捷键已卸下，交还给前台应用: name={}, def={}"
        }},
        {"快捷键已重新占用: name={}, def={}", {
            "[{}] 快捷键已重新占用: name={}, def={}",
            "[{}] 快捷键已重新占用: name={}, def={}"
        }},
        {"快捷键已暂停触发 (录制模式中)", {
            "[{}] 快捷键已暂停触发 (录制模式中)",
            "[{}] 快捷键已暂停触发 (录制模式中)"
        }},
        {"快捷键触发: name={}", {
            "[{}] 快捷键触发: name={}",
            "[{}] 快捷键触发: name={}"
        }},
        {"快捷键回调异常: name={}, error={}", {
            "[{}] 快捷键回调异常: name={}, error={}",
            "[{}] 快捷键回调异常: name={}, error={}"
        }},
        {"KeyboardHook 发生未捕获异常 {}", {
            "[{}] KeyboardHook 发生未捕获异常 {}",
            "[{}] KeyboardHook 发生未捕获异常 {}"
        }},
        {"KeyboardHook 发生未知异常", {
            "[{}] KeyboardHook 发生未知异常",
            "[{}] KeyboardHook 发生未知异常"
        }},
        {"安装全局核心鼠标钩子失败, error={}", {
            "[{}] 安装全局核心鼠标钩子失败, error={}",
            "[{}] 安装全局核心鼠标钩子失败, error={}"
        }},
        {"全局核心鼠标钩子已卸载", {
            "[{}] 全局核心鼠标钩子已卸载",
            "[{}] 全局核心鼠标钩子已卸载"
        }},
        {"MouseHook 活动事件分发异常: {}", {
            "[{}] MouseHook 活动事件分发异常: {}",
            "[{}] MouseHook 活动事件分发异常: {}"
        }},
        {"MouseHook 活动事件分发未知异常", {
            "[{}] MouseHook 活动事件分发未知异常",
            "[{}] MouseHook 活动事件分发未知异常"
        }},
        {"拒绝过大的 IPC 消息: {} bytes", {
            "[{}] 拒绝过大的 IPC 消息: {} bytes",
            "[{}] 拒绝过大的 IPC 消息: {} bytes"
        }},
        {"IPC 处理器异常: id={}, error={}", {
            "[{}] IPC 处理器异常: id={}, error={}",
            "[{}] IPC 处理器异常: id={}, error={}"
        }},
        {"IPC 处理器未知异常: id={}", {
            "[{}] IPC 处理器未知异常: id={}",
            "[{}] IPC 处理器未知异常: id={}"
        }},
        {"异步 IPC 响应回调异常: {}", {
            "[{}] 异步 IPC 响应回调异常: {}",
            "[{}] 异步 IPC 响应回调异常: {}"
        }},
        {"异步 IPC 响应回调未知异常", {
            "[{}] 异步 IPC 响应回调未知异常",
            "[{}] 异步 IPC 响应回调未知异常"
        }},
        {"异步 IPC 线程池已启动: threads={}", {
            "[{}] 异步 IPC 线程池已启动: threads={}",
            "[{}] 异步 IPC 线程池已启动: threads={}"
        }},
        {"异步 IPC 线程池已关闭", {
            "[{}] 异步 IPC 线程池已关闭",
            "[{}] 异步 IPC 线程池已关闭"
        }},
        {"IPC 方法已标记为异步执行: {}", {
            "[{}] IPC 方法已标记为异步执行: {}",
            "[{}] IPC 方法已标记为异步执行: {}"
        }},
        {"拒绝过大的异步 IPC 消息: {} bytes", {
            "[{}] 拒绝过大的异步 IPC 消息: {} bytes",
            "[{}] 拒绝过大的异步 IPC 消息: {} bytes"
        }},
        {"异步 IPC 队列过载, 丢弃最旧的待处理请求", {
            "[{}] 异步 IPC 队列过载, 丢弃最旧的待处理请求",
            "[{}] 异步 IPC 队列过载, 丢弃最旧的待处理请求"
        }},
        {"事件推送器已设置", {
            "[{}] 事件推送器已设置",
            "[{}] 事件推送器已设置"
        }},
        {"IPC 处理器与事件推送器已清空", {
            "[{}] IPC 处理器与事件推送器已清空",
            "[{}] IPC 处理器与事件推送器已清空"
        }},
        {"快捷键持久化失败且运行时回滚失败: name={}", {
            "[{}] 快捷键持久化失败且运行时回滚失败: name={}",
            "[{}] 快捷键持久化失败且运行时回滚失败: name={}"
        }},
        {"截图完成, 尺寸={}x{}", {
            "[{}] 截图完成, 尺寸={}x{}",
            "[{}] 截图完成, 尺寸={}x{}"
        }},
        {"[Lua] keyboard.sendKeys 无法解析主键: {}", {
            "[{}] [Lua] keyboard.sendKeys 无法解析主键: {}",
            "[{}] [Lua] keyboard.sendKeys 无法解析主键: {}"
        }},
        {"[Lua] http: 无效的 URL: {}", {
            "[{}] [Lua] http: 无效的 URL: {}",
            "[{}] [Lua] http: 无效的 URL: {}"
        }},
        {"[Lua] http 响应超过 {} 字节上限: {}", {
            "[{}] [Lua] http 响应超过 {} 字节上限: {}",
            "[{}] [Lua] http 响应超过 {} 字节上限: {}"
        }},
        {"[Lua] http 请求失败: {} (err={})", {
            "[{}] [Lua] http 请求失败: {} (err={})",
            "[{}] [Lua] http 请求失败: {} (err={})"
        }},
        {"Lua 脚本引擎初始化成功 (easy.* API 已注入并启用细粒度沙箱权限防御)", {
            "[{}] Lua 脚本引擎初始化成功 (easy.* API 已注入并启用细粒度沙箱权限防御)",
            "[{}] Lua 脚本引擎初始化成功 (easy.* API 已注入并启用细粒度沙箱权限防御)"
        }},
        {"Lua 引擎初始化失败: {}", {
            "[{}] Lua 引擎初始化失败: {}",
            "[{}] Lua 引擎初始化失败: {}"
        }},
        {"Lua 脚本引擎已关闭", {
            "[{}] Lua 脚本引擎已关闭",
            "[{}] Lua 脚本引擎已关闭"
        }},
        {"[Lua] 引擎未初始化, 无法执行 {}", {
            "[{}] [Lua] 引擎未初始化, 无法执行 {}",
            "[{}] [Lua] 引擎未初始化, 无法执行 {}"
        }},
        {"[Lua] {} 执行错误: {}", {
            "[{}] [Lua] {} 执行错误: {}",
            "[{}] [Lua] {} 执行错误: {}"
        }},
        {"[Lua] {} 运行时异常: {}", {
            "[{}] [Lua] {} 运行时异常: {}",
            "[{}] [Lua] {} 运行时异常: {}"
        }},
        {"[Lua] 无法打开脚本文件: {}", {
            "[{}] [Lua] 无法打开脚本文件: {}",
            "[{}] [Lua] 无法打开脚本文件: {}"
        }},
        {"[Lua] shell.run 启动失败: {}", {
            "[{}] [Lua] shell.run 启动失败: {}",
            "[{}] [Lua] shell.run 启动失败: {}"
        }},
        {"扫描插件目录失败: {}, error={}", {
            "[{}] 扫描插件目录失败: {}, error={}",
            "[{}] 扫描插件目录失败: {}, error={}"
        }},
        {"CreatePlugin 异常: {}, error={}", {
            "[{}] CreatePlugin 异常: {}, error={}",
            "[{}] CreatePlugin 异常: {}, error={}"
        }},
        {"CreatePlugin 未知异常: {}", {
            "[{}] CreatePlugin 未知异常: {}",
            "[{}] CreatePlugin 未知异常: {}"
        }},
        {"CreatePlugin 返回 null: {}", {
            "[{}] CreatePlugin 返回 null: {}",
            "[{}] CreatePlugin 返回 null: {}"
        }},
        {"初始化插件: {}", {
            "[{}] 初始化插件: {}",
            "[{}] 初始化插件: {}"
        }},
        {"停止插件: {}", {
            "[{}] 停止插件: {}",
            "[{}] 停止插件: {}"
        }},
        {"卸载插件 DLL: {}", {
            "[{}] 卸载插件 DLL: {}",
            "[{}] 卸载插件 DLL: {}"
        }},
        {"PerformanceMonitor: 启动, 采样间隔={}ms", {
            "[{}] PerformanceMonitor: 启动, 采样间隔={}ms",
            "[{}] PerformanceMonitor: 启动, 采样间隔={}ms"
        }},
        {"PerformanceMonitor: 已停止", {
            "[{}] PerformanceMonitor: 已停止",
            "[{}] PerformanceMonitor: 已停止"
        }},
        {"PerformanceMonitor: 记录延迟 [{}] = {:.2f} ms", {
            "[{}] PerformanceMonitor: 记录延迟 [{}] = {:.2f} ms",
            "[{}] PerformanceMonitor: 记录延迟 [{}] = {:.2f} ms"
        }},
        {"PerformanceMonitor: 插件初始化 [{}] = {:.2f} ms", {
            "[{}] PerformanceMonitor: 插件初始化 [{}] = {:.2f} ms",
            "[{}] PerformanceMonitor: 插件初始化 [{}] = {:.2f} ms"
        }},
        {"加载统计数据失败: {}", {
            "[{}] 加载统计数据失败: {}",
            "[{}] 加载统计数据失败: {}"
        }},
        {"保存统计数据失败: {}", {
            "[{}] 保存统计数据失败: {}",
            "[{}] 保存统计数据失败: {}"
        }},
        {"ShellContextMenuService: 捕获第三方 Shell 扩展异常，已安全隔离防御", {
            "[{}] ShellContextMenuService: 捕获第三方 Shell 扩展异常，已安全隔离防御",
            "[{}] ShellContextMenuService: 捕获第三方 Shell 扩展异常，已安全隔离防御"
        }},
        {"DialogEngine: 生命周期 WinEvent 钩子注册失败, error={}", {
            "[{}] DialogEngine: 生命周期 WinEvent 钩子注册失败, error={}",
            "[{}] DialogEngine: 生命周期 WinEvent 钩子注册失败, error={}"
        }},
        {"DialogEngine: 前台窗口钩子注册失败, error={}", {
            "[{}] DialogEngine: 前台窗口钩子注册失败, error={}",
            "[{}] DialogEngine: 前台窗口钩子注册失败, error={}"
        }},
        {"DialogEngine: 位置钩子注册失败, error={}", {
            "[{}] DialogEngine: 位置钩子注册失败, error={}",
            "[{}] DialogEngine: 位置钩子注册失败, error={}"
        }},
        {"DialogEngine: 按钮调用钩子注册失败, error={}", {
            "[{}] DialogEngine: 按钮调用钩子注册失败, error={}",
            "[{}] DialogEngine: 按钮调用钩子注册失败, error={}"
        }},
        {"DialogEngine: WinEvent 线程就绪, tid={}", {
            "[{}] DialogEngine: WinEvent 线程就绪, tid={}",
            "[{}] DialogEngine: WinEvent 线程就绪, tid={}"
        }},
        {"DialogEngine: WinEvent 线程已退出", {
            "[{}] DialogEngine: WinEvent 线程已退出",
            "[{}] DialogEngine: WinEvent 线程已退出"
        }},
        {"启动文件对话框增强引擎 (per-dialog session / per-exe memory)", {
            "[{}] 启动文件对话框增强引擎 (per-dialog session / per-exe memory)",
            "[{}] 启动文件对话框增强引擎 (per-dialog session / per-exe memory)"
        }},
        {"DialogEngine: 生命周期钩子不可用，文件对话框增强无法可靠工作", {
            "[{}] DialogEngine: 生命周期钩子不可用，文件对话框增强无法可靠工作",
            "[{}] DialogEngine: 生命周期钩子不可用，文件对话框增强无法可靠工作"
        }},
        {"DialogEngine 启动完成", {
            "[{}] DialogEngine 启动完成",
            "[{}] DialogEngine 启动完成"
        }},
        {"停止文件对话框增强引擎", {
            "[{}] 停止文件对话框增强引擎",
            "[{}] 停止文件对话框增强引擎"
        }},
        {"DialogEngine: 收到候选对话框 SHOW, hwnd=0x{:X}, pid={}", {
            "[{}] DialogEngine: 收到候选对话框 SHOW, hwnd=0x{:X}, pid={}",
            "[{}] DialogEngine: 收到候选对话框 SHOW, hwnd=0x{:X}, pid={}"
        }},
        {"DialogEngine: 对话框动作, hwnd=0x{:X}, action={}", {
            "[{}] DialogEngine: 对话框动作, hwnd=0x{:X}, action={}",
            "[{}] DialogEngine: 对话框动作, hwnd=0x{:X}, action={}"
        }},
        {"DialogEngine: 候选窗口不是文件对话框, hwnd=0x{:X}", {
            "[{}] DialogEngine: 候选窗口不是文件对话框, hwnd=0x{:X}",
            "[{}] DialogEngine: 候选窗口不是文件对话框, hwnd=0x{:X}"
        }},
        {"DialogEngine: 文件对话框会话已建立, hwnd=0x{:X}, pid={}, exe={}, initial={}, restore={}", {
            "[{}] DialogEngine: 文件对话框会话已建立, hwnd=0x{:X}, pid={}, exe={}, initial={}, restore={}",
            "[{}] DialogEngine: 文件对话框会话已建立, hwnd=0x{:X}, pid={}, exe={}, initial={}, restore={}"
        }},
        {"DialogEngine: EXE 目录恢复完成, exe={}, path={}, success={}", {
            "[{}] DialogEngine: EXE 目录恢复完成, exe={}, path={}, success={}",
            "[{}] DialogEngine: EXE 目录恢复完成, exe={}, path={}, success={}"
        }},
        {"DialogEngine: 已提交 EXE 目录记忆, exe={}, current={}, selected={}, directory={}, confirmed={}", {
            "[{}] DialogEngine: 已提交 EXE 目录记忆, exe={}, current={}, selected={}, directory={}, confirmed={}",
            "[{}] DialogEngine: 已提交 EXE 目录记忆, exe={}, current={}, selected={}, directory={}, confirmed={}"
        }},
        {"DialogEngine: 对话框关闭但未提交记忆, exe={}, cancelled={}, confirmed={}, selectionChanged={}", {
            "[{}] DialogEngine: 对话框关闭但未提交记忆, exe={}, cancelled={}, confirmed={}, selectionChanged={}",
            "[{}] DialogEngine: 对话框关闭但未提交记忆, exe={}, cancelled={}, confirmed={}, selectionChanged={}"
        }},
        {"UIA: 地址栏回车后对话框意外关闭", {
            "[{}] UIA: 地址栏回车后对话框意外关闭",
            "[{}] UIA: 地址栏回车后对话框意外关闭"
        }},
        {"UIA: {} 已向地址栏安全提交导航: {}", {
            "[{}] UIA: {} 已向地址栏安全提交导航: {}",
            "[{}] UIA: {} 已向地址栏安全提交导航: {}"
        }},
        {"UIA: IUIAutomation 接口不可用", {
            "[{}] UIA: IUIAutomation 接口不可用",
            "[{}] UIA: IUIAutomation 接口不可用"
        }},
        {"UIA: {} 候选未通过地址栏硬校验 id={}, belongs={}, upper={}, ", {
            "[{}] UIA: {} 候选未通过地址栏硬校验 id={}, belongs={}, upper={}, ",
            "[{}] UIA: {} 候选未通过地址栏硬校验 id={}, belongs={}, upper={}, "
        }},
        {"UIA: 地址栏 ValuePattern::SetValue 失败 hr=0x{:X}", {
            "[{}] UIA: 地址栏 ValuePattern::SetValue 失败 hr=0x{:X}",
            "[{}] UIA: 地址栏 ValuePattern::SetValue 失败 hr=0x{:X}"
        }},
        {"UIA: {} 未能发送到目标对话框", {
            "[{}] UIA: {} 未能发送到目标对话框",
            "[{}] UIA: {} 未能发送到目标对话框"
        }},
        {"UIA: 所有地址栏激活方式均未通过安全校验，本次不导航", {
            "[{}] UIA: 所有地址栏激活方式均未通过安全校验，本次不导航",
            "[{}] UIA: 所有地址栏激活方式均未通过安全校验，本次不导航"
        }},
        {"拒绝导航到不存在或非目录路径: {}", {
            "[{}] 拒绝导航到不存在或非目录路径: {}",
            "[{}] 拒绝导航到不存在或非目录路径: {}"
        }},
        {"执行文件对话框导航: hwnd=0x{:X}, type={}, targetPath={}", {
            "[{}] 执行文件对话框导航: hwnd=0x{:X}, type={}, targetPath={}",
            "[{}] 执行文件对话框导航: hwnd=0x{:X}, type={}, targetPath={}"
        }},
        {"UIA: Modern 对话框导航失败，目标路径={}", {
            "[{}] UIA: Modern 对话框导航失败，目标路径={}",
            "[{}] UIA: Modern 对话框导航失败，目标路径={}"
        }},
        {"Legacy: 未找到底部输入控件, hwnd=0x{:X}", {
            "[{}] Legacy: 未找到底部输入控件, hwnd=0x{:X}",
            "[{}] Legacy: 未找到底部输入控件, hwnd=0x{:X}"
        }},
        {"Legacy 导航导致对话框意外关闭，已停止后续操作: {}", {
            "[{}] Legacy 导航导致对话框意外关闭，已停止后续操作: {}",
            "[{}] Legacy 导航导致对话框意外关闭，已停止后续操作: {}"
        }},
        {"Legacy: 已提交目录导航并恢复底部输入内容: {}", {
            "[{}] Legacy: 已提交目录导航并恢复底部输入内容: {}",
            "[{}] Legacy: 已提交目录导航并恢复底部输入内容: {}"
        }},
        {"创建 DialogRibbonOverlay 窗口失败", {
            "[{}] 创建 DialogRibbonOverlay 窗口失败",
            "[{}] 创建 DialogRibbonOverlay 窗口失败"
        }},
        {"doUpdatePosition 异常: {}", {
            "[{}] doUpdatePosition 异常: {}",
            "[{}] doUpdatePosition 异常: {}"
        }},
        {"doUpdatePosition 未知异常", {
            "[{}] doUpdatePosition 未知异常",
            "[{}] doUpdatePosition 未知异常"
        }},
        {"初始化文件对话框增强插件 (DialogEnhancerPlugin)", {
            "[{}] 初始化文件对话框增强插件 (DialogEnhancerPlugin)",
            "[{}] 初始化文件对话框增强插件 (DialogEnhancerPlugin)"
        }},
        {"启动 DialogEngine 失败", {
            "[{}] 启动 DialogEngine 失败",
            "[{}] 启动 DialogEngine 失败"
        }},
        {"关闭文件对话框增强插件 (DialogEnhancerPlugin)", {
            "[{}] 关闭文件对话框增强插件 (DialogEnhancerPlugin)",
            "[{}] 关闭文件对话框增强插件 (DialogEnhancerPlugin)"
        }},
        {"应用级内置命令未注册 Handler: cmd={}", {
            "[{}] 应用级内置命令未注册 Handler: cmd={}",
            "[{}] 应用级内置命令未注册 Handler: cmd={}"
        }},
        {"关闭窗口已投递: {}", {
            "[{}] 关闭窗口已投递: {}",
            "[{}] 关闭窗口已投递: {}"
        }},
        {"手势目标窗口不可用或属于覆盖层: {}", {
            "[{}] 手势目标窗口不可用或属于覆盖层: {}",
            "[{}] 手势目标窗口不可用或属于覆盖层: {}"
        }},
        {"手势目标窗口未能取得前台: hwnd=0x{:X}, class={}, fg=0x{:X}", {
            "[{}] 手势目标窗口未能取得前台: hwnd=0x{:X}, class={}, fg=0x{:X}",
            "[{}] 手势目标窗口未能取得前台: hwnd=0x{:X}, class={}, fg=0x{:X}"
        }},
        {"KeyStroke::send 被调用但 virtualKey 为空, 跳过", {
            "[{}] KeyStroke::send 被调用但 virtualKey 为空, 跳过",
            "[{}] KeyStroke::send 被调用但 virtualKey 为空, 跳过"
        }},
        {"拒绝向手势覆盖层注入按键: keys={}, target={}", {
            "[{}] 拒绝向手势覆盖层注入按键: keys={}, target={}",
            "[{}] 拒绝向手势覆盖层注入按键: keys={}, target={}"
        }},
        {"手势按键注入: keys={}, target={}, fg={}", {
            "[{}] 手势按键注入: keys={}, target={}, fg={}",
            "[{}] 手势按键注入: keys={}, target={}, fg={}"
        }},
        {"关闭已生效，不再补发 Alt+F4: {}", {
            "[{}] 关闭已生效，不再补发 Alt+F4: {}",
            "[{}] 关闭已生效，不再补发 Alt+F4: {}"
        }},
        {"关闭投递后窗口仍在，补发 Alt+F4: {}", {
            "[{}] 关闭投递后窗口仍在，补发 Alt+F4: {}",
            "[{}] 关闭投递后窗口仍在，补发 Alt+F4: {}"
        }},
        {"SendInput 未完全发送: expected={}, sent={}, lastError={}", {
            "[{}] SendInput 未完全发送: expected={}, sent={}, lastError={}",
            "[{}] SendInput 未完全发送: expected={}, sent={}, lastError={}"
        }},
        {"执行手势动作: SendKeys, keys={}", {
            "[{}] 执行手势动作: SendKeys, keys={}",
            "[{}] 执行手势动作: SendKeys, keys={}"
        }},
        {"执行手势动作: LuaScript, name={}, script={}", {
            "[{}] 执行手势动作: LuaScript, name={}, script={}",
            "[{}] 执行手势动作: LuaScript, name={}, script={}"
        }},
        {"执行手势动作: BuiltinCommand, cmd={}", {
            "[{}] 执行手势动作: BuiltinCommand, cmd={}",
            "[{}] 执行手势动作: BuiltinCommand, cmd={}"
        }},
        {"执行手势动作: RunProgram, path={}", {
            "[{}] 执行手势动作: RunProgram, path={}",
            "[{}] 执行手势动作: RunProgram, path={}"
        }},
        {"手势引擎启动失败: 无法安装鼠标钩子", {
            "[{}] 手势引擎启动失败: 无法安装鼠标钩子",
            "[{}] 手势引擎启动失败: 无法安装鼠标钩子"
        }},
        {"手势轨迹覆盖层不可用，手势识别与动作执行将继续运行", {
            "[{}] 手势轨迹覆盖层不可用，手势识别与动作执行将继续运行",
            "[{}] 手势轨迹覆盖层不可用，手势识别与动作执行将继续运行"
        }},
        {"手势引擎已启动, 默认Profile手势数={}, 浏览器Profile手势数={}", {
            "[{}] 手势引擎已启动, 默认Profile手势数={}, 浏览器Profile手势数={}",
            "[{}] 手势引擎已启动, 默认Profile手势数={}, 浏览器Profile手势数={}"
        }},
        {"手势引擎已停止", {
            "[{}] 手势引擎已停止",
            "[{}] 手势引擎已停止"
        }},
        {"前台窗口完整性更高，低完整性 WH_MOUSE_LL 收不到事件: hwnd=0x{:X} class={} pid={}", {
            "[{}] 前台窗口完整性更高，低完整性 WH_MOUSE_LL 收不到事件: hwnd=0x{:X} class={} pid={}",
            "[{}] 前台窗口完整性更高，低完整性 WH_MOUSE_LL 收不到事件: hwnd=0x{:X} class={} pid={}"
        }},
        {"手势暂停状态持久化失败，已回滚: paused={}", {
            "[{}] 手势暂停状态持久化失败，已回滚: paused={}",
            "[{}] 手势暂停状态持久化失败，已回滚: paused={}"
        }},
        {"手势暂停状态回调异常，已回滚: {}", {
            "[{}] 手势暂停状态回调异常，已回滚: {}",
            "[{}] 手势暂停状态回调异常，已回滚: {}"
        }},
        {"手势暂停状态回调发生未知异常，已回滚", {
            "[{}] 手势暂停状态回调发生未知异常，已回滚",
            "[{}] 手势暂停状态回调发生未知异常，已回滚"
        }},
        {"手势引擎暂停状态: paused={}", {
            "[{}] 手势引擎暂停状态: paused={}",
            "[{}] 手势引擎暂停状态: paused={}"
        }},
        {"手势引擎录制模式更新: recording={}", {
            "[{}] 手势引擎录制模式更新: recording={}",
            "[{}] 手势引擎录制模式更新: recording={}"
        }},
        {"手势触发按钮已设置: {}", {
            "[{}] 手势触发按钮已设置: {}",
            "[{}] 手势触发按钮已设置: {}"
        }},
        {"手势轨迹显示状态: visible={}", {
            "[{}] 手势轨迹显示状态: visible={}",
            "[{}] 手势轨迹显示状态: visible={}"
        }},
        {"手势全屏自动免打扰状态: enable={}", {
            "[{}] 手势全屏自动免打扰状态: enable={}",
            "[{}] 手势全屏自动免打扰状态: enable={}"
        }},
        {"手势目标窗口模式: {}", {
            "[{}] 手势目标窗口模式: {}",
            "[{}] 手势目标窗口模式: {}"
        }},
        {"手势起始超时已设置: initialTimeoutMs={}ms", {
            "[{}] 手势起始超时已设置: initialTimeoutMs={}ms",
            "[{}] 手势起始超时已设置: initialTimeoutMs={}ms"
        }},
        {"手势最小识别距离已设置: minSegmentDistance={}px", {
            "[{}] 手势最小识别距离已设置: minSegmentDistance={}px",
            "[{}] 手势最小识别距离已设置: minSegmentDistance={}px"
        }},
        {"手势引擎处于暂停或录制模式，放行所有按键", {
            "[{}] 手势引擎处于暂停或录制模式，放行所有按键",
            "[{}] 手势引擎处于暂停或录制模式，放行所有按键"
        }},
        {"前台窗口处于全屏独占，手势引擎自动放行: hwnd=0x{:X} class={}", {
            "[{}] 前台窗口处于全屏独占，手势引擎自动放行: hwnd=0x{:X} class={}",
            "[{}] 前台窗口处于全屏独占，手势引擎自动放行: hwnd=0x{:X} class={}"
        }},
        {"全屏但是生产力窗口，继续手势: hwnd=0x{:X} class={}", {
            "[{}] 全屏但是生产力窗口，继续手势: hwnd=0x{:X} class={}",
            "[{}] 全屏但是生产力窗口，继续手势: hwnd=0x{:X} class={}"
        }},
        {"GestureEngine 发生未捕获异常: {}", {
            "[{}] GestureEngine 发生未捕获异常: {}",
            "[{}] GestureEngine 发生未捕获异常: {}"
        }},
        {"GestureEngine 发生未知异常", {
            "[{}] GestureEngine 发生未知异常",
            "[{}] GestureEngine 发生未知异常"
        }},
        {"手势追踪开始: pos=({},{}) hwnd=0x{:X} class={} compositorSurface={}", {
            "[{}] 手势追踪开始: pos=({},{}) hwnd=0x{:X} class={} compositorSurface={}",
            "[{}] 手势追踪开始: pos=({},{}) hwnd=0x{:X} class={} compositorSurface={}"
        }},
        {"手势追踪开始: pos=({},{}), modifiers=0x{:02X}, edgeZone={}, trailVisible={}", {
            "[{}] 手势追踪开始: pos=({},{}), modifiers=0x{:02X}, edgeZone={}, trailVisible={}",
            "[{}] 手势追踪开始: pos=({},{}), modifiers=0x{:02X}, edgeZone={}, trailVisible={}"
        }},
        {"手势绘制超过 10 秒后松手结束 ({}ms)，红底调侃淡出", {
            "[{}] 手势绘制超过 10 秒后松手结束 ({}ms)，红底调侃淡出",
            "[{}] 手势绘制超过 10 秒后松手结束 ({}ms)，红底调侃淡出"
        }},
        {"手势追踪结束: 轨迹太短，还原为普通点击", {
            "[{}] 手势追踪结束: 轨迹太短，还原为普通点击",
            "[{}] 手势追踪结束: 轨迹太短，还原为普通点击"
        }},
        {"手势识别成功: code={}, fullCode={}, arrows={}, 点数={}, 距离={:.0f}px", {
            "[{}] 手势识别成功: code={}, fullCode={}, arrows={}, 点数={}, 距离={:.0f}px",
            "[{}] 手势识别成功: code={}, fullCode={}, arrows={}, 点数={}, 距离={:.0f}px"
        }},
        {"手势在当前窗口被禁用", {
            "[{}] 手势在当前窗口被禁用",
            "[{}] 手势在当前窗口被禁用"
        }},
        {"执行手势动作: gesture={}, matchedCode={}, action={}, profile={}", {
            "[{}] 执行手势动作: gesture={}, matchedCode={}, action={}, profile={}",
            "[{}] 执行手势动作: gesture={}, matchedCode={}, action={}, profile={}"
        }},
        {"手势选窗: mode={} start=({},{}) end=({},{}) hwnd=0x{:X} class={} exe={}", {
            "[{}] 手势选窗: mode={} start=({},{}) end=({},{}) hwnd=0x{:X} class={} exe={}",
            "[{}] 手势选窗: mode={} start=({},{}) end=({},{}) hwnd=0x{:X} class={} exe={}"
        }},
        {"手势动作开始执行(输入线程): action={}, type={}, targetHwnd=0x{:X}", {
            "[{}] 手势动作开始执行(输入线程): action={}, type={}, targetHwnd=0x{:X}",
            "[{}] 手势动作开始执行(输入线程): action={}, type={}, targetHwnd=0x{:X}"
        }},
        {"手势动作执行完毕: action={}", {
            "[{}] 手势动作执行完毕: action={}",
            "[{}] 手势动作执行完毕: action={}"
        }},
        {"手势动作执行异常: action={}, error={}", {
            "[{}] 手势动作执行异常: action={}, error={}",
            "[{}] 手势动作执行异常: action={}, error={}"
        }},
        {"手势动作执行未知异常: action={}", {
            "[{}] 手势动作执行未知异常: action={}",
            "[{}] 手势动作执行未知异常: action={}"
        }},
        {"手势动作队列已满，丢弃最旧动作", {
            "[{}] 手势动作队列已满，丢弃最旧动作",
            "[{}] 手势动作队列已满，丢弃最旧动作"
        }},
        {"手势动作开始执行(后台队列): action={}, type={}, targetHwnd=0x{:X}", {
            "[{}] 手势动作开始执行(后台队列): action={}, type={}, targetHwnd=0x{:X}",
            "[{}] 手势动作开始执行(后台队列): action={}, type={}, targetHwnd=0x{:X}"
        }},
        {"主线程延迟队列不可用，立即补发触发键点击", {
            "[{}] 主线程延迟队列不可用，立即补发触发键点击",
            "[{}] 主线程延迟队列不可用，立即补发触发键点击"
        }},
        {"手势追踪已取消", {
            "[{}] 手势追踪已取消",
            "[{}] 手势追踪已取消"
        }},
        {"指定的 Profile 不存在: {}, 使用默认 Profile", {
            "[{}] 指定的 Profile 不存在: {}, 使用默认 Profile",
            "[{}] 指定的 Profile 不存在: {}, 使用默认 Profile"
        }},
        {"从配置加载手势配置集, 数量={}", {
            "[{}] 从配置加载手势配置集, 数量={}",
            "[{}] 从配置加载手势配置集, 数量={}"
        }},
        {"手势配置已保存", {
            "[{}] 手势配置已保存",
            "[{}] 手势配置已保存"
        }},
        {"手势配置持久化失败", {
            "[{}] 手势配置持久化失败",
            "[{}] 手势配置持久化失败"
        }},
        {"手势已禁用，忽略动作: profile={}, code={}", {
            "[{}] 手势已禁用，忽略动作: profile={}, code={}",
            "[{}] 手势已禁用，忽略动作: profile={}, code={}"
        }},
        {"批量设置触发方式: profile={}, state={}", {
            "[{}] 批量设置触发方式: profile={}, state={}",
            "[{}] 批量设置触发方式: profile={}, state={}"
        }},
        {"创建默认全局手势配置集, 手势数量={}", {
            "[{}] 创建默认全局手势配置集, 手势数量={}",
            "[{}] 创建默认全局手势配置集, 手势数量={}"
        }},
        {"创建浏览器专用手势配置集, 手势数量={}", {
            "[{}] 创建浏览器专用手势配置集, 手势数量={}",
            "[{}] 创建浏览器专用手势配置集, 手势数量={}"
        }},
        {"创建桌面专属手势配置集, 手势数量={}", {
            "[{}] 创建桌面专属手势配置集, 手势数量={}",
            "[{}] 创建桌面专属手势配置集, 手势数量={}"
        }},
        {"创建任务栏专属手势配置集, 手势数量={}", {
            "[{}] 创建任务栏专属手势配置集, 手势数量={}",
            "[{}] 创建任务栏专属手势配置集, 手势数量={}"
        }},
        {"手势识别: 检测到乱晃/原地反悔操作，已自动取消手势执行", {
            "[{}] 手势识别: 检测到乱晃/原地反悔操作，已自动取消手势执行",
            "[{}] 手势识别: 检测到乱晃/原地反悔操作，已自动取消手势执行"
        }},
        {"手势识别: 轨迹太短或无有效方向段, 点数={}", {
            "[{}] 手势识别: 轨迹太短或无有效方向段, 点数={}",
            "[{}] 手势识别: 轨迹太短或无有效方向段, 点数={}"
        }},
        {"手势识别完成: code={}, arrows={}, 点数={}, 总距离={:.1f}px", {
            "[{}] 手势识别完成: code={}, arrows={}, 点数={}, 总距离={:.1f}px",
            "[{}] 手势识别完成: code={}, arrows={}, 点数={}, 总距离={:.1f}px"
        }},
        {"创建手势轨迹覆盖层窗口失败", {
            "[{}] 创建手势轨迹覆盖层窗口失败",
            "[{}] 创建手势轨迹覆盖层窗口失败"
        }},
        {"手势轨迹覆盖层初始化成功 (专用异步渲染管线已启动)", {
            "[{}] 手势轨迹覆盖层初始化成功 (专用异步渲染管线已启动)",
            "[{}] 手势轨迹覆盖层初始化成功 (专用异步渲染管线已启动)"
        }},
        {"手势轨迹覆盖层已关闭", {
            "[{}] 手势轨迹覆盖层已关闭",
            "[{}] 手势轨迹覆盖层已关闭"
        }},
        {"手势轨迹结束: overlayPoints={}, label={}", {
            "[{}] 手势轨迹结束: overlayPoints={}, label={}",
            "[{}] 手势轨迹结束: overlayPoints={}, label={}"
        }},
        {"手势轨迹表面重建: {}x{} at ({},{})", {
            "[{}] 手势轨迹表面重建: {}x{} at ({},{})",
            "[{}] 手势轨迹表面重建: {}x{} at ({},{})"
        }},
        {"手势覆盖层提交失败: {}x{} error={}", {
            "[{}] 手势覆盖层提交失败: {}x{} error={}",
            "[{}] 手势覆盖层提交失败: {}x{} error={}"
        }},
        {"创建手势轨迹窗口失败", {
            "[{}] 创建手势轨迹窗口失败",
            "[{}] 创建手势轨迹窗口失败"
        }},
        {"创建手势结果卡片窗口失败，轨迹仍可绘制", {
            "[{}] 创建手势结果卡片窗口失败，轨迹仍可绘制",
            "[{}] 创建手势结果卡片窗口失败，轨迹仍可绘制"
        }},
        {"手势覆盖层初始化完成 (原生分层窗口硬件合成加速管线已就绪)", {
            "[{}] 手势覆盖层初始化完成 (原生分层窗口硬件合成加速管线已就绪)",
            "[{}] 手势覆盖层初始化完成 (原生分层窗口硬件合成加速管线已就绪)"
        }},
        {"手势轨迹 Direct2D 帧提交失败", {
            "[{}] 手势轨迹 Direct2D 帧提交失败",
            "[{}] 手势轨迹 Direct2D 帧提交失败"
        }},
        {"手势轨迹已提交: points={}, {}x{}", {
            "[{}] 手势轨迹已提交: points={}, {}x{}",
            "[{}] 手势轨迹已提交: points={}, {}x{}"
        }},
        {"手势结果卡片 Direct2D 帧提交失败", {
            "[{}] 手势结果卡片 Direct2D 帧提交失败",
            "[{}] 手势结果卡片 Direct2D 帧提交失败"
        }},
        {"HotCornerEngine: 屏幕触发角引擎已启动", {
            "[{}] HotCornerEngine: 屏幕触发角引擎已启动",
            "[{}] HotCornerEngine: 屏幕触发角引擎已启动"
        }},
        {"HotCornerEngine: 屏幕触发角引擎已停止", {
            "[{}] HotCornerEngine: 屏幕触发角引擎已停止",
            "[{}] HotCornerEngine: 屏幕触发角引擎已停止"
        }},
        {"前台处于全屏独占，触发角自动静默: hwnd=0x{:X}", {
            "[{}] 前台处于全屏独占，触发角自动静默: hwnd=0x{:X}",
            "[{}] 前台处于全屏独占，触发角自动静默: hwnd=0x{:X}"
        }},
        {"HotCornerEngine: 触发角生效角={}, 执行命令='{}'", {
            "[{}] HotCornerEngine: 触发角生效角={}, 执行命令='{}'",
            "[{}] HotCornerEngine: 触发角生效角={}, 执行命令='{}'"
        }},
        {"HotCornerEngine: 忽略无效动作 '{}': {}", {
            "[{}] HotCornerEngine: 忽略无效动作 '{}': {}",
            "[{}] HotCornerEngine: 忽略无效动作 '{}': {}"
        }},
        {"安装鼠标钩子失败, error={}", {
            "[{}] 安装鼠标钩子失败, error={}",
            "[{}] 安装鼠标钩子失败, error={}"
        }},
        {"低级鼠标钩子已安装", {
            "[{}] 低级鼠标钩子已安装",
            "[{}] 低级鼠标钩子已安装"
        }},
        {"低级鼠标钩子已卸载", {
            "[{}] 低级鼠标钩子已卸载",
            "[{}] 低级鼠标钩子已卸载"
        }},
        {"MouseHook 发生未捕获异常: {}", {
            "[{}] MouseHook 发生未捕获异常: {}",
            "[{}] MouseHook 发生未捕获异常: {}"
        }},
        {"MouseHook 发生未知异常", {
            "[{}] MouseHook 发生未知异常",
            "[{}] MouseHook 发生未知异常"
        }},
        {"【熔断告警】鼠标钩子回调耗时 {} ms (阈值 {} ms)，触发全局熔断机制保护系统！", {
            "[{}] 【熔断告警】鼠标钩子回调耗时 {} ms (阈值 {} ms)，触发全局熔断机制保护系统！",
            "[{}] 【熔断告警】鼠标钩子回调耗时 {} ms (阈值 {} ms)，触发全局熔断机制保护系统！"
        }},
        {"GesturePlugin: 初始化手势引擎", {
            "[{}] GesturePlugin: 初始化手势引擎",
            "[{}] GesturePlugin: 初始化手势引擎"
        }},
        {"GesturePlugin: 内置命令触发轮盘菜单", {
            "[{}] GesturePlugin: 内置命令触发轮盘菜单",
            "[{}] GesturePlugin: 内置命令触发轮盘菜单"
        }},
        {"IPC: hotcorner.getSettings 查询触发角配置", {
            "[{}] IPC: hotcorner.getSettings 查询触发角配置",
            "[{}] IPC: hotcorner.getSettings 查询触发角配置"
        }},
        {"IPC: hotcorner.updateSettings 更新触发角配置", {
            "[{}] IPC: hotcorner.updateSettings 更新触发角配置",
            "[{}] IPC: hotcorner.updateSettings 更新触发角配置"
        }},
        {"IPC: radialmenu.getItems 查询轮盘菜单项", {
            "[{}] IPC: radialmenu.getItems 查询轮盘菜单项",
            "[{}] IPC: radialmenu.getItems 查询轮盘菜单项"
        }},
        {"IPC: radialmenu.updateItems 更新轮盘菜单项", {
            "[{}] IPC: radialmenu.updateItems 更新轮盘菜单项",
            "[{}] IPC: radialmenu.updateItems 更新轮盘菜单项"
        }},
        {"radialmenu.updateItems: 缺少 items 数组参数", {
            "[{}] radialmenu.updateItems: 缺少 items 数组参数",
            "[{}] radialmenu.updateItems: 缺少 items 数组参数"
        }},
        {"radialmenu.updateItems: 已更新 {} 个菜单项", {
            "[{}] radialmenu.updateItems: 已更新 {} 个菜单项",
            "[{}] radialmenu.updateItems: 已更新 {} 个菜单项"
        }},
        {"GesturePlugin: 从配置加载 {} 个轮盘菜单项", {
            "[{}] GesturePlugin: 从配置加载 {} 个轮盘菜单项",
            "[{}] GesturePlugin: 从配置加载 {} 个轮盘菜单项"
        }},
        {"GesturePlugin: 使用默认轮盘菜单项, 数量={}", {
            "[{}] GesturePlugin: 使用默认轮盘菜单项, 数量={}",
            "[{}] GesturePlugin: 使用默认轮盘菜单项, 数量={}"
        }},
        {"GesturePlugin: 手势引擎启动失败", {
            "[{}] GesturePlugin: 手势引擎启动失败",
            "[{}] GesturePlugin: 手势引擎启动失败"
        }},
        {"GesturePlugin: 卸载手势引擎", {
            "[{}] GesturePlugin: 卸载手势引擎",
            "[{}] GesturePlugin: 卸载手势引擎"
        }},
        {"手势轮盘高 DPI 渲染资源创建失败", {
            "[{}] 手势轮盘高 DPI 渲染资源创建失败",
            "[{}] 手势轮盘高 DPI 渲染资源创建失败"
        }},
        {"RadialMenu: 忽略未知命令 '{}'", {
            "[{}] RadialMenu: 忽略未知命令 '{}'",
            "[{}] RadialMenu: 忽略未知命令 '{}'"
        }},
        {"作用域规则已加载, 规则数量={}", {
            "[{}] 作用域规则已加载, 规则数量={}",
            "[{}] 作用域规则已加载, 规则数量={}"
        }},
        {"ScopeRuleEngine: 缓存已失效", {
            "[{}] ScopeRuleEngine: 缓存已失效",
            "[{}] ScopeRuleEngine: 缓存已失效"
        }},
        {"成功从安装包初始配置同步模块开关并写入当前用户配置", {
            "[{}] 成功从安装包初始配置同步模块开关并写入当前用户配置",
            "[{}] 成功从安装包初始配置同步模块开关并写入当前用户配置"
        }},
        {"前台处于全屏独占应用，自动免打扰跳过搜索窗口呼出: hwnd=0x{:X}", {
            "[{}] 前台处于全屏独占应用，自动免打扰跳过搜索窗口呼出: hwnd=0x{:X}",
            "[{}] 前台处于全屏独占应用，自动免打扰跳过搜索窗口呼出: hwnd=0x{:X}"
        }},
        {"OCR 引擎已就绪 (Windows.Media.Ocr)", {
            "[{}] OCR 引擎已就绪 (Windows.Media.Ocr)",
            "[{}] OCR 引擎已就绪 (Windows.Media.Ocr)"
        }},
        {"OCR 引擎初始化异常: {}", {
            "[{}] OCR 引擎初始化异常: {}",
            "[{}] OCR 引擎初始化异常: {}"
        }},
        {"OCR 引擎已关闭", {
            "[{}] OCR 引擎已关闭",
            "[{}] OCR 引擎已关闭"
        }},
        {"OCR 输入图像为空", {
            "[{}] OCR 输入图像为空",
            "[{}] OCR 输入图像为空"
        }},
        {"OCR 不可用: 无语言包", {
            "[{}] OCR 不可用: 无语言包",
            "[{}] OCR 不可用: 无语言包"
        }},
        {"OCR: 不支持的图像通道数: {}", {
            "[{}] OCR: 不支持的图像通道数: {}",
            "[{}] OCR: 不支持的图像通道数: {}"
        }},
        {"OCR 完成, 识别行数: {}", {
            "[{}] OCR 完成, 识别行数: {}",
            "[{}] OCR 完成, 识别行数: {}"
        }},
        {"OCR WinRT 错误: 0x{:08X} {}", {
            "[{}] OCR WinRT 错误: 0x{:08X} {}",
            "[{}] OCR WinRT 错误: 0x{:08X} {}"
        }},
        {"OCR 异常: {}", {
            "[{}] OCR 异常: {}",
            "[{}] OCR 异常: {}"
        }},
        {"OCR 读取图片失败: {} ({})", {
            "[{}] OCR 读取图片失败: {} ({})",
            "[{}] OCR 读取图片失败: {} ({})"
        }},
        {"OCR: 无法加载图片 {}", {
            "[{}] OCR: 无法加载图片 {}",
            "[{}] OCR: 无法加载图片 {}"
        }},
        {"SearchPlugin: 无法取得当前用户 SID，拒绝创建搜索 IPC 端点", {
            "[{}] SearchPlugin: 无法取得当前用户 SID，拒绝创建搜索 IPC 端点",
            "[{}] SearchPlugin: 无法取得当前用户 SID，拒绝创建搜索 IPC 端点"
        }},
        {"SearchPlugin: 无法安全生成或保存搜索 IPC 端点令牌", {
            "[{}] SearchPlugin: 无法安全生成或保存搜索 IPC 端点令牌",
            "[{}] SearchPlugin: 无法安全生成或保存搜索 IPC 端点令牌"
        }},
        {"SearchPlugin: 无法查询 SCM 搜索服务，回退便携进程, error={}", {
            "[{}] SearchPlugin: 无法查询 SCM 搜索服务，回退便携进程, error={}",
            "[{}] SearchPlugin: 无法查询 SCM 搜索服务，回退便携进程, error={}"
        }},
        {"SearchPlugin: SCM 无法为本用户启动（无启动权限或服务已停止），回退便携索引进程", {
            "[{}] SearchPlugin: SCM 无法为本用户启动（无启动权限或服务已停止），回退便携索引进程",
            "[{}] SearchPlugin: SCM 无法为本用户启动（无启动权限或服务已停止），回退便携索引进程"
        }},
        {"SearchPlugin: SCM 服务未提供当前用户端点，拒绝启动第二个索引进程, error={}", {
            "[{}] SearchPlugin: SCM 服务未提供当前用户端点，拒绝启动第二个索引进程, error={}",
            "[{}] SearchPlugin: SCM 服务未提供当前用户端点，拒绝启动第二个索引进程, error={}"
        }},
        {"SearchPlugin: 已请求索引服务停机", {
            "[{}] SearchPlugin: 已请求索引服务停机",
            "[{}] SearchPlugin: 已请求索引服务停机"
        }},
        {"SearchPlugin: 索引服务停机请求失败, error={}", {
            "[{}] SearchPlugin: 索引服务停机请求失败, error={}",
            "[{}] SearchPlugin: 索引服务停机请求失败, error={}"
        }},
        {"SearchPlugin: 初始化搜索引擎", {
            "[{}] SearchPlugin: 初始化搜索引擎",
            "[{}] SearchPlugin: 初始化搜索引擎"
        }},
        {"SearchPlugin: 无法解析 JSON 结果", {
            "[{}] SearchPlugin: 无法解析 JSON 结果",
            "[{}] SearchPlugin: 无法解析 JSON 结果"
        }},
        {"SearchPlugin: 管道调用超时或返回空, error={}", {
            "[{}] SearchPlugin: 管道调用超时或返回空, error={}",
            "[{}] SearchPlugin: 管道调用超时或返回空, error={}"
        }},
        {"SearchPlugin: 重命名失败 {} -> {}, error={}", {
            "[{}] SearchPlugin: 重命名失败 {} -> {}, error={}",
            "[{}] SearchPlugin: 重命名失败 {} -> {}, error={}"
        }},
        {"SearchPlugin: 关闭", {
            "[{}] SearchPlugin: 关闭",
            "[{}] SearchPlugin: 关闭"
        }},
        {"创建/更新托盘图标未成功，启动自愈定时器, error={}", {
            "[{}] 创建/更新托盘图标未成功，启动自愈定时器, error={}",
            "[{}] 创建/更新托盘图标未成功，启动自愈定时器, error={}"
        }},
        {"托盘菜单回调异常: menuId={}, error={}", {
            "[{}] 托盘菜单回调异常: menuId={}, error={}",
            "[{}] 托盘菜单回调异常: menuId={}, error={}"
        }},
        {"SearchWindow: 打包 UI 缺失，已拒绝连接开发服务器", {
            "[{}] SearchWindow: 打包 UI 缺失，已拒绝连接开发服务器",
            "[{}] SearchWindow: 打包 UI 缺失，已拒绝连接开发服务器"
        }},
        {"创建设置窗口失败", {
            "[{}] 创建设置窗口失败",
            "[{}] 创建设置窗口失败"
        }},
        {"CreateWindowExW 失败, error={}", {
            "[{}] CreateWindowExW 失败, error={}",
            "[{}] CreateWindowExW 失败, error={}"
        }},
        {"Win32 设置窗口已创建, size={}x{} on DPI {}", {
            "[{}] Win32 设置窗口已创建, size={}x{} on DPI {}",
            "[{}] Win32 设置窗口已创建, size={}x{} on DPI {}"
        }},
        {"WebView2 环境获取失败, hr=0x{:08X}", {
            "[{}] WebView2 环境获取失败, hr=0x{:08X}",
            "[{}] WebView2 环境获取失败, hr=0x{:08X}"
        }},
        {"WebView2 控件创建已因窗口销毁而取消", {
            "[{}] WebView2 控件创建已因窗口销毁而取消",
            "[{}] WebView2 控件创建已因窗口销毁而取消"
        }},
        {"WebView2 控件创建失败, hr=0x{:08X}", {
            "[{}] WebView2 控件创建失败, hr=0x{:08X}",
            "[{}] WebView2 控件创建失败, hr=0x{:08X}"
        }},
        {"WebView2 控件创建请求失败, hr=0x{:08X}", {
            "[{}] WebView2 控件创建请求失败, hr=0x{:08X}",
            "[{}] WebView2 控件创建请求失败, hr=0x{:08X}"
        }},
        {"WebView2 控件就绪", {
            "[{}] WebView2 控件就绪",
            "[{}] WebView2 控件就绪"
        }},
        {"WebMessageReceived 处理异常: {}", {
            "[{}] WebMessageReceived 处理异常: {}",
            "[{}] WebMessageReceived 处理异常: {}"
        }},
        {"WebMessageReceived 处理未知异常", {
            "[{}] WebMessageReceived 处理未知异常",
            "[{}] WebMessageReceived 处理未知异常"
        }},
        {"WebView2 导航成功: {}", {
            "[{}] WebView2 导航成功: {}",
            "[{}] WebView2 导航成功: {}"
        }},
        {"WebView2 导航失败, status={}", {
            "[{}] WebView2 导航失败, status={}",
            "[{}] WebView2 导航失败, status={}"
        }},
        {"WebView2 正在加载前端 UI: {}", {
            "[{}] WebView2 正在加载前端 UI: {}",
            "[{}] WebView2 正在加载前端 UI: {}"
        }},
        {"打包 UI 缺失: {}", {
            "[{}] 打包 UI 缺失: {}",
            "[{}] 打包 UI 缺失: {}"
        }},
        {"成功读取到动态开发服务器地址: {}", {
            "[{}] 成功读取到动态开发服务器地址: {}",
            "[{}] 成功读取到动态开发服务器地址: {}"
        }},
        {"前台处于全屏独占应用，自动免打扰跳过鼠标聚光灯触发: hwnd=0x{:X}", {
            "[{}] 前台处于全屏独占应用，自动免打扰跳过鼠标聚光灯触发: hwnd=0x{:X}",
            "[{}] 前台处于全屏独占应用，自动免打扰跳过鼠标聚光灯触发: hwnd=0x{:X}"
        }},
        {"ToastOverlay 窗口过程异常: {}", {
            "[{}] ToastOverlay 窗口过程异常: {}",
            "[{}] ToastOverlay 窗口过程异常: {}"
        }},
        {"ToastOverlay 窗口过程未知异常", {
            "[{}] ToastOverlay 窗口过程未知异常",
            "[{}] ToastOverlay 窗口过程未知异常"
        }},
        {"TrayWindow: 打包 UI 缺失，已拒绝连接开发服务器", {
            "[{}] TrayWindow: 打包 UI 缺失，已拒绝连接开发服务器",
            "[{}] TrayWindow: 打包 UI 缺失，已拒绝连接开发服务器"
        }},
        {"TrayWindow: WebView2 导航失败, status={}", {
            "[{}] TrayWindow: WebView2 导航失败, status={}",
            "[{}] TrayWindow: WebView2 导航失败, status={}"
        }},
        {"WebView2 环境获取回调异常: {}", {
            "[{}] WebView2 环境获取回调异常: {}",
            "[{}] WebView2 环境获取回调异常: {}"
        }},
        {"WebView2 环境获取回调未知异常", {
            "[{}] WebView2 环境获取回调未知异常",
            "[{}] WebView2 环境获取回调未知异常"
        }},
        {"共享 WebView2 环境创建失败, hr=0x{:08X}", {
            "[{}] 共享 WebView2 环境创建失败, hr=0x{:08X}",
            "[{}] 共享 WebView2 环境创建失败, hr=0x{:08X}"
        }},
        {"共享 WebView2 环境已就绪", {
            "[{}] 共享 WebView2 环境已就绪",
            "[{}] 共享 WebView2 环境已就绪"
        }},
        {"WebView2 环境就绪回调异常: {}", {
            "[{}] WebView2 环境就绪回调异常: {}",
            "[{}] WebView2 环境就绪回调异常: {}"
        }},
        {"WebView2 环境就绪回调未知异常", {
            "[{}] WebView2 环境就绪回调未知异常",
            "[{}] WebView2 环境就绪回调未知异常"
        }},
        {"已拒绝非可信 WebView 消息来源", {
            "[{}] 已拒绝非可信 WebView 消息来源",
            "[{}] 已拒绝非可信 WebView 消息来源"
        }},
        {"已阻止 WebView 导航到非可信来源", {
            "[{}] 已阻止 WebView 导航到非可信来源",
            "[{}] 已阻止 WebView 导航到非可信来源"
        }},
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
