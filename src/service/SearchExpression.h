#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <regex>
#include <optional>
#include <cstdint>
#include <functional>

// A resolved, read-only view of one indexed file.
//
// The index keeps names in a StringArena rather than in per-record wstrings, so
// this is a view over that storage: building one during a scan is pointer
// arithmetic with no allocation. Callers outside the index (tests, ad-hoc
// matching) can point the views at string literals or at strings they keep alive
// themselves.
struct FileRecord {
    uint64_t fileReferenceNumber = 0;
    uint64_t parentFileReferenceNumber = 0;
    std::wstring_view fileName;
    std::wstring_view normalizedName;
    std::wstring_view pinyinInitials;
    std::wstring_view pinyinFull;
    bool isDirectory = false;
    uint32_t fileAttributes = 0;
    uint64_t fileSize = 0;
    uint64_t creationTime = 0;
    uint64_t lastWriteTime = 0;
};

// The owning form used to feed records into the index. Normalized and pinyin
// forms are derived on insert, so they are deliberately absent here.
struct FileRecordInit {
    uint64_t fileReferenceNumber = 0;
    uint64_t parentFileReferenceNumber = 0;
    std::wstring fileName;
    bool isDirectory = false;
    uint32_t fileAttributes = 0;
    uint64_t fileSize = 0;
    uint64_t creationTime = 0;
    uint64_t lastWriteTime = 0;
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
    CaseSensitive, // case:text
    PinyinOnly,    // pinyin:text, py:text
    NoPinyin,      // nopy:text
    Content,       // content:text, c:text, 内容:text
    Size           // size:>100mb, size:<10kb, size:>=1gb
};

struct SearchClause {
    enum class SizeOp { Equal, Greater, GreaterEqual, Less, LessEqual };

    bool isNegated = false;
    SearchFilterType filterType = SearchFilterType::None;
    std::wstring pattern;        // normalized (lowercased)
    std::wstring rawPattern;     // case preserved
    std::wstring pinyinPattern;   // normalized and stripped of apostrophes (e.g. tong'xi -> tongxi)
    std::vector<std::wstring> extList; // for ext:jpg;png
    std::optional<std::wregex> regexObj;
    bool hasWildcard = false;
    bool isAsciiOnly = false;
    wchar_t driveLetter = 0;
    SizeOp sizeOp = SizeOp::Equal;
    uint64_t sizeBytes = 0;
};

struct SearchOrGroup {
    std::vector<SearchClause> clauses;
};

class SearchExpression {
public:
    using PathGetter = std::function<std::wstring()>;

    static SearchExpression parse(const std::wstring& query);

    bool isEmpty() const { return m_orGroups.empty(); }
    bool requiresFullPath() const { return m_requiresFullPath; }
    bool hasContentFilter() const { return m_hasContentFilter; }
    const std::wstring& getContentQuery() const { return m_contentQuery; }

    bool matches(const FileRecord& record, wchar_t driveLetter,
                 const std::wstring& fullPath = L"") const;

    bool matchesWithLazyPath(const FileRecord& record, wchar_t driveLetter,
                             const PathGetter& getFullPath) const;

    int calculateRank(const FileRecord& record) const;

    const std::wstring& getNormalizedQuery() const { return m_normalizedQuery; }
    const std::vector<SearchOrGroup>& getOrGroups() const { return m_orGroups; }

    static bool matchWildcard(std::wstring_view pattern, std::wstring_view text);
    static std::wstring normalize(std::wstring_view text);

private:
    std::wstring m_rawQuery;
    std::wstring m_normalizedQuery;
    std::wstring m_cleanAsciiQuery;
    std::vector<SearchOrGroup> m_orGroups;
    bool m_requiresFullPath = false;
    bool m_hasContentFilter = false;
    bool m_isAsciiQuery = false;
    std::wstring m_contentQuery;
};
