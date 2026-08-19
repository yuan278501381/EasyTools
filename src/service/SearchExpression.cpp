#include "SearchExpression.h"
#include "content/ContentSearchEngine.h"
#include <algorithm>
#include <cwctype>
#include <sstream>

std::wstring SearchExpression::normalize(std::wstring_view text) {
    std::wstring result;
    result.reserve(text.size());
    for (wchar_t ch : text) {
        result.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }
    return result;
}

bool SearchExpression::matchWildcard(std::wstring_view pattern, std::wstring_view text) {
    size_t p = 0, t = 0;
    size_t starP = std::wstring_view::npos, starT = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == L'?' || pattern[p] == text[t])) {
            p++;
            t++;
        } else if (p < pattern.size() && pattern[p] == L'*') {
            starP = p++;
            starT = t;
        } else if (starP != std::wstring_view::npos) {
            p = starP + 1;
            t = ++starT;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == L'*') p++;
    return p == pattern.size();
}

namespace {

SearchClause parseSingleToken(std::wstring token) {
    SearchClause clause;
    if (token.empty()) return clause;

    if (token.front() == L'!' && token.size() > 1) {
        clause.isNegated = true;
        token.erase(0, 1);
    }

    auto startsWithNoCase = [](const std::wstring& str, const std::wstring& prefix) -> bool {
        if (str.size() < prefix.size()) return false;
        for (size_t i = 0; i < prefix.size(); ++i) {
            if (std::towlower(str[i]) != std::towlower(prefix[i])) return false;
        }
        return true;
    };

    if (token.size() >= 2 && std::iswalpha(token[0]) && token[1] == L':') {
        if (token.size() == 2) {
            clause.filterType = SearchFilterType::Drive;
            clause.driveLetter = static_cast<wchar_t>(std::towupper(token[0]));
            return clause;
        } else if (token[2] == L'\\' || token[2] == L'/') {
            clause.filterType = SearchFilterType::Path;
            clause.rawPattern = token;
            clause.pattern = SearchExpression::normalize(token);
            clause.hasWildcard = clause.pattern.find(L'*') != std::wstring::npos || clause.pattern.find(L'?') != std::wstring::npos;
            return clause;
        }
    }

    if (startsWithNoCase(token, L"file:")) {
        clause.filterType = SearchFilterType::FileOnly;
        token = token.substr(5);
    } else if (startsWithNoCase(token, L"folder:") || startsWithNoCase(token, L"dir:")) {
        clause.filterType = SearchFilterType::FolderOnly;
        token = token.substr(token[0] == L'f' || token[0] == L'F' ? 7 : 4);
    } else if (startsWithNoCase(token, L"ext:")) {
        clause.filterType = SearchFilterType::Extension;
        std::wstring exts = token.substr(4);
        size_t start = 0;
        while (start < exts.size()) {
            size_t end = exts.find_first_of(L";,|", start);
            if (end == std::wstring::npos) end = exts.size();
            std::wstring ext = SearchExpression::normalize(exts.substr(start, end - start));
            if (!ext.empty()) {
                if (ext.front() == L'.') ext.erase(0, 1);
                if (!ext.empty()) clause.extList.push_back(std::move(ext));
            }
            start = end + 1;
        }
        return clause;
    } else if (startsWithNoCase(token, L"path:")) {
        clause.filterType = SearchFilterType::Path;
        token = token.substr(5);
    } else if (startsWithNoCase(token, L"parent:") || startsWithNoCase(token, L"p:")) {
        clause.filterType = SearchFilterType::Parent;
        token = token.substr(token[0] == L'p' && token[1] == L':' ? 2 : 7);
    } else if (startsWithNoCase(token, L"regex:") || startsWithNoCase(token, L"r:")) {
        clause.filterType = SearchFilterType::Regex;
        token = token.substr(token[0] == L'r' && token[1] == L':' ? 2 : 6);
        try {
            clause.regexObj = std::wregex(token, std::regex::icase | std::regex::optimize);
        } catch (...) {
            clause.regexObj = std::nullopt;
        }
        clause.rawPattern = token;
        return clause;
    } else if (startsWithNoCase(token, L"exact:")) {
        clause.filterType = SearchFilterType::Exact;
        token = token.substr(6);
    } else if (startsWithNoCase(token, L"content:") || startsWithNoCase(token, L"内容:") || (startsWithNoCase(token, L"c:") && token.size() > 2 && token[2] != L'\\' && token[2] != L'/')) {
        clause.filterType = SearchFilterType::Content;
        if (startsWithNoCase(token, L"content:")) token = token.substr(8);
        else if (startsWithNoCase(token, L"内容:")) token = token.substr(3);
        else token = token.substr(2);
        clause.rawPattern = token;
        clause.pattern = SearchExpression::normalize(token);
        clause.hasWildcard = clause.pattern.find(L'*') != std::wstring::npos || clause.pattern.find(L'?') != std::wstring::npos;
        return clause;
    } else if (startsWithNoCase(token, L"case:") || startsWithNoCase(token, L"cs:")) {
        clause.filterType = SearchFilterType::CaseSensitive;
        token = token.substr(token[0] == L'c' && token[1] == L's' ? 3 : 5);
        clause.rawPattern = token;
        clause.pattern = SearchExpression::normalize(token);
        clause.hasWildcard = token.find(L'*') != std::wstring::npos || token.find(L'?') != std::wstring::npos;
        return clause;
    } else if (startsWithNoCase(token, L"pinyin:") || startsWithNoCase(token, L"py:")) {
        clause.filterType = SearchFilterType::PinyinOnly;
        token = token.substr(token[0] == L'p' && token[1] == L'y' ? 3 : 7);
    } else if (startsWithNoCase(token, L"nopy:")) {
        clause.filterType = SearchFilterType::NoPinyin;
        token = token.substr(5);
    }

    clause.rawPattern = token;
    clause.pattern = SearchExpression::normalize(token);
    clause.hasWildcard = clause.pattern.find(L'*') != std::wstring::npos || clause.pattern.find(L'?') != std::wstring::npos;

    clause.pinyinPattern.reserve(clause.pattern.size());
    clause.isAsciiOnly = !clause.pattern.empty();
    for (wchar_t ch : clause.pattern) {
        if (ch >= L'a' && ch <= L'z') {
            clause.pinyinPattern.push_back(ch);
        } else if (ch == L'\'' || ch == L'-') {
            // 忽略拼音音节分隔符 (如 tong'xi -> tongxi)
            continue;
        } else {
            clause.isAsciiOnly = false;
        }
    }
    if (clause.pinyinPattern.empty()) {
        clause.isAsciiOnly = false;
    }
    return clause;
}

static std::wstring expandEnvStrings(const std::wstring& input) {
    if (input.find(L'%') == std::wstring::npos) return input;
    DWORD needed = ExpandEnvironmentStringsW(input.c_str(), nullptr, 0);
    if (needed == 0) return input;
    std::wstring expanded(needed, L'\0');
    DWORD result = ExpandEnvironmentStringsW(input.c_str(), &expanded[0], needed);
    if (result == 0 || result > needed) return input;
    expanded.resize(result - 1);
    return expanded;
}

} // namespace

