#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <string>
#include <sstream>
#include "MainWindow.h"
#include "AddJobDlg.h"
#include "CopyEngine.h"
#include "resource.h"
#include "logger.h"
#include "utils.h"

static const wchar_t* k_WndClass  = L"FileCopyUtility";

// ── Admin detection ───────────────────────────────────────────────────────────
static bool CheckIsAdmin()
{
    BOOL result = FALSE;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    PSID adminSid = nullptr;
    if (AllocateAndInitializeSid(&ntAuth, 2,
            SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
            0,0,0,0,0,0, &adminSid))
    {
        CheckTokenMembership(nullptr, adminSid, &result);
        FreeSid(adminSid);
    }
    return result != FALSE;
}

// ── Registration ─────────────────────────────────────────────────────────────
bool MainWindow::RegisterClass(HINSTANCE hInst)
{
    WNDCLASSEXW wc  = { sizeof(wc) };
    wc.style        = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc  = WndProc;
    wc.hInstance    = hInst;
    wc.hCursor      = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName  = MAKEINTRESOURCE(IDR_MENU_MAIN);
    wc.lpszClassName = k_WndClass;
    wc.hIcon        = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm      = wc.hIcon;
    return RegisterClassExW(&wc) != 0;
}

// ── Create / Show ─────────────────────────────────────────────────────────────
bool MainWindow::Create(HINSTANCE hInst)
{
    m_hInst    = hInst;
    m_isAdmin  = CheckIsAdmin();

    m_cursorNS   = LoadCursor(nullptr, IDC_SIZENS);
    m_cursorBusy = LoadCursor(nullptr, IDC_APPSTARTING);

    if (!RegisterClass(hInst)) return false;
    if (!JobListPanel::RegisterClass(hInst)) return false;
    if (!LogPanel::RegisterClass(hInst))     return false;

    m_hwnd = CreateWindowExW(0, k_WndClass, L"File Copy Utility v2.1",
                 WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                 CW_USEDEFAULT, CW_USEDEFAULT, 760, 640,
                 nullptr, nullptr, hInst, this);
    return m_hwnd != nullptr;
}

void MainWindow::Show(int nCmdShow)
{
    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
}

// ── Fonts ─────────────────────────────────────────────────────────────────────
static HFONT MakeFont(int pt, int weight)
{
    LOGFONTW lf = {};
    lf.lfHeight  = -MulDiv(pt, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72);
    lf.lfWeight  = weight;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&lf);
}

// ── Layout ────────────────────────────────────────────────────────────────────
bool MainWindow::IsInSplitter(int y) const
{
    return y >= m_splitterY && y < m_splitterY + k_splitH;
}

void MainWindow::RepositionChildren()
{
    RECT rc; GetClientRect(m_hwnd, &rc);
    int W = rc.right;
    int H = rc.bottom;

    // Clamp splitter
    int minSplit = k_toolbarH + 80;
    int maxSplit = H - k_statusH - k_splitH - 60;
    if (maxSplit < minSplit) maxSplit = minSplit;
    m_splitterY = max(minSplit, min(m_splitterY, maxSplit));

    // Toolbar
    SetWindowPos(m_hwndToolbar, nullptr,
        0, 0, W, k_toolbarH, SWP_NOZORDER | SWP_NOACTIVATE);

    // Job list
    int jH = m_splitterY - k_toolbarH;
    SetWindowPos(m_jobList.Hwnd(), nullptr,
        0, k_toolbarH, W, jH, SWP_NOZORDER | SWP_NOACTIVATE);

    // Log panel (below splitter)
    int logY = m_splitterY + k_splitH;
    int logH = H - logY - k_statusH;
    if (logH < 0) logH = 0;
    SetWindowPos(m_logPanel.Hwnd(), nullptr,
        0, logY, W, logH, SWP_NOZORDER | SWP_NOACTIVATE);

    // Status strip
    SetWindowPos(m_hwndStatus, nullptr,
        0, H - k_statusH, W, k_statusH, SWP_NOZORDER | SWP_NOACTIVATE);

    // Repaint splitter area
    RECT sRc = { 0, m_splitterY, W, m_splitterY + k_splitH };
    InvalidateRect(m_hwnd, &sRc, TRUE);
}

// ── Persistence (FileCopyApplicationState.ini) ────────────────────────────────

static std::wstring GetIniPath()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) *(slash + 1) = L'\0';
    return std::wstring(path) + L"FileCopyApplicationState.ini";
}

// Thin helpers over WritePrivateProfileString / GetPrivateProfileString
static void IniWrite(const wchar_t* sec, const wchar_t* key,
                     const std::wstring& val, const wchar_t* ini)
{
    WritePrivateProfileStringW(sec, key, val.c_str(), ini);
}

static void IniWriteULL(const wchar_t* sec, const wchar_t* key,
                        ULONGLONG val, const wchar_t* ini)
{
    wchar_t buf[32]; swprintf_s(buf, L"%llu", val);
    WritePrivateProfileStringW(sec, key, buf, ini);
}

static std::wstring IniRead(const wchar_t* sec, const wchar_t* key,
                             const wchar_t* ini)
{
    wchar_t buf[MAX_PATH * 2] = {};
    GetPrivateProfileStringW(sec, key, L"", buf, (DWORD)std::size(buf), ini);
    return buf;
}

// Larger buffer for filter patterns that may exceed MAX_PATH*2
static std::wstring IniReadLong(const wchar_t* sec, const wchar_t* key,
                                 const wchar_t* ini)
{
    wchar_t buf[4096] = {};
    GetPrivateProfileStringW(sec, key, L"", buf, (DWORD)std::size(buf), ini);
    return buf;
}

static ULONGLONG IniReadULL(const wchar_t* sec, const wchar_t* key,
                             const wchar_t* ini)
{
    std::wstring s = IniRead(sec, key, ini);
    return s.empty() ? 0 : wcstoull(s.c_str(), nullptr, 10);
}

