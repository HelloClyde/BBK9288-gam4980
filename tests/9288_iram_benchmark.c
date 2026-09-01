#include "Dsys.h"
#include "gam4980_types.h"
#include "9288_iram_benchmark_payload.h"

#define APP_TITLE "IRAM BENCH"
#define GUI_GET_RTC6_OFFSET 0x590u
#define IRAM_TARGET_ADDRESS 0x00000800u
#define AMR_RESIDENT_FLAG_ADDRESS 0x00003fcau
#define TTBR_REGISTER_ADDRESS 0x00048134u
#define EXEC_BUFFER_SIZE 128u
#ifndef IRAM_BENCH_BATCHES
#define IRAM_BENCH_BATCHES 16384u
#endif
#ifndef IRAM_BENCH_ITERATIONS_PER_BATCH
#define IRAM_BENCH_ITERATIONS_PER_BATCH 1024u
#endif
#define BENCH_BATCHES IRAM_BENCH_BATCHES
#define BENCH_ITERATIONS_PER_BATCH IRAM_BENCH_ITERATIONS_PER_BATCH
#define PSR_IE_MASK 0x00000010u
#define BENCH_STORAGE __attribute__((aligned(16), section(".scratch")))

typedef u32 (*T_BenchmarkPayload)(u32 seed, u32 iterations);
typedef void (*T_Rtc6)(
    u8 *second, u8 *minute, u8 *hour,
    u16 *day, u16 *month, u16 *year
);

typedef struct T_RtcMarker {
    u32 day;
    u32 time_ms;
} T_RtcMarker;

typedef struct T_StageResult {
    u32 seed;
    u32 elapsed_ms;
    u32 ticks;
    int rtc_valid;
} T_StageResult;

static u8 g_external_exec[EXEC_BUFFER_SIZE] BENCH_STORAGE;
static u8 g_external_backup[EXEC_BUFFER_SIZE] BENCH_STORAGE;
static u8 g_iram_backup[EXEC_BUFFER_SIZE] BENCH_STORAGE;
static u8 g_iram_after[EXEC_BUFFER_SIZE] BENCH_STORAGE;
static char g_report[1024];
static char g_summary[512];

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

static char *append_hex32(char *out, u32 value)
{
    static const char digits[] = "0123456789ABCDEF";
    int shift = 28;

    while (shift >= 0) {
        *out++ = digits[(value >> (u32)shift) & 0x0fu];
        shift -= 4;
    }
    return out;
}

static u32 text_length(const char *text)
{
    u32 length = 0u;

    while (text[length])
        ++length;
    return length;
}

static u32 read_psr(void)
{
    u32 value;

    __asm__ volatile("ld.w %0,%%psr" : "=r"(value));
    return value;
}

static void write_psr(u32 value)
{
    __asm__ volatile("ld.w %%psr,%0" : : "r"(value) : "memory");
}

static u32 read_ttbr(void)
{
    return *(volatile u32 *)(unsigned long)TTBR_REGISTER_ADDRESS;
}

