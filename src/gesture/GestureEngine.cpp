// ─────────────────────────────────────────────────────────────────────────────
// GestureEngine.cpp — 手势引擎实现
// ─────────────────────────────────────────────────────────────────────────────

#include "gesture/GestureEngine.h"
#include "gesture/GestureTrailOverlay.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"
#include "core/utils/WinUtils.h"
#include "core/config/ConfigManager.h"
#include "core/events/MainThreadDispatcher.h"

#include <algorithm>
#include <thread>

namespace easy::gesture {

GestureEngine& GestureEngine::instance() {
    static GestureEngine inst;
    return inst;
}

GestureEngine::GestureEngine() {
    // 创建默认 Profile 与特殊目标专属 Profile
    m_profiles["default"] = GestureProfile::createDefaultGlobal();
    m_profiles["browser"] = GestureProfile::createBrowserProfile();
    m_profiles["special_desktop"] = GestureProfile::createDesktopProfile();
    m_profiles["special_taskbar"] = GestureProfile::createTaskbarProfile();
}

bool GestureEngine::start() {
    easy::core::TraceId::Scope scope;

    if (!m_actionWorker.joinable()) {
        m_actionWorker = std::jthread(
            [this](std::stop_token token) { actionWorkerLoop(token); });
    }

    // 安装鼠标钩子
    auto& hook = MouseHook::instance();
    hook.setEventCallback([this](const MouseEvent& event) -> bool {
        return onMouseEvent(event);
    });

    if (!hook.install()) {
        LOG_ERROR("手势引擎启动失败: 无法安装鼠标钩子");
        m_actionWorker.request_stop();
        m_actionCv.notify_all();
        m_actionWorker.join();
        return false;
    }
    hook.setPaused(m_paused.load());

    // 初始化手势轨迹覆盖层
    auto& trail = GestureTrailOverlay::instance();
    if (!trail.initialize(GetModuleHandleW(nullptr))) {
        m_trailVisible = false;
        LOG_WARN("手势轨迹覆盖层不可用，手势识别与动作执行将继续运行");
    }

    m_state = GestureState::Idle;
    const auto defaultProfile = getProfile("default");
    const auto browserProfile = getProfile("browser");
    LOG_INFO("手势引擎已启动, 默认Profile手势数={}, 浏览器Profile手势数={}",
             defaultProfile ? defaultProfile->getMappings().size() : 0,
             browserProfile ? browserProfile->getMappings().size() : 0);
    return true;
}

void GestureEngine::stop() {
    MouseHook::instance().uninstall();
    if (m_actionWorker.joinable()) {
        m_actionWorker.request_stop();
        m_actionCv.notify_all();
        m_actionWorker.join();
    }
    {
        std::lock_guard lock(m_actionMutex);
        m_actionQueue.clear();
    }
    GestureTrailOverlay::instance().shutdown();
    m_state = GestureState::Idle;
    LOG_INFO("手势引擎已停止");
}

bool GestureEngine::setPaused(bool paused) {
    const bool previous = m_paused.exchange(paused);
    const bool changed = previous != paused;
    MouseHook::instance().setPaused(paused);
    if (!changed) return true;

    PauseChangedCallback callback;
    {
        std::lock_guard lock(m_callbackMutex);
        callback = m_pauseChangedCallback;
    }
    if (callback) {
        try {
            if (!callback(paused)) {
                m_paused = previous;
                MouseHook::instance().setPaused(previous);
                LOG_ERROR("手势暂停状态持久化失败，已回滚: paused={}", previous);
                return false;
            }
        } catch (const std::exception& e) {
            m_paused = previous;
            MouseHook::instance().setPaused(previous);
            LOG_ERROR("手势暂停状态回调异常，已回滚: {}", e.what());
            return false;
        } catch (...) {
            m_paused = previous;
            MouseHook::instance().setPaused(previous);
            LOG_ERROR("手势暂停状态回调发生未知异常，已回滚");
            return false;
        }
    }
    if (paused) {
        {
            std::lock_guard lock(m_mutex);
            if (m_state.load(std::memory_order_relaxed) == GestureState::Tracking) {
                cancelTracking();
            }
        }
        // 深度释放手势全屏位图与 D2D 渲染资源
        GestureTrailOverlay::instance().hide();
        GestureTrailOverlay::instance().releaseD2DResources();
    }
    LOG_INFO("手势引擎暂停状态: paused={}", paused);
    return true;
}

void GestureEngine::setPauseChangedCallback(PauseChangedCallback callback) {
    std::lock_guard lock(m_callbackMutex);
    m_pauseChangedCallback = std::move(callback);
}

void GestureEngine::setTriggerButton(const std::string& button) {
    if (button == "middle") {
        m_triggerDown = MouseEventType::MiddleDown;
        m_triggerUp = MouseEventType::MiddleUp;
        MouseHook::instance().setTriggerMode(TriggerMode::MiddleOnly);
    } else if (button == "both" || button == "all") {
        m_triggerDown = MouseEventType::RightDown;
        m_triggerUp = MouseEventType::RightUp;
        MouseHook::instance().setTriggerMode(TriggerMode::Both);
    } else {
        m_triggerDown = MouseEventType::RightDown;
        m_triggerUp = MouseEventType::RightUp;
        MouseHook::instance().setTriggerMode(TriggerMode::RightOnly);
    }
    LOG_INFO("手势触发按钮已设置: {}", triggerButton());
}

std::string GestureEngine::triggerButton() const {
    const auto mode = MouseHook::instance().triggerMode();
    if (mode == TriggerMode::Both) return "both";
    if (mode == TriggerMode::MiddleOnly) return "middle";
    return "right";
}

void GestureEngine::setTrailVisible(bool visible) {
    m_trailVisible = visible;
    if (!visible) {
        auto& trail = GestureTrailOverlay::instance();
        trail.hide();
        trail.releaseD2DResources();
    }
    LOG_INFO("手势轨迹显示状态: visible={}", visible);
}

void GestureEngine::setAutoBypassFullscreen(bool enable) {
    m_autoBypassFullscreen = enable;
    LOG_INFO("手势全屏自动免打扰状态: enable={}", enable);
}

void GestureEngine::setProfile(const std::string& name, const GestureProfile& profile) {
    std::unique_lock lock(m_profileMutex);
    m_profiles.insert_or_assign(name, profile);
    LOG_INFO("设置手势配置集: name={}, 手势数={}", name, profile.getMappings().size());
}

std::optional<GestureProfile> GestureEngine::getProfile(const std::string& name) const {
    std::shared_lock lock(m_profileMutex);
    auto it = m_profiles.find(name);
    return it != m_profiles.end() ? std::optional<GestureProfile>(it->second) : std::nullopt;
}

std::vector<GestureProfile> GestureEngine::getProfiles() const {
    std::vector<GestureProfile> profiles;
    {
        std::shared_lock lock(m_profileMutex);
        profiles.reserve(m_profiles.size());
        for (const auto& [name, profile] : m_profiles) profiles.push_back(profile);
    }
    std::sort(profiles.begin(), profiles.end(), [](const auto& left, const auto& right) {
        return left.name() < right.name();
    });
    return profiles;
}

bool GestureEngine::removeProfile(const std::string& name) {
    std::unique_lock lock(m_profileMutex);
    return m_profiles.erase(name) > 0;
}

void GestureEngine::setRecognizerConfig(const RecognizerConfig& config) {
    m_recognizer.setConfig(config);
}

void GestureEngine::setTrailCallback(TrailRenderCallback callback) {
    std::lock_guard lock(m_callbackMutex);
    m_trailCallback = std::move(callback);
}

// ── 鼠标事件处理管道 ─────────────────────────────────────────────────────────

bool GestureEngine::onMouseEvent(const MouseEvent& event) {
    try {
        std::lock_guard lock(m_mutex);

        if (m_paused.load()) return false;

        switch (m_state.load()) {
            case GestureState::Idle:
                if (event.type == m_triggerDown.load()) {
                    // 每次触发键按下 = 一次新的用户操作, 开启全新 TraceId 贯穿整条链路
                    m_gestureTraceId = easy::core::TraceId::begin();

                    HWND hwnd = event.foregroundWindow;
                    LOG_TRACE("触发键按下: trigger={}, pos=({},{}), hwnd=0x{:X}",
                              triggerButton(), event.position.x, event.position.y,
                              reinterpret_cast<uintptr_t>(hwnd));

                    // 检查当前窗口是否在黑名单（仅触发时读取配置，避免移动时频繁消耗性能）
                    auto exceptions = easy::core::ConfigManager::instance().get<nlohmann::json>(
                        "/gesture/exceptions", nlohmann::json::array());
                    bool disabled = false;
                    std::string exeName;
                    std::string className;

                    // 全屏免打扰模式：检测前台窗口是否处于全屏独占状态
                    if (m_autoBypassFullscreen.load() && easy::core::WinUtils::isWindowFullscreen(hwnd)) {
                        LOG_INFO("前台窗口处于全屏模式，手势引擎自动放行: hwnd=0x{:X}", reinterpret_cast<uintptr_t>(hwnd));
                        return false;
                    }

                    // 绝大多数用户没有例外规则。只有确实需要匹配时才跨进程查询
                    // 可执行文件和窗口类，普通右键不再承担这项开销。
                    if (exceptions.is_array() && !exceptions.empty()) {
                        exeName = hwnd ? easy::core::WinUtils::getProcessNameFromWindow(hwnd) : "";
                        className = hwnd
                            ? easy::core::WinUtils::wstringToUtf8(
                                  easy::core::WinUtils::getWindowClassName(hwnd))
                            : "";
                        const auto normalizedExe = easy::core::WinUtils::toLower(exeName);
                        for (const auto& rule : exceptions) {
                            if (!rule.is_object()) continue;
                            const std::string ruleType = rule.value("type", "");
                            const std::string ruleValue = rule.value("value", "");
                            if (ruleType == "process" &&
                                normalizedExe == easy::core::WinUtils::toLower(ruleValue)) {
                                disabled = true;
                                break;
                            }
                            if (ruleType == "class" && className == ruleValue) {
                                disabled = true;
                                break;
                            }
                        }
                    }
                    if (disabled) {
                        LOG_INFO("窗口在手势黑名单, 不拦截: process='{}', class='{}'", exeName, className);
                        return false;  // 不拦截 → 右键照常工作
                    }

                    beginTracking(event);
                    return true;  // 拦截触发键按下, 进入手势追踪
                }
                break;

            case GestureState::Tracking:
                if (event.type == MouseEventType::Move) {
                    updateTracking(event);
                    // 不拦截移动: 否则会吞掉光标移动, 导致右键拖动手势时鼠标"卡死不动"。
                    // 触发键的按下已被吞掉, 底层应用只会收到无按键的 hover 移动, 无副作用。
                    return false;
                } else if (event.type == m_activeTriggerUp) {
                    endTracking(event);
                    return true; // 拦截触发按键的抬起事件
                } else if (event.type == MouseEventType::LeftDown || event.type == MouseEventType::LeftUp || event.type == MouseEventType::RightDown) {
                    // 如果在手势过程中按下了其他键（如左键），立即取消手势并放行按键
                    if (event.type != m_activeTriggerDown) {
                        cancelTracking();
                        return false;
                    }
                }
                return false;

            case GestureState::Executing:
                // 动作执行中，忽略事件但不一定拦截，如果拦截可能会影响脚本的输入注入
                break;
        }
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("GestureEngine 发生未捕获异常: {}", e.what());
        return false;
    } catch (...) {
        LOG_ERROR("GestureEngine 发生未知异常");
        return false;
    }
}

void GestureEngine::beginTracking(const MouseEvent& event) {
    m_activeTriggerDown = m_triggerDown.load();
    m_activeTriggerUp = m_triggerUp.load();
    m_trackingStartTime = std::chrono::steady_clock::now();
    m_recognizer.reset();
    m_recognizer.addPoint(event.position.x, event.position.y);

    // 优先获取光标下方的顶层窗口
    POINT pt = { event.position.x, event.position.y };
    HWND hwndUnderCursor = WindowFromPoint(pt);
    if (hwndUnderCursor) {
        HWND root = GetAncestor(hwndUnderCursor, GA_ROOT);
        if (root) hwndUnderCursor = root;
    }
    m_gestureStartWindow = hwndUnderCursor ? hwndUnderCursor : event.foregroundWindow;
    m_gestureModifiers = event.modifiers;  // 记录手势开始时的修饰键状态
    m_activeProfile = resolveProfile(m_gestureStartWindow); // 一次性预解析 Profile 缓存
    m_lastRecognizedDirections.clear();
    m_state = GestureState::Tracking;

    // 开始轨迹可视化
    if (m_trailVisible.load()) {
        auto& trail = GestureTrailOverlay::instance();
        trail.beginTrail();
        trail.addPoint(static_cast<float>(event.position.x), static_cast<float>(event.position.y));
    }

    LOG_TRACE("手势追踪开始: pos=({},{}), modifiers=0x{:02X}, trailVisible={}",
              event.position.x, event.position.y, m_gestureModifiers, m_trailVisible.load());
}

void GestureEngine::updateTracking(const MouseEvent& event) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_trackingStartTime).count();

    m_recognizer.addPoint(event.position.x, event.position.y);

    // 实时轨迹与按键回显样式动作可视化
    if (m_trailVisible.load()) {
        auto& trail = GestureTrailOverlay::instance();
        trail.addPoint(
            static_cast<float>(event.position.x),
            static_cast<float>(event.position.y)
        );

        if (elapsed >= 15000) {
            // 连续画了 15 秒仍未松手：弹出红底 3 个大圆点调侃状态，并持续停留，直到用户真正松手
            trail.setRecognized(false);
            trail.setLiveAction("•••");
        } else {
            const auto dirs = m_recognizer.currentDirections();
            if (dirs != m_lastRecognizedDirections) {
                m_lastRecognizedDirections = dirs;

                std::string liveLabel;
                if (!dirs.empty()) {
                    std::string modPrefix;
                    if (m_gestureModifiers & MOUSE_MOD_CTRL)  modPrefix += "Ctrl+";
                    if (m_gestureModifiers & MOUSE_MOD_ALT)   modPrefix += "Alt+";
                    if (m_gestureModifiers & MOUSE_MOD_SHIFT) modPrefix += "Shift+";

                    std::string bareCode = directionsToCode(dirs);
                    std::string fullCode = modPrefix + bareCode;

                    std::optional<GestureAction> action;
                    if (m_activeProfile) {
                        if (!modPrefix.empty()) {
                            action = m_activeProfile->findAction(fullCode);
                            if (!action && m_activeProfile->name() != "default") {
                                if (const auto fallback = getProfile("default")) {
                                    action = fallback->findAction(fullCode);
                                }
                            }
                        }
                        if (!action) {
                            action = m_activeProfile->findAction(bareCode);
                            if (!action && m_activeProfile->name() != "default") {
                                if (const auto fallback = getProfile("default")) {
                                    action = fallback->findAction(bareCode);
                                }
                            }
                        }
                    }

                    if (action) {
                        // 识别成功：轨迹变为主体色，Toast 提示动作名称
                        trail.setRecognized(true);
                        liveLabel = action->name;
                    } else {
                        // 普通未识别到手势：轨迹保持高阶银灰色，不需要任何 Toast 提示
                        trail.setRecognized(false);
                        liveLabel.clear();
                    }
                } else {
                    trail.setRecognized(false);
                }
                trail.setLiveAction(liveLabel);
            }
        }
    }

    // 回调（如有注册）
    TrailRenderCallback callback;
    {
        std::lock_guard lock(m_callbackMutex);
        callback = m_trailCallback;
    }
    if (callback) {
        auto dirs = m_recognizer.currentDirections();
        callback({}, dirs);
    }
}

