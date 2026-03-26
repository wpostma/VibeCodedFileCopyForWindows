#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>
#include <string>
#include "AddJobDlg.h"
#include "resource.h"

// ── Folder browse using IFileOpenDialog ───────────────────────────────────────

static bool BrowseForFolder(HWND owner, std::wstring& out,
                             const wchar_t* title = L"Select Folder",
                             const wchar_t* startDir = nullptr)
{
    IFileOpenDialog* pDlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg))))
        return false;

    DWORD opts = 0;
    pDlg->GetOptions(&opts);
    pDlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    pDlg->SetTitle(title);

    // Set starting directory if provided
    if (startDir && startDir[0]) {
        IShellItem* pStart = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(startDir, nullptr, IID_PPV_ARGS(&pStart)))) {
            pDlg->SetFolder(pStart);
            pStart->Release();
        }
    }

    bool ok = false;
    if (SUCCEEDED(pDlg->Show(owner))) {
        IShellItem* pItem = nullptr;
        if (SUCCEEDED(pDlg->GetResult(&pItem))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                out = path;
                CoTaskMemFree(path);
                ok = true;
            }
            pItem->Release();
        }
    }
    pDlg->Release();
    return ok;
}

// Compute a relative path from root to selected, or just the folder name
static std::wstring MakeRelativeFilter(const std::wstring& root, const std::wstring& selected)
{
    // Normalize: ensure both end without backslash for comparison
    std::wstring r = root, s = selected;
    while (!r.empty() && r.back() == L'\\') r.pop_back();
    while (!s.empty() && s.back() == L'\\') s.pop_back();

    // Check if selected is under root
    if (s.size() > r.size() && _wcsnicmp(s.c_str(), r.c_str(), r.size()) == 0
        && s[r.size()] == L'\\')
    {
        // Return relative path from root (e.g. "subdir\deep")
        return s.substr(r.size() + 1);
    }

    // Not under root — just return the folder name
    size_t slash = s.rfind(L'\\');
    if (slash != std::wstring::npos)
        return s.substr(slash + 1);
    return s;
}

// ── Dialog proc ───────────────────────────────────────────────────────────────

struct DlgCtx {
    AddJobDlgData* data;
    HINSTANCE      hInst;
};

