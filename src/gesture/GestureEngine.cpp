// ─────────────────────────────────────────────────────────────────────────────
// GestureEngine.cpp — 手势引擎实现
// ─────────────────────────────────────────────────────────────────────────────

#include "gesture/GestureEngine.h"
#include "gesture/GestureTrailOverlay.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"
#include "core/utils/WinUtils.h"
#include "core/config/ConfigManager.h"

#include <thread>

namespace easy::gesture {

GestureEngine& GestureEngine::instance() {
    static GestureEngine inst;
    return inst;
}

GestureEngine::GestureEngine() {
    // 创建默认 Profile
    m_profiles["default"] = GestureProfile::createDefaultGlobal();
    m_profiles["browser"] = GestureProfile::createBrowserProfile();
}

bool GestureEngine::start() {
    easy::core::TraceId::Scope scope;

    // 安装鼠标钩子
    auto& hook = MouseHook::instance();
    hook.setEventCallback([this](const MouseEvent& event) -> bool {
        return onMouseEvent(event);
    });

    if (!hook.install()) {
        LOG_ERROR("手势引擎启动失败: 无法安装鼠标钩子");
        return false;
    }
    hook.setPaused(m_paused.load());

    // 初始化手势轨迹覆盖层
    auto& trail = GestureTrailOverlay::instance();
    trail.initialize(GetModuleHandleW(nullptr));

    m_state = GestureState::Idle;
    LOG_INFO("手势引擎已启动, 默认Profile手势数={}, 浏览器Profile手势数={}",
             m_profiles["default"].getMappings().size(),
             m_profiles["browser"].getMappings().size());
    return true;
}

void GestureEngine::stop() {
    MouseHook::instance().uninstall();
    GestureTrailOverlay::instance().shutdown();
    m_state = GestureState::Idle;
    LOG_INFO("手势引擎已停止");
}

void GestureEngine::setPaused(bool paused) {
    bool changed = m_paused.exchange(paused) != paused;
    MouseHook::instance().setPaused(paused);
    LOG_INFO("手势引擎暂停状态: paused={}", paused);

    if (changed && m_pauseChangedCallback) {
        m_pauseChangedCallback(paused);
    }
}

void GestureEngine::setPauseChangedCallback(PauseChangedCallback callback) {
    m_pauseChangedCallback = std::move(callback);
}

void GestureEngine::setTriggerButton(const std::string& button) {
    if (button == "middle") {
        m_triggerDown = MouseEventType::MiddleDown;
        m_triggerUp = MouseEventType::MiddleUp;
    } else {
        m_triggerDown = MouseEventType::RightDown;
        m_triggerUp = MouseEventType::RightUp;
    }
    LOG_INFO("手势触发按钮已设置: {}", triggerButton());
}

std::string GestureEngine::triggerButton() const {
    return m_triggerDown == MouseEventType::MiddleDown ? "middle" : "right";
}

void GestureEngine::setTrailVisible(bool visible) {
    m_trailVisible = visible;
    if (!visible) {
        GestureTrailOverlay::instance().hide();
    }
    LOG_INFO("手势轨迹显示状态: visible={}", visible);
}

void GestureEngine::setProfile(const std::string& name, const GestureProfile& profile) {
    m_profiles[name] = profile;
    LOG_INFO("设置手势配置集: name={}, 手势数={}", name, profile.getMappings().size());
}

GestureProfile* GestureEngine::getProfile(const std::string& name) {
    auto it = m_profiles.find(name);
    return it != m_profiles.end() ? &it->second : nullptr;
}

void GestureEngine::setRecognizerConfig(const RecognizerConfig& config) {
    m_recognizer.setConfig(config);
}

void GestureEngine::setTrailCallback(TrailRenderCallback callback) {
    m_trailCallback = std::move(callback);
}

// ── 鼠标事件处理管道 ─────────────────────────────────────────────────────────

bool GestureEngine::onMouseEvent(const MouseEvent& event) {
    try {
        std::lock_guard lock(m_mutex);

        if (m_paused.load()) return false;

        switch (m_state.load()) {
            case GestureState::Idle:
                if (event.type == m_triggerDown) {
                    // 每次触发键按下 = 一次新的用户操作, 开启全新 TraceId 贯穿整条链路
                    m_gestureTraceId = easy::core::TraceId::begin();

                    HWND hwnd = event.foregroundWindow;
                    std::string exeName = hwnd ? easy::core::WinUtils::getProcessNameFromWindow(hwnd) : "";
                    std::string className = hwnd
                        ? easy::core::WinUtils::wstringToUtf8(easy::core::WinUtils::getWindowClassName(hwnd)) : "";

                    LOG_DEBUG("触发键按下: trigger={}, pos=({},{}), hwnd=0x{:X}, process='{}', class='{}'",
                              triggerButton(), event.position.x, event.position.y,
                              reinterpret_cast<uintptr_t>(hwnd), exeName, className);

                    // 检查当前窗口是否在黑名单（仅触发时读取配置，避免移动时频繁消耗性能）
                    auto exceptions = easy::core::ConfigManager::instance().get<nlohmann::json>(
                        "/gesture/exceptions", nlohmann::json::array());
                    bool disabled = false;
                    for (const auto& rule : exceptions) {
                        std::string ruleType = rule.value("type", "");
                        std::string ruleValue = rule.value("value", "");
                        if (ruleType == "process" &&
                            easy::core::WinUtils::toLower(exeName) == easy::core::WinUtils::toLower(ruleValue)) disabled = true;
                        if (ruleType == "class" && className == ruleValue) disabled = true;
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
                } else if (event.type == m_triggerUp) {
                    endTracking(event);
                    return true; // 拦截触发按键的抬起事件
                } else if (event.type == MouseEventType::LeftDown || event.type == MouseEventType::RightDown) {
                    // 如果在手势过程中按下了其他键，可能是取消手势
                    if (event.type != m_triggerDown) {
                        cancelTracking();
                        // 取消时，可以将当前按键透传
                        return false;
                    }
                }
                // 追踪期间拦截滚轮等其他事件也可以
                return true; 

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
    m_recognizer.reset();
    m_recognizer.addPoint(event.position.x, event.position.y);
    m_gestureStartWindow = event.foregroundWindow;
    m_gestureModifiers = event.modifiers;  // 记录手势开始时的修饰键状态
    m_state = GestureState::Tracking;

    // 开始轨迹可视化
    if (m_trailVisible.load()) {
        auto& trail = GestureTrailOverlay::instance();
        trail.beginTrail();
        trail.addPoint(static_cast<float>(event.position.x), static_cast<float>(event.position.y));
    }

    LOG_INFO("手势追踪开始: pos=({},{}), modifiers=0x{:02X}, trailVisible={}",
             event.position.x, event.position.y, m_gestureModifiers, m_trailVisible.load());
}

void GestureEngine::updateTracking(const MouseEvent& event) {
    m_recognizer.addPoint(event.position.x, event.position.y);

    // 实时轨迹可视化
    if (m_trailVisible.load()) {
        GestureTrailOverlay::instance().addPoint(
            static_cast<float>(event.position.x),
            static_cast<float>(event.position.y)
        );
    }

    // 回调（如有注册）
    if (m_trailCallback) {
        auto dirs = m_recognizer.currentDirections();
        m_trailCallback({}, dirs);
    }
}

void GestureEngine::endTracking(const MouseEvent& event) {
    // 恢复本次手势的 TraceId (按下/移动/抬起跨多次钩子回调, 期间可能被其它操作改写)
    easy::core::TraceId::setCurrent(m_gestureTraceId);

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
    GestureProfile* profile = resolveProfile(m_gestureStartWindow);
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
            action = m_profiles["default"].findAction(fullCode);
        }
    }

    // 带修饰键未匹配时，fallback 到纯方向编码
    if (!action) {
        matchedCode = bareCode;
        action = profile->findAction(bareCode);
        if (!action && profile->name() != "default") {
            action = m_profiles["default"].findAction(bareCode);
        }
    }

    if (action) {
        LOG_INFO("执行手势动作: gesture={}, matchedCode={}, action={}, profile={}",
                 result->toArrowString(), matchedCode, action->name, profile->name());

        // 显示轨迹结果（带修饰键前缀时显示在箭头前面）
        std::string resultLabel = modPrefix + result->toArrowString() + " " + action->name;
        if (m_trailVisible.load()) {
            GestureTrailOverlay::instance().endTrail(resultLabel);
        }

        // 关键: 动作执行 (SendInput / Lua / ShellExecute / 弹窗) 可能耗时甚至阻塞，
        // 而本函数运行在 WH_MOUSE_LL 低级钩子回调里（主线程消息泵上）。若在此同步执行，
        // 超过 LowLevelHooksTimeout 会被系统静默移除钩子，Lua 的 MessageBox 更会冻结全局输入。
        // 因此把动作放到分离线程异步执行。GestureAction 可拷贝，按值捕获保证生命周期安全。
        // 把 TraceId 一并带入线程, 让动作执行日志与本次手势串在同一条链路上。
        GestureAction actionCopy = *action;
        std::string traceId = m_gestureTraceId;
        std::thread([actionCopy = std::move(actionCopy), traceId = std::move(traceId)]() {
            easy::core::TraceId::setCurrent(traceId);
            LOG_INFO("手势动作开始执行(后台线程): action={}, type={}",
                     actionCopy.name, static_cast<int>(actionCopy.type));
            // 兜底: 分离线程中未捕获的异常会 std::terminate 整个进程
            try {
                actionCopy.execute();
                LOG_INFO("手势动作执行完毕: action={}", actionCopy.name);
            } catch (const std::exception& e) {
                LOG_ERROR("手势动作执行异常: action={}, error={}", actionCopy.name, e.what());
            } catch (...) {
                LOG_ERROR("手势动作执行未知异常: action={}", actionCopy.name);
            }
        }).detach();
    } else {
        // 有意义的手势但未绑定动作 → 按手势工具惯例直接消费 (不弹菜单)
        LOG_DEBUG("未找到手势映射: fullCode={}, bareCode={}", fullCode, bareCode);
        if (m_trailVisible.load()) {
            GestureTrailOverlay::instance().hide();
        }
    }

    m_state = GestureState::Idle;
}

// 把被吞掉的触发键点击补发出去 (注入事件会被 MouseHook 忽略, 不会再次触发手势)。
// 用于"没有有效手势/未绑定动作/窗口禁用"时, 让右键(或中键)菜单等正常工作。
void GestureEngine::reinjectTriggerClick() {
    const bool right = (m_triggerDown == MouseEventType::RightDown);
    LOG_INFO("无手势, 补发{}键点击以还原正常菜单", right ? "右" : "中");
    INPUT in[2] = {};
    in[0].type = INPUT_MOUSE;
    in[0].mi.dwFlags = right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_MIDDLEDOWN;
    in[1].type = INPUT_MOUSE;
    in[1].mi.dwFlags = right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_MIDDLEUP;
    SendInput(2, in, sizeof(INPUT));
}

void GestureEngine::cancelTracking() {
    if (m_trailVisible.load()) {
        GestureTrailOverlay::instance().hide();
    }
    m_state = GestureState::Idle;
    LOG_INFO("手势追踪已取消");
}

GestureProfile* GestureEngine::resolveProfile(HWND hwnd) {
    auto profileName = m_scopeRules.evaluate(hwnd);

    if (!profileName.has_value()) {
        // 返回 nullopt 表示手势被禁用
        return nullptr;
    }

    if (profileName->empty()) {
        // 空字符串表示使用全局默认
        return &m_profiles["default"];
    }

    // 使用指定的 Profile
    auto it = m_profiles.find(*profileName);
    if (it != m_profiles.end()) {
        return &it->second;
    }

    // 找不到指定 Profile，fallback 到默认
    LOG_WARN("指定的 Profile 不存在: {}, 使用默认 Profile", *profileName);
    return &m_profiles["default"];
}

// ── 配置持久化 ───────────────────────────────────────────────────────────────

void GestureEngine::loadFromConfig() {
    auto& config = easy::core::ConfigManager::instance();

    bool paused = config.get<bool>("/gesture/paused",
                                   !config.get<bool>("/gesture/enabled", true));
    m_paused = paused;
    setTriggerButton(config.get<std::string>("/gesture/triggerButton", "right"));
    setTrailVisible(config.get<bool>("/gesture/trailVisible", true));

    // 加载 Profile
    auto profilesJson = config.get<nlohmann::json>("/gesture/profiles");
    if (profilesJson.is_array()) {
        for (const auto& pj : profilesJson) {
            auto profile = GestureProfile::fromJson(pj);
            m_profiles[profile.name()] = profile;
        }
        LOG_INFO("从配置加载手势配置集, 数量={}", m_profiles.size());
    }

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

void GestureEngine::saveToConfig() {
    auto& config = easy::core::ConfigManager::instance();

    // 保存 Profile
    nlohmann::json profilesJson = nlohmann::json::array();
    for (const auto& [name, profile] : m_profiles) {
        profilesJson.push_back(profile.toJson());
    }
    config.set("/gesture/profiles", profilesJson);

    // 保存作用域规则
    config.set("/gesture/scopeRules", m_scopeRules.toJson());
    config.set("/gesture/paused", m_paused.load());
    config.set("/gesture/enabled", !m_paused.load());
    config.set("/gesture/triggerButton", triggerButton());
    config.set("/gesture/trailVisible", m_trailVisible.load());

    LOG_INFO("手势配置已保存");
}

}  // namespace easy::gesture