void MainWindow::SaveJobs()
{
    std::wstring iniPath = GetIniPath();
    const wchar_t* ini   = iniPath.c_str();

    // Overwrite from scratch: delete the file so stale sections don't linger
    DeleteFileW(ini);

    wchar_t buf[32];
    swprintf_s(buf, L"%d", (int)m_jobs.size());
    WritePrivateProfileStringW(L"General", L"JobCount", buf, ini);

    for (int i = 0; i < (int)m_jobs.size(); i++) {
        const CopyJob& job = *m_jobs[i];
        wchar_t sec[32]; swprintf_s(sec, L"Job_%d", i);

        IniWrite   (sec, L"Name",        job.name,        ini);
        IniWrite   (sec, L"Source",      job.sourcePath,  ini);
        IniWrite   (sec, L"Dest",        job.destPath,    ini);
        IniWrite   (sec, L"LastRunTime", job.lastRunTime, ini);

        IniWriteULL(sec, L"ChangeCount",  job.stats.changeCount,  ini);
        IniWriteULL(sec, L"CopiedFiles",  job.stats.copiedFiles,  ini);
        IniWriteULL(sec, L"TotalFiles",   job.stats.totalFiles,   ini);
        IniWriteULL(sec, L"TotalFolders", job.stats.totalFolders, ini);
        IniWriteULL(sec, L"CopiedBytes",  job.stats.copiedBytes,  ini);
        IniWriteULL(sec, L"TotalBytes",   job.stats.totalBytes,   ini);
        IniWriteULL(sec, L"ErrorCount",   job.stats.errorCount,   ini);

        // Schedule
        IniWrite(sec, L"ScheduleType",
                 job.scheduleType == ScheduleType::Interval ? L"Interval" :
                 job.scheduleType == ScheduleType::Daily    ? L"Daily"    : L"Manual", ini);
        IniWriteULL(sec, L"ScheduleValue", (ULONGLONG)job.scheduleValue, ini);

        // Filters
        IniWrite(sec, L"ExcludePatterns", NormalizePatternsForStorage(job.excludePatterns), ini);
        IniWrite(sec, L"IncludePatterns", NormalizePatternsForStorage(job.includePatterns), ini);

        // Smart defer
        IniWriteULL(sec, L"SmartDefer",    job.smartDefer ? 1 : 0, ini);
        IniWriteULL(sec, L"QuietPeriodMin", (ULONGLONG)job.quietPeriodMin, ini);
    }

    // Global settings
    IniWriteULL(L"Settings", L"CloseToTray",    m_closeToTray ? 1 : 0, ini);
    IniWriteULL(L"Settings", L"StartWithWindows", m_startWithWindows ? 1 : 0, ini);

    Log(L"SaveJobs: wrote %d jobs to %s", (int)m_jobs.size(), ini);
}

void MainWindow::LoadJobs()
{
    std::wstring iniPath = GetIniPath();
    const wchar_t* ini   = iniPath.c_str();

    int count = (int)GetPrivateProfileIntW(L"General", L"JobCount", 0, ini);
    Log(L"LoadJobs: JobCount=%d from %s", count, ini);

    for (int i = 0; i < count; i++) {
        wchar_t sec[32]; swprintf_s(sec, L"Job_%d", i);

        auto job = std::make_unique<CopyJob>();
        job->name        = IniRead(sec, L"Name",   ini);
        job->sourcePath  = IniRead(sec, L"Source", ini);
        job->destPath    = IniRead(sec, L"Dest",   ini);
        job->lastRunTime = IniRead(sec, L"LastRunTime", ini);

        job->stats.changeCount  = IniReadULL(sec, L"ChangeCount",  ini);
        job->stats.copiedFiles  = IniReadULL(sec, L"CopiedFiles",  ini);
        job->stats.totalFiles   = IniReadULL(sec, L"TotalFiles",   ini);
        job->stats.totalFolders = IniReadULL(sec, L"TotalFolders", ini);
        job->stats.copiedBytes  = IniReadULL(sec, L"CopiedBytes",  ini);
        job->stats.totalBytes   = IniReadULL(sec, L"TotalBytes",   ini);
        job->stats.errorCount   = IniReadULL(sec, L"ErrorCount",   ini);

        // Schedule
        std::wstring schedStr = IniRead(sec, L"ScheduleType", ini);
        if (schedStr == L"Interval")   job->scheduleType = ScheduleType::Interval;
        else if (schedStr == L"Daily") job->scheduleType = ScheduleType::Daily;
        else                           job->scheduleType = ScheduleType::Manual;
        int sv = (int)IniReadULL(sec, L"ScheduleValue", ini);
        if (sv > 0) job->scheduleValue = sv;

        // Filters
        job->excludePatterns = NormalizePatternsForEditor(IniReadLong(sec, L"ExcludePatterns", ini));
        job->includePatterns = NormalizePatternsForEditor(IniReadLong(sec, L"IncludePatterns", ini));

        // Smart defer
        job->smartDefer     = IniReadULL(sec, L"SmartDefer", ini) != 0;
        int qp = (int)IniReadULL(sec, L"QuietPeriodMin", ini);
        if (qp > 0) job->quietPeriodMin = qp;

        if (!job->name.empty()) {
            Log(L"LoadJobs: loaded job '%s'", job->name.c_str());
            m_jobs.push_back(std::move(job));
        }
    }

    // Global settings
    m_closeToTray      = IniReadULL(L"Settings", L"CloseToTray", ini) != 0;
    m_startWithWindows = IniReadULL(L"Settings", L"StartWithWindows", ini) != 0;
}

