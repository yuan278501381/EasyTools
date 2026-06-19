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
    hook.setEventCallback([this](const MouseEvent& event) {
        onMouseEvent(event);
    });

    if (!hook.install()) {
        LOG_ERROR("手势引擎启动失败: 无法安装鼠标钩子");
        return false;
    }

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
    m_paused = paused;
    MouseHook::instance().setPaused(paused);
    LOG_INFO("手势引擎暂停状态: paused={}", paused);
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

void GestureEngine::onMouseEvent(const MouseEvent& event) {
    if (m_paused.load(std::memory_order_relaxed)) return;

    switch (m_state.load()) {
        case GestureState::Idle:
            if (event.type == m_triggerDown) {
                beginTracking(event);
            }
            break;

        case GestureState::Tracking:
            if (event.type == MouseEventType::Move) {
                updateTracking(event);
            } else if (event.type == m_triggerUp) {
                endTracking(event);
            }
            break;

        case GestureState::Executing:
            // 动作执行中，忽略事件
            break;
    }
}

void GestureEngine::beginTracking(const MouseEvent& event) {
    m_recognizer.reset();
    m_recognizer.addPoint(event.position.x, event.position.y);
    m_gestureStartWindow = event.foregroundWindow;
    m_state = GestureState::Tracking;

    // 开始轨迹可视化
    auto& trail = GestureTrailOverlay::instance();
    trail.beginTrail();
    trail.addPoint(static_cast<float>(event.position.x), static_cast<float>(event.position.y));

    LOG_TRACE("手势追踪开始: pos=({},{})", event.position.x, event.position.y);
}

void GestureEngine::updateTracking(const MouseEvent& event) {
    m_recognizer.addPoint(event.position.x, event.position.y);

    // 实时轨迹可视化
    GestureTrailOverlay::instance().addPoint(
        static_cast<float>(event.position.x),
        static_cast<float>(event.position.y)
    );

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
        GestureTrailOverlay::instance().endTrail(resultLabel);

        action->execute();

        m_state = GestureState::Idle;
    } else {
        LOG_DEBUG("未找到手势映射: code={}", result->code);
        GestureTrailOverlay::instance().hide();
        m_state = GestureState::Idle;
    }
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

    LOG_INFO("手势配置已保存");
}

}  // namespace easy::gesture