SearchExpression SearchExpression::parse(const std::wstring& query) {
    std::wstring workingQuery = expandEnvStrings(query);
    SearchExpression expr;
    expr.m_rawQuery = query;
    expr.m_normalizedQuery = normalize(workingQuery);
    expr.m_isAsciiQuery = std::all_of(expr.m_normalizedQuery.begin(), expr.m_normalizedQuery.end(),
                                      [](wchar_t c) { return static_cast<unsigned int>(c) < 128; });
    if (expr.m_isAsciiQuery) {
        expr.m_cleanAsciiQuery.reserve(expr.m_normalizedQuery.size());
        for (wchar_t ch : expr.m_normalizedQuery) {
            if (ch != L'\'' && ch != L'-') expr.m_cleanAsciiQuery.push_back(ch);
        }
    }

    std::vector<std::wstring> rawTokens;
    size_t i = 0;
    while (i < workingQuery.size()) {
        while (i < workingQuery.size() && iswspace(workingQuery[i])) ++i;
        if (i >= workingQuery.size()) break;

        if (workingQuery[i] == L'"') {
            size_t start = ++i;
            while (i < workingQuery.size() && workingQuery[i] != L'"') ++i;
            rawTokens.push_back(workingQuery.substr(start, i - start));
            if (i < workingQuery.size()) ++i;
        } else {
            size_t start = i;
            while (i < workingQuery.size() && !iswspace(workingQuery[i])) ++i;
            rawTokens.push_back(workingQuery.substr(start, i - start));
        }
    }

    if (rawTokens.size() > 1) {
        expr.m_requiresFullPath = true;
    }

    SearchOrGroup currentGroup;
    bool nextIsOr = false;

    for (size_t idx = 0; idx < rawTokens.size(); ++idx) {
        const auto& token = rawTokens[idx];
        if (token.find(L'\\') != std::wstring::npos || token.find(L'/') != std::wstring::npos ||
            token.find(L'*') != std::wstring::npos || token.find(L'?') != std::wstring::npos) {
            expr.m_requiresFullPath = true;
        }
        if (token == L"|" || token == L"OR" || token == L"or") {
            nextIsOr = true;
            continue;
        }

        SearchClause clause = parseSingleToken(token);
        if (clause.filterType == SearchFilterType::Path ||
            clause.filterType == SearchFilterType::Parent) {
            expr.m_requiresFullPath = true;
        }
        if (clause.filterType == SearchFilterType::Content) {
            expr.m_hasContentFilter = true;
            expr.m_contentQuery = clause.rawPattern;
        }

        if (nextIsOr && !expr.m_orGroups.empty()) {
            expr.m_orGroups.back().clauses.push_back(std::move(clause));
            nextIsOr = false;
        } else {
            SearchOrGroup newGroup;
            newGroup.clauses.push_back(std::move(clause));
            expr.m_orGroups.push_back(std::move(newGroup));
            nextIsOr = false;
        }
    }

    return expr;
}

