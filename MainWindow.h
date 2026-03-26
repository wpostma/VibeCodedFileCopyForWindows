#pragma once
#include <windows.h>
#include <shellapi.h>
#include <vector>
#include <memory>
#include "CopyJob.h"
#include "JobListPanel.h"
#include "LogPanel.h"
#include "FolderWatcher.h"

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
    bool OnSetCursor(HWND hwndHit, UINT hitCode);  // returns true if handled
    void OnJobProgress(int jobIdx, ProgressMsg* msg);
    void OnJobLog(int jobIdx, LogMsg* msg);
    void OnJobDone(int jobIdx, ULONGLONG errors);
    void OnTimer(UINT_PTR timerId);
    void OnTrayIcon(LPARAM lp);
    void OnPowerBroadcast(WPARAM event);
    bool OnClose();   // returns true if handled (minimized to tray)
    void OnDestroy();

    // Job operations
    void CmdNewJob();
    void CmdEditJob();
    void CmdDeleteJob();
    void CmdRunJob(int idx = -1);   // -1 = selected; also resumes a paused job
    void CmdPauseJob(int idx = -1);
    void CmdStopJob(int idx = -1);
    void CmdRunAll();
    void CmdPauseAll();

    void UpdateToolbarState();      // enable/disable/gray toolbar buttons
    bool AnyJobActive() const;      // any job Scanning/Copying/Paused

    // Persistence
    void SaveJobs();
    void LoadJobs();

    // Tray
    void CreateTrayIcon();
    void UpdateTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayMenu();
    void MinimizeToTray();
    void RestoreFromTray();

    // Scheduling
    void StartScheduleTimer();
    void CheckSchedules();
    void UpdateStartWithWindows();

    // Settings dialog
    void ShowSettingsDialog();

    // Layout helpers
    void RepositionChildren();
    void UpdateStatusBar();
    void UpdateJobSelection();
    void RefreshLogPanel();          // build combined log from all jobs
    bool IsInSplitter(int y) const;

    // Status strip (custom painted)
    void PaintStatus(HDC dc, const RECT& rc);

    static std::wstring FormatElapsed(ULONGLONG startTick);

    HWND     m_hwnd        = nullptr;
    HINSTANCE m_hInst      = nullptr;

    // Child controls
    HWND          m_hwndToolbar  = nullptr;
    HWND          m_hwndBtnPlay  = nullptr;
    HWND          m_hwndBtnPause = nullptr;
    HWND          m_hwndBtnStop  = nullptr;
    JobListPanel  m_jobList;
    LogPanel      m_logPanel;
    HWND          m_hwndStatus   = nullptr;   // custom-drawn strip

    // Jobs
    std::vector<std::unique_ptr<CopyJob>> m_jobs;
    int m_selJob = -1;           // currently selected job index
    ULONGLONG m_lastLogRefresh = 0;  // throttle combined-log rebuilds

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

    // Global settings
    bool m_closeToTray      = true;
    bool m_startWithWindows = false;

    // Tray icon
    NOTIFYICONDATAW m_nid = {};
    bool m_trayIconActive  = false;
    bool m_minimizedToTray = false;

    // Scheduling
    ULONGLONG m_lastScheduleCheck = 0;
    // Per-job: tick of last scheduled run (to avoid re-triggering)
    std::vector<ULONGLONG> m_lastScheduledRun;

    // Folder watchers (one per job with smart defer enabled)
    std::vector<std::unique_ptr<FolderWatcher>> m_watchers;

    // GDI / cursors
    HFONT   m_fontUI     = nullptr;
    HFONT   m_fontUIBold = nullptr;
    HBRUSH  m_brStatus   = nullptr;
    HCURSOR m_cursorNS   = nullptr;
    HCURSOR m_cursorBusy = nullptr;   // IDC_APPSTARTING (arrow + spinning ring)
};
