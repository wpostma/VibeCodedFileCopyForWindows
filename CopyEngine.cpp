#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include "CopyEngine.h"
#include "resource.h"
#include "utils.h"
#include "logger.h"

#pragma comment(lib, "shlwapi.lib")

// ── Helpers ───────────────────────────────────────────────────────────────────

// All three posting helpers are nothrow: any allocation or copy failure silently
// drops the message rather than propagating an exception into the thread body or
// (worse) out of a Win32 callback.

static void PostCurrentFile(HWND hwnd, int idx,
                            const std::wstring& dir, const std::wstring& filename)
{
    try {
        auto* m = new LogMsg;
        m->jobIndex  = idx;
        m->timestamp = TimestampNow();
        m->text      = dir;
        m->text2     = filename;
        m->depth     = 2;
        m->isGroup   = false;
        m->parentIdx = -1;
        m->replaceCurrentFile = true;
        PostMessageW(hwnd, WM_JOB_LOG, (WPARAM)idx, (LPARAM)m);
    } catch (...) {}   // silently drop on OOM
}

static void PostLog(HWND hwnd, int idx, const std::wstring& text,
                    int depth = 0, bool isGroup = false, int parentIdx = -1)
{
    try {

#ifdef _DEBUG
         Log(L"[PostLog] %s", text.c_str());
#endif
        auto* m      = new LogMsg;
        m->jobIndex  = idx;
        m->timestamp = TimestampNow();
        m->text      = text;
        m->depth     = depth;
        m->isGroup   = isGroup;
        m->parentIdx = parentIdx;
        PostMessageW(hwnd, WM_JOB_LOG, (WPARAM)idx, (LPARAM)m);
    } catch (...) {}   // silently drop on OOM
}

static void PostProgress(HWND hwnd, int idx, const JobStats& stats, JobStatus status)
{
    try {
        auto* m    = new ProgressMsg;
        m->jobIndex = idx;
        m->stats    = stats;
        m->status   = status;
        PostMessageW(hwnd, WM_JOB_PROGRESS, (WPARAM)idx, (LPARAM)m);
    } catch (...) {}   // silently drop on OOM
}

// ── Long-path helpers ─────────────────────────────────────────────────────────

// Prepend the \\?\ prefix so all file operations bypass the MAX_PATH (260-char)
// limit.  Requires longPathAware=true in the manifest AND the OS registry setting
// HKLM\SYSTEM\CurrentControlSet\Control\FileSystem\LongPathsEnabled=1 (Windows 10
// 1607+).  Safe to call on paths that are already prefixed.
static std::wstring ToLongPath(const std::wstring& p)
{
    // Already a \\?\ or \\.\  (device/long-path) prefix — leave it alone.
    if (p.size() >= 4 && p[0] == L'\\' && p[1] == L'\\' &&
        (p[2] == L'?' || p[2] == L'.') && p[3] == L'\\')
        return p;

    // UNC path  \\server\share\…  →  \\?\UNC\server\share\…
    if (p.size() >= 2 && p[0] == L'\\' && p[1] == L'\\')
        return L"\\\\?\\UNC\\" + p.substr(2);

    // Ordinary absolute path  C:\…  →  \\?\C:\…
    return L"\\\\?\\" + p;
}

// CreateDirectoryW (unlike SHCreateDirectoryExW) accepts the \\?\ prefix.
// Recurse to create intermediate directories the same way.
static void EnsureDirectoryExists(const std::wstring& path)
{
    if (CreateDirectoryW(path.c_str(), nullptr)) return;
    DWORD err = GetLastError();
    if (err == ERROR_ALREADY_EXISTS) return;
    if (err != ERROR_PATH_NOT_FOUND)  return;  // unrecoverable (permissions, etc.)

    // Create the parent first.  Guard against recursing past the root of a
    // \\?\-prefixed path (minimum meaningful length: "\\?\X:\" = 7 chars).
    size_t slash = path.rfind(L'\\');
    if (slash == std::wstring::npos || slash <= 6) return;

    EnsureDirectoryExists(path.substr(0, slash));
    CreateDirectoryW(path.c_str(), nullptr);
}

// ── File enumeration ──────────────────────────────────────────────────────────

