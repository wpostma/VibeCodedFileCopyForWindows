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

static constexpr int kCopyWorkers = 2;
static constexpr ULONGLONG kMinFreeBytes = 200ULL * 1024 * 1024;  // 200 MB

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

static std::wstring ToLongPath(const std::wstring& p)
{
    if (p.size() >= 4 && p[0] == L'\\' && p[1] == L'\\' &&
        (p[2] == L'?' || p[2] == L'.') && p[3] == L'\\')
        return p;

    if (p.size() >= 2 && p[0] == L'\\' && p[1] == L'\\')
        return L"\\\\?\\UNC\\" + p.substr(2);

    return L"\\\\?\\" + p;
}

static void EnsureDirectoryExists(const std::wstring& path)
{
    if (CreateDirectoryW(path.c_str(), nullptr)) return;
    DWORD err = GetLastError();
    if (err == ERROR_ALREADY_EXISTS) return;
    if (err != ERROR_PATH_NOT_FOUND)  return;

    size_t slash = path.rfind(L'\\');
    if (slash == std::wstring::npos || slash <= 6) return;

    EnsureDirectoryExists(path.substr(0, slash));
    CreateDirectoryW(path.c_str(), nullptr);
}

// ── File enumeration ──────────────────────────────────────────────────────────

struct FindGuard {
    HANDLE h;
    explicit FindGuard(HANDLE h) : h(h) {}
    ~FindGuard() { if (h != INVALID_HANDLE_VALUE) FindClose(h); }
    FindGuard(const FindGuard&) = delete;
    FindGuard& operator=(const FindGuard&) = delete;
};

struct FileEntry {
    std::wstring relPath;
    FILETIME     lastWrite;
    ULONGLONG    size;
    bool         isDir;
};

// ── Filter helpers ───────────────────────────────────────────────────────────

static bool MatchesAnyPattern(const wchar_t* filename,
                               const std::vector<std::wstring>& patterns)
{
    for (const auto& pat : patterns)
        if (PathMatchSpecW(filename, pat.c_str()))
            return true;
    return false;
}

static void EnumerateDir(const std::wstring& root, const std::wstring& rel,
                          std::vector<FileEntry>& out,
                          const std::vector<std::wstring>& excludes = {},
                          const std::vector<std::wstring>& includes = {},
                          std::vector<std::wstring>* skippedSymlinks = nullptr)
{
    std::wstring pattern = root + L"\\" + rel + L"*";
    WIN32_FIND_DATAW fd;
    FindGuard guard(FindFirstFileW(pattern.c_str(), &fd));
    if (guard.h == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        if (_wcsicmp(fd.cFileName, L"nul") == 0)
            continue;

        // Skip symlinks/junctions/reparse points to avoid infinite recursion.
        // A junction pointing to a parent directory would loop forever.
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            if (skippedSymlinks)
                skippedSymlinks->push_back(rel + fd.cFileName);
            continue;
        }

        // Exclude filter applies to both files and directories.
        // A pattern like ".git" or "node_modules" skips the entire subtree.
        // Include filter applies to files only (directories are always recursed
        // unless excluded, so you can include "*.cpp" without blocking folders).
        if (!excludes.empty() && MatchesAnyPattern(fd.cFileName, excludes))
            continue;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            if (!includes.empty() && !MatchesAnyPattern(fd.cFileName, includes))
                continue;
        }

        std::wstring relChild = rel + fd.cFileName;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            FileEntry fe;
            fe.relPath   = relChild + L"\\";
            fe.lastWrite = fd.ftLastWriteTime;
            fe.size      = 0;
            fe.isDir     = true;
            out.push_back(fe);
            EnumerateDir(root, relChild + L"\\", out, excludes, includes, skippedSymlinks);
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

// ── Parallel copy infrastructure ─────────────────────────────────────────────

struct CopyWorkItem {
    std::wstring srcFull;
    std::wstring dstFull;
    std::wstring relPath;   // for logging
    ULONGLONG    size;
};