void GestureEngine::endTracking(const MouseEvent& event) {
    MouseHook::instance().resetTriggerState();
    m_activeProfile.reset();
    m_lastRecognizedDirections.clear();

    // 恢复本次手势的 TraceId (按下/移动/抬起跨多次钩子回调, 期间可能被其它操作改写)
    easy::core::TraceId::setCurrent(m_gestureTraceId);

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_trackingStartTime).count();

    // 如果连续绘制超过 15 秒用户才松手：结束手势，红底 3 个大圆点闪现并平滑淡出，不执行任何动作
    if (elapsed >= 15000) {
        LOG_WARN("手势绘制超过 15 秒后松手结束 ({}ms)，红底调侃淡出", elapsed);
        m_state = GestureState::Idle;
        if (m_trailVisible.load()) {
            GestureTrailOverlay::instance().setRecognized(false);
            GestureTrailOverlay::instance().endTrail("•••");
        }
        reinjectTriggerClick();
        return;
    }

    m_recognizer.addPoint(event.position.x, event.position.y);

    auto result = m_recognizer.finalize();

    if (!result || !result->isValid()) {
        // 轨迹太短 → 视为普通点击: 触发键按下已被吞掉, 这里补发一次点击, 让右键菜单正常弹出
        m_state = GestureState::Idle;
        if (m_trailVisible.load()) GestureTrailOverlay::instance().hide();
        reinjectTriggerClick();
        LOG_TRACE("手势追踪结束: 轨迹太短，还原为普通点击");
        return;
    }

    // 生成带修饰键前缀的手势编码 (如 "Ctrl+L"、"Alt+D-R"、"Ctrl+Shift+U")
    std::string modPrefix;
    if (m_gestureModifiers & MOUSE_MOD_CTRL)  modPrefix += "Ctrl+";
    if (m_gestureModifiers & MOUSE_MOD_ALT)   modPrefix += "Alt+";
    if (m_gestureModifiers & MOUSE_MOD_SHIFT) modPrefix += "Shift+";
    std::string fullCode = modPrefix + result->code;  // 带修饰键的完整编码
    std::string bareCode = result->code;               // 无修饰键的纯方向编码

    LOG_INFO("手势识别成功: code={}, fullCode={}, arrows={}, 点数={}, 距离={:.0f}px",
             bareCode, fullCode, result->toArrowString(), result->rawPoints.size(), result->totalDistance);

    // 查找适用的 Profile
    auto profile = resolveProfile(m_gestureStartWindow);
    if (!profile) {
        // 手势在当前窗口被禁用 → 还原为普通点击
        m_state = GestureState::Idle;
        if (m_trailVisible.load()) GestureTrailOverlay::instance().hide();
        reinjectTriggerClick();
        LOG_DEBUG("手势在当前窗口被禁用");
        return;
    }

    // 查找动作: 先精确匹配带修饰键的编码，再 fallback 到无修饰键编码
    std::optional<GestureAction> action;
    std::string matchedCode = fullCode;

    if (!modPrefix.empty()) {
        action = profile->findAction(fullCode);
        if (!action && profile->name() != "default") {
            if (const auto fallback = getProfile("default")) {
                action = fallback->findAction(fullCode);
            }
        }
    }

    // 带修饰键未匹配时，fallback 到纯方向编码
    if (!action) {
        matchedCode = bareCode;
        action = profile->findAction(bareCode);
        if (!action && profile->name() != "default") {
            if (const auto fallback = getProfile("default")) {
                action = fallback->findAction(bareCode);
            }
        }
    }

    if (action) {
        LOG_INFO("执行手势动作: gesture={}, matchedCode={}, action={}, profile={}",
                 result->toArrowString(), matchedCode, action->name, profile->name());

        // 显示轨迹结果（仅显示手势动作名称，干净清晰）
        std::string resultLabel = action->name;
        if (m_trailVisible.load()) {
            GestureTrailOverlay::instance().endTrail(resultLabel);
        }

        // 关键: 动作执行 (SendInput / Lua / ShellExecute / 弹窗) 可能耗时甚至阻塞，
        // 而本函数运行在 WH_MOUSE_LL 低级钩子回调里（主线程消息泵上）。若在此同步执行，
        // 超过 LowLevelHooksTimeout 会被系统静默移除钩子，Lua 的 MessageBox 更会冻结全局输入。
        // 因此把动作放到分离线程异步执行。GestureAction 可拷贝，按值捕获保证生命周期安全。
        // 把 TraceId 一并带入线程, 让动作执行日志与本次手势串在同一条链路上。
        
        // 解析手势结束时鼠标光标下方的顶层窗口
        POINT endPt = { event.position.x, event.position.y };
        HWND targetWnd = WindowFromPoint(endPt);
        if (targetWnd) {
            HWND root = GetAncestor(targetWnd, GA_ROOT);
            if (root) targetWnd = root;
        }
        if (!targetWnd) targetWnd = m_gestureStartWindow;
        if (!targetWnd) targetWnd = event.foregroundWindow;
        if (!targetWnd) targetWnd = GetForegroundWindow();

        enqueueAction(*action, m_gestureTraceId, targetWnd);
    } else {
        // 未找到手势映射：直接隐藏，不显示任何 Toast
        LOG_DEBUG("未找到手势映射: fullCode={}, bareCode={}", fullCode, bareCode);
        if (m_trailVisible.load()) {
            GestureTrailOverlay::instance().hide();
        }
    }

    m_state = GestureState::Idle;
}

