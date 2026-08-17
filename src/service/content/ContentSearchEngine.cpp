#include "ContentSearchEngine.h"
#include "PlainTextExtractor.h"
#include "ZipXmlExtractor.h"
#include "PsdAiExtractor.h"
#include "DxfExtractor.h"
#include <algorithm>
#include <cwctype>

namespace easy::service::content {

ContentSearchEngine& ContentSearchEngine::instance() {
    static ContentSearchEngine engine;
    return engine;
}

ContentSearchEngine::ContentSearchEngine() {
    m_extractors.push_back(std::make_unique<PlainTextExtractor>());
    m_extractors.push_back(std::make_unique<ZipXmlExtractor>());
    m_extractors.push_back(std::make_unique<PsdAiExtractor>());
    m_extractors.push_back(std::make_unique<DxfExtractor>());
}

bool ContentSearchEngine::canSearchContent(std::wstring_view extension) const {
    for (const auto& extractor : m_extractors) {
        if (extractor->canHandle(extension)) {
            return true;
        }
    }
    return false;
}

bool ContentSearchEngine::searchFile(
    const std::wstring& filePath,
    std::wstring_view queryPattern,
    bool caseSensitive,
    std::vector<ContentSnippet>& outSnippets,
    size_t maxSnippetsPerFile
) {
    if (queryPattern.empty()) return false;

    // 提取后缀名
    size_t dotPos = filePath.rfind(L'.');
    if (dotPos == std::wstring::npos) return false;

    std::wstring ext = filePath.substr(dotPos + 1);

    for (const auto& extractor : m_extractors) {
        if (extractor->canHandle(ext)) {
            return extractor->searchContent(filePath, queryPattern, caseSensitive, outSnippets, maxSnippetsPerFile);
        }
    }

    return false;
}

} // namespace easy::service::content
