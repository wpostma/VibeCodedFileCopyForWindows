#pragma once
#include "CopyJob.h"

// ── Copy engine ───────────────────────────────────────────────────────────────
// Starts a background thread that copies sourcePath → destPath, posting
// WM_JOB_LOG and WM_JOB_PROGRESS messages to hwndMain as it runs.

struct EngineParams {
    HWND                               hwndMain;
    int                                jobIndex;
    std::wstring                       sourcePath;
    std::wstring                       destPath;
    std::shared_ptr<std::atomic<bool>> cancelFlag;
};

// Launch a background copy thread; returns the thread handle (caller must CloseHandle).
HANDLE StartCopyEngine(const EngineParams& params);