// ── Status strip painting ─────────────────────────────────────────────────────
static LRESULT CALLBACK StatusWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    MainWindow* pMain = reinterpret_cast<MainWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);

        // Background
        HBRUSH bg = CreateSolidBrush(RGB(240, 242, 246));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        // Top border
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(205, 210, 220));
        HPEN old = (HPEN)SelectObject(hdc, pen);
        MoveToEx(hdc, 0, 0, nullptr);
        LineTo(hdc, rc.right, 0);
        SelectObject(hdc, old);
        DeleteObject(pen);

        // Info icon circle
        HPEN cpen = CreatePen(PS_SOLID, 1, RGB(60, 110, 200));
        HBRUSH cbr = CreateSolidBrush(RGB(60, 110, 200));
        SelectObject(hdc, cpen);
        SelectObject(hdc, cbr);
        Ellipse(hdc, 8, 7, 22, 21);
        DeleteObject(cpen);
        DeleteObject(cbr);

        // "i" letter
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        LOGFONTW lf = {};
        lf.lfHeight  = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        lf.lfWeight  = FW_BOLD;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        HFONT hf = CreateFontIndirectW(&lf);
        HFONT hfOld = (HFONT)SelectObject(hdc, hf);
        RECT iRc = { 8, 7, 22, 21 };
        DrawTextW(hdc, L"i", -1, &iRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, hfOld);
        DeleteObject(hf);

        // Message text
        SetTextColor(hdc, RGB(40, 50, 70));
        LOGFONTW lf2 = {};
        lf2.lfHeight  = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        lf2.lfWeight  = FW_NORMAL;
        lf2.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf2.lfFaceName, L"Segoe UI");
        HFONT hf2 = CreateFontIndirectW(&lf2);
        HFONT hf2Old = (HFONT)SelectObject(hdc, hf2);

        bool isAdmin = pMain ? pMain->m_isAdmin : false;
        const wchar_t* msg2 = isAdmin
            ? L"Running with administrative privileges."
            : L"Running as standard user.";
        RECT msgRc = { 28, 0, rc.right - 90, rc.bottom };
        DrawTextW(hdc, msg2, -1, &msgRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(hdc, hf2Old);
        DeleteObject(hf2);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_COMMAND && LOWORD(wp) == 1) {
        // Dismiss button
        ShowWindow(hwnd, SW_HIDE);
        // Resize parent
        if (pMain) {
            RECT rc2; GetClientRect(GetParent(hwnd), &rc2);
            SendMessageW(GetParent(hwnd), WM_SIZE, 0,
                         MAKELPARAM(rc2.right, rc2.bottom));
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── OnCreate ─────────────────────────────────────────────────────────────────
void MainWindow::OnCreate()
{
    RECT rc; GetClientRect(m_hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    m_fontUI     = MakeFont(9, FW_NORMAL);
    m_fontUIBold = MakeFont(9, FW_SEMIBOLD);

    // ── Toolbar strip: background is a STATIC (visual only, no interaction) ────
    // Buttons are direct children of m_hwnd so WM_COMMAND reaches our WndProc.
    // (If buttons were children of the STATIC, WM_COMMAND would go to the STATIC
    //  which discards it — clicks would silently do nothing.)
    m_hwndToolbar = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE,
        0, 0, W, k_toolbarH, m_hwnd, (HMENU)0, m_hInst, nullptr);

    // Buttons: parent = m_hwnd, y offset places them within the toolbar band
    auto makeBtn = [&](const wchar_t* label, int id, int x) {
        HWND h = CreateWindowExW(0, L"BUTTON", label,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
            x, 7, 44, 26, m_hwnd, (HMENU)(UINT_PTR)id, m_hInst, nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)m_fontUI, TRUE);
        return h;
    };
    m_hwndBtnPlay  = makeBtn(L"\x25B6", ID_BTN_PLAY,   8);    // ▶
    m_hwndBtnPause = makeBtn(L"\x23F8", ID_BTN_PAUSE,  58);   // ⏸
    m_hwndBtnStop  = makeBtn(L"\x25FC", ID_BTN_STOPSQ, 108);  // ■

    // Tooltips (parented to m_hwnd to match the buttons)
    HWND hTT = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        m_hwnd, nullptr, m_hInst, nullptr);

    TOOLINFOW ti  = { sizeof(ti) };
    ti.uFlags     = TTF_SUBCLASS | TTF_IDISHWND;
    ti.hwnd       = m_hwnd;
    ti.uId        = (UINT_PTR)m_hwndBtnPlay;
    ti.lpszText   = const_cast<LPWSTR>(L"Run selected job");
    SendMessageW(hTT, TTM_ADDTOOL, 0, (LPARAM)&ti);

    ti.uId        = (UINT_PTR)m_hwndBtnPause;
    ti.lpszText   = const_cast<LPWSTR>(L"Pause");
    SendMessageW(hTT, TTM_ADDTOOL, 0, (LPARAM)&ti);

    ti.uId        = (UINT_PTR)m_hwndBtnStop;
    ti.lpszText   = const_cast<LPWSTR>(L"Stop");
    SendMessageW(hTT, TTM_ADDTOOL, 0, (LPARAM)&ti);

    // ── Job list ──────────────────────────────────────────────────────────────
    m_jobList.Create(m_hwnd, IDC_JOBLIST, &m_jobs);
    m_jobList.SetSelectionCallback([](int i, void* ctx) {
        reinterpret_cast<MainWindow*>(ctx)->UpdateJobSelection();
    }, this);

    // ── Log panel ─────────────────────────────────────────────────────────────
    m_logPanel.Create(m_hwnd, IDC_LOGPANEL);

    // ── Status strip ─────────────────────────────────────────────────────────
    static const wchar_t* k_StatusClass = L"FCU_StatusStrip";
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc  = StatusWndProc;
    wc.hInstance    = m_hInst;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = k_StatusClass;
    RegisterClassExW(&wc);

    m_hwndStatus = CreateWindowExW(0, k_StatusClass, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, H - k_statusH, W, k_statusH,
        m_hwnd, (HMENU)IDC_STATUSSTRIP, m_hInst, nullptr);

    // Store MainWindow pointer in status window
    SetWindowLongPtrW(m_hwndStatus, GWLP_USERDATA, (LONG_PTR)this);

    // Dismiss button inside status strip
    HWND hDismiss = CreateWindowExW(0, L"BUTTON", L"Dismiss",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
        0, 4, 70, 20,
        m_hwndStatus, (HMENU)1, m_hInst, nullptr);
    SendMessageW(hDismiss, WM_SETFONT, (WPARAM)m_fontUI, TRUE);
    // Position dismiss right-aligned (will be set in size)
    ShowWindow(hDismiss, SW_SHOW);

    // Initial splitter position
    m_splitterY = H / 2;

    // Load persisted jobs and auto-select the first one
    LoadJobs();
    if (!m_jobs.empty()) {
        m_jobList.SetSelectedIndex(0);
        UpdateJobSelection();
    }
    m_jobList.Refresh();
    UpdateToolbarState();

    RepositionChildren();

    // Create tray icon and start schedule timer
    CreateTrayIcon();
    StartScheduleTimer();

    // Start folder watchers for jobs with smart defer
    for (int i = 0; i < (int)m_jobs.size(); i++) {
        auto& job = *m_jobs[i];
        auto watcher = std::make_unique<FolderWatcher>();
        if (job.smartDefer && job.scheduleType != ScheduleType::Manual) {
            watcher->Start(job.sourcePath, m_hwnd, i);
        }
        m_watchers.push_back(std::move(watcher));
    }
}

// ── Size ─────────────────────────────────────────────────────────────────────
void MainWindow::OnSize(int w, int h)
{
    if (m_splitterY == 0) m_splitterY = h / 2;

    // Keep Dismiss button right-aligned
    if (m_hwndStatus) {
        RECT sRc; GetClientRect(m_hwndStatus, &sRc);
        HWND hDismiss = GetDlgItem(m_hwndStatus, 1);
        if (hDismiss)
            SetWindowPos(hDismiss, nullptr, w - 78, 4, 70, 20,
                         SWP_NOZORDER | SWP_NOACTIVATE);
    }

    RepositionChildren();
}

