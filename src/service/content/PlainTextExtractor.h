#pragma once

#include "IContentExtractor.h"
#include <unordered_set>
#include <string>

namespace tools3000::service::content {

class PlainTextExtractor : public IContentExtractor {
public:
    PlainTextExtractor();
    ~PlainTextExtractor() override = default;

    bool canHandle(std::wstring_view extension) const override;
    bool searchContent(
        const std::wstring& filePath,
        std::wstring_view queryPattern,
        bool caseSensitive,
        std::vector<ContentSnippet>& outSnippets,
        size_t maxSnippetsPerFile = 3
    ) override;

private:
    std::unordered_set<std::wstring> m_supportedExts;
};

} // namespace tools3000::service::content
