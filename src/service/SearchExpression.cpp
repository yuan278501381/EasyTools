#include "SearchExpression.h"
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

    if (token.size() == 2 && std::iswalpha(token[0]) && token[1] == L':') {
        clause.filterType = SearchFilterType::Drive;
        clause.driveLetter = static_cast<wchar_t>(std::towupper(token[0]));
        return clause;
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
    } else if (startsWithNoCase(token, L"case:") || startsWithNoCase(token, L"c:")) {
        clause.filterType = SearchFilterType::CaseSensitive;
        token = token.substr(token[0] == L'c' && token[1] == L':' ? 2 : 5);
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

    clause.isAsciiOnly = true;
    for (wchar_t ch : clause.pattern) {
        if (ch < L'a' || ch > L'z') {
            clause.isAsciiOnly = false;
            break;
        }
    }
    return clause;
}

} // namespace

SearchExpression SearchExpression::parse(const std::wstring& query) {
    SearchExpression expr;
    expr.m_rawQuery = query;
    expr.m_normalizedQuery = normalize(query);

    std::vector<std::wstring> rawTokens;
    size_t i = 0;
    while (i < query.size()) {
        while (i < query.size() && iswspace(query[i])) ++i;
        if (i >= query.size()) break;

        if (query[i] == L'"') {
            size_t start = ++i;
            while (i < query.size() && query[i] != L'"') ++i;
            rawTokens.push_back(query.substr(start, i - start));
            if (i < query.size()) ++i;
        } else {
            size_t start = i;
            while (i < query.size() && !iswspace(query[i])) ++i;
            rawTokens.push_back(query.substr(start, i - start));
        }
    }

    SearchOrGroup currentGroup;
    bool nextIsOr = false;

    for (size_t idx = 0; idx < rawTokens.size(); ++idx) {
        const auto& token = rawTokens[idx];
        if (token == L"|" || token == L"OR" || token == L"or") {
            nextIsOr = true;
            continue;
        }

        SearchClause clause = parseSingleToken(token);
        if (clause.filterType == SearchFilterType::Path ||
            clause.filterType == SearchFilterType::Parent) {
            expr.m_requiresFullPath = true;
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

static bool matchSingleClause(const SearchClause& clause, const FileRecord& record,
                              wchar_t driveLetter, const std::wstring& fullPath) {
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
            std::wstring normPath = SearchExpression::normalize(fullPath);
            if (clause.hasWildcard) {
                matched = SearchExpression::matchWildcard(clause.pattern, normPath);
            } else {
                matched = normPath.find(clause.pattern) != std::wstring::npos;
            }
            break;
        }

        case SearchFilterType::Parent: {
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
            if (clause.regexObj) {
                matched = std::regex_search(record.fileName, *clause.regexObj);
            }
            break;

        case SearchFilterType::CaseSensitive:
            if (clause.hasWildcard) {
                matched = SearchExpression::matchWildcard(clause.rawPattern, record.fileName);
            } else {
                matched = record.fileName.find(clause.rawPattern) != std::wstring::npos;
            }
            break;

        case SearchFilterType::PinyinOnly:
            if (clause.hasWildcard) {
                matched = SearchExpression::matchWildcard(clause.pattern, record.pinyinFull) ||
                          SearchExpression::matchWildcard(clause.pattern, record.pinyinInitials);
            } else {
                matched = record.pinyinFull.find(clause.pattern) != std::wstring::npos ||
                          record.pinyinInitials.find(clause.pattern) != std::wstring::npos;
            }
            break;

        case SearchFilterType::NoPinyin:
            if (clause.hasWildcard) {
                matched = SearchExpression::matchWildcard(clause.pattern, record.normalizedName);
            } else {
                matched = record.normalizedName.find(clause.pattern) != std::wstring::npos;
            }
            break;

        case SearchFilterType::None:
        default:
            if (clause.hasWildcard) {
                matched = SearchExpression::matchWildcard(clause.pattern, record.normalizedName) ||
                          SearchExpression::matchWildcard(clause.pattern, record.pinyinFull) ||
                          SearchExpression::matchWildcard(clause.pattern, record.pinyinInitials);
            } else {
                if (record.normalizedName.find(clause.pattern) != std::wstring::npos) {
                    matched = true;
                } else if (clause.isAsciiOnly &&
                           (record.pinyinInitials.find(clause.pattern) != std::wstring::npos ||
                            record.pinyinFull.find(clause.pattern) != std::wstring::npos)) {
                    matched = true;
                }
            }
            break;
    }

    return clause.isNegated ? !matched : matched;
}

bool SearchExpression::matches(const FileRecord& record, wchar_t driveLetter,
                               const std::wstring& fullPath) const {
    if (m_orGroups.empty()) return true;

    for (const auto& group : m_orGroups) {
        bool groupPassed = false;
        for (const auto& clause : group.clauses) {
            if (matchSingleClause(clause, record, driveLetter, fullPath)) {
                groupPassed = true;
                break;
            }
        }
        if (!groupPassed) return false;
    }
    return true;
}

int SearchExpression::calculateRank(const FileRecord& record) const {
    if (m_normalizedQuery.empty()) return 6;
    if (record.normalizedName == m_normalizedQuery) return 0;
    if (record.normalizedName.starts_with(m_normalizedQuery)) return 1;
    if (record.pinyinFull.starts_with(m_normalizedQuery)) return 2;
    if (record.pinyinInitials.starts_with(m_normalizedQuery)) return 3;
    if (record.normalizedName.find(m_normalizedQuery) != std::wstring::npos) return 4;
    if (record.pinyinFull.find(m_normalizedQuery) != std::wstring::npos) return 5;
    if (record.pinyinInitials.find(m_normalizedQuery) != std::wstring::npos) return 6;
    return 7;
}
