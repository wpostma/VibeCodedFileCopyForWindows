#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <atomic>
#include <memory>

// ── Job status ───────────────────────────────────────────────────────────────
enum class JobStatus { Idle, Scanning, Copying, Done, Error, Stopped };

// ── Log entry (one row in the log panel) ─────────────────────────────────────
struct LogEntry {
    std::wstring timestamp;   // e.g. L"2026.03.01 11:56:43"
    std::wstring text;
    int          depth      = 0;      // 0 = root, 1 = child, 2 = grandchild
    bool         isGroup    = false;  // shows [+]/[-] toggle
    bool         expanded   = true;
    int          parentIdx  = -1;     // index in job.logEntries (-1 = top-level)
};

// ── Cumulative statistics for one run ────────────────────────────────────────
struct JobStats {
    ULONGLONG totalFiles    = 0;
    ULONGLONG copiedFiles   = 0;
    ULONGLONG skippedFiles  = 0;
    ULONGLONG totalBytes    = 0;
    ULONGLONG copiedBytes   = 0;
    ULONGLONG totalFolders  = 0;
    ULONGLONG errorCount    = 0;
    ULONGLONG changeCount   = 0;
    double    elapsedSec    = 0.0;
};

// ── A copy job: config + runtime state ───────────────────────────────────────
struct CopyJob {
    // Persistent configuration
    std::wstring  name;
    std::wstring  sourcePath;
    std::wstring  destPath;

    // Runtime state (main-thread only except cancelFlag)
    JobStatus     status       = JobStatus::Idle;
    JobStats      stats        = {};
    std::wstring  lastRunTime;    // e.g. L"9 hours ago"
    std::wstring  nextRunTime;    // e.g. L"in 14 hours"
    std::vector<LogEntry> logEntries;

    // Thread control
    HANDLE                               threadHandle = nullptr;
    std::shared_ptr<std::atomic<bool>>   cancelFlag
        = std::make_shared<std::atomic<bool>>(false);
};

// ── Messages posted from the copy thread to the main window ──────────────────

struct ProgressMsg {
    int       jobIndex;
    JobStats  stats;
    JobStatus status;
};

struct LogMsg {
    int          jobIndex;
    std::wstring timestamp;
    std::wstring text;
    int          depth;
    bool         isGroup;
    int          parentIdx;
};
