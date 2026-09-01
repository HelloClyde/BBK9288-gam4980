#include "Dsys.h"
#include "gam4980_types.h"

#define APP_TITLE "FRAME PATH BENCH"
#define TEST_TIMER_ID 1
#define TEST_TIMER_SPEED 1
#define TEST_SECONDS 10u
#define BENCH_SCREEN_WIDTH 320u
#define BENCH_SCREEN_HEIGHT 240u
#define BENCH_SCREEN_PITCH 80u
#define BENCH_SCREEN_BYTES (BENCH_SCREEN_PITCH * BENCH_SCREEN_HEIGHT)
#define VIDEO_BASE 0x003c0000u
#define GUI_GET_RTC6_OFFSET 0x590u
#define GUI_SYS_BLT_FRAME_OFFSET 0x5b0u
#define DIRECT_RTC_POLL_FRAMES 16u
#define BENCH_STORAGE __attribute__((aligned(4), section(".scratch")))

/* The public app_env_9288 declaration is retained as an ABI check.  V1.5
 * firmware has five private table entries before this late game API, so its
 * live entry is +0x5b0 rather than the public structure's +0x59c member. */
typedef char PublicSysBltFrameOffsetMustBe59c[
    __builtin_offsetof(T_GUI_RelocationTable, SysBltFrame) == 0x59cu ? 1 : -1
];

typedef void (*T_Rtc6)(
    u8 *second, u8 *minute, u8 *hour,
    u16 *day, u16 *month, u16 *year
);
typedef void (*T_SysBltFrame)(T_GUI_HDC hdc, unsigned char *screen);

typedef struct T_RtcMarker {
    u32 day;
    u32 time_ms;
} T_RtcMarker;

enum T_BenchMode {
    MODE_STARTING = 0,
    MODE_TIMER_SDK,
    MODE_WAIT_LOOP_SDK,
    MODE_LOOP_SDK,
    MODE_WAIT_DIRECT,
    MODE_DIRECT,
    MODE_RESULT,
    MODE_ERROR
};

static const u8 k_font[36][7] = {
    {0x0e,0x11,0x13,0x15,0x19,0x11,0x0e},
    {0x04,0x0c,0x04,0x04,0x04,0x04,0x0e},
    {0x0e,0x11,0x01,0x02,0x04,0x08,0x1f},
    {0x1e,0x01,0x01,0x0e,0x01,0x01,0x1e},
    {0x02,0x06,0x0a,0x12,0x1f,0x02,0x02},
    {0x1f,0x10,0x10,0x1e,0x01,0x01,0x1e},
    {0x06,0x08,0x10,0x1e,0x11,0x11,0x0e},
    {0x1f,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e},
    {0x0e,0x11,0x11,0x0f,0x01,0x02,0x0c},
    {0x0e,0x11,0x11,0x1f,0x11,0x11,0x11},
    {0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e},
    {0x0e,0x11,0x10,0x10,0x10,0x11,0x0e},
    {0x1e,0x11,0x11,0x11,0x11,0x11,0x1e},
    {0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f},
    {0x1f,0x10,0x10,0x1e,0x10,0x10,0x10},
    {0x0e,0x11,0x10,0x17,0x11,0x11,0x0f},
    {0x11,0x11,0x11,0x1f,0x11,0x11,0x11},
    {0x0e,0x04,0x04,0x04,0x04,0x04,0x0e},
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0c},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1f},
    {0x11,0x1b,0x15,0x15,0x11,0x11,0x11},
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    {0x0e,0x11,0x11,0x11,0x11,0x11,0x0e},
    {0x1e,0x11,0x11,0x1e,0x10,0x10,0x10},
    {0x0e,0x11,0x11,0x11,0x15,0x12,0x0d},
    {0x1e,0x11,0x11,0x1e,0x14,0x12,0x11},
    {0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e},
    {0x1f,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0e},
    {0x11,0x11,0x11,0x11,0x11,0x0a,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0a},
    {0x11,0x11,0x0a,0x04,0x0a,0x11,0x11},
    {0x11,0x11,0x0a,0x04,0x04,0x04,0x04},
    {0x1f,0x01,0x02,0x04,0x08,0x10,0x1f}
};

