# FileCopyUtility — Claude Project Notes

## What This Is

A Windows desktop file-copy/backup utility written in pure Win32 API + Common Controls
(no MFC, no ATL, no third-party UI libraries).  Styled loosely after Bvckup 2.
Static CRT linkage (`/MT` Release, `/MTd` Debug) — no runtime redistribution needed.

Visual Studio 2022, v143 toolset, C++17, x64 primary target.

---

## Building

### From Git Bash (CRITICAL)

Git Bash rewrites arguments that look like Unix paths.  MSBuild flags like `/p:`,
`/m`, `/nologo` get mangled to drive-letter paths.  **Always prefix with
`MSYS_NO_PATHCONV=1`**:

```bash
MSYS_NO_PATHCONV=1 "/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" \
    FileCopyUtility.vcxproj /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal
```

`cmd /c` does not work from Git Bash either (converts `/c` to a drive letter).
Do not use it.  Call MSBuild directly with the prefix above.

### build.cmd

`build.cmd` in the project root wraps the above.  Edit `CONFIG` and `PLAT` variables
at the top to switch configuration.

### Output

| Config | Output |
|--------|--------|
| Debug x64 | `bin\Debug_x64\FileCopyUtility.exe` |
| Release x64 | `bin\Release_x64\FileCopyUtility.exe` |

Intermediate `.obj` files go to `obj\<Config>_<Platform>\`.

Both x64 configs produce a `.map` file next to the EXE (linker `/MAP`).

### EXE Size

| Build | Size | Notes |
|-------|------|-------|
| Debug x64 | ~1.97 MB | `/MTd` debug CRT dominates (~85% of symbols) |
| Release x64 | ~266 KB | `/MT` + `/O2` + `/GL` + dead-code elimination |

The debug heap, RTC stack checks, printf debug paths, and name-demangling tables
account for most of the Debug bloat.  Release is ~7× smaller.

---

## Project Files

| File | Purpose |
|------|---------|
| `main.cpp` | Entry point, registers classes, message loop |
| `MainWindow.h/cpp` | Top-level frame window: toolbar, splitter, job list, log panel, status strip |
| `JobListPanel.h/cpp` | Custom-painted job card list (double-buffered, row height measured from font metrics) |
| `LogPanel.h/cpp` | Custom-painted expandable log tree; timestamp column width measured at runtime |
| `CopyEngine.h/cpp` | Background copy thread: enumerate → compare → copy, posts Win32 messages back |
| `CopyJob.h` | `JobStatus` enum, `LogEntry`, `JobStats`, `CopyJob` struct, `ProgressMsg`/`LogMsg` |
| `AddJobDlg.h/cpp` | Add/Edit job dialog using `IFileOpenDialog` for folder picking |
| `logger.h/logger.c` | Thread-safe logger: `OutputDebugString` + timestamped file in `log\` subfolder |
| `utils.h` | `inline TimestampNow()` — single source of truth for `"YYYY.MM.DD HH:MM:SS.mmm"` |
| `resource.h` | All `#define` IDs: icons, menus, dialogs, toolbar buttons, WM_APP messages |
| `resources.rc` | Menu, Add-Job dialog template, VERSIONINFO |
| `FileCopyUtility.manifest` | ComCtl v6, PerMonitorV2 DPI awareness.  **No** `longPathAware` yet (see Limitations) |
| `FileCopyUtility.vcxproj` | VS project; manifest embedded via `AdditionalManifestFiles` (not via RC) |
| `FileCopyApplicationState.ini` | Persisted job list (next to EXE); `WritePrivateProfileString` / `GetPrivateProfileString` |

---

## Architecture Notes

### WM_COMMAND routing
Toolbar buttons are **direct children of the main `HWND`**, not children of the
STATIC toolbar background strip.  STATIC controls discard `WM_COMMAND` silently.
This was a discovered bug: clicking Run did nothing because buttons were parented
to the STATIC.

### Manifest embedding
Use `<AdditionalManifestFiles>` in the vcxproj `<Manifest>` block.
Do **not** embed the manifest via `CREATEPROCESS_MANIFEST_RESOURCE_ID RT_MANIFEST`
in the RC file — that causes CVT1100 duplicate resource conflict with MSBuild's
manifest tool.

