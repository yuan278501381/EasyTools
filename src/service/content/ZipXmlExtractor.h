#pragma once

#include "IContentExtractor.h"
#include <unordered_set>
#include <string>

namespace easy::service::content {

class ZipXmlExtractor : public IContentExtractor {
public:
    ZipXmlExtractor();
    ~ZipXmlExtractor() override = default;

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

    bool extractZipEntriesText(
        const uint8_t* pZipData,
        size_t zipSize,
        std::wstring_view extension,
        std::wstring& outExtractedText
    ) const;
};

} // namespace easy::service::content
