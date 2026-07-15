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
    // Subscriber destructors can own RAII subscriptions which call back into
    // EventBus::unsubscribe(). Destroy them after releasing m_mutex.
    decltype(m_subscribers) subscribers;
    decltype(m_idToType) idToType;
    {
        std::lock_guard lock(m_mutex);
        subscribers.swap(m_subscribers);
        idToType.swap(m_idToType);
    }
    subscribers.clear();
    idToType.clear();
    LOG_INFO("EventBus: 清除所有订阅");
}

} // namespace easy::core
