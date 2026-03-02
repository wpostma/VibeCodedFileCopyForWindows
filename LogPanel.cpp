#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include "LogPanel.h"

static const wchar_t* k_ClassName = L"FCU_LogPanel";

// ── Color palette ─────────────────────────────────────────────────────────────
namespace LC {
    const COLORREF HdrBg     = RGB(230, 231, 236);
    const COLORREF HdrText   = RGB( 40,  45,  60);
    const COLORREF HdrBorder = RGB(195, 200, 212);
    const COLORREF RowOdd    = RGB(255, 255, 255);
    const COLORREF RowEven   = RGB(248, 249, 252);
    const COLORREF RowSelBg  = RGB(220, 230, 248);
    const COLORREF TsColor   = RGB(110, 120, 145);
    const COLORREF TxtRoot   = RGB( 25,  30,  50);
    const COLORREF TxtChild  = RGB( 80,  90, 115);
    const COLORREF TxtGrand  = RGB(120, 130, 155);
    const COLORREF TogFg     = RGB( 70,  80, 110);
    const COLORREF TogBorder = RGB(150, 160, 185);
    const COLORREF BgPanel   = RGB(245, 246, 250);
}

// ── Font helper ───────────────────────────────────────────────────────────────

HFONT LogPanel::MakeFont(int ptSize, bool mono)
{
    LOGFONTW lf = {};
    lf.lfHeight  = -MulDiv(ptSize, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72);
    lf.lfWeight  = FW_NORMAL;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, mono ? L"Consolas" : L"Segoe UI");
    return CreateFontIndirectW(&lf);
}

// ── Registration ──────────────────────────────────────────────────────────────

bool LogPanel::RegisterClass(HINSTANCE hInst)
{
    WNDCLASSEXW wc   = { sizeof(wc) };
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = k_ClassName;
    return RegisterClassExW(&wc) != 0;
}

// ── Create / Destroy ──────────────────────────────────────────────────────────

bool LogPanel::Create(HWND parent, int id)
{
    m_hwnd = CreateWindowExW(0, k_ClassName, L"",
                WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                0, 0, 100, 100,
                parent, (HMENU)(UINT_PTR)id,
                (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE),
                this);
    return m_hwnd != nullptr;
}

void LogPanel::Destroy()
{
    if (m_fontTs)   { DeleteObject(m_fontTs);   m_fontTs   = nullptr; }
    if (m_fontText) { DeleteObject(m_fontText); m_fontText = nullptr; }
    if (m_fontHdr)  { DeleteObject(m_fontHdr);  m_fontHdr  = nullptr; }
    if (m_hwnd)     { DestroyWindow(m_hwnd);    m_hwnd     = nullptr; }
}

void LogPanel::ComputeTimestampWidth()
{
    // Measure a representative worst-case timestamp with the actual font
    HDC dc = GetDC(m_hwnd);
    HFONT old = (HFONT)SelectObject(dc, m_fontTs);
    SIZE sz;
    GetTextExtentPoint32W(dc, L"0000.00.00 00:00:00.000", 23, &sz);
    SelectObject(dc, old);
    ReleaseDC(m_hwnd, dc);
    m_tsW = sz.cx + 10;  // 10px breathing room
}

// ── Data management ───────────────────────────────────────────────────────────

void LogPanel::Clear()
{
    m_all.clear();
    m_vis.clear();
    m_scrollY = 0;
    Refresh();
}

void LogPanel::LoadJob(const std::vector<LogEntry>& entries)
{
    m_all = entries;
    m_scrollY = 0;
    RebuildVisible();
    Refresh();
}

void LogPanel::AppendEntry(const LogEntry& entry)
{
    m_all.push_back(entry);
    RebuildVisible();
    // Auto-scroll to bottom
    RECT rc; GetClientRect(m_hwnd, &rc);
    int contentH = (int)m_vis.size() * k_rowH;
    int viewH    = rc.bottom - k_headerH;
    if (viewH > 0 && contentH > viewH)
        m_scrollY = contentH - viewH;
    Refresh();
}

void LogPanel::RebuildVisible()
{
    m_vis.clear();
    // An entry is visible if all its ancestors are expanded
    for (int i = 0; i < (int)m_all.size(); i++) {
        const LogEntry& e = m_all[i];
        bool vis = true;
        int pi = e.parentIdx;
        while (pi >= 0 && pi < (int)m_all.size()) {
            if (!m_all[pi].expanded) { vis = false; break; }
            pi = m_all[pi].parentIdx;
        }
        if (vis) m_vis.push_back(i);
    }
}