static u32 read_sp(void)
{
    u32 value;

    __asm__ volatile("ld.w %0,%%sp" : "=r"(value));
    return value;
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

static void copy_bytes(volatile u8 *destination, const u8 *source, u32 size)
{
    u32 index;

    for (index = 0u; index < size; ++index)
        destination[index] = source[index];
    __asm__ volatile("" : : : "memory");
}

static void snapshot_bytes(u8 *destination, const volatile u8 *source, u32 size)
{
    u32 index;

    for (index = 0u; index < size; ++index)
        destination[index] = source[index];
}

static int bytes_equal(
    const volatile u8 *left, const u8 *right, u32 size
)
{
    u32 index;

    for (index = 0u; index < size; ++index) {
        if (left[index] != right[index])
            return 0;
    }
    return 1;
}

static u32 hash_bytes(const u8 *bytes, u32 size)
{
    u32 hash = 2166136261u;
    u32 index;

    for (index = 0u; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 16777619u;
    }
    return hash;
}

static u32 run_atomic_batch(
    volatile u8 *target, const u8 *backup, u32 seed
)
{
    T_BenchmarkPayload function = (T_BenchmarkPayload)(void *)target;
    u32 saved_psr = read_psr();
    u32 result;

    write_psr(saved_psr & ~PSR_IE_MASK);
    copy_bytes(target, g_iram_benchmark_payload, IRAM_BENCHMARK_PAYLOAD_SIZE);
    result = function(seed, BENCH_ITERATIONS_PER_BATCH);
    copy_bytes(target, backup, IRAM_BENCHMARK_PAYLOAD_SIZE);
    write_psr(saved_psr);
    return result;
}

static int run_stage(
    volatile u8 *target, const u8 *backup, T_StageResult *stage
)
{
    T_RtcMarker start;
    T_RtcMarker end;
    T_UWORD start_tick;
    T_UWORD end_tick;
    u32 batch;
    u32 seed = 0x02700880u;

    if (!sync_to_rtc_second(&start))
        return 0;
    start_tick = fnGUI_GetTickCount();
    for (batch = 0u; batch < BENCH_BATCHES; ++batch)
        seed = run_atomic_batch(target, backup, seed);
    end_tick = fnGUI_GetTickCount();
    stage->seed = seed;
    stage->ticks = (u16)(end_tick - start_tick);
    stage->rtc_valid = read_rtc(&end) &&
        elapsed_rtc_ms(&start, &end, &stage->elapsed_ms);
    return 1;
}

static int write_restore_probe(
    volatile u8 *target, const u8 *backup
)
{
    u32 saved_psr = read_psr();
    int wrote;
    int restored;

    write_psr(saved_psr & ~PSR_IE_MASK);
    copy_bytes(target, g_iram_benchmark_payload, IRAM_BENCHMARK_PAYLOAD_SIZE);
    wrote = bytes_equal(
        target, g_iram_benchmark_payload, IRAM_BENCHMARK_PAYLOAD_SIZE
    );
    copy_bytes(target, backup, IRAM_BENCHMARK_PAYLOAD_SIZE);
    restored = bytes_equal(target, backup, IRAM_BENCHMARK_PAYLOAD_SIZE);
    write_psr(saved_psr);
    return wrote && restored;
}

static void write_report(const char *text)
{
    FS_FILE *file = fs_fopen("a:\\IRAMBEN.LOG", FS_O_WRONLY);

    if (!file)
        return;
    (void)fs_fwrite(text, 1, (size_t)text_length(text), file);
    (void)fs_update(file);
    fs_fclose(file);
}

static void build_report(
    const T_StageResult *external_stage,
    const T_StageResult *iram_stage,
    u32 psr_before,
    u32 psr_after,
    u32 ttbr_before,
    u32 ttbr_after,
    u32 sp_before,
    u32 sp_after,
    u8 amr_before,
    u8 amr_after,
    u32 iram_hash_before,
    u32 iram_hash_after,
    int write_restore_ok,
    int restored,
    int state_ok
)
{
    u32 rtc_speed_x100 = iram_stage->rtc_valid && iram_stage->elapsed_ms ?
        external_stage->elapsed_ms * 100u / iram_stage->elapsed_ms : 0u;
    u32 tick_speed_x100 = iram_stage->ticks ?
        external_stage->ticks * 100u / iram_stage->ticks : 0u;
    char *out = g_report;

    out = append_text(out, "[9288 IRAM BENCH 1]\niram_address=");
    out = append_hex32(out, IRAM_TARGET_ADDRESS);
    out = append_text(out, "\npayload_size=");
    out = append_u32(out, IRAM_BENCHMARK_PAYLOAD_SIZE);
    out = append_text(out, "\nbatches=");
    out = append_u32(out, BENCH_BATCHES);
    out = append_text(out, "\niterations_per_batch=");
    out = append_u32(out, BENCH_ITERATIONS_PER_BATCH);
    out = append_text(out, "\ntotal_kernel_iterations=");
    out = append_u32(out, BENCH_BATCHES * BENCH_ITERATIONS_PER_BATCH);
    out = append_text(out, "\nexternal_address=");
    out = append_hex32(out, (u32)(void *)g_external_exec);
    out = append_text(out, "\nexternal_ticks=");
    out = append_u32(out, external_stage->ticks);
    out = append_text(out, "\nexternal_rtc_elapsed_ms=");
    out = append_u32(out, external_stage->elapsed_ms);
    out = append_text(out, "\nexternal_rtc_valid=");
    *out++ = external_stage->rtc_valid ? '1' : '0';
    out = append_text(out, "\nexternal_result=");
    out = append_hex32(out, external_stage->seed);
    out = append_text(out, "\niram_ticks=");
    out = append_u32(out, iram_stage->ticks);
    out = append_text(out, "\niram_rtc_elapsed_ms=");
    out = append_u32(out, iram_stage->elapsed_ms);
    out = append_text(out, "\niram_rtc_valid=");
    *out++ = iram_stage->rtc_valid ? '1' : '0';
    out = append_text(out, "\niram_result=");
    out = append_hex32(out, iram_stage->seed);
    out = append_text(out, "\nrtc_speed_x100=");
    out = append_u32(out, rtc_speed_x100);
    out = append_text(out, "\ntick_speed_x100=");
    out = append_u32(out, tick_speed_x100);
    out = append_text(out, "\npsr_before=");
    out = append_hex32(out, psr_before);
    out = append_text(out, "\npsr_after=");
    out = append_hex32(out, psr_after);
    out = append_text(out, "\nttbr_before=");
    out = append_hex32(out, ttbr_before);
    out = append_text(out, "\nttbr_after=");
    out = append_hex32(out, ttbr_after);
    out = append_text(out, "\nsp_before=");
    out = append_hex32(out, sp_before);
    out = append_text(out, "\nsp_after=");
    out = append_hex32(out, sp_after);
    out = append_text(out, "\namr_before=");
    out = append_u32(out, amr_before);
    out = append_text(out, "\namr_after=");
    out = append_u32(out, amr_after);
    out = append_text(out, "\niram_hash_before=");
    out = append_hex32(out, iram_hash_before);
    out = append_text(out, "\niram_hash_after=");
    out = append_hex32(out, iram_hash_after);
    out = append_text(out, "\nwrite_restore_ok=");
    *out++ = write_restore_ok ? '1' : '0';
    out = append_text(out, "\niram_restored=");
    *out++ = restored ? '1' : '0';
    out = append_text(out, "\nstate_restored=");
    *out++ = state_ok ? '1' : '0';
    out = append_text(out, "\nresult=");
    out = append_text(out,
        write_restore_ok && restored && state_ok &&
        external_stage->seed == iram_stage->seed ? "PASS" : "FAIL"
    );
    out = append_text(out, "\n[END]\n");
    *out = 0;
}

static void show_abort(const char *reason)
{
    char *out = g_report;

    out = append_text(out, "[9288 IRAM BENCH 1]\nresult=ABORT\nreason=");
    out = append_text(out, reason);
    out = append_text(out, "\n[END]\n");
    *out = 0;
    write_report(g_report);
    (void)fnGUI_MessageBox(
        HWND_DESKTOP, (const T_BYTE *)reason,
        (const T_BYTE *)APP_TITLE, MB_OK
    );
}

T_WORD App_Main(void)
{
    volatile u8 *iram = (volatile u8 *)(unsigned long)IRAM_TARGET_ADDRESS;
    volatile u8 *amr_flag =
        (volatile u8 *)(unsigned long)AMR_RESIDENT_FLAG_ADDRESS;
    T_StageResult external_stage = {0u, 0u, 0u, 0};
    T_StageResult iram_stage = {0u, 0u, 0u, 0};
    u32 psr_before;
    u32 psr_after;
    u32 ttbr_before;
    u32 ttbr_after;
    u32 sp_before;
    u32 sp_after;
    u32 iram_hash_before;
    u32 iram_hash_after;
    u8 amr_before;
    u8 amr_after;
    int write_restore_ok;
    int restored;
    int state_ok;
    char *out;

    if (IRAM_BENCHMARK_PAYLOAD_SIZE == 0u ||
        IRAM_BENCHMARK_PAYLOAD_SIZE > EXEC_BUFFER_SIZE) {
        show_abort("Generated payload size is invalid.");
        return -1;
    }
    psr_before = read_psr();
    ttbr_before = read_ttbr();
    sp_before = read_sp();
    amr_before = *amr_flag;
    if (amr_before != 0u) {
        show_abort("AMR is resident. IRAM test was not started.");
        return -2;
    }
    if (sp_before >= IRAM_TARGET_ADDRESS - 0x100u &&
        sp_before < IRAM_TARGET_ADDRESS + EXEC_BUFFER_SIZE + 0x100u) {
        show_abort("Stack is too close to the IRAM test range.");
        return -3;
    }

    snapshot_bytes(
        g_iram_backup, iram, IRAM_BENCHMARK_PAYLOAD_SIZE
    );
    snapshot_bytes(
        g_external_backup, g_external_exec, IRAM_BENCHMARK_PAYLOAD_SIZE
    );
    iram_hash_before = hash_bytes(
        g_iram_backup, IRAM_BENCHMARK_PAYLOAD_SIZE
    );
    out = g_summary;
    out = append_text(out,
        "Risky IRAM test. Audio must be stopped.\n"
        "Each interrupt-off window is one short batch.\n"
        "IRAM is restored before interrupts resume.\n\n"
        "IRAM=00000800 size="
    );
    out = append_u32(out, IRAM_BENCHMARK_PAYLOAD_SIZE);
    out = append_text(out, "\nSP=");
    out = append_hex32(out, sp_before);
    out = append_text(out, " TTBR=");
    out = append_hex32(out, ttbr_before);
    out = append_text(out, "\nAMR=0\n\nPress OK to run or EXIT to cancel.");
    *out = 0;
    if (fnGUI_MessageBox(
            HWND_DESKTOP, (const T_BYTE *)g_summary,
            (const T_BYTE *)APP_TITLE, MB_OKCANCEL) != IDOK) {
        show_abort("Cancelled before writing IRAM.");
        return 0;
    }

    write_restore_ok = write_restore_probe(iram, g_iram_backup);
    if (!write_restore_ok) {
        show_abort("IRAM write/restore preflight failed.");
        return -4;
    }
    (void)fnGUI_MessageBox(
        HWND_DESKTOP,
        (const T_BYTE *)
            "Preflight passed. Running external RAM stage.\n"
            "The screen may pause for several seconds.",
        (const T_BYTE *)APP_TITLE, MB_OK
    );
    if (!run_stage(g_external_exec, g_external_backup, &external_stage)) {
        show_abort("External RAM timing failed.");
        return -5;
    }
    (void)fnGUI_MessageBox(
        HWND_DESKTOP,
        (const T_BYTE *)
            "External stage passed. Running IRAM stage.\n"
            "Do not press keys until the result appears.",
        (const T_BYTE *)APP_TITLE, MB_OK
    );
    if (!run_stage(iram, g_iram_backup, &iram_stage)) {
        show_abort("IRAM timing failed.");
        return -6;
    }

    snapshot_bytes(
        g_external_exec, g_external_backup, IRAM_BENCHMARK_PAYLOAD_SIZE
    );
    restored = bytes_equal(
        iram, g_iram_backup, IRAM_BENCHMARK_PAYLOAD_SIZE
    );
    snapshot_bytes(
        g_iram_after, iram, IRAM_BENCHMARK_PAYLOAD_SIZE
    );
    iram_hash_after = hash_bytes(
        g_iram_after, IRAM_BENCHMARK_PAYLOAD_SIZE
    );
    restored = restored && iram_hash_before == iram_hash_after;
    psr_after = read_psr();
    ttbr_after = read_ttbr();
    sp_after = read_sp();
    amr_after = *amr_flag;
    state_ok = (psr_before & 0x00000710u) ==
            (psr_after & 0x00000710u) &&
        ttbr_before == ttbr_after && amr_before == amr_after;
    build_report(
        &external_stage, &iram_stage,
        psr_before, psr_after, ttbr_before, ttbr_after,
        sp_before, sp_after, amr_before, amr_after,
        iram_hash_before, iram_hash_after,
        write_restore_ok, restored, state_ok
    );
    write_report(g_report);

    out = g_summary;
    out = append_text(out, "External RTC ms: ");
    out = append_u32(out, external_stage.elapsed_ms);
    out = append_text(out, "\nIRAM RTC ms: ");
    out = append_u32(out, iram_stage.elapsed_ms);
    out = append_text(out, "\nExternal ticks: ");
    out = append_u32(out, external_stage.ticks);
    out = append_text(out, "\nIRAM ticks: ");
    out = append_u32(out, iram_stage.ticks);
    out = append_text(out, "\nSame result: ");
    *out++ = external_stage.seed == iram_stage.seed ? '1' : '0';
    out = append_text(out, "\nIRAM restored: ");
    *out++ = restored ? '1' : '0';
    out = append_text(out, "\nState restored: ");
    *out++ = state_ok ? '1' : '0';
    out = append_text(out, "\n\nFull log: A:\\IRAMBEN.LOG");
    *out = 0;
    (void)fnGUI_MessageBox(
        HWND_DESKTOP, (const T_BYTE *)g_summary,
        (const T_BYTE *)APP_TITLE, MB_OK
    );
    return restored && state_ok &&
        external_stage.seed == iram_stage.seed ? 0 : -7;
}
