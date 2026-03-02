#pragma once

// ── Icons ─────────────────────────────────────────────────────────────────────
#define IDI_APP                 100

// ── Menus ────────────────────────────────────────────────────────────────────
#define IDR_MENU_MAIN           101

// ── Dialogs ──────────────────────────────────────────────────────────────────
#define IDD_ADD_JOB             110

// ── Menu command IDs ─────────────────────────────────────────────────────────
#define ID_FILE_NEWJOB          1001
#define ID_FILE_EDITJOB         1002
#define ID_FILE_DELETEJOB       1003
#define ID_FILE_RUNJOB          1004
#define ID_FILE_STOPJOB         1005
#define ID_FILE_EXIT            1006

#define ID_OPTIONS_SETTINGS     1101

#define ID_HELP_ABOUT           1201

// ── Toolbar button IDs ───────────────────────────────────────────────────────
#define ID_BTN_GO               2001
#define ID_BTN_PLAY             2002
#define ID_BTN_PAUSE            2003
#define ID_BTN_STOP             2004
#define ID_BTN_STOPSQ           2005

// ── Child window IDs ─────────────────────────────────────────────────────────
#define IDC_JOBLIST             3001
#define IDC_LOGPANEL            3002
#define IDC_STATUSSTRIP         3003
#define IDC_SPLITTER            3004

// ── Add-Job dialog controls ──────────────────────────────────────────────────
#define IDC_EDIT_JOBNAME        4001
#define IDC_EDIT_SOURCE         4002
#define IDC_EDIT_DEST           4003
#define IDC_BTN_BROWSE_SRC      4004
#define IDC_BTN_BROWSE_DST      4005

// ── Custom window messages (C++ only, not for RC files) ──────────────────────
#ifndef RC_INVOKED
#define WM_JOB_PROGRESS         (WM_APP + 1)   // lParam = new ProgressMsg* (heap; receiver frees)
#define WM_JOB_LOG              (WM_APP + 2)   // lParam = new LogMsg*      (heap; receiver frees)
#define WM_JOB_DONE             (WM_APP + 3)   // wParam = jobIndex, lParam = errorCount
#endif