// ── Selection changed ────────────────────────────────────────────────────────
void MainWindow::UpdateJobSelection()
{
    m_selJob = m_jobList.SelectedIndex();
    Log(L"UpdateJobSelection: m_selJob=%d  jobs.size=%d", m_selJob, (int)m_jobs.size());
    RefreshLogPanel();
    UpdateToolbarState();
}

void MainWindow::RefreshLogPanel()
{
    // Build a combined log with each job as a collapsible root entry.
    std::vector<LogEntry> combined;
    for (auto& job : m_jobs) {
        if (job->logEntries.empty()) continue;

        // Root node: job name
        LogEntry root;
        root.text      = job->name;
        root.depth     = 0;
        root.isGroup   = true;
        root.expanded  = true;
        root.parentIdx = -1;
        combined.push_back(root);

        // Job's entries shifted one depth level deeper
        for (auto& e : job->logEntries) {
            LogEntry copy = e;
            copy.depth += 1;
            combined.push_back(copy);
        }
    }
    m_logPanel.LoadJob(combined);
    m_lastLogRefresh = GetTickCount64();
}

// ── Job operations ────────────────────────────────────────────────────────────
void MainWindow::CmdNewJob()
{
    AddJobDlgData data;
    data.excludePatterns = NormalizePatternsForEditor(
        L"*.tmp;~$*;Thumbs.db;desktop.ini;.DS_Store;.git;node_modules");
    if (!ShowAddJobDialog(m_hwnd, m_hInst, data)) return;

    auto job = std::make_unique<CopyJob>();
    job->name            = data.name;
    job->sourcePath      = data.sourcePath;
    job->destPath        = data.destPath;
    job->scheduleType    = (ScheduleType)data.scheduleType;
    job->scheduleValue   = data.scheduleValue;
    job->excludePatterns = data.excludePatterns;
    job->includePatterns = data.includePatterns;
    job->smartDefer      = data.smartDefer;
    job->quietPeriodMin  = data.quietPeriodMin;
    m_jobs.push_back(std::move(job));

    SaveJobs();
    m_jobList.Refresh();
    m_jobList.SetSelectedIndex((int)m_jobs.size() - 1);
    UpdateJobSelection();
}

void MainWindow::CmdEditJob()
{
    int idx = m_selJob;
    if (idx < 0 || idx >= (int)m_jobs.size()) return;

    auto& job = *m_jobs[idx];
    AddJobDlgData data;
    data.name            = job.name;
    data.sourcePath      = job.sourcePath;
    data.destPath        = job.destPath;
    data.scheduleType    = (int)job.scheduleType;
    data.scheduleValue   = job.scheduleValue;
    data.excludePatterns = job.excludePatterns;
    data.includePatterns = job.includePatterns;
    data.smartDefer      = job.smartDefer;
    data.quietPeriodMin  = job.quietPeriodMin;
    if (!ShowAddJobDialog(m_hwnd, m_hInst, data)) return;

    job.name            = data.name;
    job.sourcePath      = data.sourcePath;
    job.destPath        = data.destPath;
    job.scheduleType    = (ScheduleType)data.scheduleType;
    job.scheduleValue   = data.scheduleValue;
    job.excludePatterns = data.excludePatterns;
    job.includePatterns = data.includePatterns;
    job.smartDefer      = data.smartDefer;
    job.quietPeriodMin  = data.quietPeriodMin;

    SaveJobs();
    m_jobList.Refresh();
}

