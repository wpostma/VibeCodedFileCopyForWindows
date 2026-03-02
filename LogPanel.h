#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#include "CopyJob.h"

// ── LogPanel ──────────────────────────────────────────────────────────────────
// Custom-painted child window with a collapsible tree-list log view.
// The header bar has "Log options ▼" and a close [×] button.
// Rows show: timestamp | indent | [+/-] | message text.

class LogPanel {
public:
    LogPanel()  = default;
    ~LogPanel() { Destroy(); }

    bool Create(HWND hwndParent, int id);
    void Destroy();

    HWND Hwnd() const { return m_hwnd; }

    // Replace all entries with those from a job's log
    void LoadJob(const std::vector<LogEntry>& entries);
    // Append a single new entry (call after LoadJob when job is running)
    void AppendEntry(const LogEntry& entry);
    // Overwrite an existing entry in place (for the live current-file slot)
    void UpdateEntry(int allIdx, const LogEntry& entry);
    void Clear();

    void Refresh() { if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE); }

    static bool RegisterClass(HINSTANCE hInst);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMsg(UINT, WPARAM, LPARAM);

    void OnPaint();
    void OnLButtonDown(int x, int y);
    void OnMouseWheel(int delta);
    void OnSize(int w, int h);

    void RebuildVisible();
    int  HitTestRow(int y) const;
    bool HitTestToggle(int x, int y, int* outIdx) const;
    void ScrollBy(int lines);
    void PaintHeader(HDC dc, int W);
    void PaintRow(HDC dc, const RECT& rc, int visIdx);

    static HFONT MakeFont(int ptSize, bool mono = false);

    static const int k_headerH = 28;
    static const int k_rowH    = 22;
    static const int k_indW    = 18;   // indent per depth level
    static const int k_togW    = 16;   // toggle [+/-] button width

    int  m_tsW = 148;                  // timestamp column width (measured at runtime)
    void ComputeTimestampWidth();      // called after m_fontTs is created

    HWND m_hwnd = nullptr;

    std::vector<LogEntry> m_all;      // master list (all entries)
    std::vector<int>      m_vis;      // indices into m_all that are visible

    int m_scrollY = 0;

    // GDI
    HFONT m_fontTs   = nullptr;   // Consolas 8pt – timestamps
    HFONT m_fontText = nullptr;   // Segoe UI 9pt – messages
    HFONT m_fontHdr  = nullptr;   // Segoe UI 9pt bold – header
};