void GestureEngine::enqueueAction(GestureAction action, std::string traceId, HWND targetWindow) {
    {
        std::lock_guard lock(m_actionMutex);
        // Bound the queue so a stuck external action cannot grow memory forever.
        if (m_actionQueue.size() >= 32) {
            LOG_WARN("手势动作队列已满，丢弃最旧动作");
            m_actionQueue.pop_front();
        }
        m_actionQueue.push_back({std::move(action), std::move(traceId), targetWindow});
    }
    m_actionCv.notify_one();
}

void GestureEngine::actionWorkerLoop(std::stop_token stopToken) {
    while (!stopToken.stop_requested()) {
        ActionJob job;
        {
            std::unique_lock lock(m_actionMutex);
            if (!m_actionCv.wait(lock, stopToken,
                                 [this]() { return !m_actionQueue.empty(); })) {
                break;
            }
            job = std::move(m_actionQueue.front());
            m_actionQueue.pop_front();
        }

        easy::core::TraceId::setCurrent(job.traceId);
        LOG_INFO("手势动作开始执行(后台队列): action={}, type={}, targetHwnd=0x{:X}",
                 job.action.name, static_cast<int>(job.action.type),
                 reinterpret_cast<uintptr_t>(job.targetWindow));
        try {
            job.action.execute(job.targetWindow);
            LOG_INFO("手势动作执行完毕: action={}", job.action.name);
        } catch (const std::exception& e) {
            LOG_ERROR("手势动作执行异常: action={}, error={}", job.action.name, e.what());
        } catch (...) {
            LOG_ERROR("手势动作执行未知异常: action={}", job.action.name);
        }
    }
}