static T_GUI_HWND g_window;
static T_GUI_HDC g_hdc;
static T_SysBltFrame g_sys_blt_frame;
static volatile enum T_BenchMode g_mode;
static volatile int g_close_requested;
static T_RtcMarker g_timer_sdk_start;
static T_RtcMarker g_loop_sdk_start;
static u32 g_timer_sdk_elapsed_ms;
static u32 g_timer_sdk_messages;
static u32 g_timer_sdk_submissions;
static u32 g_timer_sdk_paints;
static u32 g_loop_sdk_elapsed_ms;
static u32 g_loop_sdk_submissions;
static u32 g_loop_sdk_paints;
static u32 g_direct_elapsed_ms;
static u32 g_direct_frames;
static u32 g_frame_toggle;
static char g_report[1024];
static u8 g_frame_a[BENCH_SCREEN_BYTES] BENCH_STORAGE;
static u8 g_frame_b[BENCH_SCREEN_BYTES] BENCH_STORAGE;
static u8 g_result_frame[BENCH_SCREEN_BYTES] BENCH_STORAGE;

static char *append_text(char *out, const char *text)
{
    while (*text)
        *out++ = *text++;
    return out;
}

static char *append_u32(char *out, u32 value)
{
    char digits[10];
    u32 count = 0u;

    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && count < 10u);
    while (count)
        *out++ = digits[--count];
    return out;
}

static u32 text_length(const char *text)
{
    u32 length = 0u;

    while (text[length])
        ++length;
    return length;
}

static u32 date_day_number(u16 year, u16 month, u16 day)
{
    static const u16 days_before_month[12] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    u32 y;
    u32 result;

    if (year < 1900u || month == 0u || month > 12u ||
        day == 0u || day > 31u)
        return 0u;
    y = (u32)year - 1u;
    result = y * 365u + y / 4u - y / 100u + y / 400u;
    result += days_before_month[month - 1u] + day;
    if (month > 2u && (year % 4u) == 0u &&
        ((year % 100u) != 0u || (year % 400u) == 0u))
        ++result;
    return result;
}

static int read_rtc(T_RtcMarker *marker)
{
    T_Rtc6 read_clock = *(T_Rtc6 *)(void *)(
        (u8 *)(void *)tpDL_GUITable + GUI_GET_RTC6_OFFSET
    );
    u8 second = 0u;
    u8 minute = 0u;
    u8 hour = 0u;
    u16 day = 0u;
    u16 month = 0u;
    u16 year = 0u;

    if (!read_clock)
        return 0;
    read_clock(&second, &minute, &hour, &day, &month, &year);
    if (hour > 23u || minute > 59u || second > 59u ||
        year < 1900u || month == 0u || month > 12u ||
        day == 0u || day > 31u)
        return 0;
    marker->day = date_day_number(year, month, day);
    marker->time_ms = ((u32)hour * 3600u + (u32)minute * 60u +
        (u32)second) * 1000u;
    return marker->day != 0u;
}

static int elapsed_rtc_ms(
    const T_RtcMarker *start, const T_RtcMarker *end, u32 *elapsed
)
{
    u32 day_delta = (u16)((u16)end->day - (u16)start->day);

    if (day_delta == 0u && end->time_ms >= start->time_ms) {
        *elapsed = end->time_ms - start->time_ms;
        return 1;
    }
    if (day_delta == 1u) {
        *elapsed = 86400000u - start->time_ms + end->time_ms;
        return 1;
    }
    return 0;
}

static int sync_to_rtc_second(T_RtcMarker *start)
{
    T_RtcMarker first;
    T_RtcMarker current;

    if (!read_rtc(&first))
        return 0;
    do {
        if (!read_rtc(&current))
            return 0;
    } while (current.day == first.day && current.time_ms == first.time_ms);
    *start = current;
    return 1;
}

static void clear_frame(u8 *frame)
{
    u32 index;

    for (index = 0u; index < BENCH_SCREEN_BYTES; ++index)
        frame[index] = 0xffu;
}

static void black_pixel(u8 *frame, u32 x, u32 y)
{
    u8 *pixel;
    u32 shift;

    if (x >= BENCH_SCREEN_WIDTH || y >= BENCH_SCREEN_HEIGHT)
        return;
    pixel = &frame[y * BENCH_SCREEN_PITCH + (x >> 2)];
    shift = 6u - ((x & 3u) << 1);
    *pixel = (u8)(*pixel & (u8)~(3u << shift));
}

