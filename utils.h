#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <string>
#include <vector>

// Returns the current local time as "YYYY.MM.DD HH:MM:SS.mmm"
inline std::wstring TimestampNow()
{
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[32];
    swprintf_s(buf, L"%04d.%02d.%02d %02d:%02d:%02d.%03d",
               st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buf;
}

// Split filter patterns on semicolons, newlines (\r\n), or both
inline std::vector<std::wstring> SplitPatterns(const std::wstring& s)
{
    std::vector<std::wstring> out;
    size_t start = 0;
    while (start < s.size()) {
        // Find next separator: semicolon, \r, or \n
        size_t sep = s.size();
        for (size_t i = start; i < s.size(); i++) {
            if (s[i] == L';' || s[i] == L'\r' || s[i] == L'\n') {
                sep = i;
                break;
            }
        }
        std::wstring tok = s.substr(start, sep - start);
        while (!tok.empty() && tok.front() == L' ') tok.erase(tok.begin());
        while (!tok.empty() && tok.back()  == L' ') tok.pop_back();
        if (!tok.empty()) out.push_back(std::move(tok));
        start = sep + 1;
    }
    return out;
}

inline std::wstring JoinPatterns(const std::vector<std::wstring>& patterns,
                                 const wchar_t* separator)
{
    std::wstring out;
    for (size_t i = 0; i < patterns.size(); ++i) {
        if (i != 0)
            out += separator;
        out += patterns[i];
    }
    return out;
}

inline std::wstring NormalizePatternsForEditor(const std::wstring& s)
{
    return JoinPatterns(SplitPatterns(s), L"\r\n");
}

inline std::wstring NormalizePatternsForStorage(const std::wstring& s)
{
    return JoinPatterns(SplitPatterns(s), L";");
}

inline bool MatchesFilterPattern(const std::wstring& relPath,
                                 const wchar_t* leafName,
                                 bool isDir,
                                 const std::vector<std::wstring>& patterns)
{
    std::wstring relNorm = relPath;
    for (auto& ch : relNorm)
        if (ch == L'/') ch = L'\\';
    while (!relNorm.empty() && relNorm.back() == L'\\')
        relNorm.pop_back();

    for (const auto& rawPattern : patterns) {
        std::wstring pattern = rawPattern;
        for (auto& ch : pattern)
            if (ch == L'/') ch = L'\\';

        bool subtreeWildcard = false;
        if (pattern.size() >= 2 && pattern.compare(pattern.size() - 2, 2, L"\\*") == 0) {
            subtreeWildcard = true;
            pattern.erase(pattern.size() - 2);
        }
        while (!pattern.empty() && pattern.back() == L'\\')
            pattern.pop_back();
        if (pattern.empty())
            continue;

        bool pathPattern = pattern.find(L'\\') != std::wstring::npos;
        if (pathPattern) {
            if (PathMatchSpecW(relNorm.c_str(), pattern.c_str()))
                return true;
            if (subtreeWildcard && isDir && _wcsicmp(relNorm.c_str(), pattern.c_str()) == 0)
                return true;
            continue;
        }

        if (leafName && PathMatchSpecW(leafName, pattern.c_str()))
            return true;
    }
    return false;
}