template <typename PathSupplier>
static bool matchSingleClauseLazy(const SearchClause& clause, const FileRecord& record,
                                  wchar_t driveLetter, PathSupplier&& getPath, bool requiresFullPath) {
    bool matched = false;

    switch (clause.filterType) {
        case SearchFilterType::FileOnly:
            if (record.isDirectory) matched = false;
            else if (clause.pattern.empty()) matched = true;
            else {
                if (clause.hasWildcard) {
                    matched = SearchExpression::matchWildcard(clause.pattern, record.normalizedName);
                } else {
                    matched = record.normalizedName.find(clause.pattern) != std::wstring::npos;
                }
            }
            break;

        case SearchFilterType::FolderOnly:
            if (!record.isDirectory) matched = false;
            else if (clause.pattern.empty()) matched = true;
            else {
                if (clause.hasWildcard) {
                    matched = SearchExpression::matchWildcard(clause.pattern, record.normalizedName);
                } else {
                    matched = record.normalizedName.find(clause.pattern) != std::wstring::npos;
                }
            }
            break;

        case SearchFilterType::Extension: {
            if (record.isDirectory) {
                matched = false;
                break;
            }
            const auto dotPos = record.normalizedName.rfind(L'.');
            if (dotPos == std::wstring::npos) {
                matched = false;
                break;
            }
            std::wstring_view fileExt(record.normalizedName.data() + dotPos + 1,
                                      record.normalizedName.size() - dotPos - 1);
            for (const auto& ext : clause.extList) {
                if (fileExt == ext) {
                    matched = true;
                    break;
                }
            }
            break;
        }

        case SearchFilterType::Drive:
            matched = (std::towupper(driveLetter) == std::towupper(clause.driveLetter));
            break;

        case SearchFilterType::Path: {
            const std::wstring& fullPath = getPath();
            std::wstring normPath = SearchExpression::normalize(fullPath);
            if (clause.hasWildcard) {
                matched = SearchExpression::matchWildcard(clause.pattern, normPath);
            } else {
                matched = normPath.find(clause.pattern) != std::wstring::npos;
            }
            break;
        }

        case SearchFilterType::Parent: {
            const std::wstring& fullPath = getPath();
            std::wstring normPath = SearchExpression::normalize(fullPath);
            const auto slashPos = normPath.rfind(L'\\');
            if (slashPos != std::wstring::npos) {
                std::wstring parentPath = normPath.substr(0, slashPos);
                if (clause.hasWildcard) {
                    matched = SearchExpression::matchWildcard(clause.pattern, parentPath);
                } else {
                    matched = parentPath.find(clause.pattern) != std::wstring::npos;
                }
            }
            break;
        }

        case SearchFilterType::Exact:
            matched = (record.normalizedName == clause.pattern);
            break;

        case SearchFilterType::Regex:
            if (clause.regexObj.has_value()) {
                matched = std::regex_search(record.fileName, clause.regexObj.value());
                if (!matched && requiresFullPath) {
                    matched = std::regex_search(getPath(), clause.regexObj.value());
                }
            }
            break;

        case SearchFilterType::CaseSensitive:
            if (clause.hasWildcard) {
                matched = SearchExpression::matchWildcard(clause.rawPattern, record.fileName);
            } else {
                matched = record.fileName.find(clause.rawPattern) != std::wstring::npos;
            }
            if (!matched && requiresFullPath) {
                const std::wstring& fullPath = getPath();
                if (!fullPath.empty()) {
                    if (clause.hasWildcard) {
                        matched = SearchExpression::matchWildcard(clause.rawPattern, fullPath);
                    } else {
                        matched = fullPath.find(clause.rawPattern) != std::wstring::npos;
                    }
                }
            }
            break;

        case SearchFilterType::PinyinOnly:
            if (clause.hasWildcard) {
                const auto& pat = clause.pinyinPattern.empty() ? clause.pattern : clause.pinyinPattern;
                matched = SearchExpression::matchWildcard(pat, record.pinyinFull) ||
                          SearchExpression::matchWildcard(pat, record.pinyinInitials);
            } else {
                const auto& pat = clause.pinyinPattern.empty() ? clause.pattern : clause.pinyinPattern;
                matched = (record.pinyinInitials.find(pat) != std::wstring::npos ||
                           record.pinyinFull.find(pat) != std::wstring::npos);
            }
            break;

        case SearchFilterType::NoPinyin:
            if (clause.hasWildcard) {
                matched = SearchExpression::matchWildcard(clause.pattern, record.normalizedName);
            } else {
                matched = record.normalizedName.find(clause.pattern) != std::wstring::npos;
            }
            if (!matched && requiresFullPath) {
                const std::wstring& fullPath = getPath();
                if (!fullPath.empty()) {
                    std::wstring normPath = SearchExpression::normalize(fullPath);
                    if (clause.hasWildcard) {
                        matched = SearchExpression::matchWildcard(clause.pattern, normPath);
                    } else {
                        matched = normPath.find(clause.pattern) != std::wstring::npos;
                    }
                }
            }
            break;

        case SearchFilterType::Content:
            // Content 过滤在内存初筛阶段始终放行候选文件，后续由内容解析引擎穿透
            matched = true;
            break;

        case SearchFilterType::None:
        default:
            // 1. 优先在文件名中匹配 (零路径回溯，极致毫秒级热路径)
            if (clause.hasWildcard) {
                matched = SearchExpression::matchWildcard(clause.pattern, record.normalizedName);
                if (!matched && clause.isAsciiOnly) {
                    const auto& pat = clause.pinyinPattern.empty() ? clause.pattern : clause.pinyinPattern;
                    matched = SearchExpression::matchWildcard(pat, record.pinyinFull) ||
                              SearchExpression::matchWildcard(pat, record.pinyinInitials);
                }
            } else {
                if (record.normalizedName.find(clause.pattern) != std::wstring::npos) {
                    matched = true;
                } else if (clause.isAsciiOnly) {
                    const auto& pat = clause.pinyinPattern.empty() ? clause.pattern : clause.pinyinPattern;
                    matched = (record.pinyinInitials.find(pat) != std::wstring::npos ||
                               record.pinyinFull.find(pat) != std::wstring::npos);
                }
            }

            // 2. 仅当检索表达式显式需要全路径 (如包含多词组、斜杠或 path: 前缀) 且文件名未命中时，才兜底在路径中匹配
            if (!matched && requiresFullPath) {
                const std::wstring& fullPath = getPath();
                if (!fullPath.empty()) {
                    std::wstring normPath = SearchExpression::normalize(fullPath);
                    if (clause.hasWildcard) {
                        matched = SearchExpression::matchWildcard(clause.pattern, normPath);
                    } else {
                        matched = normPath.find(clause.pattern) != std::wstring::npos;
                    }
                }
            }
            break;
    }

    return clause.isNegated ? !matched : matched;
}

