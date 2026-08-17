#pragma once

#include "IContentExtractor.h"
#include <unordered_set>
#include <string>

namespace easy::service::content {

class PsdAiExtractor : public IContentExtractor {
public:
    PsdAiExtractor();
    ~PsdAiExtractor() override = default;

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

    bool extractPsdAiText(
        const uint8_t* pData,
        size_t dataSize,
        std::wstring& outExtractedText
    ) const;
};

} // namespace easy::service::content
