#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "FolderWatcher.h"
#include "resource.h"
#include "logger.h"

bool FolderWatcher::Start(const std::wstring& path, HWND hwndNotify, int jobIndex)
{
    if (m_thread) return false;  // already running

    m_path     = path;
    m_hwnd     = hwndNotify;
    m_jobIndex = jobIndex;
    m_lastActivityTick.store(0);

    m_dirHandle = CreateFileW(path.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);

    if (m_dirHandle == INVALID_HANDLE_VALUE) {
        Log(L"FolderWatcher: failed to open %s (0x%08X)", path.c_str(), GetLastError());
        m_dirHandle = nullptr;
        return false;
    }

    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_thread = CreateThread(nullptr, 0, WatchProc, this, 0, nullptr);
    if (!m_thread) {
        CloseHandle(m_dirHandle); m_dirHandle = nullptr;
        CloseHandle(m_stopEvent); m_stopEvent = nullptr;
        return false;
    }

    Log(L"FolderWatcher: started watching %s", path.c_str());
    return true;
}

void FolderWatcher::Stop()
{
    if (!m_thread) return;
    SetEvent(m_stopEvent);
    CancelIoEx(m_dirHandle, nullptr);
    WaitForSingleObject(m_thread, 5000);
    CloseHandle(m_thread);    m_thread    = nullptr;
    CloseHandle(m_stopEvent); m_stopEvent = nullptr;
    CloseHandle(m_dirHandle); m_dirHandle = nullptr;
    Log(L"FolderWatcher: stopped watching %s", m_path.c_str());
}

DWORD WINAPI FolderWatcher::WatchProc(LPVOID param)
{
    auto* self = reinterpret_cast<FolderWatcher*>(param);
    BYTE buffer[65536];  // 64 KB buffer as recommended
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    int retryCount = 0;

    while (WaitForSingleObject(self->m_stopEvent, 0) != WAIT_OBJECT_0) {
        DWORD bytesReturned = 0;
        ResetEvent(ov.hEvent);

        BOOL ok = ReadDirectoryChangesW(
            self->m_dirHandle, buffer, sizeof(buffer), TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
            &bytesReturned, &ov, nullptr);

        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_OPERATION_ABORTED) break;  // Stop() called
            Log(L"FolderWatcher: ReadDirectoryChangesW failed (0x%08X), retry %d", err, retryCount);
            if (++retryCount >= 10) break;  // give up after 10 failures
            Sleep(30000);  // retry in 30 sec
            continue;
        }

        // Wait for either a change event or the stop signal
        HANDLE handles[2] = { ov.hEvent, self->m_stopEvent };
        DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

        if (waitResult == WAIT_OBJECT_0 + 1) break;  // stop requested
        if (waitResult != WAIT_OBJECT_0) break;       // error

        if (!GetOverlappedResult(self->m_dirHandle, &ov, &bytesReturned, FALSE))
            continue;

        // We detected activity. Update the tick.
        // We do not care WHAT changed, only THAT something changed.
        self->m_lastActivityTick.store(GetTickCount64());
        retryCount = 0;  // reset retry counter on success

        // If bytesReturned == 0, the kernel buffer overflowed.
        // Treat as activity (conservative: assume something changed).
    }

    CloseHandle(ov.hEvent);
    return 0;
}
