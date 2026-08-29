#include "Dsys.h"
#include "gam4980_types.h"

#define PROBE_TIMER_ID 1
#define PROBE_MESSAGES_PER_STAGE 40u
#define PROBE_STAGE_COUNT 4u

static const T_WORD g_stage_speeds[PROBE_STAGE_COUNT] = {1, 2, 4, 8};
static T_GUI_HWND g_probe_window;
static volatile u32 g_stage_messages;
static volatile int g_stage_done;
static u32 g_stage_ticks[PROBE_STAGE_COUNT];
static char g_result[256];

static char *append_text(char *out, const char *text)
{
    while (*text)
        *out++ = *text++;
    return out;
}

static char *append_u32(char *out, u32 value)
{
    char digits[10];
    unsigned int count = 0;

    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && count < sizeof(digits));
    while (count)
        *out++ = digits[--count];
    return out;
}

static T_WORD probe_window_proc(
    T_GUI_HWND window, T_WORD message, T_GUI_WPARAM wparam, T_GUI_LPARAM lparam
)
{
    (void)wparam;
    (void)lparam;
    switch (message) {
    case MSG_TIMER:
        if (++g_stage_messages >= PROBE_MESSAGES_PER_STAGE) {
            (void)fnGUI_KillTimer(window, PROBE_TIMER_ID);
            g_stage_done = 1;
        }
        return 0;
    case MSG_ERASEBKGND:
        return 0;
    case MSG_CLOSE:
        if (g_probe_window == window)
            g_probe_window = 0;
        fnGUI_DestroyMainWindow(window);
        fnGUI_PostQuitMessage(window);
        return 0;
    default:
        return fnGUI_DefaultMainWinProc(window, message, wparam, lparam);
    }
}

static int create_probe_window(void)
{
    static T_GUI_MainWinCreate info;

    info.dwStyle = WS_VISIBLE | WS_CAPTION;
    info.dwExStyle = WS_EX_NONE;
    info.spCaption = (const T_BYTE *)"TIMER STEP TEST";
    info.MainWindowProc = probe_window_proc;
    info.lx = 20;
    info.ty = 40;
    info.rx = 300;
    info.by = 200;
    info.iBkColor = COLOR_LIGHTWHITE;
    info.hHosting = HWND_DESKTOP;
    g_probe_window = fnGUI_CreateMainWindow(&info);
    if (!g_probe_window)
        return 0;
    (void)fnGUI_ShowWindow(g_probe_window, SW_SHOWNORMAL);
    (void)fnGUI_SetActiveWindow(g_probe_window);
    (void)fnGUI_SetFocus(g_probe_window);
    return 1;
}

static void close_probe_window(void)
{
    T_GUI_Msg message;
    T_GUI_HWND window = g_probe_window;

    if (!window)
        return;
    (void)fnGUI_PostMessage(window, MSG_CLOSE, 0, 0);
    while (fnGUI_GetMessage(&message, window)) {
        fnGUI_TranslateMessage(&message);
        fnGUI_DispatchMessage(&message);
    }
    fnGUI_ThrowAwayMessages(window);
    fnGUI_MainWindowCleanup(window);
}

static int run_stage(unsigned int stage)
{
    T_GUI_Msg message;
    T_UWORD start_tick;
    T_UWORD end_tick;

    g_stage_messages = 0;
    g_stage_done = 0;
    start_tick = fnGUI_GetTickCount();
    if (!fnGUI_SetTimer(
            g_probe_window, PROBE_TIMER_ID, g_stage_speeds[stage]))
        return 0;
    while (!g_stage_done && fnGUI_GetMessage(&message, 0)) {
        fnGUI_TranslateMessage(&message);
        fnGUI_DispatchMessage(&message);
    }
    end_tick = fnGUI_GetTickCount();
    g_stage_ticks[stage] = (u16)(end_tick - start_tick);
    return g_stage_done;
}

T_WORD App_Main(void)
{
    T_UWORD total_start;
    T_UWORD total_end;
    u32 total_ticks;
    unsigned int stage;
    char *out;

    if (!create_probe_window()) {
        (void)fnGUI_MessageBox(
            HWND_DESKTOP, (const T_BYTE *)"CreateMainWindow failed.",
            (const T_BYTE *)"TIMER STEP TEST", MB_OK
        );
        return -1;
    }
    (void)fnGUI_MessageBox(
        g_probe_window,
        (const T_BYTE *)
            "Press OK and start stopwatch together.\n"
            "Runs speed 1,2,4,8; 40 messages each.",
        (const T_BYTE *)"TIMER STEP TEST",
        MB_OK
    );

    total_start = fnGUI_GetTickCount();
    for (stage = 0; stage < PROBE_STAGE_COUNT; ++stage) {
        if (!run_stage(stage)) {
            close_probe_window();
            (void)fnGUI_MessageBox(
                HWND_DESKTOP, (const T_BYTE *)"SetTimer/test failed.",
                (const T_BYTE *)"TIMER STEP TEST", MB_OK
            );
            return -2;
        }
    }
    total_end = fnGUI_GetTickCount();
    total_ticks = (u16)(total_end - total_start);

    out = g_result;
    out = append_text(out, "messages_each=40\n");
    for (stage = 0; stage < PROBE_STAGE_COUNT; ++stage) {
        out = append_text(out, "speed");
        out = append_u32(out, (u32)g_stage_speeds[stage]);
        out = append_text(out, "_ticks=");
        out = append_u32(out, g_stage_ticks[stage]);
        out = append_text(out, "\n");
    }
    out = append_text(out, "total_ticks=");
    out = append_u32(out, total_ticks);
    out = append_text(out, "\nexpected_wall_ms=15000");
    *out = 0;

    (void)fnGUI_MessageBox(
        g_probe_window, (const T_BYTE *)g_result,
        (const T_BYTE *)"TIMER STEP RESULT", MB_OK
    );
    close_probe_window();
    return 0;
}