// Shared state between scanner thread and copy workers.
// The scanner pushes work items; workers pop and copy.
struct SharedCopyState {
    CRITICAL_SECTION       cs;
    CONDITION_VARIABLE     cv;

    // Work queue (scanner pushes, workers pop via nextItem index)
    std::vector<CopyWorkItem> items;
    size_t                    nextItem = 0;
    bool                      scanDone = false;

    // Aggregate stats (protected by cs)
    JobStats stats;

    // Immutable after construction
    HWND hwnd;
    int  jobIndex;
    std::shared_ptr<std::atomic<bool>> cancelFlag;
    std::shared_ptr<std::atomic<bool>> pauseFlag;
};

// ── Worker copy-progress callback ────────────────────────────────────────────

struct WorkerCBCtx {
    SharedCopyState* shared;
    ULONGLONG        prevBytesThisFile;   // track delta per callback
};

static DWORD CALLBACK WorkerCopyProgressCB(
    LARGE_INTEGER /*TotalFileSize*/, LARGE_INTEGER TotalBytesTransferred,
    LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD dwCallbackReason,
    HANDLE, HANDLE, LPVOID lpData)
{
    auto* ctx = reinterpret_cast<WorkerCBCtx*>(lpData);
    if (ctx->shared->cancelFlag->load()) return PROGRESS_CANCEL;

    if (dwCallbackReason == CALLBACK_CHUNK_FINISHED ||
        dwCallbackReason == CALLBACK_STREAM_SWITCH)
    {
        ULONGLONG transferred = (ULONGLONG)TotalBytesTransferred.QuadPart;
        ULONGLONG delta = transferred - ctx->prevBytesThisFile;
        ctx->prevBytesThisFile = transferred;

        EnterCriticalSection(&ctx->shared->cs);
        ctx->shared->stats.copiedBytes += delta;
        JobStats snapshot = ctx->shared->stats;
        LeaveCriticalSection(&ctx->shared->cs);

        PostProgress(ctx->shared->hwnd, ctx->shared->jobIndex,
                     snapshot, JobStatus::Copying);
    }
    return PROGRESS_CONTINUE;
}

// ── Copy worker thread ───────────────────────────────────────────────────────

