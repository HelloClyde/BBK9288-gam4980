#include "Dsys.h"

static T_GUI_HWND test_window;

static T_WORD test_window_proc(
    T_GUI_HWND window, T_WORD message,
    T_GUI_WPARAM wparam, T_GUI_LPARAM lparam
)
{
    if (message == MSG_ERASEBKGND)
        return 0;
    if (message == MSG_KEYDOWN && LOUHWORD(wparam) == SCANCODE_ESCAPE)
        fnGUI_PostMessage(window, MSG_CLOSE, 0, 0);
    if (message == MSG_CLOSE) {
        fnGUI_DestroyMainWindow(window);
        fnGUI_PostQuitMessage(window);
        return 0;
    }
    return fnGUI_DefaultMainWinProc(window, message, wparam, lparam);
}

T_WORD App_Main(void)
{
    T_GUI_MainWinCreate info;
    T_GUI_Msg message;
    T_BYTE *bytes = (T_BYTE *)&info;
    unsigned long index;

    for (index = 0; index < (unsigned long)sizeof(info); ++index)
        bytes[index] = 0;
    info.dwStyle = WS_VISIBLE | WS_CAPTION;
    info.dwExStyle = WS_EX_NONE;
    info.spCaption = (const T_BYTE *)"NATIVE MIN";
    info.MainWindowProc = test_window_proc;
    info.rx = 320;
    info.by = 240;
    info.iBkColor = COLOR_LIGHTWHITE;
    info.hHosting = HWND_DESKTOP;
    test_window = fnGUI_CreateMainWindow(&info);
    if (!test_window)
        return 0;
    while (fnGUI_GetMessage(&message, test_window)) {
        fnGUI_TranslateMessage(&message);
        fnGUI_DispatchMessage(&message);
    }
    fnGUI_ThrowAwayMessages(test_window);
    fnGUI_MainWindowCleanup(test_window);
    return 1;
}
