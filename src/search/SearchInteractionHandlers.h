#pragma once

namespace easy::core {
class MessageBridge;
}

namespace easy::search {

void registerSearchInteractionHandlers(easy::core::MessageBridge& bridge);

}  // namespace easy::search
