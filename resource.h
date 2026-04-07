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

// ── Settings dialog ─────────────────────────────────────────────────────────
#define IDD_SETTINGS            111
#define IDC_CHK_CLOSE_TO_TRAY   4040
#define IDC_CHK_START_WITH_WIN  4041

// ── Add-Job dialog controls ──────────────────────────────────────────────────
#define IDC_EDIT_JOBNAME        4001
#define IDC_EDIT_SOURCE         4002
#define IDC_EDIT_DEST           4003
#define IDC_BTN_BROWSE_SRC      4004
#define IDC_BTN_BROWSE_DST      4005
#define IDC_RADIO_MANUAL        4010
#define IDC_RADIO_INTERVAL      4011
#define IDC_RADIO_DAILY         4012
#define IDC_EDIT_INTERVAL       4013
#define IDC_EDIT_DAILY_TIME     4014
#define IDC_EDIT_EXCLUDE        4020
#define IDC_EDIT_INCLUDE        4021
#define IDC_BTN_BROWSE_EXCL     4022
#define IDC_BTN_BROWSE_INCL     4023
#define IDC_CHK_SMART_DEFER     4030
#define IDC_EDIT_QUIET_PERIOD   4031

// ── Custom window messages (C++ only, not for RC files) ──────────────────────
#ifndef RC_INVOKED
#define WM_JOB_PROGRESS         (WM_APP + 1)   // lParam = new ProgressMsg* (heap; receiver frees)
#define WM_JOB_LOG              (WM_APP + 2)   // lParam = new LogMsg*      (heap; receiver frees)
#define WM_JOB_DONE             (WM_APP + 3)   // wParam = jobIndex, lParam = errorCount
#define WM_JOB_SPACE_CHECK      (WM_APP + 4)   // lParam = SpaceCheckInfo* (stack; SendMessage blocks)
#define WM_TRAYICON             (WM_APP + 5)   // Shell_NotifyIcon callback
#define WM_JOB_CHANGED          (WM_APP + 6)   // FolderWatcher: source directory changed
#define WM_SCHEDULE_TIMER       (WM_APP + 7)   // schedule check timer
#define WM_JOB_ACCESS_CHECK     (WM_APP + 8)   // lParam = AccessCheckInfo* (stack; SendMessage blocks)

// Timer IDs
#define IDT_SCHEDULE_POLL       5001
#define IDT_DEFER_CHECK         5002

// Tray menu IDs
#define ID_TRAY_OPEN            6001
#define ID_TRAY_RUNALL          6002
#define ID_TRAY_PAUSEALL        6003
#define ID_TRAY_EXIT            6004
#endif