static u8 glyph_row(char character, u32 row)
{
    static const u8 slash[7] = {
        0x01,0x02,0x02,0x04,0x08,0x08,0x10
    };
    static const u8 colon[7] = {
        0x00,0x04,0x04,0x00,0x04,0x04,0x00
    };
    static const u8 dot[7] = {
        0x00,0x00,0x00,0x00,0x00,0x06,0x06
    };
    static const u8 minus[7] = {
        0x00,0x00,0x00,0x1f,0x00,0x00,0x00
    };

    if (row >= 7u)
        return 0u;
    if (character >= '0' && character <= '9')
        return k_font[character - '0'][row];
    if (character >= 'A' && character <= 'Z')
        return k_font[10u + character - 'A'][row];
    if (character == '/')
        return slash[row];
    if (character == ':')
        return colon[row];
    if (character == '.')
        return dot[row];
    if (character == '-')
        return minus[row];
    return 0u;
}

static void draw_text(u8 *frame, u32 x, u32 y, const char *text)
{
    while (*text) {
        u32 row;

        for (row = 0u; row < 7u; ++row) {
            u8 bits = glyph_row(*text, row);
            u32 column;

            for (column = 0u; column < 5u; ++column) {
                u32 px;
                u32 py;

                if (!(bits & (u8)(1u << (4u - column))))
                    continue;
                px = x + column * 2u;
                py = y + row * 2u;
                black_pixel(frame, px, py);
                black_pixel(frame, px + 1u, py);
                black_pixel(frame, px, py + 1u);
                black_pixel(frame, px + 1u, py + 1u);
            }
        }
        x += 12u;
        ++text;
    }
}

static void black_rect(
    u8 *frame, u32 left, u32 top, u32 right, u32 bottom
)
{
    u32 y;

    for (y = top; y < bottom; ++y) {
        u32 x;

        for (x = left; x < right; ++x)
            black_pixel(frame, x, y);
    }
}

static void make_test_frames(void)
{
    clear_frame(g_frame_a);
    clear_frame(g_frame_b);
    draw_text(g_frame_a, 44u, 20u, "FRAME BENCH RUNNING");
    draw_text(g_frame_b, 44u, 20u, "FRAME BENCH RUNNING");
    black_rect(g_frame_a, 16u, 70u, 80u, 220u);
    black_rect(g_frame_b, 240u, 70u, 304u, 220u);
}

static void direct_copy(const u8 *frame)
{
    volatile u32 *video = (volatile u32 *)(unsigned long)VIDEO_BASE;
    const u32 *source = (const u32 *)(const void *)frame;
    u32 word;

    for (word = 0u; word < BENCH_SCREEN_BYTES / sizeof(u32); ++word)
        video[word] = source[word];
}

static u32 rate_millifps(u32 frames, u32 elapsed_ms)
{
    u32 elapsed_seconds = elapsed_ms / 1000u;

    if (!elapsed_seconds)
        return 0u;
    return frames * 1000u / elapsed_seconds;
}

static char *append_rate(char *out, u32 millifps)
{
    out = append_u32(out, millifps / 1000u);
    *out++ = '.';
    *out++ = (char)('0' + (millifps % 1000u) / 100u);
    return out;
}

static void draw_metric_line(
    u32 y, const char *label, u32 value, int rate
)
{
    char line[40];
    char *out = append_text(line, label);

    if (rate)
        out = append_rate(out, value);
    else
        out = append_u32(out, value);
    *out = 0;
    draw_text(g_result_frame, 8u, y, line);
}

static void save_report(void)
{
    FS_FILE *file = fs_fopen("a:\\FRAMEBEN.LOG", FS_O_WRONLY);

    if (!file)
        return;
    (void)fs_fwrite(g_report, 1, (size_t)text_length(g_report), file);
    fs_fclose(file);
}

