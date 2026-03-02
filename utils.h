#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

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