// RAII wrapper so FindClose is called even if an exception unwinds the stack.
struct FindGuard {
    HANDLE h;
    explicit FindGuard(HANDLE h) : h(h) {}
    ~FindGuard() { if (h != INVALID_HANDLE_VALUE) FindClose(h); }
    FindGuard(const FindGuard&) = delete;
    FindGuard& operator=(const FindGuard&) = delete;
};

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
    FindGuard guard(FindFirstFileW(pattern.c_str(), &fd));
    if (guard.h == INVALID_HANDLE_VALUE) return;
    // FindClose is now guaranteed via guard's destructor, even if push_back throws.

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        // Skip Windows reserved device name — cannot be a real file
        if (_wcsicmp(fd.cFileName, L"nul") == 0)
            continue;

        std::wstring relChild = rel + fd.cFileName;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            FileEntry fe;
            fe.relPath   = relChild + L"\\";
            fe.lastWrite = fd.ftLastWriteTime;
            fe.size      = 0;
            fe.isDir     = true;
            out.push_back(fe);
            EnumerateDir(root, relChild + L"\\", out);
        } else {
            FileEntry fe;
            fe.relPath   = relChild;
            fe.lastWrite = fd.ftLastWriteTime;
            ULARGE_INTEGER sz;
            sz.LowPart  = fd.nFileSizeLow;
            sz.HighPart = fd.nFileSizeHigh;
            fe.size     = sz.QuadPart;
            fe.isDir    = false;
            out.push_back(fe);
        }
    } while (FindNextFileW(guard.h, &fd));
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
    // Capture the plain-data fields before anything can throw, so the catch
    // blocks below always have a valid hwnd/idx to post WM_JOB_DONE.
    auto* rawEp = reinterpret_cast<EngineParams*>(lpParam);
    HWND hwnd = rawEp->hwndMain;
    int  idx  = rawEp->jobIndex;

    try {
    // unique_ptr owns rawEp so it's freed whether or not the wstring copy throws.
    std::unique_ptr<EngineParams> epOwner(rawEp);
    EngineParams params = *epOwner;   // deep-copies wstrings — may throw bad_alloc
    epOwner.reset();                  // delete now; params holds everything we need

    // Prefix source and dest with \\?\ so all downstream file operations
    // (FindFirstFileW, CopyFileExW, GetFileAttributesExW, CreateDirectoryW)
    // can handle paths longer than MAX_PATH (260 chars).
    params.sourcePath = ToLongPath(params.sourcePath);
    params.destPath   = ToLongPath(params.destPath);

    // Strip trailing backslash to avoid double separators in \\?\ paths.
    // \\?\ disables Win32 path normalisation, so "\\?\C:\dir\\*" is NOT
    // collapsed the way "C:\dir\\*" would be — FindFirstFileW fails silently.
    // Guard: keep the backslash for bare drive root \\?\X:\ (7 chars).
    auto stripTrailingSlash = [](std::wstring& p) {
        while (p.size() > 7 && p.back() == L'\\')
            p.pop_back();
    };
    stripTrailingSlash(params.sourcePath);
    stripTrailingSlash(params.destPath);

    auto&  cancel  = params.cancelFlag;
    auto&  pause   = params.pauseFlag;

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

    {
        wchar_t dbg[256];
        swprintf_s(dbg, L"Enumerated %llu files, %llu folders, %llu bytes  (changeCount=%llu)",
                   stats.totalFiles, stats.totalFolders, stats.totalBytes, stats.changeCount);
        PostLog(hwnd, idx, dbg, 1);
    }

    // ── Phase 2: Compare & copy ───────────────────────────────────────────────
    PostLog(hwnd, idx, L"Processing...", 1, true);

    CopyCallbackCtx cbCtx{ hwnd, idx, &stats, cancel };

    bool wasPaused = false;
    for (auto& f : files) {
        if (cancel->load()) break;

        // Pause: wait between files until the flag clears
        if (pause->load()) {
            if (!wasPaused) {
                PostProgress(hwnd, idx, stats, JobStatus::Paused);
                wasPaused = true;
            }
            while (pause->load() && !cancel->load())
                Sleep(50);
            if (!cancel->load()) {
                PostProgress(hwnd, idx, stats, JobStatus::Copying);
                wasPaused = false;
            }
        }
        if (cancel->load()) break;

        std::wstring srcFull  = params.sourcePath + L"\\" + f.relPath;
        std::wstring dstFull  = params.destPath   + L"\\" + f.relPath;

        if (f.isDir) {
            CreateDirectoryW(dstFull.c_str(), nullptr);
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
            // CreateDirectoryW accepts \\?\ prefix; SHCreateDirectoryExW does not.
            EnsureDirectoryExists(dstDir);
        }

        stats.changeCount++;

        // Log every 100th change so we can spot runaway counts
        if (stats.changeCount <= 3 || stats.changeCount % 100 == 0) {
            wchar_t dbg[128];
            swprintf_s(dbg, L"change #%llu  (copiedFiles=%llu)", stats.changeCount, stats.copiedFiles);
            PostLog(hwnd, idx, dbg, 2);
        }

        // Show which file is being copied (path | filename split display)
        {
            std::wstring dir  = f.relPath;
            std::wstring file;
            size_t slash = dir.rfind(L'\\');
            if (slash != std::wstring::npos) {
                file = dir.substr(slash + 1);
                dir.resize(slash);
            } else {
                file = dir;
                dir.clear();
            }
            PostCurrentFile(hwnd, idx, dir, file);
        }

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
    else if (stats.totalFiles == 0 && stats.errorCount == 0)
        swprintf_s(summary, L"No files found in source folder.");
    else if (stats.changeCount == 0 && stats.errorCount == 0)
        swprintf_s(summary, L"All %llu files already up to date.", stats.totalFiles);
    else if (stats.errorCount > 0)
        swprintf_s(summary, L"Completed in %d min %d sec  (%llu errors)", mins, secs, stats.errorCount);
    else
        swprintf_s(summary, L"Completed in %d min %d sec with no errors", mins, secs);

    PostLog(hwnd, idx, summary, 1);

    PostProgress(hwnd, idx, stats, cancel->load() ? JobStatus::Stopped : JobStatus::Done);
    PostMessageW(hwnd, WM_JOB_DONE, (WPARAM)idx, (LPARAM)stats.errorCount);

    } // end try
    catch (const std::exception& ex) {
        // Best-effort: log the error (PostLog is itself nothrow)
        try {
            wchar_t msg[256];
            swprintf_s(msg, L"Copy thread exception: %hs", ex.what());
            PostLog(hwnd, idx, msg, 1);
        } catch (...) {}
        // Guaranteed: always unblock the main window
        PostMessageW(hwnd, WM_JOB_DONE, (WPARAM)idx, (LPARAM)1);
    }
    catch (...) {
        PostLog(hwnd, idx, L"Unhandled exception in copy thread", 1);
        PostMessageW(hwnd, WM_JOB_DONE, (WPARAM)idx, (LPARAM)1);
    }

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
