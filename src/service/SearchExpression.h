#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <regex>
#include <optional>
#include <cstdint>

struct FileRecord {
    uint64_t fileReferenceNumber = 0;
    uint64_t parentFileReferenceNumber = 0;
    std::wstring fileName;
    std::wstring normalizedName;
    std::wstring pinyinInitials;
    std::wstring pinyinFull;
    bool isDirectory = false;
};

enum class SearchFilterType {
    None,
    FileOnly,      // file:
    FolderOnly,    // folder:, dir:
    Extension,     // ext:jpg;png, ext:txt
    Path,          // path:text
    Parent,        // parent:dir, p:dir
    Drive,         // c:, d:
    Exact,         // exact:foo.txt
    Regex,         // regex:pattern, r:pattern
    CaseSensitive, // case:text, c:text
    PinyinOnly,    // pinyin:text, py:text
    NoPinyin       // nopy:text
};

struct SearchClause {
    bool isNegated = false;
    SearchFilterType filterType = SearchFilterType::None;
    std::wstring pattern;        // normalized (lowercased)
    std::wstring rawPattern;     // case preserved
    std::vector<std::wstring> extList; // for ext:jpg;png
    std::optional<std::wregex> regexObj;
    bool hasWildcard = false;
    bool isAsciiOnly = false;
    wchar_t driveLetter = 0;
};

struct SearchOrGroup {
    std::vector<SearchClause> clauses;
};

class SearchExpression {
public:
    static SearchExpression parse(const std::wstring& query);

    bool isEmpty() const { return m_orGroups.empty(); }
    bool requiresFullPath() const { return m_requiresFullPath; }

    bool matches(const FileRecord& record, wchar_t driveLetter,
                 const std::wstring& fullPath = L"") const;

    int calculateRank(const FileRecord& record) const;

    const std::wstring& getNormalizedQuery() const { return m_normalizedQuery; }
    const std::vector<SearchOrGroup>& getOrGroups() const { return m_orGroups; }

    static bool matchWildcard(std::wstring_view pattern, std::wstring_view text);
    static std::wstring normalize(std::wstring_view text);

private:
    std::wstring m_rawQuery;
    std::wstring m_normalizedQuery;
    std::vector<SearchOrGroup> m_orGroups;
    bool m_requiresFullPath = false;
};