### RC file quirks
- `MENUITEM SEPARATOR` — bare `SEPARATOR` alone in a MENU block gives RC2122.
- `#include <windows.h>` required in `.rc` for `DS_SETFONT` and similar constants.

### Font / DPI metrics
`JobListPanel` and `LogPanel` measure actual font metrics with `GetTextMetrics` /
`GetTextExtentPoint32W` at `WM_CREATE` time.  No hardcoded pixel heights.  Row
heights and column widths scale correctly at 125%, 150%, 200% DPI.

### Copy engine thread safety
The engine thread communicates with the main window exclusively via
`PostMessageW` — never touches UI objects or job list data directly.
Messages: `WM_JOB_PROGRESS`, `WM_JOB_LOG`, `WM_JOB_DONE` (defined in `resource.h`).
Heap-allocated message structs are freed by the receiver (`OnJobProgress`, `OnJobLog`).

`PostLog` / `PostProgress` / `PostCurrentFile` are all `noexcept`-style (internal
`try/catch(...)`) so they never propagate into the Win32 callback (`CopyFileExW`
progress callback must not throw).

The entire thread body is wrapped in `try/catch` — any unhandled exception posts
`WM_JOB_DONE` with `errorCount=1` so the main window always unlocks its state.

### Pause / Stop flags
Each `CopyJob` owns two `shared_ptr<atomic<bool>>`: `cancelFlag` and `pauseFlag`.
Pause is checked **between files** only (current file copies to completion first).
Stop clears `pauseFlag` before setting `cancelFlag` so a paused thread can see the
cancel.

### Live "current file" log slot
During copying, `CopyEngine` posts `replaceCurrentFile=true` log messages.
`MainWindow::OnJobLog` overwrites `job.logEntries[curFileLogIdx]` in place rather
than appending, so the log shows one updating row instead of flooding with file names.
`LogPanel::UpdateEntry()` handles the in-place repaint.

### WM_SETCURSOR
`OnSetCursor` returns `bool`.  WndProc returns `TRUE` when handled to prevent
`DefWindowProc` from resetting the cursor.  `IDC_APPSTARTING` (arrow + spinner) is
shown when any job is `Scanning`, `Copying`, or `Paused`.

---

## Known Limitations

### MAX_PATH / long paths
Using Unicode `W` APIs handles any character a Windows filename can contain, but
does **not** bypass the 260-character MAX_PATH limit.  Paths deeper than ~260 chars
will fail silently.  To fix: add `<longPathAware>true</longPathAware>` to the
manifest `<windowsSettings>` block and prepend `\\?\` to root paths in
`EnumerateDir` and `CmdRunJob`.

### NUL device file
`EnumerateDir` explicitly skips files named `nul` (case-insensitive) because the
Claude Code agent creates zero-byte files with that name in working directories,
and Windows treats `NUL` as a device, not a file.

### Pause granularity
Pause takes effect between files.  A large file being actively copied via
`CopyFileExW` will copy to completion before the pause is honoured.

---

## Logger

`logger.c` (compiled as C, not C++) writes to:
- `OutputDebugString` (visible in VS Output / DebugView)
- `log\YYYY-MM-DDTHH-MM-SS_filecopyutility.log` next to the EXE

Thread-safe via `INIT_ONCE` (one-time init) + `CRITICAL_SECTION` (serialised writes).
Log format: `HH:MM:SS.mmm  <message>` in UTF-8.

Call from C++: `Log(L"format %s %d", wstr.c_str(), n);`

---

## RC / Resource IDs (resource.h)

```
IDI_APP          100    application icon
IDR_MENU_MAIN    101    main menu
IDD_ADD_JOB      110    Add/Edit Job dialog
ID_FILE_*       1001-   File menu commands
ID_BTN_PLAY     2002    toolbar ▶ (run / resume)
ID_BTN_PAUSE    2003    toolbar ⏸
ID_BTN_STOPSQ   2005    toolbar ■
IDC_JOBLIST     3001    job list panel child ID
IDC_LOGPANEL    3002    log panel child ID
WM_JOB_PROGRESS WM_APP+1
WM_JOB_LOG      WM_APP+2
WM_JOB_DONE     WM_APP+3
```