static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp)
{
    DlgCtx* ctx = reinterpret_cast<DlgCtx*>(GetWindowLongPtrW(hDlg, DWLP_USER));

    switch (msg) {
    case WM_INITDIALOG: {
        ctx = reinterpret_cast<DlgCtx*>(lp);
        SetWindowLongPtrW(hDlg, DWLP_USER, (LONG_PTR)ctx);

        // Pre-fill fields if editing
        if (!ctx->data->name.empty())
            SetDlgItemTextW(hDlg, IDC_EDIT_JOBNAME, ctx->data->name.c_str());
        if (!ctx->data->sourcePath.empty())
            SetDlgItemTextW(hDlg, IDC_EDIT_SOURCE, ctx->data->sourcePath.c_str());
        if (!ctx->data->destPath.empty())
            SetDlgItemTextW(hDlg, IDC_EDIT_DEST, ctx->data->destPath.c_str());

        // Schedule radio buttons
        CheckRadioButton(hDlg, IDC_RADIO_MANUAL, IDC_RADIO_DAILY,
            ctx->data->scheduleType == 1 ? IDC_RADIO_INTERVAL :
            ctx->data->scheduleType == 2 ? IDC_RADIO_DAILY : IDC_RADIO_MANUAL);
        {
            wchar_t buf[16];
            if (ctx->data->scheduleType == 2) {
                swprintf_s(buf, L"%02d:%02d", ctx->data->scheduleValue / 100,
                           ctx->data->scheduleValue % 100);
                SetDlgItemTextW(hDlg, IDC_EDIT_DAILY_TIME, buf);
            }
            swprintf_s(buf, L"%d", ctx->data->scheduleValue);
            SetDlgItemTextW(hDlg, IDC_EDIT_INTERVAL, buf);
        }

        // Smart defer
        CheckDlgButton(hDlg, IDC_CHK_SMART_DEFER, ctx->data->smartDefer ? BST_CHECKED : BST_UNCHECKED);
        { wchar_t buf[8]; swprintf_s(buf, L"%d", ctx->data->quietPeriodMin);
          SetDlgItemTextW(hDlg, IDC_EDIT_QUIET_PERIOD, buf); }

        // Filters
        if (!ctx->data->excludePatterns.empty())
            SetDlgItemTextW(hDlg, IDC_EDIT_EXCLUDE, ctx->data->excludePatterns.c_str());
        if (!ctx->data->includePatterns.empty())
            SetDlgItemTextW(hDlg, IDC_EDIT_INCLUDE, ctx->data->includePatterns.c_str());

        // Focus on name field
        SetFocus(GetDlgItem(hDlg, IDC_EDIT_JOBNAME));
        return FALSE;  // we set focus manually
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_BTN_BROWSE_SRC: {
            std::wstring path;
            if (BrowseForFolder(hDlg, path))
                SetDlgItemTextW(hDlg, IDC_EDIT_SOURCE, path.c_str());
            break;
        }
        case IDC_BTN_BROWSE_DST: {
            std::wstring path;
            if (BrowseForFolder(hDlg, path))
                SetDlgItemTextW(hDlg, IDC_EDIT_DEST, path.c_str());
            break;
        }
        case IDC_BTN_BROWSE_EXCL:
        case IDC_BTN_BROWSE_INCL: {
            // Get current source path as the starting directory
            wchar_t srcBuf[MAX_PATH] = {};
            GetDlgItemTextW(hDlg, IDC_EDIT_SOURCE, srcBuf, MAX_PATH);
            std::wstring folder;
            const wchar_t* title = (LOWORD(wp) == IDC_BTN_BROWSE_EXCL)
                ? L"Select folder to exclude" : L"Select folder to include";
            if (BrowseForFolder(hDlg, folder, title, srcBuf)) {
                std::wstring rel = MakeRelativeFilter(srcBuf, folder);
                int editId = (LOWORD(wp) == IDC_BTN_BROWSE_EXCL)
                    ? IDC_EDIT_EXCLUDE : IDC_EDIT_INCLUDE;
                // Append to existing text
                wchar_t existing[4096] = {};
                GetDlgItemTextW(hDlg, editId, existing, 4096);
                std::wstring text = existing;
                if (!text.empty() && text.back() != L'\n' && text.back() != L';')
                    text += L"\r\n";
                text += rel;
                SetDlgItemTextW(hDlg, editId, text.c_str());
            }
            break;
        }
        case IDOK: {
            wchar_t buf[MAX_PATH];
            GetDlgItemTextW(hDlg, IDC_EDIT_JOBNAME, buf, MAX_PATH);
            ctx->data->name = buf;
            GetDlgItemTextW(hDlg, IDC_EDIT_SOURCE, buf, MAX_PATH);
            ctx->data->sourcePath = buf;
            GetDlgItemTextW(hDlg, IDC_EDIT_DEST, buf, MAX_PATH);
            ctx->data->destPath = buf;

            // Schedule
            if (IsDlgButtonChecked(hDlg, IDC_RADIO_INTERVAL))
                ctx->data->scheduleType = 1;
            else if (IsDlgButtonChecked(hDlg, IDC_RADIO_DAILY))
                ctx->data->scheduleType = 2;
            else
                ctx->data->scheduleType = 0;

            GetDlgItemTextW(hDlg, IDC_EDIT_INTERVAL, buf, 16);
            int intervalMin = _wtoi(buf);
            if (intervalMin > 0) ctx->data->scheduleValue = intervalMin;

            if (ctx->data->scheduleType == 2) {
                GetDlgItemTextW(hDlg, IDC_EDIT_DAILY_TIME, buf, 16);
                // Parse HH:MM
                int h = 0, m = 0;
                swscanf_s(buf, L"%d:%d", &h, &m);
                ctx->data->scheduleValue = h * 100 + m;
            }

            // Smart defer
            ctx->data->smartDefer = IsDlgButtonChecked(hDlg, IDC_CHK_SMART_DEFER) == BST_CHECKED;
            GetDlgItemTextW(hDlg, IDC_EDIT_QUIET_PERIOD, buf, 8);
            int qp = _wtoi(buf);
            if (qp > 0) ctx->data->quietPeriodMin = qp;

            // Filters (use larger buffer)
            wchar_t filterBuf[4096];
            GetDlgItemTextW(hDlg, IDC_EDIT_EXCLUDE, filterBuf, 4096);
            ctx->data->excludePatterns = filterBuf;
            GetDlgItemTextW(hDlg, IDC_EDIT_INCLUDE, filterBuf, 4096);
            ctx->data->includePatterns = filterBuf;

            if (ctx->data->name.empty()) {
                MessageBoxW(hDlg, L"Please enter a job name.", L"Missing field", MB_ICONWARNING);
                SetFocus(GetDlgItem(hDlg, IDC_EDIT_JOBNAME));
                return TRUE;
            }
            if (ctx->data->sourcePath.empty()) {
                MessageBoxW(hDlg, L"Please specify a source folder.", L"Missing field", MB_ICONWARNING);
                SetFocus(GetDlgItem(hDlg, IDC_EDIT_SOURCE));
                return TRUE;
            }
            if (ctx->data->destPath.empty()) {
                MessageBoxW(hDlg, L"Please specify a destination folder.", L"Missing field", MB_ICONWARNING);
                SetFocus(GetDlgItem(hDlg, IDC_EDIT_DEST));
                return TRUE;
            }

            // Validate: source and dest must not overlap
            {
                // Normalize both paths: lowercase, trailing backslash
                std::wstring src = ctx->data->sourcePath;
                std::wstring dst = ctx->data->destPath;
                while (!src.empty() && src.back() == L'\\') src.pop_back();
                while (!dst.empty() && dst.back() == L'\\') dst.pop_back();
                // Case-insensitive compare (NTFS)
                std::wstring srcL = src, dstL = dst;
                for (auto& c : srcL) c = towlower(c);
                for (auto& c : dstL) c = towlower(c);

                if (srcL == dstL) {
                    MessageBoxW(hDlg,
                        L"Source and destination are the same folder.\n"
                        L"Please choose a different destination.",
                        L"Invalid paths", MB_ICONWARNING);
                    SetFocus(GetDlgItem(hDlg, IDC_EDIT_DEST));
                    return TRUE;
                }
                // Check if dest is inside source (infinite recursion risk)
                std::wstring srcPrefix = srcL + L"\\";
                if (dstL.size() > srcPrefix.size() &&
                    dstL.compare(0, srcPrefix.size(), srcPrefix) == 0)
                {
                    MessageBoxW(hDlg,
                        L"The destination is inside the source folder.\n"
                        L"This would cause infinite recursion.\n\n"
                        L"Please choose a destination outside the source.",
                        L"Invalid paths", MB_ICONWARNING);
                    SetFocus(GetDlgItem(hDlg, IDC_EDIT_DEST));
                    return TRUE;
                }
            }

            EndDialog(hDlg, IDOK);
            break;
        }
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            break;
        }
        return TRUE;
    }
    return FALSE;
}

// ── Public API ────────────────────────────────────────────────────────────────

bool ShowAddJobDialog(HWND hwndParent, HINSTANCE hInst, AddJobDlgData& out)
{
    DlgCtx ctx{ &out, hInst };
    INT_PTR result = DialogBoxParamW(hInst, MAKEINTRESOURCE(IDD_ADD_JOB),
                                     hwndParent, DlgProc, (LPARAM)&ctx);
    return result == IDOK;
}
