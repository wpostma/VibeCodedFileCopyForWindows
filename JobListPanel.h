#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#include <memory>
#include "CopyJob.h"

// ── JobListPanel ──────────────────────────────────────────────────────────────
// Custom-painted child window that shows the list of copy jobs.
// Each job renders as a two-line "card" row styled like Bvckup 2.

class JobListPanel {
public:
    JobListPanel()  = default;
    ~JobListPanel() { Destroy(); }

    bool Create(HWND hwndParent, int id,
                std::vector<std::unique_ptr<CopyJob>>* jobs);
    void Destroy();

    HWND Hwnd() const { return m_hwnd; }

    void Refresh();                     // Repaint (data changed externally)
    int  SelectedIndex() const { return m_selIdx; }
    void SetSelectedIndex(int i);

    // Called by parent to register a callback when selection changes
    using SelectionCallback = void(*)(int jobIndex, void* ctx);
    void SetSelectionCallback(SelectionCallback cb, void* ctx) {
        m_selCb = cb; m_selCtx = ctx;
    }

    static bool RegisterClass(HINSTANCE hInst);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMsg(UINT, WPARAM, LPARAM);

    void OnPaint();
    void OnLButtonDown(int x, int y);
    void OnLButtonDblClk(int x, int y);
    void OnMouseWheel(int delta);
    void OnSize(int w, int h);

    int  HitTestRow(int y) const;
    void EnsureVisible(int idx);
    void ScrollBy(int lines);

    void PaintRow(HDC dc, const RECT& rc, int idx, bool selected);

    static HFONT CreateFont_(int ptSize, bool bold);

    HWND  m_hwnd    = nullptr;
    std::vector<std::unique_ptr<CopyJob>>* m_jobs = nullptr;

    int   m_selIdx   = -1;
    int   m_scrollY  = 0;       // pixel offset (scrolled up)

    static const int k_rowH = 58;  // height per job card

    // GDI resources
    HFONT m_fontName = nullptr;   // Segoe UI 10pt bold
    HFONT m_fontSub  = nullptr;   // Segoe UI 9pt regular

    SelectionCallback m_selCb  = nullptr;
    void*             m_selCtx = nullptr;
};