static void build_result(void)
{
    u32 timer_sdk_submit_rate = rate_millifps(
        g_timer_sdk_submissions, g_timer_sdk_elapsed_ms
    );
    u32 timer_sdk_paint_rate = rate_millifps(
        g_timer_sdk_paints, g_timer_sdk_elapsed_ms
    );
    u32 loop_sdk_paint_rate = rate_millifps(
        g_loop_sdk_paints, g_loop_sdk_elapsed_ms
    );
    u32 direct_rate = rate_millifps(g_direct_frames, g_direct_elapsed_ms);
    u32 ratio_x10 = loop_sdk_paint_rate ?
        direct_rate * 10u / loop_sdk_paint_rate : 0u;
    char ratio[40];
    char *out;

    clear_frame(g_result_frame);
    draw_text(g_result_frame, 62u, 4u, "FRAME PATH BENCH");
    draw_text(g_result_frame, 50u, 22u, "EACH TEST 10 SEC");
    draw_text(g_result_frame, 8u, 44u, "TIMER PLUS SDK BLT");
    draw_metric_line(62u, "MSG ", g_timer_sdk_messages, 0);
    draw_metric_line(80u, "PAINT FPS ", timer_sdk_paint_rate, 1);
    draw_text(g_result_frame, 8u, 102u, "LOOP PLUS SDK BLT");
    draw_metric_line(120u, "PAINT FPS ", loop_sdk_paint_rate, 1);
    draw_text(g_result_frame, 8u, 142u, "LOOP PLUS DIRECT VRAM");
    draw_metric_line(160u, "DIRECT FPS ", direct_rate, 1);
    out = append_text(ratio, "DIRECT VS SDK X ");
    out = append_u32(out, ratio_x10 / 10u);
    *out++ = '.';
    *out++ = (char)('0' + ratio_x10 % 10u);
    *out = 0;
    draw_text(g_result_frame, 8u, 178u, ratio);
    draw_text(g_result_frame, 8u, 202u, "LOG A FRAMEBEN LOG");
    draw_text(g_result_frame, 8u, 222u, "PRESS EXIT OR ESC");

    out = g_report;
    out = append_text(out, "[9288 FRAME PATH BENCH 2]\n");
    out = append_text(out, "target_seconds=");
    out = append_u32(out, TEST_SECONDS);
    out = append_text(out, "\ntimer_sdk_elapsed_ms=");
    out = append_u32(out, g_timer_sdk_elapsed_ms);
    out = append_text(out, "\ntimer_sdk_messages=");
    out = append_u32(out, g_timer_sdk_messages);
    out = append_text(out, "\ntimer_sdk_submissions=");
    out = append_u32(out, g_timer_sdk_submissions);
    out = append_text(out, "\ntimer_sdk_paints=");
    out = append_u32(out, g_timer_sdk_paints);
    out = append_text(out, "\ntimer_sdk_submit_millifps=");
    out = append_u32(out, timer_sdk_submit_rate);
    out = append_text(out, "\ntimer_sdk_paint_millifps=");
    out = append_u32(out, timer_sdk_paint_rate);
    out = append_text(out, "\nloop_sdk_elapsed_ms=");
    out = append_u32(out, g_loop_sdk_elapsed_ms);
    out = append_text(out, "\nloop_sdk_submissions=");
    out = append_u32(out, g_loop_sdk_submissions);
    out = append_text(out, "\nloop_sdk_paints=");
    out = append_u32(out, g_loop_sdk_paints);
    out = append_text(out, "\nloop_sdk_submit_millifps=");
    out = append_u32(out, rate_millifps(
        g_loop_sdk_submissions, g_loop_sdk_elapsed_ms
    ));
    out = append_text(out, "\nloop_sdk_paint_millifps=");
    out = append_u32(out, loop_sdk_paint_rate);
    out = append_text(out, "\ndirect_elapsed_ms=");
    out = append_u32(out, g_direct_elapsed_ms);
    out = append_text(out, "\ndirect_frames=");
    out = append_u32(out, g_direct_frames);
    out = append_text(out, "\ndirect_millifps=");
    out = append_u32(out, direct_rate);
    out = append_text(out, "\ndirect_vs_loop_sdk_paint_x10=");
    out = append_u32(out, ratio_x10);
    out = append_text(out, "\n[END]\n");
    *out = 0;
    save_report();
}

static void show_error(const char *message)
{
    clear_frame(g_result_frame);
    draw_text(g_result_frame, 8u, 72u, "BENCHMARK ERROR");
    draw_text(g_result_frame, 8u, 102u, message);
    draw_text(g_result_frame, 8u, 144u, "PRESS EXIT OR ESC");
    g_mode = MODE_ERROR;
    direct_copy(g_result_frame);
}

