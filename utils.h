#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
