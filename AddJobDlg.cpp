#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>
#include <string>
#include "AddJobDlg.h"
#include "resource.h"

// ── Folder browse using IFileOpenDialog ───────────────────────────────────────

static bool BrowseForFolder(HWND owner, std::wstring& out)
{
    IFileOpenDialog* pDlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg))))
        return false;

    DWORD opts = 0;
    pDlg->GetOptions(&opts);
    pDlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    pDlg->SetTitle(L"Select Folder");

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
        case IDOK: {
            wchar_t buf[MAX_PATH];
            GetDlgItemTextW(hDlg, IDC_EDIT_JOBNAME, buf, MAX_PATH);
            ctx->data->name = buf;
            GetDlgItemTextW(hDlg, IDC_EDIT_SOURCE, buf, MAX_PATH);
            ctx->data->sourcePath = buf;
            GetDlgItemTextW(hDlg, IDC_EDIT_DEST, buf, MAX_PATH);
            ctx->data->destPath = buf;

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