void MainWindow::CmdDeleteJob()
{
    int idx = m_selJob;
    if (idx < 0 || idx >= (int)m_jobs.size()) return;

    std::wstring msg = L"Delete job \"" + m_jobs[idx]->name + L"\"?";
    if (MessageBoxW(m_hwnd, msg.c_str(), L"Confirm Delete",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    // Stop if running
    CmdStopJob(idx);

    m_jobs.erase(m_jobs.begin() + idx);
    m_selJob = -1;
    m_jobList.SetSelectedIndex(-1);
    RefreshLogPanel();

    SaveJobs();
    m_jobList.Refresh();
}

void MainWindow::CmdRunJob(int idx)
{
    Log(L"CmdRunJob entry: idx=%d  m_selJob=%d  jobs=%d", idx, m_selJob, (int)m_jobs.size());
    if (idx < 0) idx = m_selJob;
    if (idx < 0 || idx >= (int)m_jobs.size()) {
        Log(L"CmdRunJob: no valid job index (%d), returning", idx);
        return;
    }

    auto& job = *m_jobs[idx];
    Log(L"CmdRunJob: job='%s' status=%d src='%s' dst='%s'",
        job.name.c_str(), (int)job.status, job.sourcePath.c_str(), job.destPath.c_str());

    // If paused, resume (clear the pause flag; thread wakes up on its own)
    if (job.status == JobStatus::Paused) {
        job.pauseFlag->store(false);
        UpdateToolbarState();
        return;
    }
    if (job.status == JobStatus::Copying || job.status == JobStatus::Scanning) {
        Log(L"CmdRunJob: already running, returning");
        return;
    }

    // Fresh start — reset both flags and all runtime state
    job.cancelFlag->store(false);
    job.pauseFlag->store(false);
    job.status        = JobStatus::Scanning;
    job.runStartTick  = GetTickCount64();
    job.stats         = {};
    job.logEntries.clear();
    job.curFileLogIdx = -1;

    // Add initial log entry
    LogEntry le;
    le.timestamp = TimestampNow();
    le.text      = L"Starting job: " + job.name;
    le.depth     = 0;
    le.isGroup   = false;
    le.expanded  = true;
    le.parentIdx = -1;
    job.logEntries.push_back(le);

    RefreshLogPanel();

    // Launch engine
    EngineParams ep;
    ep.hwndMain   = m_hwnd;
    ep.jobIndex   = idx;
    ep.sourcePath = job.sourcePath;
    ep.destPath   = job.destPath;
    ep.cancelFlag = job.cancelFlag;
    ep.pauseFlag  = job.pauseFlag;
    ep.excludePatterns = SplitPatterns(job.excludePatterns);
    ep.includePatterns = SplitPatterns(job.includePatterns);

    job.threadHandle = StartCopyEngine(ep);
    Log(L"CmdRunJob: StartCopyEngine returned handle=%p", (void*)job.threadHandle);
    m_jobList.Refresh();
    UpdateToolbarState();
}

void MainWindow::CmdPauseJob(int idx)
{
    if (idx < 0) idx = m_selJob;
    if (idx < 0 || idx >= (int)m_jobs.size()) return;
    auto& job = *m_jobs[idx];
    if (job.status == JobStatus::Scanning || job.status == JobStatus::Copying)
        job.pauseFlag->store(true);
    // Status update arrives via WM_JOB_PROGRESS when thread enters its pause loop
}

void MainWindow::CmdStopJob(int idx)
{
    if (idx < 0) idx = m_selJob;
    if (idx < 0 || idx >= (int)m_jobs.size()) return;
    auto& job = *m_jobs[idx];
    // If paused, clear pause first so the thread can see the cancel
    job.pauseFlag->store(false);
    job.cancelFlag->store(true);
    m_jobList.Refresh();
}

bool MainWindow::AnyJobActive() const
{
    for (const auto& j : m_jobs) {
        if (j->status == JobStatus::Scanning ||
            j->status == JobStatus::Copying  ||
            j->status == JobStatus::Paused)
            return true;
    }
    return false;
}

void MainWindow::UpdateToolbarState()
{
    if (!m_hwndBtnPlay || !m_hwndBtnPause || !m_hwndBtnStop) return;

    JobStatus status = JobStatus::Idle;
    if (m_selJob >= 0 && m_selJob < (int)m_jobs.size())
        status = m_jobs[m_selJob]->status;

    bool running = (status == JobStatus::Scanning || status == JobStatus::Copying);
    bool paused  = (status == JobStatus::Paused);

    // Play: enabled when idle/done/error/stopped (start) or paused (resume)
    EnableWindow(m_hwndBtnPlay,  !running);
    // Pause: enabled only while actively running
    EnableWindow(m_hwndBtnPause, running);
    // Stop: enabled while running or paused
    EnableWindow(m_hwndBtnStop,  running || paused);
}

// ── Thread message handlers ───────────────────────────────────────────────────
void MainWindow::OnJobProgress(int jobIdx, ProgressMsg* msg)
{
    if (jobIdx >= 0 && jobIdx < (int)m_jobs.size()) {
        m_jobs[jobIdx]->stats  = msg->stats;
        m_jobs[jobIdx]->status = msg->status;
        m_jobList.Refresh();
        if (jobIdx == m_selJob)
            UpdateToolbarState();
    }
    delete msg;
}

void MainWindow::OnJobLog(int jobIdx, LogMsg* msg)
{
    if (jobIdx >= 0 && jobIdx < (int)m_jobs.size()) {
        auto& job = *m_jobs[jobIdx];

        if (msg->replaceCurrentFile && job.curFileLogIdx >= 0) {
            // Overwrite the existing live-file slot in place
            LogEntry& le  = job.logEntries[job.curFileLogIdx];
            le.timestamp  = msg->timestamp;
            le.text       = msg->text;
            le.text2      = msg->text2;
        } else {
            // Append a new entry
            LogEntry le;
            le.timestamp = msg->timestamp;
            le.text      = msg->text;
            le.text2     = msg->text2;
            le.depth     = msg->depth;
            le.isGroup   = msg->isGroup;
            le.expanded  = true;
            le.parentIdx = msg->parentIdx;
            int newIdx = (int)job.logEntries.size();
            job.logEntries.push_back(le);

            if (msg->replaceCurrentFile)
                job.curFileLogIdx = newIdx;
        }

        // Throttled combined-log refresh (at most every 50ms)
        ULONGLONG now = GetTickCount64();
        if (now - m_lastLogRefresh >= 50) {
            RefreshLogPanel();
        }
    }
    delete msg;
}

void MainWindow::OnJobDone(int jobIdx, ULONGLONG errors)
{
    if (jobIdx < 0 || jobIdx >= (int)m_jobs.size()) return;
    auto& job = *m_jobs[jobIdx];

    if (job.threadHandle) {
        CloseHandle(job.threadHandle);
        job.threadHandle = nullptr;
    }

    job.status = (job.cancelFlag->load()) ? JobStatus::Stopped
               : (errors > 0)            ? JobStatus::Error
               :                           JobStatus::Done;

    // Update "last run" display
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[64];
    swprintf_s(buf, L"%02d:%02d on %04d-%02d-%02d",
               st.wHour, st.wMinute, st.wYear, st.wMonth, st.wDay);
    job.lastRunTime = buf;

    m_jobList.Refresh();
    RefreshLogPanel();   // final log update (un-throttled)
    UpdateToolbarState();
    UpdateTrayIcon();
    SaveJobs();   // persist updated stats + last-run time

    // Tray balloon notification if minimized
    if (m_minimizedToTray && m_trayIconActive) {
        m_nid.uFlags = NIF_INFO;
        m_nid.dwInfoFlags = (errors > 0) ? NIIF_WARNING : NIIF_INFO;
        swprintf_s(m_nid.szInfoTitle, L"Job: %s", job.name.c_str());
        if (errors > 0)
            swprintf_s(m_nid.szInfo, L"Completed with %llu errors", errors);
        else
            wcscpy_s(m_nid.szInfo, L"Completed successfully");
        Shell_NotifyIconW(NIM_MODIFY, &m_nid);
        m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;  // restore flags
    }
}

// ── Destination write permission check / UAC fix ──────────────────────────────
LRESULT MainWindow::OnJobAccessCheck(AccessCheckInfo* info)
{
    // Strip the \\?\ long-path prefix for display and for passing to icacls.
    std::wstring displayPath = info->destPath;
    if (displayPath.size() >= 4 &&
        displayPath[0] == L'\\' && displayPath[1] == L'\\' &&
        displayPath[2] == L'?'  && displayPath[3] == L'\\')
    {
        displayPath = displayPath.substr(4);
    }

    // Read-only media cannot be fixed via icacls — just tell the user.
    if (info->winError == ERROR_WRITE_PROTECT) {
        wchar_t buf[512];
        swprintf_s(buf,
            L"The destination is on write-protected (read-only) media and cannot be written to:\n\n"
            L"  %s\n\n"
            L"Please choose a different destination folder.",
            displayPath.c_str());
        MessageBoxW(m_hwnd, buf, L"Destination Is Read-Only", MB_OK | MB_ICONERROR);
        return IDNO;
    }

    // For all other errors (typically ERROR_ACCESS_DENIED), offer a UAC-elevated fix.
    wchar_t errName[64];
    swprintf_s(errName, L"0x%08X", info->winError);
    if (info->winError == ERROR_ACCESS_DENIED) wcscpy_s(errName, L"Access denied");

    wchar_t body[1024];
    swprintf_s(body,
        L"The destination folder is not writable:\n\n"
        L"  %s\n\n"
        L"Windows error: %s\n\n"
        L"This is usually caused by missing NTFS write permissions. "
        L"Click 'Fix Permissions' to grant write access to this folder "
        L"(requires administrator approval).",
        displayPath.c_str(), errName);

    // Use TaskDialogIndirect for a proper Windows-style dialog with descriptive buttons.
    const TASKDIALOG_BUTTON buttons[] = {
        { IDYES, L"Fix Permissions\n(Requires administrator approval)" },
        { IDNO,  L"Cancel" },
    };
    TASKDIALOGCONFIG tdc   = { sizeof(tdc) };
    tdc.hwndParent         = m_hwnd;
    tdc.pszWindowTitle     = L"File Copy Utility";
    tdc.pszMainIcon        = TD_WARNING_ICON;
    tdc.pszMainInstruction = L"Destination folder is not writable";
    tdc.pszContent         = body;
    tdc.pButtons           = buttons;
    tdc.cButtons           = 2;
    tdc.nDefaultButton     = IDNO;
    tdc.dwFlags            = TDF_USE_COMMAND_LINKS;

    int nBtn = IDNO;
    if (FAILED(TaskDialogIndirect(&tdc, &nBtn, nullptr, nullptr)))
        nBtn = IDNO;

    if (nBtn != IDYES)
        return IDNO;

    // Launch an elevated cmd.exe that runs icacls to grant BUILTIN\Users full control.
    // We use the SDDL SID *S-1-5-32-545 so this works regardless of the system language.
    wchar_t params[1024];
    swprintf_s(params,
        L"/c icacls \"%s\" /grant *S-1-5-32-545:(OI)(CI)F /T",
        displayPath.c_str());

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask             = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd              = m_hwnd;
    sei.lpVerb            = L"runas";   // triggers UAC elevation
    sei.lpFile            = L"cmd.exe";
    sei.lpParameters      = params;
    sei.nShow             = SW_HIDE;

    if (!ShellExecuteExW(&sei)) {
        DWORD err = GetLastError();
        wchar_t msg[256];
        swprintf_s(msg,
            L"Could not launch the elevated permission-fix process (0x%08X).\n\n"
            L"Try running File Copy Utility as administrator.",
            err);
        MessageBoxW(m_hwnd, msg, L"Permission Fix Failed", MB_OK | MB_ICONERROR);
        return IDNO;
    }

    // Wait up to 30 s for icacls to finish, then tell the engine to retry.
    WaitForSingleObject(sei.hProcess, 30000);
    CloseHandle(sei.hProcess);
    return IDYES;
}

// ── Splitter drag ─────────────────────────────────────────────────────────────
void MainWindow::OnMouseMove(int x, int y, DWORD keys)
{
    if (m_draggingSplit) {
        RECT rc; GetClientRect(m_hwnd, &rc);
        m_splitterY = y - m_dragOffset;
        RepositionChildren();
        SetCursor(m_cursorNS);
    }
}

void MainWindow::OnLButtonDown(int x, int y)
{
    if (IsInSplitter(y)) {
        m_draggingSplit = true;
        m_dragOffset    = y - m_splitterY;
        SetCapture(m_hwnd);
        SetCursor(m_cursorNS);
    }
}

void MainWindow::OnLButtonUp(int x, int y)
{
    if (m_draggingSplit) {
        m_draggingSplit = false;
        ReleaseCapture();
    }
}

bool MainWindow::OnSetCursor(HWND, UINT hitCode)
{
    POINT pt; GetCursorPos(&pt); ScreenToClient(m_hwnd, &pt);
    if (m_draggingSplit || IsInSplitter(pt.y)) {
        SetCursor(m_cursorNS);
        return true;
    }
    if (AnyJobActive() && hitCode == HTCLIENT) {
        SetCursor(m_cursorBusy);
        return true;
    }
    return false;
}

// ── WM_PAINT (splitter bar) ───────────────────────────────────────────────────
static void PaintSplitter(HWND hwnd, int splitterY, int splitH)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);
    // Only paint the splitter strip to avoid flicker over children
    RECT sRc = { 0, splitterY, rc.right, splitterY + splitH };
    if (IntersectRect(&sRc, &sRc, &ps.rcPaint)) {
        HBRUSH hbr = CreateSolidBrush(RGB(195, 202, 218));
        FillRect(hdc, &sRc, hbr);
        DeleteObject(hbr);
        // Subtle top/bottom highlight lines
        HPEN topP = CreatePen(PS_SOLID, 1, RGB(220, 226, 240));
        HPEN botP = CreatePen(PS_SOLID, 1, RGB(170, 178, 198));
        HPEN oldP = (HPEN)SelectObject(hdc, topP);
        MoveToEx(hdc, 0, splitterY, nullptr);
        LineTo(hdc, rc.right, splitterY);
        SelectObject(hdc, botP);
        MoveToEx(hdc, 0, splitterY + splitH - 1, nullptr);
        LineTo(hdc, rc.right, splitterY + splitH - 1);
        SelectObject(hdc, oldP);
        DeleteObject(topP);
        DeleteObject(botP);
    }
    EndPaint(hwnd, &ps);
}

