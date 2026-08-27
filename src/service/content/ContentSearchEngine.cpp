#include "ContentSearchEngine.h"
#include "PlainTextExtractor.h"
#include "ZipXmlExtractor.h"
#include "PsdAiExtractor.h"
#include "DxfExtractor.h"
#include <algorithm>
#include <cwctype>
#include <mutex>

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

void ContentSearchEngine::configureFormats(const std::vector<std::wstring>& customExts, const std::vector<std::wstring>& disabledExts) {
    std::unique_lock lock(m_mutex);
    m_disabledExts.clear();
    for (const auto& ext : disabledExts) {
        std::wstring lower;
        lower.reserve(ext.size());
        for (wchar_t c : ext) {
            if (c != L'.') lower.push_back(std::towlower(c));
        }
        if (!lower.empty()) m_disabledExts.insert(std::move(lower));
    }

    m_customExts.clear();
    for (const auto& ext : customExts) {
        std::wstring lower;
        lower.reserve(ext.size());
        for (wchar_t c : ext) {
            if (c != L'.') lower.push_back(std::towlower(c));
        }
        if (!lower.empty()) {
            m_customExts.insert(std::move(lower));
        }
    }
}

bool ContentSearchEngine::canSearchContent(std::wstring_view extension) const {
    std::wstring lowerExt;
    lowerExt.reserve(extension.size());
    for (wchar_t c : extension) {
        if (c != L'.') lowerExt.push_back(std::towlower(c));
    }
    if (lowerExt.empty()) return false;

    std::shared_lock lock(m_mutex);
    if (m_disabledExts.find(lowerExt) != m_disabledExts.end()) {
        return false;
    }

    if (m_customExts.find(lowerExt) != m_customExts.end()) {
        return true;
    }

    for (const auto& extractor : m_extractors) {
        if (extractor->canHandle(lowerExt)) {
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

    std::wstring ext;
    ext.reserve(filePath.size() - dotPos - 1);
    for (size_t index = dotPos + 1; index < filePath.size(); ++index) {
        if (filePath[index] != L'.') ext.push_back(std::towlower(filePath[index]));
    }
    if (ext.empty()) return false;

    IContentExtractor* selected = nullptr;
    {
        std::shared_lock lock(m_mutex);
        if (m_disabledExts.find(ext) != m_disabledExts.end()) return false;
        if (m_customExts.find(ext) != m_customExts.end()) {
            if (!m_extractors.empty()) selected = m_extractors.front().get();
        } else {
            for (const auto& extractor : m_extractors) {
                if (extractor->canHandle(ext)) {
                    selected = extractor.get();
                    break;
                }
            }
        }
    }
    return selected && selected->searchContent(
        filePath, queryPattern, caseSensitive, outSnippets, maxSnippetsPerFile);
}

} // namespace easy::service::content
