#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include "CopyEngine.h"
#include "resource.h"
#include "utils.h"

#pragma comment(lib, "shlwapi.lib")

// ── Helpers ───────────────────────────────────────────────────────────────────

static void PostLog(HWND hwnd, int idx, const std::wstring& text,
                    int depth = 0, bool isGroup = false, int parentIdx = -1)
{
    auto* m      = new LogMsg;
    m->jobIndex  = idx;
    m->timestamp = TimestampNow();
    m->text      = text;
    m->depth     = depth;
    m->isGroup   = isGroup;
    m->parentIdx = parentIdx;
    PostMessageW(hwnd, WM_JOB_LOG, (WPARAM)idx, (LPARAM)m);
}

static void PostProgress(HWND hwnd, int idx, const JobStats& stats, JobStatus status)
{
    auto* m    = new ProgressMsg;
    m->jobIndex = idx;
    m->stats    = stats;
    m->status   = status;
    PostMessageW(hwnd, WM_JOB_PROGRESS, (WPARAM)idx, (LPARAM)m);
}

// ── File enumeration ──────────────────────────────────────────────────────────

struct FileEntry {
    std::wstring relPath;   // relative to source root
    FILETIME     lastWrite;
    ULONGLONG    size;
    bool         isDir;
};

static void EnumerateDir(const std::wstring& root, const std::wstring& rel,
                          std::vector<FileEntry>& out)
{
    std::wstring pattern = root + L"\\" + rel + L"*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        std::wstring relChild = rel + fd.cFileName;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            FileEntry fe;
            fe.relPath  = relChild + L"\\";
            fe.lastWrite = fd.ftLastWriteTime;
            fe.size     = 0;
            fe.isDir    = true;
            out.push_back(fe);
            EnumerateDir(root, relChild + L"\\", out);
        } else {
            FileEntry fe;
            fe.relPath  = relChild;
            fe.lastWrite = fd.ftLastWriteTime;
            ULARGE_INTEGER sz;
            sz.LowPart  = fd.nFileSizeLow;
            sz.HighPart = fd.nFileSizeHigh;
            fe.size     = sz.QuadPart;
            fe.isDir    = false;
            out.push_back(fe);
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

// ── CopyFileEx progress callback ──────────────────────────────────────────────

struct CopyCallbackCtx {
    HWND                               hwnd;
    int                                jobIndex;
    JobStats*                          stats;
    std::shared_ptr<std::atomic<bool>> cancelFlag;
};

static DWORD CALLBACK CopyProgressCB(
    LARGE_INTEGER TotalFileSize, LARGE_INTEGER TotalBytesTransferred,
    LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD dwCallbackReason,
    HANDLE, HANDLE, LPVOID lpData)
{
    auto* ctx = reinterpret_cast<CopyCallbackCtx*>(lpData);
    if (ctx->cancelFlag->load()) return PROGRESS_CANCEL;

    if (dwCallbackReason == CALLBACK_CHUNK_FINISHED ||
        dwCallbackReason == CALLBACK_STREAM_SWITCH)
    {
        ctx->stats->copiedBytes = (ULONGLONG)TotalBytesTransferred.QuadPart;
        PostProgress(ctx->hwnd, ctx->jobIndex, *ctx->stats, JobStatus::Copying);
    }
    return PROGRESS_CONTINUE;
}

// ── Thread entry point ────────────────────────────────────────────────────────

static DWORD WINAPI CopyThreadProc(LPVOID lpParam)
{
    auto* ep = reinterpret_cast<EngineParams*>(lpParam);
    EngineParams params = *ep;
    delete ep;

    HWND   hwnd    = params.hwndMain;
    int    idx     = params.jobIndex;
    auto&  cancel  = params.cancelFlag;

    ULONGLONG startTick = GetTickCount64();
    JobStats  stats     = {};

    // ── Log: start ────────────────────────────────────────────────────────────
    int runRoot = -1;   // we track parentIdx by sequential log count on receiver side
                        // For simplicity we pass depth only; UI rebuilds the tree
    PostLog(hwnd, idx, L"Running the backup...", 0, true);

    // ── Phase 1: Prepare / enumerate ─────────────────────────────────────────
    PostLog(hwnd, idx, L"Preparing...", 1, true);

    std::vector<FileEntry> files;
    EnumerateDir(params.sourcePath, L"", files);

    if (cancel->load()) {
        PostLog(hwnd, idx, L"Stopped by user", 1);
        PostMessageW(hwnd, WM_JOB_DONE, (WPARAM)idx, 0);
        return 0;
    }

    // Count totals
    for (auto& f : files) {
        if (f.isDir)  stats.totalFolders++;
        else {        stats.totalFiles++; stats.totalBytes += f.size; }
    }
    stats.changeCount = 0;

    // ── Phase 2: Compare & copy ───────────────────────────────────────────────
    PostLog(hwnd, idx, L"Processing...", 1, true);

    CopyCallbackCtx cbCtx{ hwnd, idx, &stats, cancel };

    for (auto& f : files) {
        if (cancel->load()) break;

        std::wstring srcFull  = params.sourcePath + L"\\" + f.relPath;
        std::wstring dstFull  = params.destPath   + L"\\" + f.relPath;

        if (f.isDir) {
            CreateDirectoryW(dstFull.c_str(), nullptr);
            stats.totalFolders++;
            continue;
        }

        // Check if copy is needed
        bool needsCopy = true;
        WIN32_FILE_ATTRIBUTE_DATA dstAttr;
        if (GetFileAttributesExW(dstFull.c_str(), GetFileExInfoStandard, &dstAttr)) {
            if (CompareFileTime(&f.lastWrite, &dstAttr.ftLastWriteTime) == 0) {
                ULARGE_INTEGER dstSz;
                dstSz.LowPart  = dstAttr.nFileSizeLow;
                dstSz.HighPart = dstAttr.nFileSizeHigh;
                if (dstSz.QuadPart == f.size) {
                    needsCopy = false;
                    stats.skippedFiles++;
                }
            }
        }

        if (!needsCopy) continue;

        // Ensure destination directory exists
        std::wstring dstDir = dstFull;
        size_t slash = dstDir.rfind(L'\\');
        if (slash != std::wstring::npos) {
            dstDir.resize(slash);
            // CreateDirectoryW is idempotent on existing dirs; ignore ERROR_ALREADY_EXISTS
            SHCreateDirectoryExW(nullptr, dstDir.c_str(), nullptr);
        }

        stats.changeCount++;
        stats.totalBytes += f.size;  // ensure counted

        BOOL  cancelled = FALSE;
        DWORD flags     = COPY_FILE_ALLOW_DECRYPTED_DESTINATION;
        BOOL  ok        = CopyFileExW(srcFull.c_str(), dstFull.c_str(),
                                      CopyProgressCB, &cbCtx, &cancelled, flags);

        if (!ok && !cancelled) {
            stats.errorCount++;
            DWORD err = GetLastError();
            wchar_t msg[300];
            swprintf_s(msg, L"Error copying %s  (0x%08X)", f.relPath.c_str(), err);
            PostLog(hwnd, idx, msg, 2);
        } else if (!cancelled) {
            stats.copiedFiles++;
            stats.copiedBytes += f.size;
            PostProgress(hwnd, idx, stats, JobStatus::Copying);
        }
    }

    // ── Phase 3: Done ─────────────────────────────────────────────────────────
    double elapsed = (GetTickCount64() - startTick) / 1000.0;
    stats.elapsedSec = elapsed;

    wchar_t summary[128];
    int mins = (int)(elapsed / 60);
    int secs = (int)elapsed % 60;
    if (cancel->load())
        swprintf_s(summary, L"Stopped after %d min %d sec  (%llu errors)", mins, secs, stats.errorCount);
    else if (stats.changeCount == 0 && stats.errorCount == 0)
        swprintf_s(summary, L"Job run skipped. All files already up to date.");
    else if (stats.errorCount > 0)
        swprintf_s(summary, L"Completed in %d min %d sec  (%llu errors)", mins, secs, stats.errorCount);
    else
        swprintf_s(summary, L"Completed in %d min %d sec with no errors", mins, secs);

    PostLog(hwnd, idx, summary, 1);

    PostProgress(hwnd, idx, stats, cancel->load() ? JobStatus::Stopped : JobStatus::Done);
    PostMessageW(hwnd, WM_JOB_DONE, (WPARAM)idx, (LPARAM)stats.errorCount);
    return 0;
}

// ── Public API ────────────────────────────────────────────────────────────────

HANDLE StartCopyEngine(const EngineParams& params)
{
    auto* ep = new EngineParams(params);
    HANDLE h = CreateThread(nullptr, 0, CopyThreadProc, ep, 0, nullptr);
    if (!h) delete ep;
    return h;
}
