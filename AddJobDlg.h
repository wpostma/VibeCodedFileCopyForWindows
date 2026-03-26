#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

// ── AddJobDlg ─────────────────────────────────────────────────────────────────
// Modal dialog for adding or editing a copy job.
// Pre-fill name/source/dest to edit an existing job.

struct AddJobDlgData {
    std::wstring name;
    std::wstring sourcePath;
    std::wstring destPath;
    int          scheduleType  = 0;   // 0=Manual, 1=Interval, 2=Daily
    int          scheduleValue = 60;  // minutes or HHMM
    std::wstring excludePatterns;
    std::wstring includePatterns;
    bool         smartDefer     = false;
    int          quietPeriodMin = 5;
};

// Returns true if the user clicked OK; out is filled with the entered values.
bool ShowAddJobDialog(HWND hwndParent, HINSTANCE hInst, AddJobDlgData& out);
