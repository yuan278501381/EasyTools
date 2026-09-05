#pragma once

#include "core/utils/Export.h"

#include <string_view>

namespace easy::core {

class MessageBridge;

// Registers the shared open/locate/admin/properties/notepad/rename/context-menu
// contract under a namespace such as "system" or "search". Keeping these
// aliases on one implementation prevents IPC behavior and validation drift.
EASYCORE_API void registerFileInteractionHandlers(
    MessageBridge& bridge,
    std::string_view methodNamespace,
    std::string_view missingPathError,
    bool preferFilepathParameter = false);

}  // namespace easy::core

