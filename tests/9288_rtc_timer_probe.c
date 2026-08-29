#include "Dsys.h"
#include "gam4980_types.h"

#define PROBE_TIMER_ID 1
#define PROBE_TIMER_SPEED 1
#define PROBE_TIMER_MESSAGES 400u

/*
 * The V1.5 firmware used by the 9288 exposes its live RTC reader at GUI
 * table +0x590.  Official 时间.exe calls this six-output ABI; the public
 * SDK's GetCurrDateTime member at +0x57c targets an older, incompatible ABI.
 */
#define GUI_GET_RTC6_OFFSET 0x590u

typedef void (*gui_get_rtc6_fn)(
    u8 *second, u8 *minute, u8 *hour,
    u16 *day, u16 *month, u16 *year
);

static T_GUI_HWND g_probe_window;
static volatile u32 g_timer_messages;
static volatile int g_timer_done;
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

static u32 date_day_number(u16 year, u16 month, u16 day)
{
    static const u16 days_before_month[12] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    u32 y;
    u32 result;

    if (year == 0u || month == 0u || month > 12u || day == 0u || day > 31u)
        return 0u;
    y = (u32)year - 1u;
    result = y * 365u + y / 4u - y / 100u + y / 400u;
    result += days_before_month[month - 1u] + day;
    if (month > 2u && (year % 4u) == 0u &&
        ((year % 100u) != 0u || (year % 400u) == 0u))
        ++result;
    return result;
}

static int read_rtc(u32 *day, u32 *time_ms)
{
    gui_get_rtc6_fn get_rtc = *(gui_get_rtc6_fn *)(
        (u8 *)tpDL_GUITable + GUI_GET_RTC6_OFFSET
    );
    u8 second = 0;
    u8 minute = 0;
    u8 hour = 0;
    u16 date_day = 0;
    u16 month = 0;
    u16 year = 0;

    get_rtc(&second, &minute, &hour, &date_day, &month, &year);
    if (hour > 23u || minute > 59u || second > 59u ||
        month == 0u || month > 12u || date_day == 0u || date_day > 31u ||
        year < 1900u)
        return 0;
    *day = date_day_number(year, month, date_day);
    *time_ms = ((u32)hour * 3600u + (u32)minute * 60u +
        (u32)second) * 1000u;
    return 1;
}

static int elapsed_rtc_ms(
    u32 start_day, u32 start_time_ms, u32 end_day, u32 end_time_ms,
    u32 *elapsed
)
{
    u32 day_delta = (u16)((u16)end_day - (u16)start_day);

    if (day_delta == 0u && end_time_ms >= start_time_ms) {
        *elapsed = end_time_ms - start_time_ms;
        return *elapsed != 0u;
    }
    if (day_delta == 1u) {
        *elapsed = 86400000u - start_time_ms + end_time_ms;
        return *elapsed != 0u;
    }
    return 0;
}

static T_WORD probe_window_proc(
    T_GUI_HWND window, T_WORD message, T_GUI_WPARAM wparam, T_GUI_LPARAM lparam
)
{
    (void)wparam;
    (void)lparam;
    switch (message) {
    case MSG_TIMER:
        if (++g_timer_messages >= PROBE_TIMER_MESSAGES) {
            (void)fnGUI_KillTimer(window, PROBE_TIMER_ID);
            g_timer_done = 1;
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
    info.spCaption = (const T_BYTE *)"RTC TIMER TEST";
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

T_WORD App_Main(void)
{
    T_GUI_Msg message;
    u32 start_day = 0;
    u32 start_time_ms = 0;
    u32 end_day = 0;
    u32 end_time_ms = 0;
    u32 elapsed_ms = 0;
    int start_valid;
    int end_valid;
    int elapsed_valid;
    char *out;

    (void)fnGUI_MessageBox(
        HWND_DESKTOP,
        (const T_BYTE *)"Minimal test. Press OK before the first RTC read.",
        (const T_BYTE *)"RTC TIMER TEST",
        MB_OK
    );
    if (!create_probe_window()) {
        (void)fnGUI_MessageBox(
            HWND_DESKTOP, (const T_BYTE *)"CreateMainWindow failed.",
            (const T_BYTE *)"RTC TIMER TEST", MB_OK
        );
        return -1;
    }

    (void)fnGUI_MessageBox(
        g_probe_window,
        (const T_BYTE *)"Press OK to start counting 400 timer messages.",
        (const T_BYTE *)"RTC TIMER TEST",
        MB_OK
    );
    start_valid = read_rtc(&start_day, &start_time_ms);
    if (!fnGUI_SetTimer(g_probe_window, PROBE_TIMER_ID, PROBE_TIMER_SPEED)) {
        close_probe_window();
        (void)fnGUI_MessageBox(
            HWND_DESKTOP, (const T_BYTE *)"SetTimer failed.",
            (const T_BYTE *)"RTC TIMER TEST", MB_OK
        );
        return -2;
    }

    while (!g_timer_done && fnGUI_GetMessage(&message, 0)) {
        fnGUI_TranslateMessage(&message);
        fnGUI_DispatchMessage(&message);
    }
    end_valid = read_rtc(&end_day, &end_time_ms);
    elapsed_valid = start_valid && end_valid && elapsed_rtc_ms(
        start_day, start_time_ms, end_day, end_time_ms, &elapsed_ms
    );

    out = g_result;
    out = append_text(out, "timer_messages=");
    out = append_u32(out, g_timer_messages);
    out = append_text(out, "\nexpected_gui_ms=10000\nrtc_start_day=");
    out = append_u32(out, start_day);
    out = append_text(out, "\nrtc_start_time_ms=");
    out = append_u32(out, start_time_ms);
    out = append_text(out, "\nrtc_end_day=");
    out = append_u32(out, end_day);
    out = append_text(out, "\nrtc_end_time_ms=");
    out = append_u32(out, end_time_ms);
    out = append_text(out, "\nrtc_elapsed_ms=");
    out = append_u32(out, elapsed_ms);
    out = append_text(out, "\nrtc_valid=");
    out = append_u32(out, elapsed_valid != 0);
    *out = 0;

    (void)fnGUI_MessageBox(
        g_probe_window, (const T_BYTE *)g_result,
        (const T_BYTE *)"RTC TIMER RESULT", MB_OK
    );
    close_probe_window();
    return 0;
}