static void run_direct_benchmark(void)
{
    T_RtcMarker start;
    T_RtcMarker current;
    u32 elapsed = 0u;
    u32 poll_countdown = 1u;

    g_mode = MODE_DIRECT;
    g_direct_frames = 0u;
    direct_copy(g_frame_a);
    if (!sync_to_rtc_second(&start)) {
        show_error("RTC SYNC FAILED");
        return;
    }
    while (elapsed < TEST_SECONDS * 1000u) {
        const u8 *frame = (g_direct_frames & 1u) ? g_frame_a : g_frame_b;

        direct_copy(frame);
        ++g_direct_frames;
        if (--poll_countdown != 0u)
            continue;
        poll_countdown = DIRECT_RTC_POLL_FRAMES;
        if (!read_rtc(&current) ||
            !elapsed_rtc_ms(&start, &current, &elapsed)) {
            show_error("RTC READ FAILED");
            return;
        }
    }
    g_direct_elapsed_ms = elapsed;
    build_result();
    g_mode = MODE_RESULT;
    /* The final SDK paint from phase one can still own the LCD HSDMA surface
     * after the direct loop finishes.  Put the result into both surfaces and
     * request one final paint; the MODE_RESULT paint path writes VRAM again
     * after the firmware has consumed the invalid region. */
    g_sys_blt_frame(g_hdc, g_result_frame);
    (void)fnGUI_InvalidateRect(
        g_window, (const T_GUI_Rect *)0, (T_BOOL)0
    );
    direct_copy(g_result_frame);
}

static void submit_sdk_frame(void)
{
    u8 *frame = g_frame_toggle ? g_frame_a : g_frame_b;

    g_frame_toggle ^= 1u;
    g_sys_blt_frame(g_hdc, frame);
    if (g_mode == MODE_TIMER_SDK)
        ++g_timer_sdk_submissions;
    else if (g_mode == MODE_LOOP_SDK)
        ++g_loop_sdk_submissions;
    (void)fnGUI_InvalidateRect(
        g_window, (const T_GUI_Rect *)0, (T_BOOL)0
    );
}

static int start_loop_sdk_benchmark(void)
{
    g_loop_sdk_submissions = 0u;
    g_loop_sdk_paints = 0u;
    direct_copy(g_frame_a);
    if (!sync_to_rtc_second(&g_loop_sdk_start)) {
        show_error("RTC SYNC FAILED");
        return 0;
    }
    g_mode = MODE_LOOP_SDK;
    submit_sdk_frame();
    return 1;
}

static T_WORD benchmark_window_proc(
    T_GUI_HWND window, T_WORD message,
    T_GUI_WPARAM wparam, T_GUI_LPARAM lparam
)
{
    T_WORD result;
    T_UHWORD scancode = LOUHWORD(wparam);

    (void)lparam;
    switch (message) {
    case MSG_KEYDOWN:
        if (scancode == SCANCODE_ESCAPE || scancode == SCANCODE_F12)
            (void)fnGUI_PostMessage(window, MSG_CLOSE, 0, 0);
        return 0;
    case MSG_KEYUP:
        if (scancode == SCANCODE_ESCAPE || scancode == SCANCODE_F12)
            (void)fnGUI_PostMessage(window, MSG_CLOSE, 0, 0);
        return 0;
    case MSG_TIMER:
        if (g_mode == MODE_TIMER_SDK) {
            T_RtcMarker current;
            u32 elapsed = 0u;

            ++g_timer_sdk_messages;
            submit_sdk_frame();
            if (!read_rtc(&current) ||
                !elapsed_rtc_ms(&g_timer_sdk_start, &current, &elapsed)) {
                (void)fnGUI_KillTimer(window, TEST_TIMER_ID);
                show_error("RTC READ FAILED");
            } else if (elapsed >= TEST_SECONDS * 1000u) {
                g_timer_sdk_elapsed_ms = elapsed;
                (void)fnGUI_KillTimer(window, TEST_TIMER_ID);
                g_mode = MODE_WAIT_LOOP_SDK;
            }
        }
        return 0;
    case MSG_ERASEBKGND:
        return 0;
    case MSG_PAINT:
        if (g_mode == MODE_RESULT) {
            result = fnGUI_DefaultMainWinProc(
                window, message, wparam, lparam
            );
            direct_copy(g_result_frame);
            return result;
        }
        if (g_mode == MODE_ERROR) {
            direct_copy(g_result_frame);
            return 0;
        }
        result = fnGUI_DefaultMainWinProc(
            window, message, wparam, lparam
        );
        if (g_mode == MODE_TIMER_SDK || g_mode == MODE_WAIT_LOOP_SDK)
            ++g_timer_sdk_paints;
        if (g_mode == MODE_WAIT_LOOP_SDK) {
            (void)start_loop_sdk_benchmark();
        } else if (g_mode == MODE_LOOP_SDK) {
            T_RtcMarker current;
            u32 elapsed = 0u;

            ++g_loop_sdk_paints;
            if (!read_rtc(&current) || !elapsed_rtc_ms(
                    &g_loop_sdk_start, &current, &elapsed)) {
                show_error("RTC READ FAILED");
            } else if (elapsed >= TEST_SECONDS * 1000u) {
                g_loop_sdk_elapsed_ms = elapsed;
                g_mode = MODE_WAIT_DIRECT;
                run_direct_benchmark();
            } else {
                submit_sdk_frame();
            }
        }
        return result;
    case MSG_CLOSE:
        g_close_requested = 1;
        (void)fnGUI_KillTimer(window, TEST_TIMER_ID);
        if (g_hdc) {
            fnGUI_ReleaseDC(g_hdc);
            g_hdc = 0;
        }
        if (g_window == window)
            g_window = 0;
        fnGUI_DestroyMainWindow(window);
        fnGUI_PostQuitMessage(window);
        return 0;
    default:
        return fnGUI_DefaultMainWinProc(window, message, wparam, lparam);
    }
}