bool SearchExpression::matchesWithLazyPath(const FileRecord& record, wchar_t driveLetter,
                                           const PathGetter& getFullPath) const {
    if (m_orGroups.empty()) return true;

    std::wstring cachedFullPath;
    bool pathLoaded = false;
    auto getOrFetchPath = [&]() -> const std::wstring& {
        if (!pathLoaded) {
            if (getFullPath) {
                cachedFullPath = getFullPath();
            }
            pathLoaded = true;
        }
        return cachedFullPath;
    };

    for (const auto& group : m_orGroups) {
        bool groupPassed = false;
        for (const auto& clause : group.clauses) {
            if (matchSingleClauseLazy(clause, record, driveLetter, getOrFetchPath, m_requiresFullPath)) {
                groupPassed = true;
                break;
            }
        }
        if (!groupPassed) return false;
    }
    return true;
}

bool SearchExpression::matches(const FileRecord& record, wchar_t driveLetter,
                               const std::wstring& fullPath) const {
    return matchesWithLazyPath(record, driveLetter, [&fullPath]() {
        return fullPath;
    });
}

int SearchExpression::calculateRank(const FileRecord& record) const {
    if (m_hasContentFilter && !m_contentQuery.empty()) {
        std::wstring normContentQuery = normalize(m_contentQuery);
        if (record.normalizedName.find(normContentQuery) != std::wstring::npos) {
            return 0;
        }
        return 1;
    }
    if (m_normalizedQuery.empty()) return 6;
    if (record.normalizedName == m_normalizedQuery) return 0;
    if (record.normalizedName.starts_with(m_normalizedQuery)) return 1;
    if (m_isAsciiQuery) {
        const auto& q = m_cleanAsciiQuery.empty() ? m_normalizedQuery : m_cleanAsciiQuery;
        if (record.pinyinFull.starts_with(q)) return 2;
        if (record.pinyinInitials.starts_with(q)) return 3;
        if (record.normalizedName.find(m_normalizedQuery) != std::wstring::npos) return 4;
        if (record.pinyinFull.find(q) != std::wstring::npos) return 5;
        if (record.pinyinInitials.find(q) != std::wstring::npos) return 6;
    } else {
        if (record.normalizedName.find(m_normalizedQuery) != std::wstring::npos) return 4;
    }
    return 7;
}