static DWORD WINAPI CopyWorkerProc(LPVOID lpParam)
{
    auto* st = reinterpret_cast<SharedCopyState*>(lpParam);

    while (true) {
        if (st->cancelFlag->load()) break;

        // Honour pause — spin-wait until cleared
        while (st->pauseFlag->load() && !st->cancelFlag->load())
            Sleep(50);
        if (st->cancelFlag->load()) break;

        // ── Pull next work item ──────────────────────────────────────────
        CopyWorkItem item;
        bool gotItem = false;

        EnterCriticalSection(&st->cs);
        while (st->nextItem >= st->items.size() && !st->scanDone
               && !st->cancelFlag->load())
        {
            SleepConditionVariableCS(&st->cv, &st->cs, 100);
        }
        if (st->nextItem < st->items.size()) {
            item = std::move(st->items[st->nextItem]);
            st->nextItem++;
            gotItem = true;
        }
        LeaveCriticalSection(&st->cs);

        if (!gotItem) break;   // queue drained + scanDone, or cancelled

        // ── Show current file in log panel ───────────────────────────────
        {
            std::wstring dir  = item.relPath;
            std::wstring file;
            size_t slash = dir.rfind(L'\\');
            if (slash != std::wstring::npos) {
                file = dir.substr(slash + 1);
                dir.resize(slash);
            } else {
                file = dir;
                dir.clear();
            }
            PostCurrentFile(st->hwnd, st->jobIndex, dir, file);
        }

        // ── Copy the file ────────────────────────────────────────────────
        WorkerCBCtx cbCtx{ st, 0 };
        BOOL  cancelled = FALSE;
        DWORD flags     = COPY_FILE_ALLOW_DECRYPTED_DESTINATION;
        BOOL  ok        = CopyFileExW(item.srcFull.c_str(), item.dstFull.c_str(),
                                      WorkerCopyProgressCB, &cbCtx,
                                      &cancelled, flags);

        // ── Update shared stats ──────────────────────────────────────────
        EnterCriticalSection(&st->cs);
        if (!ok && !cancelled) {
            st->stats.errorCount++;
        } else if (!cancelled) {
            st->stats.copiedFiles++;
            // Credit any bytes the callback didn't report (small files
            // may complete in a single chunk with no callback).
            ULONGLONG remaining = item.size - cbCtx.prevBytesThisFile;
            st->stats.copiedBytes += remaining;
        }
        JobStats snapshot = st->stats;
        LeaveCriticalSection(&st->cs);

        if (!ok && !cancelled) {
            DWORD err = GetLastError();
            const wchar_t* desc;
            switch (err) {
                case ERROR_ACCESS_DENIED:        desc = L" - Access denied";        break;
                case ERROR_SHARING_VIOLATION:    desc = L" - File in use";          break;
                case ERROR_DISK_FULL:            desc = L" - Disk full";            break;
                case ERROR_PATH_NOT_FOUND:       desc = L" - Path not found";       break;
                case ERROR_FILE_NOT_FOUND:       desc = L" - File not found";       break;
                case ERROR_WRITE_PROTECT:        desc = L" - Write protected";      break;
                case ERROR_NETWORK_UNREACHABLE:  desc = L" - Network unreachable";  break;
                case ERROR_NETNAME_DELETED:      desc = L" - Network path lost";    break;
                default:                         desc = L"";                        break;
            }
            wchar_t msg[300];
            swprintf_s(msg, L"Error copying %s  (0x%08X%s)",
                       item.relPath.c_str(), err, desc);
            PostLog(st->hwnd, st->jobIndex, msg, 2);
        } else if (!cancelled) {
            PostProgress(st->hwnd, st->jobIndex, snapshot, JobStatus::Copying);

            // Auto-stop if destination drive is critically low (< 200 MB).
            // Only check every 200 files to avoid hammering the filesystem.
            if (snapshot.copiedFiles % 200 == 0) {
                ULARGE_INTEGER freeBytes = {};
                if (GetDiskFreeSpaceExW(item.dstFull.c_str(), nullptr, nullptr, &freeBytes)
                    && freeBytes.QuadPart < kMinFreeBytes)
                {
                    st->cancelFlag->store(true);
                    PostLog(st->hwnd, st->jobIndex,
                            L"Auto-stopped: destination drive below 200 MB free", 1);
                }
            }
        }
    }

    return 0;
}

// ── Scanner / orchestrator thread ────────────────────────────────────────────

