#include "core/ipc/SystemInteractionHandlers.h"

#include "core/ipc/FileInteractionHandlers.h"
#include "core/ipc/MessageBridge.h"
#include "core/utils/WinUtils.h"

#include <string>

namespace easy::core {

void registerSystemInteractionHandlers(MessageBridge& bridge) {
    registerFileInteractionHandlers(bridge, "system", "path is required");
    bridge.registerHandler("system.copyText", [](const json& params) -> json {
        const std::string text = params.value("text", "");
        return {{"success", WinUtils::copyToClipboard(text)}};
    });
}

}  // namespace easy::core