// ── Tray icon ─────────────────────────────────────────────────────────────────

void MainWindow::CreateTrayIcon()
{
    m_nid.cbSize           = sizeof(m_nid);
    m_nid.hWnd             = m_hwnd;
    m_nid.uID              = 1;
    m_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon            = LoadIconW(m_hInst, MAKEINTRESOURCEW(IDI_APP));
    wcscpy_s(m_nid.szTip, L"File Copy Utility");
    Shell_NotifyIconW(NIM_ADD, &m_nid);
    m_trayIconActive = true;
}

void MainWindow::UpdateTrayIcon()
{
    if (!m_trayIconActive) return;
    const wchar_t* tip = L"File Copy Utility - Idle";
    if (AnyJobActive()) tip = L"File Copy Utility - Running...";
    wcscpy_s(m_nid.szTip, tip);
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void MainWindow::RemoveTrayIcon()
{
    if (!m_trayIconActive) return;
    Shell_NotifyIconW(NIM_DELETE, &m_nid);
    m_trayIconActive = false;
}

void MainWindow::ShowTrayMenu()
{
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_OPEN,     L"&Open");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_RUNALL,   L"&Run All Jobs");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_PAUSEALL, L"&Pause All");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT,     L"E&xit");

    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(m_hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);
}

void MainWindow::MinimizeToTray()
{
    ShowWindow(m_hwnd, SW_HIDE);
    m_minimizedToTray = true;
    if (!m_trayIconActive) CreateTrayIcon();
    UpdateTrayIcon();
}

void MainWindow::RestoreFromTray()
{
    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);
    m_minimizedToTray = false;
}

