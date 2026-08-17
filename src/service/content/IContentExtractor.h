#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

namespace easy::service::content {

struct ContentSnippet {
    uint32_t lineNumber = 0;
    std::wstring lineContent;
    uint32_t matchOffset = 0;
    uint32_t matchLength = 0;
};

class IContentExtractor {
public:
    virtual ~IContentExtractor() = default;

    // 是否支持处理指定扩展名（例如 L"cpp", L"docx", L"psd", L"dxf"）
    virtual bool canHandle(std::wstring_view extension) const = 0;

    // 执行全文检索并生成高亮 Snippet 摘要
    virtual bool searchContent(
        const std::wstring& filePath,
        std::wstring_view queryPattern,
        bool caseSensitive,
        std::vector<ContentSnippet>& outSnippets,
        size_t maxSnippetsPerFile = 3
    ) = 0;
};

} // namespace easy::service::content
