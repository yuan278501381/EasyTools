#pragma once

#include "core/utils/Export.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace easy::core {

struct RenamePathResult {
    bool success = false;
    std::filesystem::path newPath;
    std::string error;
};

// Renames one existing filesystem entry without allowing the caller-provided
// name to select another directory, drive, device name, or NTFS stream.
EASYCORE_API RenamePathResult renamePathWithinParent(
    const std::filesystem::path& oldPath, std::wstring_view newName);

}  // namespace easy::core
