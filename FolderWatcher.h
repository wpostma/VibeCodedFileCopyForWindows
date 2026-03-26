#pragma once
#include <windows.h>
#include <string>
#include <atomic>
#include <memory>

// Lightweight folder watcher using ReadDirectoryChangesW.
// For M2 (smart defer): only tracks whether ANY activity occurred recently.
// For M3 (full monitoring): will be extended to trigger copy runs on change.

class FolderWatcher {
public:
    FolderWatcher() = default;
    ~FolderWatcher() { Stop(); }

    // Start watching a directory.  hwndNotify receives WM_JOB_CHANGED.
    bool Start(const std::wstring& path, HWND hwndNotify, int jobIndex);
    void Stop();
    bool IsRunning() const { return m_thread != nullptr; }

    // Last tick when any file change was detected (main-thread reads this)
    ULONGLONG LastActivityTick() const { return m_lastActivityTick.load(); }

private:
    static DWORD WINAPI WatchProc(LPVOID param);

    HANDLE    m_thread    = nullptr;
    HANDLE    m_stopEvent = nullptr;
    HANDLE    m_dirHandle = nullptr;
    HWND      m_hwnd      = nullptr;
    int       m_jobIndex  = -1;
    std::wstring m_path;
    std::atomic<ULONGLONG> m_lastActivityTick{0};
};