void MainWindow::OnTrayIcon(LPARAM lp)
{
    UINT msg = LOWORD(lp);
    if (msg == WM_LBUTTONDBLCLK) {
        RestoreFromTray();
    } else if (msg == WM_RBUTTONUP) {
        ShowTrayMenu();
    }
}

bool MainWindow::OnClose()
{
    if (m_closeToTray) {
        MinimizeToTray();
        return true;  // handled — don't destroy
    }
    if (AnyJobActive()) {
        int result = MessageBoxW(m_hwnd,
            L"Jobs are still running. Quit and stop all jobs?",
            L"Confirm Exit", MB_YESNO | MB_ICONQUESTION);
        if (result != IDYES) return true;
    }
    return false;  // proceed with destroy
}

void MainWindow::CmdRunAll()
{
    for (int i = 0; i < (int)m_jobs.size(); i++) {
        auto& job = *m_jobs[i];
        if (job.status == JobStatus::Idle || job.status == JobStatus::Done ||
            job.status == JobStatus::Error || job.status == JobStatus::Stopped)
            CmdRunJob(i);
    }
}

void MainWindow::CmdPauseAll()
{
    for (int i = 0; i < (int)m_jobs.size(); i++)
        CmdPauseJob(i);
}

// ── Scheduling ────────────────────────────────────────────────────────────────

void MainWindow::StartScheduleTimer()
{
    // Poll every 60 seconds to check schedules
    SetTimer(m_hwnd, IDT_SCHEDULE_POLL, 60000, nullptr);
    m_lastScheduleCheck = GetTickCount64();
}

void MainWindow::CheckSchedules()
{
    ULONGLONG now = GetTickCount64();

    // Ensure m_lastScheduledRun is sized properly
    while (m_lastScheduledRun.size() < m_jobs.size())
        m_lastScheduledRun.push_back(0);

    for (int i = 0; i < (int)m_jobs.size(); i++) {
        auto& job = *m_jobs[i];
        if (job.scheduleType == ScheduleType::Manual) continue;
        if (job.status != JobStatus::Idle && job.status != JobStatus::Done &&
            job.status != JobStatus::Error && job.status != JobStatus::Stopped)
            continue;  // already running

        bool shouldRun = false;

        if (job.scheduleType == ScheduleType::Interval) {
            ULONGLONG intervalMs = (ULONGLONG)job.scheduleValue * 60 * 1000;
            if (now - m_lastScheduledRun[i] >= intervalMs)
                shouldRun = true;
        } else if (job.scheduleType == ScheduleType::Daily) {
            SYSTEMTIME st; GetLocalTime(&st);
            int nowHHMM = st.wHour * 100 + st.wMinute;
            int targetHHMM = job.scheduleValue;
            // Check if we're within the target minute and haven't run in the last 2 minutes
            if (nowHHMM == targetHHMM && (now - m_lastScheduledRun[i]) > 120000)
                shouldRun = true;
        }

        if (shouldRun) {
            // Smart defer: check if source is actively changing (via FolderWatcher)
            if (job.smartDefer && i < (int)m_watchers.size() && m_watchers[i]) {
                ULONGLONG lastAct = m_watchers[i]->LastActivityTick();
                if (lastAct > 0) {
                    ULONGLONG quietMs = (ULONGLONG)job.quietPeriodMin * 60 * 1000;
                    if (now - lastAct < quietMs) {
                        ULONGLONG remaining = quietMs - (now - lastAct);
                        int remMin = (int)(remaining / 60000);
                        wchar_t buf[64];
                        swprintf_s(buf, L"Deferred (quiet in %dm %ds)",
                                   remMin, (int)((remaining % 60000) / 1000));
                        job.nextRunTime = buf;
                        m_jobList.Refresh();
                        continue;
                    }
                }
            }

            m_lastScheduledRun[i] = now;
            Log(L"Schedule: auto-running job '%s'", job.name.c_str());
            CmdRunJob(i);
        }

        // Update next-run display
        if (job.scheduleType == ScheduleType::Interval && !shouldRun) {
            ULONGLONG intervalMs = (ULONGLONG)job.scheduleValue * 60 * 1000;
            ULONGLONG remaining = intervalMs - (now - m_lastScheduledRun[i]);
            int remMin = (int)(remaining / 60000);
            wchar_t buf[64];
            swprintf_s(buf, L"in %d min", remMin > 0 ? remMin : 1);
            job.nextRunTime = buf;
        } else if (job.scheduleType == ScheduleType::Daily) {
            wchar_t buf[32];
            int h = job.scheduleValue / 100;
            int m = job.scheduleValue % 100;
            int h12 = h % 12; if (h12 == 0) h12 = 12;
            swprintf_s(buf, L"daily at %d:%02d %s", h12, m, h < 12 ? L"AM" : L"PM");
            job.nextRunTime = buf;
        }
    }
    m_jobList.Refresh();
}

void MainWindow::OnTimer(UINT_PTR timerId)
{
    if (timerId == IDT_SCHEDULE_POLL) {
        CheckSchedules();
    }
}

void MainWindow::OnPowerBroadcast(WPARAM event)
{
    if (event == PBT_APMRESUMEAUTOMATIC || event == PBT_APMRESUMESUSPEND) {
        Log(L"System resumed from sleep — re-checking schedules");
        CheckSchedules();
    }
}

void MainWindow::UpdateStartWithWindows()
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
    {
        if (m_startWithWindows) {
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            std::wstring val = std::wstring(L"\"") + exePath + L"\" --minimized";
            RegSetValueExW(hKey, L"FileCopyUtility", 0, REG_SZ,
                           (const BYTE*)val.c_str(),
                           (DWORD)((val.size() + 1) * sizeof(wchar_t)));
        } else {
            RegDeleteValueW(hKey, L"FileCopyUtility");
        }
        RegCloseKey(hKey);
    }
}

// ── Settings dialog ──────────────────────────────────────────────────────────

struct SettingsDlgCtx {
    bool closeToTray;
    bool startWithWindows;
};

static INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* ctx = reinterpret_cast<SettingsDlgCtx*>(GetWindowLongPtrW(hDlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG:
        ctx = reinterpret_cast<SettingsDlgCtx*>(lp);
        SetWindowLongPtrW(hDlg, DWLP_USER, (LONG_PTR)ctx);
        CheckDlgButton(hDlg, IDC_CHK_CLOSE_TO_TRAY,  ctx->closeToTray ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHK_START_WITH_WIN, ctx->startWithWindows ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            ctx->closeToTray     = IsDlgButtonChecked(hDlg, IDC_CHK_CLOSE_TO_TRAY)  == BST_CHECKED;
            ctx->startWithWindows = IsDlgButtonChecked(hDlg, IDC_CHK_START_WITH_WIN) == BST_CHECKED;
            EndDialog(hDlg, IDOK);
        } else if (LOWORD(wp) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
        }
        return TRUE;
    }
    return FALSE;
}

