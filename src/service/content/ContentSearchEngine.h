#pragma once

#include "IContentExtractor.h"
#include <memory>
#include <vector>
#include <string_view>
#include <string>
#include <unordered_set>
#include <mutex>

namespace easy::service::content {

class ContentSearchEngine {
public:
    static ContentSearchEngine& instance();

    ContentSearchEngine();
    ~ContentSearchEngine() = default;

    bool canSearchContent(std::wstring_view extension) const;
    void configureFormats(const std::vector<std::wstring>& customExts, const std::vector<std::wstring>& disabledExts);

    bool searchFile(
        const std::wstring& filePath,
        std::wstring_view queryPattern,
        bool caseSensitive,
        std::vector<ContentSnippet>& outSnippets,
        size_t maxSnippetsPerFile = 3
    );

private:
    std::vector<std::unique_ptr<IContentExtractor>> m_extractors;
    std::unordered_set<std::wstring> m_customExts;
    std::unordered_set<std::wstring> m_disabledExts;
    mutable std::mutex m_mutex;
};

} // namespace easy::service::content
