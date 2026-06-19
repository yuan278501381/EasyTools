// ─────────────────────────────────────────────────────────────────────────────
// GestureEngine.cpp — 手势引擎实现
// ─────────────────────────────────────────────────────────────────────────────

#include "gesture/GestureEngine.h"
#include "gesture/GestureTrailOverlay.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"
#include "core/config/ConfigManager.h"

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
    if (m_paused.load(std::memory_order_relaxed)) return false;

    // 获取配置以检查窗口过滤规则
    auto& config = easy::core::ConfigManager::instance().config();

    switch (m_state.load()) {
        case GestureState::Idle:
            if (event.type == m_triggerDown) {
                // 检查当前窗口是否被禁用
                HWND hwnd = event.foregroundWindow;
                if (hwnd) {
                    std::string exeName = easy::core::WinUtils::getProcessNameFromWindow(hwnd);
                    std::string className = easy::core::WinUtils::getClassName(hwnd);
                    // 暂时将十六进制 HWND 字符串作为 handle
                    std::string handleStr = std::to_string(reinterpret_cast<uint64_t>(hwnd));
                    
                    bool disabled = false;
                    for (const auto& rule : config.gestureExceptions) {
                        if (rule.type == "process" && easy::core::WinUtils::toLower(exeName) == easy::core::WinUtils::toLower(rule.value)) disabled = true;
                        if (rule.type == "class" && className == rule.value) disabled = true;
                    }
                    if (disabled) {
                        LOG_DEBUG("窗口被手势黑名单过滤: exe={}, class={}", exeName, className);
                        return false;
                    }
                }

                beginTracking(event);
                return true; // 拦截触发按键的按下事件
            }
            break;

        case GestureState::Tracking:
            if (event.type == MouseEventType::Move) {
                updateTracking(event);
                return true; // 拦截移动事件，或者为了让底层应用也能看到鼠标移动，可以选择 return false; 但最好拦截以避免误操作
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
}

void GestureEngine::beginTracking(const MouseEvent& event) {
    m_recognizer.reset();
    m_recognizer.addPoint(event.position.x, event.position.y);
    m_gestureStartWindow = event.foregroundWindow;
    m_state = GestureState::Tracking;

    // 开始轨迹可视化
    if (m_trailVisible.load()) {
        auto& trail = GestureTrailOverlay::instance();
        trail.beginTrail();
        trail.addPoint(static_cast<float>(event.position.x), static_cast<float>(event.position.y));
    }

    LOG_TRACE("手势追踪开始: pos=({},{})", event.position.x, event.position.y);
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
    m_recognizer.addPoint(event.position.x, event.position.y);

    auto result = m_recognizer.finalize();

    if (!result || !result->isValid()) {
        // 轨迹太短，视为普通右键点击，不拦截
        m_state = GestureState::Idle;
        LOG_TRACE("手势追踪结束: 轨迹太短，视为普通点击");
        return;
    }

    easy::core::TraceId::Scope scope;
    LOG_INFO("手势识别: code={}, arrows={}", result->code, result->toArrowString());

    // 查找适用的 Profile
    GestureProfile* profile = resolveProfile(m_gestureStartWindow);
    if (!profile) {
        // 手势在当前窗口被禁用
        m_state = GestureState::Idle;
        LOG_DEBUG("手势在当前窗口被禁用");
        return;
    }

    // 查找动作
    auto action = profile->findAction(result->code);

    // 如果当前 Profile 没找到，尝试 fallback 到默认 Profile
    if (!action && profile->name() != "default") {
        action = m_profiles["default"].findAction(result->code);
    }

    if (action) {
        m_state = GestureState::Executing;
        LOG_INFO("执行手势动作: gesture={}, action={}, profile={}",
                 result->toArrowString(), action->name, profile->name());

        // 显示轨迹结果
        std::string resultLabel = result->toArrowString() + " " + action->name;
        if (m_trailVisible.load()) {
            GestureTrailOverlay::instance().endTrail(resultLabel);
        }

        action->execute();

        m_state = GestureState::Idle;
    } else {
        LOG_DEBUG("未找到手势映射: code={}", result->code);
        if (m_trailVisible.load()) {
            GestureTrailOverlay::instance().hide();
        }
    }

    m_state = GestureState::Idle;
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
