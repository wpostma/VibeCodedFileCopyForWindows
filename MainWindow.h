#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#include <memory>
#include "CopyJob.h"
#include "JobListPanel.h"
#include "LogPanel.h"

// ── MainWindow ────────────────────────────────────────────────────────────────
// Top-level frame: toolbar | job list | splitter | log panel | status strip.

class MainWindow {
public:
    MainWindow()  = default;
    ~MainWindow() = default;

    bool Create(HINSTANCE hInst);
    void Show(int nCmdShow);

    static bool RegisterClass(HINSTANCE hInst);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMsg(UINT, WPARAM, LPARAM);

    // Message handlers
    void OnCreate();
    void OnSize(int w, int h);
    void OnCommand(int id, HWND hwndCtl);
    void OnNotify(NMHDR* nm);
    void OnMouseMove(int x, int y, DWORD keys);
    void OnLButtonDown(int x, int y);
    void OnLButtonUp(int x, int y);
    void OnSetCursor(HWND hwndHit, UINT hitCode);
    void OnJobProgress(int jobIdx, ProgressMsg* msg);
    void OnJobLog(int jobIdx, LogMsg* msg);
    void OnJobDone(int jobIdx, ULONGLONG errors);
    void OnDestroy();

    // Job operations
    void CmdNewJob();
    void CmdEditJob();
    void CmdDeleteJob();
    void CmdRunJob(int idx = -1);   // -1 = selected
    void CmdStopJob(int idx = -1);

    // Persistence
    void SaveJobs();
    void LoadJobs();

    // Layout helpers
    void RepositionChildren();
    void UpdateStatusBar();
    void UpdateJobSelection();
    bool IsInSplitter(int y) const;

    // Status strip (custom painted)
    void PaintStatus(HDC dc, const RECT& rc);

    static std::wstring TimestampNow();
    static std::wstring FormatElapsed(ULONGLONG startTick);

    HWND     m_hwnd        = nullptr;
    HINSTANCE m_hInst      = nullptr;

    // Child controls
    HWND          m_hwndToolbar  = nullptr;
    JobListPanel  m_jobList;
    LogPanel      m_logPanel;
    HWND          m_hwndStatus   = nullptr;   // custom-drawn strip

    // Jobs
    std::vector<std::unique_ptr<CopyJob>> m_jobs;
    int m_selJob = -1;   // currently selected job index

    // Layout
    int  m_splitterY     = 0;
    bool m_draggingSplit = false;
    int  m_dragOffset    = 0;

    static const int k_toolbarH = 40;
    static const int k_statusH  = 28;
    static const int k_splitH   =  6;

    // Status strip state (public so StatusWndProc can read them)
public:
    bool         m_statusDismissed = false;
    bool         m_isAdmin         = false;
private:

    // GDI
    HFONT m_fontUI     = nullptr;
    HFONT m_fontUIBold = nullptr;
    HBRUSH m_brStatus  = nullptr;
    HCURSOR m_cursorNS = nullptr;
};
