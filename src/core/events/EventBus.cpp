#include "EventBus.h"

namespace easy::core {

EventBus& EventBus::instance() {
    static EventBus bus;
    return bus;
}

void EventBus::unsubscribe(SubscriptionId id) {
    std::lock_guard lock(m_mutex);
    auto typeIt = m_idToType.find(id);
    if (typeIt == m_idToType.end()) return;

    auto& subscribers = m_subscribers[typeIt->second];
    subscribers.erase(
        std::remove_if(subscribers.begin(), subscribers.end(),
            [id](const Subscription& sub) { return sub.id == id; }),
        subscribers.end()
    );
    m_idToType.erase(typeIt);

    LOG_TRACE("EventBus: 取消订阅, subId={}", id);
}

void EventBus::clearAll() {
    std::lock_guard lock(m_mutex);
    m_subscribers.clear();
    m_idToType.clear();
    LOG_INFO("EventBus: 清除所有订阅");
}

} // namespace easy::core
