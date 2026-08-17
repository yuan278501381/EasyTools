#pragma once

#include "IContentExtractor.h"
#include <memory>
#include <vector>
#include <string_view>
#include <string>

namespace easy::service::content {

class ContentSearchEngine {
public:
    static ContentSearchEngine& instance();

    ContentSearchEngine();
    ~ContentSearchEngine() = default;

    bool canSearchContent(std::wstring_view extension) const;

    bool searchFile(
        const std::wstring& filePath,
        std::wstring_view queryPattern,
        bool caseSensitive,
        std::vector<ContentSnippet>& outSnippets,
        size_t maxSnippetsPerFile = 3
    );

private:
    std::vector<std::unique_ptr<IContentExtractor>> m_extractors;
};

} // namespace easy::service::content