// 把被吞掉的触发键点击补发出去 (注入事件会被 MouseHook 忽略, 不会再次触发手势)。
// 用于"没有有效手势/未绑定动作/窗口禁用"时, 让右键(或中键)菜单等正常工作。
void GestureEngine::reinjectTriggerClick() {
    const bool right = (m_activeTriggerDown == MouseEventType::RightDown);
    const std::string traceId = m_gestureTraceId;
    auto inject = [right, traceId]() {
        easy::core::TraceId::setCurrent(traceId);
        LOG_TRACE("无手势, 补发{}键点击以还原正常菜单", right ? "右" : "中");
        INPUT in[2] = {};
        in[0].type = INPUT_MOUSE;
        in[0].mi.dwFlags = right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_MIDDLEDOWN;
        in[1].type = INPUT_MOUSE;
        in[1].mi.dwFlags = right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_MIDDLEUP;
        const UINT sent = SendInput(2, in, sizeof(INPUT));
        if (sent != 2) {
            LOG_ERROR("补发{}键点击失败: sent={}, error={}",
                      right ? "右" : "中", sent, GetLastError());
        }
    };

    // 低级钩子回调必须尽快返回；SendInput 可能同步唤醒目标程序并造成数百毫秒延迟。
    if (!easy::core::MainThreadDispatcher::instance().postDeferred(inject)) {
        LOG_WARN("主线程延迟队列不可用，立即补发触发键点击");
        inject();
    }
}