static int create_window(void)
{
    T_GUI_MainWinCreate info;
    T_GUI_Msg message;

    info.dwStyle = WS_VISIBLE | WS_CAPTION;
    info.dwExStyle = WS_EX_NONE;
    info.spCaption = (const T_BYTE *)APP_TITLE;
    info.hMenu = 0;
    info.hIcon = 0;
    info.MainWindowProc = benchmark_window_proc;
    info.lx = 0;
    info.ty = 0;
    info.rx = BENCH_SCREEN_WIDTH;
    info.by = BENCH_SCREEN_HEIGHT;
    info.iBkColor = COLOR_LIGHTWHITE;
    info.dwAddData = 0;
    info.hHosting = HWND_DESKTOP;
    g_window = fnGUI_CreateMainWindow(&info);
    if (!g_window)
        return 0;
    (void)fnGUI_ShowWindow(g_window, SW_SHOWNORMAL);
    while (fnGUI_GetMessage(&message, g_window)) {
        fnGUI_TranslateMessage(&message);
        fnGUI_DispatchMessage(&message);
        if (message.message == MSG_PAINT)
            break;
    }
    (void)fnGUI_SetActiveWindow(g_window);
    (void)fnGUI_SetFocus(g_window);
    g_hdc = fnGUI_GetClientDC(g_window);
    return g_hdc != 0;
}

T_WORD App_Main(void)
{
    T_GUI_Msg message;
    T_GUI_HWND cleanup_window;

    make_test_frames();
    g_mode = MODE_STARTING;
    if (!create_window()) {
        (void)fnGUI_MessageBox(
            HWND_DESKTOP, (const T_BYTE *)"Create window failed.",
            (const T_BYTE *)APP_TITLE, MB_OK
        );
        return -1;
    }
    cleanup_window = g_window;
    g_sys_blt_frame = *(T_SysBltFrame *)(void *)(
        (u8 *)(void *)tpDL_GUITable + GUI_SYS_BLT_FRAME_OFFSET
    );
    if (!g_sys_blt_frame) {
        show_error("SYS BLT NOT FOUND");
    } else {
        direct_copy(g_frame_a);
        if (!sync_to_rtc_second(&g_timer_sdk_start)) {
            show_error("RTC SYNC FAILED");
        } else {
            g_mode = MODE_TIMER_SDK;
            if (!fnGUI_SetTimer(
                    g_window, TEST_TIMER_ID, TEST_TIMER_SPEED))
                show_error("SET TIMER FAILED");
        }
    }

    while (!g_close_requested && fnGUI_GetMessage(&message, 0)) {
        fnGUI_TranslateMessage(&message);
        fnGUI_DispatchMessage(&message);
    }
    if (g_window)
        (void)benchmark_window_proc(g_window, MSG_CLOSE, 0, 0);
    fnGUI_ThrowAwayMessages(cleanup_window);
    fnGUI_MainWindowCleanup(cleanup_window);
    return 0;
}
