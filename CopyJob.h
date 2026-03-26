#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <atomic>
#include <memory>

// ── Job status ───────────────────────────────────────────────────────────────
enum class JobStatus { Idle, Scanning, Copying, Paused, Done, Error, Stopped };

// ── Schedule type ────────────────────────────────────────────────────────────
enum class ScheduleType { Manual, Interval, Daily };

// ── Log entry (one row in the log panel) ─────────────────────────────────────
struct LogEntry {
    std::wstring timestamp;   // e.g. L"2026.03.01 11:56:43.mmm"
    std::wstring text;
    std::wstring text2;       // secondary text rendered right of a divider (filename)
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
    ULONGLONG bytesToCopy   = 0;      // total bytes of files that need copying
    double    elapsedSec    = 0.0;
};

// ── A copy job: config + runtime state ───────────────────────────────────────
struct CopyJob {
    // Persistent configuration
    std::wstring  name;
    std::wstring  sourcePath;
    std::wstring  destPath;

    // Schedule
    ScheduleType  scheduleType  = ScheduleType::Manual;
    int           scheduleValue = 60;   // minutes (Interval) or HHMM (Daily)

    // Filters (semicolon-delimited wildcard patterns)
    std::wstring  excludePatterns;      // e.g. L"*.tmp;~$*;Thumbs.db"
    std::wstring  includePatterns;      // empty = include all

    // Smart defer
    bool          smartDefer      = false;
    int           quietPeriodMin  = 5;

    // Runtime state (main-thread only except cancelFlag)
    JobStatus     status       = JobStatus::Idle;
    ULONGLONG     runStartTick = 0;   // GetTickCount64() when run began
    JobStats      stats        = {};
    std::wstring  lastRunTime;    // e.g. L"9 hours ago"
    std::wstring  nextRunTime;    // e.g. L"in 14 hours"
    std::vector<LogEntry> logEntries;

    // Log slot for the live "current file" row (main-thread only)
    int curFileLogIdx = -1;

    // Smart defer: last detected activity in source folder (main-thread only)
    ULONGLONG lastActivityTick = 0;

    // Thread control
    HANDLE                               threadHandle = nullptr;
    std::shared_ptr<std::atomic<bool>>   cancelFlag
        = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::atomic<bool>>   pauseFlag
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
    std::wstring text2;              // non-empty → split path | filename display
    int          depth      = 0;
    bool         isGroup    = false;
    int          parentIdx  = -1;
    bool         replaceCurrentFile = false;  // overwrite the live file slot
};

// Passed via LPARAM of WM_JOB_SPACE_CHECK (lives on scanner-thread stack;
// SendMessageW blocks so it stays valid until the UI thread returns).
struct SpaceCheckInfo {
    ULONGLONG freeBytes;
    ULONGLONG neededBytes;
};