void GestureEngine::cancelTracking() {
    MouseHook::instance().resetTriggerState();
    m_activeProfile.reset();
    m_lastRecognizedDirections.clear();
    if (m_trailVisible.load()) {
        GestureTrailOverlay::instance().hide();
    }
    m_state = GestureState::Idle;
    LOG_INFO("手势追踪已取消");
}

std::optional<GestureProfile> GestureEngine::resolveProfile(HWND hwnd) const {
    auto profileName = m_scopeRules.evaluate(hwnd);

    if (!profileName.has_value()) {
        // 返回 nullopt 表示手势被禁用
        return std::nullopt;
    }

    std::shared_lock lock(m_profileMutex);
    const auto fallback = m_profiles.find("default");
    if (profileName->empty()) {
        // 空字符串表示使用全局默认
        return fallback != m_profiles.end()
            ? std::optional<GestureProfile>(fallback->second)
            : std::nullopt;
    }

    // 使用指定的 Profile
    auto it = m_profiles.find(*profileName);
    if (it != m_profiles.end()) {
        return it->second;
    }

    // 找不到指定 Profile，fallback 到默认
    LOG_WARN("指定的 Profile 不存在: {}, 使用默认 Profile", *profileName);
    return fallback != m_profiles.end()
        ? std::optional<GestureProfile>(fallback->second)
        : std::nullopt;
}