static DWORD WINAPI CopyThreadProc(LPVOID lpParam)
{
    auto* rawEp = reinterpret_cast<EngineParams*>(lpParam);
    HWND hwnd = rawEp->hwndMain;
    int  idx  = rawEp->jobIndex;

    try {
    std::unique_ptr<EngineParams> epOwner(rawEp);
    EngineParams params = *epOwner;
    epOwner.reset();

    params.sourcePath = ToLongPath(params.sourcePath);
    params.destPath   = ToLongPath(params.destPath);

    auto stripTrailingSlash = [](std::wstring& p) {
        while (p.size() > 7 && p.back() == L'\\')
            p.pop_back();
    };
    stripTrailingSlash(params.sourcePath);
    stripTrailingSlash(params.destPath);

    auto&  cancel  = params.cancelFlag;
    auto&  pause   = params.pauseFlag;

    ULONGLONG startTick = GetTickCount64();

    // ── Phase 1: Enumerate ───────────────────────────────────────────────
    PostLog(hwnd, idx, L"Running the backup...", 0, true);
    PostLog(hwnd, idx, L"Preparing...", 1, true);

    std::vector<FileEntry> files;
    std::vector<std::wstring> skippedSymlinks;
    EnumerateDir(params.sourcePath, L"", files, params.excludePatterns,
                 params.includePatterns, &skippedSymlinks);

    // Log skipped symlinks/junctions so the user knows
    for (const auto& sl : skippedSymlinks)
        PostLog(hwnd, idx, L"Skipped symlink/junction: " + sl, 1);

    if (cancel->load()) {
        PostLog(hwnd, idx, L"Stopped by user", 1);
        PostMessageW(hwnd, WM_JOB_DONE, (WPARAM)idx, 0);
        return 0;
    }

    // ── Pre-flight: destination must exist and be writable ───────────────────
    {
        // Try to create destination root (ERROR_ALREADY_EXISTS is fine).
        if (!CreateDirectoryW(params.destPath.c_str(), nullptr)) {
            DWORD err = GetLastError();
            if (err != ERROR_ALREADY_EXISTS) {
                AccessCheckInfo info{ params.destPath, err };
                LRESULT ans = SendMessageW(hwnd, WM_JOB_ACCESS_CHECK,
                                           (WPARAM)idx, (LPARAM)&info);
                // Retry after user-initiated fix
                if (ans != IDYES || (!CreateDirectoryW(params.destPath.c_str(), nullptr)
                                      && GetLastError() != ERROR_ALREADY_EXISTS)) {
                    PostLog(hwnd, idx, L"Stopped: cannot create destination folder", 1);
                    JobStats empty{};
                    PostProgress(hwnd, idx, empty, JobStatus::Error);
                    PostMessageW(hwnd, WM_JOB_DONE, (WPARAM)idx, 1);
                    return 0;
                }
            }
        }

        // Probe write access: create a zero-byte temp file and delete it immediately.
        std::wstring testFile = params.destPath + L"\\.fcu_write_test";
        HANDLE hTest = CreateFileW(testFile.c_str(), GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                                   nullptr);
        if (hTest == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            AccessCheckInfo info{ params.destPath, err };
            LRESULT ans = SendMessageW(hwnd, WM_JOB_ACCESS_CHECK,
                                       (WPARAM)idx, (LPARAM)&info);
            if (ans == IDYES) {
                // Retry after user fixed permissions
                hTest = CreateFileW(testFile.c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                                    nullptr);
            }
            if (hTest == INVALID_HANDLE_VALUE) {
                PostLog(hwnd, idx, L"Stopped: destination folder is not writable", 1);
                JobStats empty{};
                PostProgress(hwnd, idx, empty, JobStatus::Error);
                PostMessageW(hwnd, WM_JOB_DONE, (WPARAM)idx, 1);
                return 0;
            }
        }
        CloseHandle(hTest);
    }

    // ── Set up shared state ──────────────────────────────────────────────
    SharedCopyState shared;
    InitializeCriticalSection(&shared.cs);
    InitializeConditionVariable(&shared.cv);
    shared.hwnd       = hwnd;
    shared.jobIndex   = idx;
    shared.cancelFlag = cancel;
    shared.pauseFlag  = pause;
    memset(&shared.stats, 0, sizeof(shared.stats));

    // Count totals
    for (auto& f : files) {
        if (f.isDir)  shared.stats.totalFolders++;
        else {        shared.stats.totalFiles++; shared.stats.totalBytes += f.size; }
    }

    {
        wchar_t dbg[256];
        swprintf_s(dbg, L"Enumerated %llu files, %llu folders, %llu bytes",
                   shared.stats.totalFiles, shared.stats.totalFolders,
                   shared.stats.totalBytes);
        PostLog(hwnd, idx, dbg, 1);
    }

    // ── Pre-flight disk space check ──────────────────────────────────────
    // If free space on dest is less than total source size, ask the user
    // before proceeding (most data may already be there, so it's a warning,
    // not a hard block).
    {
        ULARGE_INTEGER freeBytes = {};
        if (GetDiskFreeSpaceExW(params.destPath.c_str(), nullptr, nullptr, &freeBytes)
            && freeBytes.QuadPart < shared.stats.totalBytes)
        {
            SpaceCheckInfo info{ freeBytes.QuadPart, shared.stats.totalBytes };
            // SendMessageW blocks this thread until the UI thread responds.
            LRESULT answer = SendMessageW(hwnd, WM_JOB_SPACE_CHECK,
                                          (WPARAM)idx, (LPARAM)&info);
            if (answer != IDYES) {
                PostLog(hwnd, idx, L"Stopped: user declined (low disk space)", 1);
                PostProgress(hwnd, idx, shared.stats, JobStatus::Stopped);
                PostMessageW(hwnd, WM_JOB_DONE, (WPARAM)idx, 0);
                DeleteCriticalSection(&shared.cs);
                return 0;
            }
        }
    }

    // ── Launch copy workers ──────────────────────────────────────────────
    HANDLE workers[kCopyWorkers] = {};
    for (int i = 0; i < kCopyWorkers; i++)
        workers[i] = CreateThread(nullptr, 0, CopyWorkerProc, &shared, 0, nullptr);

    // ── Phase 2: Compare & enqueue ───────────────────────────────────────
    PostLog(hwnd, idx, L"Processing...", 1, true);

    for (auto& f : files) {
        if (cancel->load()) break;

        std::wstring dstFull = params.destPath + L"\\" + f.relPath;

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
                    EnterCriticalSection(&shared.cs);
                    shared.stats.skippedFiles++;
                    LeaveCriticalSection(&shared.cs);
                }
            }
        }

        if (!needsCopy) continue;

        // Ensure destination directory exists (before enqueuing)
        std::wstring dstDir = dstFull;
        size_t slash = dstDir.rfind(L'\\');
        if (slash != std::wstring::npos) {
            dstDir.resize(slash);
            EnsureDirectoryExists(dstDir);
        }

        std::wstring srcFull = params.sourcePath + L"\\" + f.relPath;

        EnterCriticalSection(&shared.cs);
        shared.stats.changeCount++;
        shared.stats.bytesToCopy += f.size;
        shared.items.push_back({ std::move(srcFull), std::move(dstFull),
                                 f.relPath, f.size });
        LeaveCriticalSection(&shared.cs);
        WakeConditionVariable(&shared.cv);   // wake one worker
    }

    // Signal workers: no more items coming
    EnterCriticalSection(&shared.cs);
    shared.scanDone = true;
    LeaveCriticalSection(&shared.cs);
    WakeAllConditionVariable(&shared.cv);

    // Wait for workers to finish
    WaitForMultipleObjects(kCopyWorkers, workers, TRUE, INFINITE);
    for (int i = 0; i < kCopyWorkers; i++)
        if (workers[i]) CloseHandle(workers[i]);

    // ── Phase 3: Done ────────────────────────────────────────────────────
    JobStats stats = shared.stats;
    DeleteCriticalSection(&shared.cs);

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
        swprintf_s(summary, L"Completed in %d min %d sec  (%llu errors, %llu skipped)",
                   mins, secs, stats.errorCount, stats.skippedFiles);
    else if (stats.skippedFiles > 0)
        swprintf_s(summary, L"Completed in %d min %d sec  (%llu skipped, no errors)",
                   mins, secs, stats.skippedFiles);
    else
        swprintf_s(summary, L"Completed in %d min %d sec with no errors", mins, secs);

    PostLog(hwnd, idx, summary, 1);

    PostProgress(hwnd, idx, stats, cancel->load() ? JobStatus::Stopped : JobStatus::Done);
    PostMessageW(hwnd, WM_JOB_DONE, (WPARAM)idx, (LPARAM)stats.errorCount);

    } // end try
    catch (const std::exception& ex) {
        try {
            wchar_t msg[256];
            swprintf_s(msg, L"Copy thread exception: %hs", ex.what());
            PostLog(hwnd, idx, msg, 1);
        } catch (...) {}
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
#ifdef _DEBUG
    Log(L"[StartCopyEngine] src='%s' dst='%s'", params.sourcePath.c_str(),
        params.destPath.c_str());
#endif
    auto* ep = new EngineParams(params);
    HANDLE h = CreateThread(nullptr, 0, CopyThreadProc, ep, 0, nullptr);
    if (!h) delete ep;
    return h;
}
