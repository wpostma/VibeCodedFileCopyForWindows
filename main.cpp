#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include "MainWindow.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    // Enable visual styles (requires comctl32 v6 via manifest or pragma)
    INITCOMMONCONTROLSEX icex = { sizeof(icex),
        ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icex);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    MainWindow wnd;
    if (!wnd.Create(hInstance)) {
        MessageBoxW(nullptr, L"Failed to create main window.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    wnd.Show(nCmdShow);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return (int)msg.wParam;
}
