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

void ContentSearchEngine::configureFormats(const std::vector<std::wstring>& customExts, const std::vector<std::wstring>& disabledExts) {
    std::lock_guard<std::mutex> lock(m_mutex);
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
            m_customExts.insert(lower);
            if (!m_extractors.empty()) {
                auto* plainText = dynamic_cast<PlainTextExtractor*>(m_extractors[0].get());
                if (plainText) {
                    plainText->addCustomExtension(lower);
                }
            }
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

    std::lock_guard<std::mutex> lock(m_mutex);
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

    std::wstring ext = filePath.substr(dotPos + 1);
    if (!canSearchContent(ext)) return false;

    for (const auto& extractor : m_extractors) {
        if (extractor->canHandle(ext)) {
            return extractor->searchContent(filePath, queryPattern, caseSensitive, outSnippets, maxSnippetsPerFile);
        }
    }

    return false;
}

} // namespace easy::service::content