void MainWindow::ShowSettingsDialog()
{
    SettingsDlgCtx ctx{ m_closeToTray, m_startWithWindows };
    INT_PTR result = DialogBoxParamW(m_hInst, MAKEINTRESOURCE(IDD_SETTINGS),
                                      m_hwnd, SettingsDlgProc, (LPARAM)&ctx);
    if (result == IDOK) {
        m_closeToTray      = ctx.closeToTray;
        m_startWithWindows = ctx.startWithWindows;
        UpdateStartWithWindows();
        SaveJobs();
    }
}

// ── Commands ─────────────────────────────────────────────────────────────────
void MainWindow::OnCommand(int id, HWND)
{
    switch (id) {
    case ID_FILE_NEWJOB:    CmdNewJob();    break;
    case ID_FILE_EDITJOB:   CmdEditJob();   break;
    case ID_FILE_DELETEJOB: CmdDeleteJob(); break;
    case ID_FILE_RUNJOB:
    case ID_BTN_PLAY:       CmdRunJob();    break;
    case ID_BTN_PAUSE:      CmdPauseJob();  break;
    case ID_FILE_STOPJOB:
    case ID_BTN_STOPSQ:     CmdStopJob();   break;
    case ID_FILE_EXIT:      DestroyWindow(m_hwnd); break;
    case ID_TRAY_OPEN:      RestoreFromTray(); break;
    case ID_TRAY_RUNALL:    CmdRunAll();    break;
    case ID_TRAY_PAUSEALL:  CmdPauseAll();  break;
    case ID_TRAY_EXIT:      m_closeToTray = false; DestroyWindow(m_hwnd); break;
    case ID_HELP_ABOUT:
        MessageBoxW(m_hwnd,
            L"File Copy Utility v2.1\n\n"
            L"A lightweight backup and file copy tool.\n"
            L"Built with pure Win32 API + Claude.\n\n"
            L"https://github.com/wpostma/VibeCodedFileCopyForWindows",
            L"About File Copy Utility", MB_OK | MB_ICONINFORMATION);
        break;
    case ID_OPTIONS_SETTINGS:
        ShowSettingsDialog();
        break;
    }
}

void MainWindow::OnNotify(NMHDR* nm)
{
    if (nm->idFrom == IDC_JOBLIST && nm->code == NM_DBLCLK)
        CmdEditJob();
}

// ── Destroy ──────────────────────────────────────────────────────────────────
void MainWindow::OnDestroy()
{
    KillTimer(m_hwnd, IDT_SCHEDULE_POLL);
    RemoveTrayIcon();

    // Stop all folder watchers
    m_watchers.clear();

    // Stop all running jobs
    for (int i = 0; i < (int)m_jobs.size(); i++) {
        m_jobs[i]->cancelFlag->store(true);
        if (m_jobs[i]->threadHandle) {
            WaitForSingleObject(m_jobs[i]->threadHandle, 5000);
            CloseHandle(m_jobs[i]->threadHandle);
            m_jobs[i]->threadHandle = nullptr;
        }
    }
    if (m_fontUI)     DeleteObject(m_fontUI);
    if (m_fontUIBold) DeleteObject(m_fontUIBold);
    PostQuitMessage(0);
}

// ── Window proc ───────────────────────────────────────────────────────────────
LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    MainWindow* pThis = nullptr;

    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        pThis = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->m_hwnd = hwnd;
        pThis->OnCreate();
        return 0;
    }

    pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!pThis) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_SIZE:
        pThis->OnSize(LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_PAINT:
        PaintSplitter(hwnd, pThis->m_splitterY, pThis->k_splitH);
        return 0;

    case WM_ERASEBKGND:
        // Let children paint themselves; only erase the background behind the splitter
        return 1;

    case WM_COMMAND:
        Log(L"WM_COMMAND id=%d hwndCtl=%p notif=%d", LOWORD(wp), (void*)lp, HIWORD(wp));
        pThis->OnCommand(LOWORD(wp), (HWND)lp);
        return 0;

    case WM_NOTIFY:
        pThis->OnNotify((NMHDR*)lp);
        return 0;

    case WM_MOUSEMOVE:
        pThis->OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), (DWORD)wp);
        return 0;

    case WM_LBUTTONDOWN:
        pThis->OnLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_LBUTTONUP:
        pThis->OnLButtonUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_SETCURSOR:
        if (pThis->OnSetCursor((HWND)wp, HIWORD(lp)))
            return TRUE;
        break;

    case WM_JOB_PROGRESS:
        pThis->OnJobProgress((int)wp, reinterpret_cast<ProgressMsg*>(lp));
        return 0;

    case WM_JOB_LOG:
        pThis->OnJobLog((int)wp, reinterpret_cast<LogMsg*>(lp));
        return 0;

    case WM_JOB_DONE:
        pThis->OnJobDone((int)wp, (ULONGLONG)lp);
        return 0;

    case WM_JOB_SPACE_CHECK: {
        auto* info = reinterpret_cast<SpaceCheckInfo*>(lp);
        double freeGB   = info->freeBytes   / (1024.0 * 1024.0 * 1024.0);
        double neededGB = info->neededBytes / (1024.0 * 1024.0 * 1024.0);
        wchar_t buf[256];
        swprintf_s(buf,
            L"Destination has %.1f GB free.\n"
            L"A full copy could need up to %.1f GB.\n"
            L"(Files already copied will be skipped.)\n\n"
            L"Continue anyway?",
            freeGB, neededGB);
        return MessageBoxW(pThis->m_hwnd, buf, L"Low Disk Space",
                           MB_YESNO | MB_ICONWARNING);
    }

    case WM_JOB_ACCESS_CHECK:
        return pThis->OnJobAccessCheck(reinterpret_cast<AccessCheckInfo*>(lp));

    case WM_TIMER:
        pThis->OnTimer((UINT_PTR)wp);
        return 0;

    case WM_TRAYICON:
        pThis->OnTrayIcon(lp);
        return 0;

    case WM_POWERBROADCAST:
        pThis->OnPowerBroadcast(wp);
        break;

    case WM_CLOSE:
        if (pThis->OnClose())
            return 0;  // minimized to tray, don't destroy
        break;

    case WM_DESTROY:
        pThis->OnDestroy();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
