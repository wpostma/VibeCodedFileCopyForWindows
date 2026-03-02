#include "logger.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <stdarg.h>

// ── One-time init (thread-safe via Win32 INIT_ONCE) ──────────────────────────
static INIT_ONCE        s_initOnce  = INIT_ONCE_STATIC_INIT;
static HANDLE           s_hFile     = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION s_cs;

static BOOL WINAPI InitLogger(PINIT_ONCE initOnce, PVOID param, PVOID* ctx)
{
    (void)initOnce; (void)param; (void)ctx;

    InitializeCriticalSection(&s_cs);

    // ── Locate exe directory ─────────────────────────────────────────────────
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';

    // ── Create log\ subdirectory ─────────────────────────────────────────────
    wchar_t logDir[MAX_PATH];
    _snwprintf_s(logDir, MAX_PATH, _TRUNCATE, L"%slog", exePath);
    CreateDirectoryW(logDir, NULL);   /* silently ignores ERROR_ALREADY_EXISTS */

    // ── Build filename: <iso-timestamp>_filecopyutility.log ─────────────────
    // Colons are illegal in Windows filenames; use hyphens in the time part.
    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t logPath[MAX_PATH];
    _snwprintf_s(logPath, MAX_PATH, _TRUNCATE,
        L"%s\\%04d-%02d-%02dT%02d-%02d-%02d_filecopyutility.log",
        logDir,
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);

    s_hFile = CreateFileW(logPath,
        GENERIC_WRITE, FILE_SHARE_READ,
        NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    return TRUE;
}

// ── Internal: write one UTF-8 line to the file ───────────────────────────────
static void WriteFileLine(const wchar_t* msg)
{
    if (s_hFile == INVALID_HANDLE_VALUE) return;

    // Prefix every file line with a hh:mm:ss.mmm timestamp
    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t line[1280];
    _snwprintf_s(line, 1280, _TRUNCATE, L"%02d:%02d:%02d.%03d  %s\r\n",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);

    // Convert to UTF-8
    char utf8[2560];
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1,
                                    utf8, sizeof(utf8), NULL, NULL);
    if (bytes > 1) {          /* bytes includes the NUL terminator */
        DWORD written;
        WriteFile(s_hFile, utf8, (DWORD)(bytes - 1), &written, NULL);
    }
}

// ── Public API ────────────────────────────────────────────────────────────────
void Log(const wchar_t* fmt, ...)
{
    InitOnceExecuteOnce(&s_initOnce, InitLogger, NULL, NULL);

    wchar_t buf[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, sizeof(buf) / sizeof(buf[0]), _TRUNCATE, fmt, args);
    va_end(args);

    // OutputDebugString (VS Output window / DebugView)
    OutputDebugStringW(buf);
    OutputDebugStringW(L"\n");

    // File (serialised via critical section for cross-thread safety)
    EnterCriticalSection(&s_cs);
    WriteFileLine(buf);
    LeaveCriticalSection(&s_cs);
}
