#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <string>
#include "JobListPanel.h"

static const wchar_t* k_ClassName = L"FCU_JobList";

// ── Color palette ─────────────────────────────────────────────────────────────
namespace JC {
    const COLORREF BgOdd     = RGB(241, 246, 255);
    const COLORREF BgEven    = RGB(233, 240, 254);
    const COLORREF BgSel     = RGB(180, 205, 245);
    const COLORREF BgSelHot  = RGB(160, 190, 240);
    const COLORREF Border    = RGB(205, 215, 232);
    const COLORREF TxtName   = RGB( 16,  24,  56);
    const COLORREF TxtSub    = RGB(100, 112, 140);
    const COLORREF TxtStats  = RGB( 50,  60,  85);
    const COLORREF TxtErr    = RGB(180,  40,  40);
    const COLORREF RunBar    = RGB( 60, 130, 220);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::wstring FmtBytes(ULONGLONG b)
{
    wchar_t buf[32];
    if      (b >= (1ULL<<40)) swprintf_s(buf, L"%.2f TB", b/(double)(1ULL<<40));
    else if (b >= (1ULL<<30)) swprintf_s(buf, L"%.2f GB", b/(double)(1ULL<<30));
    else if (b >= (1ULL<<20)) swprintf_s(buf, L"%.2f MB", b/(double)(1ULL<<20));
    else if (b >= (1ULL<<10)) swprintf_s(buf, L"%.1f KB", b/(double)(1ULL<<10));
    else                      swprintf_s(buf, L"%llu B",  b);
    return buf;
}

static const wchar_t* StatusLabel(JobStatus s)
{
    switch (s) {
    case JobStatus::Scanning: return L"Scanning...";
    case JobStatus::Copying:  return L"Copying...";
    case JobStatus::Paused:   return L"Paused";
    case JobStatus::Done:     return L"Done";
    case JobStatus::Error:    return L"Error";
    case JobStatus::Stopped:  return L"Stopped";
    default:                  return L"Idle";
    }
}

// ── Registration ──────────────────────────────────────────────────────────────

bool JobListPanel::RegisterClass(HINSTANCE hInst)
{
    WNDCLASSEXW wc  = { sizeof(wc) };
    wc.style        = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc  = WndProc;
    wc.hInstance    = hInst;
    wc.hCursor      = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = k_ClassName;
    return RegisterClassExW(&wc) != 0;
}

// ── Create / Destroy ──────────────────────────────────────────────────────────

bool JobListPanel::Create(HWND parent, int id,
                          std::vector<std::unique_ptr<CopyJob>>* jobs)
{
    m_jobs = jobs;
    m_hwnd = CreateWindowExW(0, k_ClassName, L"",
                WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                0, 0, 100, 100,
                parent, (HMENU)(UINT_PTR)id,
                (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE),
                this);
    return m_hwnd != nullptr;
}

void JobListPanel::Destroy()
{
    if (m_fontName) { DeleteObject(m_fontName); m_fontName = nullptr; }
    if (m_fontSub)  { DeleteObject(m_fontSub);  m_fontSub  = nullptr; }
    if (m_hwnd)     { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
}

void JobListPanel::ComputeRowHeight()
{
    HDC dc = GetDC(m_hwnd);
    TEXTMETRICW tm;
    HFONT old = (HFONT)SelectObject(dc, m_fontName);
    GetTextMetrics(dc, &tm);
    m_nameH = tm.tmHeight;
    SelectObject(dc, m_fontSub);
    GetTextMetrics(dc, &tm);
    m_subH = tm.tmHeight;
    SelectObject(dc, old);
    ReleaseDC(m_hwnd, dc);
    m_gapH = max(3, m_subH / 3);
    int pad = max(6, m_nameH * 6 / 10);
    m_rowH  = pad + m_nameH + m_gapH + m_subH + pad;
}

void JobListPanel::Refresh() { if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE); }

void JobListPanel::SetSelectedIndex(int i)
{
    m_selIdx = i;
    Refresh();
}

// ── Hit test / scroll ─────────────────────────────────────────────────────────

int JobListPanel::HitTestRow(int y) const
{
    int absY = y + m_scrollY;
    int idx  = m_rowH > 0 ? absY / m_rowH : -1;
    if (!m_jobs) return -1;
    int n = (int)m_jobs->size();
    return (idx >= 0 && idx < n) ? idx : -1;
}

void JobListPanel::ScrollBy(int lines)
{
    if (!m_jobs) return;
    RECT rc; GetClientRect(m_hwnd, &rc);
    int maxScroll = (int)m_jobs->size() * m_rowH - (rc.bottom - rc.top);
    if (maxScroll < 0) maxScroll = 0;
    m_scrollY = max(0, min(m_scrollY + lines * m_rowH, maxScroll));
    Refresh();
}

// ── Font helper ───────────────────────────────────────────────────────────────

HFONT JobListPanel::CreateFont_(int ptSize, bool bold)
{
    LOGFONTW lf = {};
    lf.lfHeight  = -MulDiv(ptSize, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72);
    lf.lfWeight  = bold ? FW_BOLD : FW_NORMAL;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&lf);
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void JobListPanel::PaintRow(HDC dc, const RECT& rc, int idx, bool selected)
{
    const CopyJob& job = *(*m_jobs)[idx];

    // Background
    COLORREF bg = selected ? JC::BgSel : ((idx & 1) ? JC::BgEven : JC::BgOdd);
    HBRUSH hbr = CreateSolidBrush(bg);
    FillRect(dc, &rc, hbr);
    DeleteObject(hbr);

    // Bottom border
    HPEN hpen = CreatePen(PS_SOLID, 1, JC::Border);
    HPEN hOld = (HPEN)SelectObject(dc, hpen);
    MoveToEx(dc, rc.left,  rc.bottom - 1, nullptr);
    LineTo  (dc, rc.right, rc.bottom - 1);
    SelectObject(dc, hOld);
    DeleteObject(hpen);

    // Left edge stripe: blue=running, amber=paused
    if (job.status == JobStatus::Copying || job.status == JobStatus::Scanning) {
        HBRUSH hBar = CreateSolidBrush(JC::RunBar);
        RECT barRc = { rc.left, rc.top, rc.left + 3, rc.bottom - 1 };
        FillRect(dc, &barRc, hBar);
        DeleteObject(hBar);
    } else if (job.status == JobStatus::Paused) {
        HBRUSH hBar = CreateSolidBrush(RGB(210, 140, 30));
        RECT barRc = { rc.left, rc.top, rc.left + 3, rc.bottom - 1 };
        FillRect(dc, &barRc, hBar);
        DeleteObject(hBar);
    }

    SetBkMode(dc, TRANSPARENT);
    const int padX = 12;
    // Vertical padding: whatever space remains above/below the two text lines
    const int padY  = (m_rowH - m_nameH - m_gapH - m_subH) / 2;
    const int row2Y = padY + m_nameH + m_gapH;

    // ── Row 1 left: job name ──────────────────────────────────────────────────
    SelectObject(dc, m_fontName);
    SetTextColor(dc, JC::TxtName);
    RECT r1L = { rc.left + padX, rc.top + padY,
                 rc.right - 260, rc.top + padY + m_nameH };
    DrawTextW(dc, job.name.c_str(), -1, &r1L, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    // ── Row 1 right: "X hours ago · N changes · no errors" ────────────────────
    SelectObject(dc, m_fontSub);
    std::wstring r1Right;
    if (!job.lastRunTime.empty()) {
        r1Right = job.lastRunTime + L"  \xB7  ";
        wchar_t tmp[64];
        swprintf_s(tmp, L"%llu changes", job.stats.changeCount);
        r1Right += tmp;
        r1Right += (job.stats.errorCount == 0) ? L"  \xB7  no errors" : L"  \xB7  errors";
    } else {
        r1Right = L"Never run";
    }
    SetTextColor(dc, JC::TxtStats);
    RECT r1R = { rc.left + padX, rc.top + padY, rc.right - padX, rc.top + padY + m_nameH };
    DrawTextW(dc, r1Right.c_str(), -1, &r1R, DT_RIGHT | DT_SINGLELINE);

    // ── Row 2 left: "Next scan in..." or status ───────────────────────────────
    SetTextColor(dc, JC::TxtSub);
    std::wstring r2Left;
    if (job.status == JobStatus::Idle || job.status == JobStatus::Done) {
        r2Left = job.nextRunTime.empty() ? L"Ready" : (L"Next scan " + job.nextRunTime);
    } else if (job.status == JobStatus::Copying && job.stats.bytesToCopy > 0
               && job.stats.copiedBytes > 0) {
        int pct = (int)(job.stats.copiedBytes * 100 / job.stats.bytesToCopy);
        if (pct > 100) pct = 100;

        // Estimate end time from copy rate
        ULONGLONG elapsed = GetTickCount64() - job.runStartTick;
        std::wstring eta;
        if (elapsed > 3000 && job.stats.copiedBytes > 0) {
            // remaining ms = elapsed * bytesLeft / bytesDone
            ULONGLONG bytesLeft = job.stats.bytesToCopy - job.stats.copiedBytes;
            ULONGLONG remainMs  = elapsed * bytesLeft / job.stats.copiedBytes;

            // Add 15 min padding, then round up to nearest 15 min.
            remainMs += 15ULL * 60 * 1000;

            SYSTEMTIME now;
            GetLocalTime(&now);
            FILETIME ft;
            SystemTimeToFileTime(&now, &ft);
            ULARGE_INTEGER uli;
            uli.LowPart  = ft.dwLowDateTime;
            uli.HighPart = ft.dwHighDateTime;
            uli.QuadPart += remainMs * 10000ULL;   // FILETIME = 100ns ticks
            ft.dwLowDateTime  = uli.LowPart;
            ft.dwHighDateTime = uli.HighPart;
            SYSTEMTIME st;
            FileTimeToSystemTime(&ft, &st);

            // Round up to next 15-minute boundary
            int m = st.wMinute;
            if (st.wSecond > 0 || st.wMilliseconds > 0)
                m++;   // any partial minute counts as next
            int rounded = ((m + 14) / 15) * 15;  // ceil to 15
            int extraMin = rounded - st.wMinute;
            if (extraMin > 0) {
                uli.LowPart  = ft.dwLowDateTime;
                uli.HighPart = ft.dwHighDateTime;
                uli.QuadPart += (ULONGLONG)extraMin * 60ULL * 10000000ULL;
                ft.dwLowDateTime  = uli.LowPart;
                ft.dwHighDateTime = uli.HighPart;
                FileTimeToSystemTime(&ft, &st);
            }
            st.wSecond = 0; st.wMilliseconds = 0;

            int h12  = st.wHour % 12;
            if (h12 == 0) h12 = 12;
            const wchar_t* ampm = (st.wHour < 12) ? L"AM" : L"PM";

            wchar_t etaBuf[64];
            if (st.wYear == now.wYear && st.wMonth == now.wMonth && st.wDay == now.wDay)
                swprintf_s(etaBuf, L", est. done by %d:%02d %s",
                           h12, st.wMinute, ampm);
            else
                swprintf_s(etaBuf, L", est. done by %d:%02d %s %d/%d",
                           h12, st.wMinute, ampm, st.wMonth, st.wDay);
            eta = etaBuf;
        }

        wchar_t buf[128];
        swprintf_s(buf, L"Copying... (%d%% complete%s)", pct, eta.c_str());
        r2Left = buf;
    } else {
        r2Left = StatusLabel(job.status);
    }
    RECT r2L = { rc.left + padX, rc.top + row2Y,
                 rc.right - 260, rc.top + row2Y + m_subH };
    DrawTextW(dc, r2Left.c_str(), -1, &r2L, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    // ── Row 2 right: "Z GB · M files · N folders" ────────────────────────────
    std::wstring r2Right;
    if (job.stats.totalFiles > 0 || job.stats.copiedBytes > 0) {
        wchar_t tmp[128];
        swprintf_s(tmp, L"%s  \xB7  %llu files  \xB7  %llu folders",
            FmtBytes(job.stats.copiedBytes > 0 ? job.stats.copiedBytes : job.stats.totalBytes).c_str(),
            job.stats.copiedFiles > 0 ? job.stats.copiedFiles : job.stats.totalFiles,
            job.stats.totalFolders);
        r2Right = tmp;
    }
    if (!r2Right.empty()) {
        SetTextColor(dc, JC::TxtSub);
        RECT r2R = { rc.left + padX, rc.top + row2Y,
                     rc.right - padX, rc.top + row2Y + m_subH };
        DrawTextW(dc, r2Right.c_str(), -1, &r2R, DT_RIGHT | DT_SINGLELINE);
    }
}

void JobListPanel::OnPaint()
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hwnd, &ps);

    RECT rcClient; GetClientRect(m_hwnd, &rcClient);
    int W = rcClient.right, H = rcClient.bottom;

    // Double-buffer
    HDC memDC  = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, W, H);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, bmp);

    // Background fill
    HBRUSH hBg = CreateSolidBrush(JC::BgOdd);
    FillRect(memDC, &rcClient, hBg);
    DeleteObject(hBg);

    if (m_jobs) {
        int n = (int)m_jobs->size();
        for (int i = 0; i < n; i++) {
            int top    = i * m_rowH - m_scrollY;
            int bottom = top + m_rowH;
            if (bottom < 0 || top > H) continue;

            RECT rowRc = { 0, top, W, bottom };
            PaintRow(memDC, rowRc, i, i == m_selIdx);
        }

        // Empty state
        if (n == 0) {
            SelectObject(memDC, m_fontSub ? m_fontSub : (HGDIOBJ)GetStockObject(DEFAULT_GUI_FONT));
            SetTextColor(memDC, JC::TxtSub);
            SetBkMode(memDC, TRANSPARENT);
            RECT msg = rcClient;
            DrawTextW(memDC, L"No copy jobs.  Use File \x2192 New Job to add one.",
                      -1, &msg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(bmp);
    DeleteDC(memDC);

    EndPaint(m_hwnd, &ps);
}

// ── Input ─────────────────────────────────────────────────────────────────────

void JobListPanel::OnLButtonDown(int x, int y)
{
    int idx = HitTestRow(y);
    if (idx != m_selIdx) {
        m_selIdx = idx;
        Refresh();
        if (m_selCb) m_selCb(m_selIdx, m_selCtx);
    }
    SetFocus(m_hwnd);
}

void JobListPanel::OnLButtonDblClk(int x, int y)
{
    int idx = HitTestRow(y);
    if (idx >= 0) {
        // Notify parent to open edit dialog
        NMHDR nm = { m_hwnd, (UINT_PTR)GetDlgCtrlID(m_hwnd), NM_DBLCLK };
        SendMessageW(GetParent(m_hwnd), WM_NOTIFY, nm.idFrom, (LPARAM)&nm);
    }
}

void JobListPanel::OnMouseWheel(int delta)
{
    ScrollBy(-(delta / WHEEL_DELTA));
}

void JobListPanel::OnSize(int, int) { Refresh(); }

// ── Window proc ───────────────────────────────────────────────────────────────

LRESULT CALLBACK JobListPanel::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    JobListPanel* pThis = nullptr;

    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        pThis = reinterpret_cast<JobListPanel*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->m_hwnd = hwnd;

        // Create fonts
        pThis->m_fontName = CreateFont_(10, true);
        pThis->m_fontSub  = CreateFont_( 9, false);
        pThis->ComputeRowHeight();
        return 0;
    }

    pThis = reinterpret_cast<JobListPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!pThis) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:      pThis->OnPaint(); return 0;
    case WM_SIZE:       pThis->OnSize(LOWORD(lp), HIWORD(lp)); return 0;
    case WM_LBUTTONDOWN: pThis->OnLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0;
    case WM_LBUTTONDBLCLK: pThis->OnLButtonDblClk(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0;
    case WM_MOUSEWHEEL:
        pThis->OnMouseWheel((short)HIWORD(wp));
        return 0;
    case WM_DESTROY:
        if (pThis->m_fontName) { DeleteObject(pThis->m_fontName); pThis->m_fontName = nullptr; }
        if (pThis->m_fontSub)  { DeleteObject(pThis->m_fontSub);  pThis->m_fontSub  = nullptr; }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