// ── 配置持久化 ───────────────────────────────────────────────────────────────

void GestureEngine::loadFromConfig() {
    auto& config = easy::core::ConfigManager::instance();

    bool paused = config.get<bool>("/gesture/paused",
                                   !config.get<bool>("/gesture/enabled", true));
    m_paused = paused;
    setTriggerButton(config.get<std::string>("/gesture/triggerButton", "right"));
    setTrailVisible(config.get<bool>("/gesture/trailVisible", true));
    setAutoBypassFullscreen(config.get<bool>("/gesture/autoBypassFullscreen", false));

    // 加载 Profile
    std::unordered_map<std::string, GestureProfile> loadedProfiles;
    loadedProfiles.emplace("default", GestureProfile::createDefaultGlobal());
    loadedProfiles.emplace("browser", GestureProfile::createBrowserProfile());
    auto profilesJson = config.get<nlohmann::json>("/gesture/profiles");
    if (profilesJson.is_array()) {
        for (const auto& pj : profilesJson) {
            auto profile = GestureProfile::fromJson(pj);
            if (!profile.name().empty()) {
                loadedProfiles.insert_or_assign(profile.name(), std::move(profile));
            }
        }
    }
    const auto loadedCount = loadedProfiles.size();
    {
        std::unique_lock lock(m_profileMutex);
        m_profiles = std::move(loadedProfiles);
    }
    LOG_INFO("从配置加载手势配置集, 数量={}", loadedCount);

    // 加载作用域规则
    auto rulesJson = config.get<nlohmann::json>("/gesture/scopeRules");
    m_scopeRules.loadFromJson(rulesJson);

    // 加载识别器参数
    RecognizerConfig recognizerConfig;
    recognizerConfig.minSegmentDistance = config.get<int>("/gesture/recognizer/minSegmentDistance", 30);
    recognizerConfig.samplingInterval = config.get<int>("/gesture/recognizer/samplingInterval", 5);
    recognizerConfig.angleToleranceDeg = config.get<double>("/gesture/recognizer/angleTolerance", 22.5);
    m_recognizer.setConfig(recognizerConfig);
}

bool GestureEngine::saveToConfig() {
    auto& config = easy::core::ConfigManager::instance();

    // 保存 Profile
    auto profiles = getProfiles();
    nlohmann::json profilesJson = nlohmann::json::array();
    for (const auto& profile : profiles) profilesJson.push_back(profile.toJson());
    const bool saved = config.mergePatch({
        {"gesture", {
            {"profiles", profilesJson},
            {"scopeRules", m_scopeRules.toJson()},
            {"paused", m_paused.load()},
            {"enabled", !m_paused.load()},
            {"triggerButton", triggerButton()},
            {"trailVisible", m_trailVisible.load()},
            {"autoBypassFullscreen", m_autoBypassFullscreen.load()}
        }}
    }, "/gesture");

    if (saved) LOG_INFO("手势配置已保存");
    else LOG_ERROR("手势配置持久化失败");
    return saved;
}

}  // namespace easy::gesture