// ── Scroll ────────────────────────────────────────────────────────────────────

void LogPanel::ScrollBy(int lines)
{
    RECT rc; GetClientRect(m_hwnd, &rc);
    int viewH    = rc.bottom - rc.top - k_headerH;
    int contentH = (int)m_vis.size() * k_rowH;
    int maxScroll = max(0, contentH - viewH);
    m_scrollY = max(0, min(m_scrollY + lines * k_rowH, maxScroll));
    Refresh();
}

// ── Hit tests ─────────────────────────────────────────────────────────────────

int LogPanel::HitTestRow(int y) const
{
    int relY  = y - k_headerH + m_scrollY;
    int visIdx = relY / k_rowH;
    if (visIdx < 0 || visIdx >= (int)m_vis.size()) return -1;
    return visIdx;
}

bool LogPanel::HitTestToggle(int x, int y, int* outIdx) const
{
    int visIdx = HitTestRow(y);
    if (visIdx < 0) return false;
    int allIdx = m_vis[visIdx];
    if (!m_all[allIdx].isGroup) return false;

    int rowTop   = k_headerH + visIdx * k_rowH - m_scrollY;
    int togX     = m_tsW + m_all[allIdx].depth * k_indW;
    RECT togRc   = { togX, rowTop + 3, togX + k_togW, rowTop + k_rowH - 3 };
    POINT pt     = { x, y };
    if (PtInRect(&togRc, pt)) { *outIdx = visIdx; return true; }
    return false;
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void LogPanel::PaintHeader(HDC dc, int W)
{
    RECT hdr = { 0, 0, W, k_headerH };
    HBRUSH hbr = CreateSolidBrush(LC::HdrBg);
    FillRect(dc, &hdr, hbr);
    DeleteObject(hbr);

    // Bottom border
    HPEN hpen = CreatePen(PS_SOLID, 1, LC::HdrBorder);
    HPEN old  = (HPEN)SelectObject(dc, hpen);
    MoveToEx(dc, 0, k_headerH - 1, nullptr);
    LineTo(dc, W, k_headerH - 1);
    SelectObject(dc, old);
    DeleteObject(hpen);

    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, m_fontHdr);
    SetTextColor(dc, LC::HdrText);
    RECT tRc = { 10, 0, W - 36, k_headerH };
    DrawTextW(dc, L"Log options \x25BC", -1, &tRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void LogPanel::PaintRow(HDC dc, const RECT& rc, int visIdx)
{
    int allIdx = m_vis[visIdx];
    const LogEntry& e = m_all[allIdx];

    // Background (alternating)
    COLORREF bg = (visIdx & 1) ? LC::RowEven : LC::RowOdd;
    HBRUSH hbr = CreateSolidBrush(bg);
    FillRect(dc, &rc, hbr);
    DeleteObject(hbr);

    SetBkMode(dc, TRANSPARENT);
    int midY = rc.top + (k_rowH - 14) / 2;

    // ── Timestamp column ──────────────────────────────────────────────────────
    SelectObject(dc, m_fontTs);
    SetTextColor(dc, LC::TsColor);
    RECT tsRc = { 6, rc.top, m_tsW, rc.bottom };
    DrawTextW(dc, e.timestamp.c_str(), -1, &tsRc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // ── Indent ────────────────────────────────────────────────────────────────
    int textX = m_tsW + e.depth * k_indW;

    // ── Toggle button ─────────────────────────────────────────────────────────
    if (e.isGroup) {
        RECT togRc = { textX, rc.top + 4, textX + k_togW, rc.bottom - 4 };
        HPEN bpen = CreatePen(PS_SOLID, 1, LC::TogBorder);
        HPEN bpOld = (HPEN)SelectObject(dc, bpen);
        HBRUSH bBr = CreateSolidBrush(RGB(245, 246, 252));
        HBRUSH bBrOld = (HBRUSH)SelectObject(dc, bBr);
        RoundRect(dc, togRc.left, togRc.top, togRc.right, togRc.bottom, 2, 2);
        SelectObject(dc, bpOld);
        SelectObject(dc, bBrOld);
        DeleteObject(bpen);
        DeleteObject(bBr);

        SetTextColor(dc, LC::TogFg);
        SelectObject(dc, m_fontText);
        DrawTextW(dc, e.expanded ? L"\x2212" : L"+", -1,
                  &togRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        textX += k_togW + 4;
    } else {
        textX += 4;  // small left nudge so text aligns with non-group siblings
    }

    // ── Message text ─────────────────────────────────────────────────────────
    COLORREF txtColor = (e.depth == 0) ? LC::TxtRoot
                      : (e.depth == 1) ? LC::TxtChild
                      :                  LC::TxtGrand;
    SetTextColor(dc, txtColor);
    SelectObject(dc, m_fontText);

    RECT tRc = { textX, rc.top, rc.right - 6, rc.bottom };
    DrawTextW(dc, e.text.c_str(), -1, &tRc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void LogPanel::OnPaint()
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hwnd, &ps);

    RECT rcClient; GetClientRect(m_hwnd, &rcClient);
    int W = rcClient.right, H = rcClient.bottom;

    HDC     memDC  = CreateCompatibleDC(hdc);
    HBITMAP bmp    = CreateCompatibleBitmap(hdc, W, H);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, bmp);

    // Panel background
    HBRUSH bgBr = CreateSolidBrush(LC::BgPanel);
    FillRect(memDC, &rcClient, bgBr);
    DeleteObject(bgBr);

    // Header
    PaintHeader(memDC, W);

    // Log rows
    int viewY0 = k_headerH;
    int n      = (int)m_vis.size();
    for (int vi = 0; vi < n; vi++) {
        int top    = viewY0 + vi * k_rowH - m_scrollY;
        int bottom = top + k_rowH;
        if (bottom < viewY0 || top > H) continue;
        RECT rowRc = { 0, top, W, bottom };
        PaintRow(memDC, rowRc, vi);
    }

    // Empty state
    if (n == 0) {
        SelectObject(memDC, m_fontText);
        SetTextColor(memDC, LC::TsColor);
        SetBkMode(memDC, TRANSPARENT);
        RECT msg = { 0, k_headerH, W, H };
        DrawTextW(memDC, L"No log entries for this job.", -1, &msg,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(bmp);
    DeleteDC(memDC);

    EndPaint(m_hwnd, &ps);
}

// ── Input ─────────────────────────────────────────────────────────────────────

void LogPanel::OnLButtonDown(int x, int y)
{
    int visIdx;
    if (y < k_headerH) return;  // header click (future: show menu)
    if (HitTestToggle(x, y, &visIdx)) {
        int allIdx = m_vis[visIdx];
        m_all[allIdx].expanded = !m_all[allIdx].expanded;
        RebuildVisible();
        Refresh();
    }
}

void LogPanel::OnMouseWheel(int delta)
{
    ScrollBy(-(delta / WHEEL_DELTA));
}

void LogPanel::OnSize(int, int) { Refresh(); }

// ── Window proc ───────────────────────────────────────────────────────────────

LRESULT CALLBACK LogPanel::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    LogPanel* pThis = nullptr;

    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        pThis = reinterpret_cast<LogPanel*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->m_hwnd = hwnd;

        pThis->m_fontTs   = MakeFont(8, true);   // Consolas
        pThis->m_fontText = MakeFont(9, false);  // Segoe UI
        pThis->ComputeTimestampWidth();
        // Bold header font
        LOGFONTW lf = {};
        lf.lfHeight  = -MulDiv(9, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72);
        lf.lfWeight  = FW_SEMIBOLD;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        pThis->m_fontHdr = CreateFontIndirectW(&lf);
        return 0;
    }

    pThis = reinterpret_cast<LogPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!pThis) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:      pThis->OnPaint(); return 0;
    case WM_SIZE:       pThis->OnSize(LOWORD(lp), HIWORD(lp)); return 0;
    case WM_LBUTTONDOWN: pThis->OnLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0;
    case WM_MOUSEWHEEL:  pThis->OnMouseWheel((short)HIWORD(wp)); return 0;
    case WM_DESTROY:
        if (pThis->m_fontTs)   { DeleteObject(pThis->m_fontTs);   }
        if (pThis->m_fontText) { DeleteObject(pThis->m_fontText); }
        if (pThis->m_fontHdr)  { DeleteObject(pThis->m_fontHdr);  }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
