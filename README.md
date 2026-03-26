# FileCopyUtility

A Windows desktop file-copy/backup utility written in pure Win32 API + Common Controls —
no MFC, no ATL, no third-party UI libraries.  Static CRT linkage means no runtime
redistribution is needed.

![FileCopyUtility main window](mainscreen.png)

---

## What This Is

A lightweight, self-contained backup tool that copies changed files from one folder to
another.  It compares last-write timestamps and file sizes to skip files that are already
up to date, so only changed files are transferred.

Key characteristics:

- **Pure Win32** — no frameworks, no dependencies beyond the Windows SDK
- **Static CRT** (`/MT` Release, `/MTd` Debug) — single portable EXE, ~266 KB Release
- **Background copy thread** with live progress — pause and stop at any time
- **Collapsible log view** — per-job timestamped log with expandable sections
- **DPI-aware** — row heights and column widths measured from actual font metrics at runtime;
  scales correctly at 125 %, 150 %, 200 % DPI
- **Multiple jobs** — each job has its own source/destination pair and independent state

Built with Visual Studio 2022, v143 toolset, C++17, targeting x64.

---

## Features

| Feature | Details |
|---------|---------|
| File comparison | Last-write timestamp + size; copies only changed files |
| Live progress | Per-file progress via `CopyFileExW` callback; overall bytes/files counters |
| Pause / Resume | Pause between files; current file copies to completion first |
| Stop | Cancels after the current file; reports elapsed time and error count |
| Log view | Timestamped entries (`HH:MM:SS.mmm`), collapsible groups, live current-file row |
| Job persistence | Jobs saved to an INI file next to the EXE; restored on next launch |
| DPI scaling | `PerMonitorV2` DPI awareness; no hardcoded pixel sizes |
| Unicode filenames | Uses `W` API variants throughout; handles all valid Windows filenames |

---

## Building

### Requirements

- Visual Studio 2022 (Community or higher) with the **Desktop development with C++** workload
- Inno Setup 6 (only needed to build the installer)

### From Git Bash

Git Bash mangles MSBuild flags like `/p:` into Unix paths.  Prefix the command with
`MSYS_NO_PATHCONV=1` to prevent this:

```bash
MSYS_NO_PATHCONV=1 "/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" \
    FileCopyUtility.vcxproj /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
```

Or open `FileCopyUtility.sln` in Visual Studio and build normally.

### Build Installer

After building, create a distributable setup EXE with:

```cmd
build-installer.cmd
```

This will:

1. Build `Release|x64`
2. Compile `installer\FileCopyUtility.iss` using Inno Setup (`ISCC.exe`)
3. Write the installer to `installers\`

If Inno Setup is not installed, get it from [jrsoftware.org](https://jrsoftware.org/isdl.php).

### Output

| Configuration | EXE location |
|---------------|-------------|
| Debug x64 | `bin\Debug_x64\FileCopyUtility.exe` |
| Release x64 | `bin\Release_x64\FileCopyUtility.exe` |

---

## Project Structure

```
FileCopyUtility/
├── main.cpp              Entry point, registers window classes, message loop
├── MainWindow.h/cpp      Top-level frame: toolbar, splitter, status strip
├── JobListPanel.h/cpp    Custom-painted job card list
├── LogPanel.h/cpp        Custom-painted expandable log tree
├── CopyEngine.h/cpp      Background copy thread
├── CopyJob.h             Shared data types (JobStatus, LogEntry, JobStats, …)
├── AddJobDlg.h/cpp       Add/Edit job dialog
├── utils.h               Shared inline utilities (TimestampNow)
├── logger.h/logger.c     Thread-safe debug/file logger
├── resource.h            All resource and message IDs
├── resources.rc          Menu, dialog template, VERSIONINFO
├── FileCopyUtility.manifest  ComCtl v6 + PerMonitorV2 DPI awareness
└── FileCopyUtility.vcxproj
```